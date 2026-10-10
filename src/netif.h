#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dn {

struct PhysicalInterface {
    std::string name;
    std::string description;
    uint32_t ifIndexV4 = 0;
    uint32_t ifIndexV6 = 0;
    std::string ipv4;                      // first IPv4 unicast address (LAN address behind the router)
    std::vector<std::string> globalIpv6;   // public IPv6 addresses, stable ones first
};

// The adapter that owns the real default gateway, ignoring VPN/TUN/virtual adapters.
std::optional<PhysicalInterface> findPhysicalInterface();

// The local address this machine sends from towards the internet (AF_INET or AF_INET6), as the routing table picks
// it: a UDP socket connected to a public address, which sends nothing. Needs no adapter to look physical (PPPoE,
// mobile broadband, Wine's adapter list). "" without a route.
std::string egressAddress(int family);
// True for a global unicast IPv6 address (2000::/3).
bool isGlobalIpv6Text(const std::string& address);

}  // namespace dn
