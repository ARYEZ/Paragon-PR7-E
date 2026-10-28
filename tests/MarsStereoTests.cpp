// MARS and STEREO (0.22.0). MARS is PR7-V's saturation (inspired by Saturn
// 2), so its first tests are PR7-V's MARS tests (tests/EffectTests.cpp
// there), ported: the same engine, so the same behaviour. STEREO is PR7-C's
// mono maker and width, with PR7-C's stereo tests (tests/StereoTests.cpp
// there) ported. The rest check what's new here: both sides kept apart,
// the correlation meter and MONO listening.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/MarsPresets.h"

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

    std::unique_ptr<ParagonX1AudioProcessor> makeProcessor()
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        processor->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        return processor;
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

    std::vector<float> sine (double hz, float amplitude, double seconds)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    // RMS of the last part of a signal (skipping the first `skipSeconds`).
    float rms (const std::vector<float>& x, double skipSeconds)
    {
        const auto from = juce::jmin (x.size(), (size_t) (skipSeconds * sampleRate));
        double sum = 0.0;
        for (auto i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return (float) std::sqrt (sum / (double) juce::jmax ((size_t) 1, x.size() - from));
    }

    // The amplitude of one frequency (Goertzel), after `skipSeconds`.
    float levelAt (const std::vector<float>& x, double hz, double skipSeconds)
    {
        const auto from = (size_t) (skipSeconds * sampleRate);
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < x.size(); ++i)
        {
            const auto s0 = (double) x[i] + 2.0 * std::cos (w) * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto length = (double) (x.size() - from);
        return (float) (2.0 * std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2)) / length);
    }

    float dB (float ratio) { return juce::Decibels::gainToDecibels (ratio, -200.0f); }

    float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        float most = 0.0f;
        for (auto i = from; i < juce::jmin (a.size(), b.size()); ++i)
            most = juce::jmax (most, std::abs (a[i] - b[i]));
        return most;
    }

    bool allFinite (const std::vector<float>& x)
    {
        return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
    }

    // MARS on, everything else as a fresh PR7-X has it (all off).
    struct MarsRig
    {
        std::unique_ptr<ParagonX1AudioProcessor> processor = makeProcessor();
        MarsRig() { set (ParamIDs::marsOn, 1.0f); }
        void set (const char* id, float value) { ::set (*processor, id, value); }
        void band (int index, const char* part, float value) { ::set (*processor, ParamIDs::marsBand (index, part), value); }

        // Left and right through it; both sides come back.
        std::array<std::vector<float>, 2> run (const std::vector<float>& left, const std::vector<float>& right)
        {
            juce::AudioBuffer<float> buffer (2, (int) left.size());
            buffer.copyFrom (0, 0, left.data(), (int) left.size());
            buffer.copyFrom (1, 0, right.data(), (int) right.size());
            ::process (*processor, buffer);
            return { std::vector<float> (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples()),
                     std::vector<float> (buffer.getReadPointer (1), buffer.getReadPointer (1) + buffer.getNumSamples()) };
        }
        // The same on both sides (as PR7-V's mono tests had it): the left comes back.
        std::vector<float> runMono (const std::vector<float>& input) { return run (input, input)[0]; }
    };
}

