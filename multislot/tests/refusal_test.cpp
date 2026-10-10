// A plugin that refuses is unloaded at once: EDFModLoader calls FreeLibrary as soon as EML6_Load returns false
// (its dllmain.cpp). 1.5.13 had already started the log's writer thread with the first line, and that thread
// woke up inside the unmapped DLL about 200 ms later and took the game down. This loads the built plugin the way
// the loader does, without the game: with no EDF.dll in the process it refuses before touching any file.
// (Enabled=0 against the real game is PluginLoad_off.)
//   RefusalTests EDF6Coop.dll work-folder
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#pragma warning(push)
#pragma warning(disable : 4201)
#include "PluginAPI.h"
#pragma warning(pop)

#include "../src/log.h"

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

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

using Load = bool(EDFMLAPI*)(PluginInfo*);
using QueryThreadFn = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
constexpr int kThreadQuerySetWin32StartAddress = 9;

// Threads of this process that started in `module`'s code.
int ThreadsStartedIn(HMODULE module) {
    const auto query = reinterpret_cast<QueryThreadFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    const std::uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    int found = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        std::uintptr_t start = 0;
        if (query && query(thread, kThreadQuerySetWin32StartAddress, &start, sizeof(start), nullptr) == 0 && start >= base &&
            start < end)
            ++found;
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return found;
}

// One load as EDFModLoader does it: LoadLibrary, EML6_Load, and FreeLibrary when it says no.
void LoadAndRefuse(const std::wstring& dll, const char* mode) {
    const HMODULE plugin = LoadLibraryW(dll.c_str());
    const auto load = plugin ? reinterpret_cast<Load>(reinterpret_cast<void*>(GetProcAddress(plugin, "EML6_Load"))) : nullptr;
    Check(load != nullptr, "the plugin loads and exports EML6_Load");
    if (!load) return;
    PluginInfo info{};
    Check(!load(&info), mode);
    Check(info.version.major == MULTISLOT_VERSION_MAJOR && info.version.minor == MULTISLOT_VERSION_MINOR &&
              info.version.patch == MULTISLOT_VERSION_PATCH && info.version.build == 0,
          "PluginInfo carries CMakeLists.txt's project version");
    Check(ThreadsStartedIn(plugin) == 0, "a plugin that refuses leaves no thread of its own running");
    FreeLibrary(plugin);
    Check(GetModuleHandleW(dll.c_str()) == nullptr, "and nothing keeps it loaded after the loader lets go");
    // 1.5.13's writer thread faulted in the unmapped code within one 200 ms drain interval; give it three.
    Sleep(600);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: RefusalTests EDF6Coop.dll work-folder\n");
        return 2;
    }
    if (GetModuleHandleW(L"EDF.dll")) {
        std::printf("FAIL: this test needs a process without EDF.dll\n");
        return 1;
    }
    {
        const std::wstring folder = std::wstring(argv[2]) + L"\\refused";
        const std::wstring plugins = folder + L"\\Mods\\Plugins";
        for (const auto& path : {std::wstring(argv[2]), folder, folder + L"\\Mods", plugins}) CreateDirectoryW(path.c_str(), nullptr);
        const std::wstring dll = plugins + L"\\EDF6Coop.dll";
        const std::wstring log = plugins + L"\\EDF6Coop.log";
        const std::wstring ini = plugins + L"\\EDF6Coop.ini";
        const std::wstring replaced = plugins + L"\\EDF6MultiSlot.dll";
        CopyFileW(argv[1], dll.c_str(), FALSE);
        for (const auto& path : {log, log + multislot::kLogQueueSuffix, ini, replaced, replaced + L".disabled"})
            DeleteFileW(path.c_str());
        std::ofstream(std::filesystem::path(replaced), std::ios::binary | std::ios::trunc) << "MZ not loaded";
        const char* mode = "without EDF.dll the plugin refuses";

        LoadAndRefuse(dll, mode);
        std::string text = ReadText(log);
        Check(Contains(text, "REFUSED: EDF.dll is not loaded"), "the reason is logged");
        Check(GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES, "outside the game no settings file is written");
        Check(GetFileAttributesW(replaced.c_str()) != INVALID_FILE_ATTRIBUTES, "and no replaced plugin is renamed");
        const HANDLE queue = CreateFileW((log + multislot::kLogQueueSuffix).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(queue != INVALID_HANDLE_VALUE, "the unloaded plugin holds nothing open (its .queue file is free)");
        if (queue != INVALID_HANDLE_VALUE) CloseHandle(queue);
    }
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("a refusing plugin unloads cleanly\n");
    return 0;
}
