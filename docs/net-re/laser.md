# Closed Laser M2 diagnostics

The supported EDF.dll has TimeDateStamp 0x678CCB46. Local asset inspection of
Root.cpk WEAPON/PWEAPON128.SGO confirms name.en `Closed Laser M2`,
`Weapon_ChargeShoot`, AmmoClass `LaserBullet01`, and `LaserMuzzleFlash01`.
Other weapons share LaserBullet01, so these logs are not exclusive to M2.

The diagnostic wraps three native vtable slots and always forwards the original arguments:

| Role | Slot RVA | Native target |
|---|---|---|
| Update, main object slot 5 | 0x179FCD8 | 0x2535C0 |
| Receive, NetworkBullet at object+0x120 slot 3 | 0x179FD40 | 0x22F660 |
| Draw, render helper at object+0xE90 slot 1 | 0x179FD60 | 0x252E10 |

Update increments age +0xF38 and reads lifetime +0xB48. The constructor initializes
age to zero at 0x251EFB. SyncRender at 0x253417 subtracts the endpoints at +0xF90
and +0xFA0, computes rendered length +0xFB0, and copies width to +0xFB4.
Draw uses the forward basis at +0x80 and position +0x90, and only renders pass 1.
Snapshots run before Update or Receive, so render values may describe the previous
render synchronization. Age-zero values may precede the first render altogether.

NetworkBullet's owner query 0x22F3C0 reads a weak reference at object+0xAE8/0xAF0.
Diagnostics check the control's live count and read player-specific ownership fields
only for the five known soldier vtables. Every probe is guarded. EOS renders a player
id only when available; expired or unfamiliar owners remain unknown. No weak reference
is acquired and no network object id is allocated for logging.

Aggregate update, first-update, receive and pass-1 draw callbacks are reported every
30 seconds on the next update/receive callback. At most eight detailed snapshots are
emitted per window. A fixed 64-entry table counts draw callbacks for recently observed
beams; it can evict entries, and pointer reuse or concurrent rendering makes those
counts observational rather than a persistent projectile identity. Raw object addresses
are local to one process. Compare owner ids, wall-clock times and beam geometry across
machines, not object addresses. A receive callback is not every shot announcement, and
a draw callback is not proof that pixels were visible. These hooks do not trace target
HP or prove damage delivery.

LaserDiagnosticsGameCode exercises the installed hooks against the real, privately
mapped EDF.dll. Spies replace only the native physics/render/receive functions. It
checks whole-install refusal for a foreign slot, argument and stream preservation,
owner and beam reads, fault-safe fallback, draw association, logging limits and renewal.
Live laser behavior remains unverified. There is no laser gameplay fix in 3.0.2.

## 2026-10-17 wrong-direction report

The Australian joiner's `EDF6Coop-desyncaim.log` retains 2026-10-18
15:41:04 through 17:00:21, with its earlier lines trimmed. The local host log
retains 2026-10-17 21:45:21 through 23:11:44. Shared room and gate events identify
overlapping play; the joiner's clock is approximately 18 hours and 1.3 seconds
ahead. These clocks are not synchronized closely enough to identify a projectile
by timestamp alone.

The joiner's final summaries report RTT 173 ms, no exhausted retries, skipped
reliable packets, disconnect holds or send failures, and 47,690 accepted player
blocks with zero refused blocks and six warps. Retransmissions occurred. These
aggregates do not exclude short delivery stalls or establish correct aiming.
The movement bursts contain sample ages above 400 ms, but they are selected
around unusual corrections or old samples, not a representative latency survey.

The file contains remote host laser directions. It does not contain the host's
sent aim angles, the joiner's received aim angles, or a shared projectile ID.
The eight-snapshot cap selects different shots on each machine. Matching owner,
rough time and geometry can suggest correspondence, but does not establish that
two entries describe the same shot. Zero laser receive callbacks does not mean
that firing announcements were absent. The weapon and event time remain unknown.

Static inspection of the supported EDF.dll confirms these separate paths:

- SoldierBase writer 0x59FDB0 handles mask bit 2 at 0x59FDFF. For players it writes
  pitch from +0x1230 at 0x59FE14 and yaw as +0x1234 plus +0x1E44 at 0x59FE24.
- Reader 0x592180 handles the same bit at 0x5921BC, stores pitch/yaw at
  +0x1230/+0x1234, and clears +0x1248 at 0x5921F2. The coop hook calls this
  original reader before reading its appended position/velocity block.
- On the ordinary active-player path, mask builder 0x59FB10 includes aim every
  six sync frames. Although +0x1E41 first selects a two-frame divisor,
  0x59FBB5 subsequently forces aim into every record when that flag is set.
- PlayerSync's custom block contains position and velocity, not aim. Its update
  hook changes position-correction control and added velocity, not pitch/yaw.
  This alone does not rule out an indirect effect elsewhere in the game.
- Slot 63 at 0x570D80 adjusts +0x1210/+0x1200 using smoothed correction +0x1920,
  then rotates the result using body yaw +0x1244 into +0x11E0. It does not directly
  write pitch/yaw. The earlier description of this function as turning the
  character was too strong.

The sibling spectator investigation independently traces CharacterGhostCamera's
target matrix copy at 0xF871C..0xF8740 and its optional explicit camera rotation.
Its earlier read-only captures found remote actor forward matching received aim,
but did not capture the reported failure while spectating. Shared dependence on
the remote character makes a common cause possible; it does not prove one.

The next discriminating capture needs the affected weapon and event interval,
then corresponding sent/received native aim records and the remote actor,
projectile or spectator-camera direction during that interval. Neither existing
log establishes whether the symptom is delayed aim, pose, projectile construction
or camera orientation. No gameplay change is justified by this evidence yet.
