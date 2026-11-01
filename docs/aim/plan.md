# Remote aim driven every frame

Board: #12002 (plan-2)
Project: mods


Remote player copies show a wrong facing now and then, and their shots leave in
that facing. Enemies never do. `docs/net-re/enemy.md` (2026-10-31) reads why: a
host-owned enemy replicates position and rotation as one sequenced record, the
receiver queues the records and plays them back in order, and the copy's
rotation is written from that playback every frame, so nothing local can own
it. A remote player's aim arrives as unsequenced pitch and yaw, the native
reader stores it once, and the copy's own code is free to touch the angles until
the next record.

This plan brings the enemy's three rules to remote player copies: ordered,
fresh samples (3.6.2 and 3.6.3 did the first two for the native aim), and a
per-frame write of the aim into the copy. It replaces the blocked issue #12008
on the Network sync board (aim in the block behind a protocol bump), whose
wire change becomes a later phase here, taken only if the live check needs it.

User decision 2026-10-31: proceed with per-frame aim application, modelled on
the enemy pose control. Related decisions on the spectator camera stay as they
are (`docs/spectate/plan.md`).

## What is already there

- The native record carries pitch `+0x1230` and yaw `+0x1234` plus `+0x1E44`
  under mask bit 2; the reader `0x592180` stores both and clears `+0x1248`
  (`docs/net-re/facing.md`). Since 3.6.3 the sender sets bit 2 on every block
  it writes on foot (`MaskHook`, `game/netplayer_game.cpp`), so a receiver that
  accepts a block also receives the aim that goes with it.
- Since 3.6.2 `ReadHook` rejects paired aim older than the last accepted one,
  per copy (`AimOrder`), and restores the three native writes for a stale one.
- `UpdateHook` (slot 55) runs every frame for every remote copy, after the
  game's own `NetworkUpdate`, and already drives the position. The aim write
  goes in the same place.
- The locked spectator camera smooths the watched copy's angles itself with a
  50 ms time constant (`game/spectate.cpp`, 3.6.1), because the copy's angles
  step every 100 ms. Once the copy is driven, that smoothing is doubled.
- `PlayerAimGameCode` (`tests/game/aimtrace_test.cpp`) runs the installed
  hooks against the real native serializer and reader.

## Design

Sender: unchanged. Protocol 4, block version 1, native aim with every block.

Receiver, per remote copy, in the game-free part (`game/netplayer.*`):

- The aim sample is the native pitch and yaw as the native reader stored them,
  read back in `ReadHook` after the original reader ran, with the block's
  sequence and sender time. It replaces `AimOrder`: the ordering check moves
  into `AcceptAim`, which rejects a sample not newer than the last within the
  freshness window and restarts on a stale track. No new bytes.
