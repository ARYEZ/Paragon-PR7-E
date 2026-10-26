#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// The LIMITER's level display (0.16.0): the last few seconds, scrolling.
// Grey: the level going in (with GAIN); the gradient: what comes out; red
// from the top: how much it's turning down, with the biggest dips labelled;
// the thin line: the momentary loudness; dashed: the ceiling (OUTPUT).
class LimiterDisplay final : public juce::Component,
                             private juce::Timer
{
public:
    explicit LimiterDisplay (ParagonX1AudioProcessor&);
    void refresh() { timerCallback(); }   // takes in what the limiter has played since

    // Each scale step, from the top (0 dB) down.
    static constexpr float rangeDb = 30.0f;
    float yForDb (float db) const noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ParagonX1AudioProcessor& processor;
    std::vector<effects::MasterLimiter::Frame> history;   // oldest first, one per 10 ms
    std::vector<effects::MasterLimiter::Frame> incoming;
    juce::Rectangle<float> plot;
};

// The LIMITER's own page (stomp 7), over the pedal and dock: inspired by
// Pro-L 2's layout. GAIN on the left, the level display, the meters and
// loudness on the right; STYLE and the advanced settings, and the output
// options, underneath.
class LimiterPage final : public juce::Component,
                          private juce::Timer
{
public:
    explicit LimiterPage (ParagonX1AudioProcessor&);

    std::function<void()> onClose;
    LimiterDisplay& getDisplay() noexcept { return display; }
    void refresh() { timerCallback(); }   // the held readings and the controls, now

    // The loudness target (LUFS) and which reading is the big one (0 momentary, 1 short term, 2 integrated): saved with the project.
    static constexpr const char* targetKey = "limitLoudnessTarget";
    static constexpr const char* timeScaleKey = "limitLoudnessTimeScale";

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

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
    struct Choice
    {
        Choice (juce::AudioProcessorValueTreeState&, const char* id, const juce::String& label, const juce::StringArray& items, const juce::String& tooltip);
        juce::ComboBox box;
        juce::Label label;
        juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
    };

    void timerCallback() override;
    void showLegacyControls();
    float target() const;
    int timeScale() const;
    void resetReadings();

    ParagonX1AudioProcessor& processor;
    LimiterDisplay display;

    Toggle on;
    Choice style;
    juce::TextButton back { "BACK TO PEDAL" };

    juce::Slider gain { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    juce::Label gainLabel { {}, "GAIN" };
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;

    Knob lookahead, attack, release, linkTransients, linkRelease, mix, ceiling, trim;
    Toggle legacyLink, truePeak, unity, delta, dcFilter;
    Choice oversampling, dither, shaping;
    juce::ComboBox targetBox, timeScaleBox;
    juce::Label targetLabel { {}, "TARGET" }, timeScaleLabel { {}, "SHOW" };
    juce::TextButton reset { "RESET" };

    // Readings held since RESET.
    float heldTruePeakDb = -100.0f, heldReductionDb = 0.0f;
    juce::Rectangle<int> topBar, gainArea, meterArea, controlsArea, outputArea, peakReadout;
};
