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

Locked aim smoothing, issue #12005, additionally checks:

| Range | RVA | Size | FNV-1a 64 |
| --- | --- | --- | --- |
| Received look angle storage | `0x5921BC` | `0x3D` | `0x61920059725219C0` |
| Seated soldier matrix copy | `0x5751B9` | `0x53` | `0x56EAF9A8BA8CBB62` |
| Vehicle exit aim reset | `0x5705A5` | `0x1B` | `0xA0B16F34405F61E1` |
| Matrix forward yaw | `0x4E1F0` | `0x23` | `0xB2489B4CE99D04C6` |

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
| IME association and text-entry byte (3.2.0) | `0x1183160` | `0x2D` | `0x18B1F50D6B101044` |
| text-entry byte getter | `0x1183CE0` | `0x8` | `0x2BC3DE9F6A5D005F` |
| text-entry window procedure | `0x7D2C40` | `0x9A` | `0xAD9CA65227696BA0` |
| ghost camera HUD visibility, slot 9 (3.4.0) | `0xF7500` | `0x48` | `0x3E8A8467D6F818D0` |
| HUD widget shown flag clearing | `0x80FC50` | `0xB88` | `0x326E883AB7461031` |
| rescue message HUD factory (3.5.0) | `0x7FD7E0` | `0x257` | `0x749A67C36F8046A6` |
| rescue message HUD construction | `0x8083A0` | `0x21` | `0xACDF1813DE907759` |
| rescue message HUD destructor | `0x8083D0` | `0x34` | `0xA084B6851CE38CB1` |
| rescue message draw (the caption's template) | `0x808410` | `0x2D3` | `0xA77E2074C4927895` |
| HUD view matrix (vtable slot 5) | `0x7FC6D0` | `0x80` | `0x47DF0BDBF265D186` |
| HUD projection (vtable slot 6) | `0x7FC630` | `0x94` | `0x1933F2BD66CA6745` |
| HUD screen point | `0x7FC380` | `0x298` | `0x8E1D5E60A18102D9` |
| HUD screen transform | `0x7FC760` | `0x23E` | `0x7128546B2A55BAA6` |
| wstring deallocation | `0x3D3C0` | `0x62` | `0x001F8BD145A9B42F` |
| font text object | `0x11517A0` | `0x4F` | `0x9B63E0BB4001FB42` |
| text object construction | `0x11379B0` | `0xFA` | `0x26813887C10E97EB` |
| text style | `0x113B4A0` | `0x9` | `0x6EF651AD1E09BE2A` |
| text renderer style | `0x113B2A0` | `0x1F5` | `0x27667DDC1ADBAFB7` |
| text style copy | `0x11388E0` | `0x84` | `0xC636B896D8191BCC` |
| text measure | `0x1139790` | `0x257` | `0xD1BECF90B85D50AA` |
| text draw | `0x113C520` | `0x9` | `0x1FF136490574DAE0` |
| text renderer draw | `0x113B4B0` | `0x106C` | `0x5B28649E627DA012` |
| text flush | `0x113C6F0` | `0x9` | `0x9F2FCD3A8BCD3B1B` |
| text renderer flush | `0x113C5C0` | `0x127` | `0x96DA45BA9FC16254` |
| text object destruction | `0x1138630` | `0x79` | `0x6D074C61B1186916` |

Version 3.4.0 also checks that the ghost camera's vtable slot 9 (`0x1768C58`, a
relocated pointer) holds `0xF7500` before replacing it. Version 3.5.0 checks that
`HudPlayer_RescueMessage`'s vtable (`0x17F6E08`) holds `0x7FC750`, `0x8086F0`,
`0x11D43D0`, `0x808410`, `0x8083D0`, `0x7FC6D0` and `0x7FC630`, and that the pointer
before it is the RTTI locator `0x1CA19E0`. Its pdata entry stops short for
`0x1139790`; the hashed range runs to its `ret` at `0x11399E6`.

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

## Teammate cycling input, 2026-10-23

The user chose the space bar to cycle teammates and asked that typing in the chat
box never cycles. Static analysis with `tools/pe.py`; the running game was not read.
Built in 3.2.0 (plan M4).

The game's text-entry state:

- Window creation `0x1183470` stores the game window at `EDF+0x21360A8`, the result
  of `ImmGetContext` at `EDF+0x2135FF8`, calls `ImmAssociateContext(hwnd, NULL)`
  through the thunk `0x162C4BA` (`0x11837C0`) and clears the byte at
  `EDF+0x21360BA` (`0x11837C5`). The IME starts detached.
