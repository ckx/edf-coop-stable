// Who a downed player watches (NextTarget). Defects this catches: following a teammate who is down too, leaving a
// living teammate for another every frame, not wrapping past the last slot, keeping a teammate's camera when
// nobody is left standing, and a cycle press that skips a standing teammate or leaves the only one standing.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../game/spectate.h"

using namespace multislot::spectate;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Exercise the production soldier reader and filter. A lost update must settle without extrapolation;
// crossing +/-pi must not spin the long way, and entering a vehicle must use its moving seat transform.
void CheckLockedAim() {
    alignas(16) unsigned char soldier[0x1560]{};
    const auto object = reinterpret_cast<std::uintptr_t>(soldier);
    auto put = [&](std::size_t offset, auto value) { std::memcpy(soldier + offset, &value, sizeof(value)); };
    auto aim = [&](float pitch, float yaw) { put(0x1230, pitch); put(0x1234, yaw); };
    LockedAim state;
    aim(0.2f, 3.1f);
    Check(state.Update(object, 1, 0.016f) && state.look[1] == 3.1f, "new target starts at its own aim");
    aim(0.6f, -3.1f);
    state.Update(object, 1, 0.025f);
    Check(state.look[1] > 3.1f && state.look[1] < 3.142f, "wrapped yaw takes the short arc across pi");
    Check(state.look[0] > 0.2f && state.look[0] < 0.6f, "new aim is smoothed rather than stepped");
    for (int i = 0; i < 120; ++i) state.Update(object, 1, 1.0f / 60.0f);
    Check(std::abs(state.look[1] + 3.1f) < 0.00001f && std::abs(state.look[0] - 0.6f) < 0.00001f,
          "unchanged or lost updates settle at the latest aim without overshoot");

    LockedAim fast, slow;
    aim(0, 0);
    fast.Update(object, 1, 0); slow.Update(object, 1, 0);
    aim(1, 1);
    for (int i = 0; i < 12; ++i) fast.Update(object, 1, 1.0f / 120.0f);
    for (int i = 0; i < 3; ++i) slow.Update(object, 1, 1.0f / 30.0f);
    Check(std::abs(fast.look[1] - slow.look[1]) < 0.00001f, "smoothing duration does not depend on frame rate");
    state.Update(object, 2, 0.001f);
    Check(state.look[0] == 1 && state.look[1] == 1, "cycling targets discards the old target's smoothing");
    aim(0, 100000.0f);
    state.Update(object, 3, 0);
    Check(std::abs(state.look[1]) <= 3.142f, "unbounded received yaw is reduced before interpolation");

    alignas(8) unsigned char control[16]{};
    std::int32_t uses = 1;
    std::memcpy(control + 8, &uses, sizeof(uses));
    put(0x1550, reinterpret_cast<std::uintptr_t>(control));
    put(0x80, 1.0f); put(0x88, 0.0f);
    state.Update(object, 3, 0.016f);
    Check(state.look[0] == 0 && std::abs(state.look[1] - 1.5707963f) < 0.00001f,
          "boarding takes the seated matrix heading with zero pitch despite stale received aim");
    put(0x80, 0.0f); put(0x88, -1.0f);
    state.Update(object, 3, 0.025f);
    Check(state.look[1] > 1.5707963f && state.look[1] < 3.142f, "vehicle turning changes the locked heading");
    uses = 0;
    std::memcpy(control + 8, &uses, sizeof(uses));
    aim(0.4f, -0.5f);
    state.Update(object, 3, 0.016f);
    Check(state.look[0] == 0.4f && state.look[1] == -0.5f, "expired vehicle reference returns to on-foot aim");
    aim(0, std::numeric_limits<float>::quiet_NaN());
    Check(!state.Update(object, 3, 0.016f), "nonfinite peer aim never reaches the orbit parameters");
    aim(0, 2);
    Check(state.Update(object, 3, 0.016f) && state.look[1] == 2, "valid aim recovers without poisoned filter state");
    const float before = state.look[1];
    aim(0, 1);
    state.Update(object, 3, std::numeric_limits<float>::quiet_NaN());
    Check(state.look[1] == before, "invalid elapsed time cannot poison the filter");
    uses = 1;
    std::memcpy(control + 8, &uses, sizeof(uses));
    put(0x80, 0.0f); put(0x88, 0.0f);
    Check(!state.Update(object, 3, 0.016f), "a vertical or degenerate forward vector has no heading");
}

}  // namespace

int main() {
    CheckLockedAim();
    const Teammate all[] = {{1, true}, {2, true}, {3, true}};
    Check(NextTarget(all, 3, kOwnCamera, false) == 1, "from the player's own camera: the first living teammate");
    Check(NextTarget(all, 3, 2, false) == 2, "a living teammate stays followed");

    const Teammate secondDown[] = {{1, true}, {2, false}, {3, true}};
    Check(NextTarget(secondDown, 3, 2, false) == 3, "a teammate who goes down hands over to the next living one");
    const Teammate lastDown[] = {{1, true}, {2, true}, {3, false}};
    Check(NextTarget(lastDown, 3, 3, false) == 1, "past the last slot it wraps to the first");
    const Teammate leftRoom[] = {{1, false}, {3, true}};
    Check(NextTarget(leftRoom, 2, 2, false) == 3, "a teammate who left the room hands over to the next slot");

    const Teammate allDown[] = {{1, false}, {2, false}, {3, false}};
    Check(NextTarget(allDown, 3, 2, false) == kOwnCamera, "with every teammate down: the player's own camera");
    Check(NextTarget(nullptr, 0, kOwnCamera, false) == kOwnCamera, "alone: the player's own camera");

    // The W key.
    Check(NextTarget(all, 3, 2, true) == 3, "a press moves on to the next standing teammate");
    Check(NextTarget(all, 3, 3, true) == 1, "a press past the last slot wraps to the first");
    Check(NextTarget(secondDown, 3, 1, true) == 3, "a press skips a teammate who is down");
    const Teammate oneStanding[] = {{1, false}, {2, true}, {3, false}};
    Check(NextTarget(oneStanding, 3, 2, true) == 2, "a press with one teammate standing keeps them");
    Check(NextTarget(all, 3, kOwnCamera, true) == 1, "a press from the player's own camera: the first living teammate");
    Check(NextTarget(allDown, 3, 2, true) == kOwnCamera, "a press with every teammate down: the player's own camera");

    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
