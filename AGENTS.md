# AI coding instructions

EDF6Coop.dll is an EDFModLoader plugin for EARTH DEFENSE FORCE 6 (PC / Steam). It fixes online desync caused by packet loss, holds dropped peers while EOS reconnects and adds an optional direct UDP link, in the game's rooms of four. Players install released builds by hand and update when they choose, so wire and config changes reach a mixed population.

The networking fixes for desync are the core of the project and get the most care. Everything else is secondary to keeping them correct and simple.

Linux via Steam Proton is a first-class platform, equal to Windows. The developer plays on it. Code that calls Windows APIs whose Wine implementation differs (adapter enumeration, socket ioctls, BCrypt, dbghelp, file replacement, UI over a fullscreen game) needs a path that works under Proton. Install steps and docs cover Proton alongside Windows, including the `WINEDLLOVERRIDES="winmm=n,b" %command%` launch option EDFModLoader needs.

## Layout

- `src/`: the transport part, formerly EDF6DirectNet. EOS import-table hooks (`eos_hooks.cpp`, `iat.*`), reliable sending and queue sizing, disconnect hold (`hold.*`), lobby markers, the direct link (`direct_net.*`, `wire.*`, `auth.*`, `reliable.*`, `fragment.*`, `congestion.*`, `netclass.*`), UPnP, config.
- `multislot/src/`: the game part, first written as EDF6MultiSlot by momotori01. In-memory patches to `EDF.dll` (weapon guard, peer timeout), the netcode version gate (`netfeature*`), player sync (`netplayer*`), hit authority (`hitauth*`), the crash log and the log. `plugin.cpp` is the EDFModLoader entry point for the whole DLL.
- `multislot/CMakeLists.txt` builds both parts into one DLL and holds the one version number (`project(EDF6Coop VERSION x.y.z)`).
- `tests/` covers the transport part; `multislot/tests/` covers the game part, and `multislot/tests/gamenet/` runs multi-process rooms against the real `EDF.dll` with a fake EOS.
- `docs/net-re/` holds reverse-engineering notes for the game's network code (Chinese).

## Build and test

- Two toolchains build the same DLL. Requirements for both are in README.md, "Build".
  - Windows: MSVC (Visual Studio 2022). `build.ps1 -Test` builds and runs ctest; release packages come from this build.
  - Linux: `build.sh -t` cross-builds with clang for MinGW-w64 (`cmake/mingw-w64.cmake`) and runs ctest under Wine in its own prefix. This is the everyday loop on the dev box, and the Proton check: every test, GameNet included, runs there.
- Keep code buildable by both. No `__try`/`__except` (MinGW compilers drop the handler): probes of game memory go through `Guarded`/`ReadGuarded` (`multislot/src/guard.h`). Wide text in a log line goes through `Utf8Text` (`src/utf8.h`), never `%ls`. Library types handed to the game are laid out by hand (`Shared`, `tests/gamenet/msvc_abi.h`), never the toolchain's own `std::` types.
- Set `EDF6_GAME_DIR` to the game folder (on the dev box `/mnt/kosmos/SteamLibrary/steamapps/common/EARTH DEFENSE FORCE 6`). Tests against the game's code (label `game`: `PluginLoad_*`, `GameNet_*`, `*GameCode`) report skipped (exit 77) without `EDF.dll`, so CI never runs them. `-RequireGame` / `-r` refuses to configure without it; release builds use it. Say so when reporting results from a build without the game.
- Only one `EDF.dll` build is supported (TimeDateStamp `0x678CCB46`). Every patch verifies original bytes before writing; keep it that way, and refuse cleanly when they do not match.
- A green ctest is not a live multiplayer test. Behaviour that only real play can confirm is reported as unverified.

## Test design

- Before adding a test, name the plausible defect, the production path it exercises and the observable result that would fail. Prefer the smallest existing fixture that reaches that path. Do not copy the implementation into a test helper or mock the behaviour under test.
- Cover state transitions, stale identities, reconnect and retry, malformed peer input, format boundaries and regressions. Transport and sync tests that exercise loss, reordering, host change and version mismatch are the most valuable tests in the repo.
- Skip tests that restate constants or defaults, pin source spelling, or check trivial wrappers. Delete such tests when you find them, with any helper they leave unused.

## Source style

- Less code is better code. KISS and DRY, KISS when they conflict.
- C++20, 4-space indentation, `/W4 /WX`. Match the surrounding file's naming and comment density.
- Avoid shallow helpers. A new function or wrapper needs several call sites, an invariant or lifetime it enforces, or domain logic that would obscure the caller.
- Comments state what the code cannot: game addresses and facts, protocol contracts, the reason behind a non-obvious choice. Do not restate the code.
- Wire format changes go through the netcode version gate (`multislot/src/netfeature.*`), since peers run mixed versions. Cleanup of what older releases left behind (`RetireReplacedPlugins`, `RemoveOldModFiles`) stays until a deliberate decision drops it.
- Untrusted input includes everything from peers, lobby attributes, the update server and host data files. Bounds-check it at the parse site.

## Documentation

- User-visible behaviour is documented in `README.md`, `README.ja.md`, `README.zh-CN.md` and the bundled `dist/README_EDF6Coop*.txt`. A change to settings, install steps or behaviour updates all language versions in the same change.
- Release text goes in `release-notes/<version>.md`. The release process is in README.md, "Release".
- Code reviews go in `docs/reviews/`, named `YYYY-MM-DD-<topic>.md`. Format: Scope/Excluded/Method header, `[C]`/`[A]` confidence markers, Critical/Major/Minor tiers with continuous numbering, a concrete failure scenario and `Fix shape:` per finding, a Cleared list, architecture notes.
- Transient agent output goes outside the repo or in an untracked `tmp/`.

## Commits

- Landing completed work on `main` and pushing it to `origin/main` is authorized. Do not request separate confirmation. The data-loss rules below still apply. Release tags and GitHub Releases are not covered: those wait for the developer.
- Auto-commit at your discretion after a meaningful unit of work lands.
- Pull at session start and at sensible intervals during long sessions (`git pull --rebase`). Resolve conflict fallout yourself, preserving both sides' work.
- Fast-forwards, merges, rebases and pushes are yours to run. The hard limit is data loss: if an operation would drop anyone's commit or uncommitted change (a force-push over work you do not hold, a rebase that discards a side, a hard reset over a dirty tree), stop and ask. When in doubt, merge so both sides survive.
- Line endings are set by `.gitattributes` (CRLF for `.bat`/`.cmd`, which `cmd.exe` needs). Never set `core.autocrlf`.
- Lowercase imperative subject, 50 to 70 chars, no trailing period. Subject-only by default; add a short body (1 to 3 bullets) only when the change spans areas or the reasoning is not obvious from the diff.
- No conventional-commit prefixes, emojis or scope tags. No trailers (no Co-Authored-By, no session footers).