- `0x1183160(bool enable)` calls `ImmAssociateContext(hwnd, enable ? himc : NULL)`
  and stores `enable` in the byte at `0x21360BA` (`0x1183181`). It is the only
  writer besides window creation. `0x1183CE0` returns the byte; `0x95F320` jumps to
  it.
- The game's keyboard input builder `0x95F930` (it reads `GetKeyboardState` at
  `0x95FA51`) calls `0x1183CE0` at `0x95F98A` as its first check and, when the byte
  is set, skips every key (`jne 0x960419`) and marks the input idle. So while the
  byte is set the game itself treats no key as play input. Its second check,
  `0x1183D90`, reads the byte at `0x21360BB`, set through `0x1184540` from `0x917200`
  and `0x917700`; its meaning was not traced.
- `0x7D2C40(hwnd, msg, wParam, lParam)` is the window procedure the text-entry
  manager installs. It handles `WM_IME_*` (`0x10D` to `0x10F`, `0x281`) and two
  application messages: `0x1000` calls `0x1183160(1)` and then `0x7D0B50` on the
  manager at `EDF+0x20B39B8`; `0x1001` calls `0x1183160(0)`. Other messages go to
  `DefWindowProcW` (`0x1755FA8`).
- `0x7D0B10` begins text entry: it installs `0x7D2C40` through `0x1184680` (stored
  at `EDF+0x2135FF0`) and sends message `0x1000` to the game window with
  `SendMessageW` (`0x1755F40`), so the byte turns on synchronously. `0x7D0580` ends
  it: message `0x1001`, then it removes the field from the manager's list
  (`0x20B39B8 +0x60/+0x68`). `0x7D0B10` has no direct caller or data pointer in the
  image; it is reached through a stored function pointer, so the exact moment it
  runs relative to the chat window opening is a live check.
- `0x7D0580` is called from `0x7D4CE0`, slot 5 of the `HUiCommon_SoftkeyboardGUI`
  vtable (`0x17F3ED0`, pointer at `0x17F3EF8`), which resets the vtable pointer and
  tears the GUI down, and from `0x7D4910` (through `0x7D3AC0` and `0x7D2F60`), the
  GUI's close path. `HUiChat::CreateTextChatWindow` (RTTI lambda names) builds the
  text chat through `UiCommon_Softkeyboard`, whose PC front end is this GUI. So the
  chat box is the soft keyboard, and its open and close bracket the byte.
- `0x7D1E50` is the per-field message handler (the field holds the IME composition;
  `EDF+0x20B2898` points at a pending-text buffer the field drains). It calls
  `0x1183160(0)` when field `+0x10C` is set, another route to clearing the byte.
- `Steam_SystemService` has `OnGamepadTextInputDismissed` callbacks: the Steam
  overlay keyboard is a second text input path. Whether it sets the byte was not
  traced; check 5 and 6 in the plan cover a keyboard that is not the chat box.

Reading the key:

- Chosen: `GetAsyncKeyState(VK_SPACE)` from the plugin (`'W'` from 3.2.1, user
  decision 2026-10-24), gated by
  `GetForegroundWindow` belonging to this process. The game does the same compare
  against its window (`0x1183947` to `0x118395F`). No game dependence for the key
  itself.
- Not chosen: the game's own keyboard array at `EDF+0x21360E0`, filled by
  `0x1183880` with `GetKeyboardState` (`0x1183938`) once per frame under the flag
  at `0x21360BE`. It would tie the plugin to the game's frame timing for no gain.
- Not chosen: the game's bound actions. `0x95F930` builds play input from the
  keyboard array and the bindings; its structures were not traced. This would be
  the route for a rebindable key or a controller button.

The three ranges in the main table above (`0x1183160`, `0x1183CE0`, `0x7D2C40`)
define the byte; none holds a base relocation.

Wine: `ImmAssociateContext`, `GetAsyncKeyState`, `GetForegroundWindow` and
`GetWindowThreadProcessId` are implemented. The game stores its byte whatever
`ImmAssociateContext` returns.

## Free and locked camera, 2026-10-24

