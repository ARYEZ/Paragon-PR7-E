#include "MainView.h"

#include "Parameters.h"
#include "ui/Logo.h"

namespace
{
    // The mockup image is 1672 px wide; our canvas is 1600. Every position
    // below is measured off the mockup and scaled by this.
    constexpr float mockScale = (float) MainView::width / 1672.0f;

    juce::Rectangle<float> mockF (float left, float top, float right, float bottom)
    {
        return juce::Rectangle<float>::leftTopRightBottom (left, top, right, bottom) * mockScale;
    }

    juce::Rectangle<int> mock (float left, float top, float right, float bottom)
    {
        return mockF (left, top, right, bottom).toNearestInt();
    }

    juce::Point<float> mockPoint (float x, float y) { return juce::Point<float> (x, y) * mockScale; }

    // Header groups.
    const auto tabsGroup = mockF (336, 10, 795, 55);
    const auto rigGroup = mockF (838, 10, 1135, 55);

    // The unit between the INPUT and OUTPUT strips (its component bounds
    // include room for its glow), laid out as a DigiTech GSP21's foot
    // controller: the face runs its whole width (there's no expression
    // pedal bay since 0.12.0), with the display centred along the top and
    // two long rows of seven footswitches.
    const auto pedalBounds = mock (126, 68, 1552, 564);
    constexpr float faceLeft = 164, faceRight = 1514;

    // The small keys (UNDO REDO BYPASS): column centres, label and key tops.
    constexpr float clusterX[] = { 470, 535, 600 };
    constexpr float clusterLabelY = 132, clusterButtonY = 149;

    // The two rows of footswitches: seven across the face, each `switchPitch` wide.
    constexpr float switchLeft = 176, switchPitch = 190;
    constexpr float switchRowY[] = { 248, 400 }, switchHeight = 124;
    // A stomp whose name opens no block (not built yet, or no knobs).
    constexpr int noBlock = -1;

    void styleInkLabel (juce::Label& label, float size)
    {
        label.setFont (juce::FontOptions (size, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::ink);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
InkArrowButton::InkArrowButton (const juce::String& name, float direction) : juce::Button (name)
{
    path.addTriangle (0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
    path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * direction, 0.5f, 0.5f));
}

void InkArrowButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto p = path;
    const auto offset = down ? 1.0f : 0.0f;
    p.applyTransform (path.getTransformToScaleToFit (offset, offset, (float) getWidth() - 3.0f, (float) getHeight() - 3.0f, false));
    juce::DropShadow (juce::Colours::black.withAlpha (0.3f), down ? 2 : 4, {}).drawForPath (g, p);
    g.setColour (Finish::ink());
    g.fillPath (p);
}

//==============================================================================
GainColumn::GainColumn (const juce::String& title, juce::AudioProcessorValueTreeState& parameters,
                        const juce::String& parameterId, LevelMeterSource& meterSource)
    : attachment (parameters, parameterId, knob),
      meter (meterSource)
{
    titleLabel.setText (title, juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (juce::FontOptions (14.5f, juce::Font::bold));

    // Set on the knob itself: the editor's look-and-feel is attached after the
    // knob's value box has already been created, so it wouldn't pick these up.
    knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
    knob.setDoubleClickReturnValue (true, 0.0);   // double-click resets to 0 dB

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (knob);
    addAndMakeVisible (meter);
}

void GainColumn::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
}

void GainColumn::resized()
{
    // Proportions from the mockup: title, a compact knob, its value in
    // orange, then a tall slim meter.
    auto area = getLocalBounds();
    titleLabel.setBounds (area.withHeight (26).withY (14));
    knob.setBounds (juce::Rectangle<int> (76, 92).withCentre ({ area.getCentreX(), 86 }));
    meter.setBounds (juce::Rectangle<int> (46, 0).withX (area.getCentreX() - 26).withY (146).withBottom (area.getBottom() - 22));
}

namespace
{
    // The menus' gradient at block `index` of `count` across the signal chain.
    juce::Colour chainColour (int index, int count)
    {
        return ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f }).getColourAtPosition ((double) index / juce::jmax (1, count - 1));
    }
}

