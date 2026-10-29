# Facing reversal capture, 2026-10-27

Issue #12009, Network sync board. The user saw Clam facing away from an ant while
Clam reported aiming toward it. It appeared in both free and locked spectator
modes. Clam also reported the reversal while watching K1 after a hit. Knockback
or ragdoll involvement is a hypothesis, not an established cause.

## Session evidence

Inputs were the local installed `Mods/Plugins/EDF6Coop.log` and
`/home/ckx/rgx/EDF6Coop-K1.log` and `/home/ckx/rgx/EDF6Coop-Clam.log`.
The logs contain older sessions too. The final room is
`fec1ad8ab30c4f13a5578c8d800d92fa`, with everyone on 3.6.0. K1's earlier launch
was 3.1.2, followed by a 3.6.0 restart at 23:30:32. Clam's clock is approximately
18 hours ahead, with several seconds of skew. Local timestamps alone do not
identify a shared packet or hit.

The host watched slot 2 around 23:35:33, switched to locked at 23:35:38, free at
23:35:43 and locked again shortly afterward. Clam watched slot 2 around 17:35:36
on their clock. These intervals narrow the report but contain no aim measurements.
The final player-sync summaries show no refused position blocks on any machine.
This says nothing about native aim, which is outside the custom position block.

K1's direct link fell back to EOS at host time 23:34:49 after 1609 ms without
a response. Direct routing resumed at 23:34:50. Host summaries show
66 gave-up reliable packets and 351 ms held; K1 shows 68 and 386 ms. Both logs
also show EOS traffic. Those counters describe the direct transport's recovery,
not proof of game-state loss or the cause of the later reversal. Clam's own
host link reports no exhausted retries in that interval.

## Native path and limits

The supported EDF.dll's native writer `0x59FDB0` sends pitch `+0x1230` and
`yaw +0x1234 + offset +0x1E44` under mask bit 2. Reader `0x592180` stores
pitch and the summed yaw, then clears `+0x1248`. The existing custom player
block contains no aim. The plugin calls the native reader first.

Other native paths can write aim. The soldier update at `0x573BB0..0x573C28`
adds its own input and clamps angles; `0x587DAC` writes yaw from a matrix in
another state path. The existence of these writes does not establish that they
ran incorrectly in this session. No aim or ragdoll behavior is patched by 3.6.1.

## Diagnostic contract

With `[Diagnostics] Player=1`, `PLAYER aim` records include native pitch, yaw,
yaw offset, body yaw, matrix forward, health, position-correction enablement and
vehicle presence. Identity comes from the soldier's existing network user.

- `send` observes the native writer's inputs before it runs. The existing
  appended position block supplies `seq` and `senderMs` for pairing.
  `send-noaim` captures local angles when that record does not contain native
  aim, so a stopped aim writer can be distinguished from delivery failure.
- `receive` observes the fields after the native reader. `receive-stale` means
  the custom position block was rejected by its sequence/clock checks, even
  though the native reader has already applied the native part of that record.
  Since 3.6.2, `receive-aim-stale` instead means an older or repeated paired aim
  was discarded and the pre-read pitch, yaw and accumulator were restored.
  Aim freshness is independent of position freshness.
- `update` observes the same remote object after its native NetworkUpdate.
- `spectate-free` and `spectate-locked` observe the watched object in the
  spectator frame, with the most recently received aim alongside it.

Match room, user, sequence and sender timestamp before comparing send and receive.
Add the sender's logged offset to its yaw. Do not add the receiver's offset again.
Native serialization can quantize the angles. The periodic selection is
`seq % 30 < 3`, plus health transitions limited to one extra record per 250 ms.
The usual send interval selects about one matching native record and up to
three local snapshots per second.
Spectator observations are limited to one per object per second. An update after
a selected receive is recorded, with additional observations at most once a second
when health changes or yaw differs from the received value by more than 0.5 radians.
These limits can omit a short event. A missing paired line is not proof of loss.
Room membership changes clear the observations; no diagnostic bytes are sent.

`PlayerAimGameCode` calls the installed write/read hooks against the real native
serializer. It checks byte preservation, summed yaw, native accumulator clearing,
full record consumption and a read-only spectator observation after an artificial
local yaw change. This fixture does not simulate ragdoll physics.