TEST_CASE ("MARS: the bands split and add back up, and off is off", "[mars]")
{
    SECTION ("Switched off, it's exactly the input")
    {
        MarsRig rig;
        rig.set (ParamIDs::marsOn, 0.0f);
        rig.band (0, "Drive", 1.0f);
        const auto input = sine (440.0, 0.3f, 0.5);
        CHECK (maxAbsDifference (input, rig.runMono (input)) < 1.0e-6f);
    }

    SECTION ("MIX at 0: the sound as it was (in level and tone)")
    {
        for (const auto hz : { 100.0, 1000.0, 8000.0 })
        {
            MarsRig rig;
            rig.set (ParamIDs::marsMix, 0.0f);
            rig.band (0, "Drive", 1.0f);
            const auto input = sine (hz, 0.3f, 0.5);
            CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (0.0, 0.1));
        }
    }

    SECTION ("At every slope, four bands with their saturation off add back up flat")
    {
        for (int slope = 0; slope < effects::Mars::slopeNames.size(); ++slope)
            for (const auto hz : { 60.0, 250.0, 700.0, 2500.0, 5000.0, 12000.0 })
            {
                MarsRig rig;
                rig.set (ParamIDs::marsBands, 3.0f);   // 4 bands
                rig.set (ParamIDs::marsSlope, (float) slope);
                for (int b = 0; b < 4; ++b)
                    rig.band (b, "On", 0.0f);
                const auto input = sine (hz, 0.3f, 0.5);
                INFO (effects::Mars::slopeNames[slope] << " at " << hz << " Hz");
                CHECK_THAT (dB (rms (rig.runMono (input), 0.25) / rms (input, 0.25)), WithinAbs (0.0, 0.3));
            }
    }

    SECTION ("Each band is its own: drive the highs, and a low note stays clean")
    {
        const auto harmonicsOf = [] (double hz, int drivenBand)
        {
            MarsRig rig;
            rig.set (ParamIDs::marsBands, 1.0f);   // 2 bands
            rig.set (ParamIDs::marsCross1, 600.0f);
            for (int b = 0; b < 2; ++b)
            {
                rig.band (b, "Style", (float) effects::Mars::Style::saturatedTube);
                rig.band (b, "Drive", 1.0f);
                rig.band (b, "On", b == drivenBand ? 1.0f : 0.0f);   // the other band passes clean
            }
            const auto output = rig.runMono (sine (hz, 0.3f, 0.5));
            return dB ((levelAt (output, 2.0 * hz, 0.2) + levelAt (output, 3.0 * hz, 0.2)) / levelAt (output, hz, 0.2));
        };
        CHECK (harmonicsOf (2000.0, 1) > -30.0f);   // the high band, driven: rich
        CHECK (harmonicsOf (150.0, 1) < -40.0f);    // a low note, in the clean low band: clean
        CHECK (harmonicsOf (150.0, 0) > -30.0f);    // unless the low band is driven
    }
}

TEST_CASE ("MARS: every style saturates more with DRIVE, its level kept even", "[mars]")
{
    for (int style = 0; style < effects::Mars::numStyles; ++style)
    {
        INFO (effects::Mars::styleNames[style]);
        const auto run = [style] (float drive)
        {
            MarsRig rig;
            rig.band (0, "Style", (float) style);
            rig.band (0, "Drive", drive);
            return rig.runMono (sine (330.0, 0.3f, 0.6));
        };
        const auto light = run (0.1f), heavy = run (1.0f);
        REQUIRE (allFinite (heavy));
        // What isn't the note itself: the harmonics (and, for the FX styles, whatever else they make).
        const auto other = [] (const std::vector<float>& output)
        {
            const auto total = rms (output, 0.3), fundamental = levelAt (output, 330.0, 0.3) / std::sqrt (2.0f);
            return std::sqrt (std::max (0.0f, total * total - fundamental * fundamental)) / total;
        };
        CHECK (other (heavy) > other (light) + 0.02f);
        // Level matching: full DRIVE is within a few dB of the input, not 36 dB louder.
        CHECK (std::abs (dB (rms (heavy, 0.3) / rms (sine (330.0, 0.3f, 0.6), 0.3))) < 8.0f);
    }

    SECTION ("Breakdown pitches down: an octave at full DRIVE")
    {
        MarsRig rig;
        rig.band (0, "Style", (float) effects::Mars::Style::breakdown);
        rig.band (0, "Drive", 1.0f);
        const auto output = rig.runMono (sine (440.0, 0.3f, 0.8));
        CHECK (levelAt (output, 220.0, 0.4) > 0.2f * levelAt (output, 440.0, 0.4));
    }
}

