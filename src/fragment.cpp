#include "fragment.h"

#include <algorithm>
#include <cstring>

namespace dn {
namespace {

constexpr uint8_t kMagic[4] = {'E', 'D', 'F', 'G'};
constexpr uint8_t kVersion = 3;

template <typename T>
void put(uint8_t* at, T value) {
    memcpy(at, &value, sizeof(value));
}
template <typename T>
T get(const uint8_t* at) {
    T value{};
    memcpy(&value, at, sizeof(value));
    return value;
}

size_t fragmentsFor(size_t total) { return (total + kFragmentPayload - 1) / kFragmentPayload; }

}  // namespace

std::vector<std::vector<uint8_t>> splitIntoFragments(uint64_t id, uint16_t channel, const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> out;
    if (!data || size == 0 || size > kMaxFragmentedBytes) return out;
    const size_t count = fragmentsFor(size);
    for (size_t i = 0; i < count; ++i) {
        const size_t at = i * kFragmentPayload;
        const size_t n = std::min(kFragmentPayload, size - at);
        std::vector<uint8_t>& f = out.emplace_back(kFragmentHeader + n);
        memcpy(f.data(), kMagic, 4);
        f[4] = kVersion;
        put<uint16_t>(f.data() + 6, channel);
        put<uint64_t>(f.data() + 8, id);
        put<uint32_t>(f.data() + 16, static_cast<uint32_t>(size));
        put<uint16_t>(f.data() + 20, static_cast<uint16_t>(i));
        put<uint16_t>(f.data() + 22, static_cast<uint16_t>(count));
        memcpy(f.data() + kFragmentHeader, data + at, n);
    }
    return out;
}

bool isFragment(const uint8_t* data, size_t size) {
    return data && size >= kFragmentHeader && memcmp(data, kMagic, 4) == 0;
}

void Reassembler::drop(Map::iterator it) {
    buffered_ -= it->second.bytes.size();
    partial_.erase(it);
    ++dropped_;
}

void Reassembler::expire(uint64_t nowMs) {
    for (auto it = partial_.begin(); it != partial_.end();) {
        auto next = std::next(it);
        if (nowMs > it->second.lastMs + kTimeoutMs) drop(it);  // two threads' clocks may be read out of order
        it = next;
    }
}

std::optional<FragmentMessage> Reassembler::add(const std::string& src, const uint8_t* data, size_t size,
                                                uint64_t nowMs) {
    expire(nowMs);
    while (!completedOrder_.empty() &&
           (completedOrder_.size() > kCompletedKept || nowMs > completedOrder_.front().second + kCompletedMs)) {
        completed_.erase(completedOrder_.front().first);
        completedOrder_.pop_front();
    }
    if (!isFragment(data, size) || data[4] != kVersion || data[5] != 0) {
        ++malformed_;
        return std::nullopt;
    }
    const uint16_t channel = get<uint16_t>(data + 6);
    const uint64_t id = get<uint64_t>(data + 8);
    const uint32_t total = get<uint32_t>(data + 16);
    const uint16_t index = get<uint16_t>(data + 20);
    const uint16_t count = get<uint16_t>(data + 22);
    const size_t n = size - kFragmentHeader;
    // What a valid sender writes: the count its total needs, a full payload in all but the last fragment.
    const bool valid = total > 0 && total <= kMaxFragmentedBytes && count == fragmentsFor(total) && index < count &&
                       n == std::min<size_t>(kFragmentPayload, total - size_t{index} * kFragmentPayload);
    if (!valid) {
        ++malformed_;
        return std::nullopt;
    }
    const auto key = std::make_pair(src, id);
    if (completed_.count(key)) return std::nullopt;  // arrived whole already: a copy
    auto it = partial_.find(key);
    if (it == partial_.end()) {
        size_t mine = 0;
        auto oldest = partial_.end();
        for (auto p = partial_.lower_bound({src, 0}); p != partial_.end() && p->first.first == src; ++p) {
            ++mine;
            if (oldest == partial_.end() || p->second.firstMs < oldest->second.firstMs) oldest = p;
        }
        if (mine >= kMaxPartialPerSender && oldest != partial_.end()) drop(oldest);
        // Room for the whole message, the oldest message of anyone making way.
        while (buffered_ + total > kMaxBuffered && !partial_.empty()) {
            auto first = std::min_element(partial_.begin(), partial_.end(),
                                          [](const auto& a, const auto& b) { return a.second.firstMs < b.second.firstMs; });
            drop(first);
        }
        Partial p;
        p.channel = channel;
        p.total = total;
        p.count = count;
        p.firstMs = nowMs;
        p.lastMs = nowMs;
        p.got.assign(count, false);
        p.bytes.assign(total, 0);
        buffered_ += total;
        it = partial_.emplace(key, std::move(p)).first;
    }
    Partial& p = it->second;
    if (p.total != total || p.count != count || p.channel != channel) {
        ++malformed_;  // another message under the same id: the sender's ids wrapped, or it lies
        return std::nullopt;
    }
    if (p.got[index]) return std::nullopt;  // a copy (resent, or sent over two paths)
    p.got[index] = true;
    ++p.have;
    p.lastMs = nowMs;
    memcpy(p.bytes.data() + size_t{index} * kFragmentPayload, data + kFragmentHeader, n);
    if (p.have < p.count) return std::nullopt;
    FragmentMessage done{p.channel, std::move(p.bytes)};
    buffered_ -= total;
    partial_.erase(it);
    completed_.insert(key);
    completedOrder_.emplace_back(key, nowMs);
    return done;
}

void Reassembler::clear() {
    partial_.clear();
    completed_.clear();
    completedOrder_.clear();
    buffered_ = 0;
}

}  // namespace dn
