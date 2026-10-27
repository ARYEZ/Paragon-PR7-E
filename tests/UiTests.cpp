#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/EqPage.h"
#include "ui/Finish.h"
#include "ui/PerformanceControls.h"
#include "ui/OraclePage.h"

using Catch::Matchers::WithinAbs;

namespace
{
    template <typename Type>
    void findAll (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAll (*child, found);
        }
    }

    // Every visible component sits inside its parent and has some size.
    void checkLayout (juce::Component& parent, const juce::String& path)
    {
        for (auto* child : parent.getChildren())
        {
            if (! child->isVisible() || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
                continue;

            const auto name = path + "/" + (child->getName().isNotEmpty() ? child->getName()
                                                                          : juce::String (typeid (*child).name()));
            INFO (name << " at " << child->getBounds().toString() << " in " << parent.getLocalBounds().toString());
            CHECK_FALSE (child->getBounds().isEmpty());
            CHECK (parent.getLocalBounds().expanded (1).contains (child->getBounds()));
            checkLayout (*child, name);
        }
    }

    std::vector<float> playThrough (ParagonX1AudioProcessor& processor, const std::vector<float>& input)
    {
        juce::AudioBuffer<float> buffer (2, (int) input.size());
        buffer.clear();
        std::copy (input.begin(), input.end(), buffer.getWritePointer (0));
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        return { buffer.getReadPointer (1), buffer.getReadPointer (1) + buffer.getNumSamples() };
    }

    void set (ParagonX1AudioProcessor& processor, const char* id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    }
}

TEST_CASE ("The window lays out cleanly at every size", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    for (const auto width : { 960, 1280, 1920 })
    {
        DYNAMIC_SECTION (width << " wide")
        {
            editor->setSize (width, width * 9 / 16);
            REQUIRE (editor->getWidth() == width);

            // The view is scaled, not re-laid-out: its transform matches the window.
            auto* view = editor->getChildComponent (0);
            REQUIRE (view != nullptr);
            CHECK_THAT (view->getTransform().mat00, WithinAbs ((float) width / (float) MainView::width, 0.001));

            checkLayout (*view, "view");
        }
    }
}

TEST_CASE ("The window opens as big as fits above the taskbar, up to 1824 x 1026", "[ui]")
{
    using Editor = ParagonX1AudioProcessorEditor;
    // 1080p with a 40-pixel taskbar: room left for the DAW's title bar and toolbar.
    const auto fullHd = Editor::openingSize ({ 1920, 1040 });
    CHECK (fullHd == juce::Point<int> (1706, 959));
    CHECK (fullHd.y + 80 <= 1040);
    CHECK (Editor::openingSize ({ 2560, 1400 }) == juce::Point<int> (1824, 1026));
    const auto laptop = Editor::openingSize ({ 1366, 728 });
    CHECK (laptop.x <= 1366 - 40);
    CHECK (laptop.y + 80 <= 728);
    CHECK (Editor::openingSize ({ 800, 600 }).x == 960);   // never below the smallest size
}

TEST_CASE ("PEARL BLACK recolours the unit's printing, and every open window follows", "[ui]")
{
    Finish::set (Finish::Type::pearlWhite, false);
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> first (processor.createEditor()), second (processor.createEditor());

    // RIG LEVEL is printed on the unit: ink on white, light on black.
    const auto rigLevelColour = [] (juce::Component& editor)
    {
        std::vector<juce::Label*> labels;
        findAll (editor, labels);
        for (auto* label : labels)
            if (label->getText() == "RIG LEVEL")
                return label->findColour (juce::Label::textColourId);
        return juce::Colour();
    };
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // Chosen in one window (here, without saving the preference), it shows in both.
    Finish::set (Finish::Type::pearlBlack, false);
    CHECK (Finish::isBlack());
    CHECK (rigLevelColour (*first) == Finish::ink());
    CHECK (rigLevelColour (*second) == Finish::ink());
    CHECK (Finish::ink().getPerceivedBrightness() > 0.8f);

    // Pearl white with neon keys: the white unit's ink, the black unit's keys.
    Finish::set (Finish::Type::pearlWhiteNeon, false);
    CHECK (! Finish::isBlack());
    CHECK (Finish::neonKeys());
    CHECK (rigLevelColour (*first) == ParagonColours::ink);

    // The key and knob drawing still works in every finish.
    for (const auto type : { Finish::Type::pearlBlack, Finish::Type::pearlWhiteNeon, Finish::Type::pearlWhite })
    {
        Finish::set (type, false);
        first->setSize (1280, 720);
        const auto image = first->createComponentSnapshot (first->getLocalBounds());
        CHECK (image.isValid());
    }
    CHECK (rigLevelColour (*first) == ParagonColours::ink);
}

