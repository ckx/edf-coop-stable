#pragma once
// The netcode protocol number on its own, so that code which must not pull in the gate (the game-code
// tests' driver, GameNetTests) can name the same value the plugin publishes (netfeature.h).
#include <cstdint>

namespace multislot {

// Bumped whenever a message, a wire format or a game rule of the netcode changes in a way an older build would read
// differently. Builds of different protocols never run new netcode together.
//   1: the first gate, transport classes, mesh, fragments, compression.
//   2: hit events carry their sender, enemy dice carry their sender, fragment ids carry the sender's epoch.
//   3: this fork: no traffic classes, mesh, compression, world authority or bulk messages; rooms of four.
constexpr std::int64_t kNetProtocol = 3;

}  // namespace multislot
