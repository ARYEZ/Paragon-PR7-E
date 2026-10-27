// The LIMITER stomp (0.8.0). The first tests are PR7-C's limiter tests
// (tests/LimiterTests.cpp there), ported: the same limiter, so the same
// behaviour. The rest check how it sits in PR7-E's chain.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/FactoryRigs.h"
#include "ui/EffectPanel.h"
#include "ui/LimiterPage.h"
#include "ui/Footswitch.h"

#include <optional>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    void set (ParagonX1AudioProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    std::unique_ptr<ParagonX1AudioProcessor> makeLimiter (float thresholdDb, int mode = 0, int inputs = 2)
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        processor->setPlayConfigDetails (inputs, 2, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        set (*processor, ParamIDs::limitOn, 1.0f);
        set (*processor, ParamIDs::limitCeiling, thresholdDb);   // GAIN 0: the ceiling is PR7-C's threshold
        set (*processor, ParamIDs::limitMode, (float) mode);
        return processor;
    }

    juce::AudioBuffer<float> sine (float hz, float levelDb, int numSamples, std::optional<float> rightLevelDb = {})
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto level = juce::Decibels::decibelsToGain (channel == 1 && rightLevelDb ? *rightLevelDb : levelDb);
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (channel, i, level * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) sampleRate));
        }
        return buffer;
    }

    // Drum-like hits: bursts of noise that jump straight to full level and
    // decay, every 250 ms, peaking near 0 dBFS.
    juce::AudioBuffer<float> drumHits (int numSamples)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        juce::Random random (42);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto sinceHit = (float) (i % 12000) / (float) sampleRate;
            const auto envelope = std::exp (-sinceHit * 25.0f);
            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, i, envelope * (random.nextFloat() * 2.0f - 1.0f));
        }
        return buffer;
    }

    void process (ParagonX1AudioProcessor& processor, juce::AudioBuffer<float>& buffer, const std::function<void (int)>& atBlock = {})
    {
        juce::MidiBuffer midi;
        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            if (atBlock)
                atBlock (start);
            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start,
                                            juce::jmin (blockSize, buffer.getNumSamples() - start));
            processor.processBlock (block, midi);
        }
    }

    float settledChangeDb (ParagonX1AudioProcessor& processor, float levelDb, int channel = 0, std::optional<float> rightLevelDb = {})
    {
        const auto input = sine (1000.0f, levelDb, 48000, rightLevelDb);
        auto output = input;
        process (processor, output);
        return juce::Decibels::gainToDecibels (output.getRMSLevel (channel, 24000, 24000) / input.getRMSLevel (channel, 24000, 24000));
    }

    float peakDb (const juce::AudioBuffer<float>& buffer)
    {
        return juce::Decibels::gainToDecibels (juce::jmax (buffer.getMagnitude (0, 0, buffer.getNumSamples()),
                                                           buffer.getMagnitude (1, 0, buffer.getNumSamples())));
    }

    float sharpestCorner (const juce::AudioBuffer<float>& buffer, int from, int to)
    {
        float sharpest = 0.0f;
        const auto* x = buffer.getReadPointer (0);
        for (int i = juce::jmax (2, from); i < to; ++i)
            sharpest = juce::jmax (sharpest, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        return sharpest;
    }
}

//==============================================================================
// PR7-C's tests
TEST_CASE ("Limiter: below the threshold, nothing changes", "[limit]")
{
    auto limiter = makeLimiter (-6.0f);
    CHECK (std::abs (settledChangeDb (*limiter, -12.0f)) < 0.01f);
}

TEST_CASE ("Limiter: no mode lets a peak past the threshold", "[limit]")
{
    for (const auto mode : { 0, 1, 2 })   // BRICK, SOFT and AUTO
    {
        DYNAMIC_SECTION ("mode " << mode)
        {
            auto limiter = makeLimiter (-10.0f, mode);
            auto hits = drumHits (96000);
            REQUIRE (peakDb (hits) > -1.0f);
            process (*limiter, hits);
            CHECK (peakDb (hits) <= -10.0f + 0.001f);
        }
    }
}

