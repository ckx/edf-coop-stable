# Code base review, 2026-10-10

Review date: 2026-10-10.

**Scope:** the whole repository at `a4883879930d49881ce2817724aa9f35886cfb98` (`main`, clean apart from the new AGENTS.md and CLAUDE.md). Production C++ in `src/` and `multislot/src/`, tests, CMake, PowerShell build/package/release/sign scripts, CI workflows, install scripts in `dist/`, READMEs, release notes and `docs/`. Priority areas: the desync networking fixes (reliable upgrade, queue sizing, disconnect hold, direct link, mission start sync, netcode gate) and Linux via Steam Proton as a first-class platform.

**Excluded:** no build and no test run (this checkout is on Linux; the build needs MSVC, and no clang-cl cross toolchain or Windows SDK is set up). No live multiplayer play. No disassembly of `EDF.dll` beyond a count of ymm instructions; claims about game functions rest on `docs/net-re/` and source comments. No check of Wine source; Wine behaviour is marked `[A]`. The companion mod EDF6VehicleCrew (All Forces) was not reviewed.

**Method:** seven parallel reviewers, each tracing production paths in one area (transport hooks, direct link, lobby/extension/updater, room sync glue, netcode rewrite modules, game patches, build/tests/docs), with a Proton lens added to each. The lead re-read the source behind every Major finding and several Minor ones before including it. Line numbers refer to the reviewed revision.

`[C]` means the path is established from source. `[A]` means a material assumption remains (Wine behaviour, game behaviour, EOS behaviour); the assumption is named. Neither marker claims a reproduction.

## Resolution (same day)

Work that followed this review, by commit. The owner chose to keep only the networking fixes: rooms of four, the
reliable upgrade, the disconnect hold, the direct link as a star around the host, the netcode gate with player sync
and hit authority, the weapon guard, the peer-timeout fix and the crash log. Everything else was removed (48dbc93).
Line references below are to the reviewed revision and no longer match.

| Finding | Status |
|---|---|
| 1, 2 | Fixed (6efffa8). A link carries game data only while its peer answers within 1.5 s plus its round trip; what it had not delivered goes over EOS, and a host returns relay data it cannot deliver. A joiner's link comes up only on the host's first authentic datagram. GameNet `directoutage` covers it end to end. Crypto primitives and the link key derivation have known-answer tests. |
| 3, 4, 5, 6, 7, 8, 11, 12, 13, 14 | Gone with the feature: traffic classes, compression, ExtraEnemies, Extend, rooms above four, member slots, All Forces, the updater. |
| 9, 18, 19, 20 | Fixed (6efffa8): held closes are absorbed while held, reconnects back off to 30 s while the direct link is alive, a retired transport hands its inbox to the game and its undelivered data to EOS, AutoJoin keeps a working link when the host adds an address. |
| 10 | Fixed (67f283b): a host advertises the addresses the routing table sends from when the adapter list does not name a physical adapter. |
| 15 | Fixed (b490bc7). |
| 16 | Fixed (48dbc93, 67f283b): Proton install steps in every readme, `dist/install.sh`. |
| 17, 38, 40, 45 | Fixed (bb86d7f). |
| 21 | Open [A]. Data the direct link hands to EOS is sent from the game's own send call; reconnect requests and lobby calls still run on the EOS tick. |
| 22, 41, 42, 43, 46, 58, 66 | Open [A]. |
| 23-27, 29, 30, 32-37, 39, 44, 47-52, 54, 55, 57, 59-62, 68, 70, 73 | Gone with the feature. |
| 24, 28, 31 | Fixed (6efffa8): every direct-link datagram is tracked, completed fragments are forgotten oldest first, fragments go unordered. |
| 53 | Fixed (2d69c87): the crash log leaves faults inside `Guarded` alone. |
| 56 | Fixed (6efffa8): an unknown `Mode`, `Relay` or `HoldDisconnects` value is warned about. |
| 63, 64, 65, 69, 71 | Fixed (67f283b and the layout commit): pinning and socket buffer limits are logged, a UPnP router that does not say its WAN address is warned about, `package.ps1` runs under pwsh on Linux, the firewall rule is added before the old one goes. |
| 67, 72 | Fixed (48dbc93, 67f283b): the old menu and HUD files are deleted when ours, uninstall keeps the loader's license, the installer comment says what it does. |

The layout follows a smaller version of the proposal below: `CMakeLists.txt` at the root, `src/` the transport,
`game/` the game part, `tests/`, `tests/game/`, `tests/gamenet/`, `tools/`, `third_party/`, `dist/`, build output in
`out/`. `src/eos_hooks.cpp` is about 1,300 lines and was not split.

## Summary

The core fix is sound. `hookSendPacket` upgrades unreliable non-state datagrams to `ReliableUnordered` (`src/eos_hooks.cpp:2449-2465`), queues are enlarged without shrinking an unlimited queue, and held peers get `bAllowDelayedDelivery`. The reliable layer, fragment parsing, wire decoding and handshake crypto all hold up under review.

The risk sits in what was layered on top since 2.0:

- The direct link has no health gate on a single path, so a link that stops delivering swallows all game traffic for up to 60 s while reporting success (finding 1).
- Runtime-learned "state" record types bring back unrepaired loss, which is what the plugin was written to remove (finding 3).
- Several settings that change the simulation are per-machine INI values that the room never compares (findings 5, 6, 7).
- Capabilities are published from INI flags even when the feature failed to start. Under Proton this can break compression for everyone sending to a Proton player (finding 4).
- About 49 tests that exercise real game code, including every multi-process room test, never run in CI, and the release script passes when they skip (finding 15).

Proton has no install path and no documentation (finding 16). Several Windows APIs the plugin depends on behave differently or are unverified under Wine (findings 4, 10, 13, 14 and the Proton notes).

Roughly 2,000 lines of C++ can go without behaviour change (dead code, diagnostics, duplicated EOS declarations, test-only production hooks, CMake repetition). Another 2,500 or so can go if the owner drops features that do not earn their complexity (RNG sync, interest management, smoothing, DummyMembers, handshake diagnostics, the log queue file, the EDF6VR update check, pre-2.3 migrations).

## Critical

None found.

## Major

### 1. [C] A single-path direct link that stops delivering keeps all traffic for up to 60 s

