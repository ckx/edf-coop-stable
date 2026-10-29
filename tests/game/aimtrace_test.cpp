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
    ClearNetFeatureForTest();
    LogClose();
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
