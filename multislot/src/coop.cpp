#include <windows.h>

#include "coop.h"

#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include "log.h"
#include "src/config.h"
#include "src/product.h"

namespace multislot {
namespace {

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// EDF6MultiSlot's EightPlayerRooms is RoomSize since 2.3.0 (hostmode.h); its ON was eight-player rooms.
void RenameOldSettings(std::vector<dn::IniValue>& values) {
    for (dn::IniValue& value : values) {
        if (_wcsicmp(value.section.c_str(), L"MultiSlot") != 0 || _wcsicmp(value.key.c_str(), L"EightPlayerRooms") != 0)
            continue;
        value.key = L"RoomSize";
        value.value = _wtoi(value.value.c_str()) ? L"8" : L"0";
    }
}

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

void PrepareSettings(const std::wstring& dir, const std::wstring& iniPath, const char* roomDefaults) {
    const std::wstring oldUpnp = dir + L"EDF6DirectNet.upnp", newUpnp = dir + coop::kNameW + L".upnp";
    if (Exists(oldUpnp) && !Exists(newUpnp)) MoveFileExW(oldUpnp.c_str(), newUpnp.c_str(), 0);
    if (Exists(iniPath)) return;

    std::string text = dn::mergeIniTexts(dn::defaultIni(GetUserDefaultUILanguage()), roomDefaults);
    std::vector<dn::IniValue> carried;
    for (const wchar_t* name : coop::kReplacedSettings) {
        std::vector<dn::IniValue> values = dn::readIniValues(dir + name);
        if (!values.empty()) Log("Settings: carrying %zu values over from %s", values.size(), Utf8Text(name).c_str());
        carried.insert(carried.end(), values.begin(), values.end());
    }
    RenameOldSettings(carried);
    std::vector<std::string> dropped;
    text = dn::applyIniValues(text, carried, &dropped);
    for (const std::string& line : dropped) Log("Settings: %s is no longer a setting; not carried over", line.c_str());
    if (dn::writeNewIniUtf16(iniPath, text))
        Log("Settings: wrote %s%s", Utf8Text(coop::kNameW).c_str(), Utf8Text(carried.empty() ? L".ini with the defaults"
                                                                     : L".ini (the old settings files are no longer read)").c_str());
    else
        Log("Settings: could not write %s.ini (error %lu); running on the defaults", Utf8Text(coop::kNameW).c_str(), GetLastError());
}

void ForwardDirectNetLine(const char* line) { Log("[DN] %s", line); }

}  // namespace multislot