The user asked for a switch between the orbit camera (free, the default) and the
camera as it was before 0.4.0 (locked: the teammate's own view), on the E key, and
asked why the old camera sometimes drifted to a wrong direction. Static analysis
with `tools/pe.py` and the 2026-10-17 capture; the running game was not read. Built
in 3.3.0 (plan M10).

The switch needs no new game interface. The locked camera is the ghost camera's own
behaviour: `0xFAF29` takes the rotation from the cached target matrix while camera
byte `+0x3F0` is clear, so the plugin leaves the orbit byte at 0 on every remote
soldier while locked. The health gauge is expected to show while locked and hide
while free (slot 9, above).

What the locked camera shows, and why it can point wrong:

- The 2026-10-17 capture (`local/camera-direction.jsonl`, 299 samples at 10 Hz,
  local player down and watching slot 1 for 23 s) has the camera's forward equal to
  the cached target forward in every sample, and the watched soldier's forward equal
  to its received pitch/yaw (`+0x1230/+0x1234`) within 0.001 degrees, apart from
  non-atomic reads (largest difference 4 degrees). The camera showed the teammate's
  aim as the game had received it. A wrong direction comes from what the game knows
  of the teammate's aim, not from the camera.
- The received angles changed 205 times in 30 s, at the sync cadence: median
  interval 0.10 s, eight gaps over 0.5 s, one of 1.1 s. The mask builder `0x59FB10`
  includes aim (bit 2) only every sixth sync frame on foot (`0x59FB76`, divisor 6;
  2 while byte `+0x1E41` is set), and the receiver `0x592180` stores each record as
  it arrives, without interpolation. The locked camera turns in steps of up to
  100 ms and freezes through a lost record.
- No aim is sent while the sender holds a vehicle reference (`+0x1548/+0x1550`;
  the control block's use count, `0x59FB3C`) or is dead or down (`+0x2E8`,
  `0x59FB41`): those paths take `0x59FBBB`, whose mask never has bit 2. Vehicle exit
  (`0x5701B0`, from the state machine `0x572810`) sets the soldier's pitch to 0 and
  its yaw to the yaw of its own matrix row `+0x80` (`0x5705A5`, `0x5705B8`;
  `0x4E1F0` is atan2). The original notes called this boarding, but the function
  clears the vehicle references at `0x5705DE..0x57060D`. That was corrected during
  issue #12005. The old capture does not establish that a frozen camera occurred
  in a vehicle or prove the exact origin of the reported wrong direction.
- The float writer `0x12B5350` truncates the angle to half precision (mantissa
  `shr 13`, no rounding) and sends the 3-byte half when the truncation error is at
  most 0.1 (`0x1765A0C`), else the 5-byte float. Yaw is unbounded (the capture has
  18.8 and -11.4 rad; the clamp at `0x573C03` applies only with a limit at
  `+0x1254`), so a player who has turned many times sends a yaw that is low by up
  to the half step: 0.9 degrees between 16 and 32 rad, 1.8 up to 64, 3.6 up to 128,
  then mostly the float. Pitch stays below 2 rad and loses nothing visible.
- Other writers of the angles: `0x57ADF0` (from `0x5A190D`) zeroes
  `+0x1230..+0x124F`; `0x573BB0` adds the input vector at `+0xD60` each frame under
  the limits at `+0x1250/+0x1254`; `0x578680` (from `0x59B2F6`) rescales the delta
  against `+0x1240` when `+0x1260` changes. None of them changed the remote copy's
  angles between records in the capture.

Not found: a camera-side source of drift. The follow state's `+0x301` branch
(`0xFB123`) moves the camera position toward `+0x550` through the target's vtable
slots 9 to 11 and leaves the rotation alone.

Implementation: while locked and watching, the plugin compares the watched soldier's
`+0x1230/+0x1234` with the previous frame's and logs once when they have not changed
for 120 frames, naming the slot. The line separates "no aim arrived" (a vehicle, a
lost record) from a wrong aim in the user's next report. Nothing is written.

## Health gauge in both camera modes, 2026-10-24

The user asked for the watched teammate's health in both camera modes (2026-10-24:
whatever the spectator camera shows, it shows in both modes). Static analysis with
`tools/pe.py`; the running game was not read. Built in 3.4.0 (plan M11).

How the game hides the gauge while a camera orbits:

- Ghost camera slot 9 (`0xF7500`, vtable `0x1768C10` + `0x48`, 0x48 bytes to its
  padding) returns false when camera byte `+0x3F0` is set. Otherwise, when byte
  `+0x301` is set, it returns false unless the target's weak_ptr (`+0x358`) is alive
  and the target's vehicle reference (`+0x360` → `+0x1550`) is alive; else true.
