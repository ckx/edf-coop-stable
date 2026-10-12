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
