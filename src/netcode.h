// The transport side of the netcode, as the room part uses it. Implemented in eos_hooks.cpp, which owns the game's
// EOS calls.
#pragma once
#include <cstdint>

namespace dn {

// Capability bits a member publishes with its netcode protocol (multislot netfeature.h is their one list).
enum NetCap : uint32_t {
    kCapFragments = 1u << 2,  // datagrams above EOS's 1170 bytes travel in fragments
};

struct NetcodeOptions {
    bool fragments = true;             // [Netcode] Fragments
    uint32_t statsIntervalMs = 60000;  // [Netcode] StatsSeconds: the STATS line
};
void setNetcodeOptions(const NetcodeOptions& options);
NetcodeOptions netcodeOptions();

// Whether `cap` is on for the whole room (every member publishes it and runs our protocol). Answered by the room
// part (netfeature.cpp); unset: nothing is.
using RoomCapQuery = bool (*)(uint32_t cap);
void setRoomCapQuery(RoomCapQuery query);

// Our netcode protocol and features, said in every direct-link hello; a host refuses another protocol when
// `refuseOthers`.
void setNetcodeIdentity(uint32_t protocol, uint32_t caps, bool refuseOthers);
// Tests only ([Test] LoopbackHosts): a room host may advertise a loopback address (gamenet runs on one machine).
void setTestLoopbackHosts(bool on);

}  // namespace dn