TEST_CASE ("The chain's blocks open their stomps' pages; there's no dock under the unit (from 0.17.0)", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    // The dock (the rig browser, the selected rig, ECHELON's overview) went for rows 3 and 4.
    std::vector<EqOverview*> eqPanels;
    findAll (*editor, eqPanels);
    CHECK (eqPanels.empty());

    std::vector<SignalChain*> chains;
    findAll (*editor, chains);
    REQUIRE (chains.size() == 1);
    std::vector<EqPage*> eqPages;
    std::vector<LimiterPage*> limiterPages;
    findAll (*editor, eqPages);
    findAll (*editor, limiterPages);
    REQUIRE (eqPages.size() == 1);
    REQUIRE (limiterPages.size() == 1);

    // ECHELON in the chain opens its page; BACK TO PEDAL comes back to the unit.
    chains[0]->onSelect (0);
    CHECK (eqPages[0]->isVisible());
    eqPages[0]->onClose();
    CHECK_FALSE (eqPages[0]->isVisible());

    // The LIMITER's EDIT opens its own page; BACK TO PEDAL comes back.
    std::vector<RackUnit*> racks;
    findAll (*editor, racks);
    REQUIRE (racks.size() == 4);
    racks[3]->getEdit().onClick();
    CHECK (limiterPages[0]->isVisible());
    std::vector<juce::TextButton*> buttons;
    findAll (*limiterPages[0], buttons);
    const auto back = std::find_if (buttons.begin(), buttons.end(), [] (auto* b) { return b->getButtonText() == "BACK TO PEDAL"; });
    REQUIRE (back != buttons.end());
    (*back)->onClick();
    CHECK_FALSE (limiterPages[0]->isVisible());
    CHECK (racks[0]->isVisible());   // back on the unit
}

TEST_CASE ("Four rack units, one to a row in the chain's order, each switched by its POWER footswitch", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);

    // 0.18.0: the four rows are rack units, after the owner's photo of a
    // rack (SPX90, PCM70, SDE-3000): ECHELON, DOMINION, CONSTRICTOR, LIMITER.
    std::vector<RackUnit*> racks;
    findAll (*editor, racks);
    REQUIRE (racks.size() == 4);
    const juce::StringArray names { "ECHELON", "DOMINION", "CONSTRICTOR", "LIMITER" };
    for (size_t i = 0; i < racks.size(); ++i)
    {
        INFO (names[(int) i]);
        CHECK (racks[i]->getSpec().name == names[(int) i]);
        if (i > 0)
        {
            CHECK (racks[i]->getX() == racks[0]->getX());                                    // stacked, left edges level
            CHECK (racks[i]->getY() >= racks[i - 1]->getBottom());                           // under the one above, not overlapping
        }
    }
    CHECK (racks[3]->getBottom() <= editor->getChildComponent (0)->getHeight());

    // The footswitches are the units' POWER switches: no stomps or rig pads any more.
    std::vector<Footswitch*> switches;
    findAll (*editor, switches);
    REQUIRE (switches.size() == 4);
    const auto isOn = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load() > 0.5f; };
    const char* onIds[] = { ParamIDs::eqOn, ParamIDs::domOn, ParamIDs::compOn, ParamIDs::limitOn };
    for (size_t i = 0; i < racks.size(); ++i)
    {
        INFO (names[(int) i]);
        CHECK (&racks[i]->getPower() == switches[i]);
        REQUIRE_FALSE (isOn (onIds[i]));
        racks[i]->getPower().toggle();
        CHECK (isOn (onIds[i]));
    }

    // ORACLE, stomp 1 before 0.18.0, is a small key beside BYPASS: it opens
    // its page, and is lit while ORACLE listens.
    auto* oracleKey = dynamic_cast<juce::Button*> (editor->findChildWithID ("oracleKey"));
    if (oracleKey == nullptr)
    {
        std::vector<juce::TextButton*> buttons;
        findAll (*editor, buttons);
        for (auto* b : buttons)
            if (b->getComponentID() == "oracleKey")
                oracleKey = b;
    }
    REQUIRE (oracleKey != nullptr);
    std::vector<OraclePage*> oraclePages;
    findAll (*editor, oraclePages);
    REQUIRE (oraclePages.size() == 1);
    CHECK_FALSE (oraclePages[0]->isVisible());
    oracleKey->onClick();
    CHECK (oraclePages[0]->isVisible());
    oraclePages[0]->onClose();
    CHECK_FALSE (oraclePages[0]->isVisible());
    CHECK_FALSE (oracleKey->getToggleState());   // its LED dark: ORACLE is off
}

