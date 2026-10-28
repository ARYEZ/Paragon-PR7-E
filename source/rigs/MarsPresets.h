#pragma once

#include "StompPresets.h"

// MARS's own presets (0.22.0), as the other stomps': a preset holds every
// MARS setting but its on switch. Yours are .pr7mars files in "Mars Presets"
// beside the rigs.
struct MarsPresetTraits
{
    static constexpr const char* fileExtension = ".pr7mars";
    static constexpr const char* xmlTag = "MarsPreset";
    static bool holds (const juce::String& parameterId);
    static std::vector<StompPreset> makeFactory();
};

using MarsPreset = StompPreset;
using MarsPresets = StompPresets<MarsPresetTraits>;
