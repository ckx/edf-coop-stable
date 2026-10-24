# Spectator camera research notes

Collected for the standalone EDF6Spectate plugin, now `game/spectate.*` in EDF6Coop.
Paths under `local/` are in the edf6-spectate checkout on the dev box.

Static analysis of the supported EDF.dll on 2026-10-11, plus read-only sampling of a
running game in four-player online missions on the same date (two hours, three
missions). The sampler read
process memory through `/proc/<pid>/mem` under Proton and wrote nothing. The first
live run of 0.1.0 on 2026-10-12 crashed on entering spectate; see the correction
below. The user reported that 0.1.1 worked online. The log confirms following a
teammate and returning to the local camera. The 0.2.0 combat HUD attempt crashed
online; 0.2.1 removes its native HUD attachment. Version 0.3.0 uses only health
and multiplayer name/health indicators. This smaller HUD is not live-verified.
Version 0.4.0 turns the spectator view into an orbit around the watched teammate,
aimed by the downed player's own look angles. It is not live-verified.

## Supported image

`EDF.dll`, PE TimeDateStamp `0x678CCB46`, SizeOfImage `0x22CE000`, SHA-256
`0d5092393a1b9d8f1b75b0c93d8a8f65615a36a92c46e40349c2b66c733d0916`.
Image base `0x180000000`; all addresses below are RVAs.

The plugin checks the timestamp and size, then the FNV-1a 64 hash of each range
below. None of these ranges contains a base relocation, so the mapped bytes equal
the file bytes. The live process had the same bytes at the hook site and the called
functions. Any mismatch refuses installation before anything is written.

| Range | RVA | Size | FNV-1a 64 |
| --- | --- | --- | --- |
| `System::Update` call in `Application::Update` | `0x705715` | `0x5` | `0xF9744FB4304DB3F6` |
| `System::Update` | `0x1198DA0` | `0x9FB` | `0x7F0564B7A3BF2724` |
| CharacterGhostCamera factory | `0x82B40` | `0x1DC` | `0x6F6DB55CC4005B3C` |
| View camera switch | `0x1193FA0` | `0x18D` | `0xF859E0B97E448F2F` |
| `weak_from_this` | `0x1198950` | `0x9C` | `0x580BFED010D5E37E` |
| Camera flush | `0x1195A80` | `0x159` | `0xFAAA5CBBF9469555` |
| Current camera update | `0x11978D0` | `0x1000` | `0x15240CC9F660A4DA` |
| `create_remote_camera` debug command | `0xCCCA0` | `0x2D9` | `0x7B438E325C57D000` |
| CharacterGhostCamera constructor | `0xF4860` | `0x974` | `0xB9D5E9BDAA015326` |
| `IsPlayer` (SoldierBase slot 33) | `0x550890` | `0xA` | `0x1D923A0DEC9EF9A4` |
| Script `Object::IsAlive` | `0x1C0BE0` | `0x2E` | `0xA905EB899CAEB9D9` |
| Script `IsOnlineMode` | `0x1B5DF0` | `0x2B` | `0xA71A18502F48BC92` |
| `__RTDynamicCast` import thunk | `0x12DA7AA` | `0x6` | `0xD6E2BD7734AF1D4E` |
| Ghost camera target copy (slot 4) | `0xF86A0` | `0x4A7` | `0xC8F39D3D226684CB` |
| Ghost camera orbit rotation | `0xFAF20` | `0xF0` | `0x5A931A41EBC8898A` |
| Network look angles | `0x59FE04` | `0x38` | `0x5271702E19DA8061` |

Version 0.3.0 additionally checks these HUD ranges. The native builder is verified
as evidence of parameter layout, but is never called by the plugin.

