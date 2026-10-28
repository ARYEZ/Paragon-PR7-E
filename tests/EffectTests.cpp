#include <catch2/catch_test_macros.hpp>
#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Circuit.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // Reports a fixed tempo, like a DAW with its transport set to `bpm`.
    struct FakePlayHead final : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool playing = false;              // a transport that's playing reports the song position
        double startBeats = 0.0;           // where the song was when the run began, in beats
        juce::int64 samplesPlayed = 0;     // moved on by EffectRig::run

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (playing);
            if (playing)
                info.setPpqPosition (startBeats + (double) samplesPlayed / sampleRate * bpm / 60.0);
            return info;
        }
    };

    struct EffectRig
    {
        explicit EffectRig (int outputChannels = 2, int inputChannels = 1) : channels (outputChannels)
        {
            processor.setPlayConfigDetails (inputChannels, channels, sampleRate, blockSize);
            processor.setPlayHead (&playHead);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        // Runs `input` through the plugin in blocks; returns every output channel.
        std::vector<std::vector<float>> run (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> output ((size_t) channels);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            juce::MidiBuffer midi;

            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, input.size() - start);
                buffer.setSize (channels, count, false, false, true);
                buffer.clear();
                std::copy (input.begin() + (long) start, input.begin() + (long) start + count, buffer.getWritePointer (0));
                processor.processBlock (buffer, midi);
                playHead.samplesPlayed += count;
                for (int ch = 0; ch < channels; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        int channels;
        FakePlayHead playHead;
        ParagonX1AudioProcessor processor;
    };

    std::vector<float> sine (double hz, float amplitude, double seconds)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    std::vector<float> impulse (double seconds, float amplitude = 1.0f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        samples[0] = amplitude;
        return samples;
    }

    std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * sampleRate), 0.0f); }

    float rms (const std::vector<float>& samples, double fromSeconds = 0.0, double toSeconds = 1.0e9)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto to = std::min (samples.size(), (size_t) (toSeconds * sampleRate));
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += samples[i] * samples[i];
        return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    float dB (float ratio) { return 20.0f * std::log10 (ratio); }

    // Level of one frequency in a signal (Goertzel algorithm), skipping the
    // first `fromSeconds` so filters and fades have settled.
    float levelAt (const std::vector<float>& samples, double hz, double fromSeconds = 0.1)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto n = samples.size() - from;
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) n);
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
    }

    float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        float result = 0.0f;
        for (auto i = from; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("With every effect off, both channels are the clean input", "[effects]")
{
    EffectRig rig;
    const auto input = sine (440.0, 0.3f, 0.5);
    const auto output = rig.run (input);
    CHECK (maxAbsDifference (input, output[0]) < 1.0e-6f);   // a mono track: on both speakers
    CHECK (maxAbsDifference (input, output[1]) < 1.0e-6f);
}

TEST_CASE ("Stereo stays stereo: left and right keep their own sound, on and bypassed", "[effects][stereo]")
{
    // A stereo recording: a low note on the left, a high one on the right.
    // Before 0.3.0 PR7-X played the left channel on both sides.
    const auto left = sine (110.0, 0.3f, 0.5), right = sine (1760.0, 0.2f, 0.5);
    const auto play = [&] (EffectRig& rig)
    {
        std::vector<std::vector<float>> output (2);
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (size_t start = 0; start + blockSize <= left.size(); start += blockSize)
        {
            std::copy (left.begin() + (long) start, left.begin() + (long) start + blockSize, buffer.getWritePointer (0));
            std::copy (right.begin() + (long) start, right.begin() + (long) start + blockSize, buffer.getWritePointer (1));
            rig.processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
        }
        return output;
    };

    SECTION ("Everything off: untouched, sample for sample")
    {
        EffectRig rig (2, 2);
        const auto output = play (rig);
        CHECK (maxAbsDifference (left, output[0]) < 1.0e-6f);
        CHECK (maxAbsDifference (right, output[1]) < 1.0e-6f);
    }

    SECTION ("The EQ and PRESENCE work on each side without mixing them")
    {
        EffectRig rig (2, 2);
        rig.set (ParamIDs::eqOn, 1.0f);
        rig.set (ParamIDs::presence, 6.0f);
        const auto output = play (rig);
        CHECK (levelAt (output[0], 110.0) > 0.25f);
        CHECK (levelAt (output[0], 1760.0) < 0.001f);   // nothing of the right channel on the left
        CHECK (levelAt (output[1], 1760.0) > 0.15f);
        CHECK (levelAt (output[1], 110.0) < 0.001f);
    }

    SECTION ("BYPASS passes both channels as they came in")
    {
        EffectRig rig (2, 2);
        rig.set (ParamIDs::presence, 10.0f);
        rig.set (ParamIDs::bypass, 1.0f);
        const auto output = play (rig);
        CHECK (maxAbsDifference (left, output[0], (size_t) (0.05 * sampleRate)) < 1.0e-6f);
        CHECK (maxAbsDifference (right, output[1], (size_t) (0.05 * sampleRate)) < 1.0e-6f);
    }
}

TEST_CASE ("Turning the EQ on and off doesn't click, and off really is off", "[effects]")
{
    EffectRig rig;
    // A band +8 dB at 220 Hz, so ON makes a real difference.
    rig.set (ParamIDs::eqBand (0, "Freq").toRawUTF8(), 220.0f);
    rig.set (ParamIDs::eqBand (0, "Gain").toRawUTF8(), 8.0f);
    rig.set (ParamIDs::eqBand (0, "Used").toRawUTF8(), 1.0f);
    const auto input = sine (220.0, 0.3f, 0.3);
    rig.set (ParamIDs::eqOn, 1.0f);
    rig.runMono (input);

    // Switching off mid-note: no sample may jump further than the biggest
    // step the EQ makes on its own.
    const auto on = rig.runMono (input);
    rig.set (ParamIDs::eqOn, 0.0f);
    const auto switching = rig.runMono (sine (220.0, 0.3f, 0.05));
    float normalStep = 0.0f, switchStep = 0.0f;
    for (size_t i = 1; i < on.size(); ++i)
        normalStep = std::max (normalStep, std::abs (on[i] - on[i - 1]));
    for (size_t i = 1; i < switching.size(); ++i)
        switchStep = std::max (switchStep, std::abs (switching[i] - switching[i - 1]));
    CHECK (switchStep < std::max (normalStep, 0.03f) * 1.5f);

    rig.runMono (silence (0.5));
    const auto after = rig.runMono (input);
    CHECK (maxAbsDifference (input, after) < 1.0e-4f);
}

TEST_CASE ("The chain copes with a mono track and with oversized blocks", "[effects]")
{
    const auto turnEverythingOn = [] (EffectRig& rig)
    {
        rig.set (ParamIDs::eqOn, 1.0f);
        rig.set (ParamIDs::presence, 5.0f);
    };

    SECTION ("Mono output")
    {
        EffectRig rig (1);
        turnEverythingOn (rig);
        const auto output = rig.runMono (sine (220.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        CHECK (rms (output, 0.2) > 0.01f);
    }

    SECTION ("A host sending 3x the promised block size")
    {
        EffectRig rig;
        turnEverythingOn (rig);
        juce::AudioBuffer<float> big (2, blockSize * 3);
        juce::MidiBuffer midi;
        for (int repeat = 0; repeat < 20; ++repeat)
        {
            big.clear();
            const auto input = sine (220.0, 0.3f, (double) big.getNumSamples() / sampleRate);
            std::copy (input.begin(), input.end(), big.getWritePointer (0));
            rig.processor.processBlock (big, midi);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < big.getNumSamples(); ++i)
                REQUIRE (std::isfinite (big.getSample (ch, i)));
    }
}

TEST_CASE ("Full chain runs comfortably faster than real time", "[effects][performance]")
{
    EffectRig rig (2, 2);
    rig.set (ParamIDs::eqOn, 1.0f);
    rig.set (ParamIDs::presence, 5.0f);
    rig.set (ParamIDs::tunerOn, 1.0f);
    rig.set (ParamIDs::domOn, 1.0f);      // the heaviest stomp
    rig.set (ParamIDs::compOn, 1.0f);
    rig.set (ParamIDs::compCharacter, 2.0f);   // FET, with its grit
    rig.set (ParamIDs::limitOn, 1.0f);

    const auto input = sine (220.0, 0.3f, 5.0);
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    rig.runMono (input);
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto realtimeFraction = elapsedSeconds / 5.0;

    WARN ("Full chain uses " << juce::String (realtimeFraction * 100.0, 1) << "% of one core in real time");
    CHECK (realtimeFraction < 0.5);
}

TEST_CASE ("The circuit solver behaves like the circuit: an RC low-pass", "[effects][circuit]")
{
    // 1k and 1 uF: the corner is at 159 Hz, falling 6 dB per octave.
    effects::Circuit c;
    const auto in = c.node(), out = c.node();
    c.resistor (in, out, 1.0e3);
    c.capacitor (out, 0, 1.0e-6);
    c.setInput (in);
    c.setOutput (out);
    c.prepare (sampleRate);

    const auto gainAt = [&c] (double hz)
    {
        c.reset();
        std::vector<float> output;
        for (int i = 0; i < (int) sampleRate; ++i)
            output.push_back ((float) c.process (std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate)));
        return dB (levelAt (output, hz, 0.5));
    };
    CHECK_THAT (gainAt (159.15), WithinAbs (-3.01, 0.1));
    CHECK_THAT (gainAt (1591.5), WithinAbs (-20.04, 0.2));
    CHECK_THAT (gainAt (20.0), WithinAbs (0.0, 0.1));
}

TEST_CASE ("Presence boosts the top end and leaves the lows alone", "[effects][presence]")
{
    EffectRig rig;
    rig.set (ParamIDs::presence, 10.0f);
    const auto highs = sine (9000.0, 0.1f, 0.5);
    const auto lows = sine (150.0, 0.1f, 0.5);
    CHECK (dB (rms (rig.runMono (highs), 0.2) / rms (highs, 0.2)) > 8.0f);
    CHECK_THAT (dB (rms (rig.runMono (lows), 0.2) / rms (lows, 0.2)), WithinAbs (0.0, 0.5));
}


TEST_CASE ("Fuzz: any settings at all, changed as it plays, never crash it or make it blow up", "[effects][fuzz]")
{
    // As pluginval's "Fuzz parameters": every parameter set at random, again and again, while audio runs.
    for (const auto inputs : { 1, 2 })
    {
        ParagonX1AudioProcessor processor;
        processor.setPlayConfigDetails (inputs, 2, 48000.0, 256);
        processor.prepareToPlay (48000.0, 256);
        juce::Random random (inputs * 977);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int round = 0; round < 1500; ++round)
        {
            for (auto* parameter : processor.AudioProcessor::getParameters())
                if (random.nextFloat() < 0.5f)
                    parameter->setValue (random.nextFloat());
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
            processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    REQUIRE (std::isfinite (buffer.getSample (ch, i)));
        }
    }
}