TEST_CASE ("MARS: DYNAMICS, FEEDBACK, TONE, LEVEL and QUALITY do what they say", "[mars]")
{
    const auto clean = [] (MarsRig& rig)
    {
        rig.band (0, "Style", (float) effects::Mars::Style::cleanTube);
        rig.band (0, "Drive", 0.0f);
    };

    SECTION ("DYNAMICS right compresses: loud and soft come out closer")
    {
        const auto spread = [&] (float dynamics)
        {
            const auto levelFor = [&] (float amplitude)
            {
                MarsRig rig;
                clean (rig);
                rig.band (0, "Dynamics", dynamics);
                return rms (rig.runMono (sine (440.0, amplitude, 0.8)), 0.4);
            };
            return dB (levelFor (0.5f) / levelFor (0.02f));
        };
        CHECK (spread (1.0f) < spread (0.0f) - 10.0f);
    }

    SECTION ("DYNAMICS left gates: quiet sounds taken well down, loud ones through")
    {
        const auto through = [&] (float amplitude, float dynamics)
        {
            MarsRig rig;
            clean (rig);
            rig.band (0, "Dynamics", dynamics);
            const auto input = sine (440.0, amplitude, 0.8);
            return dB (rms (rig.runMono (input), 0.4) / rms (input, 0.4));
        };
        CHECK (through (0.003f, -1.0f) < through (0.003f, 0.0f) - 15.0f);
        CHECK_THAT (through (0.3f, -1.0f) - through (0.3f, 0.0f), WithinAbs (0.0, 1.0));
    }

    SECTION ("FEEDBACK rings at FEEDBACK FREQ")
    {
        const auto ringAt = [&] (float feedback)
        {
            MarsRig rig;
            rig.band (0, "Style", (float) effects::Mars::Style::warmTape);
            rig.band (0, "Feedback", feedback);
            rig.band (0, "FbFreq", 2000.0f);
            juce::Random random (7);
            std::vector<float> noise ((size_t) (0.8 * sampleRate));
            for (auto& sample : noise)
                sample = 0.1f * (random.nextFloat() * 2.0f - 1.0f);
            const auto output = rig.runMono (noise);
            return dB (levelAt (output, 2000.0, 0.3) / levelAt (output, 500.0, 0.3));
        };
        CHECK (ringAt (1.0f) > ringAt (0.0f) + 6.0f);
    }

    SECTION ("TONE: BASS and TREBLE shape what comes out")
    {
        const auto levelWith = [&] (const char* part, float db, double hz)
        {
            MarsRig rig;
            clean (rig);
            rig.band (0, part, db);
            return levelAt (rig.runMono (sine (hz, 0.1f, 0.5)), hz, 0.2);
        };
        CHECK_THAT (dB (levelWith ("Bass", 12.0f, 50.0) / levelWith ("Bass", 0.0f, 50.0)), WithinAbs (11.0, 1.5));
        CHECK_THAT (dB (levelWith ("Treble", -12.0f, 16000.0) / levelWith ("Treble", 0.0f, 16000.0)), WithinAbs (-11.0, 1.5));
        CHECK_THAT (dB (levelWith ("Treble", -12.0f, 300.0) / levelWith ("Treble", 0.0f, 300.0)), WithinAbs (0.0, 0.5));
    }

    SECTION ("LEVEL: a band all the way down is silent; OUTPUT moves everything")
    {
        MarsRig off;
        off.band (0, "Level", effects::Mars::minLevelDb);
        CHECK (rms (off.runMono (sine (440.0, 0.3f, 0.5)), 0.2) < 1.0e-4f);

        const auto outputLevel = [&] (float db)
        {
            MarsRig rig;
            clean (rig);
            rig.set (ParamIDs::marsOutput, db);
            return rms (rig.runMono (sine (440.0, 0.1f, 0.5)), 0.2);
        };
        CHECK_THAT (dB (outputLevel (-6.0f) / outputLevel (0.0f)), WithinAbs (-6.0, 0.2));
    }

    SECTION ("QUALITY: oversampling keeps the fold-back (aliasing) down")
    {
        // A 9 kHz note folded hard: its 5th harmonic, 45 kHz, folds back
        // down to 3 kHz without oversampling.
        const auto aliasAt3k = [&] (float quality)
        {
            MarsRig rig;
            rig.set (ParamIDs::marsQuality, quality);
            rig.band (0, "Style", (float) effects::Mars::Style::foldback);
            rig.band (0, "Drive", 0.7f);
            const auto output = rig.runMono (sine (9000.0, 0.3f, 0.5));
            return dB (levelAt (output, 3000.0, 0.2) / levelAt (output, 9000.0, 0.2));
        };
        CHECK (aliasAt3k (1.0f) < aliasAt3k (0.0f) - 15.0f);   // Good (4x) against Off
        CHECK (aliasAt3k (2.0f) <= aliasAt3k (1.0f) + 1.0f);   // Superb (8x) at least as clean
    }
}

