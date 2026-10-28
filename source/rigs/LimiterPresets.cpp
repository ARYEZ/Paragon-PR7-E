#include "LimiterPresets.h"

#include "Parameters.h"
#include "dsp/effects/MasterLimiter.h"

namespace
{
    using Style = effects::limiter::Style;

    // A factory preset: its style, then the settings it changes from the defaults.
    StompPreset make (const juce::String& category, const juce::String& name, Style style,
                      std::initializer_list<std::pair<const char*, float>> settings)
    {
        StompPreset preset;
        preset.category = category;
        preset.name = name;
        preset.isFactory = true;
        preset.parameters[ParamIDs::limitMode] = (float) style;
        for (const auto& [id, value] : settings)
            preset.parameters[id] = value;
        return preset;
    }

    // Choices by index (effects::limiter's name lists).
    constexpr float x2 = 1.0f, x4 = 2.0f;
    constexpr float bits24 = 1.0f, bits16 = 5.0f;
    constexpr float optimized = 2.0f;
}

bool LimiterPresetTraits::holds (const juce::String& id)
{
    return (id.startsWith ("limit") && id != ParamIDs::limitOn && id != ParamIDs::limitUnity && id != ParamIDs::limitDelta
            && id != ParamIDs::limitThreshold && id != ParamIDs::limitMakeUp)
        || id == targetKey;
}