- `0x8168B0`, slot 3 of `HUiHudBaseObject` (the health gauge's wrapper), returns at
  once unless the wrapper's camera (`+0x18`) has byte `+0x200` set and its slot 9
  returns true. Only then does it walk the wrapper's widgets (`+0x70..+0x78`), cast
  each and set the widget's shown flag, byte `+0x770`, to 1 (`0x8169A4`). It never
  clears the flag.
- `0x80FC50` (0xB88 bytes, called from `0x80B824` in `0x80B610` and `0x80D1C9` in
  `0x80B970`) clears `+0x770` first thing (`0x80FC86`); `0x80B693` in `0x80B610` writes
  it from a register too. Since the game's own HUD hides as soon as the local
  camera orbits (a downed player's camera), the clear runs at least as often as the
  enable pass, which runs from render command `0x118D810` after `System::Update`.
- The crosshair widget (`0x8039F0`, at `0x803AD9`) and the rescue message
  (`0x808410`, at `0x80844F`) also ask slot 9; the names indicators and the chat log
  do not.

The plugin writes its own function into the slot. For a camera whose target weak_ptr
is alive and whose cached `SoldierBase*` (`+0x360`) has the remote bit (`+0x128`
bit 0) it returns true; every other camera gets the game's `0xF7500`. The stock game
never builds a ghost camera on a remote player, so the local player's camera and
the game's hide-while-orbiting behaviour are unchanged. The function runs on the
render thread outside `Guarded`; it reads only what `0xF7500` reads itself (the
control block's strong count, then the cached pointer), under the same checks. The
plugin's own camera recognition still compares the vtable pointer, which the slot
write does not change. `Install` verifies the slot's value and the two new hashes,
writes the slot before the `System::Update` call and restores it if the call write
fails. `PluginLoadTests` checks that the slot points into the plugin when the camera
is on and at `0xF7500` when it is off.

Not verified live: that the gauge appears in free mode, that it still appears in
locked mode, and that the crosshair and rescue message (which the spectator camera
does not own) are unaffected.

## Wing Diver energy, 2026-10-24

The user's second HUD priority after health; a Wing Diver mechanic (user decision
2026-10-24). Two static passes; nothing built. Tracked on the Spectator HUD board.

The energy gauge is not a HUD object of its own. `lyt_HudEnergyGuage_R.sgo`
(`0x17FD660`) is the right weapon gauge's variant, chosen for weapon 0 when the
builder's InitParam byte `+0x20` is set (`0x82E3EC`), else `lyt_HudWeaponGuageR`
(`0x17FD6B0`); other weapons take `lyt_HudWeaponGuageL` (`0x17FD6F0`). The builder's
InitParam for the weapon HUD factory `0x7FDA40` is 0x28 bytes like the health one:
camera `+8`, player `+0x10`, flag `+0x20`, zero `+0x21`, view index `+0x24`.

Local-player index use (the 0.2.0 crash: `SoldierBase +0x338`, -1 on a remote copy,
times `0x2800` into the settings table at `0x20B2890`). Direct 32-bit reads of `+0x338`
in `0x7C0000..0x850000`: `0x7FFDF4` (names helper `0x7FFBD0`, which runs for the
indicators and takes a different path for remote players), `0x803B74` (crosshair
`0x8039F0`), `0x8225C8` (damage `0x8224A0`), `0x82B62B` (radar `0x82B4D0`).

The first pass (four levels of direct calls from `0x82E170`, `0x7FDA40` and the
22-entry vtable `0x17FD150`, 179 functions) found none in the weapon HUD. The second
pass corrected its picture of the classes and went further:

Static analysis with `tools/pe.py` and capstone, with the unwind table's chained entries
merged so cold chunks count as part of their function. The running game was not read.

Corrections to the first pass:

- `0x82E170` is the constructor of `HuiHudWeaponObject` (vtable `0x17FD208`, 7 entries),
  the wrapper factory `0x7FDA40` builds. It is not `HUiHudWeapon`. The `0x90`-byte
  allocation at `0x82E420` creates the `ui::System` singleton (`EDF+0x20B2960`) when it does
  not exist yet.
- `HUiHudWeapon` (vtable `0x17FD150`, 22 entries) is the gauge widget. The wrapper makes
  each one through `ui::System` `0x119C600` from a layout path, with an
  `InitParam@HUiHudBase` (vtable `0x17F84C8`) holding the soldier at `+0x28`, the wrapper at
  `+0x30`, the weapon record index at `+0x38`, the gauge number at `+0x3C`, the energy flag
  at `+0x40` (from the builder's InitParam `+0x20`), the vehicle flag at `+0x41`, the
  sub-weapon flag at `+0x44`, the sub-weapon index at `+0x48` and the view index at `+0x4C`.
  The layout's factory (`Factory@HUiHudWeapon`, create `0x82C890`) allocates `0xCA0` bytes
  and runs the constructor `0x82D070`.
- `0x603A00` and `0x604300` are the destructor and deleting destructor of
  `Vehicle501_FortressRobo` (vtable `0x17D98C8`), which has its own field at `+0x2360`. They
  are not an energy getter.
- The energy flag at `0x7FE8CC` comes from an integer parameter of the slot-0 weapon
  record, read through the parameter visitors at `0x179EAF0`, `0x179EA90` and `0x1765220`.
  It is weapon data, not a virtual method of a weapon class.

What the wrapper builds:

- For each weapon record at soldier `+0x1970` (`0x150` bytes each, count `+0x1980`), the
  variant visitor at `0x179EEF0` (`0x240AE0`, `0x240AD0`, `0x240AF0`, trivial) gives the
  number of gauges, and the wrapper makes one widget per gauge: weapon 0 takes
  `lyt_HudEnergyGuage_R` when the energy flag is set, else `lyt_HudWeaponGuageR`; other
  weapons take `lyt_HudWeaponGuageL`.
- For each sub-weapon slot (`+0x1A00`, `0x20` bytes each, count `+0x1A10`) whose weapon
  (`0x592F60`, an index into `+0x1950`) has `+0x248` set, one widget with
  `lyt_HudSubWeaponGuageR`.

What a widget reads:

- The constructor locks the soldier through `HUiHudBase`'s weak reference (`+0x778/+0x780`,
  bound by `0x815810`) and casts it to `SoldierBase` (`0x82DA86`). For a main gauge it stores
  the weapon object at `+0x7E8`: soldier `+0x1950[i]`, where `i` comes from the weapon
  record's parameter data through `0x5A2220` (`0x82DFCC` to `0x82DFDE`). There is no bound
  check against the count at `+0x1960` and no null check; the constructor reads the
  object's `+0x1B0` at once. For a sub gauge it is `0x592F60(soldier, index)`, which checks
  the bound.
- Every update (slot 1, `0x832B30`) calls `+0x7E8`'s vtable slot 27 (`+0xD8`) at `0x832D75`
  with no null check, and copies the 32-byte result to `+0x9D0`. Slot 27 is `0x692100` for
  `WeaponBase` and every non-drone weapon class; the four Air Raider drone classes override
  it (`0x681A60`, `0x683E90`, `0x685970`, `0x6AEBA0`) and call `0x692100` plus direct drone
  helpers. `0x692100` reads only the weapon object's own fields (`+0xBE8`, `+0x248`, `+0x20C`,
  `+0xE68`, `+0xE7C`, `+0x22C`, `+0xE0C`, `+0xF18`, `+0xF28`, `+0xDD0`, `+0x1538`, `+0x142`,
  `+0x78D`, `+0x798`). So ammunition and reload on a remote teammate's gauge are the remote
  copy's own weapon counters, as the game simulates them on this machine.
- The energy gauge (the widget's energy path, `0x8353D0`, called from slot 2 `0x831A40` with
  soldier `+0x2E0`) reads soldier `+0x2FC` (1 during emergency charge), `+0x304` (maximum)
  and `+0x308` (current). It does not read `+0x2360`.
- Other soldier fields read by the widget: the vehicle reference `+0x1540/+0x1550`, the
  class id through soldier vtable slot 83 (`+0x298`: Ranger `0x550A20` returns 0, Wing Diver
  `0x580510` 1, Air Raider `0x563D90` 2, Fencer `0x567240` 3), and the Fencer hammer through a
  cast of `+0x7E8` to `Weapon_ImpactHammer` (`0x82FBD9`).

Local-player settings: a direct-call walk six levels deep from the factory, both
constructors, the wrapper's 7 and the widget's 22 vtable entries, slot 27's five
implementations and `0x8353D0` (609 functions, 115 with cold chunks) found no read of
`SoldierBase +0x338` and no `0x2800` stride. Its settings-table reads all use fixed
offsets: `+0x14FF4` (`0x8161B3`, `0x8318E8`), `+0x2E8` (`0x7F6410`, `0x8D59AB`) and `+0x15000`
or `+0x48` (`0x83E886`). The one `+0x338` access (`0x1159A09`) is a pointer field of a render
object, not the soldier. The resolved indirect calls are the class id, slot 27, the
parameter visitors, the widget's own vtable, `HUiHudBase` slot 21 (`0x816763`) and
`ui::Object` transforms and animations (`+0x10`, `+0x20`, `+0x28` on layout objects, the
`+0x860` animation). Not followed: virtual calls inside the generic UI and engine code
(`0x7E0000` to `0x7F0000`, `0x84B000` to `0x851000`, `0x1100000` and above). At the call
sites inspected they receive layout objects, never the soldier or a weapon.

Energy on a remote copy:

- `SoldierBase`'s constructor (`0x545670`) sets `+0x304` and `+0x308` to 100. The Wing
  Diver's update (vtable slot 5, `0x5808B0`) recharges `+0x308` by `+0x2374` (or `+0x2378`
  during emergency charge, `+0x2FC` = 1) and clamps it to `+0x304`.