TEST_CASE ("MARS: left and right stay apart", "[mars]")
{
    // A note on each side, driven hard: what the left makes (its harmonics)
    // stays on the left, and nothing of the right's note reaches it.
    MarsRig rig;
    rig.band (0, "Style", (float) effects::Mars::Style::saturatedTube);
    rig.band (0, "Drive", 1.0f);
    const auto [left, right] = rig.run (sine (440.0, 0.3f, 0.5), sine (660.0, 0.3f, 0.5));
    CHECK (dB (levelAt (left, 1320.0, 0.2) / levelAt (left, 440.0, 0.2)) > -40.0f);   // driven: its 3rd harmonic
    CHECK (dB (levelAt (left, 660.0, 0.2) / levelAt (left, 440.0, 0.2)) < -100.0f);    // none of the right's note
    CHECK (dB (levelAt (right, 440.0, 0.2) / levelAt (right, 660.0, 0.2)) < -100.0f);

    // A sound on the left only leaves the right silent.
    MarsRig oneSide;
    oneSide.band (0, "Drive", 1.0f);
    const auto [loud, silent] = oneSide.run (sine (440.0, 0.3f, 0.5), std::vector<float> ((size_t) (0.5 * sampleRate)));
    CHECK (rms (loud, 0.2) > 0.1f);
    CHECK (rms (silent, 0.0) < 1.0e-6f);

    // The processor's drive reading follows the louder side.
    CHECK (rig.processor->getMarsDrive (0) > 0.0f);
}

TEST_CASE ("MARS: a stereo track's mono sum keeps its level", "[mars]")
{
    // The same note on both sides, MARS on lightly: the level stays near
    // where it was (the automatic level matching), so switching it in to
    // compare is fair.
    MarsRig rig;
    rig.band (0, "Drive", 0.3f);
    const auto input = sine (220.0, 0.25f, 0.6);
    const auto [left, right] = rig.run (input, input);
    CHECK (std::abs (dB (rms (left, 0.3) / rms (input, 0.3))) < 3.0f);
    CHECK (maxAbsDifference (left, right) < 1.0e-6f);   // both sides alike: the same input, the same sound
}

namespace
{
    std::unique_ptr<ParagonX1AudioProcessor> makeStereo (bool mono, bool width)
    {
        auto processor = makeProcessor();
        set (*processor, ParamIDs::stereoOn, 1.0f);
        set (*processor, ParamIDs::stereoMonoOn, mono ? 1.0f : 0.0f);
        set (*processor, ParamIDs::stereoWidthOn, width ? 1.0f : 0.0f);
        return processor;
    }

    // A wide test signal: a tone in the middle (mid) and another only in the
    // difference between the sides (side), at each frequency given.
    juce::AudioBuffer<float> wide (float midHz, float sideHz, int numSamples)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (float) i / (float) sampleRate;
            const auto mid = 0.25f * std::sin (juce::MathConstants<float>::twoPi * midHz * t);
            const auto side = 0.25f * std::sin (juce::MathConstants<float>::twoPi * sideHz * t);
            buffer.setSample (0, i, mid + side);
            buffer.setSample (1, i, mid - side);
        }
        return buffer;
    }

    // The level (dB) of one frequency in the mid or side signal (Goertzel).
    float levelDb (const juce::AudioBuffer<float>& buffer, bool side, float hz, int from, int length)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < from + length; ++i)
        {
            const auto x = 0.5 * (buffer.getSample (0, i) + (side ? -1.0 : 1.0) * buffer.getSample (1, i));
            const auto s0 = x + 2.0 * std::cos (w) * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return juce::Decibels::gainToDecibels ((float) (2.0 * std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2)) / length), -200.0f);
    }

    // How STEREO changes the mid and side at two frequencies (dB).
    struct Image { float midChange, sideChange; };
    Image imageChange (ParagonX1AudioProcessor& processor, float midHz, float sideHz)
    {
        const auto input = wide (midHz, sideHz, 48000);
        auto output = input;
        process (processor, output);
        return { levelDb (output, false, midHz, 24000, 24000) - levelDb (input, false, midHz, 24000, 24000),
                 levelDb (output, true, sideHz, 24000, 24000) - levelDb (input, true, sideHz, 24000, 24000) };
    }

    float sharpestCorner (const juce::AudioBuffer<float>& buffer, int channel, int from, int to)
    {
        float sharpest = 0.0f;
        const auto* x = buffer.getReadPointer (channel);
        for (int i = juce::jmax (2, from); i < to; ++i)
            sharpest = juce::jmax (sharpest, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
        return sharpest;
    }
}

