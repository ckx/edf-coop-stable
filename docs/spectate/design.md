# Spectator camera design

The spectator camera was the standalone EDF6Spectate plugin (0.1.0 to 0.4.0) and is
part of EDF6Coop from 3.1.0 (`game/spectate.*`). Version numbers below up to 0.4.0
are EDF6Spectate releases.

## Goal

In an online room, a player who is down and waiting for a revive watches a living
teammate through the game's own follow camera instead of looking at their own body.
The camera orbits that teammate and points where the downed player looks, so they aim
it with their normal look controls. When they are revived, or nobody is left to
watch, the view returns to their own camera.

## Behavior

- Online only. Offline play, including split screen, is never touched.
- When the local player's health reaches 0 while their character stays in the room
  (the downed state), view 0 switches to a camera following the teammate with the
  lowest slot who is not down.
- The followed teammate stays followed while they stand. When they go down or leave,
  the view moves to the next standing teammate in slot order, wrapping round.
- When the local player is revived, the view returns to their own camera. It also
  returns when every teammate is down.
- The spectator camera orbits the watched teammate at the downed player's own look
  pitch and yaw, as set by mouse or right stick through the game's bindings. It does
  not copy the teammate's aim. Version 0.4.0 adds this; it is unverified online.
- A camera following someone who left the room is never left current.
- While the game holds two sets of players at the end of a mission, the plugin does
  nothing, so the game's own camera change goes through.
- Nothing is sent over the network. Each player's plugin acts on its own view only.
  Players without the plugin see no difference.
- Version 0.3.0 attaches only a health gauge and multiplayer name/health indicators.
  The gauge reads the watched teammate's current/maximum health directly. From 3.1.2
  the watched teammate is the indicators' viewer: they show every other player, the
  downed local player's own body included, and not the watched teammate, who fills
  the middle of the view. (0.3.0 to 3.1.1 kept the local player as viewer, which hid
  their own body's indicator, as the game's own camera does.) Names use the game's
  settings.
- From 3.1.2 the spectator camera also carries a chat log of its own (the game's
  `HudPlayer_Chat`), so chat stays readable while watching. It shows the messages
  received while it exists; the local camera's log keeps the full history and shows
  it again on revival.
- Returning to the local camera returns its original HUD. No ammo, radar, damage
  or class gauges are created for spectator cameras. The smaller HUD is unverified
  online. Full combat HUD support is deferred in favor of health and names.

- From 3.2.0 a press of the space bar (the W key from 3.2.1) while down moves the view to the next
  standing teammate in slot order, wrapping round; the chosen teammate stays watched
  until they go down or leave. The key counts as held, so the next press needs a
  release first, while the game's text entry (the chat box) is open, while the game
  window is not in the foreground and while the local player stands. Keyboard only
  (user decisions 2026-10-23, W 2026-10-24). Unverified online.

Not in the prototype: a detached free-fly camera, watching while alive, and offline
split screen.

## Runtime design

The plugin takes the `System::Update` call in `Application::Update` (`0x705715`).
Each frame, before calling the game's `System::Update`, it:

1. Returns unless `IsOnlineMode()` is true and view 0's current camera is a
   `CharacterGhostCamera`.
2. Reads the players from the TeamManager map, as the game's `create_remote_camera`
   debug command does: the local player (local controller, not remote) and the
   remote players (remote bit, network record), with slot and health. It returns if
   any slot has two players.
3. Works out whom the current camera follows: the local player, a teammate by slot,
   or someone gone.
4. Chooses the target with `NextTarget` when the local player is down, else the
   local player's own camera.
5. Sets the target teammate's camera parameters to orbit at the local player's look
   angles, and clears that setting on every other remote player (see Orbit camera).
6. When the target differs, it makes an existing camera in view 0's list that follows
   the target current (`weak_from_this` and the view switch, as the game's
   `0x11932F0` does), or, for a teammate without one, creates a camera through the
   game's factory with the same InitParam `create_remote_camera` builds. The game's
   flush in the `System::Update` that follows makes the new camera current.
