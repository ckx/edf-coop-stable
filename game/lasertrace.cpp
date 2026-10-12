#include "lasertrace.h"

#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "code.h"
#include "guard.h"
#include "identity.h"
#include "log.h"
#include "patches.h"

namespace multislot {
namespace {

// Root.cpk WEAPON/PWEAPON128.SGO: name.en=Closed Laser M2, AmmoClass=LaserBullet01.
constexpr std::uint32_t kUpdateSlot = 0x179FCD8, kUpdate = 0x2535C0;
constexpr std::uint32_t kReceiveSlot = 0x179FD40, kReceive = 0x22F660;
constexpr std::uint32_t kDrawSlot = 0x179FD60, kDraw = 0x252E10;
using UpdateFn = void (*)(void*, void*);
using DrawFn = void (*)(void*, void*, int, void*);
UpdateFn originalUpdate = nullptr, originalReceive = nullptr;
DrawFn originalDraw = nullptr;
std::uintptr_t image = 0;
SRWLOCK lock = SRWLOCK_INIT;
std::uint64_t windowStart = 0, updates = 0, firstUpdates = 0, receives = 0, unreadable = 0, suppressed = 0;
unsigned details = 0, nextWatch = 0;
std::atomic<std::uint64_t> draws{0};
struct Watch {
    std::atomic<std::uintptr_t> object{0};
    std::atomic<unsigned> draws{0};
};
std::array<Watch, 64> watched;

// Reads only inside Guarded. In particular, do not acquire a game object's weak reference or allocate an id.
template <typename T>
T Field(std::uintptr_t object, std::size_t offset) {
    T result{};
    std::memcpy(&result, reinterpret_cast<const void*>(object + offset), sizeof(result));
    return result;
}
struct Snapshot {
    std::uint32_t age = 0, lifetime = 0;
    float position[3]{}, forward[3]{}, head[3]{}, tail[3]{}, length = 0, width = 0;
    std::uintptr_t owner = 0, userId = 0;
    unsigned ownerFlags = 0;
    bool ownerKnown = false;
};

void Trace(void* object, bool receive) {
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    std::uint32_t age = 0;
    const bool ageKnown = ReadGuarded(address + 0xF38, age);
    const auto now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    if (!windowStart) windowStart = now;
    if (now - windowStart >= 30000) {
        Log("LASER summary windowMs=%llu updates=%llu firstUpdates=%llu receiveCallbacks=%llu "
            "drawCallbacks=%llu unreadable=%llu detailsSuppressed=%llu",
            now - windowStart, updates, firstUpdates, receives, draws.exchange(0), unreadable, suppressed);
        windowStart = now;
        updates = firstUpdates = receives = unreadable = suppressed = 0;
        details = 0;
    }
    if (receive) ++receives;
    else ++updates;
    if (!ageKnown) ++unreadable;
    Watch* watch = nullptr;
    for (auto& entry : watched)
        if (entry.object.load(std::memory_order_relaxed) == address) { watch = &entry; break; }
    if (!receive && ageKnown && age == 0) {
        ++firstUpdates;
        if (!watch) watch = &watched[nextWatch++ % watched.size()];
        watch->object.store(0, std::memory_order_relaxed);
        watch->draws.store(0, std::memory_order_relaxed);
        watch->object.store(address, std::memory_order_relaxed);
    }
    const unsigned objectDraws = watch ? watch->draws.load(std::memory_order_relaxed) : 0;
    const bool drawKnown = watch != nullptr;
    const bool sample = receive || !ageKnown || age < 3 || age % 60 == 0;
    if (!sample || details >= 8) {
        if (sample) ++suppressed;
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    ++details;
    ReleaseSRWLockExclusive(&lock);

    Snapshot s;
    const bool readable = Guarded([&] {
        // Update 253605..25365A: age F38, lifetime B48. SyncRender 253417..2534DB:
        // segment endpoints F90/FA0, rendered length FB0 and width FB4. Draw uses the +80 forward basis.
        s.age = Field<std::uint32_t>(address, 0xF38);
        s.lifetime = Field<std::uint32_t>(address, 0xB48);
        std::memcpy(s.position, reinterpret_cast<const void*>(address + 0x90), sizeof(s.position));
        std::memcpy(s.forward, reinterpret_cast<const void*>(address + 0x80), sizeof(s.forward));
        std::memcpy(s.head, reinterpret_cast<const void*>(address + 0xF90), sizeof(s.head));
        std::memcpy(s.tail, reinterpret_cast<const void*>(address + 0xFA0), sizeof(s.tail));
        s.length = Field<float>(address, 0xFB0);
        s.width = Field<float>(address, 0xFB4);
        // NetworkBullet GetOwner at 22F3C0 reads this+9C8/9D0; this is bullet+120.
        const auto control = Field<std::uintptr_t>(address, 0xAF0);
        if (!control || Field<int>(control, 8) <= 0) return;
        s.owner = Field<std::uintptr_t>(address, 0xAE8);
        if (!s.owner) return;
        const auto table = Field<std::uintptr_t>(s.owner, 0);
        for (const auto rva : {0x17CDF28u, 0x17CF100u, 0x17CF5B8u, 0x17D24D8u, 0x17D0FF8u}) {
            if (table != image + rva) continue;
            s.ownerFlags = Field<std::uint8_t>(s.owner, 0x128);
            const auto user = Field<std::uintptr_t>(s.owner, 0x1ED0);
            if (user) s.userId = Field<std::uintptr_t>(user, 0x18);
            s.ownerKnown = true;
            break;
        }
    });
    char user[33]{};
    if (readable) ProductUserIdText(reinterpret_cast<void*>(s.userId), user, sizeof(user));
    Log("LASER sample event=%s object=%p readable=%d age=%u lifetime=%u owner=%p ownerKnown=%d ownerFlags=%u "
        "user=%s position=%.3f,%.3f,%.3f forward=%.4f,%.4f,%.4f head=%.3f,%.3f,%.3f tail=%.3f,%.3f,%.3f "
        "renderLength=%.3f renderWidth=%.3f drawCountKnown=%d drawCallbacks=%u",
        receive ? "receive-before" : "update-before", object, readable ? 1 : 0, s.age, s.lifetime,
        reinterpret_cast<void*>(s.owner), s.ownerKnown ? 1 : 0, s.ownerFlags, user[0] ? user : "unknown",
        s.position[0], s.position[1], s.position[2], s.forward[0], s.forward[1], s.forward[2],
        s.head[0], s.head[1], s.head[2], s.tail[0], s.tail[1], s.tail[2], s.length, s.width,
        drawKnown ? 1 : 0, objectDraws);
}

void Update(void* object, void* context) {
    Trace(object, false);
    originalUpdate(object, context);
}
void Receive(void* network, void* reader) {
    Trace(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(network) - 0x120), true);
    originalReceive(network, reader);
}
void Draw(void* helper, void* context, int pass, void* view) {
    if (pass == 1) {
        draws.fetch_add(1, std::memory_order_relaxed);
        const auto address = reinterpret_cast<std::uintptr_t>(helper) - 0xE90;
        for (auto& entry : watched) {
            if (entry.object.load(std::memory_order_relaxed) != address) continue;
            entry.draws.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    originalDraw(helper, context, pass, view);
}
}  // namespace

bool InstallLaserTrace(unsigned char* base) {
    struct Site { std::uint32_t slot, target; std::uintptr_t hook; };
    const Site sites[] = {
        {kUpdateSlot, kUpdate, reinterpret_cast<std::uintptr_t>(&Update)},
        {kReceiveSlot, kReceive, reinterpret_cast<std::uintptr_t>(&Receive)},
        {kDrawSlot, kDraw, reinterpret_cast<std::uintptr_t>(&Draw)},
    };
    for (const auto& site : sites) {
        if (!SlotTargets(base + site.slot, reinterpret_cast<std::uint64_t>(base), site.target)) {
            Log("LASER diagnostics OFF: EDF+%X does not target EDF+%X; nothing changed", site.slot, site.target);
            return false;
        }
    }
    struct Check { std::uint32_t rva; std::initializer_list<unsigned char> bytes; };
    const Check checks[] = {
        {0x253605, {0x8B, 0x8B, 0x38, 0x0F, 0, 0}},
        {0x25360E, {0x8B, 0x93, 0x48, 0x0B, 0, 0}},
        {0x253417, {0x0F, 0x10, 0x93, 0x90, 0x0F, 0, 0}},
        {0x253425, {0x0F, 0x5C, 0x9B, 0xA0, 0x0F, 0, 0}},
        {0x2534B2, {0xF3, 0x0F, 0x11, 0x93, 0xB0, 0x0F, 0, 0}},
        {0x2534DB, {0x89, 0x83, 0xB4, 0x0F, 0, 0}},
        {0x22F3C0, {0x4C, 0x8B, 0x81, 0xD0, 0x09, 0, 0}},
        {0x22F3D5, {0x48, 0x8B, 0x81, 0xC8, 0x09, 0, 0}},
    };
    for (const auto& check : checks) {
        if (std::memcmp(base + check.rva, check.bytes.begin(), check.bytes.size()) == 0) continue;
        Log("LASER diagnostics OFF: field access at EDF+%X differs; nothing changed", check.rva);
        return false;
    }
    image = reinterpret_cast<std::uintptr_t>(base);
    originalUpdate = reinterpret_cast<UpdateFn>(base + kUpdate);
    originalReceive = reinterpret_cast<UpdateFn>(base + kReceive);
    originalDraw = reinterpret_cast<DrawFn>(base + kDraw);
    for (std::size_t i = 0; i < std::size(sites); ++i) {
        if (WriteCode(base + sites[i].slot, reinterpret_cast<const unsigned char*>(&sites[i].hook), 8)) continue;
        for (std::size_t j = i; j-- > 0;) {
            const auto target = image + sites[j].target;
            WriteCode(base + sites[j].slot, reinterpret_cast<const unsigned char*>(&target), 8);
        }
        Log("LASER diagnostics OFF: could not install hooks");
        return false;
    }
    Log("LASER diagnostics ON: LaserBullet01 used by Closed Laser M2 and other weapons; "
        "8 snapshots per 30 seconds plus activity summaries; draw callbacks do not prove visible pixels");
    return true;
}
}  // namespace multislot
