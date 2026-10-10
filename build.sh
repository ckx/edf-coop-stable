#!/usr/bin/env bash
# Builds EDF6Coop.dll and its tests on Linux (clang for MinGW-w64, cmake/mingw-w64.cmake); -t runs them under Wine.
#   ./build.sh [-t] [-r]
# Lands in out/ like build.ps1. Set EDF6_GAME_DIR to the game folder (holds EDF.dll; only read) for the
# tests against the game's code; without it those tests report themselves skipped. -r refuses to configure without
# EDF.dll, so no test against the game's code can pass as skipped.
# Fedora: dnf install clang lld mingw64-gcc-c++ mingw64-winpthreads-static wine cmake ninja-build python3
# The tests run in their own Wine prefix (EDF6COOP_WINEPREFIX, default ~/.local/share/wine-edf6coop-tests).
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
build=$root/build-mingw
test=0
require=OFF
while getopts tr option; do
    case $option in
        t) test=1 ;;
        r) require=ON ;;
        *) sed -n '2,7p' "$0"; exit 2 ;;
    esac
done

# The loader fix: pefile and capstone pinned by hash (as in CI).
venv=$root/.venv
if [ ! -x "$venv/bin/python" ]; then
    python3 -m venv "$venv"
    "$venv/bin/pip" install -q --disable-pip-version-check --require-hashes -r "$root/.github/loader-fix-requirements.txt"
fi
python=$venv/bin/python

"$python" -B "$root/tools/fetch_modloader.py"

# Always explicit: CMake keeps cached values.
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEDF6_REQUIRE_GAME=$require \
    -DPython3_EXECUTABLE="$python" ${EDF6_GAME_DIR:+-DEDF6_GAME_DIR="$EDF6_GAME_DIR"}
cmake --build "$build"

if [ $test = 1 ]; then
    export WINEPREFIX=${EDF6COOP_WINEPREFIX:-$HOME/.local/share/wine-edf6coop-tests}
    export WINEDEBUG=-all
    if [ ! -d "$WINEPREFIX" ]; then
        wineboot -i >/dev/null 2>&1
        # A test that crashes ends instead of waiting in Wine's debugger.
        wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null
        wine reg delete 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' /v Debugger /f >/dev/null 2>&1 || true
    fi
    ctest --test-dir "$build" --output-on-failure --no-tests=error --timeout 600 -j4
fi
