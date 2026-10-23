#!/usr/bin/env bash
# EDF6Coop installer for Linux (Steam Proton): finds the EARTH DEFENSE FORCE 6 folder in the Steam libraries and
# copies the plugin in, as INSTALL.bat does on Windows.
#   bash install.sh [game folder]              install
#   bash install.sh --uninstall [game folder]  remove the plugin, its settings and its log
# The game then needs the launch option  WINEDLLOVERRIDES="winmm=n,b" %command%  (Steam: EDF6, Properties).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
uninstall=0
if [ "${1:-}" = "--uninstall" ]; then
    uninstall=1
    shift
fi
# SHA-256 of the official EDFModLoader v1.0.10 winmm.dll (it has a multithread bug; the package ships a fixed build).
official=b80e4da6ae7264f0e9774c992de3c5f9b6e9bc9422146adeeb5bb2bd681e112e

find_game() {
    for d in "$here" "$(dirname "$here")"; do
        [ -f "$d/EDF6.exe" ] && { echo "$d"; return; }
    done
    for steam in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        vdf=$steam/steamapps/libraryfolders.vdf
        libs=$steam
        [ -f "$vdf" ] && libs="$libs"$'\n'"$(sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"/\1/p' "$vdf")"
        while IFS= read -r lib; do
            d="$lib/steamapps/common/EARTH DEFENSE FORCE 6"
            [ -f "$d/EDF6.exe" ] && { echo "$d"; return; }
        done <<< "$libs"
    done
}

game=${1:-$(find_game)}
if [ -z "$game" ] || [ ! -f "$game/EDF6.exe" ]; then
    echo "No EDF6.exe found${game:+ in $game}. In Steam right-click EDF6, Manage, Browse local files, and run:"
    echo "  bash install.sh \"<that folder>\""
    exit 1
fi
echo "Game folder: $game"
if pgrep -x EDF6.exe >/dev/null 2>&1; then
    echo "Quit the game first, then run this again."
    exit 1
fi
plugins=$game/Mods/Plugins
files=(Mods/Plugins/EDF6Coop.dll README_EDF6Coop.txt EDFModLoader_LICENSE.txt)

if [ $uninstall = 1 ]; then
    # The loader's license stays with the loader.
    for f in "${files[@]}"; do [ "$f" = EDFModLoader_LICENSE.txt ] || rm -f "$game/$f"; done
    # As UNINSTALL.bat: settings, logs, the crash dump, the UPnP records and what the auto-updater of 2.5.0 and older
    # left next to the DLL.
    rm -f "$plugins"/EDF6Coop.ini "$plugins"/EDF6Coop.log "$plugins"/EDF6Coop.log.1 "$plugins"/EDF6Coop.log.queue \
          "$plugins"/EDF6Coop-crash.dmp "$plugins"/EDF6Coop.upnp "$plugins"/EDF6DirectNet.upnp \
          "$plugins"/EDF6Coop.dll.{old,trial,bad,rolledback} "$plugins"/EDF6Coop.dll.new*
    echo "EDF6Coop removed. EDFModLoader and other mods were left untouched."
    echo "A UPnP mapping the plugin made stays on the router until a game start that does not host; you can also"
    echo "delete the entry named EDF6DirectNet in the router's port-forwarding page."
    exit 0
fi

for f in "${files[@]}"; do
    [ "$here/$f" -ef "$game/$f" ] && continue  # the zip was extracted straight into the game folder
    mkdir -p "$(dirname "$game/$f")"
    cp -f "$here/$f" "$game/$f"
done
# The plugins EDF6Coop replaces would hook the same game code: renamed, not deleted.
for old in EDF6DirectNet.dll EDF6MultiSlot.dll; do
    [ -f "$plugins/$old" ] || continue
    mv -f "$plugins/$old" "$plugins/$old.disabled"
    echo "Disabled the replaced plugin (renamed to .disabled): $old"
done
# What EDF6Spectate put next to the game, and the translated readmes of EDF6Coop 3.0.4 and earlier.
rm -f "$game"/README_EDF6Spectate.txt "$game"/EDF6Coop_GUARD_LICENSE.txt "$game"/EDF6Coop_TOOLS_LICENSE.txt \
      "$game"/README_EDF6Coop_zh.txt "$game"/README_EDF6Coop_ja.txt
# EDF6Spectate is part of EDF6Coop and has no settings to keep: its DLL, the .disabled copy EDF6Coop 3.1.0 left and
# its log are deleted.
spectate=0
for f in EDF6Spectate.dll EDF6Spectate.dll.disabled EDF6Spectate.log; do
    [ -f "$plugins/$f" ] || continue
    rm -f "$plugins/$f"
    spectate=1
done
[ $spectate = 1 ] && echo "Uninstalled the standalone EDF6Spectate (the spectator camera is part of EDF6Coop)."
echo "EDF6Coop installed."
# EDFModLoader: installed when missing; an existing winmm.dll is replaced only when it is the official v1.0.10 one.
if [ ! -f "$game/winmm.dll" ]; then
    cp "$here/winmm.dll" "$game/winmm.dll"
    echo "Installed EDFModLoader (v1.0.10, fixed build, MIT license, see EDFModLoader_LICENSE.txt)."
elif [ "$(sha256sum "$game/winmm.dll" | cut -d' ' -f1)" = "$official" ] && ! [ "$here/winmm.dll" -ef "$game/winmm.dll" ]; then
    [ -f "$game/winmm.dll.bak-official" ] || cp "$game/winmm.dll" "$game/winmm.dll.bak-official"
    cp -f "$here/winmm.dll" "$game/winmm.dll"
    echo "Replaced the official v1.0.10 EDFModLoader with the fixed build; the original is winmm.dll.bak-official."
else
    echo "Found an existing EDFModLoader (winmm.dll); left it as is."
fi
echo
echo "Now set the game's launch options in Steam (right-click EDF6, Properties) to:"
echo "  WINEDLLOVERRIDES=\"winmm=n,b\" %command%"
echo "The first start creates Mods/Plugins/EDF6Coop.ini (settings) and EDF6Coop.log (log)."
