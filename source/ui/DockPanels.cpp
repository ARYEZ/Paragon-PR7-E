#include "DockPanels.h"

#include "Parameters.h"
#include "ui/Finish.h"
#include "ui/ParagonLookAndFeel.h"

namespace DockStyle
{
    void drawPanel (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, area.getY(),
                                                 juce::Colour (0xff09070a), 0.0f, area.getBottom(), false));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
    }

    void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (ParagonColours::text.withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (title, area, juce::Justification::centredLeft, false);
    }
}

namespace
{
    void styleButton (juce::TextButton& button, bool primary)
    {
        button.setColour (juce::TextButton::buttonColourId, primary ? ParagonColours::accentDim.withAlpha (0.6f) : juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, primary ? ParagonColours::accent : ParagonColours::text);
    }

    void styleCombo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
        box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box.setColour (juce::ComboBox::textColourId, ParagonColours::text);
        box.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    }
}

//==============================================================================
RigNavigator::RigNavigator()
{
    rows = {
        { "All Rigs", {}, Icons::Type::flame, 0, -1, {} },
        { "Favourites", {}, Icons::Type::star, 1, -1, {} },
        { "My Rigs", {}, Icons::Type::user, 2, -1, {} },
        { "Factory Rigs", {}, Icons::Type::factory, 3, -1, {} },
    };
    for (int bank = 0; bank < Rig::numBanks; ++bank)
        rows.push_back ({ "Bank " + Rig::bankName (bank), Rig::bankName (bank), Icons::Type::rig, -1, bank, {} });

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void RigNavigator::layoutRows()
{
    auto area = getLocalBounds().reduced (12, 10);
    area.removeFromTop (22);
    for (size_t i = 0; i < 4; ++i)
        rows[i].area = area.removeFromTop (23);

    // The banks: one row of letter keys (A B C D E), like a floor unit's bank
    // buttons, so adding a bank doesn't need another line.
    area.removeFromTop (6);
    banksTitle = area.removeFromTop (20);
    auto keys = area.removeFromTop (26);
    const auto numKeys = (int) rows.size() - 4;
    constexpr int keyGap = 6;
    const auto keyWidth = (keys.getWidth() - keyGap * (numKeys - 1)) / juce::jmax (1, numKeys);
    for (size_t i = 4; i < rows.size(); ++i)
    {
        rows[i].area = keys.removeFromLeft (keyWidth);
        keys.removeFromLeft (keyGap);
    }
}

int RigNavigator::rowAt (juce::Point<int> position) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].area.contains (position))
            return (int) i;
    return -1;
}

void RigNavigator::paint (juce::Graphics& g)
{
    layoutRows();
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    DockStyle::drawTitle (g, getLocalBounds().reduced (16, 10).removeFromTop (18), "RIG BROWSER");
    DockStyle::drawTitle (g, banksTitle.withTrimmedLeft (4), "BANKS");

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const auto area = row.area.toFloat();
        const auto isSelected = (int) i == selected;

        if (row.bank >= 0)
        {
            // A bank key: its letter in a small rounded key, lit when chosen.
            g.setColour (isSelected ? ParagonColours::accentDim.withAlpha (0.45f)
                                    : (int) i == hovered ? juce::Colours::white.withAlpha (0.08f) : juce::Colour (0xff151217));
            g.fillRoundedRectangle (area, 4.0f);
            g.setColour (isSelected ? ParagonColours::accent.withAlpha (0.7f) : ParagonColours::panelEdge);
            g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (isSelected ? ParagonColours::accent : ParagonColours::text.withAlpha (0.8f));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (row.letter, area, juce::Justification::centred, false);
            continue;
        }

        if (isSelected)
        {
            g.setColour (ParagonColours::accentDim.withAlpha (0.45f));
            g.fillRoundedRectangle (area, 4.0f);
            g.setColour (ParagonColours::accent.withAlpha (0.7f));
            g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
        }
        else if ((int) i == hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (area, 4.0f);
        }
        else
        {
            g.setColour (ParagonColours::panelEdge.withAlpha (0.6f));
            g.drawHorizontalLine (row.area.getBottom() - 1, area.getX() + 4.0f, area.getRight() - 4.0f);
        }

        auto content = area.reduced (8.0f, 0.0f);
        const auto colour = isSelected ? ParagonColours::accent : ParagonColours::text.withAlpha (0.8f);
        auto iconArea = content.removeFromLeft (16.0f);
        if (row.letter.isNotEmpty())
        {
            g.setColour (colour);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (row.letter, iconArea, juce::Justification::centred, false);
        }
        else
        {
            Icons::draw (g, row.icon, iconArea.withSizeKeepingCentre (13.0f, 13.0f), colour, 1.3f);
        }
        content.removeFromLeft (10.0f);
        g.setColour (colour);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (row.text, content, juce::Justification::centredLeft, false);
    }
}

void RigNavigator::mouseMove (const juce::MouseEvent& event)
{
    if (const auto row = rowAt (event.getPosition()); row != hovered)
    {
        hovered = row;
        repaint();
    }
}

