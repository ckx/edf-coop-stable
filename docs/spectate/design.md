# Spectator camera design

The spectator camera was the standalone EDF6Spectate plugin (0.1.0 to 0.4.0) and is
part of EDF6Coop from 3.1.0 (`game/spectate.*`). Version numbers below up to 0.4.0
are EDF6Spectate releases.

## Goal

In an online room, a player who is down and waiting for a revive watches a living
teammate through the game's own follow camera instead of looking at their own body.
By default the camera orbits that teammate and points where the downed player looks,
so they aim it with their normal look controls (the free camera); the E key switches
to smoothed received aim on foot or the teammate's seat heading in a vehicle (the locked camera) and back. When they are revived, or
nobody is left to watch, the view returns to their own camera.

## Behavior

- Online only. Offline play, including split screen, is never touched.
- When the local player's health reaches 0 while their character stays in the room
  (the downed state), view 0 switches to a camera following the teammate with the
  lowest slot who is not down.
- The followed teammate stays followed while they stand. When they go down or leave,
  the view moves to the next standing teammate in slot order, wrapping round.
- When the local player is revived, the view returns to their own camera. It also
  returns when every teammate is down.
- The spectator camera has two modes from 3.3.0, switched with the E key while
  down (user decision 2026-10-24; the key follows the W key's rules below). The
  choice lasts the session and starts free.
  - Free (the default): the camera orbits the watched teammate at the downed
    player's own look pitch and yaw, as set by mouse or right stick through the
    game's bindings. It does not copy the teammate's aim. Version 0.4.0 adds this;
    it is unverified online.
  - Locked: follows the teammate's received aim on foot, smoothed between frames.
    In a vehicle it follows the heading of the teammate's seated matrix with zero
    pitch. The game's camera offsets stay the same. This replaces the original
    locked view by user decision on 2026-10-26, issue #12005. The remote copy does
    not expose the teammate's complete camera or vehicle gun aim.

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
- From 3.4.0 the health gauge shows in both camera modes (user decision 2026-10-24:
  whatever the spectator camera shows, it shows in both modes). The game hides a
  camera's HUD widgets while the camera orbits, which is how a downed player's own
  camera behaves; the plugin answers that question itself for cameras following a
  remote player (`research.md`, "Health gauge in both camera modes").
- From 3.5.0 the spectator camera shows the watched teammate's name at the top of
  the view, in both modes. It is the game's rescue message widget bound to the
  watched teammate, with the plugin's draw in a copy of its vtable: the same text
  calls, the name as the indicators get it, centred a tenth of the view's height
  from the top (`research.md`, "Spectator name caption").
- Returning to the local camera returns its original HUD. No ammo, radar, damage
  or class gauges are created for spectator cameras. The smaller HUD is unverified
  online. Energy for Wing Divers and ammo are planned in that order (`plan.md`, M11
  onwards).

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
5. Sets the target teammate's camera parameters to orbit at the local player's
   look angles in free mode, or smoothed received aim and seated matrix heading
   in locked mode. Clears orbit on every other remote player.
6. When the target differs, it makes an existing camera in view 0's list that follows
   the target current (`weak_from_this` and the view switch, as the game's
   `0x11932F0` does), or, for a teammate without one, creates a camera through the
   game's factory with the same InitParam `create_remote_camera` builds. The game's
   flush in the `System::Update` that follows makes the new camera current.
7. For a new camera, attach the health widget through `0x7FC9A0` using only the
   `lyt_HudPowerGauge01.sgo` layout and the watched player. Attach multiplayer
   indicators through `0x7FD580` with the watched teammate as the viewer, and the
   chat log through `0x7FCE60` with the local player as the viewer, as on the game's
   own camera. Attach the caption through `0x7FD7E0` (the rescue message) with the
   watched teammate as its player and point the object at the plugin's copy of its
   vtable. Release all four returned weak references; the camera owns the
   objects. The full HUD builder at `0x7FE5D0` is never called. Invalid health/max
   values skip the health widget.

The plugin keeps no game pointers between frames. Its state is a bit per slot for
cameras it asked for during the current downed period, so a camera that fails to
appear is not requested every frame, the held state of the W and E keys, the chosen
camera mode, and the locked view's smoothed angles, slot and vehicle state.
The filter resets on a target or mode change, revival, loss of the spectator view
or an invalid sample. Cameras it creates stay in view 0's list until
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
`System::Update`, it writes the selected view's pitch and yaw into both blocks of
the watched remote soldier and sets the byte. Free mode uses the downed local
player's look pitch (`+0x1230`) and yaw (`+0x1234` plus `+0x1E44`, as the network
sends it). Locked mode reads the watched soldier's received pitch/yaw, or yaw
`atan2(matrix[+0x80], matrix[+0x88])` and pitch 0 while the vehicle weak reference
at `+0x1548/+0x1550` lives. It converges toward those angles with a 50 ms time
constant using measured monotonic elapsed time and the shortest wrapped yaw arc.
It never extrapolates through a missing update. Equal field values cannot tell us
whether another packet arrived, so the filter makes no packet-timing estimate. Every other remote soldier with the byte set gets 0
back. The local player's own soldier and camera are never written.

No game function sets these fields, so the plugin writes them. The writes touch only
the local copy of a remote player. The only readers found are the ghost camera's
constructor and update, and class updates that run for local players. Only the
plugin's spectator cameras follow remote players. Nothing is sent to the network. A parameter reload the
game runs inside `System::Update` (on a revive or some state changes) clears the byte
for one frame, after which the plugin sets it again.

A ghost camera in orbit mode reports through vtable slot 9 that HUD widgets should
hide, and the health gauge's enable pass checks that slot every frame. Up to 3.3.0
the gauge was therefore expected hidden on the free camera and shown on the locked
one. From 3.4.0 the plugin's own function sits in that slot: it answers yes for a
camera whose target is a remote player and hands every other camera to the game's
function, so the gauge shows in both modes. The caption's draw asks the slot as
the rescue message's does, so it shows in both modes too. The multiplayer name/health indicators
and the chat log do not call the slot. Whether the downed player's look input keeps
working while the spectator camera is in orbit mode, and whether the gauge shows in
both modes, are live checks.

## Installation and refusal

EDF6Coop's `LoadGame` checks the image timestamp and size. With
`[Display] SpectateWhenDown=1` (the default) it then calls `spectate::Install`,
which checks that the call at `0x705715` targets `System::Update`, that the ghost
camera's vtable slot 9 (`0x1768C58`) holds the game's `0xF7500`, that the rescue
message's vtable (`0x17F6E08`) holds its seven functions after its RTTI locator, and the FNV-1a 64
hash of every range listed in the research notes. It adds a stub to a thunk page of
its own within rel32 reach of EDF.dll, registered with the unwinder like EDF6Coop's
other stubs, writes its HUD visibility function into the slot and rewrites the call.
Any failure before the writes leaves both alone and logs why; a failed call write
puts the slot back. Only the spectator camera is then off; the rest of EDF6Coop
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

- A controller button or a rebindable key for cycling and for the camera mode, and
  a key to return to the own camera while down. W and E are decided; the game's
  bound actions would be the route for these (`research.md`, "Teammate cycling
  input").
- Whether to watch while the local player has bled out, if the game has such a state.

## On-screen controls and version

From 3.6.1, the spectator camera shows `W: switch player / E: switch camera`
at the bottom in both modes, by user request on 2026-10-27. The hint belongs to
the spectator caption and disappears with that camera on revival or return.
The online room screen shows the running EDF6Coop version in the bottom right,
including when `SpectateWhenDown=0`. It does not publish a new lobby attribute.
