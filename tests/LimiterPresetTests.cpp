// The LIMITER's own presets (0.19.0): what they hold, what loading one does,
// your own, and that they do what they say on drums.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/CompPresets.h"
#include "rigs/DominionPresets.h"
#include "rigs/LimiterPresets.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // The LIMITER page's TARGET menu.
    const std::vector<float> pageTargets { -9.0f, -14.0f, -16.0f, -23.0f, -24.0f };
}

TEST_CASE ("LIMITER presets: the factory ones hold only the LIMITER's settings, all in range", "[limit][presets]")
{
    ParagonX1AudioProcessor processor;
    REQUIRE (LimiterPresets::factory().front().name == "Default");
    juce::StringArray names;
    for (const auto& preset : LimiterPresets::factory())
    {
        INFO (preset.name);
        names.addIfNotAlreadyThere (preset.name);
        for (const auto& [id, value] : preset.parameters)
        {
            INFO (id);
            CHECK (LimiterPresets::holds (id));
            if (id == LimiterPresetTraits::targetKey)
            {
                CHECK (std::find (pageTargets.begin(), pageTargets.end(), value) != pageTargets.end());   // one the page offers
                continue;
            }
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);
            CHECK (value >= parameter->getNormalisableRange().start);
            CHECK (value <= parameter->getNormalisableRange().end);
        }
        if (preset.category == "Mastering")
            CHECK (preset.parameters.count (LimiterPresetTraits::targetKey) == 1);
    }
    CHECK (names.size() == (int) LimiterPresets::factory().size());
    CHECK (names.size() >= 20);
    for (const auto* id : { ParamIDs::limitOn, ParamIDs::limitUnity, ParamIDs::limitDelta, ParamIDs::limitThreshold, ParamIDs::eqOn })
        CHECK_FALSE (LimiterPresets::holds (id));
    CHECK (LimiterPresets::holds (ParamIDs::limitGain));
    CHECK (LimiterPresets::holds (ParamIDs::limitTrim));
}

TEST_CASE ("Stomp presets: each category is one run in its menu (a heading each), Default first", "[presets]")
{
    // The PRESET menu starts a new heading or submenu whenever the category
    // changes: a category split in two would show twice.
    for (const auto* presets : { &CompPresets::factory(), &DominionPresets::factory(), &LimiterPresets::factory() })
    {
        CHECK (presets->front().name == "Default");
        juce::StringArray seen;
        juce::String last = "-";
        for (const auto& preset : *presets)
        {
            INFO (preset.name);
            if (preset.category == last)
                continue;
            CHECK_FALSE (seen.contains (preset.category));
            seen.add (preset.category);
            last = preset.category;
        }
    }
}

TEST_CASE ("LIMITER presets: loading one sets the LIMITER and its TARGET, nothing else, and one undo takes it back", "[limit][presets]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    const auto set = [&] (const char* id, float value)
    {
        auto* p = state.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    };
    const auto value = [&] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    set (ParamIDs::eqOn, 1.0f);
    set (ParamIDs::limitOn, 1.0f);
    set (ParamIDs::limitAttack, 80.0f);   // not in the preset: back to its default
    processor.flushUndoStep();

    const auto* club = processor.getLimiterPresets().findFactory ("Loud Club (-9 LUFS)");
    REQUIRE (club != nullptr);
    processor.loadLimiterPreset (*club);
    processor.flushUndoStep();
    CHECK (juce::roundToInt (value (ParamIDs::limitMode)) == (int) effects::limiter::Style::aggressive);
    CHECK_THAT (value (ParamIDs::limitGain), WithinAbs (8.0, 0.01));
    CHECK_THAT (value (ParamIDs::limitCeiling), WithinAbs (-0.3, 0.01));
    CHECK_THAT (value (ParamIDs::limitAttack), WithinAbs (20.0, 0.01));
    CHECK ((float) state.state.getProperty (LimiterPresetTraits::targetKey) == -9.0f);   // the page's TARGET
    CHECK (value (ParamIDs::limitOn) > 0.5f);   // its switch, and the rest of the rig, are left alone
    CHECK (value (ParamIDs::eqOn) > 0.5f);
    CHECK (processor.getLimiterPresetName() == "Loud Club (-9 LUFS)");
    CHECK_FALSE (processor.isLimiterPresetModified());
    set (ParamIDs::limitGain, 3.0f);
    CHECK (processor.isLimiterPresetModified());

    processor.flushUndoStep();
    processor.undo();
    processor.undo();
    CHECK_THAT (value (ParamIDs::limitAttack), WithinAbs (80.0, 0.5));
    CHECK_THAT (value (ParamIDs::limitGain), WithinAbs (0.0, 0.01));
}

