#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "midhook.h"

namespace multislot {

// EDF.dll build every site below was taken from (Steam, 2025-01 update).
constexpr std::uint32_t kImageTimeDateStamp = 0x678CCB46;
constexpr std::uint32_t kImageSize = 0x22CE000;

// Bytes replaced at a fixed RVA. `original` is verified before anything is written.
struct Patch {
    const char* name;
    std::uint32_t rva;
    std::vector<std::uint8_t> original;
    std::vector<std::uint8_t> replacement;
};

// A jump to a mid-function hook thunk (midhook.h) replaces `original` at `rva` (verified first). The thunk
// calls a handler and then runs original[displacedOffset, +displacedSize) before returning to rva +
// original.size(). Displaced bytes must be position-independent.
struct MidSite {
    const char* name;
    std::uint32_t rva;
    std::vector<std::uint8_t> original;
    std::size_t displacedOffset;
    std::size_t displacedSize;
};

// A `call rel32` whose target is verified, then pointed at a plugin function.
struct CallSite {
    const char* name;
    std::uint32_t rva;
    std::uint32_t target;
};

// An established member stays in the room when a newcomer's P2P handshake with it times out (peertimeout.h):
// Users::Add records when each user joined, and the room update's "a link timed out: leave" call asks first.
std::vector<MidSite> PeerTimeoutHooks();
std::vector<CallSite> PeerTimeoutCalls();
// Weapon IDs of other players that this machine's weapon table does not have (weaponguard.h): the room info
// and the mission start records.
std::vector<MidSite> WeaponGuardHooks();

bool Matches(const std::uint8_t* at, const Patch& patch);
bool CallTargets(const std::uint8_t* at, std::uint32_t siteRva, std::uint32_t targetRva);
bool SlotTargets(const std::uint8_t* at, std::uint64_t imageBase, std::uint32_t targetRva);

}  // namespace multislot
