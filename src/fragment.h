// Fragments: one game datagram of up to kMaxFragmentedBytes carried as several packets of at most EOS's 1170 bytes.
//
// EOS refuses any packet above 1170 bytes (EOS_P2P_MAX_PACKET_SIZE) and so does the direct link, while the game
// writes datagrams of up to 1408 bytes (one 1400-byte record, 12D0BE6, with the 8-byte header) and reads up to
// 4096 (12C8D1D): such a datagram never arrived, and the game resent it for ~26 s before it dropped the room. They
// go as fragments here, and only to a receiver that reads them (kCapFragments): any other would hand them to its
// game as datagrams that do not decrypt, which the game drops.
//
// Each fragment:
//   'EDFG' | u8 version (3) | u8 0 | u16 channel | u64 message id | u32 total bytes | u16 index | u16 count | bytes
// Every fragment but the last carries kFragmentPayload bytes. Fragments are sent reliably and unordered (the
// reassembly takes them in any order); a message is complete once every index arrived. Message ids are 64 bits: a
// sender's random epoch (one per process) above a counter, so that a sender back within kCompletedMs after a
// restart (counting from 1 again) never meets the receiver's record of its earlier messages.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dn {

constexpr size_t kFragmentHeader = 24;
constexpr size_t kFragmentPacket = 1170;
constexpr size_t kFragmentPayload = kFragmentPacket - kFragmentHeader;  // 1146
// The game reads datagrams of up to 4096 bytes: four fragments hold one.
constexpr size_t kMaxFragments = 4;
constexpr size_t kMaxFragmentedBytes = kFragmentPayload * kMaxFragments;
// The channel fragments travel on: the game ignores the channel when it receives and sends everything on 0.
constexpr uint8_t kFragmentChannel = 0x4E;

struct FragmentMessage {
    uint16_t channel = 0;
    std::vector<uint8_t> bytes;
};

// The packets for message `id`; empty when the message is empty or larger than kMaxFragmentedBytes.
std::vector<std::vector<uint8_t>> splitIntoFragments(uint64_t id, uint16_t channel, const uint8_t* data, size_t size);
bool isFragment(const uint8_t* data, size_t size);

// Puts messages back together per sender. Bounded: kMaxPartialPerSender messages per sender at a time, kMaxBuffered
// bytes in all, and kTimeoutMs for a message to complete; what does not fit or takes too long is dropped (counted),
// the oldest first. Not thread-safe.
class Reassembler {
public:
    static constexpr size_t kMaxPartialPerSender = 16;
    static constexpr size_t kMaxBuffered = 1u << 20;
    // A message that has made no progress (no fragment it lacked) for this long is let go: its sender gave up, or
    // went.
    static constexpr uint64_t kTimeoutMs = 15000;
    // A fragment of a message completed in the last kCompletedMs (a copy: one sent over the direct link and again
    // over EOS when the link stopped answering) gives nothing. The newest kCompletedKept are remembered.
    static constexpr uint64_t kCompletedMs = 60000;
    static constexpr size_t kCompletedKept = 1024;
    // A fragment from `src`; the message once it is complete, exactly once. Malformed fragments are dropped
    // (counted).
    std::optional<FragmentMessage> add(const std::string& src, const uint8_t* data, size_t size, uint64_t nowMs);
    void clear();
    uint64_t dropped() const { return dropped_; }
    uint64_t malformed() const { return malformed_; }
    size_t buffered() const { return buffered_; }

private:
    using Key = std::pair<std::string, uint64_t>;
    struct Partial {
        uint16_t channel = 0;
        uint32_t total = 0;
        uint16_t count = 0;
        uint16_t have = 0;
        uint64_t firstMs = 0;
        uint64_t lastMs = 0;  // the last fragment it lacked
        std::vector<bool> got;
        std::vector<uint8_t> bytes;
    };
    using Map = std::map<Key, Partial>;
    void expire(uint64_t nowMs);
    void drop(Map::iterator it);
    Map partial_;
    std::set<Key> completed_;
    std::deque<std::pair<Key, uint64_t>> completedOrder_;  // oldest first, with when
    size_t buffered_ = 0;
    uint64_t dropped_ = 0, malformed_ = 0;
};

}  // namespace dn
