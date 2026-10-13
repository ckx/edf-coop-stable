#pragma once
#include "patches.h"

namespace multislot {

// Pickups on the radar in the color of their type: health white, all other pickups the game's green.
//
// The radar's update (HUiHudRader slot 2, 82B4D0) collects every unit of DropItemManager's item list into one
// vector of positions and draws them all in one color, green (1F779A0), with the call at 82BB3D:
// 82A890(radar, rdx, &positions, r9, color, sixth) draws one dot per position. With [Display] RadarItemColors=1
// (the default) that call comes here, and the dots are drawn in at most two calls to 82A890, one per color.
CallSite RadarItemDotsCall();

// Verifies the call, points it at the plugin through a stub page of its own; false (logged, nothing changed) when
// the site is not the expected code.
bool InstallRadarItemColors(unsigned char* base);

}  // namespace multislot
