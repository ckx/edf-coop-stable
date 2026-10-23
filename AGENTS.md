# AI coding instructions

EDF6Coop.dll is an EDFModLoader plugin for EARTH DEFENSE FORCE 6 (PC / Steam). It fixes online desync caused by packet loss, holds dropped peers while EOS reconnects and adds an optional direct UDP link, in the game's rooms of four. It also lets a downed player watch a standing teammate until revived (the spectator camera, formerly the EDF6Spectate plugin). Players install released builds by hand and update when they choose, so wire and config changes reach a mixed population.

The networking fixes for desync are the core of the project and get the most care. Everything else is secondary to keeping them correct and simple.

Linux via Steam Proton is a first-class platform, equal to Windows. The developer plays on it. Code that calls Windows APIs whose Wine implementation differs (adapter enumeration, socket ioctls, BCrypt, dbghelp, file replacement, UI over a fullscreen game) needs a path that works under Proton. Install steps and docs cover Proton alongside Windows, including the `WINEDLLOVERRIDES="winmm=n,b" %command%` launch option EDFModLoader needs.

## Layout

- `src/`: the transport part, formerly EDF6DirectNet. EOS import-table hooks (`eos_hooks.cpp`, `iat.*`), reliable sending and queue sizing, disconnect hold (`hold.*`), lobby markers, the direct link (`direct_net.*`, `wire.*`, `auth.*`, `reliable.*`), fragments (`fragment.*`), UPnP, config.
- `game/`: the game part, first written as EDF6MultiSlot by momotori01 (its namespace is still `multislot`). In-memory patches to `EDF.dll` (weapon guard, peer timeout), the spectator camera (`spectate.*`), the netcode version gate (`netfeature*`), player sync (`netplayer*`), hit authority (`hitauth*`), the crash log and the log. `plugin.cpp` is the EDFModLoader entry point for the whole DLL.
- `CMakeLists.txt` builds both parts into one DLL (in `out/`) and holds the one version number (`project(EDF6Coop VERSION x.y.z)`). `game/version.rc.in` puts it in the DLL's version resource.
- `tests/` covers the transport part; `tests/game/` covers the game part, and `tests/gamenet/` runs multi-process rooms against the real `EDF.dll` with a fake EOS.
- `dist/` is what the package ships besides the DLLs (install scripts, readmes, the loader fix note); `tools/` and `third_party/` make the race-fixed EDFModLoader.
- `docs/net-re/` holds reverse-engineering notes for the game's network code (Chinese). `docs/spectate/` holds the spectator camera's design, its evidence for every address (`research.md`) and its live checks (`plan.md`).

## Build and test

- Two toolchains build the same DLL. Requirements for both are in README.md, "Build".
  - Windows: MSVC (Visual Studio 2022). `build.ps1 -Test` builds and runs ctest; release packages come from this build.
  - Linux: `build.sh -t` cross-builds with clang for MinGW-w64 (`cmake/mingw-w64.cmake`) and runs ctest under Wine in its own prefix. This is the everyday loop on the dev box, and the Proton check: every test, GameNet included, runs there.
- Keep code buildable by both. No `__try`/`__except` (MinGW compilers drop the handler): probes of game memory go through `Guarded`/`ReadGuarded` (`game/guard.h`). Wide text in a log line goes through `Utf8Text` (`src/utf8.h`), never `%ls`. Library types handed to the game are laid out by hand (`Shared`, `tests/gamenet/msvc_abi.h`), never the toolchain's own `std::` types.
- Set `EDF6_GAME_DIR` to the game folder (on the dev box `/mnt/kosmos/SteamLibrary/steamapps/common/EARTH DEFENSE FORCE 6`). Tests against the game's code (label `game`: `PluginLoad_*`, `GameNet_*`, `*GameCode`) report skipped (exit 77) without `EDF.dll`, so CI never runs them. `-RequireGame` / `-r` refuses to configure without it; release builds use it. Say so when reporting results from a build without the game.
- Only one `EDF.dll` build is supported (TimeDateStamp `0x678CCB46`). Every patch verifies original bytes before writing; keep it that way, and refuse cleanly when they do not match.
- A green ctest is not a live multiplayer test. Behaviour that only real play can confirm is reported as unverified.

## Test design

- Skip stress tests by default, including during hotfixes. Run them only when needed to investigate a concrete stress-related defect, when explicitly requested, or for confirmed fixed release validation. The transport and log stress tests are `DirectNet` and `LogFileCap`; `build.sh -t` and `build.ps1 -Test` include them, so do not use those full-suite commands for routine verification. Build first, then run the remaining suite with `ctest --test-dir build-mingw -E '^(DirectNet|LogFileCap)$' --output-on-failure --timeout 600 -j4` under the test Wine prefix. On Windows, use the same exclusion with the Windows build directory and configuration.

- Before adding a test, name the plausible defect, the production path it exercises and the observable result that would fail. Prefer the smallest existing fixture that reaches that path. Do not copy the implementation into a test helper or mock the behaviour under test.
- Cover state transitions, stale identities, reconnect and retry, malformed peer input, format boundaries and regressions. Transport and sync tests that exercise loss, reordering, host change and version mismatch are the most valuable tests in the repo.
- Skip tests that restate constants or defaults, pin source spelling, or check trivial wrappers. Delete such tests when you find them, with any helper they leave unused.

