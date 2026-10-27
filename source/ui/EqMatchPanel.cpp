#include "EqMatchPanel.h"

#include "DockPanels.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    using Recording = eq::match::Session::Recording;

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb3261e));   // recording
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    juce::String secondsText (double seconds)
    {
        return seconds < 60.0 ? juce::String (seconds, 1) + " s"
                              : juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2);
    }
}

EqMatchPanel::EqMatchPanel (ParagonX1AudioProcessor& p, EqDisplay& d)
    : processor (p), display (d), session (p.getEqMatch())
{
    for (auto* button : { &trackRecord, &trackClear, &referenceRecord, &loadFile, &saved, &add, &close })
    {
        styleButton (*button);
        addAndMakeVisible (button);
    }
    add.setColour (juce::TextButton::buttonColourId, ParagonColours::accentDim.withAlpha (0.7f));
    add.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);

    trackRecord.setTooltip ("Listen to your track: play the part you want to match (a few seconds at least; longer is better)");
    trackClear.setTooltip ("Forget what's been heard of your track and start again");
    referenceRecord.setTooltip ("Use another part of this track as the reference: move to it in your song and play it");
    loadFile.setTooltip ("Use a song as the reference: pick an audio file (WAV, AIFF, FLAC, Ogg, MP3)");
    saved.setTooltip ("A reference saved before, or save your track or this reference for another time");
    add.setTooltip ("Add the proposed bands to ECHELON, as ordinary bands you can change (undo takes them all back)");
    close.setTooltip ("Close MATCH without adding anything (what's been heard is kept)");

    trackRecord.onClick = [this]
    {
        session.setRecording (session.getRecording() == Recording::track ? Recording::none : Recording::track);
        updateButtons();
    };
    trackClear.onClick = [this] { session.clearTrack(); };
    referenceRecord.onClick = [this]
    {
        session.setRecording (session.getRecording() == Recording::reference ? Recording::none : Recording::reference);
        updateButtons();
    };
    loadFile.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a reference song", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& c)
                              {
                                  if (c.getResult().existsAsFile())
                                      session.loadFile (c.getResult());
                                  updateButtons();
                              });
    };
    saved.onClick = [this] { showSavedMenu(); };
    add.onClick = [this] { addBands(); };
    close.onClick = [this] { if (onClose) onClose(); };

    for (auto* knob : { &detail, &amount })
    {
        knob->setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        knob->setRange (0.0, 1.0, 0.01);
        knob->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        knob->valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() / 100.0; };
        addAndMakeVisible (knob);
    }
    detail.setValue (0.5, juce::dontSendNotification);
    detail.setDoubleClickReturnValue (true, 0.5);
    amount.setValue (1.0, juce::dontSendNotification);
    amount.setDoubleClickReturnValue (true, 1.0);
    detail.setTooltip ("Low: a few broad bands for the overall tone. High: more bands, matching finer differences");
    amount.setTooltip ("How much of the difference to make up: 100% all of it, 50% halfway");
    for (auto* label : { &detailLabel, &amountLabel })
    {
        label->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, ParagonColours::textDim);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }
    updateButtons();
}

EqMatchPanel::~EqMatchPanel()
{
    display.setMatchPreview ({}, {});
}

void EqMatchPanel::visibilityChanged()
{
    if (isVisible())
    {
        // Opening it starts listening to your track, as in Pro-Q, unless it's already heard enough.
        if (session.getRecording() == Recording::none && ! session.getTrack().isValid())
            session.setRecording (Recording::track);
        seenVersion = -1;
        startTimerHz (10);
        timerCallback();
    }
    else
    {
        stopTimer();
        session.setRecording (Recording::none);
        display.setMatchPreview ({}, {});
    }
    updateButtons();
}

int EqMatchPanel::freeSlots() const
{
    const auto settings = processor.getEqSettings();
    return (int) std::count_if (settings.bands.begin(), settings.bands.end(), [] (const eq::Band& b) { return ! b.used; });
}

void EqMatchPanel::timerCallback()
{
    if (! isShowing())
        return;
    recompute();
    updateButtons();
    repaint();
}

