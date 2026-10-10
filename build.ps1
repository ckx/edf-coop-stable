# Builds EDF6Coop.dll (the direct link and the room part in one plugin) with CMake + Ninja + MSVC x64.
#   build.ps1 [-Test] [-CI] [-RequireGame]
# Lands in multislot\dist\ with the race-fixed EDFModLoader winmm.dll next to it. One build for every room
# size: the host picks the size in the game (F2, patches.h / hostmode.h).
# Set EDF6_GAME_DIR to the game folder (holds EDF.dll and Root.cpk; only read) for the menu and HUD assets and the
# tests against the game's code. -CI builds without the game's assets (placeholders; cannot be packaged).
# -RequireGame refuses to configure without EDF.dll, so no test against the game's code can pass as skipped.
param(
    [switch]$Test,
    [switch]$CI,
    [switch]$RequireGame
)
$ErrorActionPreference = 'Stop'
$multislot = Join-Path $PSScriptRoot 'multislot'
$buildDir = Join-Path $multislot 'build'

if (-not (Test-Path -LiteralPath (Join-Path $multislot 'third_party\EDFModLoader\winmm.dll'))) {
    python -B (Join-Path $multislot 'tools\fetch_modloader.py')
    if ($LASTEXITCODE) { throw "fetch_modloader.py failed ($LASTEXITCODE)" }
}
if (-not $CI -and -not (Test-Path -LiteralPath (Join-Path $multislot 'assets\LYT_MAINFRAME.SGO'))) {
    python -B (Join-Path $multislot 'tools\make_menu_label.py')
    if ($LASTEXITCODE) { throw "make_menu_label.py failed ($LASTEXITCODE); set EDF6_GAME_DIR to the game folder" }
}
if (-not $CI -and -not (Test-Path -LiteralPath (Join-Path $multislot 'assets\ONLINEHUDTEXTURE.RAB'))) {
    python -B (Join-Path $multislot 'tools\make_hud_colours.py')
    if ($LASTEXITCODE) { throw "make_hud_colours.py failed ($LASTEXITCODE); set EDF6_GAME_DIR to the game folder" }
}

$configure = "cmake -S `"$multislot`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
# Always explicit: CMake keeps a cached ON, and a later build without -CI would stay a placeholder build.
$configure += if ($CI) { ' -DMULTISLOT_CI=ON' } else { ' -DMULTISLOT_CI=OFF' }
if ($env:EDF6_GAME_DIR) { $configure += " `"-DEDF6_GAME_DIR=$env:EDF6_GAME_DIR`"" }
$configure += if ($RequireGame) { ' -DEDF6_REQUIRE_GAME=ON' } else { ' -DEDF6_REQUIRE_GAME=OFF' }
if ($env:LOADER_PYTHON) { $configure += " `"-DPython3_EXECUTABLE=$env:LOADER_PYTHON`"" }
$steps = @(
    "call `"$(Join-Path $multislot 'tools\msvc-x64-env.cmd')`" >nul",
    'set VSLANG=1033',
    $configure,
    "cmake --build `"$buildDir`""
)
if ($Test) { $steps += "ctest --test-dir `"$buildDir`" --output-on-failure --no-tests=error" }
cmd /c ($steps -join ' && ')
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
