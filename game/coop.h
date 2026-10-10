// What EDF6Coop does as one plugin before either part starts: it takes over from EDF6DirectNet.dll and
// EDF6MultiSlot.dll, and it keeps one log for both parts.
#pragma once

#include <string>

namespace multislot {

// EDF6DirectNet.dll and EDF6MultiSlot.dll in `dir` (the plugin folder, trailing backslash) are renamed to
// <name>.disabled, so they no longer load. EDFModLoader loads Mods\Plugins in name order and EDF6Coop.dll
// comes first, so this normally happens before it reaches them. False when one of them is loaded in this game
// already or could not be renamed: running next to it would hook the same game code twice, so EDF6Coop must
// then stay off (the log says what to do).
bool RetireReplacedPlugins(const std::wstring& dir);

// Deletes the menu layout and HUD textures that 2.5.0 and earlier wrote under Mods (Mods\UI\LYT_MAINFRAME.SGO,
// Mods\HUD\ONLINEHUDTEXTURE.RAB) for the bigger rooms, which EDFModLoader would otherwise keep loading. Only a file
// whose size and FNV-1a are one an earlier release wrote: another mod's file of the same name stays. `dir` as above.
void RemoveOldModFiles(const std::wstring& dir);

// The direct-link part's log lines go into this log (dn::logToSink), marked "[DN] ".
void ForwardDirectNetLine(const char* line);

}  // namespace multislot