| Range | RVA | Size | FNV-1a 64 |
| --- | --- | --- | --- |
| native HUD call parameters | `0x7FE5D0` | `0x988` | `0x30B0F56AE0009F97` |
| health HUD factory | `0x7FC9A0` | `0x259` | `0xC053F7AAE37CD46D` |
| teammate names HUD factory | `0x7FD580` | `0x257` | `0xD3B9016D4F01994A` |
| camera HUD ownership transfer | `0x7FF390` | `0x9A` | `0x7B2D65E7F57F7623` |
| health HUD wrapper construction | `0x815990` | `0x63` | `0x5FF1DB331C4F55D1` |
| health HUD layout construction | `0x815E60` | `0x218` | `0xFECAC8EC031E8E36` |
| health HUD player binding | `0x815810` | `0x172` | `0x2F4117E29D9C1FE6` |
| health HUD wrapper cleanup | `0x815B10` | `0x1A9` | `0x4FF33942129D9B7D` |
| health HUD camera visibility | `0x8168B0` | `0x1D3` | `0x03B27D69E6200892` |
| health HUD layout setup | `0x816080` | `0x41A` | `0x70DDB2694F60B2B4` |
| health HUD layout transform | `0x8164A0` | `0x17A` | `0xBD774FC23FF6F171` |
| health gauge construction | `0x825250` | `0xDB4` | `0x3C71A55DAA5E8A8A` |
| health gauge display | `0x826250` | `0xDB8` | `0xF4FE74DF3E238442` |
| health gauge update | `0x827010` | `0x79A` | `0xC44952D123F21801` |
| health gauge vehicle display | `0x8277B0` | `0x390` | `0xEBF4C2C16F34319F` |
| HUD vehicle lookup | `0x8024D0` | `0xA0` | `0x55389265C9EC92B0` |
| multiplayer HUD player binding | `0x7FF670` | `0x167` | `0x7AA4C0C4DC39856A` |
| teammate names HUD construction | `0x8047C0` | `0xD4` | `0x89F3DD8D4B8AF127` |
| teammate names HUD resources | `0x806FE0` | `0x590` | `0xA5BC1A7D743AD0EA` |
| teammate names HUD update | `0x807870` | `0x50B` | `0x4982CB0CA1FA9638` |
| teammate names owner exclusion | `0x807680` | `0x12A` | `0x3799AE9CAA03B906` |
| teammate names HUD draw loop | `0x8077B0` | `0xB9` | `0xD3C67D358E532814` |
| teammate name and health drawing | `0x804C90` | `0x1A1E` | `0x793D6D440F8AE747` |
| teammate name and network slot | `0x7FFBD0` | `0x316` | `0x928DCDE1820225DC` |
| network display name | `0x784830` | `0x1D6` | `0xAC8CA0E518BFCB02` |
| network platform name | `0x12ABFF0` | `0x266` | `0x2FFD4BD265F55D78` |
| chat HUD factory (3.1.2) | `0x7FCE60` | `0x257` | `0xD0128CEBB3F70692` |
| chat HUD construction | `0x800520` | `0x125` | `0x31F4B8FFF36A2126` |
| chat HUD destructor | `0x8007E0` | `0x15A` | `0x6E653C90FCC9C658` |
| chat HUD deleting destructor | `0x800A80` | `0x17C` | `0x0E3BB1E3AAF4F75E` |
| chat receive callback | `0x800C00` | `0x59` | `0xE9AE064FC00E2944` |
| chat HUD layout setup | `0x802970` | `0x289` | `0x863B694B975811BE` |
| chat message add | `0x802C00` | `0x310` | `0xB48D3FC86CCDE78C` |
| chat HUD draw | `0x802F10` | `0x102` | `0x8F50E7AEBAE1EE37` |
| chat HUD update | `0x803020` | `0x214` | `0xB8C6C261062A53DC` |
| chat message draw | `0x801420` | `0xEB2` | `0xDB38601F64406159` |

## Hook: the System::Update call

Observed:

- `Application` vtable slot 1 is `0x705530`, the per-frame update. At `0x705715` it
  runs `call 0x1198DA0` (`E8 86 36 A9 00`) with `rcx = *(EDF+0x20B2958)` (the
  `xgs::game::System`), `xmm1` a float step and `r8d` a count. Nothing else sets
  arguments for it. The signature is `void (System*, float, uint32)`.
- `System::Update` flushes cameras created since the last frame (`0x1195A80`, called
  at `0x1198F61`) before the scene steps.
- Later in the same frame, `0x7058B7` calls `0x11977F0`, which queues each view's
  current camera for drawing. That call sits behind a frame-skip branch at
  `0x70577D`; the `System::Update` call does not.

Hook: the plugin rewrites the call's rel32 to reach a stub next to EDF.dll
(`mov rax, imm64; jmp rax`) that enters the plugin. The plugin does its work and then
calls `0x1198DA0` with the same arguments. The return address stays in
`Application::Update`, so nothing unwinds through the stub.

Thread: the main thread, once a frame. Inference: game objects step inside
`System::Update` (the loop over `System+0xCC0` at `0x1198EA0`), so before the call the
scene is between steps. Job threads were not examined.

## Cameras

Observed:

- `System` has four views of `0x188` bytes from `+0x4D0`. A view holds its current
  camera as a `weak_ptr` at `+0x8` (object) and `+0x10` (control block), and its
  cameras as a `std::list<shared_ptr<SceneCamera>>` at `+0x18` (sentinel pointer;
  size at `+0x20`; node: next `+0`, value `+0x10`). `0x1195BE0` and `0x1196850`
  index views this way.
- Live: online, view 0 held one camera, a `CharacterGhostCamera` (vtable
  `0x1768C10`) following the local player. Views 1 to 3 were empty.
- Player creation (`0x5A4F40`) builds the player and a `CharacterGhostCamera` for it
  through the factory `0x82B40`, with the view index equal to the player index.
- Factory `0x82B40(System* rcx, weak_ptr* out rdx, uint32 view r8d,
  const Matrix* r9, InitParam* [rsp+0x20])`: fills the InitParam's base fields
  (matrix at `+0x10`, scene at `+0x50`, view at `+0x58`, viewport at `+0x5C`, owner
  output shared-pointer address at `+0x70`), allocates `0xC20` bytes, runs the constructor, queues the camera on
  `System+0x4C0` (`0x1195990`, under the lock at `System+0x308`) and returns a
  `weak_ptr<CharacterGhostCamera>` in `out`. Matrix and InitParam must be 16-byte
  aligned (`movaps`).
- The InitParam vtable is `0x1762168` (`CharacterGhostCamera::InitParam`). Its
  `+0x80` is the object to follow. Callers fill only the vtable and `+0x80`; the
  game's own InitParams are `0x88` bytes on the stack.
- The constructor `0xF4860` copies the target's `weak_ptr` (from the target's
  `enable_shared_from_this` at `+0x28`/`+0x30`) into the camera's `+0x350`/`+0x358`.
  It holds no strong reference. `+0x360` caches a raw `SoldierBase*` from the same
  object.