TEST_CASE ("Rack units: the keys switch what they're labelled, the knobs are their parameters", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<RackUnit*> racks;
    findAll (*editor, racks);
    REQUIRE (racks.size() == 4);
    const auto value = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); };
    const auto press = [&] (RackUnit& rack, const juce::String& label)
    {
        for (const auto& key : rack.getSpec().keys)
            if (key.label == label)
            {
                key.press();
                return key.isOn();
            }
        FAIL ("no key " << label);
        return false;
    };

    CHECK (press (*racks[0], "AUTO GAIN"));
    CHECK (value (ParamIDs::eqAutoGain) > 0.5f);
    CHECK (press (*racks[1], "DELTA"));
    CHECK (value (ParamIDs::domDelta) > 0.5f);
    CHECK (press (*racks[1], "HARD"));
    CHECK (juce::roundToInt (value (ParamIDs::domMode)) == 1);
    CHECK (press (*racks[2], "FET"));
    CHECK (juce::roundToInt (value (ParamIDs::compCharacter)) == 2);
    CHECK (press (*racks[2], "OPTO"));   // the characters are one choice: OPTO replaces FET
    CHECK (juce::roundToInt (value (ParamIDs::compCharacter)) == 1);
    const auto truePeak = value (ParamIDs::limitTruePeak) > 0.5f;
    CHECK (press (*racks[3], "TRUE PEAK") == ! truePeak);

    // ECHELON's MATCH opens ECHELON's page.
    std::vector<EqPage*> eqPages;
    findAll (*editor, eqPages);
    REQUIRE (eqPages.size() == 1);
    press (*racks[0], "MATCH");
    CHECK (eqPages[0]->isVisible());

    const std::array<std::array<const char*, 2>, 4> knobs { { { ParamIDs::eqOutput, ParamIDs::eqGainScale },
                                                              { ParamIDs::domDepth, ParamIDs::domDetail },
                                                              { ParamIDs::compThreshold, ParamIDs::compRatio },
                                                              { ParamIDs::limitGain, ParamIDs::limitCeiling } } };
    for (size_t i = 0; i < racks.size(); ++i)
        for (size_t k = 0; k < 2; ++k)
            CHECK (juce::String (racks[i]->getSpec().knobs[k].parameterId) == knobs[i][k]);
}

TEST_CASE ("BYPASS passes the clean guitar, on both channels", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Make the processed sound very different from the input...
    set (processor, ParamIDs::eqOn, 1.0f);
    set (processor, ParamIDs::eqOutput, 12.0f);
    set (processor, ParamIDs::inputGain, 12.0f);
    set (processor, ParamIDs::bypass, 1.0f);

    std::vector<float> input (512);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = 0.3f * std::sin (0.05f * (float) i);

    for (int block = 0; block < 4; ++block)
        playThrough (processor, input);   // let the 20 ms bypass fade finish (one block is ~11 ms)
    const auto output = playThrough (processor, input);
    for (size_t i = 0; i < input.size(); ++i)
        REQUIRE_THAT (output[i], WithinAbs (input[i], 1.0e-6));

    CHECK (processor.getBypassParameter() == processor.getParameters().getParameter (ParamIDs::bypass));
}

TEST_CASE ("No latency: BYPASS and the processed sound line up with the input", "[ui][bypass]")
{
    // Nothing in PR7-E's chain looks ahead (the NAM amp, which did, went with
    // PR7-B's bass blocks), so the DAW needn't compensate and bypassing
    // can't shift the sound in time.
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 2, 44100.0, 512);
    processor.prepareToPlay (44100.0, 512);
    CHECK (processor.getLatencySamples() == 0);

    std::vector<float> click (512, 0.0f);
    click[10] = 0.5f;
    for (const auto bypassed : { false, true })
    {
        INFO ("Bypassed: " << (bypassed ? "yes" : "no"));
        set (processor, ParamIDs::bypass, bypassed ? 1.0f : 0.0f);
        for (int block = 0; block < 4; ++block)
            playThrough (processor, std::vector<float> (512, 0.0f));   // let the bypass fade finish
        const auto output = playThrough (processor, click);
        CHECK_THAT (output[10], WithinAbs (0.5, 1.0e-6));
        CHECK_THAT (output[11], WithinAbs (0.0, 1.0e-6));
    }
}

