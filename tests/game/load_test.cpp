// Loads the built plugin the way EDFModLoader does, against the real EDF.dll mapped without running its
// initialisation, and checks which of the game's sites it took over. The plugin sits in <work-folder>\<mode>\Mods\Plugins,
// as in a game folder.
//   PluginLoadTests EDF.dll EDF6Coop.dll work-folder on|off|fresh|optionsoff|replaced
#include <windows.h>
#include "late_fault.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include "PluginAPI.h"
#pragma warning(pop)

#include "../../game/hitauthgame.h"
#include "../../game/midhook.h"
#include "../../game/patches.h"
#include "../../game/radar.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::string ReadText(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// An INI as text: UTF-16LE with its BOM is turned into UTF-8, anything else is returned as it is.
std::string ReadIniText(const std::wstring& path) {
    const std::string raw = ReadText(path);
    if (raw.size() < 2 || static_cast<unsigned char>(raw[0]) != 0xFF || static_cast<unsigned char>(raw[1]) != 0xFE) return raw;
    const std::wstring wide(reinterpret_cast<const wchar_t*>(raw.data() + 2), (raw.size() - 2) / sizeof(wchar_t));
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), n, nullptr, nullptr);
    return text;
}

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

using Load = bool(EDFMLAPI*)(PluginInfo*);

bool InModule(std::uintptr_t address, HMODULE module) {
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
}

// `call rel32` at site -> stub `mov rax, imm64; jmp rax` -> address inside the plugin image.
bool RedirectedInto(const unsigned char* site, HMODULE plugin) {
    if (site[0] != 0xE8) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, site + 1, 4);
    const unsigned char* stub = site + 5 + relative;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(stub, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    if (stub[0] != 0x48 || stub[1] != 0xB8 || stub[10] != 0xFF || stub[11] != 0xE0) return false;
    std::uintptr_t target = 0;
    std::memcpy(&target, stub + 2, 8);
    return InModule(target, plugin);
}

// The site jumps to a read-only executable thunk that is exactly MidThunkCode for it, apart from the handler and
// resume addresses; the handler is in the plugin and the unwinder knows the thunk.
bool HookedInto(const unsigned char* base, const MidSite& site, HMODULE plugin) {
    const unsigned char* at = base + site.rva;
    if (at[0] != 0xE9) return false;
    for (std::size_t i = 5; i < site.original.size(); ++i)
        if (at[i] != 0x90) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, at + 1, 4);
    const unsigned char* thunk = at + 5 + relative;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(thunk, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || mbi.Protect != PAGE_EXECUTE_READ) return false;
    const std::uint64_t handlerMarker = 0x1122334455667788ull, resumeMarker = 0x8877665544332211ull;
    const auto reference = MidThunkCode(reinterpret_cast<MidHandler>(static_cast<std::uintptr_t>(handlerMarker)),
                                        site.original.data() + site.displacedOffset, site.displacedSize, resumeMarker);
    const auto expectedResume = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(at + site.original.size()));
    std::vector<bool> operand(reference.size(), false);
    std::uint64_t handler = 0;
    int handlers = 0, resumes = 0;
    for (std::size_t i = 0; i + 8 <= reference.size(); ++i) {
        std::uint64_t value = 0, actual = 0;
        std::memcpy(&value, reference.data() + i, 8);
        std::memcpy(&actual, thunk + i, 8);
        if (value != handlerMarker && value != resumeMarker) continue;
        for (std::size_t j = i; j < i + 8; ++j) operand[j] = true;
        if (value == handlerMarker) {
            handler = actual;
            ++handlers;
        } else if (actual == expectedResume) {
            ++resumes;
        } else {
            return false;
        }
    }
    for (std::size_t i = 0; i < reference.size(); ++i)
        if (!operand[i] && thunk[i] != reference[i]) return false;
    DWORD64 imageBase = 0;
    const bool described = RtlLookupFunctionEntry(static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(thunk)), &imageBase,
                                                  nullptr) != nullptr;
    return handlers == 1 && resumes == 2 && described && InModule(static_cast<std::uintptr_t>(handler), plugin);
}

bool SiteUntouched(const unsigned char* base, const MidSite& site) {
    return std::memcmp(base + site.rva, site.original.data(), site.original.size()) == 0;
}

