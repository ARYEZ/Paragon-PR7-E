#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// BINARY's own page (0.22.0; "stereo" in the code): the stereo image after
// it, as a scope (each moment of left and right a dot: up the middle is
// mono, left and right lean that way, a wide sound spreads out, and sideways
// is out of phase), the correlation meter, and its controls: MONO MAKER and
// its frequency, WIDTH, and MONO to listen in mono.
class StereoPage final : public juce::Component,
                         private juce::Timer
{
public:
    explicit StereoPage (ParagonX1AudioProcessor&);

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        Knob (juce::AudioProcessorValueTreeState&, const char* id, const juce::String& label, const juce::String& tooltip);
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    };
    struct Toggle
    {
        Toggle (juce::AudioProcessorValueTreeState&, const char* id, const juce::String& text, const juce::String& tooltip);
        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    Toggle on, monoOn, widthOn, listenMono;
    Knob monoFrequency, width;
    juce::TextButton back { "BACK TO PEDAL" };

    static constexpr int scopeSamples = 2048;
    std::vector<float> left = std::vector<float> (scopeSamples), right = std::vector<float> (scopeSamples);
    bool haveScope = false;
    float shownCorrelation = 1.0f, lowestCorrelation = 1.0f;
    juce::Rectangle<int> topBar, scopeArea, meterArea, controlsArea;
};