TEST_CASE ("Limiter: the ceiling rises with MAKE UP (from 0.16.0: GAIN into OUTPUT)", "[limit]")
{
    // PR7-C's THRESHOLD -10 dB and MAKE UP +6 dB: GAIN +6 dB into an OUTPUT of -4 dB.
    auto limiter = makeLimiter (-4.0f);
    set (*limiter, ParamIDs::limitGain, 6.0f);
    auto hits = drumHits (96000);
    process (*limiter, hits);
    CHECK_THAT (peakDb (hits), WithinAbs (-4.0, 0.3));
}

TEST_CASE ("Limiter: a steady tone over the threshold comes out at the threshold", "[limit]")
{
    auto limiter = makeLimiter (-12.0f);
    CHECK_THAT (settledChangeDb (*limiter, -6.0f), WithinAbs (-6.0, 0.3));
    CHECK_THAT (limiter->getLimiterReductionDb (0), WithinAbs (6.0, 0.3));
}

TEST_CASE ("Limiter: SOFT eases in just below the threshold; BRICK doesn't", "[limit]")
{
    auto soft = makeLimiter (-10.0f, 1);
    CHECK (settledChangeDb (*soft, -11.0f) < -0.2f);
    auto brick = makeLimiter (-10.0f, 0);
    CHECK (std::abs (settledChangeDb (*brick, -11.0f)) < 0.01f);
}

TEST_CASE ("Limiter: AUTO lets go quickly after a short peak, slowly under steady limiting", "[limit]")
{
    const auto recoveryMs = [] (int burstSamples)
    {
        auto limiter = makeLimiter (-20.0f, 2);
        const auto total = burstSamples + 48000;
        auto buffer = sine (1000.0f, -30.0f, total);
        for (int channel = 0; channel < 2; ++channel)
            buffer.applyGain (channel, 0, burstSamples, juce::Decibels::decibelsToGain (24.0f));   // the burst at -6, 14 dB over
        process (*limiter, buffer);
        for (int start = burstSamples; start + 480 < total; start += 480)
            if (buffer.getRMSLevel (0, start, 480) > juce::Decibels::decibelsToGain (-31.0f) * 0.7071f)
                return (float) (start - burstSamples) / 48.0f;
        return 1000.0f;
    };
    CHECK (recoveryMs (1200) < recoveryMs (96000) * 0.5f);   // after 25 ms of limiting, against 2 s
}

TEST_CASE ("Limiter: MIX blends the limited sound with the dry", "[limit]")
{
    auto limiter = makeLimiter (-20.0f);
    set (*limiter, ParamIDs::limitMix, 50.0f);
    // Half the dry -6 dBFS tone and half the limited -20 dBFS one, in phase.
    const auto expected = juce::Decibels::gainToDecibels (0.5f + 0.5f * juce::Decibels::decibelsToGain (-14.0f));
    CHECK_THAT (settledChangeDb (*limiter, -6.0f), WithinAbs (expected, 0.3));
}

TEST_CASE ("Limiter: LINK limits both sides together", "[limit]")
{
    SECTION ("linked")
    {
        auto limiter = makeLimiter (-20.0f);
        CHECK (settledChangeDb (*limiter, -6.0f, 1, -24.0f) < -12.0f);
    }
    SECTION ("unlinked")
    {
        auto limiter = makeLimiter (-20.0f);
        set (*limiter, ParamIDs::limitLink, 0.0f);
        CHECK (std::abs (settledChangeDb (*limiter, -6.0f, 1, -24.0f)) < 0.01f);
    }
}

TEST_CASE ("Limiter: switching it on, or between modes, doesn't click", "[limit]")
{
    const auto check = [] (int fromMode, const std::function<void (ParagonX1AudioProcessor&)>& change, bool startOff)
    {
        auto limiter = makeLimiter (-20.0f, fromMode);
        if (startOff)
            set (*limiter, ParamIDs::limitOn, 0.0f);
        auto buffer = sine (200.0f, -14.0f, 48000);
        process (*limiter, buffer, [&] (int start) { if (start == 24064) change (*limiter); });
        const auto steady = juce::jmax (sharpestCorner (buffer, 12000, 24000), sharpestCorner (buffer, 40000, 48000));
        CHECK (sharpestCorner (buffer, 24000, 40000) < steady * 3.0f);
    };
    SECTION ("on") { check (0, [] (auto& l) { set (l, ParamIDs::limitOn, 1.0f); }, true); }
    SECTION ("BRICK to SOFT") { check (0, [] (auto& l) { set (l, ParamIDs::limitMode, 1.0f); }, false); }
    SECTION ("SOFT to BRICK") { check (1, [] (auto& l) { set (l, ParamIDs::limitMode, 0.0f); }, false); }
    SECTION ("BRICK to AUTO") { check (0, [] (auto& l) { set (l, ParamIDs::limitMode, 2.0f); }, false); }
}