For the next capture, everyone runs 3.6.1 with player diagnostics enabled. Note the
time, watched player and mode when it reverses, then close the game and collect
all logs. Persistent disagreement between the paired angles points to replication;
agreement at receive followed by a changed frame angle points to a local update;
correct frame aim with a wrong visible pose requires the pose/camera path next.

## Reordered aim protection, 3.6.2

Issue #12012 adds a separate per-player aim sequence and arrival baseline for
records carrying both native aim and a valid custom block. Older or repeated
aim within the 400 ms freshness window restores only the native reader's three
aim writes. The reader still consumes and applies all other state. Native-only
records are unchanged; room changes clear the ordering table, and stale aim
tracks accept a new baseline. Installation checks the native store instructions.

The real-game reader test covers reordered aim, an intervening newer
position-only record, a local pose change, the accumulator and native-only
records. The native object reader at EDF+592130 reads the mask and directly
calls slot 53, with no sequence check at that layer. This does not establish
whether an earlier game dispatcher filters old records, or whether reordering
caused the reported post-hit reversal. The live capture remains necessary.

## On-foot aim gaps, 2026-10-28 capture and 3.6.3

Issue #12013, Network sync board. Inputs are `EDF6Coop-K1-1.log`,
`EDF6Coop-Clam-1.log` and the host's installed log, preserved under
`/tmp/edf-aim-20261028/`. The shared room is
`2f14e9596d294dea99ee5a4746f526a5`. Host and K1 run 3.6.2; Clam runs
3.6.0 during the mission and restarts on 3.6.2 afterward. The user confirms
K1's wrong-direction shots were on foot, shortly before the host noted 22:19.
The nearest logged on-foot spectator interval is 22:18:49 through 22:18:54,
in locked and then free mode. This narrows the report, not the exact shot.

Matched K1 aim records give a median host-receive minus sender-log time of
2.696 seconds, including clock skew and delivery. Across 134 matched records,
the largest yaw difference is 0.422 degrees and pitch difference 0.046 degrees,
consistent with native half-float quantization. This is not a latency estimate.

The host's last paired aim before the gap has sequence 14571 and sender time
2161662788, received at about 22:17:16.733. It remains the trace baseline until
paired receives resume at 22:20:26.337. K1 logs 72 aim-bearing writes between
host times 22:17:17 and 22:20:26 after the above clock adjustment. The host's
motion bursts contain 21 distinct accepted position sequences in that interval;
all are 1 or 2 modulo 3, while those 72 aim-bearing writes are 0 modulo 3.
Buffered motion lines must be aligned using their `t` field, not their later
emission timestamp. The connection's retry counters do not increase around
22:19; no exhausted retries or disconnect hold are recorded there.

This establishes a gap in paired native aim while position continues. It does
not establish the exact production step thinning the records, exclude all
native-only aim updates, or match any of K1's projectiles. The remote soldier
continues changing angles during the gap. Its large diagnostic `ageMs` must
not be read as proof that all aim fields froze.

3.6.3 adds native aim bit 2 to every local player-sync block while the player
is eligible for ordinary on-foot aim. The eligibility reads match
EDF+59FB30..59FB49: a live vehicle control at +0x1550 or a nonzero downed flag
at +0x2E8 excludes the player. Installation verifies those instructions before
changing any hooks. Both soldier mask families retain their native bits. The
existing PlayerSync feature gate controls the additional aim, and the existing
native writer/reader carry it. No block fields or protocol numbers change;
older compatible receivers already understand bit 2. The sender must update.

The `PlayerAimGameCode` regression uses both real native mask families and the
installed mask/write/read hooks, delivering every third block in all three
phases. Only aim and the custom block are serialized because other soldier
fields are outside this fixture. Before the fix, the base-family regression
fails all 12 deliveries in the two phases without aim. After the fix, every
delivered sample updates pitch and summed yaw. It also checks the closed gate,
remote players, downed players, live vehicle controls and expired controls.
The delivery selection models the capture; it is not a reproduction of the
unknown thinning implementation. Live shot-direction improvement remains
unverified. This change makes fresh aim independent of which position block
survives, rather than changing packet ordering or position correction.

Validation: all 31 non-stress tests pass under both MSVC/Windows and
clang/MinGW/Wine, including all 18 game-code cases without skips. DirectNet
and LogFileCap stress tests were excluded.
