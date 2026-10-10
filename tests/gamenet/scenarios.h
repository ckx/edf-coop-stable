#pragma once
// The rooms GameNetTests plays (included by gamenet_test.cpp, after Spawned, Check and Result).

struct Seat {
    std::string user;  // EOS ProductUserId text
    std::string role;  // roles.cpp
    std::string ini;   // its EDF6Coop.ini
};

struct Scenario {
    std::string name;
    std::vector<Seat> seats;
    DWORD timeoutMs;  // above the longest any role waits, so a machine reports its own failure first
    void (*check)(const std::vector<Spawned>& machines, const gamenet::Network& network);
    // Imperfections of the network and role settings (net_shared.h: EDF6NET_DELAY, EDF6NET_DROP; roles.cpp:
    // EDF6NET_SECONDS).
    std::vector<std::pair<std::string, std::string>> network = {};
};

// What every machine runs with: the EOS hooks as players have them (Mode=off: no direct link, but the reliable
// upgrade, the disconnect hold and the lobby glue sit on the game's EOS calls).
inline std::string BaseIni(const std::string& extra = "") {
    return "[Log]\r\nCrashLog=0\r\n[DirectNet]\r\nEnabled=1\r\nMode=off\r\nUPnP=0\r\n" + extra;
}

// host0...01 hosts; guest...02 and on join one after another, in that order (roles.cpp: EDF6NET_SEAT), so seat n
// is player n+1.
inline std::vector<Seat> Seats(int count, const std::string& step, const std::string& ini) {
    std::vector<Seat> seats;
    for (int i = 0; i < count; ++i) {
        char user[33];
        std::snprintf(user, sizeof(user), "%s%027d", i ? "guest" : "host0", i + 1);
        seats.push_back({user, std::string(i ? "guest-" : "host-") + step, ini});
    }
    return seats;
}

// A room on the direct link: the host listens on a port of this run, the joiners dial it over loopback.
inline std::vector<Seat> DirectSeats(int count, const std::string& step) {
    auto seats = Seats(count, step, "");
    for (std::size_t i = 0; i < seats.size(); ++i)
        seats[i].ini = "[Log]\r\nCrashLog=0\r\n" +
                       std::string(i ? "[DirectNet]\r\nEnabled=1\r\nMode=join\r\nHostAddress=127.0.0.1:@PORT@\r\nUPnP=0\r\n"
                                       "BindPhysicalInterface=0\r\n"
                                     : "[DirectNet]\r\nEnabled=1\r\nMode=host\r\nListenPort=@PORT@\r\nUPnP=0\r\n"
                                       "BindPhysicalInterface=0\r\nPublicAddress=127.0.0.1:@PORT@\r\n");
    return seats;
}

// Every packet that crossed the fake EOS, in order.
struct WirePacket {
    gamenet::PacketHeader header;
    std::string data;
};
inline std::vector<WirePacket> Wire(const gamenet::Network& network) {
    std::vector<WirePacket> packets;
    for (std::uint64_t at = 0; at + sizeof(gamenet::PacketHeader) <= network.wire.used;) {
        WirePacket packet;
        std::memcpy(&packet.header, network.wire.bytes + at, sizeof(packet.header));
        packet.data.assign(reinterpret_cast<const char*>(network.wire.bytes + at + sizeof(packet.header)),
                           packet.header.size);
        at += sizeof(packet.header) + packet.header.size;
        packets.push_back(std::move(packet));
    }
    return packets;
}

// The netstats role's state datagrams on the wire (89 bytes on channel 0: nothing else is that size).
inline std::vector<WirePacket> StateDatagrams(const gamenet::Network& network) {
    std::vector<WirePacket> states;
    for (auto& packet : Wire(network))
        if (packet.header.channel == 0 && packet.header.size == 89) states.push_back(std::move(packet));
    return states;
}

// EOS refused nothing: no packet above its 1170 bytes, no inbox full.
inline void CheckEosAcceptedAll(const gamenet::Network& network) {
    Check(network.refused == 0, std::to_string(network.refused) + " packet(s) above EOS's 1170 bytes");
    Check(network.overflowed == 0, std::to_string(network.overflowed) + " packet(s) refused for a full inbox");
    Check(network.wire.dropped == 0, "the wire log kept every packet");
}

inline void CheckRoom(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    for (const auto& machine : machines) {
        int seen = 0;
        for (auto it = machine.results.equal_range("member"); it.first != it.second; ++it.first)
            seen += it.first->second.find(" marked") != std::string::npos ? 1 : 0;
        Check(seen == static_cast<int>(machines.size()),
              machine.user + " sees all " + std::to_string(machines.size()) + " members publish their netcode protocol");
        Check(machine.results.count("unimplemented-eos") == 0, machine.user +
                                                                   " reached no EOS function the fake only stubs (first: " +
                                                                   Result(machine, "unimplemented-eos") + ")");
    }
    Check(network.lobby.count == machines.size(), "EOS has every machine in the room");
}

