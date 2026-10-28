#pragma once

#include "StompPresets.h"

// COMP's own presets (0.14.0), apart from PR7-X's rigs: a preset holds
// COMP's settings (the full band and its switch, the bands, LOOK-AHEAD and
// MIX), never its on switch or anything else in the rig. Yours are .pr7comp
// files in "Comp Presets" beside the rigs.
struct CompPresetTraits
{
    static constexpr const char* fileExtension = ".pr7comp";
    static constexpr const char* xmlTag = "CompPreset";
    static bool holds (const juce::String& parameterId);   // every comp* parameter but compOn, and every mb* one
    static std::vector<StompPreset> makeFactory();
};

using CompPreset = StompPreset;
using CompPresets = StompPresets<CompPresetTraits>;