- The ghost camera's update, slot 3 (`0xF8210`), locks the target `weak_ptr`
  (`0xF828C`) but then uses `+0x360` with only a null check (`0xF82A4`). A camera
  whose target was destroyed would read freed memory there.
- `0x11978D0` runs slot 3 on each view's current camera only (`0x11979B4`). Outside
  debug commands, its one caller is `System::Update` at `0x1198FA4`, after the camera
  flush and before the scene steps. A camera that is not current is not updated,
  and an object destroyed during the scene steps is seen as expired by the plugin
  at the start of the next frame, before the next camera update. The plugin moves
  view 0 off a ghost camera whose target expired or left the room.
- The flush `0x1195A80` adds each queued camera to its view's list and makes it the
  view's current camera through `0x1193FA0`. A new camera is shown on the frame
  after it is created.
- `0x1193FA0(View*, weak_ptr<SceneCamera> by value)`: calls slot 7 (`+0x38`) on the
  old current camera, stores the new one, calls slot 6 (`+0x30`) on it, and releases
  its by-value argument (the weak count decrement at `0x1194109`). Callers build the
  argument with `weak_from_this` (`0x1198950`), as `0x11932F0` does. The camera test
  runs these two functions from EDF.dll and checks the reference counts.
- The developers' debug command `create_remote_camera <id>` (`0xCCCA0`, registered at
  `0xCCB1F`) finds the remote player with that slot in the TeamManager map and creates
  a `CharacterGhostCamera` for it on view 0 through the same factory. This is the
  evidence that the ghost camera supports remote players. `next_camera`
  (`0x9F5B0`) cycles a view through its list with `0x1196850`.
- No game code removing one camera from a view's list was found. Lists are emptied
  when the System tears down (`0x11956C0`). Cameras the plugin creates stay until
  then; the plugin creates at most one per teammate per downed period and reuses
  them.
- Only debug commands call `0x1196850` (cycle cameras) and `0x11988D0` (visit every
  camera), so extra cameras in view 0's list do not change normal play.

## Players

Observed:

- `*(EDF+0x20B2978)` is the `TeamManager`. `[TeamManager+0x38]` points at a
  `std::map` whose values are the players' objects (node: left `+0`, parent `+8`,
  right `+0x10`, nil flag `+0x19`, object `+0x20`). `create_remote_camera` walks it
  from the head's left node and casts each object with
  `__RTDynamicCast(object, 0, GameObjectBase 0x2006400, SoldierBase 0x2006428, 0)`.
  Live, the map held exactly the four players (three `PaleWing`, one `HeavyArmor`).
  Missions with NPC allies were not sampled.
- `IsPlayer` (`0x550890`): `+0x340` (local controller) or `+0x1ED0` (network player
  record) is non-null. Live: the local player had `+0x340` set and `+0x128` bit 0
  clear; the three remote players had `+0x340` null and `+0x128` bit 0 set. Every
  player had a record at `+0x1ED0` whose `+0x48` was its slot (local 0, remotes 1
  to 3).
- Health is the float at `+0x2F8`; `+0x2F4` held a constant per-player value larger
  than every observed health (inference: maximum armor). The script's
  `Object::IsAlive` (`0x1C0BE0`) is `health > 0`.

### Downed state

Observed in 2.5 minutes of sampling at 2 Hz: the local player (slot 0) went down at
02:35:45 and was back at 02:35:53 with health 54.4 of 190.6; slot 1 went down at
02:36:28 and was back at 02:36:47 with 13.6. While down, the same object stayed in
the map, health was exactly 0, and these fields differed from every sample while up:

| Field | Down | Up |
| --- | --- | --- |
| `+0x2F8` health | `0.0` | `> 0` |
| `+0x2E8`, `+0x2E9` (bytes) | `1`, `1` | `0`, `0` |
| `+0x1180` | `1` | `0` |
| `+0x194` (byte) | `1` | `0` |

The local player's camera stayed the same object throughout. Its `+0x3D4` byte
followed the target's `+0x194` (the constructor copies `+0x170..` or `+0x230..` of the
target into `+0x3B0..`).

The plugin treats `health <= 0` as down, as `IsAlive` does. Over the full two-hour
sample (three missions, 02:35 to 03:08, then the game closed), there were 12 downed
periods across slots 0, 1 and 2, each ending in a revive after 8 to 20 seconds.
`+0x1180` and `+0x194` matched the downed state in every sample for slots 0 and 2;
`+0x2E8` did not hold across the whole sample. No player was seen staying down
until the mission ended, and nobody left the room.

### End of a mission

About 12 seconds before each mission unloaded (02:38:40, 02:54:09, 03:06:36), a
second set of four players appeared in the TeamManager map, one more object per
slot, with full health and the same local and remote flags. The game then created a
second `CharacterGhostCamera` on view 0 following the new local object, and it became
current. Health of the new remote objects changed while the old ones stayed
constant. Then every view and player was gone until the next mission started.

The plugin does nothing while any slot has two players, so it never moves the view
off the game's new camera.

## Online mode

`IsOnlineMode` (`0x1B5DF0`) reads the session mode table at `*(EDF+0x20B2890)`. See
the AutoLoot research notes for the layout. The live sample returned online.

## First live crash and ownership correction