7. For a new camera, attach the health widget through `0x7FC9A0` using only the
   `lyt_HudPowerGauge01.sgo` layout and the watched player. Attach multiplayer
   indicators through `0x7FD580` with the watched teammate as the viewer, and the
   chat log through `0x7FCE60` with the local player as the viewer, as on the game's
   own camera. Release all three returned weak references; the camera owns the
   objects. The full HUD builder at `0x7FE5D0` is never called. Invalid health/max
   values skip the health widget.

The plugin keeps no game pointers between frames. Its only state is a bit per slot
for cameras it asked for during the current downed period, so a camera that fails to
appear is not requested every frame. Cameras it creates stay in view 0's list until
the System tears the views down at the end of the mission, and are reused if the
player goes down again. The game updates only a view's current camera, so the extra
cameras cost nothing while unused.

The camera factory returns a weak reference; the System's queue owns the camera.
The plugin releases only that weak reference. Version 0.1.0 incorrectly released a
strong reference and crashed on its first live camera activation. Version 0.1.1
corrects this. The user confirmed that 0.1.1 worked online; its log also recorded
following a teammate and returning to the player's own camera. Version 0.2.1
restores that camera-only path after the HUD addition in 0.2.0 crashed online.
Version 0.3.0 adds the two isolated HUD components described above.

Every read and call runs in `Guarded`. A fault stops the plugin for the rest of the
session and logs its exception code and instruction address; the game call is not
retried. This cannot undo partial game
object creation or make those objects safe for later native updates.

## Orbit camera

A `CharacterGhostCamera` copies a camera parameter block from its target each update,
before its follow state computes the view. Byte `+0x40` of that block switches the
follow state from the target's own facing to an orbit at the pitch/yaw pair at
`+0x50`, with the target's normal offsets, smoothing and position. The game never sets
this byte to anything but 0 on a soldier, so the plugin owns it. Each frame, before
`System::Update`, it writes the downed local player's look pitch (`+0x1230`) and yaw
(`+0x1234` plus `+0x1E44`, as the network sends it) into both blocks of the watched
remote soldier and sets the byte. Every other remote soldier with the byte set gets 0
back. The local player's own soldier and camera are never written.

No game function sets these fields, so the plugin writes them. The writes touch only
the local copy of a remote player. The only readers found are the ghost camera's
constructor and update, and class updates that run for local players. Only the
plugin's spectator cameras follow remote players. Nothing is sent to the network. A parameter reload the
game runs inside `System::Update` (on a revive or some state changes) clears the byte
for one frame, after which the plugin sets it again.

Inference from static analysis: a ghost camera in orbit mode reports through vtable
slot 9 that HUD widgets should hide. The health gauge checks that slot, so it is
expected to be hidden while orbiting. The multiplayer name/health indicators do not
call it. Whether the downed player's look input keeps working while the spectator
camera is in orbit mode, and how the HUD behaves, are live checks.

## Installation and refusal

EDF6Coop's `LoadGame` checks the image timestamp and size. With
`[Display] SpectateWhenDown=1` (the default) it then calls `spectate::Install`,
which checks that the call at `0x705715` targets `System::Update` and the FNV-1a 64
hash of every range listed in the research notes. It adds a stub to a thunk page of
its own within rel32 reach of EDF.dll, registered with the unwinder like EDF6Coop's
other stubs, and rewrites the call. Any failure before the write leaves the call
alone and logs why. Only the spectator camera is then off; the rest of EDF6Coop
runs.

## Compatibility

- EDF6Coop: the spectator camera is one of its parts. No other part takes the
  `System::Update` call or patches a hashed range; player sync and hit authority do
  not touch cameras. The user reported successful camera following with
  EDF6Spectate 0.1.1 installed next to EDF6Coop. EDF6Coop renames a standalone
  `EDF6Spectate.dll` to `.disabled`, since both would take the same call.
- Other mods that change any hashed range turn the spectator camera off.
- Every hashed range matched in the running game on 2026-10-11, with EDF6Coop and
  Lootmaster's Patcher installed. The ranges added in 0.4.0 matched the running
  game on 2026-10-22.

## Open decisions

- A controller button or a rebindable key for cycling, and a key to return to the
  own camera while down. W is decided; the game's bound actions would be the
  route for these (`research.md`, "Teammate cycling input").
- Whether to watch while the local player has bled out, if the game has such a state.
- Whether a separate fixed spectator-name caption is needed, now that the watched
  teammate has no indicator of their own (3.1.2).
