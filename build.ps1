# Builds EDF6Coop.dll with CMake + Ninja + MSVC x64.
#   build.ps1 [-Test] [-RequireGame]
# Lands in out\ with the race-fixed EDFModLoader winmm.dll next to it.
# Set EDF6_GAME_DIR to the game folder (holds EDF.dll; only read) for the tests against the game's code.
# -RequireGame refuses to configure without EDF.dll, so no test against the game's code can pass as skipped.
param(
    [switch]$Test,
    [switch]$RequireGame
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build'

if (-not (Test-Path -LiteralPath (Join-Path $root 'third_party\EDFModLoader\winmm.dll'))) {
    python -B (Join-Path $root 'tools\fetch_modloader.py')
    if ($LASTEXITCODE) { throw "fetch_modloader.py failed ($LASTEXITCODE)" }
}

$configure = "cmake -S `"$root`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
if ($env:EDF6_GAME_DIR) { $configure += " `"-DEDF6_GAME_DIR=$env:EDF6_GAME_DIR`"" }
# Always explicit: CMake keeps a cached ON.
$configure += if ($RequireGame) { ' -DEDF6_REQUIRE_GAME=ON' } else { ' -DEDF6_REQUIRE_GAME=OFF' }
if ($env:LOADER_PYTHON) { $configure += " `"-DPython3_EXECUTABLE=$env:LOADER_PYTHON`"" }
$steps = @(
    "call `"$(Join-Path $root 'tools\msvc-x64-env.cmd')`" >nul",
    'set VSLANG=1033',
    $configure,
    "cmake --build `"$buildDir`""
)
if ($Test) { $steps += "ctest --test-dir `"$buildDir`" --output-on-failure --no-tests=error" }
cmd /c ($steps -join ' && ')
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