On 2026-10-12, the user went down in an online mission and the game immediately
closed. The spectator log recorded `Following the player in slot 1 (new camera)`
at 19:04:55. The co-op crash log recorded an access violation at `EDF+0x11940CD`
at 19:04:55.621, with callers `0x1195B81`, `0x1198F66`, and Spectate's update hook.
The failing instruction is the new camera's activation call through vtable slot 6.
The recorded vtable address was in the heap, rather than EDF.dll. Logs and the
installed binary are preserved under `local/crash-20261013T020534Z/` and are not
committed.

The initial factory return-type analysis above was wrong. Rechecking its code:

- `0x82C69` and `0x82C6C` write the object and control block to the return buffer.
- `0x82C70` increments the control block's weak count at `+0xC`.
- `0x82C76` releases the temporary strong reference at `+0x8`.
- The native debug command releases this result at `0xCCF2E`, decrementing only
  `+0xC` and calling the block-deletion function at vtable `+0x8` if it reaches zero.

Version 0.1.0 instead decremented the strong count after the factory returned.
That can destroy the queued camera before the flush activates it. This is a
confirmed ownership bug and explains the observed invalid activation call; no
memory dump was available to inspect the destroyed object directly.

Version 0.1.1 releases only the returned weak reference, matching the native caller.
The factory and caller are already in the plugin's verified code ranges. The user
reported a successful online retry. The installed 0.1.1 log recorded following
slot 1 at 19:34:30, returning to the player's own camera at 19:35:43, and another
spectate at 20:01:05 on 2026-10-12. This confirms basic following and return, not
the complete disconnect, class and mission-transition checklist.

## Combat HUD attached to the spectator camera

Scope: the user requested the full teammate combat HUD after the successful
camera retry. Version 0.2.0 used the native player-HUD builder, with no additional
network messages or new overlay. This approach was removed in 0.2.1 after the
crash below. Camera ownership alone does not make these HUDs safe for remote players.

Observed in the supported game code:

- Normal player creation `0x5A4F40` calls the camera factory at `0x5A5278`, locks
  its returned weak reference and takes the camera object into `r15` at
  `0x5A52B0`. It then calls `0x7FE5D0` at `0x5A5306` with `rcx = camera` and
  `rdx = player`. The return value is unused.
- `0x7FE5D0(SceneCamera*, GameObjectBase*)` casts the player to SoldierBase and
  builds the HUD objects through eight native factories. These include
  `HuiHudWeaponObject`, the health/power gauge and radar UI objects, crosshairs,
  damage display, follower durability, rescue text, multiplayer status and chat.
  It reads the camera's view index at `+0x60` for the UI layout.
- The weapon HUD factory `0x7FDA40` puts the camera into the InitParam at `+8`.
  After constructing the HUD, it moves a strong reference into the camera's HUD
  list at `+0x208` through `0x7FF390`, called at `0x7FDB64`. That helper moves the
  object/control pair into a list node and zeros the source pair. The wrapper's
  return value is a weak reference, which the builder releases at `+0xC`.
- `HUiHudBase` construction at `0x815810` copies the selected player's weak
  reference into the widget's `+0x778/+0x780`. Weapon update `0x831A40`, power
  gauge update `0x826250` and radar update `0x82B4D0` lock that reference before
  reading the player. The weapon constructor chooses its layout and weapon slots
  from that player. Rebinding the local widget alone would not rebuild that layout.
- The HUD wrapper stores its camera at `+0x18`, populated by `0x11D4320`.
  `0x8168B0` checks that camera's visibility and enables its UI widgets for update.
  Camera ownership retains the HUDs. This does not prove that a partially
  constructed UI object is removed from the game's update registry after a fault.
- None of the three additional verified code ranges has PE base relocations.

A read-only `/proc/<pid>/mem` sample of the running 0.1.1 session found the current
local camera with 11 HUD objects and the existing teammate camera with zero. Both
were CharacterGhostCamera instances. The sample is stored in
`local/hud-research/live-camera-huds.json`; the running process was not modified.

Version 0.2.0 implementation: after creating a teammate camera, call `0x7FE5D0`
once with that camera and its target, before releasing the factory's weak reference. The queued
camera still has the System's strong owner. Reusing the camera reuses its HUD.
The original local camera and its widgets are untouched, so returning to that
camera also returns its HUD through the normal camera path.

### Online HUD crash and withdrawal in 0.2.1

The supplied `EDF6Spectate-clam.log` identifies 0.2.0, installed at 14:18:18,
and records `STOPPED: a fault while following a player` at 14:29:05. There is no
successful camera-follow log entry. Both supplied co-op logs contain the same
crash at 2026-10-13 14:29:05.729, at `EDF+0x8225D7`, with stack callers
`0x820543`, `0x119D74B`, `0x7058FC`, `0x11A3009`, `0x70341C`, `0x6FF47F`
and `0x3E639`. These are two copies of the same recorded crash, not evidence of
two separate failures. Copies are preserved in `local/crash-clam-0.2.0/`.

The supported game binary establishes:

- `HUiHudDamage` has vtable `0x17FC970`; its update slot is `0x820520`.
  That update calls `0x8224A0` at `0x82053E`.
- At `0x8225C8`, the damage refresh reads the followed SoldierBase's 32-bit
  index at `+0x338`. It zero-extends that value, multiplies it by `0x2800`,
  then reads a settings entry from `*(EDF+0x20B2890) + index*0x2800 + 0x334`.
  There is no bounds or remote-player check before the read at `0x8225D7`.
