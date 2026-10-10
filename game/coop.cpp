#include <windows.h>

#include "coop.h"

#include <cstdint>
#include <string>
#include <vector>

#include "log.h"
#include "src/product.h"

namespace multislot {
namespace {

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

}  // namespace

bool RetireReplacedPlugins(const std::wstring& dir) {
    bool clear = true;
    for (const wchar_t* name : coop::kReplacedPlugins) {
        const std::wstring path = dir + name, aside = path + L".disabled";
        const bool loaded = GetModuleHandleW(name) != nullptr;
        if (!Exists(path)) {
            if (loaded) {  // loaded from elsewhere, which is just as bad
                Log("REFUSED: %s is running in this game, so EDF6Coop stays off (both would hook the same game "
                    "code). Remove it", Utf8Text(name).c_str());
                clear = false;
            }
            continue;
        }
        if (!MoveFileExW(path.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            Log("REFUSED: %s is still in Mods\\Plugins and could not be renamed (error %lu), so EDF6Coop stays off "
                "(both would hook the same game code). Delete %s by hand; EDF6Coop replaces it",
                Utf8Text(name).c_str(), GetLastError(), Utf8Text(name).c_str());
            clear = false;
            continue;
        }
        if (loaded) {
            Log("REFUSED: %s was loaded before EDF6Coop in this game, so EDF6Coop stays off this time (both would "
                "hook the same game code). It is renamed to %s.disabled and EDF6Coop runs from the next start",
                Utf8Text(name).c_str(), Utf8Text(name).c_str());
            clear = false;
        } else {
            Log("Replaced plugin: %s renamed to %s.disabled (EDF6Coop does what it did)", Utf8Text(name).c_str(), Utf8Text(name).c_str());
        }
    }
    return clear;
}

void RemoveOldModFiles(const std::wstring& dir) {
    static const struct {
        const wchar_t* path;  // under Mods
        std::uint64_t size, fnv1a;
    } kOld[] = {{L"UI\\LYT_MAINFRAME.SGO", 7098, 0xDA755A27A8E88468ull}, {L"HUD\\ONLINEHUDTEXTURE.RAB", 205334, 0x1CD5561D661F20BBull}};
    for (const auto& old : kOld) {
        const std::wstring path = dir + L"..\\" + old.path;
        WIN32_FILE_ATTRIBUTE_DATA about{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &about) || about.nFileSizeHigh ||
            about.nFileSizeLow != old.size)
            continue;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        std::vector<unsigned char> bytes(static_cast<std::size_t>(old.size));
        DWORD read = 0;
        const bool ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
        CloseHandle(file);
        std::uint64_t hash = 0xCBF29CE484222325ull;
        for (unsigned char b : bytes) hash = (hash ^ b) * 0x100000001B3ull;
        if (!ok || hash != old.fnv1a) continue;
        if (DeleteFileW(path.c_str())) {
            RemoveDirectoryW(path.substr(0, path.find_last_of(L'\\')).c_str());  // only when that leaves it empty
            Log("Removed Mods\\%s, which an earlier EDF6Coop wrote for its bigger rooms", Utf8Text(old.path).c_str());
        } else {
            Log("Mods\\%s, which an earlier EDF6Coop wrote for its bigger rooms, could not be deleted (error %lu)",
                Utf8Text(old.path).c_str(), GetLastError());
        }
    }
}

void ForwardDirectNetLine(const char* line) { Log("[DN] %s", line); }

}  // namespace multislot
