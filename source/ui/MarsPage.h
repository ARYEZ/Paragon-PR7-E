#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/PresetButton.h"

// MARS's band display (0.22.0): the frequencies left to right, each band a
// column with its style and how hard it's driven, glowing as it works.
// Click a band to pick it; drag a crossover line to move it; drag up or down
// in a band for its DRIVE. (As PR7-V's MARS panel, without its modulation.)
class MarsDisplay final : public juce::Component,
                          private juce::Timer
{
public:
    explicit MarsDisplay (ParagonX1AudioProcessor&);

    std::function<void (int band)> onSelect;
    void setSelected (int band);
    int getSelected() const noexcept { return selected; }

    // The display's frequency at an x position, and back. Public for tests.
    float frequencyAt (float x) const;
    float xFor (float hz) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    int numBands() const;
    std::array<float, effects::Mars::maxBands + 1> edges() const;   // each band's left and right, in Hz
    int bandAt (float x) const;
    int crossoverAt (float x) const;   // the crossover line near x, or -1
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plain);

    ParagonX1AudioProcessor& processor;
    juce::Rectangle<float> plot;
    int selected = 0, draggingCrossover = -1, draggingDriveOf = -1;
    float dragStartDrive = 0.0f;
    std::array<float, effects::Mars::maxBands> shownDrive {};
};

// MARS's own page (0.22.0), over the unit: inspired by Saturn 2's layout.
// Along the top ON, BANDS, QUALITY, SLOPE and the PRESET; the band display
// with INPUT, OUTPUT and MIX beside it; underneath, the picked band's
// controls: ON, STYLE, DRIVE, DYNAMICS, FEEDBACK, FB FREQ, BASS, MID,
// TREBLE, PRESENCE, MIX and LEVEL.
class MarsPage final : public juce::Component,
                       private juce::Timer
{
public:
    explicit MarsPage (ParagonX1AudioProcessor&);

    std::function<void()> onClose;
    MarsDisplay& getDisplay() noexcept { return display; }
    void selectBand (int band);
    int getSelectedBand() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        Knob (const juce::String& label, const juce::String& tooltip);
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        void attach (juce::AudioProcessorValueTreeState&, const juce::String& id);
    };
    struct Choice
    {
        Choice (const juce::String& label, const juce::StringArray& items, const juce::String& tooltip);
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
        void attach (juce::AudioProcessorValueTreeState&, const juce::String& id);
    };

    void timerCallback() override;
    std::vector<Knob*> bandKnobs();

    ParagonX1AudioProcessor& processor;
    MarsDisplay display;
    int selected = 0;

    juce::ToggleButton on { "ON" }, bandOn { "BAND ON" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandOnAttachment;
    Choice bands, quality, slope, style;
    juce::TextButton back { "BACK TO PEDAL" };
    PresetButton preset;
    juce::Label presetLabel { {}, "PRESET" };

    Knob input, output, mix;
    Knob drive, dynamics, feedback, feedbackFreq, bass, mid, treble, presence, bandMix, level;

    juce::Rectangle<int> topBar, globalArea, bandArea;
};
