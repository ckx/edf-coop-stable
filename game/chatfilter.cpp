#include "chatfilter.h"

#include <windows.h>

#include <cstddef>

#include "code.h"
#include "log.h"

namespace multislot {

std::vector<CallSite> ChatFilterCalls() {
    return {{"chat word filter on send", 0x722BD5, kChatWordFilter},
            {"chat word filter on receive", 0x7230D7, kChatWordFilter},
            {"chat word filter on text entry", 0x869F8C, kChatWordFilter}};
}

bool InstallChatFilterOff(unsigned char* base) {
    const auto sites = ChatFilterCalls();
    for (const auto& site : sites) {
        if (!CallTargets(base + site.rva, site.rva, site.target)) {
            Log("CHAT: EDF+%X does not call EDF+%X (%s); the game's word filter stays on", site.rva, site.target, site.name);
            return false;
        }
    }
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (WriteCode(base + sites[i].rva, kChatFilterNoMatch, sizeof(kChatFilterNoMatch))) continue;
        Log("CHAT: could not write EDF+%X (%s), error %lu; restoring %zu earlier sites, the game's word filter stays on",
            sites[i].rva, sites[i].name, GetLastError(), i);
        for (std::size_t j = i; j-- > 0;) {
            const auto call = CallBytes(base + sites[j].rva, base + sites[j].target);
            WriteCode(base + sites[j].rva, call.data(), call.size());
        }
        return false;
    }
    Log("CHAT: WordFilter=0: the game's chat word filter is off (send, receive and text entry); players without the "
        "plugin still filter what they receive");
    return true;
}

}  // namespace multislot