void EqMatchPanel::recompute()
{
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto settings = processor.getEqSettings();
    std::vector<double> curve;
    for (const auto hz : eq::match::grid())
        curve.push_back (eq::ParametricEq::curveDb (settings, hz, sampleRate, eq::Placement::stereo));

    // Fitted again when anything changes; while it's still listening, at most twice a second.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto changed = session.getVersion() != seenVersion || curve != existing
                      || ! juce::approximatelyEqual (detail.getValue(), seenDetail) || ! juce::approximatelyEqual (amount.getValue(), seenAmount);
    if (! changed || (session.getRecording() != Recording::none && now - lastFitMs < 500.0 && seenVersion >= 0))
        return;
    seenVersion = session.getVersion();
    seenDetail = detail.getValue();
    seenAmount = amount.getValue();
    existing = curve;
    lastFitMs = now;

    const auto track = session.getTrack(), reference = session.getReference();
    if (! track.isValid() || ! reference.isValid())
    {
        proposal.clear();
        display.setMatchPreview ({}, {});
        return;
    }
    const auto target = eq::match::target (track, reference, existing, detail.getValue());
    eq::match::Options options;
    options.detail = detail.getValue();
    options.amount = amount.getValue();
    options.maxBands = freeSlots();
    options.sampleRate = sampleRate;
    proposal = eq::match::fit (target, options);

    // On the display: where the EQ would be with all of the difference made up (scaled by AMOUNT), and with the proposed bands.
    std::vector<double> wanted (existing.size()), proposed = eq::match::curveOnGrid (proposal, sampleRate);
    for (size_t i = 0; i < existing.size(); ++i)
    {
        wanted[i] = existing[i] + target[i] * amount.getValue();
        proposed[i] += existing[i];
    }
    display.setMatchPreview (wanted, proposed);
}

void EqMatchPanel::updateButtons()
{
    trackRecord.setToggleState (session.getRecording() == Recording::track, juce::dontSendNotification);
    trackRecord.setButtonText (trackRecord.getToggleState() ? "RECORDING" : "RECORD");
    referenceRecord.setToggleState (session.getRecording() == Recording::reference, juce::dontSendNotification);
    referenceRecord.setButtonText (referenceRecord.getToggleState() ? "RECORDING" : "RECORD INPUT");
    loadFile.setEnabled (! session.isLoading());
    add.setEnabled (! proposal.empty());
    add.setButtonText (proposal.empty() ? juce::String ("ADD BANDS")
                                        : "ADD " + juce::String ((int) proposal.size()) + (proposal.size() == 1 ? " BAND" : " BANDS"));
}

void EqMatchPanel::addBands()
{
    if (proposal.empty())
        return;
    EqBands bands (processor);
    for (const auto& band : proposal)
    {
        const auto index = bands.create (band.shape, band.frequency, band.gainDb);
        if (index < 0)
            break;   // the slots ran out
        bands.set (index, "Q", (float) band.q);
        bands.set (index, "Slope", (float) band.slope);
    }
    // Bands you can't hear would be no use: the EQ comes on if it was off.
    if (auto* on = processor.getParameters().getParameter (ParamIDs::eqOn); on != nullptr && on->getValue() < 0.5f)
    {
        on->beginChangeGesture();
        on->setValueNotifyingHost (1.0f);
        on->endChangeGesture();
    }
    proposal.clear();
    session.setRecording (Recording::none);
    display.setMatchPreview ({}, {});
    if (onClose)
        onClose();
}

void EqMatchPanel::showSavedMenu()
{
    const auto files = session.getSavedReferences();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Saved references");
    for (int i = 0; i < files.size(); ++i)
        menu.addItem (100 + i, files[i].getFileNameWithoutExtension(), true, session.getReferenceName() == files[i].getFileNameWithoutExtension());
    if (files.isEmpty())
        menu.addItem (99, "(none yet)", false);
    menu.addSeparator();
    menu.addItem (1, "Save your track as a reference...", session.getTrack().isValid());
    menu.addItem (2, "Save this reference...", session.getReference().isValid());
    menu.addItem (3, "Show the references folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&saved), [this, files] (int chosen)
    {
        if (chosen >= 100 && chosen - 100 < files.size())
            session.useSaved (files[chosen - 100]);
        else if (chosen == 1)
            askForName ("Save your track as a reference", [this] (juce::String name) { session.save (session.getTrack(), name); });
        else if (chosen == 2)
            askForName ("Save this reference", [this] (juce::String name) { session.save (session.getReference(), name); });
        else if (chosen == 3)
        {
            session.getReferencesFolder().createDirectory();
            session.getReferencesFolder().revealToUser();
        }
        updateButtons();
    });
}

