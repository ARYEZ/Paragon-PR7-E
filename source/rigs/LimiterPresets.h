#pragma once

#include "StompPresets.h"

// The LIMITER's own presets (0.19.0), apart from PR7-E's rigs: a preset
// holds the LIMITER's settings (style, GAIN, OUTPUT, the advanced and
// output settings), never its on switch, UNITY or DELTA (listening aids),
// or the old THRESHOLD and MAKE UP (GAIN and OUTPUT replaced them). Yours
// are .pr7lim files in "Limiter Presets" beside the rigs.
struct LimiterPresetTraits
{
    static constexpr const char* fileExtension = ".pr7lim";
    static constexpr const char* xmlTag = "LimiterPreset";
    static bool holds (const juce::String& parameterId);   // and targetKey
    // Not a parameter: the LIMITER page's loudness TARGET (LUFS), which a
    // mastering preset sets so the meter shows what to aim GAIN at.
    static constexpr const char* targetKey = "limitLoudnessTarget";
    static std::vector<StompPreset> makeFactory();
};

using LimiterPreset = StompPreset;
using LimiterPresets = StompPresets<LimiterPresetTraits>;