std::vector<StompPreset> LimiterPresetTraits::makeFactory()
{
    // Our own starting points, not copies of anyone's presets. GAIN is what
    // sets the loudness, and how much a mix needs depends on how loud it
    // already is: these GAINs suit a mix peaking around -6 dBFS. Turn it
    // until INTEGRATED reaches the TARGET (the mastering ones set it).
    using namespace ParamIDs;
    std::vector<StompPreset> presets {
        make ("", "Default", Style::modern, {}),

        // Mastering: by where the music is going.
        make ("Mastering", "Streaming (-14 LUFS)", Style::modern,
              { { limitGain, 4.0f }, { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { targetKey, -14.0f } }),
        make ("Mastering", "Loud Club (-9 LUFS)", Style::aggressive,
              { { limitGain, 8.0f }, { limitCeiling, -0.3f }, { limitTruePeak, 1.0f }, { limitOversampling, x4 }, { limitLookahead, 1.5f },
                { targetKey, -9.0f } }),
        make ("Mastering", "CD Master (16-bit)", Style::modern,
              { { limitGain, 5.0f }, { limitCeiling, -0.3f }, { limitTruePeak, 1.0f }, { limitDither, bits16 }, { limitShaping, optimized },
                { targetKey, -9.0f } }),
        make ("Mastering", "Hi-Res (24-bit)", Style::clear,
              { { limitGain, 3.0f }, { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { limitOversampling, x4 }, { limitDither, bits24 },
                { targetKey, -14.0f } }),
        make ("Mastering", "Gentle & Dynamic", Style::safe,
              { { limitGain, 2.0f }, { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { limitLookahead, 3.0f }, { limitRelease, 400.0f },
                { targetKey, -14.0f } }),
        make ("Mastering", "Vinyl Safe", Style::safe,
              { { limitGain, 1.0f }, { limitCeiling, -3.0f }, { limitTruePeak, 1.0f }, { limitLookahead, 3.0f }, { limitRelease, 500.0f },
                { limitDcFilter, 1.0f }, { targetKey, -14.0f } }),
        make ("Mastering", "Podcast (-16 LUFS)", Style::allRound,
              { { limitGain, 6.0f }, { limitCeiling, -1.5f }, { limitTruePeak, 1.0f }, { limitDcFilter, 1.0f }, { targetKey, -16.0f } }),
        make ("Mastering", "Broadcast EBU R128 (-23 LUFS)", Style::safe,
              { { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { limitLookahead, 2.0f }, { targetKey, -23.0f } }),
        make ("Mastering", "Broadcast ATSC A/85 (-24 LUFS)", Style::safe,
              { { limitCeiling, -2.0f }, { limitTruePeak, 1.0f }, { limitLookahead, 2.0f }, { targetKey, -24.0f } }),

        // The mix bus, before mastering.
        make ("Mix Bus", "Safety Net", Style::clear, { { limitCeiling, -0.3f } }),
        make ("Mix Bus", "Glue Bus", Style::glue, { { limitGain, 2.0f }, { limitCeiling, -1.0f }, { limitRelease, 300.0f } }),
        make ("Mix Bus", "Punchy Bus", Style::punchy, { { limitGain, 3.0f }, { limitCeiling, -1.0f }, { limitLookahead, 0.5f } }),
        make ("Mix Bus", "Transparent Loud", Style::modern,
              { { limitGain, 5.0f }, { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { limitOversampling, x2 } }),

        // On a track: catching peaks, not loudness (OUTPUT well under 0 dB).
        make ("Tracks", "Vocal Peak Catcher", Style::clear, { { limitGain, 3.0f }, { limitCeiling, -6.0f }, { limitLookahead, 2.0f } }),
        make ("Tracks", "Drum Bus Smash", Style::aggressive, { { limitGain, 6.0f }, { limitCeiling, -3.0f }, { limitLookahead, 0.0f } }),
        make ("Tracks", "Drum Transients Kept", Style::dynamic, { { limitGain, 3.0f }, { limitCeiling, -3.0f } }),
        make ("Tracks", "Bass Steady", Style::safe,
              { { limitGain, 4.0f }, { limitCeiling, -6.0f }, { limitLookahead, 3.0f }, { limitRelease, 150.0f } }),
        make ("Tracks", "808 Clip & Catch", Style::aggressive, { { limitGain, 4.0f }, { limitCeiling, -3.0f }, { limitOversampling, x4 } }),
    };

    // Signature sounds (0.19.0): each record-inspired set's master. How hard
    // it works follows the record's measured dynamic range (the DR database:
    // lower is louder and more limited): DR 4 hardest, DR 8 lightest. Where
    // no CD figure was published (the first and fifth), it's a guess. All
    // end at -1 dBTP, safe for streaming.
    const auto signature = [] (const char* sound, Style style, float gain, std::initializer_list<std::pair<const char*, float>> more)
    {
        auto preset = make (juce::String ("Signature Sounds/") + sound, juce::String (sound) + " Master", style,
                            { { limitGain, gain }, { limitCeiling, -1.0f }, { limitTruePeak, 1.0f }, { targetKey, -9.0f } });
        for (const auto& [id, value] : more)
            preset.parameters[id] = value;
        return preset;
    };
    for (auto&& preset : {
             signature ("Ethereal Djent", Style::modern, 4.0f, { { targetKey, -14.0f } }),                    // DR 11 on vinyl; CD not found: a guess
             signature ("Dark Synth Metal", Style::modern, 7.0f, { { limitOversampling, x2 } }),              // DR 4: very loud
             signature ("Raw 90s Nu Metal", Style::allRound, 3.0f, { { limitRelease, 300.0f }, { targetKey, -14.0f } }),   // DR 8: raw, open
             signature ("Clean Prog Djent", Style::modern, 5.0f, { { limitOversampling, x4 }, { limitLookahead, 1.5f } }),   // DR 6, clinical
             signature ("Cinematic Post-Hardcore", Style::modern, 5.0f, { { limitOversampling, x2 } }),       // no figure found: a guess
             signature ("Swedish Melodic Death", Style::modern, 6.5f, {}),                                     // DR 5: loud
             signature ("Early 2000s Metalcore", Style::punchy, 4.5f, {}),                                     // DR 7
             signature ("Polished Nu Metal", Style::modern, 5.5f, { { limitLookahead, 1.0f } }),              // DR 6
         })
        presets.push_back (preset);
    return presets;
}
