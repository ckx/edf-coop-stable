#pragma once
#include <array>
#include <cstddef>

namespace multislot {

// Mission transitions hold two copies of the room's four players. An inactive object yields its slot first.
template <typename Track>
class PlayerTracks {
public:
    Track* Find(const void* soldier, double now, bool create) {
        Entry* oldest = &entries_[0];
        for (auto& entry : entries_) {
            if (entry.soldier == soldier) {
                entry.touched = now;
                return &entry.track;
            }
            if (entry.touched < oldest->touched) oldest = &entry;
        }
        if (!create) return nullptr;
        *oldest = Entry{soldier, now, Track{}};
        return &oldest->track;
    }

private:
    struct Entry {
        const void* soldier = nullptr;
        double touched = -1.0;
        Track track{};
    };
    static constexpr std::size_t kPlayers = 8;
    std::array<Entry, kPlayers> entries_{};
};

}  // namespace multislot