void RigNavigator::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void RigNavigator::mouseUp (const juce::MouseEvent& event)
{
    const auto row = rowAt (event.getPosition());
    if (row < 0 || ! event.mouseWasClicked())
        return;

    selected = row;
    repaint();
    if (onShow)
        onShow (juce::jmax (0, rows[(size_t) row].filter), rows[(size_t) row].bank);
}

//==============================================================================
SelectedRigPanel::SelectedRigPanel (ParagonX1AudioProcessor& p) : processor (p)
{
    for (int bank = 0; bank < Rig::numBanks; ++bank)
        bankBox.addItem (Rig::bankName (bank) + "   Bank " + Rig::bankName (bank), bank + 1);
    for (int number = 1; number <= Rig::maxNumber; ++number)
        numberBox.addItem (juce::String (number).paddedLeft ('0', 3), number);

    for (auto* box : { &bankBox, &numberBox })
    {
        styleCombo (*box);
        box->onChange = [this] { refile(); };
        box->setTooltip ("Where this rig is filed. Change it to move one of your own rigs; factory rigs stay where they are.");
        addAndMakeVisible (*box);
    }

    styleButton (save, true);
    styleButton (saveAs, false);
    save.onClick = [this] { if (onSave) onSave(); };
    saveAs.onClick = [this] { if (onSaveAs) onSaveAs(); };
    addAndMakeVisible (save);
    addAndMakeVisible (saveAs);

    refresh();
}

void SelectedRigPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    const auto current = processor.getCurrentRig();
    const auto* entry = processor.findCurrentRigEntry();

    name = current.name + (processor.isRigModified() ? " *" : "");
    hasEntry = entry != nullptr;
    favourite = entry != nullptr && entry->rig.favourite;
    canRefile = entry != nullptr && ! entry->isFactory;

    bankBox.setSelectedId (entry != nullptr ? entry->rig.bank + 1 : 0, juce::dontSendNotification);
    numberBox.setSelectedId (entry != nullptr ? entry->rig.number : 0, juce::dontSendNotification);
    bankBox.setTextWhenNothingSelected ("Not saved");
    numberBox.setTextWhenNothingSelected ("---");
    bankBox.setEnabled (canRefile);
    numberBox.setEnabled (canRefile);
    repaint();
}

void SelectedRigPanel::refile()
{
    if (refreshing || ! canRefile)
        return;

    if (const auto* entry = processor.findCurrentRigEntry())
    {
        auto details = entry->rig;
        details.bank = bankBox.getSelectedId() - 1;
        details.number = numberBox.getSelectedId();
        processor.updateRigDetails (*entry, details);
    }
    refresh();
}

void SelectedRigPanel::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    DockStyle::drawTitle (g, getLocalBounds().reduced (16, 10).removeFromTop (18), "SELECTED RIG");

    // Name box with the favourite star, as in the mockup.
    g.setColour (juce::Colour (0xff09070a));
    g.fillRoundedRectangle (nameBox.toFloat(), 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (nameBox.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (21.0f));
    g.drawFittedText (name, nameBox.reduced (14, 0).withTrimmedRight (starArea.getWidth()), juce::Justification::centredLeft, 1, 0.8f);

    if (hasEntry)
    {
        auto star = Icons::get (Icons::Type::star);
        const auto area = starArea.toFloat().withSizeKeepingCentre (22.0f, 22.0f);
        star.applyTransform (juce::AffineTransform::scale (area.getWidth()).translated (area.getX(), area.getY()));
        g.setColour (ParagonColours::accent);
        if (favourite)
            g.fillPath (star);
        else
            g.strokePath (star, juce::PathStrokeType (1.4f));
    }

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("BANK", bankLabel, juce::Justification::centredLeft, false);
    g.drawText ("NUMBER", numberLabel, juce::Justification::centredLeft, false);
}

void SelectedRigPanel::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    area.removeFromTop (24);
    nameBox = area.removeFromTop (42);
    starArea = nameBox.withLeft (nameBox.getRight() - 44);
    area.removeFromTop (10);

    auto labels = area.removeFromTop (18);
    const auto half = (area.getWidth() - 16) / 2;
    bankLabel = labels.removeFromLeft (half);
    numberLabel = labels.withTrimmedLeft (16);

    auto combos = area.removeFromTop (30);
    bankBox.setBounds (combos.removeFromLeft (half));
    numberBox.setBounds (combos.withTrimmedLeft (16));

    area.removeFromTop (18);
    auto buttons = area.removeFromTop (32);
    save.setBounds (buttons.removeFromLeft (half));
    saveAs.setBounds (buttons.withTrimmedLeft (16));
}

void SelectedRigPanel::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasClicked() || ! starArea.contains (event.getPosition()))
        return;

    if (const auto* entry = processor.findCurrentRigEntry())
    {
        processor.getRigLibrary().setFavourite (*entry, ! entry->rig.favourite);
        refresh();
    }
}