TEST_CASE ("STEREO mono maker: the side signal below the frequency goes, everything else stays", "[stereo]")
{
    const auto monoMaker = [] (float hz)
    {
        auto processor = makeStereo (true, false);
        set (*processor, ParamIDs::stereoMonoFreq, hz);
        return processor;
    };

    // The lows: the side is cut steeply (24 dB an octave), the mid keeps its level.
    const auto low = imageChange (*monoMaker (100.0f), 40.0f, 40.0f);
    CHECK (low.sideChange < -30.0f);
    CHECK_THAT (low.midChange, WithinAbs (0.0, 0.05));
    CHECK (imageChange (*monoMaker (100.0f), 50.0f, 50.0f).sideChange < -20.0f);   // an octave below

    // Above it, the image is as it was.
    const auto high = imageChange (*monoMaker (100.0f), 2000.0f, 1000.0f);
    CHECK_THAT (high.sideChange, WithinAbs (0.0, 0.05));
    CHECK_THAT (high.midChange, WithinAbs (0.0, 0.05));

    // A sound only on the left, well above the frequency, stays only on the
    // left: the mid and side are shifted alike, so they still cancel on the right.
    auto processor = monoMaker (200.0f);
    juce::AudioBuffer<float> buffer (2, 48000);
    for (int i = 0; i < 48000; ++i)
    {
        buffer.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 3000.0f * (float) i / (float) sampleRate));
        buffer.setSample (1, i, 0.0f);
    }
    process (*processor, buffer);
    CHECK (juce::Decibels::gainToDecibels (buffer.getRMSLevel (1, 24000, 24000) / buffer.getRMSLevel (0, 24000, 24000)) < -40.0f);
}

TEST_CASE ("STEREO width: 0 is mono, 200 doubles the side, and the mid never changes", "[stereo]")
{
    for (const auto [width, expectedSideDb] : { std::pair (0.0f, -200.0f), { 50.0f, -6.02f }, { 200.0f, 6.02f } })
    {
        DYNAMIC_SECTION ("width " << width)
        {
            auto processor = makeStereo (false, true);
            set (*processor, ParamIDs::stereoWidth, width);
            const auto change = imageChange (*processor, 500.0f, 700.0f);
            CHECK_THAT (change.midChange, WithinAbs (0.0, 0.01));
            if (width == 0.0f)
                CHECK (change.sideChange < -100.0f);
            else
                CHECK_THAT (change.sideChange, WithinAbs (expectedSideDb, 0.05));
        }
    }
}

TEST_CASE ("STEREO: off, and at 100 percent, leaves the audio as it was", "[stereo]")
{
    // Off (its POWER), with both halves on and their knobs moved: untouched.
    auto processor = makeProcessor();
    set (*processor, ParamIDs::stereoWidth, 180.0f);
    set (*processor, ParamIDs::stereoMonoFreq, 300.0f);
    const auto input = wide (60.0f, 80.0f, 24000);
    auto output = input;
    process (*processor, output);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < input.getNumSamples(); ++i)
            REQUIRE (juce::exactlyEqual (input.getSample (channel, i), output.getSample (channel, i)));

    processor = makeStereo (false, true);
    output = input;
    process (*processor, output);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < input.getNumSamples(); ++i)
            REQUIRE_THAT (output.getSample (channel, i), WithinAbs (input.getSample (channel, i), 1.0e-6));
}

