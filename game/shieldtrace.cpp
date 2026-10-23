#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "code.h"
#include "guard.h"
#include "log.h"
#include "patches.h"

namespace multislot {
namespace {

// PENERGYSHIELD01.SGO names EfsShieldBullet01 and pale_shield01.efarc/field.esb.
// Its native Update moves the collision shape separately from the effect children.
constexpr std::uint32_t kUpdateSlot = 0x179F258;
constexpr std::uint32_t kUpdate = 0x248EC0;
// effect::EfsObject's Umbra render helper (EfsObject +0x120) vtable 0x17A84E0, slot 1: the draw callback. Umbra
// calls it only for helpers it found visible, so its count separates "never drawn" from "drawn but empty".
constexpr std::uint32_t kDrawSlot = 0x17A84E8;
constexpr std::uint32_t kDraw = 0x2EDE20;
constexpr std::uint32_t kEfsObjectVtable = 0x17A8478;
constexpr std::uint32_t kEmitterVtable = 0x19AD1C0;  // efs::Emitter, the module that owns a particle buffer
using UpdateFn = void (*)(void*, void*);
using DrawFn = void (*)(void*, void*, int, void*);
UpdateFn original = nullptr;
DrawFn originalDraw = nullptr;
std::uintptr_t imageBase = 0;
std::atomic<unsigned> samples{0};

// Visual children seen under a shield, and how often Umbra drew each.
struct Watched {
    std::atomic<std::uintptr_t> object{0};
    std::atomic<std::uint32_t> draws{0};
};
Watched watched[32];

Watched* Find(std::uintptr_t object) {
    for (auto& w : watched)
        if (w.object.load(std::memory_order_relaxed) == object) return &w;
    return nullptr;
}

void Watch(std::uintptr_t object) {
    if (Find(object)) return;
    static std::atomic<unsigned> next{0};
    auto& w = watched[next.fetch_add(1, std::memory_order_relaxed) % 32];
    w.draws.store(0, std::memory_order_relaxed);
    w.object.store(object, std::memory_order_relaxed);
}

void TraceDraw(void* helper, void* context, int pass, void* view) {
    if (auto* w = Find(reinterpret_cast<std::uintptr_t>(helper) - 0x120))
        w->draws.fetch_add(1, std::memory_order_relaxed);
    originalDraw(helper, context, pass, view);
}

template <class T>
T Field(std::uintptr_t object, std::size_t offset) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(object + offset), sizeof(value));
    return value;
}

struct Child {
    std::uintptr_t object = 0, vtable = 0, effect = 0;
    std::uint32_t sceneFlags = 0, renderFlags = 0, passMask = 0, draws = 0;
    std::int32_t strongRefs = 0, remaining = 0;
    std::uint64_t emitters = 0, pending = 0, particles = 0, instances = 0;
    float frame = 0, position[3]{}, umbraCenter[3]{}, umbraRadius = 0, emitterTime = 0, particleCenter[3]{},
          particleExtent[3]{};
    std::uint32_t emitterDuration = 0;
    bool readable = false;
};

