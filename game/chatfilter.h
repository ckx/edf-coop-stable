#pragma once
#include <cstdint>
#include <vector>

#include "patches.h"

namespace multislot {

// The game's chat word filter, off.
//
// Chat text goes through one function, EDF+7213B0(tables, std::wstring* text) -> bool: 720500 and 721410 match the
// text against the word tables of the chat object (pc_VerifyStrings 79A660 names their file for its own copy,
// app:/Network/chat_message.%LOCALE%.sgo at 17E90B0), replace every listed word with '*' (1761DE4) in place and
// return whether anything matched. It runs on chat text at three sites:
//   - 722BD5 in the send path 722AD0: the message, copied to a local wstring, before 7236C0 encodes and sends it (the
//     result is not read);
//   - 7230D7 in the receive path 722FB0: the decoded message, before the chat widgets get it (the result is not read);
//   - 869F8C in the text entry's confirm (HUiChat, 869EA0): a match keeps the filtered text (+0xA68) and builds the
//     Room_EditChatBan / Room_InputChatBan notice (17FF300, 17FF328); no match continues at 86A79F.
// With [Chat] WordFilter=0 (the default) each call becomes `xor eax, eax`: no match, the text untouched. The fourth
// caller, pc_VerifyStrings::Verify (79ACE0) behind net::RoomVerify's free-word checks of the room settings, is left
// alone. Nothing new is sent; a player without the plugin still filters what their game receives.

constexpr std::uint32_t kChatWordFilter = 0x7213B0;
// What replaces each call: `xor eax, eax` and a three-byte nop.
constexpr std::uint8_t kChatFilterNoMatch[5] = {0x31, 0xC0, 0x0F, 0x1F, 0x00};
std::vector<CallSite> ChatFilterCalls();

// Verifies every call, then replaces them all; false (logged, nothing changed) when a site is not the expected code.
bool InstallChatFilterOff(unsigned char* base);

}  // namespace multislot
