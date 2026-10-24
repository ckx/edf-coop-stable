#include "spectate.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "code.h"
#include "guard.h"
#include "log.h"

namespace multislot::spectate {
namespace {

constexpr std::uint32_t kSystemUpdateCall = 0x705715;
constexpr std::uint32_t kSystemUpdate = 0x1198DA0;

constexpr std::uint32_t kGhostCameraFactory = 0x82B40;  // System's factory for CharacterGhostCamera
constexpr std::uint32_t kHealthHudFactory = 0x7FC9A0;  // (SceneCamera*, weak_ptr* out, HUiHudBaseObject::InitParam*)
constexpr std::uint32_t kNamesHudFactory = 0x7FD580;   // (SceneCamera*, weak_ptr* out, HudPlayer::InitParam*)
constexpr std::uint32_t kChatHudFactory = 0x7FCE60;    // (SceneCamera*, weak_ptr* out, HudPlayer::InitParam*)
constexpr std::uint32_t kHealthHudParamVtable = 0x17F6768;
constexpr std::uint32_t kNamesHudParamVtable = 0x17F6738;
constexpr std::uint32_t kHealthHudLayout = 0x17F6790;  // app:/ui/lyt_HudPowerGauge01.sgo
constexpr std::uint32_t kWeakFromThis = 0x1198950;      // (SceneCamera*, weak_ptr* out)
constexpr std::uint32_t kSetViewCamera = 0x1193FA0;     // (View*, weak_ptr<SceneCamera> by value)
constexpr std::uint32_t kIsOnlineMode = 0x1B5DF0;       // the mission script's bool IsOnlineMode()
constexpr std::uint32_t kDynamicCast = 0x12DA7AA;       // __RTDynamicCast
constexpr std::uint32_t kGameObjectBaseType = 0x2006400;  // RTTI type descriptors
constexpr std::uint32_t kSoldierBaseType = 0x2006428;
constexpr std::uint32_t kTeamManager = 0x20B2978;  // TeamManager*
constexpr std::uint32_t kGhostCameraVtable = 0x1768C10;
constexpr std::uint32_t kGhostCameraParamVtable = 0x1762168;

// System: four views of 0x188 bytes from +0x4D0. A view holds its current camera as a weak_ptr at +0x8 and its
// cameras in a std::list<shared_ptr<SceneCamera>> at +0x18 (nodes: next +0, value +0x10). Online, the local
// player's camera is on view 0.
constexpr std::size_t kView0 = 0x4D0;
constexpr std::size_t kViewCurrent = 0x8;
constexpr std::size_t kViewCameras = 0x18;
constexpr std::size_t kNodeValue = 0x10;
// CharacterGhostCamera follows the object in the weak_ptr at +0x350, taken from its InitParam's +0x80.
constexpr std::size_t kCameraTarget = 0x350;
constexpr std::size_t kParamTarget = 0x80;
constexpr std::size_t kParamSize = 0xA0;  // the game's own are 0x88 bytes
// TeamManager +0x38 points at the players' std::map. Tree nodes: left +0, parent +8, right +0x10, nil flag +0x19,
// the player at +0x20.
constexpr std::size_t kTeams = 0x38;
constexpr std::size_t kTreeLeft = 0x0;
constexpr std::size_t kTreeParent = 0x8;
constexpr std::size_t kTreeRight = 0x10;
constexpr std::size_t kTreeNil = 0x19;
constexpr std::size_t kTreeObject = 0x20;
// SoldierBase
constexpr std::size_t kRemote = 0x128;        // bit 0: a remote player's copy
constexpr std::size_t kMaxHealth = 0x2F4;     // float; HUiHudPowerGauge uses it as the health-bar denominator
constexpr std::size_t kHealth = 0x2F8;        // float; the script's IsAlive is health > 0, and a downed player has 0
constexpr std::size_t kLocalControl = 0x340;  // set on a locally controlled player
constexpr std::size_t kNetPlayer = 0x1ED0;    // a network player's record, with its slot at +0x48
constexpr std::size_t kNetSlot = 0x48;
// Look angles in radians, which keep following the local player's input while they are down. The network sends
// pitch and yaw + yaw offset (59FE14, 59FE24), and a remote copy stores that sum as its yaw.
constexpr std::size_t kLookPitch = 0x1230;
constexpr std::size_t kLookYaw = 0x1234;
constexpr std::size_t kLookYawOffset = 0x1E44;
// Camera parameters a ghost camera copies from its target each update (F8745): the block at +0x230 while byte
// +0x224 is set, else the one at +0x170. A block's byte +0x40 makes the camera orbit the target at the pitch/yaw
// pair at +0x50 instead of looking along the target's own facing (FAF3C). No game code sets that byte to anything
// but 0.
constexpr std::size_t kCameraParams[] = {0x170, 0x230};
constexpr std::size_t kParamOrbit = 0x40;
constexpr std::size_t kParamAngles = 0x50;
// shared_ptr control block (MSVC): vtable (+0 destroy the object, +8 delete the block), uses +8, weaks +0xC.
constexpr std::size_t kUses = 0x8;
constexpr std::size_t kWeaks = 0xC;
// Well above the four players and the handful of cameras a view holds: anything longer is not the game's.
constexpr int kMaxNodes = 64;
constexpr std::size_t kMaxPlayers = 8;

using SystemUpdateFn = void (*)(void* system, float step, std::uint32_t frames);
using FactoryFn = void* (*)(void* system, void* weakOut, std::uint32_t view, const float* matrix, void* param);
using HudFactoryFn = void* (*)(void* camera, void* weakOut, void* param);
using WeakFromThisFn = void* (*)(void* camera, void* out);
using SetViewCameraFn = void (*)(void* view, void* weak);
using IsOnlineModeFn = bool (*)();
using DynamicCastFn = void* (*)(void* object, long vfDelta, void* sourceType, void* targetType, int isReference);
using ControlFn = void (*)(std::uintptr_t control);

// Game code the spectator calls or relies on, by FNV-1a 64 of its bytes. None of these ranges holds a relocation.
struct Code {
    const char* name;
    std::uint32_t rva;
    std::uint32_t size;
    std::uint64_t hash;
};
constexpr Code kCode[] = {
    {"System::Update", kSystemUpdate, 0x9FB, 0x7F0564B7A3BF2724},
    {"CharacterGhostCamera factory", kGhostCameraFactory, 0x1DC, 0x6F6DB55CC4005B3C},
    {"native HUD call parameters", 0x7FE5D0, 0x988, 0x30B0F56AE0009F97},
    {"health HUD factory", 0x7FC9A0, 0x259, 0xC053F7AAE37CD46D},
    {"teammate names HUD factory", 0x7FD580, 0x257, 0xD3B9016D4F01994A},
    {"camera HUD ownership transfer", 0x7FF390, 0x9A, 0x7B2D65E7F57F7623},
    {"health HUD wrapper construction", 0x815990, 0x63, 0x5FF1DB331C4F55D1},
    {"health HUD layout construction", 0x815E60, 0x218, 0xFECAC8EC031E8E36},
    {"health HUD player binding", 0x815810, 0x172, 0x2F4117E29D9C1FE6},
    {"health HUD wrapper cleanup", 0x815B10, 0x1A9, 0x4FF33942129D9B7D},
    {"health HUD camera visibility", 0x8168B0, 0x1D3, 0x03B27D69E6200892},
    {"health HUD layout setup", 0x816080, 0x41A, 0x70DDB2694F60B2B4},
    {"health HUD layout transform", 0x8164A0, 0x17A, 0xBD774FC23FF6F171},
    {"health gauge construction", 0x825250, 0xDB4, 0x3C71A55DAA5E8A8A},
    {"health gauge display", 0x826250, 0xDB8, 0xF4FE74DF3E238442},
    {"health gauge update", 0x827010, 0x79A, 0xC44952D123F21801},
    {"health gauge vehicle display", 0x8277B0, 0x390, 0xEBF4C2C16F34319F},
    {"HUD vehicle lookup", 0x8024D0, 0xA0, 0x55389265C9EC92B0},
    {"multiplayer HUD player binding", 0x7FF670, 0x167, 0x7AA4C0C4DC39856A},
    {"teammate names HUD construction", 0x8047C0, 0xD4, 0x89F3DD8D4B8AF127},
    {"teammate names HUD resources", 0x806FE0, 0x590, 0xA5BC1A7D743AD0EA},
    {"teammate names HUD update", 0x807870, 0x50B, 0x4982CB0CA1FA9638},
    {"teammate names owner exclusion", 0x807680, 0x12A, 0x3799AE9CAA03B906},
    {"teammate names HUD draw loop", 0x8077B0, 0xB9, 0xD3C67D358E532814},
    {"teammate name and health drawing", 0x804C90, 0x1A1E, 0x793D6D440F8AE747},
    {"teammate name and network slot", 0x7FFBD0, 0x316, 0x928DCDE1820225DC},
    {"network display name", 0x784830, 0x1D6, 0xAC8CA0E518BFCB02},
    {"network platform name", 0x12ABFF0, 0x266, 0x2FFD4BD265F55D78},
    {"chat HUD factory", 0x7FCE60, 0x257, 0xD0128CEBB3F70692},
    {"chat HUD construction", 0x800520, 0x125, 0x31F4B8FFF36A2126},
    {"chat HUD destructor", 0x8007E0, 0x15A, 0x6E653C90FCC9C658},
    {"chat HUD deleting destructor", 0x800A80, 0x17C, 0x0E3BB1E3AAF4F75E},
    {"chat receive callback", 0x800C00, 0x59, 0xE9AE064FC00E2944},
    {"chat HUD layout setup", 0x802970, 0x289, 0x863B694B975811BE},
    {"chat message add", 0x802C00, 0x310, 0xB48D3FC86CCDE78C},
    {"chat HUD draw", 0x802F10, 0x102, 0x8F50E7AEBAE1EE37},
    {"chat HUD update", 0x803020, 0x214, 0xB8C6C261062A53DC},
    {"chat message draw", 0x801420, 0xEB2, 0xDB38601F64406159},
    {"view camera switch", kSetViewCamera, 0x18D, 0xF859E0B97E448F2F},
    {"weak_from_this", kWeakFromThis, 0x9C, 0x580BFED010D5E37E},
    {"camera flush", 0x1195A80, 0x159, 0xFAAA5CBBF9469555},
    {"current camera update", 0x11978D0, 0x1000, 0x15240CC9F660A4DA},
    {"create_remote_camera", 0xCCCA0, 0x2D9, 0x7B438E325C57D000},
    {"CharacterGhostCamera constructor", 0xF4860, 0x974, 0xB9D5E9BDAA015326},
    {"ghost camera target copy", 0xF86A0, 0x4A7, 0xC8F39D3D226684CB},
    {"ghost camera orbit rotation", 0xFAF20, 0xF0, 0x5A931A41EBC8898A},
    {"network look angles", 0x59FE04, 0x38, 0x5271702E19DA8061},
    {"IsPlayer", 0x550890, 0xA, 0x1D923A0DEC9EF9A4},
    {"IsAlive", 0x1C0BE0, 0x2E, 0xA905EB899CAEB9D9},
    {"IsOnlineMode", kIsOnlineMode, 0x2B, 0xA71A18502F48BC92},
    {"__RTDynamicCast import thunk", kDynamicCast, 0x6, 0xD6E2BD7734AF1D4E},
};

std::uint64_t Fnv1a(const unsigned char* bytes, std::size_t size) {
    std::uint64_t hash = 0xCBF29CE484222325;
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001B3;
    return hash;
}

template <typename T>
T At(std::uintptr_t address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

std::uintptr_t Ptr(std::uintptr_t address) { return At<std::uintptr_t>(address); }

template <typename T>
void Put(std::uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
}

bool Expired(std::uintptr_t control) { return !control || At<std::int32_t>(control + kUses) <= 0; }

unsigned char* game = nullptr;
SystemUpdateFn systemUpdate = nullptr;
bool stopped = false;
// A slot no player has: the current camera follows someone who left the room.
constexpr std::uint32_t kGone = kOwnCamera - 1;
std::uint32_t createdThisDown = 0;  // slots whose camera was asked for since the local player went down

struct Player {
    std::uintptr_t object;
    std::uint32_t slot;
    bool alive;
};

struct Roster {
    Player local{};
    bool hasLocal = false;
    // Two players for one slot. Seen for about 12 seconds at the end of each mission, when the game builds a second
    // set of players and a camera for the new local one: the view is the game's to change then.
    bool doubled = false;
    Player remote[kMaxPlayers]{};  // in slot order
    std::size_t remotes = 0;
};

// The players in TeamManager's map, as the game's create_remote_camera (CCCA0) walks it.
Roster ReadRoster() {
    Roster roster;
    const auto cast = reinterpret_cast<DynamicCastFn>(game + kDynamicCast);
    const std::uintptr_t teams = Ptr(Ptr(reinterpret_cast<std::uintptr_t>(game) + kTeamManager) + kTeams);
    const auto nil = [](std::uintptr_t node) { return At<std::uint8_t>(node + kTreeNil) != 0; };
    std::uintptr_t node = Ptr(Ptr(teams) + kTreeLeft);  // the head's left is the first node
    for (int visited = 0; !nil(node) && visited < kMaxNodes; ++visited) {
        void* object = cast(reinterpret_cast<void*>(Ptr(node + kTreeObject)), 0, game + kGameObjectBaseType,
                            game + kSoldierBaseType, 0);
        if (object) {
            const auto soldier = reinterpret_cast<std::uintptr_t>(object);
            const std::uintptr_t net = Ptr(soldier + kNetPlayer);
            const Player player{soldier, net ? At<std::uint32_t>(net + kNetSlot) : kOwnCamera,
                                At<float>(soldier + kHealth) > 0.0f};
            const bool remote = (At<std::uint32_t>(soldier + kRemote) & 1) != 0;
            if (!remote && Ptr(soldier + kLocalControl)) {
                roster.doubled |= roster.hasLocal;
                roster.local = player;
                roster.hasLocal = true;
            } else if (remote && net && roster.remotes < kMaxPlayers) {
                for (std::size_t i = 0; i < roster.remotes; ++i) roster.doubled |= roster.remote[i].slot == player.slot;
                std::size_t at = roster.remotes++;
                for (; at > 0 && roster.remote[at - 1].slot > player.slot; --at)
                    roster.remote[at] = roster.remote[at - 1];
                roster.remote[at] = player;
            }
        }
        // In-order successor.
        if (!nil(Ptr(node + kTreeRight))) {
            node = Ptr(node + kTreeRight);
            for (int i = 0; !nil(Ptr(node + kTreeLeft)) && i < kMaxNodes; ++i) node = Ptr(node + kTreeLeft);
        } else {
            std::uintptr_t parent = Ptr(node + kTreeParent);
            for (int i = 0; !nil(parent) && node == Ptr(parent + kTreeRight) && i < kMaxNodes; ++i) {
                node = parent;
                parent = Ptr(node + kTreeParent);
            }
            node = parent;
        }
    }
    return roster;
}

// The object a ghost camera follows, or 0 when the camera is another kind or its target no longer lives.
std::uintptr_t Followed(std::uintptr_t camera) {
    if (!camera || Ptr(camera) != reinterpret_cast<std::uintptr_t>(game + kGhostCameraVtable)) return 0;
    return Expired(Ptr(camera + kCameraTarget + 8)) ? 0 : Ptr(camera + kCameraTarget);
}

// View 0's ghost camera that follows `object`, or 0.
std::uintptr_t FindCamera(std::uintptr_t view, std::uintptr_t object) {
    const std::uintptr_t sentinel = Ptr(view + kViewCameras);
    std::uintptr_t node = Ptr(sentinel);
    for (int i = 0; node != sentinel && i < kMaxNodes; ++i, node = Ptr(node)) {
        const std::uintptr_t camera = Ptr(node + kNodeValue);
        if (Followed(camera) == object) return camera;
    }
    return 0;
}

// The factory returns a weak_ptr. Match create_remote_camera at CCF2E: release only +0xC, never the queue's owner.
void ReleaseWeak(std::uintptr_t control) {
    if (!control || InterlockedDecrement(reinterpret_cast<volatile LONG*>(control + kWeaks)) != 0) return;
    reinterpret_cast<ControlFn>(Ptr(Ptr(control) + 8))(control);
}

// Match the isolated calls at 7FEC45, 7FEDBA and 7FEE5D, not the full local-player HUD builder. The factory fills in
// the camera. The HudPlayer widgets keep the player's weak self-reference (SoldierBase +0x28/+0x30, read at 7FF766).
struct HudParam {
    std::uintptr_t vtable;
    std::uintptr_t camera;
    std::uintptr_t player;
    const wchar_t* layout;
    std::uint8_t flags[2];
    std::uint16_t reserved;
    std::uint32_t view;
};
static_assert(sizeof(HudParam) == 0x28 && offsetof(HudParam, view) == 0x24);

void AttachHud(std::uintptr_t camera, std::uint32_t factory, HudParam& param) {
    alignas(16) std::uintptr_t hud[2]{};
    reinterpret_cast<HudFactoryFn>(game + factory)(reinterpret_cast<void*>(camera), hud, &param);
    // Both factories move the strong reference into camera+208 and return only a weak reference.
    ReleaseWeak(hud[1]);
}

// A ghost camera following `object` on view 0, made as the game's create_remote_camera (CCCA0) makes one. The System
// adds it to the view and makes it current in its next flush, which the System::Update about to run does.
bool Create(std::uintptr_t system, std::uintptr_t object, std::uintptr_t local, std::uint32_t slot) {
    // Read before making any game objects. The native gauge divides by maximum health.
    const float health = At<float>(object + kHealth);
    const float maximum = At<float>(object + kMaxHealth);
    const bool healthValid = std::isfinite(health) && std::isfinite(maximum) && health > 0 && maximum > 0;
    alignas(16) const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    alignas(16) unsigned char param[kParamSize]{};
    const auto vtable = reinterpret_cast<std::uintptr_t>(game + kGhostCameraParamVtable);
    std::memcpy(param, &vtable, sizeof(vtable));
    std::memcpy(param + kParamTarget, &object, sizeof(object));
    alignas(16) std::uintptr_t camera[2]{};  // weak_ptr<CharacterGhostCamera>
    reinterpret_cast<FactoryFn>(game + kGhostCameraFactory)(reinterpret_cast<void*>(system), camera, 0, identity,
                                                             param);
    if (!camera[0] || Expired(camera[1])) {
        ReleaseWeak(camera[1]);
        return false;
    }
    // Damage, radar and weapon widgets use an invalid local-settings index for remote players. The health
    // gauge instead reads SoldierBase health directly; never call the full HUD builder at 7FE5D0 here.
    if (healthValid) {
        Log("SPECTATE: creating health bar for the player in slot %u", slot);
        HudParam healthParam{reinterpret_cast<std::uintptr_t>(game + kHealthHudParamVtable), 0, object,
                             reinterpret_cast<const wchar_t*>(game + kHealthHudLayout), {}, 0, 0};
        AttachHud(camera[0], kHealthHudFactory, healthParam);
    } else {
        Log("SPECTATE: skipping health bar for slot %u: health or maximum health is invalid", slot);
    }
    // The name/health indicators enumerate everyone except their viewer (807689) and draw nothing without one
    // (804D67). The watched teammate is the viewer: they fill the middle of the view anyway, and the downed local
    // player's own body gets an indicator like everyone else's, which the game's own camera never shows. The
    // viewer's only other use is the vehicle lookup (8024D0) the indicators run on every player.
    Log("SPECTATE: creating teammate name indicators for the spectator camera");
    HudParam namesParam{reinterpret_cast<std::uintptr_t>(game + kNamesHudParamVtable), 0, object, nullptr, {}, 0, 0};
    AttachHud(camera[0], kNamesHudFactory, namesParam);
    // The chat log belongs to the camera it was built for, so the spectator camera gets one of its own, with the
    // local player as its viewer as on the game's camera. It registers a receiver with the chat service (800C00)
    // and shows the messages that arrive while it exists; the local camera's log keeps receiving as well.
    Log("SPECTATE: creating the chat log for the spectator camera");
    HudParam chatParam{reinterpret_cast<std::uintptr_t>(game + kNamesHudParamVtable), 0, local, nullptr, {}, 0, 0};
    AttachHud(camera[0], kChatHudFactory, chatParam);
    // The System's queue owns the camera. Releasing a strong reference here would destroy it before the flush.
    ReleaseWeak(camera[1]);
    return true;
}

// Cameras following `soldier` orbit it at `look` (pitch, yaw), or go back to its own facing. Only the plugin sets
// the orbit byte, so clearing it restores the game's value. The camera copies it during the System::Update that
// follows; a parameter reload the game does in that update (on a revive, 56D6C9) costs one frame of the old view.
void Orbit(std::uintptr_t soldier, const float* look) {
    for (const std::size_t params : kCameraParams) {
        if (look) {
            Put<float>(soldier + params + kParamAngles, look[0]);
            Put<float>(soldier + params + kParamAngles + 4, look[1]);
            Put<std::uint8_t>(soldier + params + kParamOrbit, 1);
        } else if (At<std::uint8_t>(soldier + params + kParamOrbit)) {
            Put<std::uint8_t>(soldier + params + kParamOrbit, 0);
        }
    }
}

void Follow(std::uintptr_t system) {
    if (!reinterpret_cast<IsOnlineModeFn>(game + kIsOnlineMode)()) {
        createdThisDown = 0;
        return;
    }
    const std::uintptr_t view = system + kView0;
    const std::uintptr_t current = Ptr(view + kViewCurrent);
    if (!current || Expired(Ptr(view + kViewCurrent + 8))) return;
    // Another kind of camera is none of the plugin's business. None was seen in online play.
    if (Ptr(current) != reinterpret_cast<std::uintptr_t>(game + kGhostCameraVtable)) return;
    const std::uintptr_t followed = Followed(current);

    const Roster roster = ReadRoster();
    if (!roster.hasLocal || roster.doubled) return;
    std::uint32_t followedSlot = kOwnCamera;
    if (followed != roster.local.object) {
        // A teammate, or someone who left the room. The ghost camera's update (slot 3, F8210, which 11978D0 runs for
        // each view's current camera only) reads its cached SoldierBase* (+0x360) without checking that the target
        // still lives, so a camera on someone gone must not stay current.
        followedSlot = kGone;
        for (std::size_t i = 0; i < roster.remotes; ++i)
            if (followed && roster.remote[i].object == followed) followedSlot = roster.remote[i].slot;
    }

    std::uint32_t target = kOwnCamera;
    if (roster.local.alive) {
        createdThisDown = 0;
    } else {
        Teammate mates[kMaxPlayers];
        for (std::size_t i = 0; i < roster.remotes; ++i) mates[i] = {roster.remote[i].slot, roster.remote[i].alive};
        target = NextTarget(mates, roster.remotes, followedSlot);
    }
    // The watched teammate's camera looks where the downed local player looks.
    const std::uintptr_t local = roster.local.object;
    const float look[2] = {At<float>(local + kLookPitch),
                           At<float>(local + kLookYaw) + At<float>(local + kLookYawOffset)};
    const bool steerable = std::isfinite(look[0]) && std::isfinite(look[1]);
    for (std::size_t i = 0; i < roster.remotes; ++i)
        Orbit(roster.remote[i].object, steerable && roster.remote[i].slot == target ? look : nullptr);
    if (target == followedSlot) return;

    std::uintptr_t object = roster.local.object;
    for (std::size_t i = 0; i < roster.remotes; ++i)
        if (roster.remote[i].slot == target) object = roster.remote[i].object;
    if (const std::uintptr_t camera = FindCamera(view, object)) {
        ShowCamera(game, view, camera);
        if (target == kOwnCamera) Log("SPECTATE: back to the player's own camera");
        else Log("SPECTATE: following the player in slot %u", target);
    } else if (target != kOwnCamera && target < 32 && !(createdThisDown & (1u << target))) {
        createdThisDown |= 1u << target;
        if (Create(system, object, roster.local.object, target))
            Log("SPECTATE: following the player in slot %u (new camera with health, names and chat)", target);
        else
            Log("SPECTATE: could not create a camera for the player in slot %u", target);
    }
}

void SystemUpdate(void* system, float step, std::uint32_t frames) {
    Fault fault;
    if (!stopped && system && !Guarded([&] { Follow(reinterpret_cast<std::uintptr_t>(system)); }, &fault)) {
        stopped = true;
        const auto base = reinterpret_cast<std::uintptr_t>(game);
        if (fault.instruction >= base && fault.instruction - base < kImageSize)
            Log("SPECTATE STOPPED: fault %08lX at EDF+%llX while following a player; restart the game", fault.code,
                static_cast<unsigned long long>(fault.instruction - base));
        else
            Log("SPECTATE STOPPED: fault %08lX at %llX while following a player; restart the game", fault.code,
                static_cast<unsigned long long>(fault.instruction));
    }
    systemUpdate(system, step, frames);
}

}  // namespace

std::uint32_t NextTarget(const Teammate* mates, std::size_t count, std::uint32_t followed) {
    std::size_t start = count;
    for (std::size_t i = 0; i < count; ++i) {
        if (mates[i].slot == followed && mates[i].alive) return followed;
        if (start == count && mates[i].slot > followed) start = i;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Teammate& mate = mates[(start + i) % count];
        if (mate.alive) return mate.slot;
    }
    return kOwnCamera;
}

// As the game's own 11932F0 does after adding a camera to a view.
void ShowCamera(unsigned char* base, std::uintptr_t view, std::uintptr_t camera) {
    alignas(16) std::uintptr_t weak[2]{};
    reinterpret_cast<WeakFromThisFn>(base + kWeakFromThis)(reinterpret_cast<void*>(camera), weak);
    // Takes the weak_ptr by value, so it releases `weak` itself.
    reinterpret_cast<SetViewCameraFn>(base + kSetViewCamera)(reinterpret_cast<void*>(view), weak);
}

CallSite UpdateCall() { return {"System::Update call", kSystemUpdateCall, kSystemUpdate}; }

bool Install(unsigned char* base) {
    static ThunkPage page;
    const CallSite site = UpdateCall();
    if (!CallTargets(base + site.rva, site.rva, site.target)) {
        Log("SPECTATE: EDF+%X does not call EDF+%X; a downed player keeps their own camera", site.rva, site.target);
        return false;
    }
    for (const Code& code : kCode) {
        if (Fnv1a(base + code.rva, code.size) != code.hash) {
            Log("SPECTATE: EDF+%X (%s) is not the expected code; a downed player keeps their own camera", code.rva,
                code.name);
            return false;
        }
    }
    const unsigned char* stub = page.Allocate(base) ? page.Add(reinterpret_cast<void*>(&SystemUpdate)) : nullptr;
    const auto bytes = stub ? CallBytes(base + site.rva, stub) : std::vector<std::uint8_t>{};
    if (bytes.empty() || !page.Seal()) {
        Log("SPECTATE: no call stub within reach of EDF.dll; a downed player keeps their own camera");
        page.Release();
        return false;
    }
    game = base;
    systemUpdate = reinterpret_cast<SystemUpdateFn>(base + site.target);
    if (!WriteCode(base + site.rva, bytes.data(), bytes.size())) {
        Log("SPECTATE: could not write EDF+%X (error %lu); a downed player keeps their own camera", site.rva,
            GetLastError());
        page.Release();
        return false;
    }
    Log("SPECTATE: a downed player online orbits a living teammate with their own look controls until revived "
        "(System::Update call at EDF+%X)",
        site.rva);
    return true;
}

}  // namespace multislot::spectate