void ThrowAndCatch() {
    try {
        throw std::runtime_error("thrown and caught outside EDF.dll");
    } catch (const std::exception&) {
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::printf("usage: PluginLoadTests EDF.dll EDF6Coop.dll work-folder on|off|fresh|optionsoff|replaced\n");
        return 2;
    }
    const std::wstring mode = argv[4];
    // CMake registers this test with or without the game; without it the test says so and counts as skipped.
    if (GetFileAttributesW(argv[1]) == INVALID_FILE_ATTRIBUTES) {
        std::printf("SKIPPED: %ls is not there (set EDF6_GAME_DIR to the game folder to run this test)\n", argv[1]);
        return 77;
    }
    const HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) {
        std::printf("FAIL: cannot map %ls (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    const auto base = reinterpret_cast<const unsigned char*>(game);

    const std::wstring folder = std::wstring(argv[3]) + L"\\" + mode;
    const std::wstring plugins = folder + L"\\Mods\\Plugins";
    for (const auto& path : {std::wstring(argv[3]), folder, folder + L"\\Mods", plugins}) CreateDirectoryW(path.c_str(), nullptr);
    const std::wstring dll = plugins + L"\\EDF6Coop.dll";
    const std::wstring logPath = plugins + L"\\EDF6Coop.log";
    const std::wstring iniPath = plugins + L"\\EDF6Coop.ini";
    CopyFileW(argv[2], dll.c_str(), FALSE);
    DeleteFileW(logPath.c_str());
    DeleteFileW(iniPath.c_str());
    // A plugin EDF6Coop replaces, as a player who had it still has it in Mods\Plugins.
    const std::wstring replaced = plugins + L"\\EDF6DirectNet.dll";
    DeleteFileW(replaced.c_str());
    DeleteFileW((replaced + L".disabled").c_str());
    if (mode == L"replaced") std::ofstream(std::filesystem::path(replaced), std::ios::binary) << "MZ not loaded";
    if (mode == L"off" || mode == L"optionsoff" || mode == L"on") {
        const char* ini = mode == L"off"           ? "[DirectNet]\r\nEnabled=0\r\n"
                          : mode == L"optionsoff" ? "[Resilience]\r\nKeepRoomOnPeerTimeout=0\r\n[Display]\r\nRadarItemColors=0\r\n"
                                                   : "[Log]\r\nCrashLog=1\r\n";
        std::ofstream(std::filesystem::path(iniPath), std::ios::binary | std::ios::trunc) << ini;
    }

    const HMODULE plugin = LoadLibraryW(dll.c_str());
    const auto load = plugin ? reinterpret_cast<Load>(GetProcAddress(plugin, "EML6_Load")) : nullptr;
    Check(load != nullptr, "plugin exports EML6_Load");
    if (!load) return 1;
    PluginInfo info{};
    const bool loaded = load(&info);
    const std::string log = ReadText(logPath);
    const auto guard = WeaponGuardHooks();

    if (mode == L"off") {
        Check(!loaded, "Enabled=0 asks the loader to unload");
        for (const auto& hook : guard) Check(SiteUntouched(base, hook), "Enabled=0 leaves the weapon guard sites alone");
        for (const auto& hook : PeerTimeoutHooks()) Check(SiteUntouched(base, hook), "Enabled=0 leaves Users::Add alone");
        for (const auto& call : PeerTimeoutCalls())
            Check(CallTargets(base + call.rva, call.rva, call.target), "Enabled=0 leaves the room-leave check alone");
        for (const auto& hook : HitAuthorityHooks()) Check(SiteUntouched(base, hook), "Enabled=0 leaves the hit sites alone");
        Check(CallTargets(base + RadarItemDotsCall().rva, RadarItemDotsCall().rva, RadarItemDotsCall().target),
              "Enabled=0 leaves the radar alone");
        Check(Contains(log, "Enabled=0"), "Enabled=0 is logged");
        FreeLibrary(plugin);  // what EDFModLoader does with a plugin that refuses
        Check(GetModuleHandleW(dll.c_str()) == nullptr, "the refused plugin is unloaded completely");
    } else {
        Check(loaded, "EML6_Load succeeds against the supported EDF.dll");
        Check(info.infoVersion == PluginInfo::MaxInfoVer && info.name && std::strcmp(info.name, "EDF6Coop") == 0,
              "PluginInfo names EDF6Coop");
        Check(info.version.major == EDF6COOP_VERSION_MAJOR && info.version.minor == EDF6COOP_VERSION_MINOR &&
                  info.version.patch == EDF6COOP_VERSION_PATCH && info.version.build == 0,
              "PluginInfo carries the project version");
        for (const auto& hook : guard) Check(HookedInto(base, hook, plugin), hook.name);
        // optionsoff turns off the patches a setting can turn off.
        const bool keepRoom = mode != L"optionsoff";
        for (const auto& hook : PeerTimeoutHooks())
            Check(keepRoom ? HookedInto(base, hook, plugin) : SiteUntouched(base, hook), hook.name);
        for (const auto& call : PeerTimeoutCalls())
            Check(keepRoom ? RedirectedInto(base + call.rva, plugin) : CallTargets(base + call.rva, call.rva, call.target),
                  call.name);
        Check(Contains(log, keepRoom ? "KeepRoomOnPeerTimeout=1:" : "KeepRoomOnPeerTimeout=0:"), "KeepRoomOnPeerTimeout is logged");
        const CallSite radar = RadarItemDotsCall();
        Check(keepRoom ? RedirectedInto(base + radar.rva, plugin) : CallTargets(base + radar.rva, radar.rva, radar.target),
              radar.name);
        Check(Contains(log, "Crash log armed"), "the crash log is armed");
        if (mode == L"fresh") {
            const std::string written = ReadIniText(iniPath);
            Check(ReadText(iniPath).rfind("\xFF\xFE", 0) == 0, "the INI is written as UTF-16LE with a BOM");
            Check(Contains(written, "[DirectNet]") && Contains(written, "[Resilience]") && Contains(written, "[Netcode]") &&
                      Contains(written, "[Display]") &&
                      Contains(written, "[Log]") && Contains(written, "CrashDump=0\r\n"),
                  "a missing INI is written with every section, crash dumps off");
        }
        if (mode == L"replaced") {
            Check(GetFileAttributesW(replaced.c_str()) == INVALID_FILE_ATTRIBUTES &&
                      GetFileAttributesW((replaced + L".disabled").c_str()) != INVALID_FILE_ATTRIBUTES,
                  "a replaced plugin is renamed .disabled");
        }
        if (mode == L"on") {
            ThrowAndCatch();
            LateFault(0x10);
            const std::string crash = ReadText(logPath);
            Check(Contains(crash, "EXCEPTION C0000005"), "access violation is logged");
            Check(Contains(crash, "writing 0000000000000010"), "fault address and access kind are logged");
            Check(Contains(crash, "#01 PluginLoadTests.exe+"), "call stack is unwound into the caller");
            Check(!Contains(crash, "E06D7363"), "a C++ exception thrown and caught outside EDF.dll is not reported");
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n----- log -----\n%s\n", failures, ReadText(logPath).c_str());
        return 1;
    }
    std::printf("mode %ls verified\n", mode.c_str());
    return 0;
}
