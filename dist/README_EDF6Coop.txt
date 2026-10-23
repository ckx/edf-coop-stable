EDF6Coop - steadier online co-op for EARTH DEFENSE FORCE 6
Project page: https://github.com/hajisensai/edf-coop-stable
中文: README_EDF6Coop_zh.txt / 日本語: README_EDF6Coop_ja.txt
If something goes wrong, open an Issue and attach Mods\Plugins\EDF6Coop.log.

EDF6Coop is one EDFModLoader plugin (Mods\Plugins\EDF6Coop.dll) that keeps online missions from drifting out
of sync or ending when a connection drops:
- lost game packets are resent (the desync fix);
- a short connection drop is hidden from the game while EOS reconnects (up to 30 s);
- a guest stays in the room when the P2P handshake with a later joiner never completes;
- an optional direct link to the host when Epic's relay is slow or NAT traversal fails;
- with everyone on this version: the shooter decides hits, and player positions carry velocity.
Enemy/object hits keep the game's complete collision data on the shooter's machine, including armor and weak
points. Hits on other players/vehicles go to their owners. NetHit logs separate replayed events from HP reductions;
armor, invulnerability and friendly-fire rules can prevent HP loss. Version 3.0.4 uses netcode protocol 4; update
everyone in the room together to use these fixes.
On your own radar, pickups take the color of their type: health white, armor and weapons green
([Display] RadarItemColors=0 keeps them all green).
It plays the game's own rooms of up to four players. The bigger rooms (Player MOD), room screen, weapon pages,
room weapon files, armor copy and automatic updates of 2.5.0 and earlier are gone; whoever needs them stays
on 2.5.0. Everyone in a room should run the same version: a host refuses a player whose netcode protocol
differs ([Netcode] RejectMismatched=1).
Player correction measures movement per second, so uneven frame times do not turn a correctly moving copy into alternating forward and backward corrections. A correction the physics does not apply (a landing, a stagger, the ground under a downward push) is not read as the copy moving the other way, so it no longer grows into a shove. The velocity a player sends covers the 100 ms at which the others receive its position, not the last 33 ms, so the start of a dash is not sent as a stop.

The default [NetPlayer] MaxExtrapolateMs is 350 ms, retaining diagnostic 5’s prediction window. Explicit INI overrides are preserved. Longer prediction bridges short update gaps but can overshoot when a player stops or turns; the stale-data fallback remains 400 ms.

Laser diagnostics write up to eight LASER sample lines per 30 seconds and an activity summary when the next laser callback occurs. Samples include owner identity when available, position, forward direction, age, lifetime, beam endpoints and render dimensions, and observed draw callbacks. receive-before means the laser’s native network callback ran; it does not count every shot announcement. Draw callbacks do not prove visible pixels, and zero callbacks before the first render do not prove a missing beam. These diagnostics do not establish whether damage synchronized. When a beam differs, note the time and collect the shooter’s and observers’ logs after closing the game.
[Diagnostics] Laser=0: disable laser diagnostics.

Player motion diagnostics are on by default. A correction of at least 0.25 m, a warp, or a sample age of at least 200 ms captures up to eight preceding and sixteen current/following driven frames. Each object can trigger once per 30 seconds, with a total limit of 96 PLAYER motion lines per 30 seconds. The lines record received positions and velocities, proxy and object positions, observed movement, requested corrections and the estimated local velocity. t is the original hook time in milliseconds; preceding frames are logged later. These traces do not prove that physics applied a requested correction. Set [Diagnostics] Player=0 to disable them. When reporting jitter, note the time and collect the host and affected joiners' logs after closing the game.

[Install / uninstall]
Windows: extract the whole zip, double-click INSTALL.bat, then start the game from Steam as usual. (Or drop
the contents of the zip into the game folder, next to EDF6.exe: the layout is the game folder's.)
Linux (Steam Proton): extract the whole zip and run "bash install.sh" there (it finds the game in the Steam
libraries; or "bash install.sh <game folder>"), then in Steam set the game's launch options (Properties) to
    WINEDLLOVERRIDES="winmm=n,b" %command%
Without it Wine loads its own winmm.dll and EDFModLoader never runs.
If EDFModLoader (winmm.dll) is missing, the bundled one is installed: official v1.0.10 with a fix for a
multithread bug (LOADER_FIX.md). An existing winmm.dll is never overwritten, except one that is
byte for byte the official v1.0.10 file: it is replaced and kept as winmm.dll.bak-official.
Coming from EDF6DirectNet / EDF6MultiSlot: their DLLs in Mods\Plugins are renamed to .disabled. Their
settings files stay but are not read. The menu layout and HUD files that 2.5.0 and earlier wrote under Mods
(UI\LYT_MAINFRAME.SGO, HUD\ONLINEHUDTEXTURE.RAB) are deleted at the first start; another mod's file of the
same name stays.
Upgrade: install the newer release the same way; your settings are kept.
Uninstall: double-click UNINSTALL.bat (as administrator to remove the firewall rule too). It removes
EDF6Coop's files, settings, log and its UPnP mapping. EDFModLoader and other mods stay. On Linux run
"bash install.sh --uninstall"; it leaves the UPnP mapping, which goes at the next game start that does not
host, or delete the entry named EDF6DirectNet on the router.

[Direct link (optional, host only)]
Players joining your room connect straight to you. The ini comments (Chinese / Japanese / English, after
the Windows display language) explain every setting.
1. Set Mode=host in the [DirectNet] section of EDF6Coop.ini.
2. Windows: right-click EDF6Coop_AllowFirewall.bat in the game folder -> Run as administrator (once, and
   again after changing ListenPort).
3. Be reachable: leave PublicAddress empty (the plugin maps the UDP port with UPnP and also advertises your
   public IPv6), or forward a UDP port yourself and set PublicAddress=your public IP:port.
4. Create a room as usual. "DIRECT client ... connected" in the log means a player is linked directly.
Joiners need nothing: they find the host's address in the room. If the link cannot be made, play goes on
through Epic as usual. Key= is an optional shared secret: every joiner must enter the same Key.

[Log]
Mods\Plugins\EDF6Coop.log: one log for everything (direct-link lines start with [DN]). It keeps itself
below about 2 MB. Please attach it to bug reports.

[Turning it off]
[DirectNet] Enabled=0 in EDF6Coop.ini: the plugin unloads itself and the game is the original.

[Included]
- EDF6Coop: Mods\Plugins\EDF6Coop.dll (MIT, LICENSE.txt; the game part, first written by momotori01 as
  EDF6MultiSlot, is public domain).
- EDFModLoader by BlueAmulet (MIT, EDFModLoader_LICENSE.txt): winmm.dll, with the fix in LOADER_FIX.md.
