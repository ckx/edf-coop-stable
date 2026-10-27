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
#include "netplayer.h"
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
using SendFn = void (*)(void*, void*, bool);
SendFn originalSend = nullptr;
ThunkPage sendThunks;
constexpr std::uint32_t kSendSites[] = {0x22F21B, 0x22F28C, 0x22F309, 0x2A5764};
std::uintptr_t image = 0;
SRWLOCK lock = SRWLOCK_INIT;
std::uint64_t windowStart = 0, updates = 0, firstUpdates = 0, receives = 0, sends = 0, unreadable = 0, suppressed = 0;
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
// Decode a private Serialize copy. The live reader is never advanced or rewound by diagnostics.
struct Message {
    alignas(16) std::array<std::uint8_t, 0x600> stream{};
    bool captured = false, valid = false;
    int type = -1;
    std::int64_t reference = -1, part = -1, event = -1;
    float first[3]{}, second[3]{};
    std::uint64_t hash = 0;
    std::size_t bytes = 0;
};

void CaptureMessage(void* stream, bool sending, Message& m) {
    if (!stream) return;
    Guarded([&] {
        const auto address = reinterpret_cast<std::uintptr_t>(stream);
        const auto end = Field<std::uint64_t>(address, kSerializeEnd);
        const auto at = sending ? 0 : Field<std::uint64_t>(address, kSerializeReadPos);
        if (end > kSerializeCapacity || at >= end) return;
        std::memcpy(m.stream.data() + kSerializeData,
                    reinterpret_cast<const void*>(address + kSerializeData + at), end - at);
        const std::uint64_t length = end - at;
        std::memcpy(m.stream.data() + kSerializeEnd, &length, sizeof(length));
        m.bytes = static_cast<std::size_t>(length);
        m.hash = 0xCBF29CE484222325ULL;
        for (std::size_t i = 0; i < m.bytes; ++i)
            m.hash = (m.hash ^ m.stream[kSerializeData + i]) * 0x100000001B3ULL;
        m.captured = true;
    });
}

bool ReadInteger(Message& m, std::int64_t& value) {
    const auto at = Field<std::uint64_t>(reinterpret_cast<std::uintptr_t>(m.stream.data()), kSerializeReadPos);
    if (at >= m.bytes) return false;
    const unsigned kind = m.stream[kSerializeData + at] >> 5;
    constexpr unsigned lengths[] = {1, 2, 3, 5, 9};
    if (kind >= std::size(lengths) || lengths[kind] > m.bytes - at) return false;
    // 12B4660 reads integer elements without bounds checks. The check above supplies them.
    value = reinterpret_cast<std::int64_t (*)(void*)>(image + 0x12B4660)(m.stream.data());
    return true;
}

void DecodeMessage(Message& m) {
    if (!m.captured) return;
    std::int64_t type = -1;
    if (!ReadInteger(m, type) || type < 0 || type > 3) return;
    m.type = static_cast<int>(type);
    if (type == 1 || type == 2) {
        // 233690/232E80 read the target reference and part BEFORE the geometry.
        if (!ReadInteger(m, m.reference) || !ReadInteger(m, m.part)) return;
        if (type == 2 && !ReadInteger(m, m.event)) return;
    }
    std::size_t size = 0;
    if (!ReadBinElement(m.stream.data(), reinterpret_cast<std::uint8_t*>(m.first), sizeof(m.first), size) ||
        size != sizeof(m.first)) return;
    if (type == 0 || type == 3) {
        if (!ReadBinElement(m.stream.data(), reinterpret_cast<std::uint8_t*>(m.second), sizeof(m.second), size) ||
            size != sizeof(m.second)) return;
    } else {
        // The remaining vector uses a six-byte bin or a two-byte header plus eight packed bytes (760C50).
        // Only its bounds matter for this trace; do not resolve the reference or call game collision code.
        const auto at = Field<std::uint64_t>(reinterpret_cast<std::uintptr_t>(m.stream.data()), kSerializeReadPos);
        if (at >= m.bytes) return;
        if ((m.stream[kSerializeData + at] & 0xE0) == 0xE0) {
            if (m.bytes - at != 10) return;
            const auto end = static_cast<std::uint64_t>(m.bytes);
            std::memcpy(m.stream.data() + kSerializeReadPos, &end, sizeof(end));
        } else {
            std::uint8_t packed[6]{};
            if (!ReadBinElement(m.stream.data(), packed, sizeof(packed), size) || size != sizeof(packed)) return;
        }
    }
    m.valid = Field<std::uint64_t>(reinterpret_cast<std::uintptr_t>(m.stream.data()), kSerializeReadPos) == m.bytes;
}

