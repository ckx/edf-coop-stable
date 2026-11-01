# Enemy replication compared with players, 2026-10-31

Static reading of the supported EDF.dll (TimeDateStamp 0x678CCB46) with
`tools/pe.py`. H = read from instructions, M = inferred with evidence, L = guess.
Question: how does the game replicate a host-owned enemy, and what of it explains
why enemies never show a wrong facing while remote players sometimes do.

## Classes (H)

GameObjectBase 0x17CD3F0 / WalkingEnemyBase 0x17CCB98 / InsectBase 0x17C5140 /
GiantAnt 0x17BB798 / MonsterBase 0x17C5FA8. Every one has the 22-slot
NetworkObject table at +0x120, the same base interface as SoldierBase.

| Sub-table slot | SoldierBase | WalkingEnemyBase | GiantAnt |
| --- | --- | --- | --- |
| 5 (write record) | 0x125A70 thunk to main slot 92/52 | 0x53EF80 | 0x40C510 |
| 6 (read record) | 0x125180 thunk to main slot 53 | 0x53CAD0 | 0x408400 |
| 7 (per-write hook) | 0x59FA80 | default 0x125A80 | 0x4AA550 (counter `+0x11C0`++) |

Enemies build mask, write and read in one function, no main-table slots 92/52/53.
`net::WalkCharacterSyncControl` (vtable 0x17ABA10, slot 0 write 0x77D500, slot 1
read 0x77C2C0) sits at GiantAnt+0x1F60 (0x4059B0) and WalkingEnemyBase+0x1510
(0x53CB02). It is the enemy counterpart of `HumanCharacterSyncControl`
(soldier+0x1830, `player.md`).

## What the owner sends (H)

GiantAnt writer 0x40C510, object offsets (the function's `this` is the sub-object
at +0x120):

- Nothing while the copy is remote (`+0x128` bit 0) or `+0x2E8` is set (0x40C53A).
- Bit 1, pose: `+0x1F60` sync control slot 0, every `n` counts of `+0x11C0` where
  `n` is 90 (40 when `+0x2131` is set) while idle, 12 in AI state 0 with a live
  timer, and 3 in AI states 1 and 2 with a live timer (`+0x1530` state,
  `+0x1640` timer, 0x40C567..0x40C5C1). A moving enemy sends its pose every third
  sync frame; a still one every 1.5 s.
- Bit 2, one state byte `+0x1F22` while a countdown `+0x1F21` runs (0x40C5FF).
- Bit 4, a 64-bit id from `+0x490` every record (0x40C75C). M: the target object.
- Bit 8, every sixth count or on demand (`+0x1F1A`): one byte packing three AI
  timers `+0x1640/+0x1644/+0x1648` into two bits each (above 40, above a float
  threshold, positive, zero) plus `+0x1F19` in the top two bits (0x40C775..0x40C814).
- Bit 0x10, a 64-bit value `+0x1F10` every 80 counts; bit 0x20 flags `+0x1F1B`.
- Bit 0x40, a half-float vec3 `+0x1540` every 80 counts while not held and `+0x548`
  is null (0x40C69D..0x40C6E2).
- Bit 0x80, a vec3 `+0x1F40` (only when `+0x5C8` == 1) and a 64-bit `+0x1F50`
  while a countdown `+0x1F30` runs. M: an attack target point and id.

The pose record (0x77D500) is: one flags byte (bit 8 set when the control has no
pose, bits 0..2 event flags), one signed 8-bit sequence `+0x1B1`, a half-float
vec3 position `+0x190` (0x761B20, 6 bytes) and four half floats of rotation
`+0x1A0` (0x7617B0, 8 bytes). M: `+0x1A0` is a quaternion; the reader's
counterpart 0x760A40 reads four halves and the receive path runs it through the
dot-product and cross-product code at 0x77CCC0..0x77CE75.

The owner refreshes the control every frame from its own NetworkUpdate
(WalkingEnemyBase main slot 54, 0x53DFC0 / 0x53E010): the local path computes
the position from the body matrix `+0x1180/+0x11B0` and `+0x1210`, calls
0x77C600 with it and the rotation `+0x1190`, and 0x77C600 copies both into the
control and increments the sequence (0x77C671). Each sent record therefore
carries the frame's pose with a fresh sequence.

## What the receiver does (H code, M semantics)

Reader 0x77C2C0 reads flags, sequence, position and rotation, then compares the
sequence with the stored one using signed 8-bit wrap arithmetic (0x77C397..
0x77C3F3). An older or repeated record is dropped (0x77C40F); the first record
after a reset takes any sequence (`+0x138`). Accepted records go into a ring of
five entries of 48 bytes at `+0x40` (flags, position, rotation; head `+0x28`,
count `+0x30`). When the ring is full the newest entry replaces the last one
(0x77C439..0x77C470); when the previous entry's rotation is close to the new one
(dot product above 0x1765A1C) the last entry is overwritten instead of appended
(0x77C4C8..0x77C511).