- The logged `rax = 0xFFFFFFFF`, `rcx = 0x27FFFFFFD800` and
  `rdx = 0x1E50D2FA130` reproduce the reported fault address exactly:
  `0x1E50D2FA130 + 0xFFFFFFFF*0x2800 + 0x334 = 0x29E50D2F7C64`.
  This is an invalid local settings index, not a camera reference-count failure.
- Construction also calls the same damage refresh at `0x81DF8B`.
  It is a plausible source of the earlier caught fault, but the spectator log
  did not capture that fault's instruction address. Do not claim it is confirmed.
- This assumption appears in other HUD code. Radar update reads `player+0x338`
  at `0x82B62B` and uses it unchecked to index settings at `+0x6E5C`.
  Weapon HUD code at `0x803B74` reads the same index and uses `index*0x2800`
  to read settings at `+0x326` at `0x803B82`. Skipping the damage widget alone
  would not address these other paths.

Inference from the two logs and native code: the builder faulted after registering
UI state. Guarded stopped future spectator calls, but did not unwind or remove
that state. The game's later UI update then reached the damage widget and crashed
outside the spectator guard. The exact first fault and full partially created
object set cannot be recovered from these logs.

Version 0.2.1 removes the native combat HUD builder call, its function type and
its three additional code checks. The remaining runtime is the 0.1.1 camera path,
with a comment explaining why the HUD builder must not be called for a teammate.
No game settings, player indices or native HUD code are patched. Building and
packaging do not establish that a fresh online attempt has passed.

Full combat HUD work remains open. A replacement must separate the viewed
teammate's combat data from the local viewer's settings, and inspect constructors
and updates before attaching widgets. Remote ammo/reload and class-gauge accuracy,
all class layouts, crosshair behavior and cleanup after disconnects or mission
end still need live checks. This mod sends no extra state over the network.

### Smaller health and name HUD in 0.3.0

The user accepted a smaller HUD with health as the priority and remote player
names. Version 0.3.0 creates two isolated components through existing factories.
It does not call `0x7FE5D0` or construct weapon, radar or damage widgets.

Health gauge evidence:

- The native call at `0x7FEC45` invokes `0x7FC9A0(camera, weakOut, param)`.
  Its 0x28-byte InitParam has vtable `0x17F6768`, camera at `+8` populated by the
  factory, player at `+0x10`, UTF-16 layout path at `+0x18`, two zero bytes at
  `+0x20/+0x21`, and view index at `+0x24`. The layout pointer is `0x17F6790`,
  `app:/ui/lyt_HudPowerGauge01.sgo`. Spectate uses view 0, as its camera does.
- The factory constructs `HUiHudBaseObject` through `0x815990`, which loads that
  layout through `0x815E60`. Its weak reference to the UI widget is in the vector
  at `+0x70`. `0x815810` binds the InitParam's player to `+0x778/+0x780`.
- `HUiHudPowerGauge` has vtable `0x17FCC98`, constructor `0x825250`, update
  `0x827010` and display `0x826250`. Construction reads health `+0x2F8` and
  maximum health `+0x2F4` at `0x825C9F/0x825CA8`. Update reads the same pair at
  `0x827126/0x82712F`; display converts them to current/maximum numbers at
  `0x826A83/0x826A9C`. This confirms the earlier maximum-armor inference.
- Those methods use the player's weak reference and do not index the local-player
  settings through `player+0x338`. Common layout setup `0x816080` reads a global
  layout option at `session+0x14FF4`; it does not need the remote player's missing
  local settings index. Vehicle display uses `0x8024D0`, which locks the vehicle
  reference at `+0x1548/+0x1550`, and `0x8277B0`.
- The plugin checks that current/max health are finite and positive before
  construction, so an invalid initial denominator skips the gauge. Future values
  follow the game's normal health widget behavior. This initial check does not
  prove future remote values or vehicle transitions are valid.

Multiplayer name/health indicator evidence:

- The native call at `0x7FEDBA` invokes `0x7FD580(camera, weakOut, param)`.
  InitParam vtable is `0x17F6738`, camera at `+8`, owner/viewer at `+0x10`.
  The factory constructs `HudPlayer_MultiPlayStatus` via `0x8047C0` and its base
  `0x7FF670`. The base binds that viewer as a weak reference at `+0x70/+0x78`.
- Update `0x807870` enumerates TeamManager with callback `0x807680`. The callback
  compares a candidate with the supplied viewer at `0x807689` and skips an equal
  pointer. Spectate supplies the local player as viewer, not the watched remote
  player, so that exclusion does not remove the watched player from the indicators.
- Draw loop `0x8077B0` calls `0x804C90` per player. That path reads a player's
  current/max health at `0x804E45/0x804E4F`. It calls `0x7FFBD0` for name and slot.
  That helper uses SoldierBase's network record at `+0x1ED0`, checks for a null
  record, and uses its network slot at `+0x48`. It obtains the display name through
  `0x784830` and, when game settings request it, platform name `0x12ABFF0`.
  This is the game's existing remote-name path, not the missing local index.
- Indicators keep the game's name-display settings and native placement. The
  implementation does not add a separate fixed "Spectating <name>" caption.
  Whether the watched character's indicator remains readable close to the camera
  is a live-check item.

Both factories move their strong reference into the camera's HUD list through
`0x7FF390` and return a weak reference. Spectate releases only the returned weak
references. Each new spectator camera gets its HUD once; subsequent spectating
reuses that camera. The local camera and its HUD bindings are unchanged. No game
pointers are cached by the plugin between frames. `0x8168B0` gates the health
widget on its camera's visibility; native camera drawing owns the name indicators.