void EqMatchPanel::askForName (const juce::String& title, std::function<void (juce::String)> then)
{
    auto* window = new juce::AlertWindow (title, "A name for it (it's kept with your rigs):", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", session.getReferenceName().isNotEmpty() && session.getReferenceName() != "Input"
                                       ? session.getReferenceName() : juce::String ("My Reference"));
    window->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, then] (int result)
    {
        if (result == 1)
            then (window->getTextEditorContents ("name"));
    }), true);
}

void EqMatchPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    titleArea = area.removeFromLeft (150);
    area.removeFromLeft (10);

    trackArea = area.removeFromLeft (300);
    auto buttons = trackArea.withTrimmedTop (trackArea.getHeight() - 32);
    trackRecord.setBounds (buttons.removeFromLeft (120).reduced (0, 2));
    buttons.removeFromLeft (8);
    trackClear.setBounds (buttons.removeFromLeft (80).reduced (0, 2));
    area.removeFromLeft (16);

    referenceArea = area.removeFromLeft (400);
    buttons = referenceArea.withTrimmedTop (referenceArea.getHeight() - 32);
    referenceRecord.setBounds (buttons.removeFromLeft (128).reduced (0, 2));
    buttons.removeFromLeft (8);
    loadFile.setBounds (buttons.removeFromLeft (120).reduced (0, 2));
    buttons.removeFromLeft (8);
    saved.setBounds (buttons.removeFromLeft (100).reduced (0, 2));
    area.removeFromLeft (16);

    auto knobs = area.removeFromLeft (190);
    const auto place = [&knobs] (juce::Label& label, juce::Slider& knob)
    {
        auto column = knobs.removeFromLeft (92);
        label.setBounds (column.removeFromTop (16));
        knob.setBounds (column);
        knobs.removeFromLeft (6);
    };
    place (detailLabel, detail);
    place (amountLabel, amount);
    area.removeFromLeft (10);

    auto right = area;
    close.setBounds (right.removeFromBottom (32).removeFromRight (100).reduced (0, 2));
    right.removeFromBottom (6);
    add.setBounds (right.removeFromBottom (36).reduced (0, 2));
    statusArea = right;
}

void EqMatchPanel::paint (juce::Graphics& g)
{
    // The title, and what to do next.
    auto title = titleArea;
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("MATCH", title.removeFromTop (24), juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText ("Make your track's tone like a reference: listen to both, then add the bands it proposes.",
                      title.withTrimmedTop (4), juce::Justification::topLeft, 5);

    const auto section = [&] (juce::Rectangle<int> area, const juce::String& name, const juce::String& status, bool problem)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (name, area.removeFromTop (16), juce::Justification::centredLeft, false);
        g.setColour (problem ? juce::Colour (0xffff6b5e) : ParagonColours::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (status, area.withTrimmedBottom (34), juce::Justification::topLeft, 3);
    };

    const auto track = session.getTrack();
    juce::String trackStatus;
    if (session.getRecording() == Recording::track)
        trackStatus = "Listening: play the part to match. " + secondsText (track.seconds) + " heard";
    else if (track.seconds > 0.0)
        trackStatus = secondsText (track.seconds) + " heard";
    else
        trackStatus = "Nothing heard yet: press RECORD and play your track.";
    if (track.seconds > 0.0 && ! track.isValid())
        trackStatus << " (" << secondsText (eq::match::minSeconds) << " needed)";
    section (trackArea, "YOUR TRACK", trackStatus, false);

    const auto reference = session.getReference();
    juce::String referenceStatus;
    auto problem = false;
    if (session.isLoading())
        referenceStatus = "Reading the file...";
    else if (session.getRecording() == Recording::reference)
        referenceStatus = "Listening: play the reference part of your song. " + secondsText (reference.seconds) + " heard";
    else if (session.getProblem().isNotEmpty())
    {
        referenceStatus = session.getProblem();
        problem = true;
    }
    else if (session.getReferenceName().isEmpty())
        referenceStatus = "None yet: record another part of the song, load a song file, or pick a saved one.";
    else
        referenceStatus = session.getReferenceName() + (reference.seconds > 0.0 ? ", " + secondsText (reference.seconds) + " heard" : juce::String());
    section (referenceArea, "REFERENCE", referenceStatus, problem);

    // What it proposes.
    juce::String status;
    if (! track.isValid() || ! reference.isValid())
        status = "Waiting for both to be heard...";
    else if (proposal.empty())
        status = freeSlots() == 0 ? "All 24 bands are in use: delete some to make room." : "Already a close match: nothing to add.";
    else
        status = "Proposes " + juce::String ((int) proposal.size()) + (proposal.size() == 1 ? " band" : " bands") + " (dashed on the display).";
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 3);
}