// Reads one EfsObject. Layout from 0x2ED6E0 (constructor), 0x2EE1E0 (OnSyncRender: particle bounds to Umbra through
// 0x11B30C0, which keeps the centre at helper +0x50 and the radius at +0x64), 0x10B69F0 (emitter list at effect
// +0x30, emitter time +0x20, duration +0x28, particle system +0x70) and 0x10F4970 (live count and bounds of an
// Emitter module's buffer, instance +0x48 slot, +0x90 count, +0xE0 centre, +0xF0 half extent).
void ReadChild(Child& child) {
    child.vtable = Field<std::uintptr_t>(child.object, 0);
    if (child.vtable != imageBase + kEfsObjectVtable) return;
    child.sceneFlags = Field<std::uint32_t>(child.object, 0x18);
    child.renderFlags = Field<std::uint32_t>(child.object, 0x140);
    child.remaining = Field<std::int32_t>(child.object, 0x1D4);
    std::memcpy(child.position, reinterpret_cast<const void*>(child.object + 0x90), sizeof(child.position));
    std::memcpy(child.umbraCenter, reinterpret_cast<const void*>(child.object + 0x120 + 0x50),
                sizeof(child.umbraCenter));
    child.umbraRadius = Field<float>(child.object, 0x120 + 0x64);
    child.effect = Field<std::uintptr_t>(child.object, 0x1B0);
    if (const auto* w = Find(child.object)) child.draws = w->draws.load(std::memory_order_relaxed);
    child.readable = true;
    if (!child.effect) return;
    child.emitters = Field<std::uint64_t>(child.effect, 0x38);
    child.pending = Field<std::uint64_t>(child.effect, 0x58);
    child.frame = Field<float>(child.effect, 0xA8);
    child.passMask = Field<std::uint32_t>(child.effect, 0x128);
    const auto head = Field<std::uintptr_t>(child.effect, 0x30);
    bool first = true;
    auto node = Field<std::uintptr_t>(head, 0);
    for (int e = 0; node && node != head && e < 8; ++e, node = Field<std::uintptr_t>(node, 0)) {
        const auto emitter = Field<std::uintptr_t>(node, 0x10);
        if (!emitter) continue;
        if (e == 0) {
            child.emitterTime = Field<float>(emitter, 0x20);
            child.emitterDuration = Field<std::uint32_t>(emitter, 0x28);
        }
        const auto system = Field<std::uintptr_t>(emitter, 0x70);
        if (!system) continue;
        const auto begin = Field<std::uintptr_t>(system, 0x48), end = Field<std::uintptr_t>(system, 0x50);
        if (e == 0 && end >= begin) child.instances = (end - begin) / 0xB8;
        if (!begin || end <= begin) continue;
        const auto modules = Field<std::uintptr_t>(system, 0);
        auto m = Field<std::uintptr_t>(modules, 0);
        for (int k = 0; m && m != modules && k < 16; ++k, m = Field<std::uintptr_t>(m, 0)) {
            const auto module = Field<std::uintptr_t>(m, 0x10);
            if (!module || Field<std::uintptr_t>(module, 0) != imageBase + kEmitterVtable) continue;
            const auto slot = Field<std::uint32_t>(module, 0x20);
            const auto shared = Field<std::uintptr_t>(Field<std::uintptr_t>(begin, 0x48), slot * 0x10);
            if (!shared) continue;
            const auto live = Field<std::uint64_t>(shared, 0x90);
            child.particles += live;
            if (first && live) {
                first = false;
                std::memcpy(child.particleCenter, reinterpret_cast<const void*>(shared + 0xE0),
                            sizeof(child.particleCenter));
                std::memcpy(child.particleExtent, reinterpret_cast<const void*>(shared + 0xF0),
                            sizeof(child.particleExtent));
            }
        }
    }
}

bool Sampled(std::uint32_t age) {
    switch (age) {
    case 0: case 1: case 2: case 10: case 30: case 60: case 90: case 120: case 150: case 180: case 240: case 300:
    case 450: case 600: case 900:
        return true;
    default:
        return false;
    }
}

