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

    // The LIMITER's name opens its own page (from 0.16.0); BACK TO PEDAL comes back.
    std::vector<Footswitch*> footswitches;
    findAll (*editor, footswitches);
    REQUIRE (footswitches.size() == 28);   // the seven stomps first, then the rigs, then BANK down and up
    footswitches[6]->onSelect();
    CHECK (limiterPages[0]->isVisible());
    std::vector<juce::TextButton*> buttons;
    findAll (*limiterPages[0], buttons);
    const auto back = std::find_if (buttons.begin(), buttons.end(), [] (auto* b) { return b->getButtonText() == "BACK TO PEDAL"; });
    REQUIRE (back != buttons.end());
    (*back)->onClick();
    CHECK_FALSE (limiterPages[0]->isVisible());
    CHECK (footswitches[0]->isVisible());   // back on the unit
}

TEST_CASE ("Seven stomps in a row over three rows of rigs, labelled from the plan", "[ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1600, 900);

    // Laid out as a DigiTech GSP21's foot controller, four rows deep (0.17.0):
    // the seven stomps in a row, and under them nineteen rigs over three
    // rows, then BANK down and BANK up.
    std::vector<Footswitch*> switches;
    findAll (*editor, switches);
    REQUIRE (switches.size() == 28);
    const std::vector<Footswitch*> stomps (switches.begin(), switches.begin() + 7);

    const juce::StringArray names { "ORACLE", "CONSTRICTOR", "INSERTS", "ECHELON", "DOMINION", "AUTO EQ", "LIMITER" };
    for (size_t i = 0; i < stomps.size(); ++i)
        CHECK (stomps[i]->getTitle() == names[(int) i] + " footswitch");

    const auto bounds = [&] (size_t i) { return switches[i]->getBoundsInParent(); };
    for (size_t i = 0; i < 7; ++i)
    {
        INFO ("Column " << i + 1);
        CHECK (bounds (i).getY() == bounds (0).getY());              // the stomps in one row
        for (size_t row = 1; row < 4; ++row)
        {
            CHECK (bounds (i + 7 * row).getY() == bounds (7 * row).getY());               // each row level
            CHECK (bounds (i + 7 * row).getY() >= bounds (i + 7 * (row - 1)).getBottom()); // under the one above, not overlapping
            CHECK (bounds (i + 7 * row).getX() == bounds (i).getX());                      // in columns
        }
        if (i > 0)
            CHECK (bounds (i).getX() >= bounds (i - 1).getRight());  // side by side, not overlapping
    }

    // ORACLE's name opens its own page, over the pedal; BACK TO PEDAL closes it.
    std::vector<OraclePage*> oraclePages;
    findAll (*editor, oraclePages);
    REQUIRE (oraclePages.size() == 1);
    CHECK_FALSE (oraclePages[0]->isVisible());
    stomps[0]->onSelect();
    CHECK (oraclePages[0]->isVisible());
    REQUIRE (oraclePages[0]->onClose);
    oraclePages[0]->onClose();
    CHECK_FALSE (oraclePages[0]->isVisible());

    // ORACLE, COMP, EQ and LIMITER switch what PR7-E already has.
    const auto isOn = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load() > 0.5f; };
    REQUIRE_FALSE (isOn (ParamIDs::tunerOn));
    stomps[0]->toggle();
    CHECK (isOn (ParamIDs::tunerOn));
    stomps[3]->toggle();
    CHECK (isOn (ParamIDs::eqOn));
    stomps[6]->toggle();
    CHECK (isOn (ParamIDs::limitOn));
    stomps[1]->toggle();
    CHECK (isOn (ParamIDs::compOn));
    stomps[4]->toggle();
    CHECK (isOn (ParamIDs::domOn));

    // The rest aren't built yet: their pads change nothing and their names open nothing.
    for (const auto i : { 2, 5 })
    {
        INFO (names[i]);
        auto& stomp = *stomps[(size_t) i];
        CHECK (stomp.isPlaceholder());
        CHECK_FALSE (stomp.onSelect);
        const auto before = processor.captureRig().parameters;
        stomp.toggle();
        CHECK_FALSE (stomp.isAnyOn());
        CHECK (processor.captureRig().parameters == before);
    }
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

TEST_CASE ("The rig rows: a bank's rigs, nineteen at a time; BANK down and up change the bank shown, a pad loads its rig", "[ui][rigs]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::createTempFile ("rigRow");
    processor.useRigFolder (folder);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    std::vector<Footswitch*> switches;
    findAll (*editor, switches);
    REQUIRE (switches.size() == 28);
    const std::vector<Footswitch*> rigs (switches.begin() + 7, switches.begin() + 26);
    auto& bankDown = *switches[26];
    auto& bankUp = *switches[27];
    CHECK (bankDown.getName() == "BANK");
    CHECK (bankUp.getName() == "BANK");

    std::vector<const RigLibrary::Entry*> bankA;
    for (const auto& e : processor.getRigLibrary().getEntries())
        if (e.rig.bank == 0)
            bankA.push_back (&e);
    REQUIRE (bankA.size() > 7);
    REQUIRE (bankA.size() <= rigs.size());

    // Starting on the page with the current rig (none yet: the first page): all of bank A, the empty pads blank.
    for (size_t i = 0; i < rigs.size(); ++i)
        CHECK (rigs[i]->getName() == (i < bankA.size() ? bankA[i]->rig.name.toUpperCase() : juce::String ("-")));
    CHECK (rigs[7]->getName() == "MODERN METAL VOCAL");   // A11 starts row 3

    // A pad loads its rig and lights its LED.
    rigs[9]->toggle();
    CHECK (processor.getCurrentRig().name == bankA[9]->rig.name);
    CHECK (rigs[9]->isAnyOn());
    CHECK_FALSE (rigs[0]->isAnyOn());

    // BANK up shows bank B without loading one; BANK down goes back.
    bankUp.toggle();
    CHECK (rigs[0]->getName() == "KICK PUNCH");
    CHECK (processor.getCurrentRig().name == bankA[9]->rig.name);
    CHECK_FALSE (rigs[9]->isAnyOn());
    bankDown.toggle();
    CHECK (rigs[9]->getName() == bankA[9]->rig.name.toUpperCase());
    CHECK (rigs[9]->isAnyOn());

    folder.deleteRecursively();
}
