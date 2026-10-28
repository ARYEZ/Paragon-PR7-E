#pragma once

#include "StompPresets.h"

// DOMINION's own presets (0.10.3), apart from PR7-X's rigs: a preset holds
// only DOMINION's settings (MODE, DEPTH, DETAIL, the timing, tilts, stereo,
// scale, output and the depth curve), never its on switch or anything else
// in the rig. Yours are .pr7dom files in "Dominion Presets" beside the rigs.
struct DominionPresetTraits
{
    static constexpr const char* fileExtension = ".pr7dom";
    static constexpr const char* xmlTag = "DominionPreset";
    static bool holds (const juce::String& parameterId);   // every dom* parameter but domOn
    static std::vector<StompPreset> makeFactory();
};

using DominionPreset = StompPreset;
using DominionPresets = StompPresets<DominionPresetTraits>;
