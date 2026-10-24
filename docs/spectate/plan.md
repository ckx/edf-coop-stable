# Spectator camera plan

Milestones and live checks. Versions up to 0.4.0 are standalone EDF6Spectate
releases; from 3.1.0 the camera ships in EDF6Coop.

## M1. Research (done)

Static analysis of the camera system, the player map and the downed state, and a
read-only sample of a live four-player room. Evidence is in `research.md`.

## M2. Automatic follow prototype

The `System::Update` call hook, the roster read, `NextTarget`, camera reuse and
creation through the game's factory, and refusal on any unexpected byte.

Automated checks in EDF6Coop, run under Wine with the game's EDF.dll:

- `SpectateTarget`: whom a downed player watches.
- `SpectateCameraGameCode`: the game's `weak_from_this` and view switch move a view
  between two cameras with balanced reference counts.
- `PluginLoad_*`: the `System::Update` call leads into the plugin, stays untouched
  with `SpectateWhenDown=0` or `Enabled=0`, and an old `EDF6Spectate.dll` is
  renamed `.disabled`.

Camera creation, the roster walk and the live hand-over cannot run outside the game
(the factory allocates through the game's heap and builds scene objects).

## M3. Live check in an online room (partly verified)

The first live attempt with 0.1.0 crashed when activating the new camera. Version
0.1.1 corrects the weak-reference release, as documented in `research.md`. The
user confirmed that 0.1.1 worked online. Its log recorded a camera switch at
19:34:30 and a return at 19:35:43 on 2026-10-12. This establishes basic following
and return, not the complete checklist below.

Install EDF6Coop with the game closed, back up saves, and play an online mission
with at least two other players. Check:

1. `EDF6Coop.log` has the `SPECTATE: a downed player online orbits` line and no
   other `SPECTATE` refusal.
2. Going down switches the view to a standing teammate within a frame or two, and the
   log names the slot. The view follows their movement and aim.
3. When that teammate goes down, the view moves to the next one.
4. Being revived returns the view to the player's own camera, and aiming, zoom and
   the HUD behave as before.
5. Going down a second time in the same mission reuses the cameras (no `new camera`
   line for a slot already watched).
6. A teammate leaving the room while watched: the view moves on, and the game keeps
   running.
7. Mission clear and mission failure while watching: results and the next mission
   start normally.
8. Offline play and split screen: no `SPECTATE` lines after the install line.

Record the results, with the log, in `research.md`. A green test run does not stand in
for any of these.

## M4. Choosing whom to watch (built in 3.2.0, unverified online)

The user chose the space bar (2026-10-23): keyboard only, no controller button, no
INI key. On 2026-10-24 the user moved it to the W key (3.2.1); the rest of M4 is
unchanged, and Space below reads as W for the live checks. Holding W (running
forward) as the player goes down is not a press. A press must not change the camera while the player is typing in the chat
box. Evidence for the game's text-entry state is in `research.md`, "Teammate
cycling input".

Behaviour:

- A press of Space (a down edge, sampled in the `System::Update` call) while the
  local player is down and view 0 is on a spectator camera moves the view to the
  next standing teammate in slot order after the watched one, wrapping round. With
  one standing teammate the press changes nothing.
- The chosen teammate stays watched until they go down or leave. Then the automatic
  rule continues from their slot, as it does today.
- Holding the key is one press. The key is not read while the local player stands,
  so jumping is untouched.
- The press is ignored while the game's text entry is open: the byte at
  `EDF+0x21360BA` is 1 from the chat box's open to its close, and the game's own
  keyboard reader (`0x95F930`) drops every key for play while it is set. The plugin
  applies the same rule, so a typed space only goes into the message. While the
  byte is set the key counts as held, so the first press after the box closes needs
  a release first.
- The key is read only while the game window is in the foreground
  (`GetForegroundWindow` belongs to this process), so a space typed in another
  window after alt-tab does not cycle.

Implementation:

- `NextTarget(mates, count, followed, advance)`: with `advance` the "followed still
  stands" case is skipped, so the search starts at the first slot after `followed`
  and wraps, which is the existing second loop. Nothing else in `Follow` changes:
  a new target goes through the same `FindCamera` / `ShowCamera` / `Create` path and
  `createdThisDown` still limits creation to one attempt per slot per down.
- Key: `GetAsyncKeyState(VK_SPACE)` high bit, edge-tracked in a static next to
  `createdThisDown`, reset when the local player stands. Wine implements
  `GetAsyncKeyState`, `GetForegroundWindow` and `GetWindowThreadProcessId`.
- Text-entry gate: `At<std::uint8_t>(game + 0x21360BA)`. The byte is data, so the
  hashed ranges are the code that defines it: IME association `0x1183160` (writes
  it), its getter `0x1183CE0` (the game's keyboard reader calls it first) and the
  text-entry window procedure `0x7D2C40` (turns it on and off). They are in the
  `kCode` table and in the main table in `research.md`.
- Log: the existing `SPECTATE: following the player in slot N` line covers a cycle.
  No new line.
- Nothing is sent, so no netcode version gate.
- Tests: `SpectateTarget` gets the advance cases (next standing slot after the
  watched one; wrap to a lower slot; one standing teammate stays; watched teammate
  down plus advance gives the next standing one). The key read and the gate are
  Win32 and game memory and are live checks only.
- Docs in the same change: `README.md` (spectator section and the settings table
  row), `dist/README_EDF6Coop.txt` (replace "there is no button to pick a teammate
  yet"), `design.md` Behavior, and the release notes.

Live checks, online, with at least two standing teammates:

1. Down: Space moves the view to the next standing teammate and the log names the
   slot. The look direction is kept.
2. Space again wraps round to the first standing teammate.
3. The chosen teammate goes down: the view moves on to the next standing one.
4. Open the chat box while down, type a sentence with spaces, send it, close the
   box: the camera does not move while typing; Space cycles again after the box is
   closed. Note whether a Space pressed before opening the box and still held when
   it closes does anything.
5. The chat shortcut window (phrase list) open while down: note whether Space does
   anything in it and whether the camera cycles. The gate covers free-text entry
   only.
6. The pause menu open while down: the same note.
7. Standing: Space does what the game binds it to and no `SPECTATE` line appears.
8. Alt-tabbed: a space typed in another window does not cycle.
9. The same checks under Proton.

## M5. Release packaging (done, then replaced)

EDF6Spectate 0.1.0 to 0.4.0 shipped as experimental prereleases of their own, with
installers and a bundled EDFModLoader. From EDF6Coop 3.1.0 the camera ships in the
EDF6Coop package and follows its release process (README, "Release").

## M6. Spectated teammate's health and names

The user requested the full combat HUD after the successful 0.1.1 camera retry.
Version 0.2.0 attached the native local-player HUD builder to each teammate camera.
The online attempt crashed in the damage HUD after setup had already faulted.
Version 0.2.1 removes that attachment and restores the working camera-only path.

Before another HUD build, separate the viewed teammate's combat state from the
local viewer's settings. Damage, radar and weapon widgets read a local-player
index that is invalid for remote players. Check constructor and update paths,
including indirect calls, before enabling any widget. A caught construction fault
cannot roll back registered objects. Evidence and exact addresses are in
`research.md`.

After those dependencies are resolved, live checks still needed: health, ammo and
reload changes; different classes and weapon layouts; radar and crosshair; return
on revival; repeated spectating; disconnects and mission transitions. Remote
values are only as current as the game's own copy of that teammate.

The user narrowed the next attempt to health and remote names after the crash.
Version 0.3.0 calls only the health-layout factory and the multiplayer indicators
factory. The watched player owns the displayed health values; the local player
remains the name indicators' viewer. The native enumeration excludes that viewer,
not the watched teammate. Full HUD replication is deferred.

The health constructor, update/display methods and their player-data helpers were
inspected against EDF.dll. The multiplayer name path uses the player's network
record and room slot. New code checks cover those interfaces. The release is built
and packaged, but no live HUD check has passed yet. Check health changes, name
visibility, inactive-camera HUD hiding, revival, a second death, vehicles and
teammate departures before expanding the HUD.

## M7. Orbit camera aimed by the downed player

The user asked for a free camera instead of the teammate's own view and chose an
orbit around the watched teammate, aimed with the downed player's normal look
controls (2026-10-22). A detached free-fly camera is out of scope.

Version 0.4.0 sets the watched remote soldier's camera parameters to the ghost
camera's orbit mode with the local player's look pitch and yaw each frame, and
clears it on every other remote soldier. Evidence and the reasoning about the HUD
are in `research.md` (Orbit camera). Built and run against the existing tests; no
live check has passed.

Live checks:

1. Going down: the view moves to the teammate and then turns with mouse and right
   stick. Up/down limits match normal aiming.
2. The camera stays near the teammate as they move, with no clipping or jitter
   beyond the normal follow camera.
3. A target change keeps the current look direction.
4. Revive: the player's own camera returns facing the last look direction, with
   the normal HUD and aim.
5. Whether the health gauge hides while orbiting (predicted) and whether the
   name/health indicators stay.
6. Watched teammates of each class, on foot and in vehicles.

## M9. Chat log and own name indicator (3.1.2)

The user reported that chat was not visible while spectating and that their own
name indicator was missing (2026-10-23). Version 3.1.2 attaches the game's chat log
widget to each spectator camera and makes the watched teammate the name indicators'
viewer. Evidence is in `research.md` (Chat log and own name indicator). Built and
run against the existing tests; no live check has passed.

Live checks:

1. A chat message sent while spectating appears in the spectator view; messages
   from before the first death do not (the local camera's log shows them again on
   revival).
2. The downed player's own body has a name/health indicator; the other teammates
   keep theirs; the watched teammate has none.
3. Revival, a second death in the same mission (camera reuse, no second chat log),
   the watched teammate going down or leaving, and mission end: the chat keeps
   working on the own camera in the next mission.

## M10. Free and locked camera on the E key (built in 3.3.0, unverified online)

The user asked to switch between the orbit camera (free) and the camera as it was
before 0.4.0 (locked: the teammate's own view), with E as the key and free as the
default (2026-10-24), and asked why the old camera sometimes drifted to a wrong
direction. The analysis is in `research.md`, "Free and locked camera": the old
camera showed the teammate's aim as the game received it, which arrives every
sixth sync frame on foot and not at all while the teammate rides a vehicle or is
down.

Behaviour:

- A press of E (the W key's rules: a down edge while down and on a spectator
  camera, ignored while the chat box is open, while another window has the focus
  and while standing) switches between free and locked. The choice lasts the
  session and starts free.
- Free: the orbit camera of M7. Locked: the plugin leaves the orbit byte clear on
  every remote soldier, so the game's follow camera looks along the teammate's
  facing. A switch takes effect in the next camera update, with the game's own
  smoothing.
- While locked and watching, the log notes once per stale period when the watched
  teammate's received aim has not changed for 120 frames.

Implementation: `Key` (the W key's edge logic with the key code as data) for W and
E, `freeCam` as the mode, `NoteStaleAim` for the log line. `NextTarget`, camera
creation and the HUD are unchanged. No test covers the key or the mode: both are
Win32 and game memory. Docs in the same change: `README.md`, `dist/README_EDF6Coop.txt`,
`design.md`, the release notes.

Live checks, online, with a standing teammate:

1. Down: the view starts free (turns with the look controls). E switches to the
   teammate's own view, which turns as they turn; E again returns to free, facing
   the current look direction. The log has a line per switch.
2. Locked, the teammate aims up and down: the view follows within about 100 ms, in
   steps. Note any direction the view holds while they clearly look elsewhere, and
   whether they were in a vehicle: the log should show the stale-aim line.
3. Locked, the teammate boards a vehicle: predicted straight ahead along a fixed
   heading until they get out.
4. The health gauge: predicted hidden while free and shown while locked (3.3.0;
   from 3.4.0 shown in both, M11).
5. Revive while locked, then go down again: the camera is still locked. Restart
   the game: it starts free.
6. E while the chat box is open, while standing and while alt-tabbed: no switch and
   no `SPECTATE` line.

## M8. Part of EDF6Coop (done in 3.1.0)

The user asked for one install and one mod (2026-10-22), with the camera on by
default, under EDF6Coop's terms. The source moved in one commit to `game/spectate.*`
and logs to `EDF6Coop.log` with a `SPECTATE` prefix. `[Display] SpectateWhenDown`
turns it off. A standalone `EDF6Spectate.dll` is renamed `.disabled` at load and by
the installers. The M3 and M7 live checks still apply to the merged build.

## M11. Health gauge in both camera modes (built in 3.4.0, unverified online)

The user asked for the watched teammate's health in both camera modes and set the
rule that whatever the spectator camera shows, it shows in both modes (2026-10-24).
Priorities after this: energy (a Wing Diver mechanic; other classes have other
mechanics and matter less), a caption with the watched teammate's name at the top
of the screen in both modes, then ammo. The board for this work is "Spectator HUD"
in the Mods project of the Little Legion tracker.

Up to 3.3.0 the health gauge was attached to every spectator camera but predicted
hidden on the free camera: the game hides a camera's HUD widgets while the camera
orbits. Version 3.4.0 puts a plugin function in the ghost camera's HUD visibility
slot that answers yes for a camera following a remote player, which only the plugin
makes. Evidence is in `research.md`, "Health gauge in both camera modes". Built and
run against the existing tests; `PluginLoadTests` covers the slot write and its
absence with `SpectateWhenDown=0`.

Live checks, online, with a standing teammate:

1. Down on the free camera: the teammate's armor bar is at its usual place with
   their current and maximum health, and follows their health changes.
2. E to the locked camera: the bar stays. E back: the bar stays.
3. Revive: the own HUD returns complete. Go down again: the bar is back.
4. The own crosshair and rescue text behave as before while down (they belong to
   the own camera, which the plugin does not touch).
5. The watched teammate boards a vehicle: the game's vehicle display takes over
   the bar as it does for the own camera, or the bar hides; note which.
