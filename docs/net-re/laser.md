# Closed Laser M2 diagnostics

The supported EDF.dll has TimeDateStamp 0x678CCB46. Local asset inspection of
Root.cpk WEAPON/PWEAPON128.SGO confirms name.en `Closed Laser M2`,
`Weapon_ChargeShoot`, AmmoClass `LaserBullet01`, and `LaserMuzzleFlash01`.
Other weapons share LaserBullet01, so these logs are not exclusive to M2.

The diagnostic wraps three native vtable slots and four sender calls, always forwarding the original arguments. The sender evidence is below.

| Role | Slot RVA | Native target |
|---|---|---|
| Update, main object slot 5 | 0x179FCD8 | 0x2535C0 |
| Receive, NetworkBullet at object+0x120 slot 3 | 0x179FD40 | 0x22F660 |
| Draw, render helper at object+0xE90 slot 1 | 0x179FD60 | 0x252E10 |

Update increments age +0xF38 and reads lifetime +0xB48. The constructor initializes
age to zero at 0x251EFB. SyncRender at 0x253417 subtracts the endpoints at +0xF90
and +0xFA0, computes rendered length +0xFB0, and copies width to +0xFB4.
Draw uses the forward basis at +0x80 and position +0x90, and only renders pass 1.
Snapshots run before Update and after Receive. Render values may describe the previous
render synchronization. Age-zero values may precede the first render altogether.

NetworkBullet's owner query 0x22F3C0 reads a weak reference at object+0xAE8/0xAF0.
Diagnostics check the control's live count and read player-specific ownership fields
only for the five known soldier vtables. Every probe is guarded. EOS renders a player
id only when available; expired or unfamiliar owners remain unknown. No weak reference
is acquired and no network object id is allocated for logging.

Aggregate update, first-update, receive and pass-1 draw callbacks are reported every
30 seconds on the next update/receive callback. At most eight detailed snapshots are
emitted per window; a message event also has a decoded-message line. A fixed 64-entry table counts draw callbacks for recently observed
beams; it can evict entries, and pointer reuse or concurrent rendering makes those
counts observational rather than a persistent projectile identity. Raw object addresses
are local to one process. Compare owner ids and native bullet IDs across machines;
message type and payload hash can further identify a corresponding message. A receive callback is not every shot announcement, and
a draw callback is not proof that pixels were visible. These hooks do not trace target
HP or prove damage delivery.

LaserDiagnosticsGameCode exercises the installed hooks against the real, privately
mapped EDF.dll. Spies replace only the native physics/render/receive functions. It
checks whole-install refusal for a foreign slot, argument and stream preservation,
post-receive state, owner and beam reads, bounded decoding, draw association, logging limits and renewal.
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

## 2026-10-26 owner message capture

The diagnostic now records the native bullet identity at `bullet+0x128` in initial
update snapshots and in sent and received messages. Pair it with the owner EOS id
and the room/session. Its low eight bits select the weapon component; its upper
24 bits are the native sequence. Registration at `0x7639FC..0x763A07` writes this
packed value. `0x7650BA..0x7650DC` splits it into the registry key. The sender
serializes owner reference, low byte and upper sequence at `0x7645F8..0x764616`;
the receiver matches them at `0x764B10..0x764B26` before calling the registered
NetworkBullet receive. Logging reads the existing identity and allocates no ids.
An unknown owner or bullet id -1 cannot establish a cross-machine match.

The owner send is `0x765570`, with NetworkBullet in RCX, the Serialize writer in
RDX and an immediate-route bool in R8b. The diagnostic wraps only the four calls
at `0x22F21B`, `0x22F28C`, `0x22F309` and `0x2A5764`, filtering LaserBullet01
before logging and always preserving the native call. The first three calls are
inside main-object slot 13 at `0x22F0B0`; the last sends type 3 from `0x2A56B0`.
`0x773DA0` is a different NetworkObject path. Writer bytes start at +0x10, and
+0x5F0 holds their length, as `0x12B4630` confirms.

Receive captures a bounded private copy of the unread bytes before invoking the
original callback, then decodes that copy and logs the bullet's post-receive
matrix. The live stream is never marked, rewound or modified by diagnostics.
This avoids decoding from the cursor the native callback has already consumed.
The copied stream uses the native integer reader only after checking its tag and
encoded length. Vector bins use `ReadBinElement` with an exact destination size.
The parser does not resolve object references or invoke collision code.

| Type | Fields after the type |
| --- | --- |
| 0, 3 | 12-byte velocity bin, then 12-byte position bin |
| 1 | target reference id, part, 12-byte vector bin, packed vector |
| 2 | target reference id, part, event integer, 12-byte vector bin, packed vector |

The reference and part precede the geometry for types 1 and 2. The review's
suggested read order was incomplete. Their final packed vector is bounds-checked
but not interpreted in the log. The meanings of their event and attachment
geometry still need live evidence.

Types 0 and 3 update physics fields +0xCD0 and +0xCC0. Constructor code
`0x231D97..0x231E7B` copies matrix translation into +0xCC0 and builds +0xCD0
from forward, a scalar and inherited motion divided by 1/60 second. Thus
`vector1` is velocity and `vector2` is position. Compare vector2 with the
sample's position to measure the matrix discrepancy when the correction arrives.
The matrix may change only on the next Update; receipt alone does not prove
that the displayed beam moved immediately.

Every new game range used by the capture is checked with FNV-1a64 before any
hook installs. The four original call targets and three vtable slots are checked
too. The test maps the supported EDF.dll privately and executes installed hooks,
uses the game's integer writer and reader, and replaces native simulation and
network delivery with spies. It checks post-receive observation, untouched live
bytes and cursor, type decoding, malformed length refusal and send arguments.

The limit remains eight detailed events per 30-second window, including initial
update snapshots and messages, plus activity summaries. A message event has a
`LASER sample` line followed by `LASER message`. Match owner, bulletId, type and
payload hash across machines. The hash is an observation of bytes, not an
authentication check; identical repeated messages remain ambiguous. Independent
window limits can omit corresponding events. Native sequences wrap and are not
global identifiers. A type 0 correction is conditional on the bullet's flags,
callback argument and physics state, so zero received messages is not proof of
missing shot announcements. Initial update snapshots remain useful when no
corrections are sent, but age zero is the first observed update, not construction.

For the live capture, both players install the diagnostic prerelease and keep
`[Diagnostics] Laser=1`. Start a new two-player room, hold still, and fire one
Closed Laser M2 shot at a distinct point. Wait at least 31 seconds between shots
so one burst cannot consume the next capture window. Repeat while turning, then
with the previously reported wall/ant discrepancy if reproducible. Close the
game and collect both logs. Match a known owner and bulletId before comparing
initial positions or messages. Record which questions the matched lines answer:
initial observed position/direction, whether corrections arrive, and the position
discrepancy at receipt. If caps omit a counterpart, repeat that shot; do not infer
loss from the absent detail line.

No matched two-machine capture has been made in this change. Initial construction
agreement and the reported live aim failure remain unverified. The decoded
diagnostic stays on by default for this prerelease so the requested capture can
be collected. A stable default-off decision remains after that capture.
