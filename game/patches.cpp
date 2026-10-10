#include "patches.h"

#include <cstring>

namespace multislot {

std::vector<MidSite> PeerTimeoutHooks() {
    return {
        // Users::Add (12B7F50) right after make_shared<eos::User> (12B7610): `mov r12, [rax]; mov rsi, [rax+8]`
        // takes the new user out of the returned shared_ptr; rbx is still the {ProductUserId, bool remote} argument.
        {"room user joined (join time)", 0x12B80E0, {0x4C, 0x8B, 0x20, 0x48, 0x8B, 0x70, 0x08}, 0, 7},
    };
}

std::vector<CallSite> PeerTimeoutCalls() {
    // Online room update (788460): `if (!IsLocalHost(room) && AnyLinkTimedOut(room))` leaves the room (787090).
    return {{"leave the room when a P2P handshake timed out", 0x788ABF, 0x12BE750}};
}

std::vector<MidSite> WeaponGuardHooks() {
    return {{"weapon guard room info", 0x745636, {0x4D, 0x8D, 0x65, 0x38, 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00}, 0, 4},
            {"weapon guard mission start", 0x790887, {0x4C, 0x69, 0xC1, 0xD4, 0x00, 0x00, 0x00}, 0, 7}};
}

bool Matches(const std::uint8_t* at, const Patch& patch) {
    return at && patch.original.size() == patch.replacement.size() &&
           std::memcmp(at, patch.original.data(), patch.original.size()) == 0;
}

bool SlotTargets(const std::uint8_t* at, std::uint64_t imageBase, std::uint32_t targetRva) {
    if (!at) return false;
    std::uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value == imageBase + targetRva;
}

bool CallTargets(const std::uint8_t* at, std::uint32_t siteRva, std::uint32_t targetRva) {
    if (!at || at[0] != 0xE8) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, at + 1, sizeof(relative));
    return static_cast<std::int64_t>(siteRva) + 5 + relative == static_cast<std::int64_t>(targetRva);
}

}  // namespace multislot