void TraceUpdate(void* object, void* context) {
    const auto shield = reinterpret_cast<std::uintptr_t>(object);
    std::uint32_t age = 0;
    if (samples.load(std::memory_order_relaxed) < 512 && ReadGuarded(shield + 0xEA0, age) && Sampled(age) &&
        samples.fetch_add(1, std::memory_order_relaxed) < 512) {
        Child children[8];
        std::uint64_t count = 0;
        std::uint32_t lifetime = 0;
        float position[3]{};
        const bool readable = Guarded([&] {
            lifetime = Field<std::uint32_t>(shield, 0xB48);
            std::memcpy(position, reinterpret_cast<const void*>(shield + 0x90), sizeof(position));
            const auto owner = Field<std::uintptr_t>(shield, 0xE90);
            if (!owner) return;
            count = Field<std::uint64_t>(owner, 0x30);
            const auto entries = Field<std::uintptr_t>(owner, 0x20);
            // Diagnostic snapshots only. Weak children may expire between reads; never call through them.
            for (std::uint64_t i = 0; entries && i < count && i < 8; ++i) {
                auto& child = children[i];
                const auto entry = entries + i * 0x58;
                const auto control = Field<std::uintptr_t>(entry, 8);
                if (!control) continue;
                child.strongRefs = Field<std::int32_t>(control, 8);
                if (child.strongRefs <= 0) continue;
                child.object = Field<std::uintptr_t>(entry, 0);
                if (!child.object) continue;
                Watch(child.object);
                ReadChild(child);
            }
        });
        Log("SHIELD trace object=%p age=%u lifetime=%u position=%.2f,%.2f,%.2f children=%llu readable=%d",
            object, age, lifetime, position[0], position[1], position[2],
            static_cast<unsigned long long>(count), readable ? 1 : 0);
        for (std::uint64_t i = 0; i < count && i < 8; ++i) {
            const auto& c = children[i];
            Log("SHIELD child=%llu object=%p vtable=%p refs=%d readable=%d scene=%08X render=%08X effect=%p "
                "emitters=%llu pending=%llu frame=%.2f remaining=%d pass=%X draws=%u instances=%llu "
                "particles=%llu emitterTime=%.2f/%u position=%.2f,%.2f,%.2f umbra=%.2f,%.2f,%.2f r=%.2f "
                "particleBox=%.2f,%.2f,%.2f+-%.2f,%.2f,%.2f",
                static_cast<unsigned long long>(i), reinterpret_cast<void*>(c.object),
                reinterpret_cast<void*>(c.vtable), c.strongRefs, c.readable ? 1 : 0, c.sceneFlags, c.renderFlags,
                reinterpret_cast<void*>(c.effect), static_cast<unsigned long long>(c.emitters),
                static_cast<unsigned long long>(c.pending), c.frame, c.remaining, c.passMask, c.draws,
                static_cast<unsigned long long>(c.instances), static_cast<unsigned long long>(c.particles),
                c.emitterTime, c.emitterDuration, c.position[0], c.position[1], c.position[2], c.umbraCenter[0],
                c.umbraCenter[1], c.umbraCenter[2], c.umbraRadius, c.particleCenter[0], c.particleCenter[1],
                c.particleCenter[2], c.particleExtent[0], c.particleExtent[1], c.particleExtent[2]);
        }
    }
    original(object, context);
}

}  // namespace

bool InstallShieldTrace(unsigned char* base) {
    if (!SlotTargets(base + kUpdateSlot, reinterpret_cast<std::uint64_t>(base), kUpdate) ||
        !SlotTargets(base + kDrawSlot, reinterpret_cast<std::uint64_t>(base), kDraw)) {
        Log("SHIELD trace refused: EfsShieldBullet01 Update or EfsObject draw slot differs");
        return false;
    }
    imageBase = reinterpret_cast<std::uintptr_t>(base);
    original = reinterpret_cast<UpdateFn>(base + kUpdate);
    originalDraw = reinterpret_cast<DrawFn>(base + kDraw);
    const UpdateFn hook = TraceUpdate;
    const DrawFn drawHook = TraceDraw;
    if (!WriteCode(base + kDrawSlot, reinterpret_cast<const unsigned char*>(&drawHook), sizeof(drawHook)) ||
        !WriteCode(base + kUpdateSlot, reinterpret_cast<const unsigned char*>(&hook), sizeof(hook)))
        return false;
    Log("SHIELD diagnostic build 2: native behavior unchanged; visibility snapshots limited to 512 per process");
    return true;
}

}  // namespace multislot
