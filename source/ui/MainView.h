#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/DockPanels.h"
#include "ui/DominionPage.h"
#include "ui/EffectPanel.h"
#include "ui/Finish.h"
#include "ui/EqPage.h"
#include "ui/Footswitch.h"
#include "ui/LedDisplay.h"
#include "ui/LevelMeter.h"
#include "ui/ParagonLookAndFeel.h"
#include "ui/PedalBody.h"
#include "ui/PerformanceControls.h"
#include "ui/RigBrowser.h"
#include "ui/SignalChain.h"
#include "ui/OraclePage.h"
#include "ui/CompPage.h"
#include "ui/LimiterPage.h"

// A side strip from the mockup: a title, a gain knob with its value
// underneath, and a level meter.
class GainColumn final : public juce::Component
{
public:
    GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                const juce::String& parameterId, LevelMeterSource& meterSource);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    LevelMeter meter;
};

// A solid triangle printed on the unit beside the screen (the rig up and
// down arrows), in the finish's ink: like juce::ArrowButton, whose colour
// can't change after it's made.
class InkArrowButton final : public juce::Button
{
public:
    InkArrowButton (const juce::String& name, float direction);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path path;
};

// Everything in the plugin window, laid out on a fixed 1600 x 900 canvas.
// That's the mockup image (1672 x 941) scaled by 0.957, so positions are
// written in the mockup's own pixel coordinates and scaled: what you see in
// the mockup at (x, y) is drawn at (x, y) * 0.957. The editor then scales
// the whole view to the window size, staying sharp (everything is vectors).
//
//   header:  logo | PROCESSOR PRESETS SETTINGS | < rig > | undo redo save settings
//   middle:  INPUT | the unit, laid out as a DigiTech GSP21's foot controller:
//            the display along the top, the 7 stomps over RIG 1-5, BANK down, BANK up | OUTPUT
//   dock:    RIG BROWSER | SELECTED RIG | the EQ's overview (or a block's knobs)
//   bottom:  SIGNAL CHAIN
class MainView final : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit MainView (ParagonX1AudioProcessor&);
    ~MainView() override;

    static constexpr int width = 1600;
    static constexpr int height = 900;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    // The blocks in signal-chain order; footswitches, the chain strip and
    // the summary cards select these to show their knobs.
    // The signal chain's blocks (the EQ, DOMINION, COMP and the LIMITER, so far).
    // DOMINION's block opens its page, as its stomp's name does.
    // tunerBlock isn't in the chain (ORACLE, with the tuner, only listens): stomp 1's name opens its page.
    enum Block { eqBlock, dominionBlock, compBlock, limiterBlock, numBlocks, tunerBlock = numBlocks };

    enum class Page { processor, presets, settings, eq, dominion, oracle, comp, limiter };   // eq to limiter: those stomps' own pages

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyFinish();   // colours the printed labels and redraws the unit in the current finish
    void timerCallback() override;

    void selectBlock (int block);
    void refreshPerformance();
    // The bottom row: which five rigs it shows (pages of five within each
    // bank, in the order the rig arrows step), and the page with the current rig.
    std::vector<std::vector<const RigLibrary::Entry*>> rigPages() const;
    void showRigPage (int index);
    void followCurrentRig();
    void showPage (Page);
    void refreshRig();
    void saveRig();
    juce::String valueText (const char* parameterId) const;
    float rawValue (const char* parameterId) const;

    ParagonX1AudioProcessor& audioProcessor;

    // Header
    IconButton processorTab { "Pedalboard and effects", Icons::Type::processor, "PROCESSOR" },
               presetsTab { "Browse, save and organise rigs", Icons::Type::presets, "PRESETS" },
               settingsTab { "Tuner, MIDI and other settings", Icons::Type::settings, "SETTINGS" };
    IconButton previousRig { "Previous rig", Icons::Type::chevronLeft }, nextRig { "Next rig", Icons::Type::chevronRight };
    juce::TextButton rigNameButton;
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::Type::undo }, redoButton { "Redo (Ctrl+Shift+Z)", Icons::Type::redo },
               saveButton { "Save the rig (as a new rig if it's a factory rig or not saved yet)", Icons::Type::save },
               settingsButton { "Settings", Icons::Type::settings };

    GainColumn inputColumn, outputColumn;

    // The pedal unit and what sits on it
    PedalBody pedal;
    LedDisplay display;
    InkArrowButton rigUp { "Previous rig", 0.75f }, rigDown { "Next rig", 0.25f };
    juce::TextButton pedalSave { "SAVE" }, pedalSaveAs { "SAVE AS..." }, bypassButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    // The small keys: UNDO REDO BYPASS
    juce::TextButton undoPedal, redoPedal;
    juce::Label undoLabel { {}, "UNDO" }, redoLabel { {}, "REDO" }, bypassLabel { {}, "BYPASS" };

    juce::Slider rigLevelKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 presenceKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::AudioProcessorValueTreeState::SliderAttachment rigLevelAttachment, presenceAttachment;
    juce::Label rigLevelLabel { {}, "RIG LEVEL" }, presenceLabel { {}, "PRESENCE" };
    // The seven stomps along the top row.
    std::array<std::unique_ptr<Footswitch>, 7> footswitches;
    // The bottom row: five rigs, then BANK down and BANK up.
    std::array<std::unique_ptr<Footswitch>, 5> rigSwitches;
    std::unique_ptr<Footswitch> bankDown, bankUp;
    int rigPage = 0;
    juce::File followedRig;   // the current rig the bottom row last followed
    juce::String followedRigName;

    // The tuner's note for the screen
    TunerReader tunerReader;
    LedDisplay::Contents::Tuner tunerState;
    int ticks = 0;

    // Dock and signal chain
    RigNavigator rigNavigator;
    SelectedRigPanel selectedRig;
    EqOverview eqPanel;   // the dock's default: the EQ's curve, and OPEN EQ
    EffectPanel limiterPanel;   // stomp 7's
    juce::TextButton closePanel { "ECHELON" };   // back to the EQ overview, the dock's default
    SignalChain signalChain;
    int selectedBlock = eqBlock;

    // Other pages
    RigBrowser rigBrowser;
    SettingsPage settingsPage;
    EqPage eqPage;
    DominionPage dominionPage;
    OraclePage oraclePage;
    CompPage compPage;
    LimiterPage limiterPage;
    std::vector<juce::Component*> processorPage;
    Page page = Page::processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
