#include "spectate.h"

#include <windows.h>

#include <chrono>
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
constexpr std::uint32_t kRescueHudFactory = 0x7FD7E0;  // (SceneCamera*, weak_ptr* out, HudPlayer::InitParam*)
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
// Slot 9: bool(camera), whether the camera's HUD widgets may show this frame. False while the camera orbits (byte
// +0x3F0), which hides a downed local player's HUD while their own camera circles their body.
constexpr std::uint32_t kGhostCameraHudVisibleSlot = kGhostCameraVtable + 9 * 8;
constexpr std::uint32_t kGhostCameraHudVisible = 0xF7500;
// Byte +0x200, set by SceneCamera's constructor (118B03A): the camera's HUD draws at all.
constexpr std::size_t kCameraHudOn = 0x200;
// HudPlayer_RescueMessage: 7 entries after its RTTI locator, whose class offset is 0. Slot 3 draws (Hud*,
// DrawContext*, Viewport*); the viewport's width and height are ints at +8 and +0xC. The caption takes a copy of the
// table with its own draw.
constexpr std::uint32_t kRescueHudVtable = 0x17F6E08;
constexpr std::uint32_t kRescueHudLocator = 0x1CA19E0;
constexpr std::uint32_t kRescueHudSlots[] = {0x7FC750, 0x8086F0, 0x11D43D0, 0x808410, 0x8083D0, 0x7FC6D0, 0x7FC630};
constexpr std::size_t kRescueHudDraw = 3;
// A HudPlayer (MultiPlayHud_Base, 7FF670): its camera at +0x18, its 4x4 matrix at +0x20 (the screen transform
// 7FC760 hands the draw context), its player's weak_ptr at +0x70.
constexpr std::size_t kHudCamera = 0x18;
constexpr std::size_t kHudMatrix = 0x20;
constexpr std::size_t kHudPlayer = 0x70;
// The draw calls of the rescue message's draw (808410), which the caption repeats with its own text and place.
constexpr std::uint32_t kHudScreenPoint = 0x7FC380;  // (Hud*, float4* out, const float4* {x, y, depth, 1}, Viewport*)
constexpr std::uint32_t kHudTransform = 0x7FC760;    // (Hud*, DrawContext*, Viewport*)
constexpr std::uint32_t kPlayerName = 0x7FFBD0;  // (weak_ptr<SoldierBase> by value, int* slot, int*, wstring* out, bool*)
constexpr std::uint32_t kStringFree = 0x3D3C0;   // std::wstring's _Tidy_deallocate
// The HUD font: style defaults at +0x80..+0x94 and +0xB0/+0xB4. Its text objects lock its critical section at +0x10
// from 11517A0 to 1138630.
constexpr std::uint32_t kFont = 0x20B29C0;
constexpr std::uint32_t kTextBegin = 0x11517A0;    // (font + 8, Text* out)
constexpr std::uint32_t kTextStyle = 0x113B4A0;    // (Text*, DrawContext*, const TextStyle*), kept until kTextFlush
constexpr std::uint32_t kTextMeasure = 0x1139790;  // (Text*, float4* out width/height, const wchar_t*, length, bool)
constexpr std::uint32_t kTextDraw = 0x113C520;  // (Text*, DrawContext*, const float4x4*, const float4* colour, text, length)
constexpr std::uint32_t kTextFlush = 0x113C6F0;    // (Text*, DrawContext*)
constexpr std::uint32_t kTextEnd = 0x1138630;      // (Text*)
// Byte 1 while the game's text entry (the chat box, a soft keyboard GUI) is open: 1183160 writes it with the IME
// association, and the game's own keyboard reader (95F930) drops every key for play while it is set.
constexpr std::uint32_t kTextEntry = 0x21360BA;