New setup logs identify health versus names construction. `Guarded` can now
return the caught exception code and instruction address to the caller. Its
exception handler records those scalar values before restoring the saved context;
it does not call the logger or attempt to undo game calls. The update hook logs
the fault and disables further work, as before. A partial native construction
still cannot be rolled back by this guard.

A read-only sample of the user's running game on 2026-10-12 confirmed that its
local camera owns a separate `HUiHudBaseObject` containing the health widget
`0x17FCC98`, and a separate `HudPlayer_MultiPlayStatus` with its viewer bound to
the local soldier. Both slots were down in this sample: slot 0 had health/max
`0/290`, and remote slot 1 had `0/298.4`. The remote's local settings index was
`0xFFFFFFFF`, while its network slot remained 1. The snapshot is in
`local/health-hud-research/live-native-bindings.json`. The process was read only;
this is evidence of the stock objects and data, not a 0.3.0 runtime test.

Validation so far: static inspection against the installed supported EDF.dll,
read-only live inspection, relocation checks for the added byte ranges, and a
MinGW/clang release build.
No new tests or CI were added. No online HUD rendering, health changes, revival,
vehicle transitions or disconnect behavior has yet been verified for 0.3.0.

## Spectator direction investigation, 2026-10-17

The user reports spectator views pointing in the wrong direction, at the ground,
or straight ahead. The cause is unresolved. No camera correction has been installed.

Observed in the supported EDF.dll:

- The network writer `0x59FDB0` sends the player's float at `+0x1230` at
  `0x59FE14`, followed by the sum of `+0x1234` and `+0x1E44` at `0x59FE24`.
  The receiver `0x592180` stores these at `+0x1230` and `+0x1234` and clears
  `+0x1248`, at `0x5921D2..0x5921F2`. These fields carry pitch and yaw in radians.
  Missing pitch transmission is therefore not an explanation for this report.
- Ghost-camera slot 4, `0xF86A0`, copies the target's matrix at `+0x60..+0x9F`
  into camera `+0x370..+0x3AF` at `0xF871C..0xF8740`. It then copies camera
  parameters from target `+0x170`, or `+0x230` when target byte `+0x224` is set,
  into camera `+0x3B0` onwards at `0xF8745..0xF87E4`.
- The follow calculation at `0xFAF29` uses the cached target matrix. Camera byte
  `+0x3F0` enables explicit pitch/yaw from `+0x400` and `+0x404` instead, through
  `0xFAF9A..0xFB00B`. These correspond to target byte `+0x1B0` and angles
  `+0x1C0` in the normal parameter block. The vectors at camera `+0x3B0` and
  `+0x3C0` are positional offsets, not look angles.
- The camera can also adjust its direction when byte `+0x301` is set. The branch
  at `0xFB123` reads a local target's setting at session player `+0x326`; a remote
  target has no local controller and takes the default enabled value. This is a
  path to inspect during the reported failure, not an established cause.
- Player vtables used to identify the inspector's readable objects are Ranger
  `0x17CDF28`, Air Raider `0x17CF100`, Fencer `0x17CF5B8`, Wing Diver `0x17D0FF8`
  and SoldierBase `0x17D24D8`. Other object types are skipped.

Read-only live captures at 04:24 and 04:26 UTC on 2026-10-18 contain 300 and
600 samples at roughly 10 Hz. The room held one local and one remote player.
The remote actor's forward axis matched the received pitch/yaw in every sample,
within 0.001 degrees after normalizing both vectors. The local player's health
stayed above zero, and the current camera followed the local player throughout.
These captures do not reproduce or disprove the spectator fault. They do rule out
a persistent mismatch between the remote angles and actor rotation in this sample.
Evidence is in `local/direction-research/`, including disassembly and JSON captures.

`tools/inspect_camera.py` makes this comparison repeatable on Linux/Proton. It
opens `/proc/PID/mem` read-only, checks the mapped image headers, and records the
current ghost camera, target, health, received angles, actor direction, cached
target direction, output direction and camera mode flags. Unknown player classes
are skipped. Invalid reads during mission changes produce an unavailable sample.
Reads are not atomic, so isolated angular differences can reflect a frame advancing
between reads. A camera's angular difference from the received look is diagnostic,
not an assertion that every native camera mode must match those angles.

Run with permission to inspect the game's process, during the bad spectator view:

```sh
mkdir -p tmp
python3 tools/inspect_camera.py "$(pgrep -n -x EDF6.exe)" --duration 30 > tmp/camera-direction.jsonl
```

Validation: the inspector produced valid samples against the running supported
game and identified both players and the local camera. No game memory was written.
No plugin implementation changed, and no build or native-code test run was needed
for this investigation. A capture while spectating incorrectly is still needed
to distinguish stale received aim, a target transform problem and a camera mode
that changes the output direction.

## Orbit camera, 2026-10-22

The user chose an orbit camera around the watched teammate, aimed with the downed
player's own look controls, over a detached free-fly camera. Version 0.4.0
implements it. Static analysis of the supported EDF.dll and read-only reads of the
running game; nothing below has been seen working in a live room.

Observed:

- The 2026-10-17 capture `local/camera-direction.jsonl` was taken while the local
  player was down (health 0) and the spectator camera showed slot 1. The local
  player's look pitch `+0x1230` went from 0.004 to 1.21 rad and yaw `+0x1234` from
  18.76 to 17.54 rad in the 23 seconds before the revive. A downed player's look input keeps updating
  these angles while a teammate camera is current.