inline void CheckLink(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    for (const auto& machine : machines) {
        int received = 0;
        for (auto it = machine.results.equal_range("received"); it.first != it.second; ++it.first)
            received += it.first->second.find("PROBE-PLAINTEXT-FROM-") != std::string::npos ? 1 : 0;
        Check(received == static_cast<int>(machines.size()) - 1,
              machine.user + " got every other machine's record through the game's controller");
    }
    const auto wire = Wire(network);
    bool plaintext = false;
    for (const auto& packet : wire) plaintext = plaintext || packet.data.find("PROBE-PLAINTEXT") != std::string::npos;
    Check(!wire.empty(), std::to_string(wire.size()) + " packets crossed EOS");
    Check(!plaintext, "no record crossed EOS in plaintext (the game encrypts every datagram)");
    CheckEosAcceptedAll(network);
}

inline void CheckPlayerSync(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    CheckEosAcceptedAll(network);
    for (const auto& machine : machines) {
        Check(machine.text.find("PLAYER sync: hooks installed (20 vtable slots + flush interval)") != std::string::npos,
              machine.user + " installed the player sync hooks against EDF.dll");
        Check(Result(machine, "player-records") == "8 ok 0 bad",
              machine.user + " read every player record back exactly (" + Result(machine, "player-records") + ")");
    }
}

// The last line of `log` that starts with `prefix` ("" when none).
inline std::string LastLine(const std::string& log, const std::string& prefix) {
    std::string found;
    for (std::size_t at = log.find(prefix); at != std::string::npos; at = log.find(prefix, at + 1)) {
        const std::size_t end = log.find('\n', at);
        found = log.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }
    return found;
}

// A member of another netcode protocol ([Test] NetProtocol=99) is refused by the host: removed from the room, both
// logs say why, and the members that stay run the netcode features once it is gone.
inline void CheckVersionGate(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    const auto& host = machines.front();
    const auto& odd = machines.back();
    Check(host.text.find("NETCODE REFUSED " + odd.user) != std::string::npos, "the host refused the member of protocol 99");
    Check(network.lobby.count == machines.size() - 1, "it is out of the room (" + std::to_string(network.lobby.count) + ")");
    Check(odd.text.find("the room's host " + host.user + " runs netcode protocol " + std::to_string(multislot::kNetProtocol) +
                        ", this machine 99") != std::string::npos,
          "the refused member says why its netcode is off");
    for (std::size_t i = 0; i + 1 < machines.size(); ++i) {
        const std::string& log = machines[i].text;
        const std::string last = LastLine(log, "NETCODE features in room");
        Check(last.find(" on: Fragments,PlayerSync,HitAuthority") != std::string::npos,
              machines[i].user + " runs the netcode features once the odd member is gone (" + last + ")");
        Check(log.find(" off: ") != std::string::npos, machines[i].user + " had them off while the odd member was in");
    }
}

// Every "<key> <user> states=<n> events=<n>[ gap=<ms>]" a machine reported (netstats role), by member.
struct Counts {
    std::size_t states = 0, events = 0, gapMs = 0;
};
inline std::map<std::string, Counts> Reported(const Spawned& machine, const std::string& key) {
    std::map<std::string, Counts> counts;
    for (auto it = machine.results.equal_range(key); it.first != it.second; ++it.first) {
        char user[64]{};
        Counts c;
        if (sscanf_s(it.first->second.c_str(), "%63s states=%zu events=%zu gap=%zu", user, static_cast<unsigned>(sizeof(user)),
                     &c.states, &c.events, &c.gapMs) >= 3)
            counts[user] = c;
    }
    return counts;
}

// Every record one machine sent another, counted at both ends. Reliable records arrive exactly once (the game's own
// resends included); state records arrive all (`allStates`, what the reliable upgrade is for) or at least some.
// Returns the state records lost over the whole room.
inline std::size_t CheckEveryRecord(const std::vector<Spawned>& machines, bool allStates, std::size_t maxGapMs = 1000) {
    std::size_t lost = 0;
    for (const auto& to : machines) {
        const auto got = Reported(to, "got-from");
        for (const auto& from : machines) {
            if (&from == &to) continue;
            const auto sent = Reported(from, "sent-to");
            const auto s = sent.find(to.user);
            const auto g = got.find(from.user);
            const std::string pair = from.user + " -> " + to.user + ": ";
            if (s == sent.end() || g == got.end()) {
                Check(false, pair + "no counts reported");
                continue;
            }
            Check(g->second.events == s->second.events, pair + std::to_string(g->second.events) + " of " +
                                                            std::to_string(s->second.events) + " reliable records, each once");
            const std::string states = std::to_string(g->second.states) + " of " + std::to_string(s->second.states) + " state records";
            Check(allStates ? g->second.states == s->second.states : g->second.states > 0,
                  pair + states + (allStates ? ", all of them" : ", some of them"));
            if (allStates)
                Check(g->second.gapMs <= maxGapMs, pair + "state never stopped for more than " + std::to_string(maxGapMs) +
                                                       " ms (longest gap " + std::to_string(g->second.gapMs) + " ms)");
            lost += s->second.states > g->second.states ? s->second.states - g->second.states : 0;
        }
    }
    return lost;
}

