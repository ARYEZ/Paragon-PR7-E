#include "MainView.h"

#include "Parameters.h"
#include "ui/RackSpecs.h"
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
    // four rows down to the signal chain: a rack unit each since 0.18.0
    // (footswitches before: seven stomps, then rigs; two rows and a dock
    // before 0.17.0).
    const auto pedalBounds = mock (126, 68, 1552, 822);
    constexpr float faceLeft = 164, faceRight = 1514, faceBottom = 792;

    // The small keys (UNDO REDO BYPASS ORACLE): column centres, label and key tops.
    constexpr float clusterX[] = { 426, 488, 550, 612 };
    constexpr float clusterLabelY = 132, clusterButtonY = 149;

    // The four rows: each a raised panel holding a rack unit, as wide as
    // seven footswitches were (`switchPitch` each, before 0.18.0).
    constexpr float switchLeft = 176, switchPitch = 190;
    constexpr float switchRowY[] = { 244, 380, 516, 652 }, switchHeight = 118;
    juce::Rectangle<float> rowPanel (int row)
    {
        return mockF (switchLeft - 2, switchRowY[row] - 8, switchLeft + 7.0f * switchPitch + 2, switchRowY[row] + switchHeight + 6);
    }

    // Since 0.22.0, six rack units share the four rows' height (the owner's
    // Option A): each a little shorter, in the chain's order.
    constexpr int numRackRows = 6;
    juce::Rectangle<float> rackRow (int row)
    {
        const auto top = rowPanel (0).getY(), bottom = rowPanel (3).getBottom();
        const auto gap = 4.0f * mockScale;
        const auto h = (bottom - top - gap * (float) (numRackRows - 1)) / (float) numRackRows;
        return rowPanel (0).withY (top + (h + gap) * (float) row).withHeight (h);
    }

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
      // The blocks that shape the sound, in order; it grows as the stomps are built.
      // Each block in the red to pink to orange of the menus and DOMINION's
      // and ORACLE's pages, by its place in the chain: red first, orange last.
      signalChain (processorToShow.getParameters(), {
          { "ECHELON", Icons::Type::eq, chainColour (0, 6), ParamIDs::eqOn },
          { "DOMINION", Icons::Type::filterPitch, chainColour (1, 6), ParamIDs::domOn },
          { "CONSTRICTOR", Icons::Type::comp, chainColour (2, 6), ParamIDs::compOn },
          { "MARS", Icons::Type::drive, chainColour (3, 6), ParamIDs::marsOn },
          { "BINARY", Icons::Type::modulation, chainColour (4, 6), ParamIDs::stereoOn },
          { "GUILLOTINE", Icons::Type::comp, chainColour (5, 6), ParamIDs::limitOn } }),
      rigBrowser (processorToShow),
      settingsPage (processorToShow),
      eqPage (processorToShow),
      dominionPage (processorToShow),
      oraclePage (processorToShow),
      compPage (processorToShow),
      limiterPage (processorToShow),
      marsPage (processorToShow),
      stereoPage (processorToShow)
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

    for (auto* button : { &pedalSave, &pedalSaveAs, &bypassButton, &undoPedal, &redoPedal, &oraclePedal })
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    pedalSave.onClick = [this] { saveRig(); };
    pedalSaveAs.onClick = [this] { rigBrowser.promptSaveAs(); };
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the whole plugin (the same as your DAW's bypass button)");

    undoPedal.setTooltip ("Undo the last change (Ctrl+Z)");
    redoPedal.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoPedal.onClick = [this] { audioProcessor.undo(); };
    redoPedal.onClick = [this] { audioProcessor.redo(); };
    // ORACLE (melody to MIDI, with the tuner) was stomp 1 until 0.18.0; its key
    // opens its page (with its own on switch) and is lit while it listens.
    oraclePedal.setComponentID ("oracleKey");
    oraclePedal.setTooltip ("ORACLE: melody to MIDI, and the tuner on the screen. Opens its page; lit while it's listening.");
    oraclePedal.onClick = [this] { showPage (Page::oracle); };

    // The cluster's labels are printed in red, as in the mockup.
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &oracleLabel })
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

    // The six rack units, one to a row, in the signal chain's order.
    RackActions actions;
    actions.openPage = [this] (int unit)
    {
        constexpr Page pages[] = { Page::eq, Page::dominion, Page::comp, Page::mars, Page::stereo, Page::limiter };
        showPage (pages[unit]);
    };
    actions.openMatch = [this]
    {
        showPage (Page::eq);
        eqPage.showMatch (true);
    };
    actions.analyserOn = [this] { return eqPage.isAnalyserOn(); };
    actions.setAnalyser = [this] (bool on) { eqPage.setAnalyser (on); };
    auto specs = makeRackSpecs (audioProcessor, std::move (actions));
    for (size_t i = 0; i < racks.size(); ++i)
        racks[i] = std::make_unique<RackUnit> (state, std::move (specs[i]));

    addAndMakeVisible (pedal);   // first, so everything on the pedal sits on top of it
    processorPage = { &pedal, &display, &rigUp, &rigDown, &pedalSave, &pedalSaveAs, &bypassButton,
                      &undoPedal, &redoPedal, &oraclePedal, &undoLabel, &redoLabel, &bypassLabel, &oracleLabel,
                      &rigLevelKnob, &presenceKnob, &rigLevelLabel, &presenceLabel };
    for (auto& rack : racks)
        processorPage.push_back (rack.get());
    for (auto* component : processorPage)
        addAndMakeVisible (component);

    eqPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (eqPage);
    dominionPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (dominionPage);
    oraclePage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (oraclePage);
    compPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (compPage);
    limiterPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (limiterPage);
    marsPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (marsPage);
    stereoPage.onClose = [this] { showPage (Page::processor); };
    addChildComponent (stereoPage);

    signalChain.onSelect = [this] (int block)
    {
        if (block == dominionBlock)
            showPage (Page::dominion);
        else if (block == compBlock)
            showPage (Page::comp);
        else if (block == limiterBlock)
            showPage (Page::limiter);
        else if (block == marsBlock)
            showPage (Page::mars);
        else if (block == stereoBlock)
            showPage (Page::stereo);
        else if (block == eqBlock)
            showPage (Page::eq);
        else
            selectBlock (block);
    };
    addAndMakeVisible (signalChain);
    addChildComponent (rigBrowser);
    addChildComponent (settingsPage);

    selectBlock (eqBlock);

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
    // The selected block, marked in the chain.
    selectedBlock = block;
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
    limiterPage.setVisible (newPage == Page::limiter);
    marsPage.setVisible (newPage == Page::mars);
    if (newPage == Page::mars)
        marsPage.getDisplay().grabKeyboardFocus();
    stereoPage.setVisible (newPage == Page::stereo);
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

    display.setContents ({ entry != nullptr ? entry->rig.number : 0, rig.name,
                           entry != nullptr ? Rig::bankName (entry->rig.bank) : juce::String(), modified, bypassed, tunerState });
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
    oraclePedal.setToggleState (rawValue (ParamIDs::tunerOn) > 0.5f, juce::dontSendNotification);

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
    for (auto* label : { &undoLabel, &redoLabel, &bypassLabel, &oracleLabel })
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
    inputColumn.setBounds (mock (12, 72, 118, 822));
    outputColumn.setBounds (mock (1558, 72, 1662, 822));

    // The unit. Its own drawing is positioned relative to its bounds.
    pedal.setBounds (pedalBounds);
    const auto origin = pedalBounds.getPosition().toFloat();
    const auto local = [origin] (juce::Rectangle<float> r) { return r - origin; };

    PedalBody::Geometry geometry;
    // The face plate sits inside the rim with the same rim showing all round.
    geometry.face = local (mockF (faceLeft, 94, faceRight, faceBottom));
    for (int row = 0; row < (int) racks.size(); ++row)   // a raised panel for each row, its rack unit on it
        geometry.sections.push_back (local (rackRow (row)));
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

    // UNDO REDO BYPASS ORACLE between the badge and the display: a red label printed above each key.
    const auto placeKey = [] (juce::Label& label, juce::Button& button, int column)
    {
        const auto x = clusterX[column];
        label.setBounds (mock (x - 28, clusterLabelY, x + 28, clusterLabelY + 14));
        button.setBounds (mock (x - 18, clusterButtonY, x + 18, clusterButtonY + 19));
    };
    placeKey (undoLabel, undoPedal, 0);
    placeKey (redoLabel, redoPedal, 1);
    placeKey (bypassLabel, bypassButton, 2);
    placeKey (oracleLabel, oraclePedal, 3);

    // RIG LEVEL and PRESENCE on the right.
    rigLevelKnob.setBounds (mock (1260, 98, 1374, 212));
    rigLevelLabel.setBounds (mock (1257, 196, 1377, 214));
    presenceKnob.setBounds (mock (1383, 98, 1497, 212));
    presenceLabel.setBounds (mock (1380, 196, 1500, 214));

    // The six rack units, each on its row's panel.
    for (size_t i = 0; i < racks.size(); ++i)
        racks[i]->setBounds (rackRow ((int) i).toNearestInt());

    signalChain.setBounds (mock (12, 832, 1662, 930));

    // Other pages cover the unit.
    const auto pageArea = mock (126, 72, 1548, 822);   // between the INPUT and OUTPUT strips
    rigBrowser.setBounds (pageArea);
    settingsPage.setBounds (pageArea);
    eqPage.setBounds (pageArea);
    dominionPage.setBounds (pageArea);
    oraclePage.setBounds (pageArea);
    compPage.setBounds (pageArea);
    limiterPage.setBounds (pageArea);
    marsPage.setBounds (pageArea);
    stereoPage.setBounds (pageArea);
}