- Ghost camera slot 4 (`0xF86A0`) is its per-step update. Camera update phase
  `0x1199554` calls it through `0x118DF40` on each view's current camera, after the
  object update phase. It copies the target's camera parameter block into camera
  `+0x3B0..+0x45F`: the block at target `+0x230` when target byte `+0x224` is set,
  else the one at `+0x170` (`0xF8745..0xF87E4`). Block byte `+0x40` becomes camera
  `+0x3F0`, and the pair at `+0x50` becomes camera `+0x400/+0x404`. It then runs the
  camera's state machine at `+0xB58` (`0xF8985`).
- The follow state at `0xFAF20` builds the camera rotation from camera `+0x400`
  (pitch) and `+0x404` (yaw) when `+0x3F0` is set: rows `(1,0,0)`, `(0,cos p,sin p)`,
  `(0,-sin p,cos p)` (`sinf`/`cosf` thunks `0x12DA8B8`/`0x12DA8B2`), then
  `0x4CD10` rotates them about the vertical axis by the yaw. The forward row becomes
  `(sin y cos p, -sin p, cos y cos p)`, the convention the inspector uses for a
  player's look angles. Position, offsets `+0x3B0/+0x3C0` and smoothing `+0x3D0`
  are the same as in the normal path; only the rotation source changes
  (`0xFAF9A..0xFB00B`, again at `0xFB562`).
- Writers of soldier `+0x1B0` and `+0x1C0` (block `+0x170` plus `+0x40/+0x50`):
  the constructor `0x545670` (zero, `r12` cleared at `0x5456AF`) and the parameter
  load `0x54DDF0` (byte 0). No soldier code sets the byte to 1. `0x54DDF0` also runs
  on a revive (`0x56D6C9`, when `+0x1180` is set) and on a state change at
  `0x5697D9`.
- The Ranger and Air Raider per-frame class updates (`0x550C30`, `0x563DC0`, vtable
  slot 5) fill the `+0x230` block from the `+0x170` block, including `+0x40/+0x50`,
  and set `+0x224`, only for a soldier with a local controller (`+0x340`). The
  2026-10-17 capture recorded remote `+0x224` set in 3 of 299 samples, so the
  plugin writes both blocks.
- Network writer `0x59FDB0` sends `+0x1230` and `+0x1234 + +0x1E44`. `+0x1E44` is a
  Ranger yaw offset clamped to ±π/2 (`0x552287`), zeroed elsewhere. The plugin uses
  the same sum for the local player's yaw.
- Ghost camera slot 9 (`0xF7500`) returns false when `+0x3F0` is set.
  `HUiHudBaseObject` slot 3 (`0x8168B0`, the health gauge's visibility check) skips
  enabling its widgets when the camera's slot 9 returns false. It runs from the
  render command `0x118D810` that `0x11977F0` queues after `System::Update`. The
  same slot 3 is shared by the damage and weapon HUD objects.
  `HudPlayer_MultiPlayStatus` (vtable `0x17F6C98`) has no slot 9 call in its own
  methods.
- Read-only check of the running game on 2026-10-22 (three-player room, 0.3.0
  installed): every hashed range matched except the `System::Update` call, which the
  installed plugin had already redirected. All three soldiers had byte 0 and angles
  0 in both blocks.

Inference: setting the byte and angles on the watched remote soldier before
`System::Update` makes its ghost camera orbit at those angles from the next camera
update, with the game's own offsets and smoothing. A parameter reload during the
object update phase clears the byte for that frame only. The health gauge is
expected to hide while the camera orbits; the name/health indicators are expected
to stay.

Implementation: each frame after choosing the target, `Follow` reads the local look
angles. For the target teammate it writes pitch and yaw to `+0x50/+0x54` of both
blocks and sets `+0x40`. Every other remote soldier with `+0x40` set gets 0. Non-finite
angles clear the byte, so the camera falls back to the teammate's facing. The local
soldier is not written. No game pointers are kept.

Validation: MinGW/clang build, the existing four tests under Wine with the game's
EDF.dll (all passed; `PluginLoadsIntoGame` hashes the new ranges in the mapped DLL),
and the read-only live range check above. No test exercises the camera update; it
needs the game's scene objects. Live checks still needed: the view follows mouse
and right-stick look while down, pitch limits, the transition at going down and at
revive, target changes, the health gauge and indicators, vehicles, and other
classes as the watched teammate.

## Merge into EDF6Coop, 2026-10-22

The camera code moved into EDF6Coop 3.1.0 unchanged in behaviour. The call at
`0x705715` is now checked with `CallTargets` (an `E8` to `0x1198DA0`) instead of a
hash of its five bytes, and its stub comes from a `ThunkPage` of its own, so the
unwinder knows it. `LoadGame` checks the image. Every other hashed range is as in
0.4.0.

No EDF6Coop patch site lies inside a hashed range. Every hex address in
EDF6Coop's `game/` sources was compared with the ranges: only `IsPlayer`
(`0x550890`) appears, and player sync only verifies its bytes, as the camera does.

Validation: MinGW/clang build and the EDF6Coop suite under Wine with the game's
EDF.dll, except the `DirectNet` and `LogFileCap` stress tests. `PluginLoad_on`,
`_fresh` and `_replaced` hash every range in the mapped DLL and find the call
redirected into the plugin. Not live-tested.

## Chat log and own name indicator, 2026-10-23

The user reported that the chat log was not visible while spectating and that their
own name indicator was missing. Static analysis of the supported EDF.dll with
`tools/pe.py`; the running game was not read (the user was playing).

Chat log evidence:

- The builder `0x7FE5D0` makes its eighth factory call at `0x7FEE5D` to
  `0x7FCE60(camera, weakOut, param)`. The InitParam vtable is `0x17F6738`, the same
  `HudPlayer::InitParam` as the names call (`rdi` is not written between `0x7FED94`
  and `0x7FEE3E`), with the player at `+0x10`. The factory writes the camera into
  `+8` itself (`0x7FCE7B`).
- `0x7FCE60` allocates `0x120` bytes, constructs `HudPlayer_Chat` (vtable
  `0x17F6A58`) through `0x800520`, moves the strong reference into the camera's HUD
  list at `+0x208` through `0x7FF390` and returns a weak reference: the same
  contract as the names factory, so `AttachHud` serves both.
- `0x800520` runs the `MultiPlayHud_Base` constructor `0x7FF670`, which copies the
  player's weak self-reference (`SoldierBase +0x28/+0x30`, read at `0x7FF766`) to
  `+0x70/+0x78` and the raw pointer to `+0x80`. It then links a node {callback
  `0x800C00`, this} into the chat receiver list at `*(EDF+0x20B2AC8)+0x58`. The
  callback filters through `0x7207E0` and appends through `0x802C00` to the widget's
  own message list at `+0xB0`. Each widget has its own list, so a spectator camera's
  log shows the messages that arrive after its creation.
- Both destructors (`0x8007E0`, `0x800A80`) remove every receiver node whose
  callback is `0x800C00`, not only their own. That is the game's behaviour for its
  own chat widgets; a second widget changes nothing, because view 0's cameras, the
  local one included, are torn down together (`0x11956C0`), and the local camera's
  own chat widget removes the same nodes then.
- Update `0x803020` (ends at `0x803234`; the unwind table splits it) ages messages
  and looks up senders' vehicles (`0x8024D0`). Draw `0x802F10` lays out each message
  through `0x801420` and submits with the camera's view index (`+0x18`, then
  `+0x60`). `0x801420` reads game settings through the global local index at
  `*(EDF+0x20B2890)+0x38`, not through the player, and locks the viewer's weak
  reference; with the local player as viewer this is the stock path. No chat method
  reads `SoldierBase +0x338` or calls the camera's slot 9, so the log is expected to
  stay while the camera orbits.