// The reliable upgrade: the network loses unreliable packets (EDF6NET_DROP), but the game's state datagrams go to EOS
// reliably, so none is lost and every state record arrives.
inline void CheckNetStats(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    CheckEosAcceptedAll(network);
    CheckEveryRecord(machines, true);
    const auto states = StateDatagrams(network);
    std::size_t unreliable = 0;
    for (const auto& packet : states) unreliable += packet.header.reliability == 0 ? 1 : 0;
    Check(states.size() > 20 && unreliable == 0,
          std::to_string(states.size()) + " state datagrams crossed EOS, " + std::to_string(unreliable) + " of them unreliably");
    Check(network.dropped == 0, std::to_string(network.dropped) + " packets lost on the way");
}

// [Sync] ReliableGameTraffic=0 under the same loss: the state datagrams go unreliably and some are lost for good (the
// game does not resend them). What the netstats scenario guards against.
inline void CheckNetStatsUnreliable(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    const std::size_t lost = CheckEveryRecord(machines, false);
    Check(network.dropped > 0, std::to_string(network.dropped) + " packets lost on the way");
    Check(lost > 0, std::to_string(lost) + " state records never arrived");
}

// The direct link (Mode=host, joiners Mode=join to it over loopback): every joiner connects to the host and the
// game's records go over it, none lost.
inline void CheckDirect(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    CheckEveryRecord(machines, true);
    for (std::size_t i = 1; i < machines.size(); ++i)
        Check(machines[i].text.find("DIRECT connected to host") != std::string::npos, machines[i].user + " connected to the host");
    const std::string clients = LastLine(machines.front().text, "DIRECT client ");
    Check(clients.find("(" + std::to_string(machines.size() - 1) + " direct clients)") != std::string::npos,
          "the host has every joiner as a direct client (" + clients + ")");
    std::printf("INFO: %zu state datagrams crossed EOS instead of the direct link\n", StateDatagrams(network).size());
}

// The direct link of the first guest loses everything it sends for 2.5 s mid-game: both ends stop routing over it
// within about 1.5 s, what it had not delivered goes over EOS, it carries again once it answers, and every record
// still arrives exactly once.
inline void CheckDirectOutage(const std::vector<Spawned>& machines, const gamenet::Network& network) {
    CheckRoom(machines, network);
    // State stops for as long as the link takes to count as not answering (direct_net.cpp kAnswerMs) and a round
    // of the role's sends.
    CheckEveryRecord(machines, true, 2000);
    for (std::size_t i : {std::size_t{0}, std::size_t{1}}) {
        const std::string& log = machines[i].text;
        Check(log.find("has not answered for") != std::string::npos && log.find("answers again") != std::string::npos,
              machines[i].user + " stopped routing over the link and took it up again");
    }
    Check(machines[1].text.find("had not delivered") != std::string::npos,
          machines[1].user + " handed what its link had not delivered on");
}

inline const std::vector<Scenario>& Scenarios() {
    static const std::vector<Scenario> all = {
        {"room", Seats(2, "room", BaseIni()), 60000, &CheckRoom},
        {"link", Seats(2, "link", BaseIni()), 90000, &CheckLink},
        {"playersync", Seats(2, "playersync", BaseIni()), 90000, &CheckPlayerSync},
        {"versiongate",
         [] {
             auto seats = Seats(3, "versiongate", BaseIni());
             seats[2].ini = BaseIni("[Test]\r\nNetProtocol=99\r\n");
             return seats;
         }(),
         90000, &CheckVersionGate},
        // Each machine loses its first 40 unreliable packets of 80 bytes or more: about the first 40 state datagrams.
        {"netstats", Seats(3, "netstats", BaseIni()), 90000, &CheckNetStats, {{"EDF6NET_SECONDS", "6"}, {"EDF6NET_DROP", "80:40"}}},
        {"netstatsunreliable", Seats(3, "netstats", BaseIni("[Sync]\r\nReliableGameTraffic=0\r\n")), 90000,
         &CheckNetStatsUnreliable, {{"EDF6NET_SECONDS", "6"}, {"EDF6NET_DROP", "80:40"}}},
        {"direct", DirectSeats(3, "netstats"), 120000, &CheckDirect, {{"EDF6NET_SECONDS", "8"}}},
        {"directoutage",
         [] {
             auto seats = DirectSeats(3, "netstats");
             seats[1].ini += "[Test]\r\nDirectOutageAfterMs=6000\r\nDirectOutageForMs=2500\r\n";
             return seats;
         }(),
         120000, &CheckDirectOutage, {{"EDF6NET_SECONDS", "10"}}},
    };
    return all;
}

inline const Scenario* FindScenario(const std::string& name) {
    for (const auto& scenario : Scenarios())
        if (scenario.name == name) return &scenario;
    return nullptr;
}