// System: four views of 0x188 bytes from +0x4D0. A view holds its current camera as a weak_ptr at +0x8 and its
// cameras in a std::list<shared_ptr<SceneCamera>> at +0x18 (nodes: next +0, value +0x10). Online, the local
// player's camera is on view 0.
constexpr std::size_t kView0 = 0x4D0;
constexpr std::size_t kViewCurrent = 0x8;
constexpr std::size_t kViewCameras = 0x18;
constexpr std::size_t kNodeValue = 0x10;
// CharacterGhostCamera follows the object in the weak_ptr at +0x350, taken from its InitParam's +0x80, and caches
// it as a raw SoldierBase* at +0x360.
constexpr std::size_t kCameraTarget = 0x350;
constexpr std::size_t kCameraTargetObject = 0x360;
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
// The soldier update copies the seat transform into +0x60..+0x9F while this weak reference lives (5751B9).
constexpr std::size_t kVehicleControl = 0x1550;
constexpr std::size_t kForward = 0x80;
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
using HudVisibleFn = bool (*)(void* camera);
using DynamicCastFn = void* (*)(void* object, long vfDelta, void* sourceType, void* targetType, int isReference);
using ControlFn = void (*)(std::uintptr_t control);
using HudDrawFn = void (*)(void* hud, void* context, const std::int32_t* viewport);
using HudScreenPointFn = void (*)(void* hud, float* out, const float* point, const std::int32_t* viewport);
using HudTransformFn = void (*)(void* hud, void* context, const std::int32_t* viewport);
using TextBeginFn = void (*)(void* font, void* text);
using TextStyleFn = void (*)(void* text, void* context, const void* style);
using TextMeasureFn = void (*)(void* text, float* out, const wchar_t* string, std::int64_t length, bool);
using TextDrawFn = void (*)(void* text, void* context, const float* matrix, const float* colour, const wchar_t* string,
                            std::int64_t length);
using TextFlushFn = void (*)(void* text, void* context);
using TextEndFn = void (*)(void* text);

// std::wstring as MSVC lays it out: 7 characters inline, else a pointer to them; then size and capacity.
struct GameString {
    union {
        wchar_t inlineChars[8];
        wchar_t* heapChars;
    };
    std::size_t size;
    std::size_t capacity;
    const wchar_t* Chars() const { return capacity > 7 ? heapChars : inlineChars; }
};
static_assert(sizeof(GameString) == 0x20);
using PlayerNameFn = void (*)(std::uintptr_t* weak, std::int32_t* slot, std::int32_t* colour, GameString* name,
                              bool* platform);
using StringFreeFn = void (*)(GameString* string);

// The text style 808410 builds on its stack; 11388E0 copies its 0x38 bytes into the font's renderer.
struct TextStyle {
    float defaults[6];  // the font's +0x80..+0x94; [1] and [2] are the scale (113A3F0 writes them)
    std::int32_t one;
    float black[4];
    float size;
    std::int32_t flags[2];  // the font's +0xB0/+0xB4
};
static_assert(sizeof(TextStyle) == 0x38);

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
    {"ghost camera HUD visibility", kGhostCameraHudVisible, 0x48, 0x3E8A8467D6F818D0},
    {"HUD widget shown flag clearing", 0x80FC50, 0xB88, 0x326E883AB7461031},
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
    {"rescue message HUD factory", kRescueHudFactory, 0x257, 0x749A67C36F8046A6},
    {"rescue message HUD construction", 0x8083A0, 0x21, 0xACDF1813DE907759},
    {"rescue message HUD destructor", 0x8083D0, 0x34, 0xA084B6851CE38CB1},
    {"rescue message draw", 0x808410, 0x2D3, 0xA77E2074C4927895},
    {"HUD view matrix", 0x7FC6D0, 0x80, 0x47DF0BDBF265D186},
    {"HUD projection", 0x7FC630, 0x94, 0x1933F2BD66CA6745},
    {"HUD screen point", kHudScreenPoint, 0x298, 0x8E1D5E60A18102D9},
    {"HUD screen transform", kHudTransform, 0x23E, 0x7128546B2A55BAA6},
    {"wstring deallocation", kStringFree, 0x62, 0x001F8BD145A9B42F},
    {"font text object", kTextBegin, 0x4F, 0x9B63E0BB4001FB42},
    {"text object construction", 0x11379B0, 0xFA, 0x26813887C10E97EB},
    {"text style", kTextStyle, 0x9, 0x6EF651AD1E09BE2A},
    {"text renderer style", 0x113B2A0, 0x1F5, 0x27667DDC1ADBAFB7},
    {"text style copy", 0x11388E0, 0x84, 0xC636B896D8191BCC},
    {"text measure", kTextMeasure, 0x257, 0xD1BECF90B85D50AA},
    {"text draw", kTextDraw, 0x9, 0x1FF136490574DAE0},
    {"text renderer draw", 0x113B4B0, 0x106C, 0x5B28649E627DA012},
    {"text flush", kTextFlush, 0x9, 0x9F2FCD3A8BCD3B1B},
    {"text renderer flush", 0x113C5C0, 0x127, 0x96DA45BA9FC16254},
    {"text object destruction", kTextEnd, 0x79, 0x6D074C61B1186916},
    {"view camera switch", kSetViewCamera, 0x18D, 0xF859E0B97E448F2F},
    {"weak_from_this", kWeakFromThis, 0x9C, 0x580BFED010D5E37E},
    {"camera flush", 0x1195A80, 0x159, 0xFAAA5CBBF9469555},
    {"current camera update", 0x11978D0, 0x1000, 0x15240CC9F660A4DA},
    {"create_remote_camera", 0xCCCA0, 0x2D9, 0x7B438E325C57D000},
    {"CharacterGhostCamera constructor", 0xF4860, 0x974, 0xB9D5E9BDAA015326},
    {"ghost camera target copy", 0xF86A0, 0x4A7, 0xC8F39D3D226684CB},
    {"ghost camera orbit rotation", 0xFAF20, 0xF0, 0x5A931A41EBC8898A},
    {"received look angle storage", 0x5921BC, 0x3D, 0x61920059725219C0},
    {"seated soldier matrix copy", 0x5751B9, 0x53, 0x56EAF9A8BA8CBB62},
    {"vehicle exit aim reset", 0x5705A5, 0x1B, 0xA0B16F34405F61E1},
    {"matrix forward yaw", 0x4E1F0, 0x23, 0xB2489B4CE99D04C6},
    {"network look angles", 0x59FE04, 0x38, 0x5271702E19DA8061},
    {"IsPlayer", 0x550890, 0xA, 0x1D923A0DEC9EF9A4},
    {"IsAlive", 0x1C0BE0, 0x2E, 0xA905EB899CAEB9D9},
    {"IsOnlineMode", kIsOnlineMode, 0x2B, 0xA71A18502F48BC92},
    {"IME association", 0x1183160, 0x2D, 0x18B1F50D6B101044},
    {"text-entry byte getter", 0x1183CE0, 0x8, 0x2BC3DE9F6A5D005F},
    {"text-entry window procedure", 0x7D2C40, 0x9A, 0xAD9CA65227696BA0},
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
HudVisibleFn hudVisible = nullptr;  // the game's F7500
bool stopped = false;
// A slot no player has: the current camera follows someone who left the room.
constexpr std::uint32_t kGone = kOwnCamera - 1;
std::uint32_t createdThisDown = 0;  // slots whose camera was asked for since the local player went down
// The camera orbits the watched teammate at the downed player's own look angles (free), or looks along the
// teammate's smoothed aim or seated heading (locked). E switches; the choice lasts the session.
bool freeCam = true;
LockedAim lockedAim;

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

