// Fragments (fragment.h): game datagrams above EOS's 1170 bytes, split and put back together. No game needed.
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "../src/fragment.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                   \
    do {                                                              \
        ++g_checks;                                                   \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                             \
    } while (0)

const std::string kA = "0002bbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const std::string kB = "0002cccccccccccccccccccccccccccc";

void testFragments() {
    printf("fragment: the game's largest datagrams, in any order, copies and all\n");
    std::vector<uint8_t> datagram(1408);
    for (size_t i = 0; i < datagram.size(); ++i) datagram[i] = static_cast<uint8_t>(i * 7);
    auto parts = dn::splitIntoFragments(1, 3, datagram.data(), datagram.size());
    CHECK(parts.size() == 2);
    for (const auto& p : parts) CHECK(p.size() <= dn::kFragmentPacket && dn::isFragment(p.data(), p.size()));
    dn::Reassembler r;
    CHECK(!r.add(kA, parts[1].data(), parts[1].size(), 1000));  // the last one first
    CHECK(!r.add(kA, parts[1].data(), parts[1].size(), 1001));  // and again: a copy
    auto done = r.add(kA, parts[0].data(), parts[0].size(), 1002);
    CHECK(done && done->bytes == datagram && done->channel == 3);
    CHECK(r.buffered() == 0);
    // Delivered once: a copy of a datagram that came whole (sent again over EOS when the direct link stopped
    // answering) gives nothing.
    CHECK(!r.add(kA, parts[0].data(), parts[0].size(), 1003) && !r.add(kA, parts[1].data(), parts[1].size(), 1003));
    CHECK(r.buffered() == 0);
    // A sender back after a restart counts from 1 again under another epoch: its datagram is not taken for the
    // earlier one (which completed within kCompletedMs).
    auto earlier = dn::splitIntoFragments(0xAAAA000000000001ull, 0, datagram.data(), 100);
    auto restarted = dn::splitIntoFragments(0xBBBB000000000001ull, 0, datagram.data() + 1, 100);
    CHECK(r.add(kB, earlier[0].data(), earlier[0].size(), 1006));
    CHECK(r.add(kB, restarted[0].data(), restarted[0].size(), 1007));

    // The largest the game reads, shuffled.
    std::vector<uint8_t> big(4096);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i ^ (i >> 8));
    parts = dn::splitIntoFragments(77, 0, big.data(), big.size());
    CHECK(parts.size() == 4);
    std::optional<dn::FragmentMessage> whole;
    for (size_t i : {2, 0, 3, 1})
        if (auto m = r.add(kB, parts[i].data(), parts[i].size(), 2000 + i)) whole = std::move(m);
    CHECK(whole && whole->bytes == big);
    // Too large, or empty: not split.
    std::vector<uint8_t> huge(dn::kMaxFragmentedBytes + 1);
    CHECK(dn::splitIntoFragments(2, 0, huge.data(), huge.size()).empty());
    CHECK(dn::splitIntoFragments(2, 0, huge.data(), 0).empty());
    // A fragment that lies about its size, a short one, or one claiming more than the game reads: dropped.
    auto bad = dn::splitIntoFragments(3, 0, datagram.data(), datagram.size());
    bad[0].pop_back();
    CHECK(!r.add(kA, bad[0].data(), bad[0].size(), 3000) && r.malformed() == 1);
    auto lying = dn::splitIntoFragments(5, 0, datagram.data(), datagram.size());
    const uint32_t tooBig = dn::kMaxFragmentedBytes + 1;
    memcpy(lying[0].data() + 16, &tooBig, 4);
    CHECK(!r.add(kA, lying[0].data(), lying[0].size(), 3001) && r.malformed() == 2 && r.buffered() == 0);
    // A datagram that never completes is let go after the timeout, and its memory with it.
    parts = dn::splitIntoFragments(4, 0, datagram.data(), datagram.size());
    r.add(kA, parts[0].data(), parts[0].size(), 4000);
    CHECK(r.buffered() == datagram.size());
    r.add(kA, parts[0].data(), 3, 4000 + dn::Reassembler::kTimeoutMs + 1);  // anything, after the timeout
    CHECK(r.buffered() == 0 && r.dropped() == 1);
    // Per sender at most kMaxPartialPerSender datagrams wait: the oldest gives way.
    for (uint32_t id = 10; id < 10 + dn::Reassembler::kMaxPartialPerSender + 1; ++id) {
        parts = dn::splitIntoFragments(id, 0, datagram.data(), datagram.size());
        r.add(kA, parts[0].data(), parts[0].size(), 5000 + id);
    }
    CHECK(r.dropped() == 2 && r.buffered() == dn::Reassembler::kMaxPartialPerSender * datagram.size());
}

void testCompletedForgetsOldestFirst() {
    printf("fragment: the record of completed datagrams forgets the oldest first\n");
    dn::Reassembler r;
    std::vector<uint8_t> small(10, 1);
    auto one = [&](uint64_t id, uint64_t at) {
        auto p = dn::splitIntoFragments(id, 0, small.data(), small.size());
        return r.add(kA, p[0].data(), p[0].size(), at).has_value();
    };
    // Ids that sort before the first one's, completed after it: an eviction by key would drop them, not it.
    CHECK(one(5000, 1));
    for (uint64_t id = 1; id <= dn::Reassembler::kCompletedKept; ++id) CHECK(one(id, 2));
    CHECK(one(5000, 3));   // the oldest was forgotten: a later copy of it counts as new
    CHECK(!one(dn::Reassembler::kCompletedKept, 3));  // the newest is still known
    CHECK(one(5, 3 + dn::Reassembler::kCompletedMs + 1));  // and after kCompletedMs every one is forgotten
}

}  // namespace

int main() {
    testFragments();
    testCompletedForgetsOldestFirst();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 && g_checks > 0 ? 0 : 1;
}
