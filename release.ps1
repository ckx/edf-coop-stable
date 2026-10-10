# Builds, tests and packages the tagged version and stages the release files:
#   release.ps1 [-Upload]
# Runs on a machine with the game (EDF6_GAME_DIR): every test against the game's code must run, not skip.
# Without -Upload it stops after staging release\upload-<version>\. With -Upload it creates a DRAFT Release
# for the tag with those files (gh, tag already pushed); the developer publishes it.
# Keep this script ASCII: PowerShell 5.1 reads it as ANSI.
param([switch]$Upload)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$multislot = Join-Path $root 'multislot'

$cmake = Get-Content -LiteralPath (Join-Path $multislot 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(EDF6Coop VERSION (\d+\.\d+\.\d+)[\s)]') { throw 'project(EDF6Coop VERSION x.y.z) not found in multislot\CMakeLists.txt' }
$version = $Matches[1]
$tag = "v$version"
$notes = Join-Path $root "release-notes\$version.md"
if (-not (Test-Path -LiteralPath $notes)) { throw "missing release-notes\$version.md: write the release text first" }

# gh picks its repository from the current directory, not from $root: name it explicitly, from $root's origin.
$origin = git -C $root remote get-url origin
if ($LASTEXITCODE) { throw 'git remote get-url origin failed' }
if ($origin -notmatch 'github\.com[:/]([^/]+/[^/]+?)(\.git)?/?$') { throw "origin is not a GitHub repository: $origin" }
$repo = $Matches[1]

# What is built must be exactly the tagged commit.
$dirty = git -C $root status --porcelain --untracked-files=no
if ($LASTEXITCODE) { throw 'git status failed' }
if ($dirty) { throw "uncommitted changes; release only a committed tree:`n$($dirty -join "`n")" }
$head = git -C $root rev-parse HEAD
$tagged = git -C $root rev-parse --verify --quiet "$tag^{commit}"
if (-not $tagged) { throw "tag $tag does not exist; tag the release commit first" }
if ($tagged -ne $head) { throw "HEAD $head is not the commit of $tag ($tagged)" }

$stage = Join-Path $root "release\upload-$version"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

& (Join-Path $root 'build.ps1') -Test -RequireGame
& (Join-Path $root 'package.ps1') | Out-Null
$zip = Join-Path $root "release\EDF6Coop-$version.zip"
Copy-Item -LiteralPath $zip, "$zip.sha256" -Destination $stage

$files = @(Get-ChildItem -LiteralPath $stage -File | Sort-Object Name)
$files | ForEach-Object { '{0,-36} {1,10}  {2}' -f $_.Name, $_.Length, (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
if (-not $Upload) {
    "Staged in $stage. Re-run with -Upload to create the draft Release $tag."
    return
}

gh release create $tag @($files.FullName) --repo $repo --draft --verify-tag --title "EDF6Coop $version" --notes-file $notes
if ($LASTEXITCODE) { throw 'gh release create failed' }
"Draft Release $tag created in $repo. Check it and publish it on GitHub."