Update 0x77C8E0 runs every frame on the remote copy (0x53E1AA, with the copy's
current position and rotation `+0x1190`) and returns an output struct:

- State `+0x10`: 0 idle, 2 moving to a target. When idle and the ring is not
  empty it pops the oldest entry into the current target `+0x180/+0x190/+0x1A0`
  and enters state 2 (0x77CAC3..0x77CB55), granting 40 more frames of
  convergence time, capped at 120 (`+0x134`, 0x77CAD0).
- Warp (0x77D610) when the ring holds five entries (0x77C9B2), when the current
  target is 40 m or more from the copy (0x77CA7A, 0x1765A50), or when the
  120-frame budget runs out (0x77CA9B).
- In state 2 the copy is moved toward the target with a per-frame step derived
  from the error (0x77CF61: error / 60 * 5, further capped around 0x77D361 by the
  frame's own displacement * 1.1, 0x179D1C0), and the rotation is turned toward
  the target rotation by an axis-angle step built from the angle between the two
  (acos at 0x77CD41, 0x64B90 builds the step, 0x643D0 multiplies, 0x64500
  normalizes). When position and rotation are close (0x77D0C2, 0.3 at 0x1765A18;
  0x77D2EB) the state returns to 0 and the next ring entry is taken.
- Output flags: 4 = position valid (every state-2 frame, 0x77D46B), 8 = rotation
  valid (0x77D47C), 1 = stop, 2 = start, 0x10 = push through the controller.

The copy applies the output in 0x53E1AF..0x53E3B4: flag 4 writes the position
through the character controller at `+0x1160` (0x6DAE50) and derives the copy's
velocity `+0x14E0` from the frame's displacement; flag 8 writes the rotation
directly into `+0x1190` (0x53E291) or through the controller (0x6DB170). The
copy's AI does not own its rotation: whatever the AI's own movement does, the
sync output overwrites the pose every frame the control is active.

The GiantAnt reader (0x408420) also feeds the replicated AI timers back into
`+0x1640/+0x1644/+0x1648` (0x4084B5..0x4085D8: set to 10, clamp to 10..40, keep
below 10, or zero) and the target id into `+0x490`. M: the copy runs its
animation and movement state machine from the owner's intent, while the pose
comes from the sync control. Its facing cannot disagree with the owner's for
longer than the ring playback lag.

## Soldiers for comparison (H)

Mask builder 0x59FB10, player path (main slot 33 true): position bit 1 every
sixth sync frame, aim bit 2 every sixth frame (every frame while `+0x1E41` is
set: that flag is set on entry to the Ranger state registered at 0x5508FF
(0x552040, online only, 0x5521C2) and cleared on its exit (0x5528AD); the same
state uses the yaw offset `+0x1E44`). No sequence, no body rotation: the record
carries pitch `+0x1230` and yaw `+0x1234` plus `+0x1E44`, which the reader
0x592180 stores as they arrive. Nothing queues or smooths them, and the copy's
own state code can write them from a matrix (0x587D9E/0x587DAC in the state
function 0x587A10; vehicle exit 0x5705A5, `docs/spectate/research.md`).

AI soldier path (slot 33 false, 0x59FC3A): position and aim every 30 sync
frames, bit 0x20 every 120, bit 0x200 every 6, bit 0x100 every 60, bit 4 every
10 while `+0x1E40` is set. The remote NetworkUpdate 0x596130 runs the same
HumanCharacterSyncControl for both, with a `PlayerCtrlSyncChecker` (0x17D2E58)
or `AICtrlSyncChecker` (0x17D2E70) (0x5962A7..0x596303). An AI soldier copy
gets aim twice a second, so its facing between records is its own AI's.

HumanCharacterSyncControl's record is the half-float position alone (0x77AD00),
read by 0x77AB00 into a single target, no sequence and no ring.

## What this means for player sync

- The enemy model replicates the pose (position and rotation together) with a
  sequence and plays it back in order; the copy's logic runs on replicated
  intent and never owns the pose. The player model replicates position and aim
  angles in separate records on separate frames, unsequenced, and leaves the
  copy's body to local code between records.
- The plugin's block already has sequence and sender time for position and,
  since 3.6.3, native aim bit 2 in every block. What it does not have is the
  enemy's per-frame application: the received aim is written once by the native
  reader and then left to whatever local path touches `+0x1230/+0x1234`.
- `WalkCharacterSyncControl` cannot be reused for soldiers: it drives the
  walking enemy's controller at `+0x1160` (0x6DAA10/0x6DAE50/0x6DB170), not the
  soldier's proxy at `+0x680`, and soldiers have no such object. The model can be
  copied in the plugin: carry aim (and the body yaw `+0x1244`, M) in the block,
  keep the enemy's ordering and freshness rules, and write the angles into the
  copy every frame from the slot 55 wrapper after the native update.
- Not examined: how enemy bullets reach other machines (`net::WeaponBulletSyncTable`
  exists in the RTTI, not traced) against the owner messages of player bullets (review 2026-10-26,
  finding 3). The enemy pose model says nothing about projectiles.
