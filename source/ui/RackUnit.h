#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/Footswitch.h"

// One of the unit's four rows since 0.18.0: a stomp drawn as a rack
// processor, after the owner's photo of a Yamaha SPX90, Lexicon PCM70 and
// Roland SDE-3000, in the PR7's own parts and finish. Left to right: rack
// screws, the stomp's footswitch as its POWER switch, a name plate, a
// ladder of the unit's LED bars (a meter), the PR7's orange LED screen with
// the stomp's own readout, three small keys (the PR7's keys, each with an
// LED bar), two of the unit's knobs, and EDIT (its big page) over a small
// screen with its preset or style.
class RackUnit final : public juce::Component,
                       private juce::Timer
{
public:
    struct Key
    {
        juce::String label, tooltip;
        std::function<bool()> isOn;      // its LED
        std::function<void()> press;
    };
    struct Knob
    {
        const char* parameterId;
        juce::String label;
    };
    struct Spec
    {
        int number = 1;
        juce::String name, kind, series;               // ECHELON / PARAMETRIC EQUALIZER / PR7-E SERIES ...
        juce::StringArray onParameterIds;              // POWER
        std::array<Key, 3> keys;
        std::array<Knob, 2> knobs;
        // The ladder: its title, and five marks from the top; a mark's bar
        // lights when the reading reaches it, so it fills from the bottom.
        juce::String ladderTitle;
        std::array<float, 5> ladderMarks {};
        std::array<juce::String, 5> ladderLabels;
        std::function<float()> ladderReading;
        std::function<void (juce::Graphics&, juce::Rectangle<float> glass)> paintReadout;
        juce::String footTitle;
        std::function<juce::String()> footText;
        std::function<void()> onEdit;
        juce::String powerTooltip;
    };

    RackUnit (juce::AudioProcessorValueTreeState&, Spec);
    ~RackUnit() override;

    Footswitch& getPower() noexcept { return *power; }
    juce::TextButton& getEdit() noexcept { return edit; }
    const Spec& getSpec() const noexcept { return spec; }
    void refresh() { timerCallback(); }

    // Positions inside the unit, scaled from its drawn size (1332 x 132).
    static constexpr float referenceWidth = 1332.0f, referenceHeight = 132.0f;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;   // the name plate opens the page, as EDIT does

private:
    void timerCallback() override;
    juce::Rectangle<float> at (float x, float y, float w, float h) const noexcept;

    juce::AudioProcessorValueTreeState& state;
    Spec spec;
    std::unique_ptr<Footswitch> power;
    std::array<juce::TextButton, 3> keys;
    std::array<juce::Slider, 2> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    juce::TextButton edit { "EDIT" };
    float ladderHeld = -100.0f;   // the ladder's reading, falling back slowly after a peak
    std::array<bool, 3> keyLit {};
};