- None of the ten ranges contains a base relocation.

Own name evidence:

- The enumeration callback `0x807680` skips a candidate equal to the locked viewer
  (`0x807689`) and null candidates. The viewer is locked in update `0x807870` from
  `+0x70/+0x78`; an expired weak reference locks to null and excludes nobody.
- The per-player draw `0x804C90` locks the viewer at `0x804D4C` and returns without
  drawing when that fails (`0x804D67` to `0x8065D1`). Its other uses of the viewer
  are the vehicle lookup `0x8024D0` (`0x804E0D`) and the seat lookup `0x806A80`,
  which returns -1 for a missing viewer. So clearing the viewer after construction
  would remove every indicator, and a null InitParam player faults in `0x7FF766`.
- The raw pointer at `+0x80` is written at `0x7FF7A7` and not read by any
  `HudPlayer_MultiPlayStatus`, `MultiPlayHud_Base` or `HudBase` method inspected
  (`0x7FC2D0`, `0x7FC620`, `0x7FC630`, `0x7FC6D0`, `0x7FC750`, `0x7FF800`).
- The viewer therefore has to be a live soldier, and the only way to show the local
  player with the game's code is to make someone else the viewer. 3.1.2 passes the
  watched teammate: a remote `SoldierBase` with the weak self-reference the callback
  at `0x807B72` already reads on every candidate, and a valid argument to the
  vehicle lookup, which runs on remote candidates in the stock path. Its indicator
  is the one dropped.

Validation: MinGW/clang build and the non-stress suite under Wine with the game's
EDF.dll; `PluginLoad_on` hashes the ten new ranges in the mapped DLL. Not
live-tested; the live checks are M9 in `plan.md`.

## Open questions

- Capture the reported incorrect spectator direction, including whether it happens
  on foot or in a vehicle and which class/action triggers it. Version 0.4.0 no
  longer takes the view direction from the teammate, so this applies only if a
  fallback to the teammate's facing is kept.

- Live-check the isolated health/name components in 0.3.0, including name placement,
  health changes, revival, repeated deaths, vehicles and disconnected targets.
  The basic 0.1.1 camera follow and return were confirmed by the user and log.
- A separate fixed spectator-name caption if the native indicators are hard to read.
- Full combat HUD support is deferred; remote ammo and class gauges remain unverified.
- Whether a player can stay down until the mission ends, or bleeds out, and what
  happens to the object then.
- Whether NPC allies appear in the TeamManager map. The plugin only takes objects
  with a network player record and the remote bit, so NPCs would be skipped.
- Whether a remote player's object is destroyed when that player leaves the room,
  and whether anything other than the scene steps destroys it between the plugin's
  check and the camera update.
- Whether any job thread reads view 0's camera while `Application::Update` runs.
- Which input should cycle teammates. The prototype has none.
- Whether the orbit camera follows the downed player's look input in a live room,
  and whether the health gauge hides as the static analysis predicts.
