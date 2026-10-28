# Aim and movement jitter review

Scope: `4f035fea476f5c4e12bf31ab5d6591675a945764`, player sample generation,
acceptance and correction, native aim hooks, locked spectator filtering, related
tests and the unordered transport delivery path.

Excluded: live multiplayer, projectile replication, a full transport audit and
changes to gameplay. No fix or release is included.

Method: source review, the existing research notes, direct disassembly of the
supported EDF.dll aim reader, and a small C++ probe calling production
`AcceptSample` and `StepRemote`. Rebuilt and ran `NetPlayerSync` and
`PlayerAimGameCode` under the test Wine prefix. Both passed; neither skipped.
The latter uses the installed EDF.dll. No stress tests or MSVC tests ran.

`[C]` means confirmed in code or the stated fixture. `[A]` means a material
assumption remains. Neither means the user's live symptom has been reproduced.

## Critical

None established.

## Major

### 1. [C] The braking exception can still wind up after movement becomes blocked

Location: `game/netplayer.cpp:228-244`, `tests/game/netplayer_test.cpp:430-505`.

The earlier collision fix limits how much requested correction is treated as
applied. However, `prior * a < 0` bypasses that limit and subtracts the entire
request. This preserves real braking, but the prior velocity is itself an
estimate. When motion stops against a constraint, that estimate can keep the
exception active indefinitely.

Failure scenario: a copy moves upward, then rests on a surface 5 cm above the
sender's position. Its estimated upward velocity makes the downward correction
look like braking. With zero observed movement, subtracting the unapplied
downward request produces a fictitious upward velocity. The next request grows.

The production-code probe below reproduces this with default parameters, 60 Hz
steps and lossless 100 ms samples. The copy is stationary at y=1.05 from frame 60.
At frame 120 its estimated upward speed is 10.94 m/s; at frame 420 it is
59.30 m/s. By frame 480 the requested downward displacement is 1 m per frame
and the estimated speed is 60 m/s. No warp occurs. A control run starting and
remaining stationary above a 5 cm offset floor stays at zero estimated speed
and requests only 0.0094 m per frame.

The simulated support surface and its transition are fixture assumptions.
This proves feedback windup, not visible bouncing in Havok or its occurrence
in the reported session. Existing tests cover a stationary blocked copy and a
moving copy whose brake is applied, but not the transition between them.

Fix shape: make the braking estimate lose authority when its supporting motion
disappears. Preserve the real-braking regression while adding this transition
case. Do not simply delete the exception, since that breaks the reason it was
introduced. Live traces should distinguish a stationary proxy with growing
`own` and `add` from changing received positions.

Reproduction, compiled against `game/netplayer.cpp` with C++20 and `-Igame`:

```cpp
#include "netplayer.h"
#include <algorithm>
#include <cstdio>
using namespace multislot;
int main() {
    NetPlayerParams params;
    RemoteTrack track;
    Vec3 copy{0, 0.05f, 0};
    for (int frame = 0; frame <= 600; ++frame) {
        const double now = frame * 1000.0 / 60.0;
        if (frame % 6 == 0) {
            PlayerSample sample;
            sample.seq = static_cast<std::uint16_t>(frame / 6 + 1);
            sample.senderMs = static_cast<std::uint32_t>(now);
            sample.position.y = std::min(frame, 60) / 60.0f;
            sample.velocity.y = frame < 60 ? 1.0f : 0.0f;
            AcceptSample(track, sample, now, params);
        }
        const auto step = StepRemote(track, copy, now, params);
        if (frame % 60 == 0)
            std::printf("%d y=%.4f own=%.4f add=%.4f warp=%d\n",
                        frame, copy.y, track.ownVelocity.y, step.add.y, step.warp);
        copy = step.warp ? step.target
                        : copy + step.add + Vec3{0, frame < 60 ? 1.0f / 60 : 0, 0};
        copy.y = std::max(copy.y, frame >= 59 ? 1.05f : 0.05f);
    }
}
```

### 2. [A] A stale record reaching the hook can rewind aim while position stays current

Location: `game/netplayer_game.cpp:344-363`, `game/netplayer.cpp:141-143`,
`src/reliable.cpp:223-242`, `tests/game/aimtrace_test.cpp:85-105`.

`ReadHook` calls the native reader before inspecting the custom sequence.
EDF+5921BC..5921F8 applies the record's aim fields and clears the aim accumulator.
`AcceptSample` subsequently rejects an older position sample without restoring
the aim. The direct reliable receiver deliberately delivers unordered packets
as they arrive; EOS traffic is also upgraded to reliable unordered.

Failure scenario: a delayed record from before a turn arrives after a newer
record. If the game's enclosing replication code dispatches both to the hook,
the delayed aim replaces the newer aim while our position predictor retains
the newer position. The camera filter then follows the wrong angle until a
later aim update arrives. Loss recovery can produce such delayed packets without
losing data permanently.

The hook behavior is established, and the existing native-reader test exercises
a stale record. Whether the enclosing game receiver suppresses these records
in a real room remains unverified. This is a candidate, not a confirmed live
ordering bug. It also cannot explain jitter in a session with no relevant
reordering.

Fix shape: first correlate `receive-stale` with the turn and matching sender
sequence, or exercise reordered packets through the full game receiver. If
confirmed, give aim an explicit freshness rule. Do not skip the entire native
record, which also carries other state, or change all traffic to ordered as a
speculative remedy.

## Minor

No additional findings raised.

## Cleared

- The new camera filter takes the shortest wrapped yaw difference. Crossing
  positive/negative pi does not itself cause a full turn.
- Sender yaw includes the native offset once. `PlayerAimGameCode` verifies
  pitch and yaw round trips through the real writer and reader.
- Position rejects stale sequences while the track is fresh; the aim concern
  is specifically outside that protection.
- The old four-entry track capacity is already eight, with room membership
  changes clearing the tables. The previous review's capacity finding is not
  a new explanation here.
- The existing blocked-from-rest and applied-braking regressions pass. Their
  success does not cover finding 1's transition.

## Architecture notes

Position and aim still have different treatment. The custom block contains
position and velocity, not aim. Ordinary native aim updates occur every sixth
sync frame on foot, with special-state exceptions described in
`docs/net-re/player.md`. The new locked camera smooths the received angle with
a 50 ms time constant; it neither supplies missing angles nor smooths the
remote soldier's aim. During a vehicle ride it follows the seated matrix,
which need not match the gun direction.

The previously captured 30 Hz writes versus 10 Hz accepted position samples
remain unexplained. A roughly 100 ms backward-looking velocity estimate also
lags stops and reversals, and extrapolation continues between arrivals. This
can contribute overshoot without packet loss, but is not a new confirmed bug.
Check actual sample cadence before changing the velocity window or send rate.

The next movement capture should prioritize the transition into blocked motion
and compare `proxy`, `moved`, `prevAdd`, `own` and `add` while the sample stays
still. For aim, compare sender yaw plus offset with receive yaw, then post-update
yaw and spectator facing. This separates stale transmission, reordered records
and a later local pose change. Projectile direction remains a separate question.
