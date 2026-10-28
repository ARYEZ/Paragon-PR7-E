#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    void setParameter (ParagonX1AudioProcessor& processor, const char* id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }

    // Fills channel 0 with a 1 kHz sine and any other channels with silence,
    // like a guitar on the left input of a stereo track.
    void fillWithSine (juce::AudioBuffer<float>& buffer, float amplitude, int& phaseSample)
    {
        buffer.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i, ++phaseSample)
            buffer.setSample (0, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * phaseSample / sampleRate));
    }

    // Runs enough blocks for gain smoothing to settle, returning the last one.
    juce::AudioBuffer<float> processSettled (ParagonX1AudioProcessor& processor, int numChannels, float amplitude)
    {
        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        juce::MidiBuffer midi;
        int phase = 0;
        for (int block = 0; block < 20; ++block)
        {
            fillWithSine (buffer, amplitude, phase);
            processor.processBlock (buffer, midi);
        }
        return buffer;
    }

    float peakOf (const juce::AudioBuffer<float>& buffer, int channel)
    {
        return buffer.getMagnitude (channel, 0, buffer.getNumSamples());
    }

    std::unique_ptr<ParagonX1AudioProcessor> makePreparedProcessor()
    {
        auto processor = std::make_unique<ParagonX1AudioProcessor>();
        processor->setPlayConfigDetails (1, 2, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        return processor;
    }
}

TEST_CASE ("Default settings pass the signal through at unity gain", "[processor]")
{
    auto processor = makePreparedProcessor();
    const auto output = processSettled (*processor, 2, 0.5f);

    CHECK_THAT (peakOf (output, 0), WithinAbs (0.5, 0.01));
}

TEST_CASE ("Mono guitar input comes out identically on both output channels", "[processor]")
{
    auto processor = makePreparedProcessor();
    const auto output = processSettled (*processor, 2, 0.5f);

    for (int i = 0; i < output.getNumSamples(); ++i)
        REQUIRE (output.getSample (0, i) == output.getSample (1, i));
}

TEST_CASE ("Input and output gain apply the requested dB", "[processor]")
{
    auto processor = makePreparedProcessor();

    SECTION ("+6 dB input doubles the level")
    {
        setParameter (*processor, ParamIDs::inputGain, 6.0f);
        CHECK_THAT (peakOf (processSettled (*processor, 2, 0.25f), 0), WithinAbs (0.25 * 1.9953, 0.01));
    }

    SECTION ("-12 dB output quarters the level")
    {
        setParameter (*processor, ParamIDs::outputGain, -12.0f);
        CHECK_THAT (peakOf (processSettled (*processor, 2, 0.8f), 0), WithinAbs (0.8 * 0.2512, 0.01));
    }
}

TEST_CASE ("Level meters report the peak and reset after reading", "[processor]")
{
    auto processor = makePreparedProcessor();
    processSettled (*processor, 2, 0.5f);

    CHECK_THAT (processor->getInputMeter().takePeak(), WithinAbs (0.5, 0.01));
    CHECK_THAT (processor->getOutputMeter().takePeak(), WithinAbs (0.5, 0.01));
    CHECK (processor->getOutputMeter().takePeak() == 0.0f);
}

TEST_CASE ("Output is always finite, even with extreme input", "[processor]")
{
    auto processor = makePreparedProcessor();
    setParameter (*processor, ParamIDs::inputGain, ParamRanges::gainMaxDb);
    setParameter (*processor, ParamIDs::outputGain, ParamRanges::gainMaxDb);
    const auto output = processSettled (*processor, 2, 1.0f);

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        for (int i = 0; i < output.getNumSamples(); ++i)
            REQUIRE (std::isfinite (output.getSample (channel, i)));
}

TEST_CASE ("Settings survive a save and reload, as in a DAW project", "[processor]")
{
    ParagonX1AudioProcessor original;
    setParameter (original, ParamIDs::inputGain, 4.5f);
    setParameter (original, ParamIDs::outputGain, -7.0f);

    juce::MemoryBlock state;
    original.getStateInformation (state);

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (restored.getParameters().getRawParameterValue (ParamIDs::inputGain)->load(), WithinAbs (4.5, 0.001));
    CHECK_THAT (restored.getParameters().getRawParameterValue (ParamIDs::outputGain)->load(), WithinAbs (-7.0, 0.001));
}

TEST_CASE ("Supports mono and stereo tracks", "[processor]")
{
    ParagonX1AudioProcessor processor;
    using Set = juce::AudioChannelSet;

    const auto supports = [&] (Set in, Set out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return processor.checkBusesLayoutSupported (layout);
    };

    CHECK (supports (Set::mono(), Set::mono()));
    CHECK (supports (Set::mono(), Set::stereo()));
    CHECK (supports (Set::stereo(), Set::stereo()));
    CHECK_FALSE (supports (Set::create5point1(), Set::stereo()));
}

TEST_CASE ("The processor is small enough to hold several on Windows' stack", "[processor]")
{
    // 0.22.0: with MARS's two engines it passed 300 KB, and a test holding
    // four at once crashed on Windows, whose stack is 1 MB. The big engines
    // (ECHELON, DOMINION, MARS, ORACLE's timeline) now live on the heap.
    CHECK (sizeof (ParagonX1AudioProcessor) < 100 * 1024);
}