// The new HUD object, which the camera owns.
std::uintptr_t AttachHud(std::uintptr_t camera, std::uint32_t factory, HudParam& param) {
    alignas(16) std::uintptr_t hud[2]{};
    reinterpret_cast<HudFactoryFn>(game + factory)(reinterpret_cast<void*>(camera), hud, &param);
    // The factories move the strong reference into camera+208 and return only a weak reference.
    ReleaseWeak(hud[1]);
    return hud[0];
}

// HudPlayer_RescueMessage's table with DrawCaption as its draw, after the RTTI locator. Filled by Install.
std::uintptr_t captionVtable[1 + std::size(kRescueHudSlots)] = {};

// The caption's draw, in place of the rescue message's (808410): the same calls with the watched teammate's name for
// the text, centred a tenth of the view's height from its top. The name comes from 7FFBD0, as the name indicators
// get theirs in their own draw (804EBF). Runs where the game draws its HUD, with no guard, so it calls only the
// functions 808410 and 804C90 call there, with their arguments laid out as theirs are.
void DrawCaption(void* hud, void* context, const std::int32_t* viewport) {
    const auto at = reinterpret_cast<std::uintptr_t>(hud);
    const std::uintptr_t camera = Ptr(at + kHudCamera);
    if (!At<std::uint8_t>(camera + kCameraHudOn) ||
        !reinterpret_cast<HudVisibleFn>(Ptr(Ptr(camera) + 9 * 8))(reinterpret_cast<void*>(camera)))
        return;
    const auto font = Ptr(reinterpret_cast<std::uintptr_t>(game) + kFont);
    if (!font) return;
    // 7FFBD0 takes the weak_ptr by value and releases it; it locks it itself and names nobody once the player is gone.
    std::uintptr_t player[2] = {Ptr(at + kHudPlayer), Ptr(at + kHudPlayer + 8)};
    if (player[1]) InterlockedIncrement(reinterpret_cast<volatile LONG*>(player[1] + kWeaks));
    GameString name{{}, 0, 7};
    std::int32_t slot = -1, colour = -1;
    bool platform = false;
    reinterpret_cast<PlayerNameFn>(game + kPlayerName)(player, &slot, &colour, &name, &platform);
    if (slot >= 0 && name.size > 0) {
        alignas(16) const float place[4] = {viewport[2] * 0.5f, viewport[3] * 0.1f, -600.0f, 1.0f};
        alignas(16) float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        reinterpret_cast<HudScreenPointFn>(game + kHudScreenPoint)(hud, matrix + 12, place, viewport);
        std::memcpy(reinterpret_cast<void*>(at + kHudMatrix), matrix, sizeof(matrix));
        reinterpret_cast<HudTransformFn>(game + kHudTransform)(hud, context, viewport);
        // 808410's style: the font's defaults, scale 1.33, then 1, black and 2.5.
        TextStyle style{{At<float>(font + 0x80), 1.33f, 1.33f, At<float>(font + 0x8C), At<float>(font + 0x90),
                         At<float>(font + 0x94)},
                        1,
                        {0, 0, 0, 1},
                        2.5f,
                        {At<std::int32_t>(font + 0xB0), At<std::int32_t>(font + 0xB4)}};
        alignas(16) unsigned char text[0x38];
        reinterpret_cast<TextBeginFn>(game + kTextBegin)(reinterpret_cast<void*>(font + 8), text);
        reinterpret_cast<TextStyleFn>(game + kTextStyle)(text, context, &style);
        alignas(16) float size[4];
        reinterpret_cast<TextMeasureFn>(game + kTextMeasure)(text, size, name.Chars(), -1, false);
        matrix[12] -= size[0] * 0.5f;
        matrix[13] -= size[1] * 0.5f;
        alignas(16) const float white[4] = {1, 1, 1, 1};
        reinterpret_cast<TextDrawFn>(game + kTextDraw)(text, context, matrix, white, name.Chars(), -1);
        reinterpret_cast<TextFlushFn>(game + kTextFlush)(text, context);
        reinterpret_cast<TextEndFn>(game + kTextEnd)(text);
    }
    reinterpret_cast<StringFreeFn>(game + kStringFree)(&name);
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
    // The caption is a rescue message bound to the watched teammate whose table draws their name instead. Its
    // destructor and its other slots are the game's.
    Log("SPECTATE: creating the name caption for the spectator camera");
    HudParam captionParam{reinterpret_cast<std::uintptr_t>(game + kNamesHudParamVtable), 0, object, nullptr, {}, 0, 0};
    const std::uintptr_t caption = AttachHud(camera[0], kRescueHudFactory, captionParam);
    if (caption && Ptr(caption) == reinterpret_cast<std::uintptr_t>(game + kRescueHudVtable))
        Put(caption, reinterpret_cast<std::uintptr_t>(&captionVtable[1]));
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

// A key read while the local player is down. It counts as held, so the next press needs a release first, while the
// game's text entry is open (a typed letter goes into the message only) and while another window has the focus.
struct Key {
    int code;
    bool held = true;  // counted as down last frame, so it is not a new press
    bool Pressed() {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool down = process != GetCurrentProcessId() ||
                          At<std::uint8_t>(reinterpret_cast<std::uintptr_t>(game) + kTextEntry) != 0 ||
                          (GetAsyncKeyState(code) & 0x8000) != 0;
        const bool pressed = down && !held;
        held = down;
        return pressed;
    }
};
Key cycleKey{'W'};  // the next standing teammate
Key modeKey{'E'};   // free or locked camera

void Follow(std::uintptr_t system, float elapsed) {
    if (!reinterpret_cast<IsOnlineModeFn>(game + kIsOnlineMode)()) {
        createdThisDown = 0;
        lockedAim = {};
        return;
    }
    const std::uintptr_t view = system + kView0;
    const std::uintptr_t current = Ptr(view + kViewCurrent);
    if (!current || Expired(Ptr(view + kViewCurrent + 8))) {
        lockedAim = {};
        return;
    }
    // Another kind of camera is none of the plugin's business. None was seen in online play.
    if (Ptr(current) != reinterpret_cast<std::uintptr_t>(game + kGhostCameraVtable)) {
        lockedAim = {};
        return;
    }
    const std::uintptr_t followed = Followed(current);

    const Roster roster = ReadRoster();
    if (!roster.hasLocal || roster.doubled) {
        lockedAim = {};
        return;
    }
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
        cycleKey.held = modeKey.held = true;  // a key held while going down (W runs forward) is not a press
        lockedAim = {};
    } else {
        if (modeKey.Pressed()) {
            freeCam = !freeCam;
            Log(freeCam ? "SPECTATE: free camera, aimed with the look controls (E toggles)"
                        : "SPECTATE: locked camera, smoothed aim or vehicle heading (E toggles)");
        }
        Teammate mates[kMaxPlayers];
        for (std::size_t i = 0; i < roster.remotes; ++i) mates[i] = {roster.remote[i].slot, roster.remote[i].alive};
        target = NextTarget(mates, roster.remotes, followedSlot, cycleKey.Pressed());
    }
    std::uintptr_t object = roster.local.object;
    for (std::size_t i = 0; i < roster.remotes; ++i)
        if (roster.remote[i].slot == target) object = roster.remote[i].object;
    const std::uintptr_t local = roster.local.object;
    const float ownLook[2] = {At<float>(local + kLookPitch),
                             At<float>(local + kLookYaw) + At<float>(local + kLookYawOffset)};
    const float* look = nullptr;
    if (freeCam || target == kOwnCamera) {
        lockedAim = {};
        if (freeCam && std::isfinite(ownLook[0]) && std::isfinite(ownLook[1])) look = ownLook;
    } else {
        // A replacement in the same slot must not inherit its predecessor's view.
        if (object != followed) lockedAim = {};
        if (lockedAim.Update(object, target, elapsed)) look = lockedAim.look;
    }
    for (std::size_t i = 0; i < roster.remotes; ++i)
        Orbit(roster.remote[i].object, roster.remote[i].slot == target ? look : nullptr);
    if (target == followedSlot) return;

    if (const std::uintptr_t camera = FindCamera(view, object)) {
        ShowCamera(game, view, camera);
        if (target == kOwnCamera) Log("SPECTATE: back to the player's own camera");
        else Log("SPECTATE: following the player in slot %u", target);
    } else if (target != kOwnCamera && target < 32 && !(createdThisDown & (1u << target))) {
        createdThisDown |= 1u << target;
        if (Create(system, object, roster.local.object, target))
            Log("SPECTATE: following the player in slot %u (new camera with health, names, chat and caption)", target);
        else
            Log("SPECTATE: could not create a camera for the player in slot %u", target);
    }
}