- `StepAim(track, now)` returns the angles to write this frame: the stored
  target smoothed from the last written value with a time constant
  (`[NetPlayer] AimTauMs`, default 50 ms, the camera's value), yaw through the
  shortest arc, with the same bounded 4..50 ms step as the position. A new
  track, or a target further than `AimSnapRadians` from the written value
  (default pi/2: a quick about-turn is shown, not slurred), writes the target
  outright. No extrapolation: the block carries no rate, and the enemy control
  does not extrapolate either.
- The write stops when the track goes stale (`staleMs`, 400 ms, as for the
  position); the native path then owns the angles again. Vehicles and the
  downed state send no native aim, so they are never driven, which matches the
  game's own rule and the camera's seated-heading fallback.

Receiver, in the game (`game/netplayer_game.cpp`):

- `UpdateHook`, after the original `NetworkUpdate`, for a remote copy with a
  fresh aim track and the feature gate on: write pitch to `+0x1230` and yaw to
  `+0x1234`. `+0x1248` stays what the native reader left (0). Nothing else is
  written; the pose follows from the angles as it does today.
- Counters: `aimDriven` and `aimSnaps` in the `PLAYER sync` summary. The aim
  diagnostic's `update` record gains the written values so send, receive and
  update can be compared as before.
- `[NetPlayer] DriveAim=1` turns it off without touching the rest. The
  existing `[Netcode] PlayerSync` gate covers it: a receiver on an older build
  simply keeps the native behaviour, and a sender on an older build (before
  3.6.3) gives sixth-frame aim, which is still driven, just with longer holds.

Phase 1 (2026-10-31, `docs/net-re/player.md`, "frame order of the aim writers"):
`System::Update` runs slot 4 of every soldier (input, body follow, matrices) before
slot 5 (0x5750E0, slot 55 at `0x575BBD`) and slot 9 (states) in each iteration. A value
written in `UpdateHook` reaches the next iteration's slot 4, where the body angles
`+0x1240/+0x1244` are pulled 30 percent toward the aim and the pose is built: one
step late, no repeat from the `System::Update` hook. The write site is slot 55.
Between writes slot 4 adds one step of the replicated look stick (`+0xD60`, slot 54) to
the aim, which the next write replaces.

## Phases

| Phase | Scope | Tier |
| --- | --- | --- |
| 1. Frame order of the aim writers | Read `0x5750E0`, `0x572DF0` and the pose build from `+0x1230/+0x1234/+0x1244` to establish, per frame, where slot 55 runs against the angle writers `0x573BB0`, `0x587DAC` and `0x5705A5`, and whether a value written in `UpdateHook` reaches that frame's pose. Record it in `docs/net-re/player.md` with addresses, and name the write site (slot 55, or the `System::Update` hook as a repeat). | lead |
| 2. Aim track and step arithmetic | In `game/netplayer.*`: aim fields on `RemoteTrack` (target pitch and yaw, sequence, arrival time, written pitch and yaw), `AcceptAim` taking over `AimOrder`'s rule, `StepAim` with the time constant, shortest-arc yaw, the snap radius and the bounded step, stale handling. Unit tests in `tests/netplayer_test.cpp`: convergence from a step, wrap at plus and minus pi, snap on a large change, restart after stale, reordered and repeated samples, uneven frames. | engineer |
| 3. Per-frame write in the game | `ReadHook` stores the paired native aim into the track after the original reader (remove `AimOrder` and its restore path, keeping the stale-aim behaviour through the track). `UpdateHook` writes the stepped angles at the site Phase 1 names. `DriveAim` and `AimTauMs` in the INI, config comments in the three languages, counters and the diagnostic field. Extend `PlayerAimGameCode`: delivered paired records drive the copy's angles over following frames, a local overwrite of `+0x1234` between records is replaced on the next frame, a stale track leaves the angles alone, and `DriveAim=0` changes nothing. Both toolchains, non-stress suite. | engineer |
| 4. Two-machine scenario and release | GameNet `playersync` scenario: the sender turns while the receiver runs, the receiver's summary shows `aimDriven` and no refused blocks, the copy's angles follow the sender's. Release 3.8.0 with notes (protocol 4 unchanged, plays with 3.0.1 to 3.7.0), README and `dist/README_EDF6Coop.txt` for the two settings, install locally. | engineer |
| 5. Live check of driven aim | With `[Diagnostics] Player=1` on all machines, in free and locked spectator modes: a teammate turning on foot shows a continuous turn without 100 ms steps; after a hit or knockdown the facing follows their aim within a second; entering and leaving a vehicle hands the angles back and forth without a jump; their shots leave in the direction they face. Note time, player and mode on any failure, close the game, collect all logs. | user |
| 6. Camera on the driven angles | After Phase 5: the locked camera reads the copy's driven angles without its own 50 ms smoothing when the aim track is fresh, keeping its smoothing for undriven copies and the seated heading. Sends nothing; `SpectateCamera` tests. | engineer |
| 7. Full-precision and seated aim in the block | Only if Phase 5 shows half-float steps in the facing or a gap while seated: block version 2 with pitch and yaw (and the seated heading) behind `kNetProtocol + 1`, mixed rooms on version 1, GameNet mixed-version case. This is the former #12008. | lead |

## Acceptance

- Phases 1 to 4 landed on `main`, 3.8.0 published and installed locally, the
  non-stress suite green on both toolchains including the GameNet case.
- Phase 5 reported on its issue with logs. A green suite does not stand in for
  it: whether a copy's facing and shots follow its owner is only seen in play.
- Phases 6 and 7 are decided on Phase 5's result, not started before it.

## Evidence limits

The enemy model is read from code, not traced in a session. That a remote
copy's angles are changed by local code between records is not established
(the 2026-10-22 capture saw no such change); the per-frame write's proven gain
is the removal of the 100 ms steps on the copy and the camera, and a hold
against the matrix writers if they fire on a copy. The bullet message path
(`docs/reviews/2026-10-26-net-sync.md`, finding 3) is untouched: a shot still
leaves with the copy's angles at the time its fire message arrives.