//==============================================================================
SettingsPage::SettingsPage (ParagonX1AudioProcessor& p)
    : processor (p)
{
    reference.setRange (430.0, 450.0, 1.0);
    reference.setTextValueSuffix (" Hz");
    reference.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    reference.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    reference.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    reference.setColour (juce::Slider::thumbColourId, ParagonColours::accent);
    reference.setDoubleClickReturnValue (true, 440.0);
    reference.onValueChange = [this] { processor.setTunerReferenceHz (reference.getValue()); };
    reference.setTooltip ("The pitch the tuner calls A4. 440 Hz is standard; some recordings are tuned to 432 or 442. Double-click for 440.");

    styleButton (openFolder, false);
    openFolder.onClick = [this]
    {
        processor.getRigLibrary().getFolder().createDirectory();
        processor.getRigLibrary().getFolder().startAsProcess();
    };

    pearlWhite.setTooltip ("The original pearl white unit with black keys and knobs");
    pearlWhiteNeon.setTooltip ("The pearl white unit with neon pink-to-orange keys and knobs");
    pearlBlack.setTooltip ("A pearl black unit with neon pink-to-orange keys and knobs");
    pearlWhite.onClick = [] { Finish::set (Finish::Type::pearlWhite); };
    pearlWhiteNeon.onClick = [] { Finish::set (Finish::Type::pearlWhiteNeon); };
    pearlBlack.onClick = [] { Finish::set (Finish::Type::pearlBlack); };
    Finish::changes().addChangeListener (this);
    showFinish();

    for (auto* component : std::initializer_list<juce::Component*> { &reference, &openFolder, &pearlWhite, &pearlWhiteNeon, &pearlBlack })
        addAndMakeVisible (component);
}

SettingsPage::~SettingsPage()
{
    Finish::changes().removeChangeListener (this);
}

void SettingsPage::showFinish()
{
    // The chosen finish is lit like the dock's primary buttons.
    styleButton (pearlWhite, Finish::get() == Finish::Type::pearlWhite);
    styleButton (pearlWhiteNeon, Finish::get() == Finish::Type::pearlWhiteNeon);
    styleButton (pearlBlack, Finish::get() == Finish::Type::pearlBlack);
    repaint();
}

void SettingsPage::visibilityChanged()
{
    // Settings can change from a loaded project, so read them each time the page opens.
    reference.setValue (processor.getTunerReferenceHz(), juce::dontSendNotification);
}

void SettingsPage::paint (juce::Graphics& g)
{
    const auto section = [&g] (juce::Rectangle<int> area, const juce::String& title, const juce::String& body)
    {
        DockStyle::drawPanel (g, area.toFloat());
        auto inner = area.reduced (24, 18);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (title, inner.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (body, inner.removeFromTop (60), juce::Justification::topLeft, 4, 1.0f);
    };

    section (tunerArea, "TUNER AND ORACLE", "The ORACLE key, beside BYPASS, opens ORACLE's page: switch it on there to show the pitch of what's "
                                            "playing on the screen and transcribe it to MIDI as your DAW plays. Reference pitch for A4:");
    section (finishArea, "FINISH",
             "The unit's colour: pearl white with black keys, or pearl white or pearl black with neon pink-to-orange keys. "
             "Saved on this computer, so every Paragon PR7-X you open uses it.");
    section (rigsArea, "RIGS", "Your rigs are files in this folder, so you can back them up or share them:\n"
                               + processor.getRigLibrary().getFolder().getFullPathName());
    section (aboutArea, "ABOUT",
             juce::String ("Paragon PR7-X v") + JucePlugin_VersionString
                 + ". Free and open source under the GNU AGPLv3.\n"
                   "Built with JUCE (AGPLv3) and the Inter typeface (SIL OFL).");
}

void SettingsPage::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (area.getHeight() / 2 - 6);
    area.removeFromTop (12);
    tunerArea = top.removeFromLeft (top.getWidth() / 2 - 6);
    finishArea = top.withTrimmedLeft (12);
    rigsArea = area.removeFromLeft (area.getWidth() / 2 - 6);
    aboutArea = area.withTrimmedLeft (12);

    auto tunerControls = tunerArea.reduced (24, 18).withTrimmedTop (90);
    reference.setBounds (tunerControls.removeFromTop (32).withWidth (juce::jmin (420, tunerControls.getWidth())));

    openFolder.setBounds (rigsArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (32).withWidth (200));
    auto finishButtons = finishArea.reduced (24, 18).withTrimmedTop (100).removeFromTop (32);
    const auto buttonWidth = juce::jmin (150, (finishButtons.getWidth() - 24) / 3);
    pearlWhite.setBounds (finishButtons.removeFromLeft (buttonWidth));
    pearlWhiteNeon.setBounds (finishButtons.withTrimmedLeft (12).removeFromLeft (buttonWidth));
    pearlBlack.setBounds (finishButtons.withTrimmedLeft (24 + buttonWidth).removeFromLeft (buttonWidth));
}
