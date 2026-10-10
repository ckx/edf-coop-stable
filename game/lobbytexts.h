#pragma once
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace multislot {

// Text attributes on our own lobby member and on the lobby itself, read and published once a second on the EOS tick:
// the netcode version gate's (netfeature.h). EDF.dll never reads member attributes (it imports none of those
// functions), so nothing here reaches the game.
constexpr const char* kSeqKey = "EDF6MS_SEQ";  // bumped on every publish, so EOS sends it to everyone


// Our lobby as the beat read it: who owns it, and the watched attributes of each member (absent: not set).
struct LobbyView {
    std::string lobbyId;
    std::string self;   // EOS_ProductUserId as text
    std::string owner;  // empty when the lobby details do not say
    struct Member {
        std::string id;
        std::map<std::string, std::string, std::less<>> texts;
    };
    std::vector<Member> members;
    // The lobby's own texts of the watched lobby keys (WatchLobbyTexts), as Epic relays them.
    std::map<std::string, std::string, std::less<>> lobbyTexts;
};
using LobbyObserver = std::function<void(const LobbyView& view)>;
// Read `keys` of every member on each beat (once a second while we are in a lobby) and hand them to `observer`, on
// the EOS tick, outside every lock of this file. Leaving a lobby hands it an empty view. Each call adds a watcher;
// every watcher's view holds every watcher's keys.
void WatchMemberTexts(std::vector<std::string> keys, LobbyObserver observer);
// Lobby keys (PublishLobbyText) read into every view's lobbyTexts on the same beat.
void WatchLobbyTexts(std::vector<std::string> keys);
// Our value of `key` (at most 1000 characters), published on our lobby member on the next beat and in every lobby
// we enter afterwards. Any thread. Never empty: EOS refuses an empty text with EOS_InvalidParameters, and with it the
// whole update, so an empty value is logged and not published.
void PublishMemberText(const std::string& key, const std::string& value);
// Called after every EOS tick with the platform handle, outside every lock of this file. Each call adds a listener.
void ListenToTicks(std::function<void(void* platform)> listener);
// Asks EOS to remove member `id` (EOS_ProductUserId text) from our lobby (the room owner's call: netfeature.h refuses
// a member whose netcode differs). Any thread; false when we are in no lobby or EOS lacks the function.
bool KickLobbyMember(const std::string& id);
// A text attribute of the lobby itself (not of our member), published while we own the lobby, on the same beat.
// Never empty, like PublishMemberText. Any thread.
void PublishLobbyText(const std::string& key, const std::string& value);

// Redirects one of EDF.dll's EOS imports (the transport part's dn::patchImport).
using ImportRedirect = bool (*)(HMODULE game, const char* dll, const char* function, void* replacement, void** original);
// Redirects EOS_Lobby_CreateLobby, EOS_Lobby_JoinLobby, EOS_Lobby_LeaveLobby, EOS_Lobby_DestroyLobby and
// EOS_Platform_Tick (the tick publishes and reads attributes, on the thread EOS wants its calls on). False when the
// EOS functions it needs are missing or an import could not be redirected.
bool InstallLobbyTexts(HMODULE game, ImportRedirect redirect);

}  // namespace multislot