- Wing Diver slot 54 (`0x5802A0`) does two different things by the remote bit
  (`+0x128` bit 0). On the local player it packs `+0x2FC == 1` and `+0x1EF1` bit 0 into the
  byte `+0x2478`, which the record element behind mask `0x800` sends (`0x582E40`). On a
  remote copy it sets `+0x308` from that received byte: 0 when the sender is in emergency
  charge, the maximum `+0x304` otherwise, and copies the flag to `+0x2FC`.
- No read of `+0x304` or `+0x308` was found in the player record writer `0x59FDB0`, the
  reader `0x592180` or the mask builder `0x59FB10` (two call levels), nor in the Wing Diver
  element (`0x582E40`, `0x57FCC0`). The record carries `+0x2360`, `+0x2364` and `+0x2478`;
  `+0x2360` and `+0x2364` are set from a scaled 32-bit value at `0x57EDC5` and `0x580444`,
  not from the energy.
- So the remote copy has no copy of the sender's energy. Its `+0x308` is pinned to full or
  empty by the emergency-charge flag whenever slot 54 runs, and moves by the local recharge
  in between. Whether the copy also spends energy from replicated flight input, and how
  often slot 54 runs (it has no direct caller), were not traced.

Remote copies and their weapons: `SoldierBase::CreateOnlinePlayerObject` (`0x591130`)
builds a remote copy through `0x5A3F90` with the member's `EquipWeaponType` (from
`0x595960`) and stores the network record at `+0x1ED0`. Whether `+0x1950` and `+0x1970` on
that copy hold the sender's weapons, with a non-null object for every record, was not
established statically.

