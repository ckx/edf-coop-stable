# edf-coop-stable

**English** | [中文](README.zh-CN.md) | [日本語](README.ja.md)

**EDF6Coop** keeps online co-op missions in EARTH DEFENSE FORCE 6 (PC / Steam) from drifting out of sync or ending when a connection drops. It resends lost packets, hides a short disconnect from the game while EOS reconnects, and offers an optional direct UDP link to the host. It is one [EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) plugin, `EDF6Coop.dll`, and runs on Windows and on Linux through Steam Proton.

> Unofficial mod, not affiliated with D3 PUBLISHER / SANDLOT / Epic Games. It changes network calls and a few game functions in memory and never modifies game files. Set `[DirectNet] Enabled=0` or delete the DLL to get the original game back.
>
> **Status: experimental.** Reliable sending and the disconnect grace period were verified in four-player online play (0.3.2: every player connected directly, and the game never saw 7 connection drops). If something goes wrong, open an [Issue](https://github.com/hajisensai/edf-coop-stable/issues) with your log attached.

## Rooms of four

This version plays the game's own rooms of up to four players. Up to 2.5.0 EDF6Coop also had bigger rooms (Player MOD), the room screen, weapon pages, room weapon files, armor copy and an automatic updater. Those are gone; whoever needs them stays on 2.5.0.

Everyone in a room should run the same version. A host refuses a player whose netcode protocol differs (`[Netcode] RejectMismatched=1`), because the two would apply different rules. Reliable sending and the disconnect grace period work with any version, and with players who do not have the plugin.

Installing renames an old `EDF6DirectNet.dll` / `EDF6MultiSlot.dll` to `.disabled`. Their settings files stay but are not read.

## What it fixes

| Symptom | Cause | What the plugin does | Who needs it |
|---|---|---|---|
| Enemies, positions or health drift apart mid-mission (desync) | EDF6 sends all online data as `UnreliableUnordered`, so a lost packet loses that piece of state for good | Sends it as `ReliableUnordered` (EOS resends lost packets; order is still not guaranteed, so the game sees nothing it could not see before) and raises the EOS send and receive queues to at least 64 MB | **The sender.** What you send is no longer lost; best when everyone has it |
| A short network hiccup throws you out of the mission | When EOS closes a connection on a timeout or network error, the game removes that player at once | Hides the disconnect from the game and lets EOS reconnect in the background. If the connection comes back within 30 s the game never notices; otherwise it is handled as in the original game | **Both sides** (the plugin detects the other side, see below) |
| A player who joins later never finishes the P2P handshake, and everyone but the host leaves the room | The original game ends the room for the guests on that timeout | Guests stay in the room (`KeepRoomOnPeerTimeout=1`) | Each guest |
| High latency through the Epic relay, NAT traversal fails | All traffic goes through EOS P2P / relay | The host can enable the direct link: others connect straight to the host after joining (star, the host relays) | The host sets it up; joiners only need the plugin |
| Hits that do nothing, players that jump around | The target decides hits; other players' positions are sent coarsely | With everyone on this version: the shooter decides hits, and player positions carry velocity for extrapolation | Everyone in the room |
| You get disconnected and do not know why | | Writes a log: NAT type, direct or relay, disconnect reasons, lobby members, per-minute traffic, crash details | Yourself |

Everything is on by default, except the direct link, which the host enables.

## Install

### Windows

1. Download `EDF6Coop-<version>.zip` from [Releases](https://github.com/hajisensai/edf-coop-stable/releases/latest) and **extract the whole archive** to any folder.
2. Double-click **`INSTALL.bat`**. It finds EDF6 in your Steam library and installs.
   - If EDFModLoader is not present, the bundled loader is installed: [BlueAmulet/EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) v1.0.10 (MIT) **with a fix for a multithread bug in the official build** (its call forwarders shared one target variable, so calls from several threads at once could jump to the wrong function; see [dist/LOADER_FIX.md](dist/LOADER_FIX.md)). An existing `winmm.dll` is never overwritten, with one exception: if it is byte for byte the official v1.0.10 file, it is replaced by the fixed build and the original is kept as `winmm.dll.bak-official`.
   - If the game cannot be found, you are asked for the game folder (in your Steam library, right-click EDF6, Manage, Browse local files).
3. Start the game from Steam as usual. The first start creates `Mods\Plugins\EDF6Coop.ini` (settings) and `EDF6Coop.log` (log).

### Linux (Steam Proton)

1. Extract the zip and run `bash install.sh` in the extracted folder. It finds EDF6 in your Steam libraries (native and Flatpak Steam) and installs as `INSTALL.bat` does; give it the game folder (`bash install.sh "<folder>"`) when it does not find it. By hand: copy the zip's contents into the game folder (the one with `EDF6.exe`; in Steam, right-click EDF6, Manage, Browse local files), but keep a `winmm.dll` you already have.
2. In Steam, right-click EDF6, Properties, and set the launch options to

   ```
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

   Without it Wine loads its own `winmm.dll` and EDFModLoader never runs.
3. Start the game. The settings file and log appear in `Mods/Plugins/` as on Windows.

The zip has README_EDF6Coop.txt (English), README_EDF6Coop_zh.txt (中文) and README_EDF6Coop_ja.txt (日本語). The settings file is commented in your Windows display language (Chinese, Japanese, otherwise English).

Upgrade: install the newer release the same way. Your settings are kept. There is no automatic update.

Uninstall: double-click `UNINSTALL.bat` (on Linux, `bash install.sh --uninstall`). It removes the plugin, settings, log, leftovers of older versions, and the UPnP mapping the plugin made on your router (only one that points to this PC and is named `EDF6DirectNet`). EDFModLoader and other mods stay, including a loader the installer upgraded and its backup `winmm.dll.bak-official`. On Linux the UPnP mapping stays until the next game start that does not host, or until you delete the entry named `EDF6DirectNet` on the router. If you added the firewall rule, run `UNINSTALL.bat` as administrator to remove it too; without administrator rights it shows the command.

## Host: the direct link (optional)

Only the host sets this up. Joiners with the plugin and `AutoJoin=1` (the default) connect to it after joining the room.

1. Open `Mods\Plugins\EDF6Coop.ini` and set `Mode=host`.
2. Windows: right-click `EDF6Coop_AllowFirewall.bat` in the game folder and choose **Run as administrator** (once). The rule allows inbound UDP for `EDF6.exe` on your `ListenPort` only (27015 if unset); run it again after changing `ListenPort`. Linux: allow that UDP port in your firewall if it has one.
3. Make yourself reachable from the internet, one of:
   - **Automatic**: leave `PublicAddress` empty. The plugin maps the UDP port with your router's UPnP and also advertises this PC's public IPv6.
   - **Manual**: forward the UDP port to this PC on your router, then set `PublicAddress=public IP:external port` (a DDNS name works too, e.g. `myroom.ddns.net:40000`).
   - **PC dials the internet itself (PPPoE, no router)**: the plugin cannot detect this kind of adapter. Set `PublicAddress=public IP:27015` by hand.
4. Restart the game and create a room as usual.

Check the log `Mods\Plugins\EDF6Coop.log`:

| Log | Meaning |
|---|---|
| `DIRECT players who join your room connect to ...` | The host address is known and in the room info |
| `UPNP router now forwards UDP ...` | The UPnP mapping worked |
| `UPNP no router with UPnP port mapping found` / `UPNP port mapping failed` | The router has no UPnP or it is off; forward the port by hand |
| `UPNP WARNING: the router WAN address ... is private (carrier-grade NAT)` | Your ISP gives you no public IPv4, so IPv4 direct links cannot reach you; only IPv6 can, or ask your ISP for a public IP |
| `UPNP UDP 27015 is already forwarded to ...; left alone` | The port already goes to another device on your LAN; the plugin does not touch it. Pick another `ListenPort` or forward by hand |
| `DIRECT client ... connected from ...` (host) / `DIRECT connected to host ...` (joiner) | Direct link up |
| `DIRECT auto-connect stopped (the room host did not answer on any advertised address ...)` | The joiner cannot reach the host (firewall, port forwarding, `Key` mismatch, another EDF6Coop version). The game keeps using Epic and the plugin tries again after 60 s |

Joiners try each address for 10 s, IPv4 before IPv6. If none answers they stay on EOS and play goes on as normal.

**`Key=`** is an optional passphrase; the link is safe without it. Every player with the plugin publishes, in its own lobby member info, the fingerprint of a key made for this game session. The host lets someone connect as player X only after they prove they hold X's key, and a joiner accepts only a host that proves it holds the room owner's key. Every direct-link packet then carries a tag made with keys only those two have, so nobody else (another player in the room, someone on the network path) can connect as someone else, take over a link, or alter, inject or replay packets. A `Key` is mixed into those keys as an extra shared secret. It is **not** written into the room info: if the host sets one, every joiner must put the same `Key` in their own ini, or auto-connect fails and falls back to EOS.

**Privacy**: the host's public address is in its lobby member attributes, so anyone who can see the room can read it. A joiner who connects directly (`AutoJoin=1`) does so from their own public address, which the host then sees (EOS peer-to-peer usually shows both addresses to each other too). Set `AutoJoin=0` to stay on EOS.

## Settings (`EDF6Coop.ini`, restart the game after editing)

| Key | Default | Description |
|---|---|---|
| `[DirectNet] Enabled` | `1` | `0` = the plugin installs nothing; the original game |
| `Mode` | `off` | `off` normal player / `host` direct-link host / `join` enter the host address by hand (rarely needed) |
| `ListenPort` | `27015` | host: the UDP port to listen on (forward this one and allow it in the firewall); join: local port, empty = automatic |
| `PublicAddress` | empty | host: where others connect. Empty = public IPv6 + UPnP-mapped IPv4 |
| `AutoJoin` | `1` | Connect to the host's direct link after joining its room (the host then sees your public address; `0` stays on EOS) |
| `HostAddress` | empty | `Mode=join` only: the host address, e.g. `123.45.67.89:27015` / `[2408:8207::5]:27015` |
| `Key` | empty | Direct-link passphrase, the same for everyone; letters and digits only |
| `UPnP` | `1` | Let the router forward the port when hosting. Used only with `Mode=host` |
| `BindPhysicalInterface` | `1` | Keep direct-link traffic on the physical network adapter, so the TUN adapter of Clash, a VPN or a game accelerator cannot capture it |
| `LinkTimeoutMs` | `60000` | How long without data from the other side before a direct link counts as lost (3000 to 300000) |
| `[EOS] FixedPort` | `0` | EOS uses the fixed UDP ports `FixedPort` to `FixedPort+7`; 0 = random |
| `Relay` | `norelay` | EOS relay: `norelay` never use Epic's relay servers / `allow` / `force` / `default` the game's own setting. Set `allow` if you cannot connect: without the relay, a player whose NAT blocks a direct EOS connection gets in only through the direct link |
| `[Sync] ReliableGameTraffic` | `1` | Reliable sending (the desync fix). `0` = original |
| `[Resilience] HoldDisconnects` | `auto` | Disconnect grace: `auto` for players with the plugin / `off` original / `all` for everyone, no detection (only if you are sure everyone has the plugin) |
| `GraceSeconds` | `30` | Longest a disconnect is hidden, in seconds (1 to 600) |
| `KeepRoomOnPeerTimeout` | `1` | Stay in the room when the P2P handshake with a later joiner never completes. `0` = original |
| `[Netcode] Fragments` | `1` | Split data above EOS's 1170-byte packet limit. Used only while everyone in the room has it on |
| `PlayerSync` | `1` | Player positions carry velocity, so other players move smoother. Used only while everyone has it on |
| `HitAuthority` | `1` | The shooter decides hits. Used only while everyone has it on |
| `RejectMismatched` | `1` | As host, refuse players whose netcode protocol differs |
| `[Display] RadarItemColors` | `1` | Pickups on the radar take the color of their type: armor red, health white, weapons green. Only your own screen. `0` = all green (original) |
| `[Log] CrashLog` | `1` | Write crash details (address, call stack) to the log |
| `CrashDump` | `0` | Also write `EDF6Coop-crash.dmp` for the first crash. It holds game memory (room name, chat), so turn it on only when asked |

Player correction measures movement per second, so uneven frame times do not turn a correctly moving copy into alternating forward and backward corrections.

## How it works

### Reliable sending

Both `EOS_P2P_SendPacket` call sites in `EDF.dll+0x12c8bc0` pass `Reliability = UnreliableUnordered`. The plugin changes it to `ReliableUnordered` in the import table: every packet arrives exactly once, possibly out of order. Unreliable delivery may already reorder packets, so the game logic sees nothing new; packets just stop getting lost. The receiver needs no cooperation.

### Disconnect grace and plugin detection

If the other player does not have the plugin, their game removes you as usual. If your side kept hiding the disconnect, the two games would diverge, so grace goes only to players who have the plugin.

EDF6 reads every EOS packet it receives as game data, so the plugin cannot send probes over P2P. It uses lobby **member attributes** instead: in a room it writes `EDF6DN=<protocol>` on itself (`EDF.dll` imports no member-attribute functions, so the game never sees it), and on a disconnect it checks whether the other player has that attribute:

- Attribute present and the connection was up before: grace, asking EOS to reconnect every 2 s.
- No attribute: handled as in the original game.
- Direct-link member: grace for as long as the direct link answers (it pings four times a second, in menus too); its game data does not go through EOS anyway.
- Epic's lobby service reports a member as disconnected (`DISCONNECTED`: it lost Epic's lobby service, not the game) while that member's direct link answers: hidden from the game. When EOS lists the member again (`JOINED`), both events are dropped. The game hears of it only once the direct link has been silent for `GraceSeconds`, or the member leaves or is kicked. The same applies to yourself.
- The other player left the lobby: the game is told at once.

### Direct link

The host writes the member attribute `EDF6DN_ADDR` (a space-separated address list). Other members read it every 2 s and try each address for 10 s, IPv4 before IPv6; if all fail they try again after 60 s. The transport is a UDP protocol of its own: selective acknowledgement, rate-limited resends, per-link keys. `IP_UNICAST_IF` binds it to the physical adapter.

A link carries game data only while the other side answers: it pings four times a second, and a link that has had no answer (a pong or an acknowledgement) for about 1.5 s stops carrying. What it had not delivered goes over EOS, reliably, and so does the game's data from then on, until the link has answered again for a second. The host relays between joiners; what it cannot deliver to a joiner goes back to the sender, which sends it over EOS, and the other joiners stop sending to that joiner through the host.

Every player with the plugin also writes `EDF6DN_ID`: the SHA-256 fingerprint of an ECDSA P-256 key made for this game session. Connecting starts with a cookie round trip: the host answers a hello with a cookie bound to the sender's address and keeps nothing until it comes back, so forged addresses and hello floods achieve nothing. The joiner repeats the hello with the cookie, signed with its key. The host accepts it for EOS ID X only if X is in the room and published that key's fingerprint; a signed hello from an earlier session is rejected as a replay. The joiner accepts the host's welcome only if the room owner's published key signed it. Hello and welcome carry fresh ECDH keys under those signatures, so every link has keys of its own (with `Key` mixed in when set), and every packet carries a tag and a counter: altered, injected or replayed packets are dropped. There is no encryption. Players whose direct-link protocols differ ignore each other's packets and stay on EOS with each other.

### Netcode version gate

Enemy and object hits are applied on the shooter's machine using the game's complete collision data, including armor and weak points. Hits on other players and vehicles are sent to their owner's machine. The `NetHit` log distinguishes replayed events from those that reduced HP; no HP change can also mean armor, invulnerability or friendly-fire rules blocked the hit. Version 3.0.1 uses netcode protocol 4, so everyone in the room must update together to use these fixes.

Every player publishes its netcode protocol and the features it has on (`EDF6NET_PROTO`, `EDF6NET_CAPS`). The host publishes what the whole room runs (`EDF6NET_ROOMCAPS`). Fragments, player sync and hit authority run only while every member runs the same protocol with the feature on; a single player without it turns it off for the room. With `RejectMismatched=1` the host removes a player of another protocol.

### Other fixes

- **Weapon guard**: another player's weapon that your weapon table does not have (their mod adds weapons) no longer crashes your game in the room or at mission start. In the room it shows as an empty slot; in a mission it becomes your own weapon of that class and slot.
- **Crash log**: on a crash the log gets the address, the module and the call stack; `CrashDump=1` also writes a minidump.

## Troubleshooting

- **Check the log first**: `Mods\Plugins\EDF6Coop.log` (it stays below about 2 MB; direct-link lines start with `[DN]`). A line `==== EDF6Coop x.y.z ====` means the plugin loaded. Without it EDFModLoader is not installed right; on Linux, check the launch option above.
- `REFUSED: EDF.dll is not the supported build`: a game update changed `EDF.dll`. The plugin changes nothing and the game runs as without it. Please open an Issue.
- `EOS hooks FAILED`: the `EDF.dll` import table is not as expected; the plugin leaves EOS alone. Please open an Issue.
- `LOBBY ... other players treat us as vanilla`: the plugin could not publish its lobby attribute, so other players give you no disconnect grace.
- `EOS incoming packet queue FULL`: the EOS queue is full and packets are dropped. Please open an Issue with your log.
- `RESILIENCE ... RECOVERED`: a disconnect was hidden. `did not come back within`: it was handed to the game after `GraceSeconds`.
- `RESILIENCE ... lost Epic's lobby service but the direct link is up` / `back in Epic's lobby service`: Epic's lobby service dropped a player and let them back in; the game never saw it.
- `NETCODE features in room ... off: ...`: someone in the room runs another protocol or has a feature off; the log names who. `NETCODE REFUSED`: as host you removed a player of another protocol.
- `STATS last 60s: ...`: one line per minute while the game sends or receives: what the game sends (kbps), what the direct link uses on the wire (`wire up` / `down`, resends included, and what the host relays for others), direct and EOS packet counts, how many went to EOS reliably, send failures, fragments, and per link the resend state. If `send-failures` is not 0, please attach your log.
- `DIRECT ... has not answered for ... ms: game data goes over EOS` / `answers again`: the direct link stopped working for a moment and EOS carried the game meanwhile. `... packets the link to ... had not delivered: ... go over EOS` names what was handed over. In the STATS line such a link shows `NOT ANSWERING`.
- `DIRECT ... timed out: nothing received for ...`: the other side went silent. `DIRECT ... stalled: a packet the game sent reliably is unacknowledged ...`: the other side answers, but one packet the game needs never got through.
- `DIRECT refused hello for ...`: someone tried to connect as a player and could not prove it. `published no direct-link identity` for a second or two after a player joins is normal (their room info has not reached the host yet). `not signed by the identity that player published` means someone else claimed to be that player and was turned away.
- `DIRECT the socket's receive buffer is N bytes, not the 1 MB asked for` (Linux): a host's burst beyond that is dropped and resent. Raise the limit with `sudo sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576`.
- `UPNP WARNING: the router did not say its WAN address`: the mapping is there, but the plugin cannot tell joiners your IPv4 address. Set `PublicAddress`.
- `DIRECT ... speaks direct-link protocol N, we speak M`: that player runs another version. There is no direct link between you; the game keeps working over EOS.

## Build

### Windows

Requires Windows x64, Visual Studio 2022 with *Desktop development with C++* (its CMake and Ninja are used), Python 3.10+ with `pip install pefile==2024.8.26 capstone==5.0.9`, and EARTH DEFENSE FORCE 6 for the tests against the game's code (EDF.dll build `678CCB46`).

```powershell
$env:EDF6_GAME_DIR = 'C:\Program Files (x86)\Steam\steamapps\common\EARTH DEFENSE FORCE 6'
powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # -> out\EDF6Coop.dll
powershell -ExecutionPolicy Bypass -File package.ps1        # -> release\EDF6Coop-<version>.zip (+ .sha256)
```

- `-Test` runs the tests: unit tests, loopback direct-link tests, and the tests against the game's code, which run rooms of several processes on the real `EDF.dll` with a fake EOS. Without the game those are reported as skipped; `-RequireGame` refuses to configure without it.
- The game folder is only read.
- `build.ps1` fetches the official EDFModLoader v1.0.10 `winmm.dll` (checked by SHA-256) and builds the race-fixed loader the package ships (`tools/fix_winmm_proxy.py`).
- `package.ps1` refuses a DLL of another version or one older than its sources.

### Linux

`build.sh` cross-builds the same DLL with clang for MinGW-w64 (`cmake/mingw-w64.cmake`) and runs the tests under Wine, in a Wine prefix of their own (`~/.local/share/wine-edf6coop-tests`). It needs no Windows SDK or Visual Studio. On Fedora:

```sh
sudo dnf install clang lld mingw64-gcc-c++ mingw64-winpthreads-static wine cmake ninja-build python3
export EDF6_GAME_DIR="$HOME/.local/share/Steam/steamapps/common/EARTH DEFENSE FORCE 6"
./build.sh -t    # -> out/EDF6Coop.dll; -r refuses to configure without EDF.dll
```

The Python tools run from a virtual environment `build.sh` makes in `~/.local/share/edf6coop-venv` (override with `EDF6COOP_VENV`). Release packages are built with `build.ps1` and `package.ps1` on Windows.

## Release

Release packages are built on a machine with the game, so every test against the game's code runs. GitHub Actions runs the other tests (`.github/workflows/ci.yml`).

1. Bump the version: `project(EDF6Coop VERSION x.y.z)` in `CMakeLists.txt`.
2. Write the release notes `release-notes/<version>.md` (the body of the Release page and `RELEASE_NOTES_EDF6Coop.md` in the package).
3. Commit to `main`, tag it and push the tag: `git tag vX.Y.Z && git push origin vX.Y.Z`.
4. On the machine with the game, at that commit: `powershell -ExecutionPolicy Bypass -File release.ps1 -Upload`. It runs `build.ps1 -Test -RequireGame` and `package.ps1`, then creates a **draft** Release with `EDF6Coop-<version>.zip` and its `.sha256` (without `-Upload` it only stages them in `release\upload-<version>\`).
5. Check the draft on GitHub and publish it.

## Layout

| Path | Contents |
|---|---|
| `game/plugin.cpp` | EDFModLoader entry point: reads `EDF6Coop.ini`, starts the transport part, patches `EDF.dll` |
| `game/` | The game part: patches (weapon guard, peer timeout), netcode version gate, player sync, hit authority, crash log, log |
| `src/config.*` | Settings and their comments (Chinese / Japanese / English after the Windows display language) |
| `src/eos_min.h` | The EOS SDK structs in use (from the official 1.15.5 headers; the game uses 1.16.1) |
| `src/eos_hooks.cpp`, `src/iat.*` | Patches the `EDF.dll` import table to take over EOS P2P and lobby calls |
| `src/hold.*` | Disconnect grace |
| `src/lobby_marker.*` | Lobby member attributes: who has the plugin, the host address |
| `src/direct_net.*`, `src/reliable.*`, `src/wire.*`, `src/auth.*`, `src/fragment.*` | Direct link transport, fragments |
| `src/netif.*`, `src/upnp.*` | Physical adapter detection, UPnP |
| `CMakeLists.txt` | The build and the one version number (`project(EDF6Coop VERSION x.y.z)`) |
| `dist/` | Install scripts, bundled readmes (English / Chinese / Japanese) and the loader fix note |
| `tests/` | Tests: the transport part; `tests/game/` the game part; `tests/gamenet/` rooms of several processes on the real `EDF.dll` |
| `tools/`, `third_party/` | The loader fix tool and its inputs, the MSVC environment script; EDFModLoader's plugin header and license |

## Known limitations

- Joining in the middle of a mission is not possible (the game has no such feature).
- Only desync caused by packet loss is fixed; desync in the game logic itself needs concrete symptoms before it can be reverse-engineered.
- During the disconnect grace period other players may wait at a sync point, for at most `GraceSeconds`.
- The host relays between joiners: when both the host's link to the receiver and its link to the sender fail at once, what the host was relaying between them is lost.
- Direct-link traffic is authenticated, not encrypted: someone on the network path can read it and can always drop it (the game then falls back to EOS).
- UPnP mappings are not removed when the game exits (there is no safe point for network calls then). The plugin removes its mapping at the next start that does not host (`Mode` not `host`, or `UPnP=0`), and `UNINSTALL.bat` removes it too. It never touches mappings of other devices. You can also delete the mapping named `EDF6DirectNet` in your router's admin page.
- Nothing can be hidden when the direct link itself is down: if a player's own internet is gone for longer than `GraceSeconds`, the game handles it as usual.

## License

MIT, see [LICENSE](LICENSE). The bundled EDFModLoader is MIT; its license is in the package as `EDFModLoader_LICENSE.txt`. The game part (`game/`, first written by momotori01) is public domain ([Unlicense](game/LICENSE)).

## Acknowledgements

- The game part started as EDF6MultiSlot by momotori01: [EARTH-DEFENSE-FORCE-6-VR-MOD](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD) (public domain).
