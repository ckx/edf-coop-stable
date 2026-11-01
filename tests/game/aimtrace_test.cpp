// A native aim record must remain byte-for-byte identical with diagnostics enabled. Exercise the installed
// soldier write/read hooks and the real serializers, including the yaw offset and a hit-facing discontinuity.
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "../../game/netplayer_game.h"
#include "../../game/netplayer.h"
#include "../../game/netfeature.h"
#include "../../game/log.h"
#include "../../game/code.h"
#include "native_runtime.h"

using namespace multislot;
namespace {
int failures = 0;
void Check(bool ok, const char* message) { if (!ok) { ++failures; std::printf("FAIL: %s\n", message); } }
template <typename T> void Put(void* p, std::size_t at, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + at, &value, sizeof(value));
}
template <typename T> T Get(const void* p, std::size_t at) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(p) + at, sizeof(value)); return value;
}
bool IsPlayer(void*) { return true; }
}
int main(int argc, char** argv) {
    if (argc < 2 || GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES) return 77;
    auto* game = reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES));
    if (!game || !native_test::ResolveCrtImports(reinterpret_cast<HMODULE>(game))) return 1;
    DeleteFileW(L"aimtrace-test.log");
    LogOpen(L"aimtrace-test.log");
    PlayerSyncSettings settings;
    settings.flushIntervalMs = 0;
    if (!InstallPlayerSync(game, settings)) return 1;
    using Record = void (*)(void*, void*, std::uint16_t);
    const auto write = Get<Record>(game, 0x17D2678);
    const auto read = Get<Record>(game, 0x17D2680);
    const auto nativeWrite = reinterpret_cast<Record>(game + 0x59FDB0);
    std::array<void*, 100> table{};
    table[33] = reinterpret_cast<void*>(&IsPlayer);
    alignas(16) std::array<unsigned char, 0x2600> sender{}, receiver{};
    Put(sender.data(), 0, table.data());
    Put(receiver.data(), 0, table.data());
    Put(sender.data(), 0x128, std::uint8_t{2});
    Put(receiver.data(), 0x128, std::uint8_t{1});
    Put(sender.data(), 0x2F8, 100.0f);
    Put(receiver.data(), 0x2F8, 100.0f);
    Put(sender.data(), 0x1230, 0.25f);
    Put(sender.data(), 0x1E44, 0.5f);
    Put(receiver.data(), 0x1248, 3.0f);
    alignas(16) std::array<unsigned char, 0x600> noAim{};
    write(sender.data(), noAim.data(), kPlayerBlockBit);
    Check(Get<std::uint64_t>(noAim.data(), kSerializeEnd) == kPlayerBlockBytes + 2,
          "a record without native aim still contains only the existing position block");
    std::array<unsigned char, 0x600> firstRecord{};
    std::size_t firstSize = 0;
    for (float yaw : {1.0f, -2.0f}) {
        Put(sender.data(), 0x1234, yaw);
        alignas(16) std::array<unsigned char, 0x600> native{}, hooked{}, reader{};
        nativeWrite(sender.data(), native.data(), 2);
        write(sender.data(), hooked.data(), 2 | kPlayerBlockBit);
        const auto nativeSize = Get<std::uint64_t>(native.data(), kSerializeEnd);
        const auto size = Get<std::uint64_t>(hooked.data(), kSerializeEnd);
        if (!firstSize) {
            firstSize = static_cast<std::size_t>(size);
            std::memcpy(firstRecord.data(), hooked.data() + kSerializeData, firstSize);
        }
        Check(nativeSize > 0 && size > nativeSize, "native aim followed by the existing position block");
        Check(!std::memcmp(native.data() + kSerializeData, hooked.data() + kSerializeData,
                           static_cast<std::size_t>(nativeSize)), "diagnostics preserve native aim bytes");
        using Reader = void* (*)(void*, const void*, std::size_t);
        reinterpret_cast<Reader>(game + 0x79A430)(reader.data(), hooked.data() + kSerializeData,
                                                 static_cast<std::size_t>(size));
        read(receiver.data(), reader.data(), 2 | kPlayerBlockBit);
        Check(std::fabs(Get<float>(receiver.data(), 0x1234) - (yaw + 0.5f)) < 0.01f,
              "native receive applies sent yaw plus offset exactly once");
        Check(std::fabs(Get<float>(receiver.data(), 0x1230) - 0.25f) < 0.01f, "native pitch survives");
        Check(Get<float>(receiver.data(), 0x1248) == 0, "native receive still clears its aim accumulator");
        Check(Get<std::uint64_t>(reader.data(), kSerializeReadPos) == size, "reader consumes exactly the record");
    }
    // Reproduce an intervening local pose change without pretending to simulate the game's ragdoll physics.
    Put(receiver.data(), 0x1234, 0.75f);
    Put(receiver.data(), 0x1230, -0.5f);
    Put(receiver.data(), 0x1248, 3.0f);
    TraceSpectatorAim(receiver.data(), false);
    Check(Get<float>(receiver.data(), 0x1234) == 0.75f, "spectator diagnostic never writes the observed aim");
    alignas(16) std::array<unsigned char, 0x600> staleReader{};
    reinterpret_cast<void* (*)(void*, const void*, std::size_t)>(game + 0x79A430)(
        staleReader.data(), firstRecord.data(), firstSize);
    read(receiver.data(), staleReader.data(), 2 | kPlayerBlockBit);
    Check(std::fabs(Get<float>(receiver.data(), 0x1234) - 0.75f) < 0.01f,
          "stale paired aim preserves the current yaw");
    Check(Get<float>(receiver.data(), 0x1230) == -0.5f && Get<float>(receiver.data(), 0x1248) == 3.0f,
          "stale aim preserves pitch and the native accumulator");
    Check(Get<std::uint64_t>(staleReader.data(), kSerializeReadPos) == firstSize,
          "a stale aim record is still fully consumed");
    alignas(16) std::array<unsigned char, 0x600> delayedAim{}, newerPosition{}, latestAim{}, nativeOnly{};
    Put(sender.data(), 0x1234, 2.0f);
    write(sender.data(), delayedAim.data(), 2 | kPlayerBlockBit);
    write(sender.data(), newerPosition.data(), kPlayerBlockBit);
    const auto deliver = [&](const auto& record, std::uint16_t mask) {
        alignas(16) std::array<unsigned char, 0x600> buffer{};
        const auto size = Get<std::uint64_t>(record.data(), kSerializeEnd);
        reinterpret_cast<void* (*)(void*, const void*, std::size_t)>(game + 0x79A430)(
            buffer.data(), record.data() + kSerializeData, static_cast<std::size_t>(size));
        read(receiver.data(), buffer.data(), mask);
        Check(Get<std::uint64_t>(buffer.data(), kSerializeReadPos) == size, "full reordered record consumed");
    };
    deliver(newerPosition, kPlayerBlockBit);
    deliver(delayedAim, 2 | kPlayerBlockBit);
    Check(std::fabs(Get<float>(receiver.data(), 0x1234) - 2.5f) < 0.01f,
          "newer aim survives an older position sequence when the intervening record had no aim");
    Put(sender.data(), 0x1234, -1.0f);
    write(sender.data(), latestAim.data(), 2 | kPlayerBlockBit);
    deliver(latestAim, 2 | kPlayerBlockBit);
    deliver(delayedAim, 2 | kPlayerBlockBit);
    Check(std::fabs(Get<float>(receiver.data(), 0x1234) + 0.5f) < 0.01f,
          "a delayed aim record cannot rewind a newer received angle");
    Put(sender.data(), 0x1234, 0.0f);
    nativeWrite(sender.data(), nativeOnly.data(), 2);
    deliver(nativeOnly, 2);
    Check(std::fabs(Get<float>(receiver.data(), 0x1234) - 0.5f) < 0.01f,
          "native-only records keep their original behavior");
    LogFlush();
    std::ifstream file("aimtrace-test.log", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Check(text.find("event=send-noaim") != std::string::npos,
          "sender observations distinguish a record without native aim from a lost aim record");
    Check(text.find("event=send user=") != std::string::npos && text.find("event=receive") != std::string::npos,
          "paired send and receive observations reach the log");
    Check(text.find("event=spectate-locked") != std::string::npos &&
          text.find("yaw=0.75000") != std::string::npos && text.find("received=(0.25000,-1.50000)") != std::string::npos,
          "frame observation distinguishes current facing from the most recently received aim");
    Check(text.find("event=receive-aim-stale") != std::string::npos,
          "diagnostics identify a rejected paired aim update");
    // The live capture delivers one of every three written position blocks. Keep that selection
    // explicit: this exercises the native mask/writer/reader, not the still-unproven thinning path.
    SetNetFeatureForTest(NetFeature::PlayerSync, true);
    using Mask = std::uint16_t (*)(void*);
    for (const auto slot : {0x17D27B8, 0x17D12D8}) {
        const auto maskFor = Get<Mask>(game, slot);
        for (int phase = 0; phase < 3; ++phase) {
            for (std::uint32_t frame = 2; frame <= 36; frame += 2) {
                Put(sender.data(), 0x1820, frame);
                Put(sender.data(), 0x1234, static_cast<float>(frame) * 0.025f);
                Put(sender.data(), 0x1230, -static_cast<float>(frame) * 0.01f);
                // Other native fields have no fixture here; retain the real mask's aim and custom block.
                const auto mask = static_cast<std::uint16_t>(maskFor(sender.data()) & (2 | kPlayerBlockBit));
                alignas(16) std::array<unsigned char, 0x600> record{};
                write(sender.data(), record.data(), mask);
                if ((frame / 2) % 3 != static_cast<unsigned>(phase)) continue;
                deliver(record, mask);
                Check(std::fabs(Get<float>(receiver.data(), 0x1234) -
                                (Get<float>(sender.data(), 0x1234) + 0.5f)) < 0.01f &&
                      std::fabs(Get<float>(receiver.data(), 0x1230) - Get<float>(sender.data(), 0x1230)) < 0.01f,
                      "every delivered on-foot position block refreshes aim in each thinning phase");
            }
        }
        Put(sender.data(), 0x1820, std::uint32_t{2});
        const auto nativeMask = reinterpret_cast<Mask>(game + (slot == 0x17D12D8 ? 0x582DF0 : 0x59FB10));
        SetNetFeatureForTest(NetFeature::PlayerSync, false);
        Check(maskFor(sender.data()) == nativeMask(sender.data()), "closed feature gate preserves the native mask");
        SetNetFeatureForTest(NetFeature::PlayerSync, true);
        Put(sender.data(), 0x128, std::uint8_t{1});
        Check(maskFor(sender.data()) == nativeMask(sender.data()), "remote players keep the native mask");
        Put(sender.data(), 0x128, std::uint8_t{2});
        Put(sender.data(), 0x2E8, std::uint8_t{1});
        Check((maskFor(sender.data()) & 2) == 0, "downed players do not acquire on-foot aim records");
        Put(sender.data(), 0x2E8, std::uint8_t{0});
        std::array<int, 4> vehicle{};
        vehicle[2] = 1;
        Put(sender.data(), 0x1550, vehicle.data());
        Check((maskFor(sender.data()) & 2) == 0, "seated players do not acquire on-foot aim records");
        vehicle[2] = 0;
        Check((maskFor(sender.data()) & 2) != 0, "expired vehicle reference allows fresh on-foot aim");
        Put(sender.data(), 0x1550, static_cast<void*>(nullptr));
    }

    // The per-frame aim write: the installed slot-55 hook on the receiver, with the hooks' clock stepped by the test.
    // The original NetworkUpdate (EDF+596130) needs the Havok proxy and the game's managers, which this fixture does
    // not build, and the hook calls it by address: its first byte becomes a ret, the hook itself is unchanged. The
    // receiver's sync control stays disabled, so the position drive returns before the proxy.
    const unsigned char ret = 0xC3;
    Check(WriteCode(game + 0x596130, &ret, 1), "stub the original NetworkUpdate");
    using Update = void (*)(void*);
    const auto update = Get<Update>(game, 0x17D2690);
    const auto frame = [&] {
        AdvancePlayerSyncClockForTest(1000.0 / 60);
        update(receiver.data());
    };
    const auto sendAim = [&](float pitch, float yaw) {  // received yaw = yaw + the sender's 0.5 offset
        Put(sender.data(), 0x1230, pitch);
        Put(sender.data(), 0x1234, yaw);
        alignas(16) std::array<unsigned char, 0x600> record{};
        write(sender.data(), record.data(), 2 | kPlayerBlockBit);
        deliver(record, 2 | kPlayerBlockBit);
    };
    const auto pitchNow = [&] { return Get<float>(receiver.data(), 0x1230); };
    const auto yawNow = [&] { return Get<float>(receiver.data(), 0x1234); };
    sendAim(0.1f, 0.0f);
    frame();
    Check(std::fabs(yawNow() - 0.5f) < 0.01f && std::fabs(pitchNow() - 0.1f) < 0.01f,
          "the first driven frame writes the received aim outright");
    const float pitch0 = pitchNow(), yaw0 = yawNow();
    Put(receiver.data(), 0x1248, 3.0f);
    sendAim(-0.3f, 1.0f);  // 0.4 and 1.0 rad away, inside the snap radius
    Check(pitchNow() == pitch0 && yawNow() == yaw0, "a record for a driven copy changes its target, not its angles");
    Check(Get<float>(receiver.data(), 0x1248) == 0, "a driven record leaves the native reader's cleared accumulator");
    frame();
    Check(yawNow() > yaw0 + 0.1f && yawNow() < 1.5f - 0.5f && pitchNow() < pitch0 && pitchNow() > -0.3f + 0.2f,
          "one frame moves the angles part of the way to the target, neither standing nor snapping");
    for (int i = 1; i < 7; ++i) frame();  // 117 ms, about 2.3 AimTauMs
    Check(std::fabs(yawNow() - 1.5f) < 0.105f && std::fabs(pitchNow() + 0.3f) < 0.045f,
          "a driven copy converges within a tenth of the step after about 2.3 AimTauMs");
    const float held = yawNow();
    Put(receiver.data(), 0x1234, held + 0.3f);
    frame();
    Check(yawNow() >= held && yawNow() <= 1.505f, "a local change of a driven copy's yaw is replaced on the next frame");

    std::array<int, 4> seat{};
    seat[2] = 1;
    Put(receiver.data(), 0x1550, seat.data());
    sendAim(0.0f, 0.2f);  // target (0, 0.7)
    const float seatedYaw = yawNow();
    frame();
    frame();
    Check(yawNow() == seatedYaw, "a copy with a live vehicle control is not written");
    seat[2] = 0;
    frame();
    Check(std::fabs(yawNow() - 0.7f) < 0.01f && std::fabs(pitchNow()) < 0.01f,
          "a copy whose vehicle control expired writes the target outright, not smoothed from the seated angle");
    Put(receiver.data(), 0x1550, static_cast<void*>(nullptr));
    Put(receiver.data(), 0x2E8, std::uint8_t{1});
    Put(receiver.data(), 0x1234, 2.5f);
    frame();
    Check(yawNow() == 2.5f, "a downed copy is not written");
    Put(receiver.data(), 0x2E8, std::uint8_t{0});

    AdvancePlayerSyncClockForTest(450.0);  // past the 400 ms freshness window
    Put(receiver.data(), 0x1230, 0.2f);
    Put(receiver.data(), 0x1234, 2.0f);
    frame();
    Check(pitchNow() == 0.2f && yawNow() == 2.0f, "a stale aim track leaves the copy's angles to the game");
    Put(receiver.data(), 0x2F8, 90.0f);  // a health change selects the record for the diagnostic
    sendAim(0.05f, -1.0f);               // target (0.05, -0.5)
    Check(std::fabs(yawNow() + 0.5f) < 0.01f, "the first record after a stale track stands as the native reader wrote it");
    const float restartedPitch = pitchNow(), restartedYaw = yawNow();
    frame();
    Check(pitchNow() == restartedPitch && yawNow() == restartedYaw, "a restarted aim track writes its target outright");

    SetDriveAimForTest(false);
    sendAim(0.0f, 0.5f);
    Check(std::fabs(yawNow() - 1.0f) < 0.01f, "with DriveAim=0 a record writes the copy's angles natively");
    Put(receiver.data(), 0x1234, 1.25f);
    frame();
    Check(yawNow() == 1.25f, "with DriveAim=0 the update hook leaves the angles alone");
    SetDriveAimForTest(true);
    LogFlush();
    std::ifstream driveFile("aimtrace-test.log", std::ios::binary);
    const std::string driveLog{std::istreambuf_iterator<char>(driveFile), std::istreambuf_iterator<char>()};
    char expected[64];
    std::snprintf(expected, sizeof(expected), "written=(%.5f,%.5f) driven=1", restartedPitch, restartedYaw);
    Check(driveLog.find(expected) != std::string::npos, "the update record shows the angles written that frame");
    ClearNetFeatureForTest();
    LogClose();
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
