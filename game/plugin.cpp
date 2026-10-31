#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include "PluginAPI.h"
#pragma warning(pop)

#include "chatfilter.h"
#include "code.h"
#include "coop.h"
#include "crashlog.h"
#include "guard.h"
#include "hitauthgame.h"
#include "lobbytexts.h"
#include "log.h"
#include "lasertrace.h"
#include "midhook.h"
#include "netfeature.h"
#include "netplayer_game.h"
#include "patches.h"
#include "peertimeout.h"
#include "radar.h"
#include "spectate.h"
#include "weaponguard.h"
#include "src/config.h"
#include "src/dn_part.h"
#include "src/iat.h"
#include "src/log.h"

namespace multislot {
#ifdef EDF6_SHIELD_TRACE
bool InstallShieldTrace(unsigned char* base);
#endif
namespace {

// CMakeLists.txt's project VERSION, the one version of EDF6Coop: EDFModLoader is told it (PluginInfo), and the
// exported marker below carries it (package.ps1 checks the DLL it packages by it).
#define EDF6COOP_TEXT2(x) #x
#define EDF6COOP_TEXT(x) EDF6COOP_TEXT2(x)
#define EDF6COOP_VERSION_TEXT \
    EDF6COOP_TEXT(EDF6COOP_VERSION_MAJOR) "." EDF6COOP_TEXT(EDF6COOP_VERSION_MINOR) "." EDF6COOP_TEXT(EDF6COOP_VERSION_PATCH)
constexpr const char* kVersion = EDF6COOP_VERSION_TEXT;
HMODULE self = nullptr;

// out: MAX_PATH characters. Refuses paths too long to also hold the rotated log name (log.cpp), instead of
// letting a *_s string function end the game over it.
bool SiblingPath(wchar_t* out, const wchar_t* extension) {
    const DWORD length = GetModuleFileNameW(self, out, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    const std::wstring module(out, length);
    const std::size_t dot = module.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    const std::wstring path = module.substr(0, dot) + extension;
    if (path.size() + 8 >= MAX_PATH) return false;  // room for the rotated log name's suffix
    out[path.copy(out, path.size())] = L'\0';
    return true;
}

bool SupportedImage(const unsigned char* base) {
    bool read = false;
    Guarded([&] {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        read = nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
               nt->FileHeader.TimeDateStamp == kImageTimeDateStamp && nt->OptionalHeader.SizeOfImage == kImageSize;
    });
    return read;
}

struct Redirect {
    CallSite site;
    void* handler;
};

struct Hook {
    MidSite site;
    MidHandler handler;
};

ThunkPage thunks;

// The weapon guard's and the peer timeout's sites: all or nothing. Every site is verified before anything is
// written, and a write that fails restores the ones before it.
bool Apply(unsigned char* base, bool keepRoom) {
    std::vector<Hook> hooks;
    const auto guard = WeaponGuardHooks();
    hooks.push_back({guard[0], &RoomWeaponsHandler});
    hooks.push_back({guard[1], &MissionWeaponsHandler});
    std::vector<Redirect> redirects;
    if (keepRoom) {
        for (const auto& site : PeerTimeoutHooks()) hooks.push_back({site, &PeerJoinedHandler});
        for (const auto& call : PeerTimeoutCalls()) redirects.push_back({call, reinterpret_cast<void*>(&PeerTimeoutLeaveCheck)});
    }

    for (const auto& hook : hooks) {
        const auto& site = hook.site;
        const Patch verify{site.name, site.rva, site.original, site.original};
        if (site.original.size() < 5 || site.displacedOffset + site.displacedSize > site.original.size() ||
            site.rva + site.original.size() > kImageSize || !Matches(base + site.rva, verify)) {
            Log("REFUSED: EDF+%X (%s) is not the expected code; nothing was changed", site.rva, site.name);
            return false;
        }
    }
    for (const auto& redirect : redirects) {
        if (!CallTargets(base + redirect.site.rva, redirect.site.rva, redirect.site.target)) {
            Log("REFUSED: EDF+%X (%s) does not call EDF+%X; nothing was changed", redirect.site.rva, redirect.site.name,
                redirect.site.target);
            return false;
        }
    }
    if (!thunks.Allocate(base)) {
        Log("REFUSED: no memory for call stubs near EDF.dll; nothing was changed");
        return false;
    }
    std::vector<Patch> writes;
    for (const auto& redirect : redirects) {
        const unsigned char* stub = thunks.Add(redirect.handler);
        auto bytes = stub ? CallBytes(base + redirect.site.rva, stub) : std::vector<std::uint8_t>{};
        if (bytes.empty()) {
            Log("REFUSED: call stub for %s out of reach; nothing was changed", redirect.site.name);
            thunks.Release();
            return false;
        }
        writes.push_back({redirect.site.name, redirect.site.rva, {base + redirect.site.rva, base + redirect.site.rva + 5}, std::move(bytes)});
    }
    for (const auto& hook : hooks) {
        const auto& site = hook.site;
        const unsigned char* at = base + site.rva;
        const auto resume = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(at + site.original.size()));
        const unsigned char* thunk = EmitMidThunk(thunks, hook.handler, at + site.displacedOffset, site.displacedSize, resume);
        auto bytes = thunk ? JumpBytes(at, thunk, site.original.size()) : std::vector<std::uint8_t>{};
        if (bytes.empty()) {
            Log("REFUSED: hook thunk for %s out of reach; nothing was changed", site.name);
            thunks.Release();
            return false;
        }
        writes.push_back({site.name, site.rva, site.original, std::move(bytes)});
    }
    if (!thunks.Seal()) {
        Log("REFUSED: could not make call stubs executable (error %lu); nothing was changed", GetLastError());
        thunks.Release();
        return false;
    }
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto& write = writes[i];
        if (!WriteCode(base + write.rva, write.replacement.data(), write.replacement.size())) {
            Log("ERROR: could not write EDF+%X (%s), error %lu; restoring %zu earlier writes", write.rva, write.name,
                GetLastError(), i);
            for (std::size_t j = i; j-- > 0;)
                WriteCode(base + writes[j].rva, writes[j].original.data(), writes[j].original.size());
            thunks.Release();
            return false;
        }
    }
    Log("Patched EDF.dll: %zu sites (weapon guard%s); stubs and hook thunks at %p (%zu bytes)", writes.size(),
        keepRoom ? ", peer timeout" : "", static_cast<const void*>(thunks.Base()), thunks.Used());
    return true;
}

// The game part: the weapon guard, KeepRoomOnPeerTimeout, radar item colors, the chat word filter, the spectator camera,
// the netcode version gate, hit authority, player sync and the crash log. Returns whether it runs; when it does not, it has changed nothing and started nothing.
bool LoadGame(HMODULE game, const wchar_t* iniPath) {
    const auto base = reinterpret_cast<unsigned char*>(game);
    if (!SupportedImage(base)) {
        Log("REFUSED: EDF.dll is not the supported build (TimeDateStamp %08X, SizeOfImage %X); nothing was changed",
            kImageTimeDateStamp, kImageSize);
        return false;
    }
    // An established member stays in the room when a newcomer's P2P handshake with it times out (peertimeout.h).
    const bool keepRoom = GetPrivateProfileIntW(L"Resilience", L"KeepRoomOnPeerTimeout", 1, iniPath) != 0;
    InitWeaponGuard(base);
    InitPeerTimeout(base);
    if (!Apply(base, keepRoom)) return false;
#ifdef EDF6_SHIELD_TRACE
    InstallShieldTrace(base);
#endif
    Log("KeepRoomOnPeerTimeout=%d: %s", keepRoom ? 1 : 0,
        keepRoom ? "when the P2P handshake with someone who joined after you times out, you stay in the room and they "
                   "stay unconnected (the game makes every member but the host leave)"
                 : "a timed-out P2P handshake makes this machine leave the room, as the game does");
    // Only this machine's radar: nothing goes over the network.
    if (GetPrivateProfileIntW(L"Display", L"RadarItemColors", 1, iniPath) != 0) InstallRadarItemColors(base);
    else Log("RadarItemColors=0: every pickup on the radar stays green");
    // The game's chat word filter (chatfilter.h): off unless the INI keeps it.
    if (GetPrivateProfileIntW(L"Chat", L"WordFilter", 0, iniPath) == 0) InstallChatFilterOff(base);
    else Log("WordFilter=1: the game's chat word filter stays on");
    spectate::InstallLobbyVersion(base);
    if (GetPrivateProfileIntW(L"Display", L"SpectateWhenDown", 1, iniPath) != 0) spectate::Install(base);
    else Log("SpectateWhenDown=0: a downed player keeps their own camera");
    if (GetPrivateProfileIntW(L"Diagnostics", L"Laser", 1, iniPath) != 0) InstallLaserTrace(base);
    // Hit authority and player sync: hooks and thunk pages of their own, so a site that is not the expected code
    // leaves only that part off and the game does as it always did. A part that did not start is not published.
    InitNetFeature(iniPath);
    if (!InstallHitAuthority(base)) WithdrawNetFeature(NetFeature::HitAuthority);
    if (!InstallPlayerSync(base, ReadPlayerSyncSettings(iniPath))) {
        Log("PLAYER sync: not installed; every player keeps the game's own sync");
        WithdrawNetFeature(NetFeature::PlayerSync);
    }
    // The netcode version gate reads and publishes lobby member attributes through its own lobby wrappers.
    StartNetFeature(InstallLobbyTexts(game, &dn::patchImport));
    if (GetPrivateProfileIntW(L"Log", L"CrashLog", 1, iniPath) != 0) {
        // A dump holds process memory (room name, chat text), so it is written only when the INI asks for one.
        const bool crashDump = GetPrivateProfileIntW(L"Log", L"CrashDump", 0, iniPath) != 0;
        wchar_t dumpPath[MAX_PATH]{};
        const bool wantDump = crashDump && SiblingPath(dumpPath, L"-crash.dmp");
        InstallCrashLog(game, wantDump ? dumpPath : nullptr);
        if (CrashDumpArmed())
            Log("Crash log armed; CrashDump=1, so the first access violation in EDF.dll or the plugin also writes %s "
                "(one per launch). It holds process memory, so only send it on purpose", Utf8Text(dumpPath).c_str());
        else
            Log("Crash log armed%s", crashDump ? "; CrashDump=1 but no dump can be written (DbgHelp.dll or the path)" : "");
    }
    return true;  // stays loaded: call stubs, import wrappers and the exception handler point here
}

// The log, the plugins EDF6Coop replaces and the settings file; then the transport part, then the game part.
// Returns whether anything runs.
bool LoadCoop(PluginInfo* info) {
    if (!info) return false;
    wchar_t iniPath[MAX_PATH]{}, logPath[MAX_PATH]{}, dllPath[MAX_PATH]{};
    if (!SiblingPath(iniPath, L".ini") || !SiblingPath(logPath, L".log") || !SiblingPath(dllPath, L".dll"))
        return false;
    LogOpen(logPath);
    dn::logToSink(&ForwardDirectNetLine);
    info->infoVersion = PluginInfo::MaxInfoVer;
    info->name = "EDF6Coop";
    info->version = PLUG_VER(EDF6COOP_VERSION_MAJOR, EDF6COOP_VERSION_MINOR, EDF6COOP_VERSION_PATCH, 0);
    Log("==== EDF6Coop %s ====", kVersion);

    // Before any file is touched: outside the game nothing is renamed or written.
    const HMODULE game = GetModuleHandleW(L"EDF.dll");
    if (!game) {
        Log("REFUSED: EDF.dll is not loaded, so this is not the game; nothing was changed");
        return false;
    }
    const std::wstring plugin = dllPath, dir = plugin.substr(0, plugin.find_last_of(L"\\/") + 1);
    if (!RetireReplacedPlugins(dir)) return false;
    RemoveOldModFiles(dir);
    const dn::Config settings = dn::loadConfig(iniPath);
    if (!settings.enabled) {
        Log("[DirectNet] Enabled=0: EDF6Coop stays off and the game is left untouched");
        return false;
    }
    const dn::PartState transport = dn::startPart(settings, dir, game, GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll"));
    const bool gamePart = LoadGame(game, iniPath);
    if (!transport.running && !gamePart) {
        Log("Neither part runs (see above why): the game goes on without EDF6Coop");
        return false;
    }
    return true;  // stays loaded: call stubs, import wrappers, threads and the exception handler point here
}

}  // namespace
}  // namespace multislot

// The version as package.ps1 checks it inside the DLL it packages (exported, so the linker keeps it).
extern "C" __declspec(dllexport) const char EDF6CoopVersion[] = "EDF6COOP_VERSION=" EDF6COOP_VERSION_TEXT;

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    // Every line so far was written by this thread, so the startup report is on disk already.
    const bool loaded = multislot::LoadCoop(info);
    if (loaded) {
        // Staying for the life of the process: from here on the writer thread keeps up, and the game's threads
        // never wait on the disk.
        multislot::LogStartWriter();
    } else {
        // EDFModLoader unloads a plugin that refuses (FreeLibrary as soon as this returns): no thread was started,
        // and the queue file is let go here.
        dn::logToSink(nullptr);
        multislot::LogClose();
    }
    return loaded;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        multislot::self = instance;
        DisableThreadLibraryCalls(instance);
    }
    // At exit the direct link's worker thread is already killed (possibly holding a lock) and static objects are
    // about to be destroyed, while EDF.dll may still call EOS through the hooks. Never wait here.
    if (reason == DLL_PROCESS_DETACH) dn::detachPart();
    return TRUE;
}