//==============================================================================
MainView::MainView (ParagonX1AudioProcessor& processorToShow)
    : audioProcessor (processorToShow),
      inputColumn ("INPUT", processorToShow.getParameters(), ParamIDs::inputGain, processorToShow.getInputMeter()),
      outputColumn ("OUTPUT", processorToShow.getParameters(), ParamIDs::outputGain, processorToShow.getOutputMeter()),
      bypassAttachment (processorToShow.getParameters(), ParamIDs::bypass, bypassButton),
      rigLevelAttachment (processorToShow.getParameters(), ParamIDs::rigLevel, rigLevelKnob),
      presenceAttachment (processorToShow.getParameters(), ParamIDs::presence, presenceKnob),
      tunerReader (processorToShow),
      selectedRig (processorToShow),
      eqPanel (processorToShow),
      // The blocks that shape the sound, in order; it grows as the stomps are built.
      limiterPanel (processorToShow.getParameters(),
                    { { "LIMIT", { { ParamIDs::limitOn, "ON" }, { ParamIDs::limitGain, "GAIN" }, { ParamIDs::limitCeiling, "OUTPUT" },
                                   { ParamIDs::limitMode, "STYLE" }, { ParamIDs::limitLookahead, "LOOKAHEAD" }, { ParamIDs::limitTruePeak, "TRUE PEAK" } } },
                      { "OUTPUT", { { ParamIDs::limitTrim, "OUTPUT TRIM" } } } }),
      // Each block in the red to pink to orange of the menus and DOMINION's
      // and ORACLE's pages, by its place in the chain: red first, orange last.
      signalChain (processorToShow.getParameters(), {
          { "EQ", Icons::Type::eq, chainColour (0, 4), ParamIDs::eqOn },
          { "DOMINION", Icons::Type::filterPitch, chainColour (1, 4), ParamIDs::domOn },
          { "COMP", Icons::Type::comp, chainColour (2, 4), ParamIDs::compOn },
          { "LIMITER", Icons::Type::comp, chainColour (3, 4), ParamIDs::limitOn } }),
      rigBrowser (processorToShow),
      settingsPage (processorToShow),
      eqPage (processorToShow),
      dominionPage (processorToShow),
      oraclePage (processorToShow),
      compPage (processorToShow)
{
    auto& state = processorToShow.getParameters();

    //==========================================================================
    // Header
    for (auto* tab : { &processorTab, &presetsTab, &settingsTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (100);
        addAndMakeVisible (*tab);
    }
    processorTab.setToggleState (true, juce::dontSendNotification);
    processorTab.setComponentID ("processorTab");
    presetsTab.setComponentID ("presetsTab");
    settingsTab.setComponentID ("settingsTab");
    processorTab.onClick = [this] { showPage (Page::processor); };
    presetsTab.onClick = [this] { showPage (Page::presets); };
    settingsTab.onClick = [this] { showPage (Page::settings); };

    previousRig.onClick = [this] { audioProcessor.stepRig (-1); };
    nextRig.onClick = [this] { audioProcessor.stepRig (1); };
    rigNameButton.getProperties().set (ParagonLookAndFeel::plainTextProperty, 19.0f);
    rigNameButton.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    rigNameButton.setTooltip ("The current rig. * means it has changed since it was loaded or saved. Click to browse rigs.");
    rigNameButton.onClick = [this] { showPage (Page::presets); };

    undoButton.onClick = [this] { audioProcessor.undo(); };
    redoButton.onClick = [this] { audioProcessor.redo(); };
    saveButton.onClick = [this] { saveRig(); };
    settingsButton.onClick = [this] { showPage (page == Page::settings ? Page::processor : Page::settings); };

    for (auto* component : std::initializer_list<juce::Component*> { &previousRig, &nextRig, &rigNameButton,
                                                                      &undoButton, &redoButton, &saveButton, &settingsButton,
                                                                      &inputColumn, &outputColumn })
        addAndMakeVisible (component);

    //==========================================================================
    // The pedal unit
    display.onClick = [this] { showPage (Page::presets); };
    rigUp.onClick = [this] { audioProcessor.stepRig (-1); };
    rigDown.onClick = [this] { audioProcessor.stepRig (1); };

    for (auto* button : { &pedalSave, &pedalSaveAs, &bypassButton, &undoPedal, &redoPedal })
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    pedalSave.onClick = [this] { saveRig(); };
    pedalSaveAs.onClick = [this] { rigBrowser.promptSaveAs(); };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the whole plugin (the same as your DAW's bypass button)");

    undoPedal.setTooltip ("Undo the last change (Ctrl+Z)");
    redoPedal.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoPedal.onClick = [this] { audioProcessor.undo(); };
    redoPedal.onClick = [this] { audioProcessor.redo(); };

    // The cluster's labels are printed in red, as in the mockup.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel })
    {
        styleInkLabel (*label, 9.5f);
        label->setColour (juce::Label::textColourId, ParagonColours::glow.darker (0.15f));
    }

    for (auto* knob : { &rigLevelKnob, &presenceKnob })
    {
        knob->getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        knob->setDoubleClickReturnValue (true, 0.0);
        knob->setPopupDisplayEnabled (true, true, this);   // shows the value while dragging
    }
    rigLevelKnob.setTooltip ("This rig's volume, saved with the rig: use it to balance rigs. "
                             "The OUTPUT knob on the right is the overall volume.");
    presenceKnob.setTooltip ("Cut or boost the top end (above about 3.5 kHz), like an amp's presence control.");
    styleInkLabel (rigLevelLabel, 12.5f);
    styleInkLabel (presenceLabel, 12.5f);

    tunerReader.onReading = [this] (const TunerReader::Reading& reading)
    {
        tunerState.hasNote = reading.hasNote;
        tunerState.note = reading.note.name;
        tunerState.octave = reading.note.octave;
        tunerState.cents = reading.note.cents;
        tunerState.frequency = reading.frequency;
        refreshRig();
    };

    // The seven stomps, from the plan in CLAUDE.md. Each press turns its
    // block on or off; clicking its name shows the block's knobs. Most aren't
    // built yet: those have no parameters (so nothing is saved under an ID we
    // might regret), a dark LED and a pad that does nothing.
    const auto addStomp = [this, &state] (int number, const char* name, juce::StringArray onIds, int block,
                                          std::function<juce::String()> describe)
    {
        auto& footswitch = footswitches[(size_t) number - 1];
        footswitch = std::make_unique<Footswitch> (number, name, state, std::move (onIds));
        footswitch->describe = std::move (describe);
        if (block != noBlock)
            footswitch->onSelect = [this, block] { selectBlock (block); };
        footswitch->refresh();
    };
    const auto planned = [] (const char* what) { return [what] { return juce::String (what); }; };

    // Top row. ORACLE (with the tuner) transcribes a part to MIDI as the DAW
    // plays it, and shows its pitch on the screen (the TUNER key and holding
    // TAP switch it too); its name opens its own page.
    addStomp (1, "ORACLE", { ParamIDs::tunerOn }, tunerBlock, [this]
    {
        const auto notes = audioProcessor.getOracle().getNotes().size();
        return notes == 0 ? juce::String ("Melody to MIDI") : juce::String ((int) notes) + (notes == 1 ? " note" : " notes");
    });
    footswitches[0]->onSelect = [this] { showPage (Page::oracle); };
    // COMP: PR7-C's compressor with bands (inspired by Pro-MB), after the EQ; its name opens its own page.
    addStomp (2, "COMP", { ParamIDs::compOn }, compBlock, [this]
    {
        return valueText (ParamIDs::compCharacter) + "  " + valueText (ParamIDs::compThreshold) + "  " + valueText (ParamIDs::compRatio);
    });
    footswitches[1]->onSelect = [this] { showPage (Page::comp); };
    addStomp (3, "INSERTS", {}, noBlock, planned ("Plugin slots: to plan"));

    // EQ (inspired by FabFilter Pro-Q 4): its name opens its own page.
    addStomp (4, "EQ", { ParamIDs::eqOn }, eqBlock, [this]
    {
        const auto settings = audioProcessor.getEqSettings();
        const auto bands = std::count_if (settings.bands.begin(), settings.bands.end(), [] (const eq::Band& b) { return b.used; });
        return bands == 0 ? juce::String ("No bands yet") : juce::String (bands) + (bands == 1 ? " band" : " bands");
    });
    footswitches[3]->onSelect = [this] { showPage (Page::eq); };
    // DOMINION: resonance suppression, inspired by soothe; its name opens its own page.
    addStomp (5, "DOMINION", { ParamIDs::domOn }, dominionBlock, [this]
    {
        return valueText (ParamIDs::domMode) + "  DEPTH " + valueText (ParamIDs::domDepth) + "  DETAIL " + valueText (ParamIDs::domDetail);
    });
    footswitches[4]->onSelect = [this] { showPage (Page::dominion); };
    addStomp (6, "AUTO EQ", {}, noBlock, planned ("Adaptive EQ: to plan"));
    // LIMITER: PR7-C's limiter and output trim, last in the chain; its name opens its knobs.
    addStomp (7, "LIMITER", { ParamIDs::limitOn }, limiterBlock, [this]
    {
        const auto ceiling = rawValue (ParamIDs::limitCeiling) + rawValue (ParamIDs::limitTrim);
        return "Ceiling " + juce::String (ceiling, 1) + (rawValue (ParamIDs::limitTruePeak) > 0.5f ? " dBTP, " : " dB, ")
               + valueText (ParamIDs::limitMode).toUpperCase();
    });

    // The bottom row, as the GSP21's in its five-preset mode: five rigs from
    // the current bank (pages of five), then BANK down and BANK up, which
    // change the five shown without loading one, as on the GSP.
    for (size_t i = 0; i < rigSwitches.size(); ++i)
    {
        auto& rigSwitch = rigSwitches[i];
        rigSwitch = std::make_unique<Footswitch> ((int) i + 1, "-", state, juce::StringArray());
        rigSwitch->onPress = [this, i]
        {
            const auto pages = rigPages();
            if (juce::isPositiveAndBelow (rigPage, (int) pages.size()) && i < pages[(size_t) rigPage].size())
                audioProcessor.loadRig (*pages[(size_t) rigPage][i]);
            refreshRig();
        };
        rigSwitch->onSelect = rigSwitch->onPress;
        rigSwitch->isLit = [this, i]
        {
            const auto pages = rigPages();
            return juce::isPositiveAndBelow (rigPage, (int) pages.size()) && i < pages[(size_t) rigPage].size()
                && pages[(size_t) rigPage][i] == audioProcessor.findCurrentRigEntry();
        };
    }
    const auto addBankSwitch = [this, &state] (std::unique_ptr<Footswitch>& bankSwitch, const char* name, int direction)
    {
        bankSwitch = std::make_unique<Footswitch> (0, name, state, juce::StringArray());
        bankSwitch->setNumberText (direction < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc"))    // down triangle
                                                 : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")));  // up triangle
        bankSwitch->onPress = [this, direction] { showRigPage (rigPage + direction); };
        bankSwitch->onSelect = bankSwitch->onPress;
        bankSwitch->isLit = [] { return false; };
        bankSwitch->describe = [this]
        {
            const auto pages = rigPages();
            if (! juce::isPositiveAndBelow (rigPage, (int) pages.size()) || pages[(size_t) rigPage].empty())
                return juce::String();
            return "Showing " + pages[(size_t) rigPage].front()->rig.getSlotName() + " to " + pages[(size_t) rigPage].back()->rig.getSlotName();
        };
    };
    addBankSwitch (bankDown, "BANK", -1);
    addBankSwitch (bankUp, "BANK", 1);

    addAndMakeVisible (pedal);   // first, so everything on the pedal sits on top of it
    processorPage = { &pedal, &display, &rigUp, &rigDown, &pedalSave, &pedalSaveAs, &bypassButton,
                      &undoPedal, &redoPedal, &undoLabel, &redoLabel, &bypassLabel,
                      &rigLevelKnob, &presenceKnob, &rigLevelLabel, &presenceLabel,
                      &rigNavigator, &selectedRig };
    for (auto& footswitch : footswitches)
        processorPage.push_back (footswitch.get());
    for (auto& rigSwitch : rigSwitches)
        processorPage.push_back (rigSwitch.get());
    processorPage.push_back (bankDown.get());
    processorPage.push_back (bankUp.get());
    for (auto* component : processorPage)
        addAndMakeVisible (component);

    addChildComponent (eqPanel);
    addChildComponent (limiterPanel);
    limiterPanel.setHeader (Icons::Type::comp, ParagonColours::comp, "LIMITER");
    limiterPanel.setMeter ([this]
    {
        return -juce::jmax (audioProcessor.getLimiterReductionDb (0), audioProcessor.getLimiterReductionDb (1));
    });

    eqPanel.onOpen = [this] { showPage (Page::eq); };
    eqPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (eqPage);
    dominionPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (dominionPage);
    oraclePage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (oraclePage);
    compPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (compPage);

    closePanel.setTooltip ("Back to the EQ");
    closePanel.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    closePanel.onClick = [this] { selectBlock (eqBlock); };
    addChildComponent (closePanel);

    rigNavigator.onShow = [this] (int filter, int bank)
    {
        rigBrowser.showFilter (filter, bank);
        showPage (Page::presets);
    };
    selectedRig.onSave = [this] { saveRig(); };
    selectedRig.onSaveAs = [this] { rigBrowser.promptSaveAs(); };

    signalChain.onSelect = [this] (int block)
    {
        if (block == dominionBlock)
            showPage (Page::dominion);
        else if (block == compBlock)
            showPage (Page::comp);
        else
            selectBlock (block);
    };
    addAndMakeVisible (signalChain);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);

    selectBlock (eqBlock);   // the dock shows the graphic EQ until another block is picked

    audioProcessor.rigBroadcaster.addChangeListener (this);
    changeListenerCallback (nullptr);
    Finish::changes().addChangeListener (this);
    applyFinish();

    // The modified marker, tempo and undo state can change from the audio
    // thread or the host, so poll them. 30 Hz for the TAP light's flash.
    startTimerHz (30);
    timerCallback();

    setWantsKeyboardFocus (true);
}

MainView::~MainView()
{
    Finish::changes().removeChangeListener (this);
    audioProcessor.rigBroadcaster.removeChangeListener (this);
}

juce::String MainView::valueText (const char* parameterId) const
{
    return audioProcessor.getParameters().getParameter (parameterId)->getCurrentValueAsText();
}

float MainView::rawValue (const char* parameterId) const
{
    return audioProcessor.getParameters().getRawParameterValue (parameterId)->load();
}

void MainView::selectBlock (int block)
{
    // The selected block's knobs fill the middle of the dock; the graphic EQ is the default.
    selectedBlock = block;
    const auto onProcessorPage = page == Page::processor;
    eqPanel.setVisible (block == eqBlock && onProcessorPage);
    limiterPanel.setVisible (block == limiterBlock && onProcessorPage);
    closePanel.setVisible (block != eqBlock && onProcessorPage);
    if (block != eqBlock)
        closePanel.toFront (false);

    const int footswitchBlocks[] = { tunerBlock, compBlock, noBlock, eqBlock, dominionBlock, noBlock, limiterBlock };
    static_assert (std::size (footswitchBlocks) == std::tuple_size_v<decltype (footswitches)>);
    for (size_t i = 0; i < footswitches.size(); ++i)
        footswitches[i]->setSelected (footswitchBlocks[i] == block);

    signalChain.setSelected (block);
}

void MainView::showPage (Page newPage)
{
    page = newPage;
    // The EQ's page belongs to the pedal: PROCESSOR stays lit, and goes back to it.
    auto& tab = newPage == Page::presets ? presetsTab : newPage == Page::settings ? settingsTab : processorTab;
    tab.setToggleState (true, juce::dontSendNotification);

    for (auto* component : processorPage)
        component->setVisible (newPage == Page::processor);
    rigBrowser.setVisible (newPage == Page::presets);
    settingsPage.setVisible (newPage == Page::settings);
    eqPage.setVisible (newPage == Page::eq);
    if (newPage == Page::eq)
        eqPage.getDisplay().grabKeyboardFocus();
    dominionPage.setVisible (newPage == Page::dominion);
    if (newPage == Page::dominion)
        dominionPage.getDisplay().grabKeyboardFocus();
    compPage.setVisible (newPage == Page::comp);
    if (newPage == Page::comp)
        compPage.getDisplay().grabKeyboardFocus();
    oraclePage.setVisible (newPage == Page::oracle);
    if (newPage == Page::oracle)
        oraclePage.getRoll().grabKeyboardFocus();
    selectBlock (selectedBlock);
}

void MainView::refreshRig()
{
    const auto rig = audioProcessor.getCurrentRig();
    const auto* entry = audioProcessor.findCurrentRigEntry();
    const auto modified = audioProcessor.isRigModified();
    const auto bypassed = audioProcessor.getParameters().getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;

    const auto headerText = rig.name + (modified ? " *" : "");
    if (rigNameButton.getButtonText() != headerText)
        rigNameButton.setButtonText (headerText);

    followCurrentRig();
    display.setContents ({ entry != nullptr ? entry->rig.number : 0, rig.name,
                           entry != nullptr ? Rig::bankName (entry->rig.bank) : juce::String(), modified, bypassed, tunerState });
    selectedRig.refresh();
}

void MainView::timerCallback()
{
    // The rig details change rarely: 5 times a second is plenty for them.
    if (ticks++ % 6 == 0)
        refreshRig();

    refreshPerformance();
}

void MainView::refreshPerformance()
{
    const auto canUndo = audioProcessor.canUndo(), canRedo = audioProcessor.canRedo();
    undoButton.setEnabled (canUndo);
    redoButton.setEnabled (canRedo);
    undoPedal.setToggleState (canUndo, juce::dontSendNotification);   // lit while there's something to undo
    redoPedal.setToggleState (canRedo, juce::dontSendNotification);

    // The tuner runs only while it's switched on.
    auto& parameters = audioProcessor.getParameters();
    const auto tunerOn = parameters.getRawParameterValue (ParamIDs::tunerOn)->load() > 0.5f;
    tunerReader.setActive (tunerOn);
    if (tunerState.active != tunerOn)
    {
        tunerState.active = tunerOn;
        if (! tunerOn)
            tunerState.hasNote = false;
        refreshRig();
    }
}

bool MainView::keyPressed (const juce::KeyPress& key)
{
    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on a Mac). DAWs often keep the
    // keyboard for themselves, so these work when the plugin window has focus.
    const auto modifiers = key.getModifiers();
    if (! modifiers.isCommandDown())
        return false;

    const auto keyCode = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (keyCode == 'Z')
    {
        if (modifiers.isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        return true;
    }
    if (keyCode == 'Y')
    {
        audioProcessor.redo();
        return true;
    }
    return false;
}

std::vector<std::vector<const RigLibrary::Entry*>> MainView::rigPages() const
{
    // Each bank's rigs in order, five to a page; banks with no rigs have none.
    // A page also starts at each ten (01, 11, 21...), so the genre rigs (x11
    // to x19) begin a page of their own whatever comes before them.
    std::vector<std::vector<const RigLibrary::Entry*>> pages;
    int bank = -1, tens = -1;
    for (const auto& entry : audioProcessor.getRigLibrary().getEntries())
    {
        const auto entryTens = (entry.rig.number - 1) / 10;
        if (entry.rig.bank != bank || entryTens != tens || pages.back().size() == 5)
            pages.emplace_back();
        bank = entry.rig.bank;
        tens = entryTens;
        pages.back().push_back (&entry);
    }
    return pages;
}

void MainView::showRigPage (int index)
{
    const auto pages = rigPages();
    rigPage = pages.empty() ? 0 : ((index % (int) pages.size()) + (int) pages.size()) % (int) pages.size();
    for (size_t i = 0; i < rigSwitches.size(); ++i)
    {
        const auto* entry = rigPage < (int) pages.size() && i < pages[(size_t) rigPage].size() ? pages[(size_t) rigPage][i] : nullptr;
        rigSwitches[i]->setNumberText (entry != nullptr ? entry->rig.getSlotName() : juce::String (i + 1));
        rigSwitches[i]->setName (entry != nullptr ? entry->rig.name.toUpperCase() : juce::String ("-"));
        rigSwitches[i]->refresh();
    }
    bankDown->refresh();
    bankUp->refresh();
}

void MainView::followCurrentRig()
{
    // When the rig changes (loaded here, from the browser, the arrows or a
    // project), the bottom row turns to its page; BANK on its own doesn't load one.
    const auto* current = audioProcessor.findCurrentRigEntry();
    const auto file = current != nullptr ? current->file : juce::File();
    const auto name = current != nullptr ? current->rig.name : juce::String();
    const auto pages = rigPages();
    if (file == followedRig && name == followedRigName && ! pages.empty())
    {
        showRigPage (rigPage);   // the library may have changed (a rig saved, renamed)
        return;
    }
    followedRig = file;
    followedRigName = name;
    for (size_t index = 0; index < pages.size(); ++index)
        for (const auto* entry : pages[index])
            if (entry == current)
            {
                showRigPage ((int) index);
                return;
            }
    showRigPage (rigPage);
}

void MainView::saveRig()
{
    // Factory rigs and never-saved sounds need a name first.
    if (! audioProcessor.saveCurrentRig())
        rigBrowser.promptSaveAs();
    refreshRig();
}

void MainView::applyFinish()
{
    for (auto* label : { &rigLevelLabel, &presenceLabel })
        label->setColour (juce::Label::textColourId, Finish::ink());
    // The cluster's names: red over black keys; over neon keys red was
    // hard to read, so they're printed in the unit's ink (white on pearl
    // black, dark on pearl white). HOLD TO TUNER stays red.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel })
        label->setColour (juce::Label::textColourId, Finish::neonKeys() ? Finish::ink() : Finish::printedRed());
    pedal.refinish();
    repaint();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &Finish::changes())
    {
        applyFinish();
        return;
    }

    refreshRig();
}

void MainView::paint (juce::Graphics& g)
{
    // Background: near black, with a faint red warmth behind the unit.
    g.fillAll (ParagonColours::background);
    const auto centre = pedalBounds.getCentre().toFloat();
    g.setGradientFill (juce::ColourGradient (ParagonColours::glow.withAlpha (0.07f), centre,
                                             ParagonColours::glow.withAlpha (0.0f), centre.translated (760.0f, 0.0f), true));
    g.fillRect (getLocalBounds());

    // Header: emblem and name, as in the mockup's top bar.
    auto emblem = mockF (33, 13, 73, 50);
    juce::Path triangle;
    triangle.addTriangle (emblem.getTopLeft(), emblem.getTopRight(), { emblem.getCentreX(), emblem.getBottom() });
    g.setColour (ParagonColours::glow.withAlpha (0.3f));
    g.strokePath (triangle, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ParagonColours::glow);
    g.strokePath (triangle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    triangle.applyTransform (juce::AffineTransform::scale (0.5f, 0.5f, emblem.getCentreX(), emblem.getCentreY() - 3.0f));
    g.strokePath (triangle, juce::PathStrokeType (1.5f));

    const auto nameArea = mockF (84, 10, 330, 55);
    const auto nameFont = juce::FontOptions (21.0f);
    g.setFont (nameFont);
    g.setColour (ParagonColours::text);
    g.drawText ("PARAGON", nameArea, juce::Justification::centredLeft, false);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (nameFont, "PARAGON ");
    Logo::drawMark (g, nameArea.withTrimmedLeft (nameWidth).withSizeKeepingCentre (nameArea.getWidth() - nameWidth, 17.0f),
                   juce::Justification::centredLeft, { ParagonColours::glow, ParagonColours::glow, {}, {}, 1.0f, true });

    // Framed groups: the tabs, the rig selector and the tempo.
    for (const auto& group : { tabsGroup, rigGroup })
    {
        g.setColour (juce::Colour (0xff0d0a0e));
        g.fillRoundedRectangle (group, 5.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (group.reduced (0.5f), 5.0f, 1.0f);
    }
    for (auto x : { 505.0f, 650.0f })
        g.drawVerticalLine (juce::roundToInt (x * mockScale), tabsGroup.getY() + 8.0f, tabsGroup.getBottom() - 8.0f);

    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (64.0f * mockScale), 0.0f, (float) getWidth());
}

void MainView::resized()
{
    // Header
    processorTab.setBounds (mock (338, 12, 505, 53));
    presetsTab.setBounds (mock (507, 12, 650, 53));
    settingsTab.setBounds (mock (652, 12, 793, 53));
    previousRig.setBounds (mock (846, 14, 886, 51));
    nextRig.setBounds (mock (1087, 14, 1127, 51));
    rigNameButton.setBounds (mock (888, 14, 1085, 51));
    undoButton.setBounds (mock (1444, 14, 1482, 50));
    redoButton.setBounds (mock (1496, 14, 1534, 50));
    saveButton.setBounds (mock (1551, 14, 1589, 50));
    settingsButton.setBounds (mock (1611, 14, 1649, 50));

    // Side strips
    inputColumn.setBounds (mock (12, 72, 118, 556));
    outputColumn.setBounds (mock (1558, 72, 1662, 556));

    // The unit. Its own drawing is positioned relative to its bounds.
    pedal.setBounds (pedalBounds);
    const auto origin = pedalBounds.getPosition().toFloat();
    const auto local = [origin] (juce::Rectangle<float> r) { return r - origin; };

    PedalBody::Geometry geometry;
    // The face plate sits inside the rim with the same rim showing all round.
    geometry.face = local (mockF (faceLeft, 94, faceRight, 534));
    const auto rowRight = switchLeft + 7.0f * switchPitch;
    geometry.sections = { local (mockF (switchLeft - 2, switchRowY[0] - 8, rowRight + 2, switchRowY[0] + switchHeight + 10)),
                          local (mockF (switchLeft - 2, switchRowY[1] - 8, rowRight + 2, switchRowY[1] + switchHeight + 6)) };
    geometry.badge = local (mockF (196, 117, 394, 207));

    // The display centred along the top, as on the GSP21's controller, with
    // SAVE ... SAVE AS printed underneath it.
    constexpr float displayLeft = (faceLeft + faceRight) * 0.5f - 188.0f, displayRight = displayLeft + 376.0f;
    geometry.printedInk.startNewSubPath (mockPoint (displayLeft + 126, 218) - origin);
    geometry.printedInk.lineTo (mockPoint (displayLeft + 212, 218) - origin);
    pedal.setGeometry (std::move (geometry));

    display.setBounds (mock (displayLeft, 108, displayRight, 202));
    rigUp.setBounds (mock (displayRight + 33, 130, displayRight + 55, 150));
    rigDown.setBounds (mock (displayRight + 33, 168, displayRight + 55, 188));
    pedalSave.setBounds (mock (displayLeft + 60, 209, displayLeft + 126, 227));
    pedalSaveAs.setBounds (mock (displayLeft + 212, 209, displayLeft + 296, 227));

    // UNDO REDO BYPASS between the badge and the display: a red label printed above each key.
    const auto placeKey = [] (juce::Label& label, juce::Button& button, int column)
    {
        const auto x = clusterX[column];
        label.setBounds (mock (x - 30, clusterLabelY, x + 30, clusterLabelY + 14));
        button.setBounds (mock (x - 18, clusterButtonY, x + 18, clusterButtonY + 19));
    };
    placeKey (undoLabel, undoPedal, 0);
    placeKey (redoLabel, redoPedal, 1);
    placeKey (bypassLabel, bypassButton, 2);

    // RIG LEVEL and PRESENCE on the right.
    rigLevelKnob.setBounds (mock (1260, 98, 1374, 212));
    rigLevelLabel.setBounds (mock (1257, 196, 1377, 214));
    presenceKnob.setBounds (mock (1383, 98, 1497, 212));
    presenceLabel.setBounds (mock (1380, 196, 1500, 214));

    // The seven stomps along the top row; the five rigs and BANK down, up along the bottom.
    const auto placeSwitch = [] (Footswitch& footswitch, int row, int column)
    {
        // Each edge rounded on its own, so neighbours meet exactly.
        const auto edge = [] (float x) { return juce::roundToInt (x * mockScale); };
        const auto x = switchLeft + (float) column * switchPitch;
        footswitch.setBounds (juce::Rectangle<int>::leftTopRightBottom (edge (x), edge (switchRowY[row]),
                                                                        edge (x + switchPitch), edge (switchRowY[row] + switchHeight)));
    };
    for (size_t i = 0; i < footswitches.size(); ++i)
        placeSwitch (*footswitches[i], 0, (int) i);
    for (size_t i = 0; i < rigSwitches.size(); ++i)
        placeSwitch (*rigSwitches[i], 1, (int) i);
    placeSwitch (*bankDown, 1, 5);
    placeSwitch (*bankUp, 1, 6);

    // Dock: the middle panel takes the room the expression pedal's panel had.
    rigNavigator.setBounds (mock (12, 568, 246, 822));
    selectedRig.setBounds (mock (256, 568, 652, 822));
    const auto summaryArea = mock (662, 568, 1662, 822);
    eqPanel.setBounds (summaryArea);
    limiterPanel.setBounds (summaryArea);
    closePanel.setBounds (summaryArea.withSizeKeepingCentre (0, 0).withX (summaryArea.getRight() - 130).withY (summaryArea.getY() + 10)
                              .withSize (118, 24));

    signalChain.setBounds (mock (12, 832, 1662, 930));

    // Other pages cover the pedal and the dock.
    const auto pageArea = mock (126, 72, 1548, 822);   // between the INPUT and OUTPUT strips
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
    eqPage.setBounds (pageArea);
    dominionPage.setBounds (pageArea);
    oraclePage.setBounds (pageArea);
    compPage.setBounds (pageArea);
}