TEST_CASE ("STEREO: switching on and turning the knobs doesn't click", "[stereo]")
{
    using Setup = std::function<void (ParagonX1AudioProcessor&)>;
    const auto check = [] (const Setup& before, const Setup& change)
    {
        auto processor = makeProcessor();
        before (*processor);
        auto buffer = wide (200.0f, 70.0f, 48000);
        process (*processor, buffer, [&] (int start) { if (start == 24064) change (*processor); });
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto steady = juce::jmax (sharpestCorner (buffer, channel, 12000, 24000), sharpestCorner (buffer, channel, 40000, 48000));
            CHECK (sharpestCorner (buffer, channel, 24000, 40000) < steady * 3.0f);
        }
    };
    const auto widthOnly = [] (ParagonX1AudioProcessor& p) { set (p, ParamIDs::stereoMonoOn, 0.0f); };
    SECTION ("POWER on") { check ([] (auto& p) { set (p, ParamIDs::stereoWidth, 200.0f); }, [] (auto& p) { set (p, ParamIDs::stereoOn, 1.0f); }); }
    SECTION ("mono maker frequency") { check ([] (auto& p) { set (p, ParamIDs::stereoOn, 1.0f); }, [] (auto& p) { set (p, ParamIDs::stereoMonoFreq, 400.0f); }); }
    SECTION ("width on") { check ([&] (auto& p) { set (p, ParamIDs::stereoOn, 1.0f); set (p, ParamIDs::stereoWidthOn, 0.0f); widthOnly (p); set (p, ParamIDs::stereoWidth, 200.0f); },
                                  [] (auto& p) { set (p, ParamIDs::stereoWidthOn, 1.0f); }); }
    SECTION ("width") { check ([&] (auto& p) { set (p, ParamIDs::stereoOn, 1.0f); widthOnly (p); }, [] (auto& p) { set (p, ParamIDs::stereoWidth, 0.0f); }); }
    SECTION ("MONO listening") { check ([] (auto&) {}, [] (auto& p) { set (p, ParamIDs::stereoListenMono, 1.0f); }); }
}

TEST_CASE ("STEREO: the correlation meter reads mono, wide and out of phase", "[stereo]")
{
    const auto correlationOf = [] (float rightSign, float rightHz)
    {
        auto processor = makeProcessor();
        juce::AudioBuffer<float> buffer (2, 48000);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto t = (float) i / (float) sampleRate;
            buffer.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * t));
            buffer.setSample (1, i, rightSign * 0.3f * std::sin (juce::MathConstants<float>::twoPi * rightHz * t));
        }
        process (*processor, buffer);
        return processor->getStereoCorrelation();
    };
    CHECK_THAT (correlationOf (1.0f, 440.0f), WithinAbs (1.0, 0.01));    // the same both sides: mono
    CHECK_THAT (correlationOf (-1.0f, 440.0f), WithinAbs (-1.0, 0.01));  // one side upside down
    CHECK_THAT (correlationOf (1.0f, 557.0f), WithinAbs (0.0, 0.1));     // unrelated: wide

    // Width at 0 makes anything mono: the meter goes to +1.
    auto processor = makeStereo (false, true);
    set (*processor, ParamIDs::stereoWidth, 0.0f);
    auto buffer = wide (300.0f, 450.0f, 48000);
    process (*processor, buffer);
    CHECK (processor->getStereoCorrelation() > 0.99f);
}

TEST_CASE ("STEREO: MONO listening folds the result to mono, and isn't saved in rigs", "[stereo]")
{
    auto processor = makeProcessor();
    set (*processor, ParamIDs::stereoListenMono, 1.0f);
    auto buffer = wide (300.0f, 450.0f, 48000);
    process (*processor, buffer);
    CHECK (levelDb (buffer, true, 450.0f, 24000, 24000) < -100.0f);                            // no side
    CHECK_THAT (levelDb (buffer, false, 300.0f, 24000, 24000) - levelDb (wide (300.0f, 450.0f, 48000), false, 300.0f, 24000, 24000),
                WithinAbs (0.0, 0.01));                                                         // the mid as it was
    CHECK (ParamIDs::notInRigs.contains (ParamIDs::stereoListenMono));
}

