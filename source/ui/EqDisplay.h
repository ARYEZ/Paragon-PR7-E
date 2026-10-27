#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginProcessor.h"

// Reading and changing the EQ's bands through their parameters, as the UI
// does (each change a gesture, so it's undoable and recorded by the DAW).
class EqBands
{
public:
    explicit EqBands (ParagonX1AudioProcessor&);

    eq::ParametricEq::Settings settings() const { return processor.getEqSettings(); }
    // The settings with each dynamic band at its gain now, as it sounds.
    eq::ParametricEq::Settings live() const;
    juce::RangedAudioParameter& parameter (int band, const char* what) const;
    float get (int band, const char* what) const;
    void set (int band, const char* what, float plainValue) const;   // a complete gesture
    void begin (int band, const char* what) const;
    void change (int band, const char* what, float plainValue) const;   // during a gesture
    void end (int band, const char* what) const;

    // Creates a band in the first free slot (dynamic when `dynamicRangeDb`
    // isn't 0); returns it, or -1 if all 24 are in use.
    int create (eq::Shape, double frequency, double gainDb, double dynamicRangeDb = 0.0) const;
    void remove (int band) const;

    // A band's colour: the menus' red to pink to orange gradient at its frequency (0.16.1), as DOMINION's and COMP's bands.
    static juce::Colour colourFor (const eq::Band&);
    static juce::Colour colourFor (eq::Placement);   // the curve of the bands on one channel, or mid or side

    ParagonX1AudioProcessor& processor;
};

// A spectrum, from the audio an InputTap keeps, for drawing: 4096-point
// FFTs about 30 times a second, falling back slowly (as an analyser's
// display does), tilted 4.5 dB per octave round 1 kHz so music reads
// roughly level (Pro-Q's default tilt).
class SpectrumAnalyser
{
public:
    void update (const InputTap&);   // message thread
    // The level in dB at `hz` (the average of the bins from `hz` to `toHz`).
    float levelDb (double hz, double toHz) const noexcept;
    bool hasSignal() const noexcept { return lastTotal >= 0; }

private:
    static constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft { order };
    juce::dsp::WindowingFunction<float> window { (size_t) size, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> buffer = std::vector<float> ((size_t) size * 2);
    std::vector<float> smoothedDb = std::vector<float> ((size_t) size / 2 + 1, -150.0f);
    double sampleRate = 48000.0;
    std::int64_t lastTotal = -1;
};

// The EQ's big display: the frequency scale (10 Hz to 30 kHz) across, dB up
// the side, the spectrum going in (filled) and coming out (a line) behind,
// each band's own curve, the overall curve (and one per channel, mid or
// side when bands work on those), and a dot for each band to drag.
//
//   - Double-click: a new band there (a bell at that height; a low cut at
//     the far left, a high cut at the far right). Alt+double-click: a
//     dynamic bell, its range to that height.
//   - Drag a dot: frequency and gain (Q for shapes without a gain). Shift: finer.
//   - A dynamic band: a dashed curve where its range takes it, a handle
//     there to drag, and a ring where its gain is now (the curves follow it).
//   - Mouse wheel over a band: Q (the slope for cuts). With Alt: the dynamic range.
//   - Alt-click a dot: switch the band on or off. Right-click: its menu.
//   - Delete or Backspace: remove the selected band.
class EqDisplay final : public juce::Component,
                        private juce::Timer
{
public:
    explicit EqDisplay (ParagonX1AudioProcessor&);

    int getSelected() const noexcept { return selected; }

    // Reads the analysers and the bands again (the timer does this 30 times a second).
    void refresh();
    void select (int band);
    std::function<void (int)> onSelect;

    // EQ Match's preview, on eq::match::grid(): the curve that would match
    // (a thick line) and the curve with the proposed bands (dashed). Empty: none.
    void setMatchPreview (std::vector<double> wantedDb, std::vector<double> proposedDb);
    bool hasMatchPreview() const noexcept { return ! matchWanted.empty(); }

    // Display range: +-3, 6, 12 or 30 dB. And which spectrums show.
    void setRangeDb (double);
    double getRangeDb() const noexcept { return rangeDb; }
    bool showInput = true, showOutput = true;

    float xForFrequency (double hz) const noexcept;
    double frequencyForX (float x) const noexcept;
    float yForDb (double db) const noexcept;
    double dbForY (float y) const noexcept;
    juce::Point<float> dotFor (const eq::Band&) const noexcept;   // where a band's dot sits
    juce::Point<float> rangeHandleFor (const eq::Band&) const noexcept;   // a dynamic band's range handle

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override { if (isShowing()) refresh(); }
    int bandAt (juce::Point<float>) const;
    int rangeHandleAt (juce::Point<float>) const;   // a dynamic band whose range handle is there, or -1
    void showMenu (int band);
    void paintGrid (juce::Graphics&);
    void paintSpectrum (juce::Graphics&, const SpectrumAnalyser&, bool filled);

    EqBands bands;
    SpectrumAnalyser input, output;
    eq::ParametricEq::Settings shown;   // the settings drawn last
    std::array<eq::ParametricEq::DynamicReading, eq::ParametricEq::numBands> dynamics {};
    std::vector<double> matchWanted, matchProposed;
    juce::Rectangle<float> plot;
    double rangeDb = 12.0;
    int selected = -1, hovered = -1, dragging = -1;
    juce::Point<float> hoverPoint;
    double dragStartFrequency = 0.0, dragStartGain = 0.0, dragStartQ = 1.0, dragStartRange = 0.0;
    bool dragHasGain = true, draggingRange = false;
};
