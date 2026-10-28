#include "MarsPresets.h"

#include "Parameters.h"
#include "dsp/effects/Mars.h"

namespace
{
    using Style = effects::Mars::Style;

    // One band's settings that differ from the defaults (warm tape, DRIVE 30%).
    struct BandSettings
    {
        Style style;
        float drive;
        std::vector<std::pair<const char*, float>> others;
    };

    // A factory preset: its bands (from the lowest), the crossovers between
    // them, and the global settings that differ from the defaults.
    StompPreset make (const juce::String& category, const juce::String& name, std::initializer_list<BandSettings> bands,
                      std::initializer_list<float> crossovers = {}, std::initializer_list<std::pair<const char*, float>> globals = {})
    {
        StompPreset preset;
        preset.category = category;
        preset.name = name;
        preset.isFactory = true;
        preset.parameters[ParamIDs::marsBands] = (float) juce::jmax (0, (int) bands.size() - 1);
        int b = 0;
        for (const auto& band : bands)
        {
            preset.parameters[ParamIDs::marsBand (b, "Style")] = (float) band.style;
            preset.parameters[ParamIDs::marsBand (b, "Drive")] = band.drive;
            for (const auto& [part, value] : band.others)
                preset.parameters[ParamIDs::marsBand (b, part)] = value;
            ++b;
        }
        const char* crossoverIds[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
        int c = 0;
        for (const auto hz : crossovers)
            preset.parameters[crossoverIds[c++]] = hz;
        for (const auto& [id, value] : globals)
            preset.parameters[id] = value;
        return preset;
    }

    // QUALITY by index: mastering presets run at Superb (8x), the cleanest.
    constexpr std::pair<const char*, float> superb { ParamIDs::marsQuality, 2.0f };
    const BandSettings clean { Style::cleanTube, 0.0f, { { "On", 0.0f } } };   // a band left untouched
}

bool MarsPresetTraits::holds (const juce::String& id)
{
    return id.startsWith ("mars") && id != ParamIDs::marsOn;
}

std::vector<StompPreset> MarsPresetTraits::makeFactory()
{
    // Our own starting points: subtle on the master and mix bus, stronger on
    // single sources. MARS keeps the level roughly even as DRIVE goes up;
    // where a preset still came out louder or quieter (measured on tones
    // and on pink noise), its OUTPUT makes up for it, so switching it in is
    // a fair comparison. Tuned by numbers, not by ear.
    using namespace ParamIDs;
    return {
        make ("", "Default", { { Style::warmTape, 0.3f } }),

        // Mastering: a touch of colour on the whole mix.
        make ("Mastering", "Tape Warmth", { { Style::warmTape, 0.15f } }, {}, { superb }),
        make ("Mastering", "Tube Glow", { { Style::cleanTube, 0.2f } }, {}, { superb, { marsMix, 0.6f } }),
        make ("Mastering", "Transformer Weight", { { Style::subtleTransformer, 0.25f } }, {}, { superb, { marsOutput, -1.5f } }),
        make ("Mastering", "Three-Band Master",
              { { Style::warmTransformer, 0.2f }, { Style::cleanTape, 0.12f }, { Style::cleanTube, 0.15f } }, { 120.0f, 5000.0f }, { superb, { marsOutput, -3.0f } }),
        make ("Mastering", "Air Exciter", { clean, { Style::cleanTube, 0.35f, { { "Mix", 0.5f }, { "Presence", 2.0f } } } }, { 6000.0f }, { superb }),
        make ("Mastering", "Clean Lows, Warm Top", { clean, { Style::warmTape, 0.25f } }, { 150.0f }, { superb }),

        // The mix bus.
        make ("Mix Bus", "Glue Tape", { { Style::warmTape, 0.25f, { { "Dynamics", 0.2f } } } }, {}, { { marsOutput, -0.5f } }),
        make ("Mix Bus", "Analog Console", { { Style::subtleTransformer, 0.3f } }, {}, { { marsOutput, -1.5f } }),
        make ("Mix Bus", "Rock Bus Grit", { { Style::saturatedTape, 0.3f, { { "Mix", 0.6f } } } }, {}, { { marsOutput, 0.5f } }),

        // Single sources.
        make ("Drums", "Drum Smash", { { Style::saturatedTape, 0.6f, { { "Dynamics", 0.5f }, { "Mix", 0.6f } } } }, {}, { { marsOutput, 1.0f } }),
        make ("Drums", "Kick & Snare Crunch", { { Style::warmTube, 0.4f }, { Style::crunchyAmp, 0.35f, { { "Mix", 0.5f } } } }, { 200.0f }),
        make ("Drums", "Room Crush", { { Style::extremeTape, 0.7f, { { "Dynamics", 0.8f }, { "Mix", 0.35f } } } }),
        make ("Bass", "Bass Growl", { clean, { Style::britishAmp, 0.5f } }, { 250.0f }, { { marsOutput, -1.7f } }),
        make ("Bass", "Warm DI", { { Style::warmTube, 0.35f } }),
        make ("Bass", "Fuzz Top", { clean, { Style::screamingAmp, 0.6f, { { "Mix", 0.5f } } } }, { 300.0f }, { { marsOutput, -0.5f } }),
        make ("Guitars", "Amp Edge", { { Style::crunchyAmp, 0.3f, { { "Mix", 0.5f } } } }, {}, { { marsOutput, 2.0f } }),
        make ("Guitars", "Thicken", { { Style::warmTransformer, 0.4f } }, {}, { { marsOutput, -2.0f } }),
        make ("Synths & Keys", "Lush Keys", { { Style::lushTransformer, 0.4f } }, {}, { { marsOutput, -2.0f } }),
        make ("Synths & Keys", "Synth Grit", { { Style::brokenTube, 0.5f, { { "Mix", 0.6f } } } }),
        make ("Vocals", "Vocal Presence", { { Style::cleanTape, 0.15f }, { Style::warmTube, 0.35f, { { "Presence", 1.5f } } } }, { 2500.0f }),
        make ("Vocals", "Aggressive Vocal", { { Style::saturatedTube, 0.55f, { { "Mix", 0.7f } } } }),

        // Effects.
        make ("Creative", "Lo-Fi Radio", { { Style::smudge, 0.6f, { { "Bass", -10.0f }, { "Treble", -8.0f } } } }, {}, { { marsOutput, 1.5f } }),
        make ("Creative", "Octave Down", { { Style::breakdown, 0.8f, { { "Mix", 0.5f } } } }, {}, { { marsOutput, 0.5f } }),
        make ("Creative", "Destroyed", { { Style::destroy, 0.7f, { { "Mix", 0.5f } } } }),
    };
}