TEST_CASE ("MARS presets: only MARS's settings, every factory one real, and the gentle ones gentle", "[mars][presets]")
{
    auto processor = makeProcessor();
    auto& state = processor->getParameters();
    const auto& factory = MarsPresets::factory();
    REQUIRE (factory.size() > 10);
    CHECK (factory.front().name == "Default");
    CHECK_FALSE (MarsPresets::holds (ParamIDs::marsOn));   // a preset never switches it on or off
    CHECK (MarsPresets::holds (ParamIDs::marsBand (3, "Level")));
    CHECK_FALSE (MarsPresets::holds (ParamIDs::mbBand (0, "Used")));   // CONSTRICTOR's bands are its own

    for (const auto& preset : factory)
    {
        INFO (preset.name);
        for (const auto& [id, value] : preset.parameters)
        {
            INFO (id);
            auto* parameter = state.getParameter (id);
            REQUIRE (parameter != nullptr);
            CHECK (MarsPresets::holds (id));
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
        // Crossovers in order.
        MarsPresets::apply (preset, state);
        CHECK (MarsPresets::matches (preset, state));
        const auto bands = juce::roundToInt (state.getRawParameterValue (ParamIDs::marsBands)->load()) + 1;
        const char* crossovers[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
        for (int i = 1; i < bands - 1; ++i)
            CHECK (state.getRawParameterValue (crossovers[i])->load() > state.getRawParameterValue (crossovers[i - 1])->load());
    }

    // The mastering and mix bus ones colour gently: what isn't the notes
    // (the saturation's harmonics) stays well down.
    for (const auto& preset : factory)
    {
        if (preset.category != "Mastering" && preset.category != "Mix Bus")
            continue;
        INFO (preset.name);
        auto rig = makeProcessor();
        set (*rig, ParamIDs::marsOn, 1.0f);
        MarsPresets::apply (preset, rig->getParameters());
        std::vector<float> input ((size_t) (0.8 * sampleRate));
        for (size_t i = 0; i < input.size(); ++i)
        {
            const auto t = (double) i / sampleRate;
            input[i] = (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 80.0 * t) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 700.0 * t)
                                + 0.05 * std::sin (juce::MathConstants<double>::twoPi * 5000.0 * t));
        }
        juce::AudioBuffer<float> buffer (2, (int) input.size());
        for (int c = 0; c < 2; ++c)
            buffer.copyFrom (c, 0, input.data(), (int) input.size());
        process (*rig, buffer);
        const std::vector<float> output (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
        double notes = 0.0;
        for (const auto hz : { 80.0, 700.0, 5000.0 })
            notes += std::pow (levelAt (output, hz, 0.4) / std::sqrt (2.0), 2.0);
        const auto total = rms (output, 0.4);
        CHECK (dB ((float) std::sqrt (std::max (0.0, (double) total * total - notes)) / total) < -20.0f);
    }
}

TEST_CASE ("MARS presets: switching one in keeps the level (within 2 dB), so comparing is fair", "[mars][presets]")
{
    // Two kinds of sound: three notes across the range (a bass-heavy mix),
    // and pink noise (a full, even spectrum). Each preset's OUTPUT is set to
    // make up for what its saturation adds or takes.
    for (const auto& preset : MarsPresets::factory())
        for (int signal = 0; signal < 2; ++signal)
        {
            INFO (preset.name << (signal == 0 ? " on three notes" : " on pink noise"));
            auto rig = makeProcessor();
            set (*rig, ParamIDs::marsOn, 1.0f);
            MarsPresets::apply (preset, rig->getParameters());
            std::vector<float> input ((size_t) (1.0 * sampleRate));
            juce::Random random (3);
            double b[3] {};
            for (size_t i = 0; i < input.size(); ++i)
            {
                const auto t = (double) i / sampleRate;
                if (signal == 0)
                    input[i] = (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 80.0 * t) + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 700.0 * t)
                                        + 0.05 * std::sin (juce::MathConstants<double>::twoPi * 5000.0 * t));
                else
                {
                    const auto white = random.nextFloat() * 2.0 - 1.0;
                    b[0] = 0.99765 * b[0] + white * 0.0990460;
                    b[1] = 0.96300 * b[1] + white * 0.2965164;
                    b[2] = 0.57000 * b[2] + white * 1.0526913;
                    input[i] = (float) (0.12 * (b[0] + b[1] + b[2] + white * 0.1848));
                }
            }
            juce::AudioBuffer<float> buffer (2, (int) input.size());
            for (int c = 0; c < 2; ++c)
                buffer.copyFrom (c, 0, input.data(), (int) input.size());
            process (*rig, buffer);
            const std::vector<float> output (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
            CHECK (std::abs (dB (rms (output, 0.4) / rms (input, 0.4))) < 2.0f);
        }
}