TEST_CASE ("BYPASS is global: rigs don't change it and it doesn't mark them modified", "[ui][bypass]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1UiTests").getChildFile (juce::Uuid().toString());
    processor.useRigFolder (folder);

    processor.loadRig (*processor.getRigLibrary().findFactory ("Init"));
    set (processor, ParamIDs::eqAutoGain, 1.0f);
    REQUIRE (processor.saveCurrentRigAs ("Reverb Rig"));
    const auto reverbRig = processor.getCurrentRig().file;
    processor.loadRig (*processor.getRigLibrary().findFactory ("Init"));
    set (processor, ParamIDs::bypass, 1.0f);
    CHECK_FALSE (processor.isRigModified());

    processor.loadRig (*processor.getRigLibrary().find (reverbRig));
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::eqAutoGain)->load() > 0.5f);   // the rig did load
    CHECK (processor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f);
    folder.deleteRecursively();
}

TEST_CASE ("Rigs off the unit (0.18.0): the arrows step through them", "[ui][rigs]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::createTempFile ("rigRow");
    processor.useRigFolder (folder);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    std::vector<InkArrowButton*> arrows;
    findAll (*editor, arrows);
    REQUIRE (arrows.size() == 2);   // up, then down, beside the screen
    const auto& entries = processor.getRigLibrary().getEntries();
    REQUIRE (entries.size() > 2);
    processor.loadRig (entries[0]);
    arrows[1]->onClick();
    CHECK (processor.getCurrentRig().name == entries[1].rig.name);
    arrows[0]->onClick();
    CHECK (processor.getCurrentRig().name == entries[0].rig.name);

    folder.deleteRecursively();
}

TEST_CASE ("Opening the window changes nothing in the sound: the rack units only read and show", "[ui][sound]")
{
    // Asked after 0.18.0 ("it sounds a little different"): two processors with
    // the same rig, one with its window open and its timers running (the rack
    // units' meters, keys and knobs), play the same stereo mix. The settings
    // and the sound must come out identical, sample for sample.
    const auto prepare = [] (ParagonX1AudioProcessor& processor)
    {
        processor.setPlayConfigDetails (2, 2, 48000.0, 256);
        processor.prepareToPlay (48000.0, 256);
        const auto* rig = processor.getRigLibrary().findFactory ("Modern Metal Mix");   // ECHELON, DOMINION, CONSTRICTOR
        REQUIRE (rig != nullptr);
        processor.loadRig (*rig);
        set (processor, ParamIDs::limitOn, 1.0f);
        set (processor, ParamIDs::limitMode, 8.0f);   // Modern
        set (processor, ParamIDs::limitGain, 6.0f);
        set (processor, ParamIDs::limitTruePeak, 1.0f);
    };
    ParagonX1AudioProcessor closed, open;
    prepare (closed);
    prepare (open);
    juce::MemoryBlock before;
    open.getStateInformation (before);

    std::unique_ptr<juce::AudioProcessorEditor> editor (open.createEditor());
    editor->setSize (1600, 900);
    juce::MemoryBlock afterOpening;
    open.getStateInformation (afterOpening);
    CHECK (afterOpening == before);   // opening it set nothing

    juce::Random random (3);
    juce::AudioBuffer<float> a (2, 256), b (2, 256);
    juce::MidiBuffer midi;
    double largest = 0.0;
    for (int block = 0, n = 0; block < 600; ++block)
    {
        for (int i = 0; i < 256; ++i, ++n)
        {
            const auto t = n / 48000.0, beat = std::fmod (t, 0.5);
            const auto kick = 0.6 * std::exp (-beat * 14.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 60.0 * std::exp (-beat * 40.0)) * beat);
            const auto left = kick + 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 2500.0 * t) + 0.06 * (random.nextFloat() * 2.0 - 1.0);
            const auto right = 0.9 * kick + 0.06 * (random.nextFloat() * 2.0 - 1.0);
            for (auto* buffer : { &a, &b })
            {
                buffer->setSample (0, i, (float) left);
                buffer->setSample (1, i, (float) right);
            }
        }
        closed.processBlock (a, midi);
        open.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                largest = std::max (largest, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        if (block % 20 == 0)   // the window's timers (30 Hz) run as they would
        {
            juce::Thread::sleep (35);
            juce::Timer::callPendingTimersSynchronously();
        }
    }
    CHECK (largest == 0.0);
    juce::MemoryBlock afterPlaying;
    open.getStateInformation (afterPlaying);
    CHECK (afterPlaying == before);
}