**Location:** `src/direct_net.cpp:427-436` (`canRoute`), `:558-566` (`routeFor` for the host and for a joiner's host link), `:320` (`usable`), `:1017` (`lastRecvMs` updated by any authentic datagram); caller `src/eos_hooks.cpp:2437-2447`.

Host-to-joiner and joiner-to-host routing only asks `usable()`, which is `up && !overloaded`. Mesh routes use `healthy()`, single paths do not. The link closes after `linkTimeoutMs` of silence (default 60 s). Liveness counts any inbound datagram, including the peer's own pings, so a one-way failure is invisible to the side whose sends are lost. While the link is "usable", `hookSendPacket` returns `EOS_Success` for every datagram it took, so nothing falls back to EOS and its reliable upgrade.

**Failure scenario:** the host's UPnP mapping drops, or the joiner's route to the host's address blackholes while EOS still works. For up to 60 s every game datagram to that peer is lost. The game disbands after about 26 s of undeliverable data (`src/eos_hooks.cpp:2428-2436`). In the one-way case the host keeps pinging, the joiner's link looks alive, and the outage lasts about 120 s.

**Fix shape:** track `lastAnswerMs`, updated only by Pong/Ack to our own probes. Make `usable`/`canRoute` false once it is older than about 1.5 s, so traffic falls back to EOS, and restore after the existing `kPathRecoverMs` hysteresis. Use one health notion for single, relay and mesh paths.

### 2. [C] A joiner routes game data over the host link before the link keys are proven

**Location:** `src/direct_net.cpp:1467` (`link.up = true` in `onClientWelcome`); compare `:1268-1275`, where the host waits for an authentic datagram.

After a welcome verifies, the joiner sends all game traffic to the host link at once. If the two sides derived different link keys, every datagram fails the tag check on the other side. Combined with finding 1, the joiner blackholes for the link timeout, reconnects and repeats. One plausible trigger is a difference between Wine's bcrypt (GnuTLS backend) and Windows CNG in `BCryptSecretAgreement` with `BCryptDeriveKey(BCRYPT_KDF_HASH)` `[A]`.

**Failure scenario:** a Proton host and a Windows joiner, or the reverse, with any key-derivation difference. Repeated 60 s outages with no fallback.

**Fix shape:** set `up` only on the first authentic datagram from the host, and have the joiner ping right after the welcome as `onPeerWelcome` does (`:1910-1913`). Add a fixed-vector known-answer test for ECDH, `deriveLinkKeys` and the HMAC, and log a startup self-test result so Proton logs show it.

### 3. [A] Runtime-learned state classes remove repair for periodic record types

**Location:** `src/netclass.cpp:72-104` (`StateLearner::observe`), `:106-122` (`classify`); `src/direct_net.cpp:607-610` (shedding); `src/eos_hooks.cpp:2309-2313` (trail), `:2295-2297` (learning log).

Any unreliable record type sent 20 times in a row at intervals of 300 ms or less becomes State for the rest of the room. State goes unreliably over EOS, untracked over the direct link, and can be shed against the budget. The only repair is the trail, which resends the last state datagram 300 ms after state to that member stops; while the game keeps sending, a lost datagram is never repaired. This is safe only if every learned type carries complete, idempotent state. The assumption is that no periodic type carries deltas or counters. `0x3000` (HP deltas, kills) is excluded explicitly; nothing excludes other types.

Learning and its log line ("its datagrams go unreliably from now on") also run when traffic classes are off for the room, so the log is wrong in that case.

**Failure scenario:** a periodic type carrying increments (ammo, HP regeneration, spawn batches) is learned as state. One loss under budget pressure is a permanent divergence, which is the class of bug the plugin was written to remove.

**Fix shape:** replace the learner with a static allowlist of record types that the reverse-engineering notes confirm are full state. Gate the learning log on `roomCap(kCapTrafficClasses)`. If the allowlist cannot be established, turn TrafficClasses off by default and keep the plain reliable upgrade.

### 4. [C] path, [A] Wine. The Compression capability is published even when XPRESS is unavailable

**Location:** `multislot/src/netfeature.cpp:325` (caps from INI), `:375` (published); `multislot/src/netcompress.cpp:189-195` (`enabled = on && PackingAvailable()`), `:162-164` (`UnpackPlaintext` returns false without the API); `multislot/src/plugin.cpp:674-675`.

The caps bit comes from the INI flag alone. `InitNetCompress` finds XPRESS missing and disables local packing, but the published caps still say Compression. Other machines pack datagrams of 128 bytes or more to this member; its decrypt hook cannot unpack them, the CRC fails and the game drops them. The assumption is that Wine's ntdll `RtlDecompressBuffer` lacks `COMPRESSION_FORMAT_XPRESS` in some Proton versions.

**Failure scenario:** three Windows players and one Proton player. Every Windows machine packs its large datagrams to the Proton player, who drops them. Mission desync or disconnect.

**Fix shape:** clear `NetFeature::Compression` from `settings.caps` before publishing when `PackingAvailable()` is false, or better, gate it on a pack/unpack self-test at init. Same pattern as finding 17.

### 5. [C] ExtraEnemies is a per-machine setting the room never compares

**Location:** `multislot/src/plugin.cpp:554`, `:745`; `multislot/src/spawn.cpp:118-145`; `multislot/EDF6MultiSlot.ini:112`.

Each machine scales enemy counts from its own `[Mission] ExtraEnemies`. Every machine runs the mission script and creates objects in order, so a different count shifts every later object. The INI says everyone must use the same setting; nothing enforces it.

**Failure scenario:** 8-player room, host ExtraEnemies=1, one guest 0. The host spawns 1.8x ants, the guest 1x. Object ids and ownership diverge from the first wave.

**Fix shape:** publish one host-authoritative "room rules" lobby attribute (ExtraEnemies, Extend, room size) and apply the host's values on every member. Or remove the option and always scale. Removing it is simpler.

### 6. [C] gate, [A] crash path. A machine with Extend=0 can join a room of 5 or more

**Location:** `multislot/src/plugin.cpp:547`, `:553`, `:747`, `:211-258`.

With `[Mission] Extend=0`, the mission patches, compares, hooks, HUD wrap and PacketFit are skipped. The session patches and SEARCH_TYPE family still apply, so the machine finds and joins rooms of any size. Its MissionSync_Update keeps records 0-3 only, and CreateOnlinePlayerObject refuses index 4 and above.

**Failure scenario:** a 6-player room starts a mission. The Extend=0 member builds 4 players, everyone else builds 6. It desyncs at once and can read the 4-entry HUD tables out of range (the 1.2.x crash).

**Fix shape:** remove the Extend option. If it stays, force RoomSize 0 on that machine and refuse rooms above 4 at join.

### 7. [C] Rooms of about 53 or more players cannot start a mission when Fragments is off

**Location:** `multislot/src/packetfit.cpp:589`, `:615-634`, `:93-99`; `src/eos_hooks.cpp:2428-2436`.

Without the records bulk, `FlushRecords` falls back to 21-byte stubs. The message exceeds the 1124 usable bytes at about 53 players. `eos_hooks` fragments oversize datagrams only when the whole room has `kCapFragments`, otherwise it logs the drop and the game disbands after about 26 s. From about 71 stubs `Append` refuses, and `:632` ignores the result, so the message carries fewer records than its header count. Nothing ties room size to Fragments: the netcode gate turns features off rather than refusing members.

**Failure scenario:** a 64-player room where one member has `[Netcode] Fragments=0`, or whose EDF6NET attributes have not been relayed yet. The host starts a mission, the start message is refused, the room disbands. At 128 players the message is also truncated.

**Fix shape:** make Fragments mandatory for MultiSlot rooms (refuse members without it above 4 players, or remove the option). This also lets the stub, side-packet and wait paths go (about 350 lines, see cleanup) and closes findings 37 and 38. Until then, check the `Append` result and refuse the start instead of sending a message that cannot arrive.

### 8. [A] The member slot table survives into the next room when the Users object is reused

**Location:** `multislot/src/userslots.cpp:23-32` (reset only on a new Users address), `:92-95`; published at `src/eos_hooks.cpp:1665-1670`, consumed at `:595-598`.

`Note` clears the table only when the `eos::Users` pointer changes. Nothing clears it on lobby leave or enter. The assumption is that the game's room teardown can release users without passing `Users::Remove` (12B87C0) and that the next room's Users object can land at the same heap address.

**Failure scenario:** the host plays room 1 with A, B, C in slots 1-3, leaves, creates room 2 at the same Users address. `hostRoomTick` publishes `["host","A","B","C"]`. Members holding joins for those slots wait up to 20 s, and phantom members appear in the room view.

**Fix shape:** clear `table` and `tableUsers` on lobby leave and enter. Validate the table against live Users memory before publishing, as `ResolveMissionPlayerPuid` already does.

### 9. [A] A held peer accumulates one hold entry per failed reconnect

**Location:** `src/hold.cpp:34-55` (`offer` pushes without checking for an existing entry), `:120-145` (each entry re-accepts every 2 s and refreshes while the direct link is alive).

`DisconnectHold::offer` adds an entry for every transient close. The assumption is that EOS fires a new `TimedOut` or `ConnectionFailed` close for each failed reconnect attempt, which the reconnect loop and the game's delayed sends keep starting.

**Failure scenario:** a peer whose EOS path is broken but whose direct link is up stays held all session. `held_` grows by one entry per failed attempt and AcceptConnection calls grow with it. When the peer finally leaves, `release()` hands the game N close callbacks for the same peer at once.

**Fix shape:** in `offer`, return true without adding when the remote is already held. While the direct link is alive, skip the EOS reconnect or back it off (finding 19). Add a test that offers the same remote twice.

### 10. [C] path, [A] Wine. A host whose adapter is not recognized as physical advertises no address

**Location:** `src/dn_part.cpp:151-201`, `src/netif.cpp:43-53`, `:75-78`.

IPv6 advertisement and UPnP both depend on `findPhysicalInterface()`. It returns nothing when `FirstGatewayAddress` is empty, when `IfType` is not 6 or 71, or when the description contains "virtual". That covers PPPoE on the PC, mobile broadband and Hyper-V external switches on Windows. Under Wine, `GetAdaptersAddresses` may report `IfType` and gateways differently `[A]`. Without an address the host advertises nothing unless `PublicAddress` is set, and auto-join cannot reach it. The README asks PPPoE users to set `PublicAddress` by hand.

**Failure scenario:** a Proton host with `Mode=host` and an empty `PublicAddress`. Nobody connects directly and the log does not say why.

**Fix shape:** use adapter detection only for `BindPhysicalInterface`. Find what to advertise independently: connect a UDP socket to a public address and read `getsockname` for the egress IPv4 and IPv6, and take global IPv6 addresses from any adapter that is up. Log the chosen addresses and the reason when none is found.

### 11. [C] path, [A] intent. All Forces joins need a direct-link host

**Location:** `src/extension_bridge.cpp:53`, `src/room_view.cpp:73-80`, `src/eos_hooks.cpp:998`, `:1830-1840`.

`missionAdmissionRequired()` is true whenever EDF6VehicleCrew.dll is loaded, so every join is parked. The world manifest that releases it arrives only in a DirectNet Room message, and `hostRoomTick` runs only for `Mode=host`. With `required` set, `decideParkedWorldEntry` skips the NoHost fallback and gives up after 10 s.

**Failure scenario:** an All Forces host on the default `Mode=off`, or behind CGNAT. Every All Forces joiner waits 10 s and fails with `EOS_NoConnection`; the log blames "member slots did not come". README.md:55 calls direct connect optional.

**Fix shape:** decide the policy. Either state and enforce that All Forces rooms need a reachable direct-link host (refuse to host with a menu message otherwise), or carry the world phase over a lobby attribute or EOS channel that does not need DirectNet. Give the world-wait give-up its own log line.

### 12. [C] An All Forces host whose admission gate is not ready locks out every joiner

**Location:** `src/extension_bridge.cpp:53-61`, `src/eos_hooks.cpp:1672-1673`, `multislot/src/plugin.cpp:418`, `:547`.

`readMissionAdmission()` returns `present=true, phase=Unknown` when the All Forces module is loaded but `missionGateReady` is false (`[Mission] Extend=0`, `Apply` failed, or an All Forces build without the export). The host still publishes the Unknown manifest. Joiners see `present` and wait, and Unknown never releases them. Separately, `supportProfileReady()` (`src/lobby_marker.cpp:22-28`) ignores `missionGateReady` and is never re-published when readiness changes.

**Failure scenario:** a player with All Forces and `Extend=0`, or a version skew, hosts. Every joiner is refused after 10-45 s with no log line naming the cause.

**Fix shape:** compute one All Forces readiness snapshot at load (module loaded, isolation ready, protocol 2, Coop gate installed) and use it in all four places that probe the module today. If the module is loaded but unusable, log one REFUSED line and show it in the menu.

### 13. [C] logic, [A] Wine PID reuse. A crashing update is not rolled back when the PID repeats

**Location:** `src/updater.cpp:262-263` (`sameGameRunning` returns true for `pid == GetCurrentProcessId()`), called at `:653`.

`beginRunFiles` runs once at load, before this process writes its PID. A match with our own PID can only mean the previous trial process had the same PID and is gone. A fresh wineserver starts with each Proton launch and hands out PIDs from a small range, so consecutive launches can get the same PID `[A]`.

**Failure scenario:** under Proton an update crashes within its 20 s trial. Every relaunch reads "that game's trial, not over" and runs the crashing build again. Rollback never happens.

**Fix shape:** return false when `pid == GetCurrentProcessId()`. Store the process creation time (`GetProcessTimes`) next to the PID in `.trial` and compare both.

### 14. [A] Under Wine, replacing the loaded DLL may fail, so updates and rollback never complete

**Location:** `src/updater.cpp:541-555` (`renameOver`: `SetFileInformationByHandle(FileRenameInfoEx, POSIX_SEMANTICS)`, then `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`), `:558-578` (`swapIn`: `CreateHardLinkW`, `ReplaceFileW`), `:661`.

The assumption is that wineserver refuses to replace a file whose inode is mapped as the running image. Whether the POSIX-semantics rename gets past that depends on the Wine version.

**Failure scenario:** every start under Proton logs `UPDATE ... could not be installed: cannot put the new file in place (error 5)`. If a trial was started under Windows earlier, the rollback swap fails the same way and the bad build keeps running.

**Fix shape:** confirm with one update under Proton. If it fails, rename the loaded DLL away first (`MoveFileExW(target, aside)`, which Windows and Wine allow for a mapped image), then rename the new file in, and restore on failure.

### 15. [C] Tests against the game skip silently in builds and releases

**Location:** `multislot/CMakeLists.txt:27-32` (`EDF6_GAME_DIR` is a cache entry defaulting to the repo root), `:160-167`; `build.ps1:28`; `release.ps1:45`; `.github/workflows/ci.yml:61`.

`build.ps1` never passes `-DEDF6_GAME_DIR`, so setting the environment variable after the first configure changes nothing, and the fallback `${CMAKE_SOURCE_DIR}/..` never holds EDF.dll. Every game test then exits 77 and ctest reports success. `release.ps1` accepts that, so a release can be packaged with none of PatchTablesMatchEDF, PluginLoad_*, GameNet_*, HitAuthorityGameCode or NetWorldSitesMatchEDF having run. CI never runs these 49 tests, and its skip list omits two of them. `testIat` (`tests/test_main.cpp:3565`) defaults to `D:\steam\...` and passes as a skip on every other machine. `WorldAdmissionNativeTests` is built and never registered (`CMakeLists.txt:345-347`).

**Failure scenario:** a release built on a machine whose cache points at the wrong folder ships with no check of any patch site or room simulation.

**Fix shape:** pass `-DEDF6_GAME_DIR` from build.ps1 whenever the variable is set and drop the `..` default. Label game tests `game`; make release.ps1 configure with an option that turns a missing EDF.dll into a configure error, and fail when ctest reports a skipped game test. Pass `${EDF_DLL}` to testIat or delete it. Register or remove WorldAdmissionNativeTests.

### 16. [C] Proton has no install path and no documentation

**Location:** `dist/INSTALL.bat`, `dist/UNINSTALL.bat`, `dist/install.ps1:13-33`, `:80`; `dist/EDF6Coop_AllowFirewall.bat`; all READMEs and `dist/README_EDF6Coop*.txt`.

No tracked file mentions Proton, Linux or `WINEDLLOVERRIDES="winmm=n,b" %command%`. Without that launch option Wine uses its builtin winmm, EDFModLoader never loads and the plugin writes nothing. INSTALL.bat needs cmd and Windows PowerShell, which a Proton prefix does not provide in usable form `[A]`. install.ps1 finds Steam libraries only through the registry, never `~/.local/share/Steam` or the Flatpak path. The firewall script uses netsh; a Linux host opens the UDP port in firewalld or ufw.

**Failure scenario:** a Linux player follows the README and plays the vanilla game with no error and no log.

**Fix shape:** add a "Linux and Steam Deck (Proton)" section to the three READMEs and three dist text files: copy the zip contents next to EDF6.exe, set the launch option, open `ListenPort` in the host firewall, uninstall by deleting the listed files. Add "no `EDF6Coop.log` under Proton means the launch option is missing" to troubleshooting. Optionally add an `install.sh` that finds the library under the native and Flatpak Steam paths.

### 17. [A] A peer-controlled block length reaches the game's unchecked stream reader

**Location:** `multislot/src/hitauthgame.cpp:434`, `:112`; `multislot/src/networld.cpp:147`, `:58`.

The length in a type-13/14 block tag (`0xA0 | len >> 8`, up to 0x1FFF) goes to the game's `12B4900`, which copies into a stack `GameStream` of 0x600 bytes. `netplayer.h:52-55` states the game's bin readers do not check bounds. Vanilla never reads these block types; the hooks add the exposure. The assumption is that `12B4900` copies the declared length without a bound.

**Failure scenario:** a malformed or hostile hit event from any room member overruns the game's stack.

**Fix shape:** read these blocks with the existing bounded `ReadBinElement` (`netplayer.cpp`) at a 94-byte or 31-byte capacity, and drop the read-side `GameStream` use.

## Minor

### Transport hooks and holds

**18. [C] A host change or link swap drops packets already received.** `src/eos_hooks.cpp:1037-1048`, `:1278-1281`, `:1982-1989`. Retiring a `DirectNet` discards its `inbox_` and un-acknowledged sends, and `hookReceivePacket` only pops the current `g.net`. **Failure scenario:** the host leaves mid-mission and we are promoted; datagrams relayed by the old host and not yet popped never reach the game. **Fix shape:** move the old inbox into the new instance, or drain it first, before retiring.

**19. [C] Reconnect requests continue all session for a peer held by its direct link.** `src/hold.cpp:129-140`, `src/eos_hooks.cpp:458-466`. The EOS reconnect fires every 2 s and logs every 10 s while game traffic already goes direct, and feeds finding 9. **Fix shape:** skip or back off the EOS reconnect while the direct link is alive.

**20. [C] AutoJoin tears down a working link when the advertised address list changes.** `src/eos_hooks.cpp:1317-1327`. The host advertises IPv6 first and IPv4 after UPnP, so a joiner connected in between drops a live link and redials. **Fix shape:** when connected and the current address is still listed, update the candidates without restarting.

**21. [A] The plugin calls EOS from the tick thread while the game sends from another.** `src/eos_hooks.cpp:2358` (`trailTick`), `:3338` (`bulkTick`), `:2066`, `:2094`, `:745`; `:3291` (`ackBulk` in the receive hook). The assumption is that EOS P2P calls are not safe concurrently. **Fix shape:** compare the "EOS tick runs on thread" and "WHOAMI" log lines; if the threads differ, queue plugin sends and flush them from the game's send thread.

**22. [A] Direct-link packets from a peer the game closed still reach the game.** `src/eos_hooks.cpp:517-525`, `:2577-2595`. `hookCloseConnection` only clears hold state; DirectNet keeps delivering. **Fix shape:** a per-peer "closed by game" flag that drops direct deliveries until EOS re-establishes or the game sends again.

**23. [C] The state filter can receive timestamps that run backwards.** `src/netcode.h:39-48`; callers `src/eos_hooks.cpp:2418-2419` (game thread), `src/direct_net.cpp:965-967` (link thread); consumer `multislot/src/netaoi.cpp:107`, `:127`. Both read the clock before the filter's lock; `nowMs - lastAskMs` wraps. **Fix shape:** clamp inside the filter, or remove the filter (see interest management under cleanup).

### Direct link

**24. [C] Event and Control datagrams on a single direct path get no transport repair.** `src/direct_net.cpp:570-574`, `:916`; the header comment at `src/direct_net.h:19-20` says eos_hooks sends a second copy over EOS, and `src/eos_hooks.cpp:2441-2446` says it does not. Over EOS the same datagram is upgraded to reliable; over the direct link a loss waits for the game's own resend (700 ms first try). **Fix shape:** track Event/Control on a single path with a short expiry, as Unknown is, and fix the comment.

**25. [C] A malicious member can make the victim's host-link datagrams match one of its peer links.** `src/direct_net.cpp:1916-1934`, `:1859`, `:1194`, `:1466`. Peer links are matched by epoch before the host link, and the epoch function is invertible, so a member can pick a hello nonce that collides with the host-link epoch. Host datagrams then fail the peer link's tag and are dropped. **Fix shape:** try `hostLink_` first, and refuse to create a link whose epoch equals an existing one.

**26. [C] Addresses in PeerInfo are dialed unfiltered.** `src/direct_net.cpp:1772-1798`. Loopback, multicast and link-local addresses are dialed and punched, unlike advertised host addresses. **Fix shape:** apply `dialable()` before dialing.

**27. [C] Roster and Room lists cost the host O(n²) uplink.** `src/direct_net.cpp:1530-1539`, `:1684-1688`, `:24-28`. Every 5 s every page of both lists goes to every client, plus about six full broadcasts per membership change. About 0.9 MB/s steady at 256 members. **Fix shape:** send on change plus a small version beacon; a stale joiner asks for the pages.

**28. [C] Completed-message eviction in the reassembler is not oldest-first.** `src/fragment.cpp:155`, `:89-90`. It evicts the lexicographically smallest `(src, id)`, and sweeps O(n) per fragment. A resent bulk message can be reassembled and handled twice. **Fix shape:** an insertion-ordered queue beside the map.

**29. [C] Replay records for members outside Epic's lobby are pruned on every identity update.** `src/direct_net.cpp:389-390`. **Fix shape:** keep `seen_` entries for ids in `provenIds_`.

**30. [C] The mesh responder closes links without a Bye.** `src/direct_net.cpp:1720-1727`. The dialer keeps routing to the dead link for about 1.5 s and its datagrams are logged as host-link auth failures. **Fix shape:** send Bye on close; drop datagrams with no matching epoch without logging.

**31. [C] Fragments are sent ReliableOrdered and wait behind lost unreliable packets.** `src/eos_hooks.cpp:2385`, `:2389`; `src/reliable.cpp:201-224`. Reassembly does not need order. **Fix shape:** send fragments ReliableUnordered.

### Room sync glue

**32. [A] The sync budget leaves 24 bytes for messages batched into the same datagram.** `multislot/src/packetfit.h:40-45`. The per-peer builder sends only past 250 bytes, so up to 250 bytes of other messages can precede the sync. Matters only without Fragments. **Fix shape:** flush the builder before appending the sync, or lower the budget; moot if finding 7 makes Fragments mandatory.

**33. [C] Split-sync records are marked sent before the send, and a failed send is not retried.** `multislot/src/packetfit.cpp:824-829`, `:842-852`. The receiver then builds that player from stale slot data after `kRecordWaitMs`. **Fix shape:** record the member in `sentTo` only after every side packet succeeded, as the bulk path does.

**34. [C] An abandoned records bulk is remembered by content hash for the whole room.** `multislot/src/packetfit.cpp:594-595`, `:150`, `:160-165`, `:737-739`. A later mission with identical loadouts is skipped instead of waited for. `AbandonBulk` also runs once per record. **Fix shape:** a per-sync nonce in the id, clear `abandoned` on each new marker, abandon once.

**35. [C] Every received game packet stringifies its sender id under `heldLock`.** `multislot/src/packetfit.cpp:277-302`, `:394-403`; `multislot/src/identity.cpp:98-119`. One `GetModuleHandleA`, two `GetProcAddress` and an EOS `ToString` per packet on the game thread, plus linear scans of up to 1024 entries per send during the 30 s window. **Fix shape:** cache the EOS exports, check an atomic "bulk in flight" counter first, and keep per-peer flags instead of scans.

**36. [A] Lobby diagnostics can flood the log, and a full queue makes the game thread write to disk.** `multislot/src/netlog.cpp:458`, `:633-643`; `multislot/src/log.cpp:556-559`; `multislot/src/packetfit.cpp:851`. `lastMemberCount` is one global across lobby handles, search counts are logged on every call, and split syncs log one line per record per member. **Fix shape:** key or drop the member-count line, log counts on change, aggregate per member.

**37. [A] The room count falls back to capacity 4 when the game's and Epic's member counts differ.** `multislot/src/rooms.cpp:198`, `:219`, `:272-275`. The game's count includes RoomView members Epic has not relayed yet. **Failure scenario:** a 12-player room publishes HIDDEN and drops out of the list until the counts agree. **Fix shape:** trust the published room size for MultiSlot rooms and clamp only the "full" test.

### Netcode rewrite modules

**38. [C] HitAuthority stays published when its hooks or sampler thread fail.** `multislot/src/plugin.cpp:700`, `multislot/src/hitauthgame.cpp:502`, `multislot/src/netfeature.cpp:328`. Other machines drop this machine's bullet copies while it never forwards, so its hits on players and on vehicles owned by others are lost. Same root cause as finding 4. **Fix shape:** a `WithdrawNetFeature` that clears the bit and republishes, called on any failed install.

**39. [C] logic, [A] draw rates. RNG sync can move an aligned enemy copy out of alignment.** `multislot/src/networld.cpp:161-162`, `multislot/src/worldrng.h:8-15`. The copy is overwritten with the owner's state from send time; an AI that draws during the latency window re-draws numbers the owner already used and stays behind until the next sync. `docs/net-re/world.md` §8.2 does not cover this. **Fix shape:** skip the overwrite when the received state is within N LCG steps behind the copy's; otherwise measure the benefit and remove the feature if there is none.

**40. [C] The range check measures from the world origin when the attacker is unknown.** `multislot/src/hitauthgame.cpp:243`, `multislot/src/hitauth.cpp:224`. Hits more than 6000 m plus radius from the origin are refused. **Fix shape:** skip the range check when neither side knows the attacker.

**41. [A] A kill whose attacker has despawned may not propagate.** `multislot/src/hitauthgame.cpp:274`. `Deal` sets `kGdiFromNetwork`, so no kill message is queued, and third machines get only the type-2 HP delta. The assumption is that the type-2 receive does not run the death path. **Fix shape:** confirm in `damage.md` terms; if it does not, queue the kill message explicitly.

**42. [A] `AttackerOwner` reads a vtable at a fixed offset without validating it.** `multislot/src/hitauthgame.cpp:151-153`. **Fix shape:** require the vtable pointer to lie inside the EDF.dll image, as `networld.cpp` does.

**43. [A] The hit dedup window shares one sequence across all targets.** `multislot/src/hitauth.cpp:245`. At high event rates a batch reordered by more than 64 sequence numbers is refused as Duplicate. **Fix shape:** a per-(sender, attacker) counter, or a wider bitmap window.

**44. [C] Interest management's once-per-interval guarantee is undone by later limiters, and it walks every observer per datagram.** `multislot/src/netaoi.cpp:55-56`, `:106`; `src/direct_net.cpp:607`, `:965-977`. The forced send is spent before `takeStateBudget` or the relay bucket can drop it. **Fix shape:** remove the gate for own traffic (see cleanup), or make W1 honour a "forced" flag.

**45. [A] No finiteness guard in remote player stepping.** `multislot/src/netplayer_game.cpp:274-285`, `multislot/src/netplayer.cpp:196`. A NaN proxy position propagates into `+0x840`. **Fix shape:** reset steps and leave the position alone when inputs are not finite.

**46. [A] Player tracks are keyed by object address.** `multislot/src/netplayer_tracks.h:13-24`, `multislot/src/netplayer_game.cpp:187`. A new soldier at a freed address within 400 ms inherits the old track. **Fix shape:** reset the track when the user pointer changes.

### Game patches, host data, settings

**47. [C] With Extend=0, copy armor raises the published armor but not the local player's.** `multislot/src/armor.cpp:379-404`, `multislot/src/mission.cpp:209-217`, `multislot/src/plugin.cpp:218-219`. The local index comes from a mission hook. **Fix shape:** closed by removing Extend (finding 6); otherwise install armor hooks only with the mission hooks.

**48. [A] MultiSlot hosting stays on when lobby state failed to install.** `multislot/src/plugin.cpp:217`, `:753-757`; `multislot/src/lobbystate.cpp:850-859`. Room updates then republish a 12-player room as a vanilla 4-player room. **Fix shape:** force RoomSize 0 and log when lobby state is unavailable.

**49. [C] Host data takes a mutex on every received game packet; the same lock covers disk scans and SHA-256.** `multislot/src/hostdatanet.cpp:631-653`, `:390-399`, `:460-466`, `:571-576`. **Fix shape:** filter channel 0x48 without the lock and verify the cache outside it.

**50. [A] Host data serving ignores mission state and restarts on repeated requests.** `multislot/src/hostdatanet.cpp:196-211`, `:232-267`. Up to 8 KB of reliable data per EOS tick, so about 1.1 MB/s at 144 fps, during missions. Any Get resets the transfer. **Fix shape:** pause during missions, budget by time, ignore repeated Gets within the stall window.

**51. [A] The consent prompt is a separate top-level MessageBox, and hotkeys fire into it.** `multislot/src/hostdataprompt.cpp:173-180`; `multislot/src/hostmode.cpp:222-228`, `multislot/src/roomview.cpp:230-236`. Under gamescope or exclusive fullscreen the box may be hidden or steal focus; F1/F2/F6 typed into it also reach the plugin. **Fix shape:** ask through the existing menu label with AcceptKey and a decline key; at least ignore hotkeys while the box is up.

**52. [C] Case-colliding files on Linux disable sharing of the whole Mods folder.** `multislot/src/hostdata.cpp:77-87`, `:128-151`. `a.sgo` and `A.SGO` normalize to one path and `CheckFiles` fails with "twice". **Fix shape:** skip the duplicate with a log line.

**53. [C] Weapon guard probes are not wrapped in `Probing`.** `multislot/src/weaponguard.cpp:366-406`. A caught fault is logged as a crash and spends the only crash dump. **Fix shape:** wrap them like `spawn.cpp` and `armor.cpp`.

**54. [C] Key settings are parsed inconsistently.** `multislot/src/plugin.cpp:97-107`, `:595-603`. `AcceptKey=None` and `WeaponPageKey=None` fall back to F1/F6; copy armor can land on the Player MOD key; several conflicts go unchecked. **Fix shape:** one key-resolution pass over all bindings, where None means none.

**55. [C] `IniText` truncates at 63 characters.** `multislot/src/plugin.cpp:90-94`, `:129`. A longer `[HostData] Page` folder name is never found after restart. **Fix shape:** a MAX_PATH buffer.

**56. [C] Typos in enum settings are silently replaced by defaults.** `src/config.cpp:404-405`, `:419-423`; the promise to warn is at `src/config.h:37-38`. `Mode=hots` runs as `off` with no log line. **Fix shape:** warn on any non-empty unrecognized value.

**57. [C] The startup log misreports enemy factors above 32 players.** `multislot/src/plugin.cpp:735-743`, `multislot/src/spawn.cpp:231`. **Fix shape:** print the rule and its cap.

**58. [A] Code is patched without suspending game threads.** `multislot/src/code.cpp:71-79`, `multislot/src/plugin.cpp:402-412`. Safe only if EDFModLoader loads plugins before game threads run EDF.dll code. **Fix shape:** log the thread count at load once to confirm.

**59. [C] `DeleteTree` follows directory junctions.** `multislot/src/hostdatanet.cpp:289-305`. `modfile.cpp:211` checks reparse points; this does not. **Fix shape:** skip `FILE_ATTRIBUTE_REPARSE_POINT`.

### Updater and network APIs under Proton

**60. [A] WinHTTP is opened with `AUTOMATIC_PROXY`; the EDF6VR check has no fallback.** `src/updater.cpp:112-115`, `multislot/src/updatecheck.cpp:122-124`, `:177-208`. **Fix shape:** retry with `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY`; one shared HTTP helper.

**61. [A] A BCrypt provider failure is reported as an invalid signature.** `src/updater.cpp:228-231`. **Fix shape:** log provider and import failures separately.

**62. [C] A manually reinstalled version recorded in `.bad` is rolled back at once while `.old` exists.** `src/updater.cpp:645`, `:660-675`. **Fix shape:** INSTALL removes `.bad`, `.old` and `.trial`.

**63. [A] Egress pinning results are ignored.** `src/direct_net.cpp:240-247`. `IP_UNICAST_IF` on a dual-stack AF_INET6 socket may be refused by Wine or pin to an adapter without an IPv6 route. **Fix shape:** log each setsockopt result; consider defaulting `BindPhysicalInterface` off when `wine_get_version` is exported.

**64. [A] Socket buffers are clamped on Linux.** `src/direct_net.cpp:236-238`. `SO_RCVBUF`/`SO_SNDBUF` of 1 MB is capped by `net.core.rmem_max` (often about 208 KB). **Fix shape:** read back and log; document the sysctl for Proton hosts.

**65. [A] UPnP depends on Wine's partial `IUPnPNAT`.** `src/upnp.cpp:132-133`, `:150-162`, `:215-238`. An empty external address is logged as "(not reported)" and the IPv4 address is never advertised. The mapping is never refreshed after a router reboot. **Fix shape:** warn and point to `PublicAddress` when the address is empty; re-map periodically.

**66. [A] IPv6 replies can leave from a different source address than the one advertised.** `src/direct_net.cpp:1231`, `:1312-1316`; `src/netif.cpp:95`. A joiner behind a stateful IPv6 firewall drops the reply. **Fix shape:** reply with `WSASendMsg` and the `IPV6_PKTINFO` of the arrival.

### Build, release and install

**67. [C] Uninstall leaves files the plugin wrote and deletes the loader's license.** `dist/install.ps1:173-194`, `:177`. `Mods/UI/LYT_MAINFRAME.SGO`, `Mods/HUD/ONLINEHUDTEXTURE.RAB` and `EDF6Coop.hostdata/` (up to 64 MB) stay; `EDFModLoader_LICENSE.txt` goes while the loader stays. **Fix shape:** remove the two files when their hash matches a shipped build and remove `EDF6Coop.hostdata`; keep the license.

**68. [C] The package staleness check misses the INI compiled into the DLL.** `package.ps1:66-70`; `multislot/CMakeLists.txt:36`. **Fix shape:** include `multislot/EDF6MultiSlot.ini` and CMakeLists.txt.

**69. [C] package.ps1 fails on Linux pwsh.** `package.ps1:77-79` hard-codes `\`. **Fix shape:** `[IO.Path]::DirectorySeparatorChar`.

**70. [A] The sign job re-checks out the tag by name.** `.github/workflows/release.yml:97-117`, `:142-145`. A tag moved between jobs runs unverified script next to the signing key. **Fix shape:** pass the verified SHA from the fetch job.

**71. [C] The firewall script deletes the old rule before adding the new one.** `dist/EDF6Coop_AllowFirewall.bat:323-326`. A failed add leaves no rule. **Fix shape:** delete after a successful add.

**72. [C] install.ps1 says old DLLs are never deleted, then deletes an existing `.disabled` copy.** `dist/install.ps1:3`, `:213`. **Fix shape:** fix the comment or skip the rename.

**73. [C] README build requirements list `pillow`, which no tool imports.** `README.md:170`. **Fix shape:** drop it.

## Cleanup candidates

Estimates are rough line counts of C++ unless stated.

### No behaviour change (about 2,000 lines)

- **Duplicated EOS declarations.** `lobbystate.cpp:17-153`, `syncmarker.cpp:16-146`, `rooms.cpp:65-106`, `netlog.cpp:22-103`, `packetfit.h:115-131`, `packetfit.cpp:212-225`, `hostdatanet.cpp` re-declare `src/eos_min.h`. About 150.
- **Duplicated game-object message plumbing.** `GameStream`, stream RVAs, send-to-copies and sender-id code in `hitauthgame.cpp:23-31`, `:101-115`, `:349-353`, `networld.cpp:29-60`, `:120-125`, `worldrng.cpp:49-55`. One header, about 60. Do it together with finding 17.
- **Split-sync marker.** `syncmarker.h:17-72`, `syncmarker.cpp:222-228`, `:269-277`, `:446-527`, `plugin.cpp:486-491`, `:819-829`, `src/eos_hooks.cpp:3045-3050`, `src/dn_part.cpp:146`, packetfit references. The sync is encrypted, so the marker can no longer gate anything, and the SEARCH_TYPE family already excludes builds that cannot read it. Keep the newcomer detection. About 125 plus tests.
- **Dead packetfit code.** `FindStubs`, `kMaxStubsPerPacket`, the unreachable overflow branch in `RecordWriteHook` (`packetfit.cpp:561-566`), the oversize diagnostic that duplicates eos_hooks' (`packetfit.cpp:405-408`, `:894-931`, `netlog.cpp:342-345`), and the duplicated `WaitForRecord`/`WaitForBulk` loops. About 110. `kRecordStoreEntries` can drop from 4096 to 256.
- **Transport diagnostics and dead counters.** `eosCopies`, `bulkResent`, `Held::reason`, the duplicate `statusName` table, duplicate forward declarations, the member-update and update-lobby log-only wrappers, the kick-repeat counter, `onInterrupted`/`onClosed`, the `src/traffic.*` copy/repeat meter (the question it answered is settled in `netclass.h`). About 150.
- **Fragments drain for channel-filtered receives.** `src/eos_hooks.cpp:2555-2576` and the channel filter in `deliverReady`. EDF6 always passes `RequestedChannel=NULL`. About 25.
- **Direct link duplication.** `onHostHello`/`onPeerHello`, welcome-to-keys and hello building, the identical `RosterMsg`/`RoomMsg` types and handlers, separate `Dial` and `peerLinks_` maps, protocol 2-6 history (only 7 is accepted), `applyPage`, `kDefaultPort`, `srttMs`, `trafficClassName`, `heardFromRecently`. About 220.
- **Test-only production hooks.** `setTestPeerBlock`, `setTestBulkFragmentsSent`, `setTestLoopbackHosts`, `testDropRate`, `testUplink*`, `setTestBlackhole`, `setTestBlockPeer(s)`, `ForgetPeerJoins`, `ForgetPeerTimeoutLog`, `CapacityFromInfo`, `SplitSyncRoom` accessors, interest overrides. GameNet uses some of them, so compile them behind a test macro rather than delete. About 110.
- **Dead netfeature and interest API.** `SetStateSendFilter`, `SetBulkHandler`, `LinkBudgetBytesPerSec` wrappers, `NetRoom::Members`, the `PluginObjects` bit, `kCapCompression` in `netcode.h`, interest link-budget overrides, `team` and `engaged` (never set). About 120.
- **dn file log.** `src/log.cpp:13-67` has no caller; production always uses the sink. About 40, plus `EDF6Coop.log.1` in the uninstall list.
- **Small items in plugin.cpp.** `netfeature.h` included three times, the copy-armor pad check twice, the game folder computed twice, `_snprintf_s` of constants, about 60 lines of startup log prose. About 80.
- **Other duplicated helpers.** `KeyDown`, `ControlBlock`, `Field32`, `kGameStatus`/`kGameStatusPointer`, the import-table walk in `rooms.cpp:111-138` and `netlog.cpp:715-739`, the four All Forces module probes. About 100.
- **Diagnostic leftovers.** `ProbeLocalArmor` (`roomview.cpp:124-142`, `:244-279`), the per-file open log in `hostdataopen.cpp:202-205`, the loader proxy style check. About 70.
- **CMake.** `src/log.cpp` is compiled 23 times, `patches`/`code`/`identity` 15 each. A `RoomCore` static library plus `room_test()` and `dn_test()` helpers, folding the appended W1 block and the two GameNet scenario loops into the main lists, would take `multislot/CMakeLists.txt` from 403 to about 200 lines.
- **Stale comments.** `src/updater.h:5-6`, `:21`, `:40`, `:61-62` (EDF6DirectNet manifest), `src/product.h:2` (32 slots), `src/direct_net.h:19-20`, `src/eos_hooks.cpp:2312`, `multislot/src/updatecheck.h:24`, `multislot/src/lobbystate.h:81-83`, `multislot/src/patches.h:41-71`, `:211` (rbp+0x20 vs +0x24), the "players 5-8" comments, `multislot/src/smoothing.h` (its premise contradicts `docs/net-re/player.md:21`).

### Owner decisions (about 2,500 more lines)

| Candidate | Lines | Case for removal | Cost |
|---|---|---|---|
| Smoothing (`smoothing.*`, plugin wiring, tests, load_test "smooth") | ~170 | The INI says it does not help; `player.md:21` says the site smooths facing, not position; W2 supersedes it; a site mismatch makes all of `Apply` refuse | None at the default |
| Interest management (`interest.*`, `netaoi.*`, `netplayer_members.*`, `FeedPlace`) | ~650 | For own traffic it duplicates W1 shedding (finding 44); it only helps relayed state in very large rooms | Large rooms lose prioritisation of relayed state |
| RNG sync (`worldrng.*`, RNG half of `networld.*`) | ~400 | Finding 39: no demonstrated reduction in divergence, adds per-enemy traffic | Keep target keeping (local only) |
| Stub/side-packet/held-packet start sync | ~350 | Only serves 8-52 player rooms without Fragments; finding 7 | Requires Fragments for MultiSlot rooms |
| Extend and ExtraEnemies options | ~30 | Findings 5, 6, 47 | Removes two user choices |
| Handshake diagnostics and `HandshakeRecovery` in netlog | ~340 | Served past investigations; recovery calls a game function twice with unverified side effects | Less evidence in user logs |
| joinlog overlap with userslots, one-off join/content/Steam lines | ~200 | Duplicates what userslots mirrors | Less log detail |
| DummyMembers test mode and `ClampMembers` | ~175 | GameNet and GhostPlayers cover multi-member rooms; the clamp cannot trigger at 1024 | Loses a manual test mode |
| Log `.queue` mmap recovery and LastRun/SHUTDOWN detection (`multislot/src/log.cpp`) | ~280 | The comment at `:467-471` says SHUTDOWN is not written for this game | Last ~200 ms of lines lost on a hard kill |
| EDF6VR update check (`updatecheck.*`, INI key, menu text) | ~330 | Another mod's feature and a second HTTP client | VR users lose the menu notice |
| Pre-2.3 INI migration (EDF6DirectNet.ini, EDF6MultiSlot.ini, EightPlayerRooms, MaxPlayers, PageKey, Scale5..32, Accept=Always, AutoUpdate warning, UPnP file rename) | ~150 | 2.3.0 is several releases back | Users jumping from 2.2 or earlier lose settings |
| 2.2.x room-size update markers and the six `EDF6Coop-<N>p.dll` release assets | ~20 plus five scripts | Branch `origin/chore/drop-room-size-release-copies` already does the release half | 2.2.x installs stop auto-updating |

Keep `RetireReplacedPlugins` (`coop.cpp:268-298`) while EDF6DirectNet.dll and EDF6MultiSlot.dll installs may exist; loading both plugins double-hooks EOS.

### Tests

- **Delete:** `testLinkTagSpeed` (`tests/test_main.cpp:865-887`, wall-clock bound), `testNetif`'s "Clash" check (`:2407-2420`), the `--mesh` and `--receive-lifecycle` re-runs (`:3543-3556`), constant restatements (`hostmode_test.cpp:163-177` except the 64 cap, `log_test.cpp:249`, `interest_test.cpp:96-99`, `refusal_test.cpp:83`, `load_test.cpp:466`, `tests.cpp:244-251`), `interest_test.cpp:100-150` (repeats `netaoi_test.cpp:60-115`), `GameNetLogStats` (tests a harness helper), the dead branch in `mod_room_test.cpp:14-21`.
- **Consolidate:** 38 private `CHECK` macros into one `tests/check.h`; two fake EOS SDKs (`multislot/tests/fake_eos.cpp`, `gamenet/fake_eos_net.cpp`); GameNet scenarios listed twice (`scenarios.h:250`, `:999`); split the 65-case `DirectNet` ctest entry by area. `gamenet/eos_stubs.inc` says "generated" with no generator in the repo.
- **Replace:** `default_ini_test.cpp:31-44` regex-scans source files; a key table shared by the reader and the INI template would replace it.
- **Gaps:** `src/eos_hooks.cpp` has no test outside GameNet, so send routing, the reliable upgrade and hold wiring are never exercised in CI. A stub game DLL importing from FakeEosSdk would let `installEosHooks` patch its IAT in CI. `src/dn_part.cpp` has no test. Socket fault injection covers drop and blackhole only, not duplicate, reorder or delay. No parser is fuzzed (wire, fragment header, netplayer block, hostdata packet, hit event).
- **Keep:** the reliable-layer loss/retransmit/ack-gap tests, malformed-input tests, version-mismatch and link-lifecycle tests, hold tests, room view and mesh tests, updater signing and rollback tests, `net_transport_test.cpp`, `world_admission_test.cpp`, and every GameNet loss, late, slow-peer, mesh, versiongate, slotmigrate, slotchurn and bulk scenario. These guard the desync fixes.

### Docs

- **The three READMEs describe 2.3, not 2.5.0.** They give 32 as the maximum and `RoomSize` 5..32 (`README.md:5`, `:15`); the code says 1024 (`multislot/src/patches.h:21`) and so do the dist text files and 2.5.0 notes. "Never modifies any game files" (`README.md:9`) is false: the plugin patches EDF.dll in memory and writes two files under Mods. "Reliable sending" (`:111-113`) predates traffic classes. The settings reference has no `[Netcode]` or `[NetPlayer]` rows. The banner format, protocol list and `v2.0.0` release example are stale. Some sentences run 900-1300 characters (`:131`, `:135`, `:137`, `:147`, `:163`).
- **Duplication.** Room sizes, host data, updater and install text appear in three to five places in three languages each, and have drifted. Make the INI comments and dist text the source for settings and link to them.
- **Move.** "How it works" (about 4 KB per language) to one English design doc in `docs/`, with a short summary per README.
- **Remove or rewrite.** `multislot/packaging/HANDSHAKE_RECOVERY_JA.md` and `LOADER_FIX_JA.md` were written for EDF6MultiSlot 1.5.5, cite missing paths, and ship in every zip; drop the first and rewrite the second short, in three languages. `multislot/README*.md` repeats the root README. `docs/netcode-rewrite-plan.md` describes merged work (and `netfeature.h:2` points to it). `release-notes/0.3.1` to `0.4.1` and `multislot/release-notes/1.5.x` (about 57 KB) are history the git log and GitHub Releases already hold.
- **Ship less.** The `[Test]` keys and DummyMembers are advertised in the shipping INI (`multislot/EDF6MultiSlot.ini:92-99`, `:225-249`); SplitSyncBudget and DropRecordsBulk break a room if a player sets them.

### Structure

The `src/` vs `multislot/` split is history. The entry point for the whole DLL is `multislot/src/plugin.cpp`, the netcode modules in `multislot/src/` depend on `src/netcode.h` and `eos_hooks`, and `src/` is built from `multislot/CMakeLists.txt` through `../src`. Pairs with near-identical names do different things: `src/room_view.*` (lobby membership) and `multislot/src/roomview.*` (room screen UI), `src/traffic.*` and `multislot/src/nettraffic.*`, `src/log.*` and `multislot/src/log.*`, `src/updater.*` and `multislot/src/updatecheck.*`.

Proposed layout:

```
CMakeLists.txt        moved up from multislot/; one version
src/core/             plugin, coop, config, log, crashlog, updater, product.h
src/transport/        eos_hooks (split, see below), iat, hold, reliable, wire, auth, direct_net,
                      fragment, congestion, netclass, netif, upnp, lobby_marker, fake_lobby,
                      room_view, extension_*
src/netcode/          netfeature, netcompress, netplayer*, hitauth*, networld, syncmarker, packetfit
src/room/             patches, code, midhook, widecmp, rooms, roomview, mission, spawn, armor,
                      hostmode, hud, modfile, hostdata*, weaponguard, userslots, vectoralloc,
                      lobbystate, peertimeout, joinlog, identity
tests/unit/, tests/gamenet/, tests/fakes/, tests/check.h
tools/                python asset and loader tools, fetch_modloader.ps1, msvc-x64-env.cmd
third_party/EDFModLoader/
packaging/            today's dist/ plus the loader-fix note
docs/net-re/, docs/design/
LICENSES/             MIT, Unlicense (EDF6MultiSlot by momotori01), EDFModLoader
```

`src/eos_hooks.cpp` (3383 lines) mixes about eight concerns: P2P send and receive, the disconnect hold, the lobby status hold and owner pin, room view and slots, AutoJoin, rejoin and the virtual room, the room-list stand-in lobbies, and bulk/fragments/trails. Splitting out transport (send, receive, fragments, trails, bulk) lets the desync core be reviewed and tested on its own.

Order: land or close `origin/chore/drop-room-size-release-copies`; move CMakeLists.txt to the root with the shared libraries; merge test folders and rename `dist/` to `packaging/`; split `src/` by area last, since it touches every include. `"src/config.h"` resolves through the `..` include directory today, and both parts have a `log.h`.

Names that must stay for compatibility: the UPnP description `EDF6DirectNet`, the `EDF6DN_UPDATE_SIGNING_KEY` secret, the `[MultiSlot]` INI section, and the old INI names while their migration stays. `MULTISLOT_*` macros, the `EDF6MultiSlot.ini` template name and test target names are internal.

33 remote branches besides `main`; 31 are fully merged. `codex/support-world-admission` is the pre-squash history of #27 with an identical tree. `chore/drop-room-size-release-copies` has one unmerged commit with real changes.

### Linux development

clang-cl, llvm-lib, llvm-rc, wine and pwsh are installed on the dev box; `lld-link` (package `lld`) and a Windows SDK and CRT (xwin) are missing. A `cmake/clang-cl.cmake` toolchain with `CMAKE_SYSTEM_NAME Windows`, lld-link, xwin paths and `CMAKE_CROSSCOMPILING_EMULATOR wine` would build the DLL and run tests under Wine. The game is installed at `/mnt/kosmos/SteamLibrary/steamapps/common/EARTH DEFENSE FORCE 6`, so the game tests can run too. That makes Proton testing part of the normal loop and would have caught findings 2, 4, 13 and 14. `/WX` will likely need clang-specific `-Wno-*` flags. `hold`, `reliable`, `room_view`, `fragment`, `congestion`, `netclass` and `interest` make no Windows calls and could also run as native Linux unit tests.

## Cleared

- **Core fix.** `hookSendPacket` upgrades unreliable non-state datagrams to ReliableUnordered for `ApiVersion >= 3`; queues only grow and 0 (unlimited) is kept (`src/eos_hooks.cpp:1387`); held peers get `bAllowDelayedDelivery`. The options copy is guarded by `ApiVersion`.
- **EOS layouts.** `src/eos_min.h` matches the SDK for SendPacket, ReceivePacket, the closed, established and queue-full infos, PacketQueueInfo, SetPortRange and PeerConnectionOptions.
- **Hold.** Transient close reasons (0, 3, 7, 8, 9, 10) match the SDK enum. Deliberate closes reach the game exactly once. A first handshake failure reaches the game for non-direct peers. Callbacks run unlocked on the tick thread.
- **Reliable layer.** Karn's rule, the 0/150/450/1050 ms schedule, forward floor, 4096 reorder cap, 8 MB pending cap and `skipped` arithmetic; the retransmit token bucket clamps a backwards clock.
- **Traffic classes and events.** `0x3000` (HP deltas, kills) is never learned as state; a datagram mixing reliable and unknown records stays Unknown and repaired.
- **Fragments.** Separate channel, `count == ceil(total/payload)` and exact per-index length, 16 partial messages per sender and 32 MB total, cleared on leaving a room.
- **Wire decode.** Header and tag lengths, string and roster bounds, page `offset+count<=total`, payload <= 1170, ack ranges; no out-of-bounds reads.
- **Direct link crypto.** Constant-time tag compares, stateless cookies bound to address, puid, nonce and time bucket, no amplification, welcomes bound to the client's ECDH key, replay window correct, relayed `src` checked by the host.
- **Extension ABI and room isolation.** Size and version checks, NUL-terminated ids, spoofed `src` dropped, `parseWorldAdmission` strict, `applyListPage` bounded and monotonic, `CompatibleRoom` checked on every virtual-room path.
- **Updater order.** Manifest shape, signature, signed version equals tag, newer than running, digest and marker; exact asset URLs; verification in memory before writing. sign-update.ps1 checks the key pair and re-verifies its output.
- **Patching.** `Apply` verifies every site before writing and restores in reverse on failure; mid-hook thunks match `CpuContext`, keep 16-byte alignment and unwind data; widened compares refuse RIP-relative shapes; the voice HUD allocation matches MSVC `_Deallocate`; room-size bounds agree at 1024 across slots, sessions, voice records, sidecars and loops.
- **Mission start sync.** Inline plan ordering and budget arithmetic, bounded stub/side/bulk parsers, held packets delivered in per-peer order, lock order `heldLock` before `storeLock` and `fragmentMutex`.
- **Netcode rewrite.** Exact-size and finiteness checks on damage events, RNG sync and player blocks; each hit dealt once across the sender/owner rule combinations; the gate ANDs the host's room caps and treats an unpublished member as off; XPRESS output bounds and CRC over unpacked records.
- **Host data.** Paths limited to `WEAPON/` and `OBJECT/V###` names with no traversal; SHA-256 checked before parsing; parts only from the member asked; nothing fetched or used in Ask mode without approval; approvals reset per lobby.
- **Under Wine.** IAT patching, VirtualProtect, FlushInstructionCache, fixed-address VirtualAlloc, RtlAddFunctionTable, the mapped log queue, steady_clock/QPC timing and the `SIO_UDP_CONNRESET` handling have no Wine-specific problem found.

## Architecture notes

- **Capability claims are not tied to success.** Features publish their bit from the INI, not from whether they started (findings 4, 38). One "withdraw on failed install" path in `netfeature` closes this for every feature.
- **Simulation settings live in each machine's INI.** Extend, ExtraEnemies and Fragments decide what every machine simulates or can receive, yet nothing compares them (findings 5, 6, 7). Either make them host-authoritative room rules or remove them. Local-only settings (NetLog, KeepRoomOnPeerTimeout) are fine as they are.
- **Liveness has three definitions.** `usable` (single paths), `healthy` (mesh) and `linkAlive` (hold and roster). Only mesh routing uses health. One answer-based health check fixes findings 1 and 2 and simplifies `direct_net.cpp`.
- **Reliability differs by path for the same class.** Over EOS an Event datagram gets EOS reliability plus the game's resends; over a single direct path it gets the game's resends only (finding 24). The original premise, that loss of unreliable records causes desync, argues for repairing everything that is not proven full state, on every path.
- **The hold hides a close but does not recover what was lost with the connection.** Upgraded reliable packets in flight at the close and a retired direct link's pending packets are gone (finding 18). Correctness after a reconnect rests on the game's own resends.
- **Lobby state is spread across four wrapper sets.** `eos_hooks`, lobbystate, syncmarker and netlog each wrap Leave, Destroy, Create, Join and the platform tick, with two per-second beats and three copies of the current lobby. One lobby module feeding observers would remove most of the duplication and the ordering comments that have drifted.
- **Startup is a chain of independent installers.** Each has its own partial-failure policy: `Apply`, `InstallLobbyState`, `StartHostData`, `InstallPacketFit`, `InstallSyncMarker`, `InstallNetLog`, `InstallHitAuthority`, `InstallPlayerSync`. Unlike `Apply`, the import redirections have no all-or-nothing step, and HostModeHooks install whether lobby state succeeded or not.
- **W2 player sync and W3 hit authority earn their complexity.** Together they remove "I hit him, he was not hit" for players and vehicles and phantom damage from remote enemy copies. W3 moves enemy-to-player hits to the host's delayed view of the player, so dodging on a client gets worse by that lag; victim-side resolution for enemy-to-player hits is worth considering. RNG sync, interest management and smoothing do not show a benefit in source.
- **Large rooms are an implementation limit, not a tested one.** O(n²) list broadcasts (finding 27), linear scans per packet (finding 35), a 9 s game-thread wait for a 1024-player bulk, and the 256-entry id cache in `eos_hooks` all scale badly past 64. The 2.5.0 notes say 1024 is unverified; the code agrees.

## Suggested order

1. Make tests trustworthy: finding 15, the Linux cross-build with tests under Wine, and an eos_hooks test that runs without the game.
2. Remove what does not earn its place: the no-behaviour-change cleanup, then the owner decisions above. Less surface makes the remaining fixes smaller.
3. Transport correctness: findings 1, 2, 3, 9, 18, 24.
4. Room rules: findings 5, 6, 7 (one host-authoritative attribute or removal of the options), then 4 and 38 (withdraw failed capabilities).
5. Proton: findings 16, 10, 13, 14, then the `[A]` items, each confirmed on the dev box.
6. Layout, docs and the eos_hooks split.