//==============================================================================
// In PR7-E
TEST_CASE ("Limiter in PR7-E: last in the chain, so turning OUTPUT up can't push a peak past it", "[limit]")
{
    auto limiter = makeLimiter (-3.0f);
    set (*limiter, ParamIDs::outputGain, 12.0f);
    set (*limiter, ParamIDs::rigLevel, 6.0f);
    auto hits = drumHits (96000);
    process (*limiter, hits);
    CHECK (peakDb (hits) <= -3.0f + 0.001f);
    CHECK (limiter->getLatencySamples() == 0);   // and it adds no delay
}

TEST_CASE ("Limiter in PR7-E: OUTPUT TRIM comes after it (moving the ceiling), only while it's on", "[limit]")
{
    auto limiter = makeLimiter (-10.0f);
    set (*limiter, ParamIDs::limitTrim, -4.0f);
    auto hits = drumHits (96000);
    process (*limiter, hits);
    CHECK_THAT (peakDb (hits), WithinAbs (-14.0, 0.3));

    // Below the threshold, the trim is simply a level change.
    CHECK_THAT (settledChangeDb (*limiter, -20.0f), WithinAbs (-4.0, 0.05));

    // Switched off: no limiting and no trim.
    set (*limiter, ParamIDs::limitOn, 0.0f);
    CHECK (std::abs (settledChangeDb (*limiter, -3.0f)) < 0.01f);
}

TEST_CASE ("Limiter in PR7-E: off by default, leaving the sound alone; a mono track is limited on both speakers", "[limit]")
{
    ParagonX1AudioProcessor fresh;
    fresh.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    fresh.prepareToPlay (sampleRate, blockSize);
    auto hits = drumHits (48000);
    const auto before = hits;
    process (fresh, hits);
    for (int i = 0; i < hits.getNumSamples(); ++i)
        REQUIRE_THAT (hits.getSample (0, i), WithinAbs (before.getSample (0, i), 1.0e-6));

    auto mono = makeLimiter (-12.0f, 0, 1);
    auto buffer = drumHits (48000);
    process (*mono, buffer);
    CHECK (peakDb (buffer) <= -12.0f + 0.001f);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        REQUIRE_THAT (buffer.getSample (1, i), WithinAbs (buffer.getSample (0, i), 1.0e-7));
}

TEST_CASE ("Limiter in PR7-E: the mastering factory rigs hold a -1 dB ceiling", "[limit][rigs]")
{
    for (const auto& rig : makeFactoryRigs())
    {
        if (rig.bank != 4)   // E: mastering
            continue;
        INFO (rig.name);
        ParagonX1AudioProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        const auto* entry = processor.getRigLibrary().findFactory (rig.name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
        auto hits = drumHits (96000);
        process (processor, hits);
        CHECK (peakDb (hits) <= -1.0f + 0.001f);
    }
}

TEST_CASE ("Limiter in PR7-E: its rack unit (row 4) switches it, and its POWER name opens its own page", "[limit][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Footswitch*> stomps;
    LimiterPage* page = nullptr;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        if (auto* stomp = dynamic_cast<Footswitch*> (&c))
            stomps.push_back (stomp);
        if (auto* p = dynamic_cast<LimiterPage*> (&c))
            page = p;
        for (auto* child : c.getChildren())
            collect (*child);
    };
    collect (*editor);
    REQUIRE (stomps.size() == 4);   // the rack units' POWER switches, in the chain's order
    REQUIRE (page != nullptr);
    CHECK_FALSE (stomps[3]->isPlaceholder());
    CHECK_FALSE (page->isVisible());
    stomps[3]->onSelect();
    CHECK (page->isVisible());
    stomps[3]->toggle();
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::limitOn)->load() > 0.5f);
}