## Source style

- Less code is better code. KISS and DRY, KISS when they conflict.
- C++20, 4-space indentation, `/W4 /WX`. Match the surrounding file's naming and comment density.
- Avoid shallow helpers. A new function or wrapper needs several call sites, an invariant or lifetime it enforces, or domain logic that would obscure the caller.
- Comments state what the code cannot: game addresses and facts, protocol contracts, the reason behind a non-obvious choice. Do not restate the code.
- Wire format changes go through the netcode version gate (`game/netfeature.*`), since peers run mixed versions. Cleanup of what older releases left behind (`RetireReplacedPlugins`, `RemoveOldModFiles`) stays until a deliberate decision drops it.
- Untrusted input includes everything from peers, lobby attributes, the update server and host data files. Bounds-check it at the parse site.

## Spectator camera

`game/spectate.*` switches view 0 of a downed online player to a camera following a standing teammate. Its behaviour is in `docs/spectate/design.md`.

- Scope (user decisions): the camera orbits the watched teammate and is aimed by the downed player's own look input (2026-10-22). It is on by default (`[Display] SpectateWhenDown=1`, 2026-10-22). Do not add a detached free-fly camera, watching while alive, offline or split-screen behaviour, network messages, more HUD widgets or unrelated patches without a new scope decision. The input for cycling teammates is an open decision (`docs/spectate/plan.md`).
- It sends nothing, so it needs no netcode version gate. Keep it that way.
- Prefer the game's own camera functions over writing camera or view fields; prove their calling conventions and ownership first. The orbit fields on remote soldiers are the one exception, with the evidence in `research.md`.
- Act on the main thread in the `System::Update` call it takes. Keep no game pointers between frames; find cameras and players again each frame. Never leave a camera current whose target has expired or left the room: the ghost camera's update reads a raw target pointer.
- Every range of game code it calls or relies on is checked by hash before the call is taken. A fault inside `Guarded` stops it for the session; it is never retried, since a recovered fault does not undo a partial game call.
- Live research reads the running game's memory (`tools/inspect_camera.py`) and never writes to it.

## Documentation

- User-visible behaviour is documented in English only, in `README.md` and the bundled `dist/README_EDF6Coop.txt` (the user dropped the Chinese and Japanese readmes in 3.1.0). A change to settings, install steps or behaviour updates both in the same change. The INI comments (`src/config.cpp`) and installer messages stay in Chinese, Japanese and English.
- Release text goes in `release-notes/<version>.md`. The release process is in README.md, "Release".
- Code reviews go in `docs/reviews/`, named `YYYY-MM-DD-<topic>.md`. Format: Scope/Excluded/Method header, `[C]`/`[A]` confidence markers, Critical/Major/Minor tiers with continuous numbering, a concrete failure scenario and `Fix shape:` per finding, a Cleared list, architecture notes.
- Transient agent output goes outside the repo or in an untracked `tmp/`.

## Commits

- Publish every build delivered to the developer on GitHub Releases, including diagnostic and experimental builds, so it can be distributed to other players. Upload a complete installable package and its SHA-256 checksum, and provide the release URL. Diagnostic builds use a clearly named prerelease and must not replace the latest stable release. A local DLL path or a source-only push is not delivery. Creating the required tags and publishing these builds is authorized; do not ask for separate confirmation.
- Every released DLL carries its version in its version resource (Explorer, Properties, Details), diagnostic and experimental builds included. Bump `project()` for each release rather than reusing a published version. `package.ps1` refuses a DLL whose resource or exported marker does not match; do not bypass it.
- After publishing any release, including diagnostic prereleases, install the published package locally in `/mnt/kosmos/SteamLibrary/steamapps/common/EARTH DEFENSE FORCE 6`. Back up the installed plugin, preserve settings and other mods, and verify the installed DLL against the published package's SHA-256. If EDF6 is running, wait for it to close before installing.
- Landing completed work on `main` and pushing it to `origin/main` is authorized. Do not request separate confirmation. The data-loss rules below still apply.
- Auto-commit at your discretion after a meaningful unit of work lands.
- Pull at session start and at sensible intervals during long sessions (`git pull --rebase`). Resolve conflict fallout yourself, preserving both sides' work.
- Fast-forwards, merges, rebases and pushes are yours to run. The hard limit is data loss: if an operation would drop anyone's commit or uncommitted change (a force-push over work you do not hold, a rebase that discards a side, a hard reset over a dirty tree), stop and ask. When in doubt, merge so both sides survive.
- Line endings are set by `.gitattributes` (CRLF for `.bat`/`.cmd`, which `cmd.exe` needs). Never set `core.autocrlf`.
- Lowercase imperative subject, 50 to 70 chars, no trailing period. Subject-only by default; add a short body (1 to 3 bullets) only when the change spans areas or the reasoning is not obvious from the diff.
- No conventional-commit prefixes, emojis or scope tags. No trailers (no Co-Authored-By, no session footers).