// In the ghost camera's vtable in place of F7500. The health gauge's enable pass (8168B0, a render command the game
// queues after System::Update) sets the widget's shown flag only when this says yes, and the widget's own update
// (80FC50) clears the flag, so the game's no while the camera orbits leaves the gauge hidden. The free spectator
// camera orbits too: for a camera following a remote player, which only the plugin makes, the answer is yes. Every
// other camera gets the game's answer. Runs on the render thread with no guard, so it reads what F7500 reads under
// the same checks: the target's control block first, then the cached pointer.
bool HudVisible(void* camera) {
    const auto at = reinterpret_cast<std::uintptr_t>(camera);
    if (!Expired(Ptr(at + kCameraTarget + 8))) {
        const std::uintptr_t target = Ptr(at + kCameraTargetObject);
        if (target && (At<std::uint32_t>(target + kRemote) & 1)) return true;
    }
    return hudVisible(camera);
}

void SystemUpdate(void* system, float step, std::uint32_t frames) {
    const auto now = std::chrono::steady_clock::now();
    static auto previous = now;
    const float elapsed = std::chrono::duration<float>(now - previous).count();
    previous = now;
    Fault fault;
    if (!stopped && system && !Guarded([&] { Follow(reinterpret_cast<std::uintptr_t>(system), elapsed); }, &fault)) {
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

bool LockedAim::Update(std::uintptr_t soldier, std::uint32_t slot, float elapsed) {
    const bool vehicle = !Expired(Ptr(soldier + kVehicleControl));
    float pitch = 0.0f;
    float yaw;
    if (vehicle) {
        const float x = At<float>(soldier + kForward);
        const float z = At<float>(soldier + kForward + 8);
        if (!std::isfinite(x) || !std::isfinite(z) || std::hypot(x, z) < 0.0001f) {
            *this = {};
            return false;
        }
        yaw = std::atan2(x, z);
    } else {
        pitch = At<float>(soldier + kLookPitch);
        yaw = At<float>(soldier + kLookYaw);
    }
    if (!std::isfinite(pitch) || !std::isfinite(yaw)) {
        *this = {};
        return false;
    }
    constexpr double turn = 6.2831853071795864769;
    const double wrappedYaw = std::remainder(static_cast<double>(yaw), turn);
    if (!ready_ || slot != slot_ || vehicle != vehicle_) {
        look[0] = pitch;
        look[1] = static_cast<float>(wrappedYaw);
    } else if (std::isfinite(elapsed) && elapsed > 0.0f) {
        // These fields expose no arrival counter. Smooth toward the latest value, including unchanged samples,
        // without guessing packet times or extrapolating. A 50 ms time constant removes the 100 ms aim steps.
        const double amount = -std::expm1(-static_cast<double>(elapsed) / 0.05);
        look[0] = static_cast<float>(look[0] + (static_cast<double>(pitch) - look[0]) * amount);
        const double delta = std::remainder(wrappedYaw - look[1], turn);
        look[1] = static_cast<float>(std::remainder(look[1] + delta * amount, turn));
    }
    slot_ = slot;
    vehicle_ = vehicle;
    ready_ = true;
    return true;
}

std::uint32_t NextTarget(const Teammate* mates, std::size_t count, std::uint32_t followed, bool advance) {
    std::size_t start = count;
    for (std::size_t i = 0; i < count; ++i) {
        if (!advance && mates[i].slot == followed && mates[i].alive) return followed;
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

VtableSlot HudVisibleSlot() {
    return {"ghost camera HUD visibility slot", kGhostCameraHudVisibleSlot, kGhostCameraHudVisible};
}

bool Install(unsigned char* base) {
    static ThunkPage page;
    const CallSite site = UpdateCall();
    if (!CallTargets(base + site.rva, site.rva, site.target)) {
        Log("SPECTATE: EDF+%X does not call EDF+%X; a downed player keeps their own camera", site.rva, site.target);
        return false;
    }
    const VtableSlot slot = HudVisibleSlot();
    if (!SlotTargets(base + slot.rva, reinterpret_cast<std::uint64_t>(base), slot.target)) {
        Log("SPECTATE: EDF+%X does not hold EDF+%X; a downed player keeps their own camera", slot.rva, slot.target);
        return false;
    }
    const auto image = reinterpret_cast<std::uint64_t>(base);
    bool rescueTable = SlotTargets(base + kRescueHudVtable - 8, image, kRescueHudLocator);
    for (std::size_t i = 0; i < std::size(kRescueHudSlots); ++i)
        rescueTable &= SlotTargets(base + kRescueHudVtable + i * 8, image, kRescueHudSlots[i]);
    if (!rescueTable) {
        Log("SPECTATE: EDF+%X is not the rescue message's table; a downed player keeps their own camera",
            kRescueHudVtable);
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
    captionVtable[0] = image + kRescueHudLocator;
    for (std::size_t i = 0; i < std::size(kRescueHudSlots); ++i) captionVtable[1 + i] = image + kRescueHudSlots[i];
    captionVtable[1 + kRescueHudDraw] = reinterpret_cast<std::uintptr_t>(static_cast<HudDrawFn>(&DrawCaption));
    systemUpdate = reinterpret_cast<SystemUpdateFn>(base + site.target);
    hudVisible = reinterpret_cast<HudVisibleFn>(base + slot.target);
    const std::uintptr_t visible = reinterpret_cast<std::uintptr_t>(&HudVisible);
    if (!WriteCode(base + slot.rva, reinterpret_cast<const unsigned char*>(&visible), sizeof(visible))) {
        Log("SPECTATE: could not write EDF+%X (error %lu); a downed player keeps their own camera", slot.rva,
            GetLastError());
        page.Release();
        return false;
    }
    if (!WriteCode(base + site.rva, bytes.data(), bytes.size())) {
        const std::uintptr_t original = reinterpret_cast<std::uintptr_t>(base + slot.target);
        WriteCode(base + slot.rva, reinterpret_cast<const unsigned char*>(&original), sizeof(original));
        Log("SPECTATE: could not write EDF+%X (error %lu); a downed player keeps their own camera", site.rva,
            GetLastError());
        page.Release();
        return false;
    }
    Log("SPECTATE: a downed player online watches a living teammate until revived: a free camera aimed with their "
        "own look controls, or the teammate's own view (E switches; System::Update call at EDF+%X, HUD visibility "
        "slot at EDF+%X)",
        site.rva, slot.rva);
    return true;
}

}  // namespace multislot::spectate