Conclusion of the static half: no local-settings dependency was found in the weapon HUD,
so the 0.2.0 crash pattern does not apply to it. Two facts block attaching it before a
live read. The main gauge dereferences `+0x1950[i]` without a bound or null check, both at
construction and on every update, so a remote copy with a missing weapon object would crash
the game outside `Guarded`. And the energy it would show on a remote Wing Diver is the
copy's own value, which slot 54 resets to full or empty from the emergency-charge flag; the
true level is not sent. `tools/inspect_camera.py --weapons` samples both.

## Spectator name caption, 2026-10-24

The user asked for the watched teammate's name at the top of the screen in both
camera modes (2026-10-24), third priority. Static analysis with `tools/pe.py`; the
running game was not read. Built in 3.5.0 (plan M12).

The template is `HudPlayer_RescueMessage`, the game's smallest HUD text widget:

- Factory `0x7FD7E0(camera, weakOut, param)`: writes the camera into the InitParam's
  `+8`, allocates `0xB0` bytes, constructs through `0x8083A0` (`MultiPlayHud_Base`
  `0x7FF670`, then vtable `0x17F6E08`), moves the strong reference into the camera's
  HUD list through `0x7FF390` and returns a weak reference whose pointer is the
  result of `__RTDynamicCast` from `xgs::game::Hud` to `HudPlayer_RescueMessage`. The
  RTTI locator `0x1CA19E0` has class offset 0, so that pointer is the object. The
  builder calls the factory at `0x7FE70E` with the names InitParam (vtable `0x17F6738`,
  player at `+0x10`), the contract `AttachHud` already serves.
- Vtable (7 entries): `0x7FC750` (returns false), `0x8086F0` (update, a bare `ret`),
  `0x11D43D0` (`ret 0`), `0x808410` (draw), `0x8083D0` (destructor: `0x800940`, then
  frees `0xB0` bytes when asked), `0x7FC6D0` (view matrix), `0x7FC630` (projection).
  Slots 5 and 6 choose their matrices by the object's int at `+0x60`.
- Draw `0x808410(hud, context, viewport)`, viewport width and height ints at `+8` and
  `+0xC`. It returns unless the camera at `hud+0x18` has byte `+0x200` set and its
  slot 9 says yes. `+0x200` is a HUD switch on the camera, not a current-camera flag: the
  `SceneCamera` constructor sets it to 1 (`0x118B03A`), and the only other writer found
  in the camera code (`0xF0000..0x110000`, `0x1180000..0x11A0000`) is the setter
  `0x118DF30`, called from `0x1BA790` and `0x14ED330`. Inferred from the health gauge,
  which shares the camera: a camera's HUD draws while it is its view's current camera.
- Placement: `0x7FC380(hud, out, {w*0.5, h*0.5, -600, 1}, viewport)` turns a pixel
  position (y down) into the HUD's space through slots 5 and 6, writing the point with
  w = 1. The draw stores an identity matrix moved to that point at `hud+0x20..+0x5F`
  and calls `0x7FC760(hud, context, viewport)`, which hands it and the projection to
  the draw context. The text's own matrix is the same, moved by the measured size
  (`x - width/2`, `y - (height + 45)`).
