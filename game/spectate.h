#pragma once
#include <cstddef>
#include <cstdint>

#include "patches.h"

namespace multislot::spectate {

// A downed player in an online room watches a standing teammate through the game's follow camera until revived,
// aimed with their own look controls (docs/spectate/design.md; the evidence for every address is in
// docs/spectate/research.md). Only this machine's view changes: nothing goes over the network.
//
// Application::Update calls System::Update (1198DA0) here once a frame, on the main thread, before the frame's
// cameras are drawn. With [Display] SpectateWhenDown=1 (the default) the call comes to the plugin, which switches
// view 0's camera before calling the game's System::Update.
CallSite UpdateCall();

// A remote player in an online room, by the slot the game numbers players with (0 to 3).
struct Teammate {
    std::uint32_t slot;
    bool alive;
};

// No teammate to follow: view 0 shows the local player's own camera.
constexpr std::uint32_t kOwnCamera = 0xFFFFFFFF;

// Who a downed local player watches. `mates` are in slot order; `followed` is the slot followed now, or kOwnCamera.
// Keeps `followed` while that teammate lives, else takes the next living teammate after it in slot order (wrapping),
// else kOwnCamera. `advance` (the W key) moves on from a living `followed` the same way; with one teammate
// standing it changes nothing.
std::uint32_t NextTarget(const Teammate* mates, std::size_t count, std::uint32_t followed, bool advance);

// Makes `camera`, a SceneCamera some shared_ptr keeps alive, the current camera of `view` through the game's own
// weak_from_this (1198950) and view switch (1193FA0). The view keeps a weak_ptr to it.
void ShowCamera(unsigned char* game, std::uintptr_t view, std::uintptr_t camera);

// Verifies the call and every byte of game code the spectator calls or relies on, then points the call at the
// plugin through a stub page of its own. False (logged, nothing changed) otherwise. `base` is the supported EDF.dll.
bool Install(unsigned char* base);

}  // namespace multislot::spectate