struct Snapshot {
    std::uint32_t age = 0, lifetime = 0;
    float position[3]{}, forward[3]{}, head[3]{}, tail[3]{}, length = 0, width = 0;
    std::uintptr_t owner = 0, userId = 0;
    unsigned ownerFlags = 0;
    bool ownerKnown = false;
};

void Trace(void* object, const char* event, Message* message = nullptr) {
    const bool receive = std::strcmp(event, "receive-after") == 0;
    const bool updating = std::strcmp(event, "update-before") == 0;
    const auto address = reinterpret_cast<std::uintptr_t>(object);
    std::uint32_t age = 0;
    const bool ageKnown = ReadGuarded(address + 0xF38, age);
    const auto now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    if (!windowStart) windowStart = now;
    if (now - windowStart >= 30000) {
        Log("LASER summary windowMs=%llu updates=%llu firstUpdates=%llu receiveCallbacks=%llu "
            "sendCallbacks=%llu drawCallbacks=%llu unreadable=%llu detailsSuppressed=%llu",
            now - windowStart, updates, firstUpdates, receives, sends, draws.exchange(0), unreadable, suppressed);
        windowStart = now;
        updates = firstUpdates = receives = sends = unreadable = suppressed = 0;
        details = 0;
    }
    if (receive) ++receives;
    else if (updating) ++updates;
    else ++sends;
    if (!ageKnown) ++unreadable;
    Watch* watch = nullptr;
    for (auto& entry : watched)
        if (entry.object.load(std::memory_order_relaxed) == address) { watch = &entry; break; }
    if (updating && ageKnown && age == 0) {
        ++firstUpdates;
        if (!watch) watch = &watched[nextWatch++ % watched.size()];
        watch->object.store(0, std::memory_order_relaxed);
        watch->draws.store(0, std::memory_order_relaxed);
        watch->object.store(address, std::memory_order_relaxed);
    }
    const unsigned objectDraws = watch ? watch->draws.load(std::memory_order_relaxed) : 0;
    const bool drawKnown = watch != nullptr;
    const bool sample = message || !ageKnown || age < 3;
    if (!sample || details >= 8) {
        if (sample) ++suppressed;
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    ++details;
    ReleaseSRWLockExclusive(&lock);

    Snapshot s;
    std::int32_t bulletId = -1;
    const bool readable = Guarded([&] {
        // Update 253605..25365A: age F38, lifetime B48. SyncRender 253417..2534DB:
        // segment endpoints F90/FA0, rendered length FB0 and width FB4. Draw uses the +80 forward basis.
        bulletId = Field<std::int32_t>(address, 0x128);
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
        "user=%s bulletId=%d position=%.3f,%.3f,%.3f forward=%.4f,%.4f,%.4f head=%.3f,%.3f,%.3f tail=%.3f,%.3f,%.3f "
        "renderLength=%.3f renderWidth=%.3f drawCountKnown=%d drawCallbacks=%u",
        event, object, readable ? 1 : 0, s.age, s.lifetime,
        reinterpret_cast<void*>(s.owner), s.ownerKnown ? 1 : 0, s.ownerFlags, user[0] ? user : "unknown", bulletId,
        s.position[0], s.position[1], s.position[2], s.forward[0], s.forward[1], s.forward[2],
        s.head[0], s.head[1], s.head[2], s.tail[0], s.tail[1], s.tail[2], s.length, s.width,
        drawKnown ? 1 : 0, objectDraws);
    if (message) {
        DecodeMessage(*message);
        Log("LASER message event=%s user=%s bulletId=%d nativeSequence=%u captured=%d decoded=%d type=%d "
            "bytes=%zu hash=%016llX reference=%lld part=%lld hitEvent=%lld "
            "vector1=%.3f,%.3f,%.3f vector2=%.3f,%.3f,%.3f",
            event, user[0] ? user : "unknown", bulletId, static_cast<std::uint32_t>(bulletId) >> 8, message->captured ? 1 : 0,
            message->valid ? 1 : 0, message->type, message->bytes, message->hash,
            message->reference, message->part, message->event,
            message->first[0], message->first[1], message->first[2],
            message->second[0], message->second[1], message->second[2]);
    }
}

void Update(void* object, void* context) {
    Trace(object, "update-before");
    originalUpdate(object, context);
}
void Receive(void* network, void* reader) {
    Message message;
    CaptureMessage(reader, false, message);
    originalReceive(network, reader);
    Trace(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(network) - 0x120), "receive-after", &message);
}
void Send(void* network, void* writer, bool route) {
    const auto address = reinterpret_cast<std::uintptr_t>(network) - 0x120;
    std::uintptr_t table = 0;
    if (ReadGuarded(address, table) && table == image + 0x179FCB0) {
        Message message;
        CaptureMessage(writer, true, message);
        Trace(reinterpret_cast<void*>(address), "send-before", &message);
    }
    originalSend(network, writer, route);
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
    struct Range { std::uint32_t rva, size; std::uint64_t hash; };
    constexpr Range ranges[] = {
        {0x760C50, 0x1D4, 0x756EAF638F409591ULL},
        {0x12B48C0, 0x3A, 0xE21044504E3F7A87ULL},
        {0x231D97, 0xE4, 0x19D95134721D66A0ULL},
        {0x22F0B0, 0x2DD, 0x97D4776433EC6996ULL},
        {0x2A56B0, 0x128, 0x13C7AF685D0B4177ULL},
        {0x765570, 0x162, 0xABF36D58F0508547ULL},
        {0x764470, 0x1E9, 0x1CF5193FE53C49AEULL},
        {0x12B4630, 0x14, 0x23E2230984F6AA17ULL},
        {0x7639E7, 0x20, 0x4B1159FD71388031ULL},
        {0x7650BA, 0x27, 0x527374B9E1C84174ULL},
        {0x764B10, 0x54, 0xBCB2F58FBFE45699ULL},
        {0x12B4660, 0x153, 0x9A40BADF8D844C37ULL},
        {0x22F660, 0x99, 0x6232F7D1F01F2909ULL},
        {0x233400, 0x4C, 0x4B5F4B6FD84B9A8AULL},
        {0x233590, 0x4C, 0xF16D312DD7BE689DULL},
        {0x233690, 0x347, 0x45D2CE803C1A7B31ULL},
        {0x232E80, 0x230, 0xCEEC03A1201F13E2ULL},
        {0x76FDC0, 0x89, 0x5D90F8AC2F397231ULL},
        {0x774220, 0x19, 0x4C7CA8A7ACEF23EFULL},
    };
    for (const auto& range : ranges) {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (std::size_t i = 0; i < range.size; ++i) hash = (hash ^ base[range.rva + i]) * 0x100000001B3ULL;
        if (hash == range.hash) continue;
        Log("LASER diagnostics OFF: code at EDF+%X differs; nothing changed", range.rva);
        return false;
    }
    for (const auto rva : kSendSites) {
        const auto expected = CallBytes(base + rva, base + 0x765570);
        if (expected.size() == 5 && std::memcmp(base + rva, expected.data(), expected.size()) == 0) continue;
        Log("LASER diagnostics OFF: send at EDF+%X differs; nothing changed", rva);
        return false;
    }
    if (!sendThunks.Allocate(base)) return false;
    auto* sendStub = sendThunks.Add(reinterpret_cast<void*>(&Send));
    if (!sendStub || !sendThunks.Seal()) { sendThunks.Release(); return false; }
    image = reinterpret_cast<std::uintptr_t>(base);
    originalUpdate = reinterpret_cast<UpdateFn>(base + kUpdate);
    originalReceive = reinterpret_cast<UpdateFn>(base + kReceive);
    originalDraw = reinterpret_cast<DrawFn>(base + kDraw);
    originalSend = reinterpret_cast<SendFn>(base + 0x765570);
    for (std::size_t i = 0; i < std::size(sites); ++i) {
        if (WriteCode(base + sites[i].slot, reinterpret_cast<const unsigned char*>(&sites[i].hook), 8)) continue;
        for (std::size_t j = i; j-- > 0;) {
            const auto target = image + sites[j].target;
            WriteCode(base + sites[j].slot, reinterpret_cast<const unsigned char*>(&target), 8);
        }
        sendThunks.Release();
        Log("LASER diagnostics OFF: could not install hooks");
        return false;
    }
    for (std::size_t i = 0; i < std::size(kSendSites); ++i) {
        const auto bytes = CallBytes(base + kSendSites[i], sendStub);
        if (bytes.size() == 5 && WriteCode(base + kSendSites[i], bytes.data(), bytes.size())) continue;
        for (std::size_t j = i; j-- > 0;) {
            const auto original = CallBytes(base + kSendSites[j], base + 0x765570);
            WriteCode(base + kSendSites[j], original.data(), original.size());
        }
        for (const auto& site : sites) {
            const auto original = image + site.target;
            WriteCode(base + site.slot, reinterpret_cast<const unsigned char*>(&original), sizeof(original));
        }
        // Keep the stub mapped even if a rollback write fails.
        Log("LASER diagnostics OFF: could not install send hooks");
        return false;
    }
    Log("LASER diagnostics ON: LaserBullet01 used by Closed Laser M2 and other weapons; "
        "8 detailed events per 30 seconds plus activity summaries; draw callbacks do not prove visible pixels");
    return true;
}
}  // namespace multislot