- Text: the font global `0x20B29C0` (a pointer). `0x11517A0(font+8, text)` builds a
  `0x38`-byte text object through `0x11379B0`: it enters the font's critical section
  at `font+0x10` (`0x50C10`, `EnterCriticalSection`) and holds the renderer at
  `+0x18`; `0x1138630` releases the renderer and leaves the section (`0x50BE0`). The
  `0x38`-byte style on the stack: the font's floats `+0x80..+0x94`, int 1, colour
  `0x1765B30` (0, 0, 0, 1), 2.5 (`0x1768188`), the font's ints `+0xB0/+0xB4`; `0x113A3F0`
  then writes the scale 1.33 (`0x17F6E98`) to `+4` and `+8`. `0x113B4A0` jumps to
  `0x113B2A0`, whose `0x11388E0` copies the 0x38 bytes into the renderer and points
  the renderer at the copy. `0x1139790(text, out, string, -1, 0)` writes width and
  height as a 16-byte aligned vector (`movdqa`). `0x113C520` jumps to the draw
  `0x113B4B0(renderer, context, matrix, colour, string, -1)` with colour `0x1765B90`
  (1, 1, 1, 1). `0x113C6F0` jumps to `0x113C5C0`, which clears the renderer's style
  pointer. The names indicators' draw (`0x804C90`) uses the same font and the same
  calls for players' names (`0x804F95`, `0x80625A`, `0x806333`).
- Name: the indicators call `0x7FFBD0(weak, &slot, &colour, &name, &platform)` from
  their draw (`0x804EBF`) with the player's weak_ptr by value, the caller adding a weak
  reference (`0x804E98`) that the callee releases (`0x7FFEB5`). It locks the player
  itself (`0x7FF4A0`) and leaves slot -1 when that fails. Online it reads the network
  record (`+0x1ED0/+0x1ED8`) and gets the display name through `0x784830`, plus the
  platform name in brackets when the game's setting `+0x579D` asks. `name` is an MSVC
  `std::wstring` the caller constructs empty (size 0, capacity 7) and frees;
  `0x3D3C0` is the game's deallocation of one (it frees with the game's allocator when
  the capacity is 8 or more and resets it to empty).

The caption in 3.5.0:

- Each new spectator camera gets a rescue message through `0x7FD7E0` with the watched
  teammate as its player. When the returned object's vtable pointer is `0x17F6E08`,
  the plugin points it at its own copy of the table, the locator and the six other
  entries unchanged, with its draw in slot 3. Destruction, update and the matrices
  stay the game's, and `dynamic_cast` still finds the class through the locator.
- The draw repeats `0x808410` with three differences: the text is the name from
  `0x7FFBD0`, the pixel position is (w*0.5, h*0.1), and the text is centred on that
  point (`x - width/2`, `y - height/2`). It draws nothing while the slot is -1 or the
  name is empty, and frees the name through `0x3D3C0` in either case. The arguments
  are laid out as `0x808410` lays out its own: 16-byte aligned matrix, colour and
  size, a `0x38`-byte style and text object.
- The draw runs where the game draws its HUD, without `Guarded`, like the 3.4.0 slot
  function. It calls only functions that `0x808410` and `0x804C90` call there.
- No message is sent and no game code is patched beyond the object's own vtable
  pointer.

Not verified live: where the caption lands and how large it is, that it follows W and
both camera modes, that it goes on revival, and names with characters outside ASCII.
The vertical centring assumes the text grows down from its matrix point in the HUD's
y-down space, as `0x808410`'s offset suggests; if it does not, the caption sits one
text height higher.

## Smoothed locked aim and vehicle heading, 2026-10-26

User decision, issue #12005: locked mode may use smoothed received aim on foot and
teammate matrix heading in vehicles, with the same camera offsets. This supersedes
the 2026-10-24 decision to use the native follow rotation unchanged.

Static inspection with `tools/pe.py` found:

- `0x5921BC..0x5921F8` tests record mask `0x02` and writes decoded pitch and yaw to
  soldier `+0x1230/+0x1234`. Reading them each frame exposes values, not arrival
  events. The same values can mean still aim, a repeated packet or no packet.
- The soldier update at `0x5750E0` has a vehicle branch at `0x5751B9`. It requires
  a non-null control block at soldier `+0x1550` with a nonzero strong count at
  control `+8`. At `0x5751D3..0x57520B` it takes the seat object at `+0x1540`,
  copying its matrix `+0x270..+0x2AF` into soldier `+0x60..+0x9F`. The fallback
  therefore reads the seated soldier's current forward row, not the received aim.
