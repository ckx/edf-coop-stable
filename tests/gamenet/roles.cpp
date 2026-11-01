// What each machine of a game-code test does once EDF.dll and EDF6Coop.dll are in (machine.cpp). Every EOS call
// goes through EDF.dll's import table, as the game makes it, so EDF6Coop's wrappers see it as they do in the game.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <functional>
#include <vector>

#include "game.h"
#include "machine.h"
#include "net_shared.h"
#include "../../src/eos_min.h"
#include "../../game/netplayer.h"

namespace gamenet {
namespace {

constexpr const char* kProtocolKey = "EDF6NET_PROTO";       // netfeature.h: kNetProtocolKey
void* const kPlatform = reinterpret_cast<void*>(0x1000);     // what the fake's EOS_Platform_Create hands out
void* const kLobbyInterface = reinterpret_cast<void*>(0x3000);

template <typename T>
T Import(const Machine& machine, const char* name) {
    return reinterpret_cast<T>(GameImport(machine, name));
}
template <typename T>
T FakeExport(const char* name) {
    return reinterpret_cast<T>(GetProcAddress(GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll"), name));
}

// One frame of the game's EOS: EOS_Platform_Tick through EDF.dll's import, where EDF6Coop watches the room.
void Tick(const Machine& machine) { Import<void (*)(void*)>(machine, "EOS_Platform_Tick")(kPlatform); }

// Ticks every ~16 ms until `done` or `timeoutMs`.
bool TickUntil(const Machine& machine, unsigned timeoutMs, const std::function<bool()>& done) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        Tick(machine);
        if (done()) return true;
        if (GetTickCount64() > deadline) return false;
        Sleep(16);
    }
}

const void* Self(const Machine& machine) { return FakeExport<const void* (*)(const char*)>("FakeNet_User")(machine.user.c_str()); }

struct LobbyCallbackInfo {
    std::int32_t ResultCode;
    void* ClientData;
    const char* LobbyId;
};
struct Entered {
    bool done = false;
    std::int32_t result = -1;
    std::string lobby;
};
void OnEntered(const LobbyCallbackInfo* info) {
    auto* entered = static_cast<Entered*>(info->ClientData);
    entered->done = true;
    entered->result = info->ResultCode;
    entered->lobby = info->LobbyId ? info->LobbyId : "";
}

// The leading fields of EOS_Lobby_CreateLobbyOptions / JoinLobbyOptions; the rest stays zero.
struct CreateOptions {
    std::int32_t ApiVersion = 8;
    const void* LocalUserId = nullptr;
    std::uint32_t MaxLobbyMembers = 0;
    std::uint8_t rest[256]{};
};
struct JoinOptions {
    std::int32_t ApiVersion = 5;
    void* LobbyDetailsHandle = nullptr;
    const void* LocalUserId = nullptr;
    std::uint8_t rest[256]{};
};
using LobbyCall = void (*)(void*, const void*, void*, void (*)(const LobbyCallbackInfo*));

// The room's members as EOS lists them, and whether each publishes its netcode protocol.
struct Seen {
    std::vector<std::string> members;
    std::vector<bool> marked;
};
Seen LookAtRoom(const Machine& machine, const std::string& lobby) {
    Seen seen;
    struct CopyOptions {
        std::int32_t ApiVersion;
        const char* LobbyId;
        const void* LocalUserId;
    } copy{1, lobby.c_str(), Self(machine)};
    void* details = nullptr;
    if (FakeExport<std::int32_t (*)(void*, const void*, void**)>("EOS_Lobby_CopyLobbyDetailsHandle")(kLobbyInterface, &copy,
                                                                                                    &details))
        return seen;
    const std::uint32_t count = FakeExport<std::uint32_t (*)(void*, const void*)>("EOS_LobbyDetails_GetMemberCount")(details, nullptr);
    for (std::uint32_t i = 0; i < count; ++i) {
        struct ByIndex {
            std::int32_t ApiVersion;
            std::uint32_t MemberIndex;
        } byIndex{1, i};
        const void* member = FakeExport<const void* (*)(void*, const void*)>("EOS_LobbyDetails_GetMemberByIndex")(details, &byIndex);
        char text[64]{};
        std::int32_t length = sizeof(text);
        FakeExport<std::int32_t (*)(const void*, char*, std::int32_t*)>("EOS_ProductUserId_ToString")(member, text, &length);
        struct ByKey {
            std::int32_t ApiVersion;
            const void* TargetUserId;
            const char* AttrKey;
        } byKey{1, member, kProtocolKey};
        // As the plugin publishes it (lobbytexts.cpp): a string.
        struct AttributeData {
            std::int32_t ApiVersion;
            const char* Key;
            const char* AsUtf8;
            std::int32_t ValueType;
        };
        struct Attribute {
            std::int32_t ApiVersion;
            const AttributeData* Data;
            std::int32_t Visibility;
        };
        Attribute* attribute = nullptr;
        const bool marked = FakeExport<std::int32_t (*)(void*, const void*, Attribute**)>(
                                "EOS_LobbyDetails_CopyMemberAttributeByKey")(details, &byKey, &attribute) == 0 &&
                            attribute && attribute->Data && attribute->Data->ValueType == 3 && attribute->Data->AsUtf8 &&
                            attribute->Data->AsUtf8[0];
        if (attribute) FakeExport<void (*)(void*)>("EOS_Lobby_Attribute_Release")(attribute);
        seen.members.push_back(text);
        seen.marked.push_back(marked);
    }
    FakeExport<void (*)(void*)>("EOS_LobbyDetails_Release")(details);
    return seen;
}

bool AllMarked(const Seen& seen, std::size_t members) {
    if (seen.members.size() != members) return false;
    for (bool marked : seen.marked)
        if (!marked) return false;
    return true;
}

int expectedMembers() {
    char text[16]{};
    GetEnvironmentVariableA("EDF6NET_MEMBERS", text, sizeof(text));
    return text[0] ? std::atoi(text) : 2;
}

struct Room {
    std::string lobby;
    std::vector<std::string> members;  // in lobby order, as the game adds them
};

// Creates (host) or joins (guest) the room the way the game does, then waits until every member is in and
// publishes its netcode protocol (EDF6Coop does that on its own, from the game's EOS ticks).
bool EnterRoom(Machine& machine, bool host, Room& room) {
    Entered entered;
    if (host) {
        CreateOptions options;
        options.LocalUserId = Self(machine);
        options.MaxLobbyMembers = gamenet::kMaxMachines;
        Import<LobbyCall>(machine, "EOS_Lobby_CreateLobby")(kLobbyInterface, &options, &entered, &OnEntered);
    } else {
        // Guests come in one after another, in seat order (EDF6NET_SEAT): seat n joins once n are in.
        char seatText[8]{};
        GetEnvironmentVariableA("EDF6NET_SEAT", seatText, sizeof(seatText));
        const std::uint32_t seat = static_cast<std::uint32_t>(std::atoi(seatText));
        void* details = nullptr;
        const auto roomDetails = FakeExport<void* (*)()>("FakeNet_RoomDetails");
        const auto roomCount = FakeExport<std::uint32_t (*)()>("FakeNet_RoomCount");
        if (!TickUntil(machine, 10000, [&] {
                if (roomCount() < seat) return false;
                return (details = roomDetails()) != nullptr;
            })) {
            Result("room", "no room to join");
            return false;
        }
        JoinOptions options;
        options.LobbyDetailsHandle = details;
        options.LocalUserId = Self(machine);
        Import<LobbyCall>(machine, "EOS_Lobby_JoinLobby")(kLobbyInterface, &options, &entered, &OnEntered);
        if (TickUntil(machine, 5000, [&] { return entered.done; })) FakeExport<void (*)(void*)>("EOS_LobbyDetails_Release")(details);
    }
    if (!TickUntil(machine, 5000, [&] { return entered.done; }) || entered.result != 0) {
        Result("room", "%s failed (%d)", host ? "create" : "join", entered.result);
        return false;
    }
    room.lobby = entered.lobby;
    Result("lobby", "%s", room.lobby.c_str());
    const std::size_t members = static_cast<std::size_t>(expectedMembers());
    const bool everyone = TickUntil(machine, 20000, [&] { return AllMarked(LookAtRoom(machine, room.lobby), members); });
    const Seen seen = LookAtRoom(machine, room.lobby);
    for (std::size_t i = 0; i < seen.members.size(); ++i)
        Result("member", "%s %s", seen.members[i].c_str(), seen.marked[i] ? "marked" : "unmarked");
    room.members = seen.members;
    return everyone;
}

// Starts the game's packet controller for the room and waits until the game's P2P handshake connected every
// other member. `listen` runs between the two and subscribes what the machine receives: a member done with its own
// handshakes sends at once, while this machine may still be waiting for someone else's, and the game's controller
// delivers what arrives on every tick of that wait - to whoever listens then, and nowhere else (it acknowledges the
// record all the same, so its sender never sends it again). In the game the subscribers (the event controller among
// them) exist before the network does.
bool Connect(Machine& machine, const Room& room, Transport& transport, const std::function<bool()>& listen = {}) {
    if (!transport.Start(machine, room.lobby, room.members)) {
        Result("link", "the game's network objects could not be built");
        return false;
    }
    if (listen && !listen()) return false;
    const bool connected = TickUntil(machine, 15000, [&] {
        transport.Tick();
        for (const auto& member : room.members)
            if (member != machine.user && !transport.Connected(member)) return false;
        return true;
    });
    for (const auto& member : room.members)
        Result("network-index", "%s %d%s", member.c_str(), transport.NetworkIndex(member),
               member == machine.user ? " (self)" : transport.Connected(member) ? " connected" : " NOT connected");
    return connected;
}

constexpr std::uint32_t kProbeType = 0x2700;  // a record type nothing in the game subscribes to

// Each machine sends every other one a reliable record through the game's controller and waits for theirs.
int Link(Machine& machine, bool host) {
    Room room;
    if (!EnterRoom(machine, host, room)) return 1;
    Transport transport;
    std::vector<std::string> received;
    const auto listen = [&] {
        transport.Subscribe(kProbeType, [&](int from, const std::uint8_t* data, std::size_t size) {
            received.emplace_back(reinterpret_cast<const char*>(data), size);
            Result("received", "from %d: %.*s", from, static_cast<int>(size), reinterpret_cast<const char*>(data));
        });
        return true;
    };
    if (!Connect(machine, room, transport, listen)) return 1;
    const std::string probe = "PROBE-PLAINTEXT-FROM-" + machine.user;
    for (const auto& member : room.members)
        if (member != machine.user && !transport.SendReliable(member, kProbeType, probe.data(), probe.size()))
            Result("send", "SendReliable to %s refused", member.c_str());
    const std::size_t others = room.members.size() - 1;
    const bool all = TickUntil(machine, 10000, [&] {
        transport.Tick();
        return received.size() >= others;
    });
    // Keep ticking a little: acknowledgements and resends of the others still need this machine.
    TickUntil(machine, 1500, [&] {
        transport.Tick();
        return false;
    });
    return all ? 0 : 1;
}


// What the game's controller sends in a room. Every machine sends every other one a state-like record (kStateType,
// unreliable to the game, every 4th frame, a size no other datagram has) and now and then a reliable one, for
// EDF6NET_SECONDS (default 4) s, and reports what it sent and what arrived.
constexpr std::uint32_t kStateType = 0x2800;
constexpr std::size_t kStateBytes = 77;  // a datagram of only this record: 8 + 4 + 77 = 89 bytes

int Seconds(int fallback) {
    char text[16]{};
    GetEnvironmentVariableA("EDF6NET_SECONDS", text, sizeof(text));
    return text[0] ? std::atoi(text) : fallback;
}

int NetStats(Machine& machine, bool host) {
    Room room;
    if (!EnterRoom(machine, host, room)) return 1;
    Transport transport;
    struct Count {
        std::size_t states = 0, events = 0;
        ULONGLONG last = 0, gap = 0;
    };
    std::map<int, Count> got;          // by sender's network index
    std::map<std::string, Count> sent;  // by member
    const auto listen = [&] {
        transport.Subscribe(kStateType, [&](int from, const std::uint8_t*, std::size_t) {
            Count& c = got[from];
            const ULONGLONG now = GetTickCount64();
            if (c.last) c.gap = (std::max)(c.gap, now - c.last);
            c.last = now;
            ++c.states;
        });
        transport.Subscribe(kProbeType, [&](int from, const std::uint8_t*, std::size_t) { ++got[from].events; });
        return true;
    };
    if (!Connect(machine, room, transport, listen)) return 1;
    const std::vector<std::uint8_t> state(kStateBytes, 0x5A);
    const std::string probe = "EVENT-" + machine.user;
    int frame = 0;
    TickUntil(machine, static_cast<unsigned>(Seconds(4)) * 1000, [&] {
        for (const auto& member : room.members) {
            if (member == machine.user) continue;
            if (frame % 4 == 0 && transport.SendUnreliable(member, kStateType, state.data(), state.size()))
                ++sent[member].states;
            if (frame % 30 == 15 && transport.SendReliable(member, kProbeType, probe.data(), probe.size()))
                ++sent[member].events;
        }
        ++frame;
        transport.Tick();
        return false;
    });
    // The others still send: keep receiving (and acknowledging) until everyone is done.
    const auto finish = FakeExport<std::uint32_t (*)(int)>("FakeNet_Finish");
    finish(1);
    TickUntil(machine, 15000, [&] {
        transport.Tick();
        return finish(0) >= room.members.size();
    });
    TickUntil(machine, 1500, [&] {
        transport.Tick();
        return false;
    });
    for (const auto& [member, c] : sent) Result("sent-to", "%s states=%zu events=%zu", member.c_str(), c.states, c.events);
    for (const auto& member : room.members) {
        if (member == machine.user) continue;
        const Count& c = got[transport.NetworkIndex(member)];
        Result("got-from", "%s states=%zu events=%zu gap=%llu", member.c_str(), c.states, c.events, c.gap);
    }
    return 0;
}

// The netcode version gate. Every machine stays in the room for EDF6NET_SECONDS (default 5) s, ticking,
// and says how many the room has at the end.
int VersionGate(Machine& machine, bool host) {
    Room room;
    if (!EnterRoom(machine, host, room)) return 1;
    TickUntil(machine, static_cast<unsigned>(Seconds(5)) * 1000, [] { return false; });
    Result("room-count", "%u", FakeExport<std::uint32_t (*)()>("FakeNet_RoomCount")());
    return 0;
}

// Player sync (netplayer.h): a player record as EDF6Coop extends it - the game's own fields written with the
// game's writers (mask, a float, the position as the game sends it), then the block as a bin element written by
// EDF6Coop's writer, then the same block written by the game's own bin writer (EDF+12B5200) - goes through the
// game's controller to the other machine, which reads it twice: with the game's readers and EDF6Coop's bin reader,
// and with the game's readers and the game's bin reader (EDF+12B49D0). Both must give back every block exactly.
constexpr std::uint32_t kPlayerType = 0x2701;  // a record type nothing in the game subscribes to
constexpr int kPlayerRecords = 8;
constexpr std::uintptr_t kSerializeWriter = 0x79A460, kSerializeReader = 0x79A430;
constexpr std::uintptr_t kWriteInt16 = 0x12B54E0, kWriteFloat = 0x12B5350, kWriteVec3 = 0x761B20, kWriteBin = 0x12B5200;
constexpr std::uintptr_t kReadInt16 = 0x12B4B80, kReadFloat = 0x12B4AD0, kReadVec3 = 0x760C50, kReadBin = 0x12B49D0;
struct alignas(16) GameSerialize {
    std::uint8_t bytes[0x600];
};

multislot::PlayerSample SampleFor(int seat, int i) {
    multislot::PlayerSample s;
    s.seq = static_cast<std::uint16_t>(100 + i);
    s.senderMs = 0xFFFFFFF0u + static_cast<std::uint32_t>(33 * i);  // across the wrap
    s.position = {700.3f + seat * 11.0f + i * 0.217f, 12.7f + i, -950.55f};
    s.velocity = {6.25f, -9.8f + i, 0.125f * seat};
    return s;
}
bool SameSample(const multislot::PlayerSample& a, const multislot::PlayerSample& b) {
    return a.seq == b.seq && a.senderMs == b.senderMs && !std::memcmp(&a.position, &b.position, sizeof(a.position)) &&
           !std::memcmp(&a.velocity, &b.velocity, sizeof(a.velocity));
}

// Remote aim driven every frame (netplayer_game.cpp, docs/aim/plan.md). Each machine's player turns: aim record i
// carries AimFor(i), written after the record mask by the installed slot 52 hook around the game's writer
// (EDF+59FDB0), with the mask the installed slot 92 hook gives. The other machine reads each record through the
// installed slot 53 hook into a remote player copy (vtable slot 33 true, +0x128 bit 0; its sync control stays
// disabled, so the position drive returns before the Havok proxy) and steps the copy with the installed slot 55
// hook every frame, on the hooks' own clock. The original NetworkUpdate (EDF+596130) needs the proxy and the game's
// managers, which no machine builds: its first byte becomes a ret, as in PlayerAimGameCode.
constexpr std::uint32_t kAimType = 0x2702;  // a record type nothing in the game subscribes to
constexpr int kAimRecords = 10;
constexpr std::uintptr_t kMaskSlot = 0x17D27B8, kWriteSlot = 0x17D2678, kReadSlot = 0x17D2680, kUpdateSlot = 0x17D2690;
constexpr std::uintptr_t kNativeRead = 0x592180, kNetworkUpdate = 0x596130;
// The copy's angles must come within kAimTolerance of the record's aim, which the game sends as half floats (at most
// 0.001 rad off below 4 rad). A 0.3 rad step at AimTauMs 50 gets there after about 2.7 tau, 140 ms; the check allows
// kAimSettleMs, inside the 400 ms after which a track is stale and no longer driven.
constexpr float kAimTolerance = 0.02f, kHalfFloat = 0.002f;
constexpr double kAimSettleMs = 300.0;

struct Aim {
    float pitch, yaw;
};
// 0.3 rad of yaw and -0.03 of pitch per record, inside the hooks' snap radius (pi/2); the last turns back 2.7 rad,
// which the hooks write outright.
Aim AimFor(int i) { return i == kAimRecords - 1 ? Aim{0.3f, 0.2f} : Aim{0.2f - 0.03f * i, 0.5f + 0.3f * i}; }

bool IsPlayerStub(void*) { return true; }
struct alignas(16) Soldier {
    std::uint8_t bytes[0x2600]{};
};

// Sends this machine's aim records and drives the copy of the other's; returns whether every check passed.
// `arrived`: the other machine's aim records as the controller delivered them. Each is read once the copy has
// settled on the one before, so what is checked is the hooks' stepping, not the delivery's timing.
bool RemoteAim(const Machine& machine, const Transport& transport, const Room& room,
               std::deque<std::vector<std::uint8_t>>& arrived) {
    using namespace multislot;
    using Clock = std::chrono::steady_clock;
    using Record = void (*)(void*, void*, std::uint16_t);
    const Game& game = transport.game();
    const auto slot = [&](std::uintptr_t rva) { return *machine.At<void* const*>(rva); };
    if (slot(kReadSlot) == machine.At<void*>(kNativeRead)) {
        Result("aim", "the player sync hooks are not installed in this process");
        return false;
    }
    const auto mask = reinterpret_cast<std::uint16_t (*)(void*)>(slot(kMaskSlot));
    const auto write = reinterpret_cast<Record>(slot(kWriteSlot));
    const auto read = reinterpret_cast<Record>(slot(kReadSlot));
    const auto update = reinterpret_cast<void (*)(void*)>(slot(kUpdateSlot));
    auto* const original = machine.At<std::uint8_t*>(kNetworkUpdate);
    DWORD protect = 0;
    if (!VirtualProtect(original, 1, PAGE_EXECUTE_READWRITE, &protect)) return false;
    *original = 0xC3;
    VirtualProtect(original, 1, protect, &protect);
    FlushInstructionCache(GetCurrentProcess(), original, 1);

    static std::array<void*, 100> vtable{};
    vtable[33] = Address(&IsPlayerStub);
    Soldier player, copy;
    for (Soldier* s : {&player, &copy}) {
        Put(s->bytes, 0, vtable.data());
        Put(s->bytes, 0x2F8, 100.0f);  // health: a change from none selects the first record for the aim diagnostic
    }
    Put(player.bytes, 0x128, std::uint8_t{2});
    Put(copy.bytes, 0x128, std::uint8_t{1});
    // The hooks add the block and drive the copy only with the room's PlayerSync feature on.
    const auto paired = static_cast<std::uint16_t>(2 | kPlayerBlockBit);
    if (!TickUntil(machine, 10000, [&] {
            transport.Tick();
            return (mask(player.bytes) & paired) == paired;
        })) {
        Result("aim", "the slot 92 hook never paired aim with the block (PlayerSync off in the room?)");
        return false;
    }
    const auto pitchNow = [&] { return Get<float>(copy.bytes, 0x1230); };
    const auto yawNow = [&] { return Get<float>(copy.bytes, 0x1234); };
    const auto yawOff = [](float yaw, float target) { return std::fabs(std::remainder(yaw - target, 6.2831853f)); };
    int sent = 0, applied = 0, frame = 0, good = 0, bad = 0, current = -1, stepped = 0;
    bool settled = true, ok = true;
    float before = 0.0f, firstFrame = 0.0f;
    double slowestMs = 0.0;
    Clock::time_point readAt;
    const auto finish = [&](bool passed, double ms) {
        settled = true;
        passed = passed && ok;
        (passed ? good : bad)++;
        slowestMs = (std::max)(slowestMs, ms);
        Result("aim-record", "#%d target (%.3f, %.3f): copy at (%.4f, %.4f) after %d frames, %.0f ms; first frame left "
               "%.0f%% of the yaw step %s", current, AimFor(current).pitch, AimFor(current).yaw, pitchNow(), yawNow(),
               stepped, ms, before > 0.0f ? 100.0f * firstFrame / before : 0.0f, passed ? "ok" : "BAD");
    };
    const bool done = TickUntil(machine, 20000, [&] {
        transport.Tick();
        // Every second frame, as the hooks send (SendIntervalFrames=2).
        if (sent < kAimRecords && frame++ % 2 == 0) {
            const Aim aim = AimFor(sent);
            Put(player.bytes, 0x1230, aim.pitch);
            Put(player.bytes, 0x1234, aim.yaw);
            const std::uint16_t bits = static_cast<std::uint16_t>(mask(player.bytes) & paired);
            GameSerialize writer{};
            game.Fn<void* (*)(void*)>(kSerializeWriter)(writer.bytes);
            game.Fn<bool (*)(void*, std::int16_t)>(kWriteInt16)(writer.bytes, static_cast<std::int16_t>(bits));
            write(player.bytes, writer.bytes, bits);
            std::uint64_t length = 0;
            std::memcpy(&length, writer.bytes + kSerializeEnd, sizeof(length));
            std::vector<std::uint8_t> record(1 + length);
            record[0] = static_cast<std::uint8_t>(sent);
            std::memcpy(record.data() + 1, writer.bytes + kSerializeData, length);
            for (const auto& member : room.members)
                if (member != machine.user && (bits != paired ||
                                               !transport.SendReliable(member, kAimType, record.data(), record.size())))
                    Result("send", "aim record %d to %s refused (mask %X)", sent, member.c_str(), bits);
            ++sent;
        }
        if (current >= 0) {
            update(copy.bytes);
            ++stepped;
            const Aim target = AimFor(current);
            const float pitchError = std::fabs(pitchNow() - target.pitch), yawError = yawOff(yawNow(), target.yaw);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - readAt).count();
            // A new or restarted track, and an about-turn, are written outright on the first frame; otherwise one
            // frame of 16 to 50 ms (the hooks' step cap) leaves exp(-dt / 50 ms), 37 to 73 percent, of the step.
            if (stepped == 1) {
                firstFrame = yawError;
                const bool snap = current == 0 || current == kAimRecords - 1;
                if (snap ? pitchError > kHalfFloat || yawError > kHalfFloat
                         : !(yawError > 0.3f * before && yawError < 0.9f * before)) {
                    ok = false;
                    Result("aim", "record %d: the first frame %s", current, snap ? "did not write the aim outright"
                                                                                 : "did not step part of the way");
                }
            }
            if (!settled && pitchError < kAimTolerance && yawError < kAimTolerance) finish(true, ms);
            if (!settled && ms > kAimSettleMs) finish(false, ms);
        }
        if (settled && !arrived.empty()) {
            const std::vector<std::uint8_t> record = std::move(arrived.front());
            arrived.pop_front();
            current = record[0];
            ok = current == applied++ && record.size() > 1;
            GameSerialize reader{};
            game.Fn<void* (*)(void*, const void*, std::size_t)>(kSerializeReader)(reader.bytes, record.data() + 1,
                                                                                  record.size() - 1);
            const std::uint16_t bits = game.Fn<std::uint16_t (*)(void*)>(kReadInt16)(reader.bytes);
            const float pitch = pitchNow(), yaw = yawNow();
            read(copy.bytes, reader.bytes, bits);
            readAt = Clock::now();
            ok = ok && bits == paired && Get<std::uint64_t>(reader.bytes, kSerializeReadPos) == record.size() - 1;
            // Once driven, a record changes the copy's target and not its angles. That the hook kept them shows it
            // decoded the block and took the aim as newer.
            if (current > 0 && (pitchNow() != pitch || yawNow() != yaw)) {
                ok = false;
                Result("aim", "record %d moved a driven copy's angles on arrival", current);
            }
            before = yawOff(yawNow(), AimFor(current).yaw);
            stepped = 0;
            settled = false;
        }
        return applied == kAimRecords && settled;
    });
    Result("aim-records", "%d ok %d bad, slowest %.0f ms", good, bad, slowestMs);
    return done && bad == 0;
}

int PlayerSync(Machine& machine, bool host) {
    using namespace multislot;
    Room room;
    if (!EnterRoom(machine, host, room)) return 1;
    Transport transport;
    const int seat = host ? 0 : 1;
    int good = 0, bad = 0;
    const auto onRecord = [&](int from, const std::uint8_t* data, std::size_t size) {
        const Game& game = transport.game();
        const int i = static_cast<int>(data[0]);
        const PlayerSample expected = SampleFor(1 - seat, i);
        bool ok = size > 1;
        float halfError = 0.0f;
        for (int pass = 0; ok && pass < 2; ++pass) {
            GameSerialize reader{};
            game.Fn<void* (*)(void*, const void*, std::size_t)>(kSerializeReader)(reader.bytes, data + 1, size - 1);
            const std::uint16_t mask = game.Fn<std::uint16_t (*)(void*)>(kReadInt16)(reader.bytes);
            const float heading = game.Fn<float (*)(void*)>(kReadFloat)(reader.bytes);
            alignas(16) float position[4]{};
            game.Fn<bool (*)(void*, float*)>(kReadVec3)(reader.bytes, position);
            ok = ok && mask == (0x58 | kPlayerBlockBit) && std::fabs(heading - 1.5f) < 0.01f;
            halfError = (std::max)(halfError, std::fabs(position[0] - expected.position.x));
            for (int copy = 0; copy < 2; ++copy) {
                std::uint8_t block[64]{};
                std::size_t got = 0;
                if (pass == 0) {
                    ok = ok && ReadBinElement(reader.bytes, block, sizeof(block), got);
                } else {
                    std::size_t capacity = sizeof(block);
                    ok = ok && game.Fn<bool (*)(void*, void*, std::size_t*)>(kReadBin)(reader.bytes, block, &capacity);
                    got = capacity;
                }
                PlayerSample sample;
                ok = ok && got == kPlayerBlockBytes && DecodePlayerBlock(block, got, sample) && SameSample(sample, expected);
            }
        }
        if (ok)
            ++good;
        else
            ++bad;
        Result("player-record", "from %d #%d %s; the game's half-float position is off by %.3f m", from, i,
               ok ? "ok" : "MISMATCH", halfError);
    };
    std::deque<std::vector<std::uint8_t>> aims;
    const auto listen = [&] {
        transport.Subscribe(kPlayerType, onRecord);
        transport.Subscribe(kAimType, [&](int, const std::uint8_t* data, std::size_t size) { aims.emplace_back(data, data + size); });
        return true;
    };
    if (!Connect(machine, room, transport, listen)) return 1;
    const Game& game = transport.game();
    for (int i = 0; i < kPlayerRecords; ++i) {
        GameSerialize writer{};
        game.Fn<void* (*)(void*)>(kSerializeWriter)(writer.bytes);
        const PlayerSample sample = SampleFor(seat, i);
        game.Fn<bool (*)(void*, std::int16_t)>(kWriteInt16)(writer.bytes, static_cast<std::int16_t>(0x58 | kPlayerBlockBit));
        game.Fn<bool (*)(void*, float)>(kWriteFloat)(writer.bytes, 1.5f);
        alignas(16) const float position[4] = {sample.position.x, sample.position.y, sample.position.z, 1.0f};
        game.Fn<bool (*)(void*, const float*)>(kWriteVec3)(writer.bytes, position);
        std::uint8_t block[kPlayerBlockBytes];
        EncodePlayerBlock(sample, block, sizeof(block));
        const bool ours = WriteBinElement(writer.bytes, block, sizeof(block));
        game.Fn<bool (*)(void*, const void*, std::size_t)>(kWriteBin)(writer.bytes, block, sizeof(block));
        std::uint64_t length = 0;
        std::memcpy(&length, writer.bytes + kSerializeEnd, sizeof(length));
        std::vector<std::uint8_t> record(1 + length);
        record[0] = static_cast<std::uint8_t>(i);
        std::memcpy(record.data() + 1, writer.bytes + kSerializeData, length);
        for (const auto& member : room.members)
            if (member != machine.user && (!ours || !transport.SendReliable(member, kPlayerType, record.data(), record.size())))
                Result("send", "player record %d to %s refused", i, member.c_str());
        if (i == 0) Result("player-record-bytes", "%llu", static_cast<unsigned long long>(length));
    }
    const bool all = TickUntil(machine, 10000, [&] {
        transport.Tick();
        return good + bad >= kPlayerRecords;
    });
    Result("player-records", "%d ok %d bad", good, bad);
    const bool aimed = RemoteAim(machine, transport, room, aims);
    TickUntil(machine, 1500, [&] {
        transport.Tick();
        return false;
    });
    return all && bad == 0 && aimed ? 0 : 1;
}

}  // namespace

int RunRole(Machine& machine, const std::string& role) {
    const bool host = role.rfind("host-", 0) == 0;
    const std::string step = role.substr(role.find('-') + 1);
    if (step == "room") {
        Room room;
        return EnterRoom(machine, host, room) ? 0 : 1;
    }
    if (step == "link") return Link(machine, host);
    if (step == "playersync") return PlayerSync(machine, host);
    if (step == "netstats") return NetStats(machine, host);
    if (step == "versiongate") return VersionGate(machine, host);
    Result("role", "unknown role %s", role.c_str());
    return 2;
}

}  // namespace gamenet
