// The netcode version gate (netfeature.h): which features are on, from what every member publishes.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/netfeature.h"

using namespace multislot;

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

const std::string kHost = "host0000000000000000000000000001";
const std::string kGuest = "guest000000000000000000000000002";
const std::string kOther = "guest000000000000000000000000003";

LobbyView::Member Member(const std::string& id, const char* protocol, const char* caps) {
    LobbyView::Member m;
    m.id = id;
    if (protocol) m.texts[kNetProtocolKey] = protocol;
    if (caps) m.texts[kNetCapsKey] = caps;
    return m;
}

LobbyView View(const std::string& self, std::vector<LobbyView::Member> members, const char* roomCaps = "FFFFFFFF") {
    LobbyView view;
    view.lobbyId = "lobby1";
    view.self = self;
    view.owner = kHost;
    view.members = std::move(members);
    if (roomCaps) view.lobbyTexts[kNetRoomCapsKey] = roomCaps;  // what the host says the whole room runs
    return view;
}


const std::uint32_t kHit = static_cast<std::uint32_t>(NetFeature::HitAuthority);
const std::uint32_t kFragments = static_cast<std::uint32_t>(NetFeature::Fragments);
const std::string kProto = std::to_string(kNetProtocol);

// Everyone decides on one set of members: a guest takes only what the host says the whole room runs.
void TestHostSaysWhatTheRoomRuns() {
    NetRoom room;
    const auto members = std::vector<LobbyView::Member>{Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304")};
    room.Observe(View(kGuest, members, nullptr));
    Check(!room.Active(kHit), "until the host said what the room runs, a guest runs nothing");
    room.Observe(View(kGuest, members, "104"));  // the host says the room lacks HitAuthority
    Check(!room.Active(kHit) && room.Active(kFragments), "a feature the host says the room lacks is off at the guest too");
    Check(room.WhyOff(kHit).find("the room's host has not said") != std::string::npos, "and the log says why");
    room.Observe(View(kHost, members, nullptr));  // the host itself goes by what it sees
    Check(room.Active(kHit) && room.ActiveMask() == 0x304, "the host decides on what it sees");
}

void TestRoomAgrees() {
    NetRoom room;
    Check(!room.Active(kHit), "outside a room nothing is on");
    room.Observe(View(kGuest, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304")}));
    Check(room.InRoom() && !room.Owner(), "a guest in the room");
    Check(room.Active(kHit) && room.Active(kHit | kFragments), "everyone runs protocol and features: on");
    // A member that turned a feature off keeps that one off for the room, the others stay on.
    room.Observe(View(kGuest, {Member(kHost, kProto.c_str(), "300"), Member(kGuest, kProto.c_str(), "304")}));
    Check(room.Active(kHit) && !room.Active(kFragments), "a feature off at one member is off for the room");
    Check(room.WhyOff(kFragments).find(kHost) != std::string::npos, "the log names who keeps it off");
}

void TestUnpublishedMember() {
    NetRoom room;
    // The game as it ships (or an older EDF6Coop): no attributes. Not refused - everyone plays the old netcode.
    const auto fresh = room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, nullptr, nullptr)}));
    Check(fresh.empty(), "a member without the attributes is not refused");
    Check(!room.Active(kHit), "a member without the attributes turns every feature off");
    Check(room.WhyOff(kHit).find("publishes no netcode protocol") != std::string::npos, "and the log says why");
    // Its attributes arrive (Epic relays them late): on.
    room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304")}));
    Check(room.Active(kHit), "once its attributes arrive the features come on");
}

void TestMismatchedProtocol() {
    NetRoom room;
    const std::string other = std::to_string(kNetProtocol + 1);
    // Not yet: it has not shown that it read our protocol (and said why on its side) - the host waits for that.
    auto fresh = room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304"),
                                           Member(kOther, other.c_str(), "304")}));
    Check(fresh.empty() && !room.Active(kHit), "a member of another protocol is refused only once it read ours");
    auto seenOther = Member(kOther, other.c_str(), "304");
    seenOther.texts[kNetSeenKey] = kProto;
    fresh = room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304"), seenOther}));
    Check(room.Owner(), "the host owns the room");
    Check(fresh.size() == 1 && fresh[0].id == kOther && fresh[0].protocol == kNetProtocol + 1,
          "a member of another protocol is reported to refuse");
    Check(!room.Active(kHit), "while it is in the room nothing is on");
    fresh = room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304"), seenOther}));
    Check(fresh.empty(), "reported once per room");
    // It was removed: on again.
    room.Observe(View(kHost, {Member(kHost, kProto.c_str(), "304"), Member(kGuest, kProto.c_str(), "304")}));
    Check(room.Active(kHit), "once it is gone the features come back");
    // Left the room: off, and a new room reports again.
    room.Observe(LobbyView{});
    Check(!room.InRoom() && !room.Active(kHit), "out of the room: off");
    auto view = View(kHost, {Member(kHost, kProto.c_str(), "304"), seenOther});
    view.lobbyId = "lobby2";
    Check(room.Observe(view).size() == 1, "another room reports it again");
}

void TestSelfListed() {
    NetRoom room;
    room.Observe(View(kGuest, {Member(kHost, kProto.c_str(), "304")}));
    Check(!room.Active(kHit), "until our own entry is listed the room is not known");
}

void TestCaps() {
    Check(ParseCaps("304") == 0x304 && ParseCaps("700") == 0x700 && ParseCaps("") == 0 && ParseCaps("x1") == 0, "caps parse as hex");
    Check(FormatCaps(0x304) == "Fragments,PlayerSync,HitAuthority" && FormatCaps(0) == "none" &&
              FormatCaps(0x10004) == "Fragments,0x10000",
          "caps format by name");
    Check(!NetFeatureActive(NetFeature::HitAuthority), "not started: NetFeatureActive is false");
}

}  // namespace

int main() {
    TestHostSaysWhatTheRoomRuns();
    TestRoomAgrees();
    TestUnpublishedMember();
    TestMismatchedProtocol();
    TestSelfListed();
    TestCaps();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 && checks > 0 ? 0 : 1;
}
