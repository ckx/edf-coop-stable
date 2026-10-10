// EDF6Coop: the transport part (src/) and the game part (game/) as one plugin, EDF6Coop.dll.
#pragma once

namespace coop {

constexpr const wchar_t* kNameW = L"EDF6Coop";
// The plugins EDF6Coop replaced. Running next to either would hook the same game code twice.
constexpr const wchar_t* kReplacedPlugins[] = {L"EDF6DirectNet.dll", L"EDF6MultiSlot.dll"};

}  // namespace coop