TEST_CASE ("LIMITER presets: yours are saved (with the TARGET), listed, loaded and deleted", "[limit][presets]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::createTempFile ("limiter-presets");
    processor.getLimiterPresets().setFolder (folder);
    auto& state = processor.getParameters();
    state.getParameter (ParamIDs::limitGain)->setValueNotifyingHost (state.getParameter (ParamIDs::limitGain)->convertTo0to1 (5.5f));

    auto& presets = processor.getLimiterPresets();
    auto mine = LimiterPresets::capture (state, "My Master");
    mine.parameters[LimiterPresetTraits::targetKey] = -16.0f;
    REQUIRE (presets.save (mine));
    const auto yours = presets.user();
    REQUIRE (yours.size() == 1);
    CHECK (yours[0].name == "My Master");
    CHECK (yours[0].parameters.count (ParamIDs::limitOn) == 0);
    CHECK (yours[0].parameters.at (LimiterPresetTraits::targetKey) == -16.0f);

    processor.loadLimiterPreset (*presets.findFactory ("Default"));
    CHECK_THAT (state.getRawParameterValue (ParamIDs::limitGain)->load(), WithinAbs (0.0, 0.01));
    processor.loadLimiterPreset (yours[0]);
    CHECK_THAT (state.getRawParameterValue (ParamIDs::limitGain)->load(), WithinAbs (5.5, 0.01));
    CHECK ((float) state.state.getProperty (LimiterPresetTraits::targetKey) == -16.0f);

    CHECK (presets.remove (yours[0]));
    CHECK (presets.user().empty());
    folder.deleteRecursively();
}

TEST_CASE ("LIMITER presets: each keeps loud drums under its own ceiling, and makes them louder by its GAIN", "[limit][presets]")
{
    for (const auto& preset : LimiterPresets::factory())
    {
        INFO (preset.name);
        ParagonX1AudioProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        processor.getParameters().getParameter (ParamIDs::limitOn)->setValueNotifyingHost (1.0f);
        processor.loadLimiterPreset (preset);
        processor.updateLatency();

        // Drum-like hits peaking at -2 dBFS over a quieter bed, two seconds.
        juce::Random random (5);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        double inSum = 0.0, outSum = 0.0;
        for (int from = 0; from < 96000; from += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = from + i;
                const auto hit = 0.79f * std::exp (-(float) (n % 12000) / 48000.0f * 25.0f);
                const auto x = (hit + 0.05f) * (random.nextFloat() * 2.0f - 1.0f);
                block.setSample (0, i, x);
                block.setSample (1, i, x);
                if (n >= 48000)
                    inSum += (double) x * x;
            }
            processor.processBlock (block, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto y = block.getSample (0, i);
                REQUIRE (std::isfinite (y));
                if (from + i >= 48000)
                {
                    peak = juce::jmax (peak, std::abs (y));
                    outSum += (double) y * y;
                }
            }
        }
        const auto value = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); };
        const auto ceilingDb = value (ParamIDs::limitCeiling) + value (ParamIDs::limitTrim);
        const auto ditherStep = value (ParamIDs::limitDither) > 0.5f ? 1.0f / 32768.0f : 0.0f;   // 16 bits at worst
        CHECK (juce::Decibels::gainToDecibels (peak) <= ceilingDb + 0.01f + juce::Decibels::gainToDecibels (1.0f + 2.0f * ditherStep / peak));
        // Louder by roughly its GAIN, less what the limiting takes off the hits (never quieter than 3 dB down).
        const auto changeDb = 10.0 * std::log10 (outSum / inSum);
        INFO ("loudness change " << changeDb << " dB, GAIN " << value (ParamIDs::limitGain));
        CHECK (changeDb <= value (ParamIDs::limitGain) + 0.5);
        CHECK (changeDb >= -3.0 + juce::jmin (0.0f, ceilingDb + 2.0f));
    }
}
