// Who a downed player watches (NextTarget). Defects this catches: following a teammate who is down too, leaving a
// living teammate for another every frame, not wrapping past the last slot, keeping a teammate's camera when
// nobody is left standing, and a cycle press that skips a standing teammate or leaves the only one standing.
#include <cstdio>

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

}  // namespace

int main() {
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

    // The space bar.
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
