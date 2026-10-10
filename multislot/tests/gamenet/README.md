# Game-code network tests (GameNet_*)

Rooms of 2 to 4 players, played without the game window, without Steam and without Epic: each player is a
process (`GameMachine.exe`) that loads the installed game's real `EDF.dll` and the real `EDF6Coop.dll` (through
`EML6_Load`, as EDFModLoader does), and the machines reach each other through a fake EOS SDK
(`fake_eos_net.cpp`, named `EOSSDK-Win64-Shipping.dll`) over one shared memory section. `GameNetTests.exe` sets
up the room, starts the machines, collects what they report and checks it.

They need the game installed (`EDF6_GAME_DIR`; from the game folder only `EDF.dll`, `steam_api64.dll` and
`umbra_sandlot.dll` are loaded, by their full paths, and nothing there is written) and report themselves skipped
without it, as the other tests against the game's code do. Each scenario takes 2-12 s.

```powershell
$env:EDF6_GAME_DIR = 'D:\steam\steamapps\common\EARTH DEFENSE FORCE 6'
powershell -ExecutionPolicy Bypass -File ..\..\..\build.ps1 -Test     # all tests, GameNet_* among them
ctest --test-dir ..\..\build -R GameNet --output-on-failure           # only these
```

## What is the game, and what stands in for it

The game's code, unchanged except for EDF6Coop's own patches and hooks:

- the P2P handshake that connects the members (`p2p::Manager`, `Link`), the packet controller
  (`packet::ControllerImpl`), the per-peer datagrams, their AES-CTR encryption (keyed by the lobby id, as in the
  game), decryption, de-duplication, acknowledgement and resend, and the dispatch of records to subscribers;
- the game's serializers, which the player sync test writes and reads player records with;
- every EOS call goes through EDF.dll's import table, so EDF6Coop's wrappers (the reliable upgrade, the
  disconnect hold, the lobby marker, the netcode version gate, the direct link) see the same calls as in the game.

Stand-ins, each as small as the game code allowed:

| Stands in for | Where | What it does |
|---|---|---|
| Epic's EOS SDK | `fake_eos_net.cpp` | lobbies and P2P as EOS behaves where the game and the plugin can tell: 32-digit lobby ids, packets above 1170 bytes refused with `EOS_LimitExceeded`, a peer's packets wait until the connection is accepted, order kept per channel only, completions and notifications (connection requests and establishments, members joining, leaving and updating) in `EOS_Platform_Tick`. Every other import is a stub that reports when it is called. |
| `eos::internal_Core` | `game.cpp` | the three observables the network subscribes to, and the platform handle; ticked through the game's own `internal_Core::Update` |

Guests join one after another in seat order, so seat n is player n+1. A room is entered once every member
publishes its netcode protocol (`EDF6NET_PROTO`), which EDF6Coop does on its own from the game's EOS ticks.

## Scenarios

| Test | Machines | What it plays |
|---|---|---|
| `GameNet_room` | 2 | the room is created and joined through the game's imports; every member publishes its netcode protocol |
| `GameNet_link` | 2 | the game's handshake connects them; a record sent through the game's controller arrives; no plaintext crosses EOS |
| `GameNet_playersync` | 2 | player records with EDF6Coop's player block, written and read with the game's serializers, arrive exactly |
| `GameNet_versiongate` | 3 | the third machine runs another netcode protocol: the host refuses it, and the other two run the netcode features once it is gone |
| `GameNet_netstats` | 3 | every machine sends state records (unreliable to the game) and reliable ones while each machine loses its first 40 unreliable packets of 80 bytes or more: the state datagrams go to EOS reliably, nothing is lost, every record arrives |
| `GameNet_netstatsunreliable` | 3 | the same with `[Sync] ReliableGameTraffic=0`: packets are lost and state records never arrive, which is what `netstats` guards against |
| `GameNet_direct` | 3 | the host listens on the direct link, the joiners dial it over loopback: both connect, and every record arrives |

Network imperfections, set per scenario: `EDF6NET_DELAY=<channel>:<ms>`, `EDF6NET_DROP=<bytes>:<count>` (only
unreliable packets), `EDF6NET_LOBBY_DELAY=<ms>`; per seat `EDF6NET_DELAY_FROM=<user>:<ms>` (everything that
machine receives from that member). `EDF6NET_SECONDS` sets how long the netstats role sends.

## What they caught

- A role subscribed to the game's packet controller only after its handshakes with every other member ended
  (`Connect`). The controller delivers a record on the tick it arrives, to whoever listens then, and acknowledges
  it either way, so its sender never resends it: a machine still in a handshake lost the first record the others
  sent. The game's event controller listens from the moment the network exists; the roles now subscribe right
  after `Transport::Start` (`Connect`'s `listen`).

## Adding to it

A scenario is a list of seats (user, role, INI) and a check (`scenarios.h`); a role is what one machine does
(`roles.cpp`). A seat without an INI runs the game without EDF6Coop. Machines report with `Result("key", ...)`
lines, and the check reads them; every machine's `EDF6Coop.log` is printed after its output.

`EDF6NET_DEBUGGER=<path of cdb.exe>` runs every machine under the debugger, which prints the stack of a crash.

The RVAs used here are those of the Steam release EDF6Coop supports; `EDF6Coop` refuses any other `EDF.dll`, and
so does every machine.
