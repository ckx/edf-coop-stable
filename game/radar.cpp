#include "radar.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "code.h"
#include "guard.h"
#include "log.h"

namespace multislot {
namespace {

constexpr std::uint32_t kDrawDots = 0x82A890;
constexpr std::uint32_t kDropItemManager = 0x20B2988;  // DropItemManager* (its constructor is 2C68D0)
constexpr std::uint32_t kItemListOffset = 0xDE0;       // std::list sentinel; the radar walks it at 82B98C
constexpr std::uint32_t kNodeUnitOffset = 0x18;        // list node -> item unit (82B9A9)
// Set by the unit's init (2C83D1) and indexing ItemModel.mrab's models in the order the constructor loads them
// (Weapon, Armor, Healingsmall, Healingbig); the pickup (2C7540) adds a weapon or an armor or heals 15% / 30%.
constexpr std::uint32_t kUnitTypeOffset = 0xC0;
enum ItemType : std::uint32_t { kWeapon = 0, kArmor = 1, kHealSmall = 2, kHealBig = 3 };

// The vector 82A890 reads: positions at +8 and their count at +0x18 (82A910, 82A989).
struct Position {
    alignas(16) float xyzw[4];
};
struct Positions {
    void* unused;
    const Position* data;
    std::size_t capacity;
    std::size_t size;
};
using DrawDotsFn = void(__fastcall*)(void* radar, void* rdx, const Positions* dots, void* r9, const float* color,
                                     void* sixth);

alignas(16) constexpr float kRed[4] = {1.0f, 0.0f, 0.0f, 1.0f};
alignas(16) constexpr float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};

unsigned char* game = nullptr;

// The game filled `dots` from the item list just before the call, one position per node in list order, so node i is
// the item at dots[i]. Anything else (a fault, a list of another length) and the types are not used.
bool ReadItemTypes(std::size_t count, std::uint32_t* types) {
    bool matched = false;
    Guarded([&] {
        const auto manager = *reinterpret_cast<const std::uintptr_t*>(game + kDropItemManager);
        if (!manager) return;
        const auto sentinel = *reinterpret_cast<const std::uintptr_t*>(manager + kItemListOffset);
        auto node = *reinterpret_cast<const std::uintptr_t*>(sentinel);
        std::size_t i = 0;
        for (; node != sentinel && i < count; ++i) {
            const auto unit = *reinterpret_cast<const std::uintptr_t*>(node + kNodeUnitOffset);
            types[i] = *reinterpret_cast<const std::uint32_t*>(unit + kUnitTypeOffset);
            node = *reinterpret_cast<const std::uintptr_t*>(node);
        }
        matched = i == count && node == sentinel;
    });
    return matched;
}

const float* ItemColor(std::uint32_t type, const float* gameColor) {
    switch (type) {
    case kArmor: return kRed;
    case kHealSmall:
    case kHealBig: return kWhite;
    default: return gameColor;
    }
}

// Replaces the call at 82BB3D.
void __fastcall DrawItemDots(void* radar, void* rdx, const Positions* dots, void* r9, const float* color, void* sixth) {
    const auto draw = reinterpret_cast<DrawDotsFn>(game + kDrawDots);
    std::vector<std::uint32_t> types(dots->size);
    std::vector<Position> group;
    if (!dots->size || !ReadItemTypes(dots->size, types.data())) {
        draw(radar, rdx, dots, r9, color, sixth);
        return;
    }
    for (const float* pass : {color, kWhite, kRed}) {
        group.clear();
        for (std::size_t i = 0; i < dots->size; ++i)
            if (ItemColor(types[i], color) == pass) group.push_back(dots->data[i]);
        if (group.empty()) continue;
        const Positions list{nullptr, group.data(), group.size(), group.size()};
        draw(radar, rdx, &list, r9, pass, sixth);
    }
}

}  // namespace

CallSite RadarItemDotsCall() { return {"radar item dots", 0x82BB3D, kDrawDots}; }

bool InstallRadarItemColors(unsigned char* base) {
    static ThunkPage page;
    game = base;
    const CallSite site = RadarItemDotsCall();
    if (!CallTargets(base + site.rva, site.rva, site.target)) {
        Log("RADAR: EDF+%X does not call EDF+%X; item dots keep the game's color", site.rva, site.target);
        return false;
    }
    const unsigned char* stub = page.Allocate(base) ? page.Add(reinterpret_cast<void*>(&DrawItemDots)) : nullptr;
    const auto bytes = stub ? CallBytes(base + site.rva, stub) : std::vector<std::uint8_t>{};
    if (bytes.empty() || !page.Seal()) {
        Log("RADAR: no call stub within reach of EDF.dll; item dots keep the game's color");
        page.Release();
        return false;
    }
    if (!WriteCode(base + site.rva, bytes.data(), bytes.size())) {
        Log("RADAR: could not write EDF+%X (error %lu); item dots keep the game's color", site.rva, GetLastError());
        page.Release();
        return false;
    }
    Log("RADAR: item dots by type (armor red, health white, weapons green); call at EDF+%X", site.rva);
    return true;
}

}  // namespace multislot