- `0x4E1F0` takes a forward vector and returns `atan2(x, z)`, or 0 when both
  components are zero. Vehicle exit uses it on soldier `+0x80` and writes the
  result to `+0x1234`. This establishes the yaw convention. The plugin rejects
  nonfinite and nearly vertical or degenerate horizontal vectors instead of
  choosing an arbitrary yaw.

Both spectator modes now set the existing orbit parameters. Locked mode uses a
50 ms exponential time constant, measured with `steady_clock`, and wraps yaw
before taking the shortest angular difference. It approaches the observed sample
without overshoot or extrapolation. It does not estimate an arrival interval from
angle changes. At steady 60 fps, each frame covers about 28% of the remaining gap.
A vehicle state change starts at the new source immediately, with pitch 0 in the
vehicle. Changing target or mode, revival and invalid input reset the filter.
The stale-aim message was removed because equal angles never proved packet loss.

`SpectateTarget` exercises the production reader and filter on soldier-shaped
storage: wrapped yaw, missing samples, different frame rates, target changes,
vehicle turns with unchanged aim, expired vehicle references and nonfinite input.
The added hash ranges contain no base relocations. No network code changes.
The MinGW build with `EDF6_GAME_DIR` and `./build.sh -r` passed. The focused Wine
run passed all seven Spectate and PluginLoad tests with the supported EDF.dll;
none skipped. This does not establish live camera behavior.

Evidence limits: no live vehicle capture was made for this change. Static code
establishes the seated matrix copy and yaw convention, but vehicle classes and
seat orientations still need live checks. The matrix represents the teammate's
seat heading, which need not equal the vehicle hull or its gun direction. On-foot
smoothness and added lag also need player acceptance; the filter cannot recover
turns that never reached this machine. Existing camera offsets, position following
and native camera smoothing remain in use.

## Open questions

- Whether the wrong directions reported for the pre-0.4.0 camera were in vehicles.
  The old capture did not record vehicle state. Equal received angles could not
  distinguish missing updates from a stationary aim.

- Live-check the isolated health/name components in 0.3.0, including name placement,
  health changes, revival, repeated deaths, vehicles and disconnected targets.
  The basic 0.1.1 camera follow and return were confirmed by the user and log.
- Full combat HUD support is deferred; remote ammo and class gauges remain unverified.
- Whether a player can stay down until the mission ends, or bleeds out, and what
  happens to the object then.
- Whether NPC allies appear in the TeamManager map. The plugin only takes objects
  with a network player record and the remote bit, so NPCs would be skipped.
- Whether a remote player's object is destroyed when that player leaves the room,
  and whether anything other than the scene steps destroys it between the plugin's
  check and the camera update.
- Whether any job thread reads view 0's camera while `Application::Update` runs.
- Who calls `0x7D0B10` (text entry begin), and whether the Steam overlay keyboard
  sets the text-entry byte. Decided input: W (plan M4).
- Whether the orbit camera follows the downed player's look input in a live room,
  and whether the health gauge shows in both camera modes from 3.4.0.

## Control hint and lobby version, 2026-10-27

The user requested a bottom spectator control hint and a bottom-right lobby version.
The caption's existing native text renderer now also draws the hint at 50% of the
viewport width and 94% of its height. It uses the same camera visibility and live
player-name lookup as the caption, in both modes. No extra HUD object is retained.

`HUiRoom` has its primary vtable at `0x180AC40`. Slot 2 points to `0x7FA970`,
which draws the UI root. At `0x7FAB70` it checks visibility through slot 15, draws
its layout at `0x7FABA9`, then calls slot 16 with the root in RCX and draw context
in RDX at `0x7FABCB`. The room's slot 16 at `0x180ACC0` points to `0x7D6C30`,
a jump to the empty `0x7E57C0`. Only this room slot is replaced.

The version label repeats the root's orthographic projection from
`0x7FAA49..0x7FAA86`: `0x4E000(out, 960, -540, 1920, 1080, 0, -4000)`.
The native projection maps positive Y down. The text's right edge is at X=930,
with its vertical center at Y=510 and Z=-600. The shader transform setter is
`0x112F590(shader, context, view, projection)`, called by the HUD transform at
`0x7FC8F0`; its singleton is `0x2139A98`. The font singleton `0x20B29C0` also
serves ordinary menu text, including room code at `0x8F966A`.

The native text calls are shared by the name, hint and version label. The lobby
hook verifies its slot, dispatch, thunk, projection, shader setter and text code
before writing. A guarded drawing fault stops the label for the session. It
installs independently of the spectator setting. `RoomLabelGameCode` exercises
the installed hook and real projection with renderer spies, including foreign-slot
refusal, missing-font handling, version text and right alignment. This establishes
the calling contract, not the rendered placement or interaction with menu panels.
Live Windows and Proton checks remain required.
