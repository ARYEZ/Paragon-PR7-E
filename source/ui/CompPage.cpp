#include "CompPage.h"

#include "DockPanels.h"
#include "DominionPage.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    namespace mb = effects::multiband;
    constexpr double lowestHz = 20.0, highestHz = 20000.0, rangeDb = 24.0;
    constexpr double displayRate = 48000.0;   // the curves' shape, whatever the session's rate

    double centreOf (const mb::Band& b) { return std::sqrt ((double) b.lowHz * b.highHz); }

    void styleKnob (juce::Slider& knob)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    }

    void styleLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setJustificationType (juce::Justification::centred);
    }

    void styleCombo (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
        box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box.setColour (juce::ComboBox::textColourId, ParagonColours::text);
        box.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    }

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }

    juce::String hzText (double hz)
    {
        return hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }
}

//==============================================================================
CompDisplay::CompDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("CONSTRICTOR bands");
    shown = processor.getMultibandSettings();
    startTimerHz (30);
}

void CompDisplay::refresh()
{
    shown = processor.getMultibandSettings();
    if (selected >= 0 && ! shown.bands[(size_t) selected].used)
        select (-1);
    repaint();
}

void CompDisplay::select (int band)
{
    if (band == selected)
        return;
    selected = band;
    if (onSelect)
        onSelect (band);
    repaint();
}

void CompDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f).withTrimmedTop (16.0f);
}

float CompDisplay::xForFrequency (double hz) const noexcept
{
    return plot.getX() + (float) (std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz)) * plot.getWidth();
}

double CompDisplay::frequencyForX (float x) const noexcept
{
    return juce::jlimit (lowestHz, highestHz, lowestHz * std::pow (highestHz / lowestHz, (double) ((x - plot.getX()) / plot.getWidth())));
}

float CompDisplay::yForDb (double db) const noexcept
{
    return plot.getCentreY() - (float) (juce::jlimit (-rangeDb, rangeDb, db) / rangeDb) * plot.getHeight() * 0.5f;
}

double CompDisplay::dbForY (float y) const noexcept
{
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.5f)) * rangeDb;
}

juce::Point<float> CompDisplay::dotFor (const mb::Band& b) const noexcept
{
    return { xForFrequency (centreOf (b)), yForDb (b.rangeDb) };
}

int CompDisplay::dotAt (juce::Point<float> position) const
{
    auto nearest = -1;
    auto best = 12.0f;
    for (int i = 0; i < mb::maxBands; ++i)
        if (const auto& b = shown.bands[(size_t) i]; b.used)
            if (const auto d = dotFor (b).getDistanceFrom (position); d < best)
            {
                best = d;
                nearest = i;
            }
    return nearest;
}

std::pair<int, CompDisplay::Grab> CompDisplay::edgeAt (juce::Point<float> position) const
{
    if (! plot.expanded (0.0f, 4.0f).contains (position))
        return { -1, Grab::none };
    // The selected band's edges first, then the others'.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < mb::maxBands; ++i)
        {
            const auto& b = shown.bands[(size_t) i];
            if (! b.used || (pass == 0) != (i == selected))
                continue;
            if (b.hasLowEdge() && std::abs (position.x - xForFrequency (b.lowHz)) < 6.0f)
                return { i, Grab::low };
            if (b.hasHighEdge() && std::abs (position.x - xForFrequency (b.highHz)) < 6.0f)
                return { i, Grab::high };
        }
    return { -1, Grab::none };
}

int CompDisplay::regionAt (juce::Point<float> position) const
{
    // The narrowest band under the mouse (so one inside another can be picked).
    auto found = -1;
    auto narrowest = 1.0e9;
    for (int i = 0; i < mb::maxBands; ++i)
        if (const auto& b = shown.bands[(size_t) i]; b.used)
        {
            const auto left = xForFrequency (b.lowHz), right = xForFrequency (b.highHz);
            if (position.x >= left && position.x <= right && (right - left) < narrowest)
            {
                narrowest = right - left;
                found = i;
            }
        }
    return found;
}

juce::RangedAudioParameter& CompDisplay::parameter (int band, const char* what) const
{
    return *processor.getParameters().getParameter (ParamIDs::mbBand (band, what));
}

void CompDisplay::setBand (int band, const char* what, float plainValue)
{
    auto& p = parameter (band, what);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    p.endChangeGesture();
}

int CompDisplay::createBand (double lowHz, double highHz)
{
    for (int band = 0; band < mb::maxBands; ++band)
    {
        if (parameter (band, "Used").getValue() > 0.5f)
            continue;
        // Everything set before "Used", so it comes in as it's meant: a
        // gentle downward compressor, waiting under its threshold.
        auto& state = processor.getParameters();
        for (const auto* what : ParamIDs::mbBandSettings)
            if (juce::String (what) != "Used")
                if (auto* p = state.getParameter (ParamIDs::mbBand (band, what)))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->getDefaultValue());
                    p->endChangeGesture();
                }
        setBand (band, "Low", (float) lowHz);
        setBand (band, "High", (float) highHz);
        setBand (band, "Used", 1.0f);
        shown = processor.getMultibandSettings();
        return band;
    }
    return -1;
}

void CompDisplay::removeBand (int band)
{
    setBand (band, "Used", 0.0f);
    shown = processor.getMultibandSettings();
    if (band == selected)
        select (-1);
}

void CompDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    // Grid
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const auto x = xForFrequency (hz);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ParagonColours::textDim);
        g.drawText (hz < 1000.0 ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0)) + "k",
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }
    for (auto db = -rangeDb; db <= rangeDb + 0.1; db += 6.0)
    {
        const auto y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (std::abs (db) < 0.1 ? 0.18f : 0.05f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText ((db > 0.0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 32.0f, y - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (2.0f).toNearestInt());
    const auto zero = yForDb (0.0);
    const auto step = 2.0f;
    std::vector<double> total ((size_t) (plot.getWidth() / step) + 2, 0.0);

    for (int i = 0; i < mb::maxBands; ++i)
    {
        const auto& b = shown.bands[(size_t) i];
        if (! b.used)
            continue;
        const auto colour = DominionDisplay::colourAt (centreOf (b));
        const auto alpha = b.on ? 1.0f : 0.35f;
        const auto chosen = i == selected;
        const auto reading = processor.getMultibandReading (i);

        // How far it can move (its RANGE, from its OUTPUT), faint; how far it's moving now, solid.
        const auto possible = mb::bandDesign (b, (double) b.outputDb + b.rangeDb, displayRate);
        const auto now = mb::bandDesign (b, (double) b.outputDb + (b.on ? reading.changeDb : 0.0f), displayRate);
        juce::Path reach, moving;
        reach.startNewSubPath (plot.getX(), zero);
        moving.startNewSubPath (plot.getX(), zero);
        size_t column = 0;
        for (auto x = plot.getX(); x <= plot.getRight(); x += step, ++column)
        {
            const auto hz = frequencyForX (x);
            reach.lineTo (x, yForDb (eq::responseDb (possible, hz, displayRate)));
            const auto db = eq::responseDb (now, hz, displayRate);
            moving.lineTo (x, yForDb (db));
            if (b.on && column < total.size())
                total[column] += db;
        }
        reach.lineTo (plot.getRight(), zero);
        moving.lineTo (plot.getRight(), zero);
        reach.closeSubPath();
        moving.closeSubPath();
        g.setColour (colour.withAlpha ((chosen ? 0.16f : 0.09f) * alpha));
        g.fillPath (reach);
        g.setColour (colour.withAlpha (0.65f * alpha));
        g.fillPath (moving);

        // Its edges, and its dot (at its RANGE).
        g.setColour (colour.withAlpha ((chosen ? 0.9f : 0.45f) * alpha));
        for (const auto* edge : { &b.lowHz, &b.highHz })
            if ((edge == &b.lowHz && b.hasLowEdge()) || (edge == &b.highHz && b.hasHighEdge()))
            {
                const auto x = xForFrequency (*edge);
                const auto hot = (i == hovered || i == dragging) && ((edge == &b.lowHz && (hoverGrab == Grab::low || grab == Grab::low))
                                                                     || (edge == &b.highHz && (hoverGrab == Grab::high || grab == Grab::high)));
                g.fillRect (juce::Rectangle<float> (hot ? 3.0f : 1.5f, plot.getHeight()).withCentre ({ x, plot.getCentreY() }));
            }
        const auto dot = dotFor (b);
        if (chosen)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (dot));
        }
        g.setColour (b.on ? colour : ParagonColours::textDim);
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot), juce::Justification::centred, false);

        // What it's doing, over it.
        if (b.on && std::abs (reading.changeDb) > 0.05f)
        {
            g.setColour (colour);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ((reading.changeDb > 0.0f ? "+" : "") + juce::String (reading.changeDb, 1) + " dB",
                        juce::Rectangle<float> (70.0f, 14.0f).withCentre ({ dot.x, plot.getY() + 9.0f }), juce::Justification::centred, false);
        }
    }

    // Everything the bands do together.
    {
        juce::Path line;
        size_t column = 0;
        for (auto x = plot.getX(); x <= plot.getRight(); x += step, ++column)
        {
            const auto y = yForDb (column < total.size() ? total[column] : 0.0);
            if (column == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (line, juce::PathStrokeType (1.6f));
    }

    if (std::none_of (shown.bands.begin(), shown.bands.end(), [] (const mb::Band& b) { return b.used; }))
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Double-click to add a band (up to 6). With no bands, CONSTRICTOR is the full-band compressor on the right, as before.",
                          plot.reduced (40.0f, 0.0f).withTrimmedTop (plot.getHeight() * 0.62f).toNearestInt(), juce::Justification::centredTop, 2);
    }
}

void CompDisplay::mouseMove (const juce::MouseEvent& event)
{
    const auto dot = dotAt (event.position);
    const auto [edgeBand, edge] = edgeAt (event.position);
    hovered = dot >= 0 ? dot : edgeBand;
    hoverGrab = dot >= 0 ? Grab::dot : edge;
    setMouseCursor (hoverGrab == Grab::low || hoverGrab == Grab::high ? juce::MouseCursor::LeftRightResizeCursor
                    : hoverGrab == Grab::dot                           ? juce::MouseCursor::DraggingHandCursor
                                                                       : juce::MouseCursor::NormalCursor);
    repaint();
}

void CompDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    hoverGrab = Grab::none;
    repaint();
}

void CompDisplay::beginDrag (int band, Grab what)
{
    const auto& b = shown.bands[(size_t) band];
    dragging = band;
    grab = what;
    startLow = b.lowHz;
    startHigh = b.highHz;
    startRange = b.rangeDb;
    for (const auto* id : { "Low", "High", "Range" })
        parameter (band, id).beginChangeGesture();
}

void CompDisplay::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto dot = dotAt (event.position);
    const auto [edgeBand, edge] = edgeAt (event.position);
    const auto band = dot >= 0 ? dot : edgeBand >= 0 ? edgeBand : regionAt (event.position);
    if (band < 0)
    {
        if (! event.mods.isPopupMenu())
            select (-1);
        return;
    }
    select (band);
    if (event.mods.isPopupMenu())
    {
        showMenu (band);
        return;
    }
    if (dot >= 0)
        beginDrag (band, Grab::dot);
    else if (edgeBand >= 0)
        beginDrag (band, edge);
}

void CompDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (dragging < 0)
        return;
    const auto fine = event.mods.isShiftDown() ? 0.1f : 1.0f;
    const auto dx = (float) event.getDistanceFromDragStartX() * fine, dy = (float) event.getDistanceFromDragStartY() * fine;
    const auto set = [this] (const char* what, float value)
    {
        auto& p = parameter (dragging, what);
        p.setValueNotifyingHost (p.convertTo0to1 (value));
    };
    const auto& b = shown.bands[(size_t) dragging];
    if (grab == Grab::dot)
    {
        // Sideways: the whole band (an edge at an end stays there); up and down: RANGE.
        const auto moved = frequencyForX (xForFrequency (std::sqrt (startLow * startHigh)) + dx) / std::sqrt (startLow * startHigh);
        if (b.hasLowEdge())
            set ("Low", (float) juce::jlimit (lowestHz * 1.01, highestHz * 0.98, startLow * moved));
        if (b.hasHighEdge())
            set ("High", (float) juce::jlimit (lowestHz * 1.02, highestHz * 0.99, startHigh * moved));
        set ("Range", (float) juce::jlimit (-(double) mb::maxRangeDb, (double) mb::maxRangeDb,
                                            startRange - (double) dy / (plot.getHeight() * 0.5) * rangeDb));
    }
    else if (grab == Grab::low)
        set ("Low", (float) juce::jlimit (lowestHz * 1.01, (double) b.highHz / 1.06, frequencyForX (xForFrequency (startLow) + dx)));
    else if (grab == Grab::high)
        set ("High", (float) juce::jlimit ((double) b.lowHz * 1.06, highestHz * 0.99, frequencyForX (xForFrequency (startHigh) + dx)));
    shown = processor.getMultibandSettings();
    repaint();
}

void CompDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    for (const auto* id : { "Low", "High", "Range" })
        parameter (dragging, id).endChangeGesture();
    dragging = -1;
    grab = Grab::none;
}

void CompDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (const auto band = dotAt (event.position); band >= 0)
    {
        setBand (band, "On", shown.bands[(size_t) band].on ? 0.0f : 1.0f);
        shown = processor.getMultibandSettings();
        return;
    }
    if (! plot.contains (event.position))
        return;
    // An octave wide round where it was clicked; near either end, reaching it.
    const auto hz = frequencyForX (event.position.x);
    const auto low = hz < 60.0 ? lowestHz : hz / std::sqrt (2.0);
    const auto high = hz > 8000.0 ? highestHz : hz * std::sqrt (2.0);
    if (const auto band = createBand (low, high); band >= 0)
        select (band);
}

void CompDisplay::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // THRESHOLD of the band under the mouse (or the selected one).
    auto band = regionAt (event.position);
    if (band < 0)
        band = selected;
    if (band < 0 || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    const auto step = (event.mods.isShiftDown() ? 0.25f : 1.0f) * (wheel.deltaY > 0.0f ? 1.0f : -1.0f);
    setBand (band, "Thresh", juce::jlimit (-60.0f, 0.0f, shown.bands[(size_t) band].thresholdDb + step));
    shown = processor.getMultibandSettings();
    repaint();
}

bool CompDisplay::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected >= 0)
    {
        removeBand (selected);
        return true;
    }
    return false;
}

void CompDisplay::showMenu (int band)
{
    const auto& b = shown.bands[(size_t) band];
    juce::PopupMenu menu, modes, stereo;
    for (int i = 0; i < mb::modeNames.size(); ++i)
        modes.addItem (100 + i, mb::modeNames[i], true, (int) b.mode == i);
    for (int i = 0; i < mb::stereoNames.size(); ++i)
        stereo.addItem (200 + i, mb::stereoNames[i], true, (int) b.stereo == i);
    menu.addSectionHeader ("Band " + juce::String (band + 1) + "  " + (b.hasLowEdge() ? hzText (b.lowHz) : juce::String ("bottom")) + " to "
                           + (b.hasHighEdge() ? hzText (b.highHz) : juce::String ("top")));
    menu.addSubMenu ("Mode", modes);
    menu.addSubMenu ("Stereo", stereo);
    menu.addItem (3, b.rangeDb < 0.0f ? "Make it upward (bring up)" : "Make it downward (turn down)");
    menu.addSeparator();
    menu.addItem (1, b.on ? "Switch off" : "Switch on");
    menu.addItem (2, "Delete");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, band] (int chosen)
    {
        const auto& now = shown.bands[(size_t) band];
        if (chosen >= 200)
            setBand (band, "Stereo", (float) (chosen - 200));
        else if (chosen >= 100)
            setBand (band, "Mode", (float) (chosen - 100));
        else if (chosen == 3)
            setBand (band, "Range", -now.rangeDb);
        else if (chosen == 1)
            setBand (band, "On", now.on ? 0.0f : 1.0f);
        else if (chosen == 2)
            removeBand (band);
        shown = processor.getMultibandSettings();
    });
}

//==============================================================================
CompPage::Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : label ({}, text), attachment (state, id, slider)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

CompPage::BandKnob::BandKnob (const juce::String& text, const char* id, const juce::String& tooltip) : what (id), label ({}, text)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
}

CompPage::BandChoice::BandChoice (const juce::String& text, const char* id, const juce::StringArray& items, const juce::String& tooltip)
    : what (id), label ({}, text)
{
    styleCombo (box);
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
    styleLabel (label);
}

namespace
{
    juce::ComboBox& filled (juce::ComboBox& box, const juce::StringArray& items)
    {
        styleCombo (box);
        box.addItemList (items, 1);
        return box;
    }

    juce::StringArray slopeItems()
    {
        juce::StringArray items;
        for (int i = 0; i < mb::numSlopes; ++i)
            items.add (eq::slopeNames[i]);
        return items;
    }
}

CompPage::CompPage (ParagonX1AudioProcessor& p)
    : processor (p),
      display (p),
      onAttachment (p.getParameters(), ParamIDs::compOn, onButton),
      lookaheadAttachment (p.getParameters(), ParamIDs::mbLookahead, lookahead),
      wideAttachment (p.getParameters(), ParamIDs::compWide, wide),
      autoAttachment (p.getParameters(), ParamIDs::compAuto, autoRelease),
      linkAttachment (p.getParameters(), ParamIDs::compLink, link),
      mix (p.getParameters(), ParamIDs::mbMix, "MIX", "How much of the bands' effect: 0% none, 100% as set, 200% twice as much"),
      threshold (p.getParameters(), ParamIDs::compThreshold, "THRESHOLD", "The full-band compressor's threshold"),
      ratio (p.getParameters(), ParamIDs::compRatio, "RATIO", "From SOFT (a curve that firms up with level) to infinite"),
      attack (p.getParameters(), ParamIDs::compAttack, "ATTACK", "How quickly it acts: slower lets more of each hit through"),
      release (p.getParameters(), ParamIDs::compRelease, "RELEASE", "How quickly it lets go"),
      makeUp (p.getParameters(), ParamIDs::compMakeUp, "MAKE UP", "Level back up after compressing"),
      characterAttachment (p.getParameters(), ParamIDs::compCharacter, filled (character, ParamRanges::compCharacters)),
      preset (PresetButton::Hooks {
          "CONSTRICTOR",
          [] () -> const std::vector<StompPreset>& { return CompPresets::factory(); },
          [&p] { return p.getCompPresets().user(); },
          [&p] { return p.getCompPresetName(); },
          [&p] { return p.isCompPresetModified(); },
          [&p] (const StompPreset& chosen) { p.loadCompPreset (chosen); },
          [&p] (const juce::String& name)
          {
              const auto saved = CompPresets::capture (p.getParameters(), name);
              if (! p.getCompPresets().save (saved))
                  return false;
              p.rememberCompPreset (saved);
              return true;
          },
          [&p] (const StompPreset& gone)
          {
              if (! p.getCompPresets().remove (gone))
                  return false;
              p.rememberCompPreset (CompPresets::capture (p.getParameters(), "Untitled"));
              return true;
          },
          [&p] { return p.getCompPresets().getFolder(); } }),
      mode ("MODE", "Mode", mb::modeNames, "COMPRESS: evens out (with RANGE below 0, turns loud parts down; above 0, brings quiet parts up). "
                                           "EXPAND: widens (below 0, turns quiet parts down, a gate; above 0, brings loud parts up, more punch)"),
      stereo ("STEREO", "Stereo", mb::stereoNames, "Both channels (LINK: how much they share one reading), or the middle or the sides only"),
      bandCharacter ("CHARACTER", "Char", ParamRanges::compCharacters, "VCA follows loudness, precise; OPTO slower and smoother; FET catches every peak"),
      slope ("SLOPE", "Slope", slopeItems(), "How steep the band's edges are"),
      bandThreshold ("THRESHOLD", "Thresh", "Where it starts working (the level in this band)"),
      range ("RANGE", "Range", "The most it moves the level: below 0 turns down, above 0 brings up"),
      bandRatio ("RATIO", "Ratio", "How strongly it works past the threshold"),
      knee ("KNEE", "Knee", "How gradually it starts round the threshold"),
      bandAttack ("ATTACK", "Attack", "How quickly it acts"),
      bandRelease ("RELEASE", "Release", "How quickly it lets go"),
      bandLink ("LINK", "Link", "Both channels: 100% share one reading (the louder), 0% each its own"),
      out ("OUTPUT", "Out", "The band's own level, before and after it works"),
      look ("LOOK-AHEAD", "Look", "How far ahead the band hears, with LOOK-AHEAD on (top): ready before a peak arrives")
{
    addAndMakeVisible (display);
    display.onSelect = [this] (int selected) { showBand (selected); };

    onButton.setTooltip ("Switch CONSTRICTOR on or off (the same as stomp 2)");
    lookahead.setTooltip ("The bands hear up to 20 ms ahead (each band's LOOK-AHEAD), so they're ready before a peak. "
                          "CONSTRICTOR is then 20 ms late, which your DAW makes up for on playback (not when monitoring live)");
    wide.setTooltip ("The full-band compressor, after the bands (on by default, as CONSTRICTOR was before it had bands)");
    autoRelease.setTooltip ("A release that follows the music: quick after a short peak, slower after long compression");
    link.setTooltip ("Both channels compressed together, from the louder one");
    character.setTooltip ("VCA clean and precise, OPTO smooth, FET fast with a little grit");
    styleButton (back);
    back.onClick = [this] { if (onClose) onClose(); };

    styleLabel (presetLabel);
    for (auto* component : std::initializer_list<juce::Component*> { &onButton, &lookahead, &wide, &autoRelease, &link, &character, &back,
                                                                    &preset, &presetLabel })
        addAndMakeVisible (component);
    for (auto* knob : { &mix, &threshold, &ratio, &attack, &release, &makeUp })
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }

    for (auto* button : { &listen, &previous, &next, &remove })
        styleButton (*button);
    listen.setTooltip ("Hold to hear only this band's part of the sound, as CONSTRICTOR leaves it");
    listen.onStateChange = [this] { processor.setMultibandSolo (listen.isDown() && band >= 0 ? band : -1); };
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    remove.onClick = [this] { if (band >= 0) display.removeBand (band); };
    for (auto* component : bandComponents())
        addChildComponent (component);

    showBand (-1);
    startTimerHz (15);
}

CompPage::~CompPage()
{
    processor.setMultibandSolo (-1);
}

std::vector<juce::Component*> CompPage::bandComponents()
{
    std::vector<juce::Component*> list { &bandOn, &listen, &previous, &next, &remove };
    for (auto* choice : { &mode, &stereo, &bandCharacter, &slope })
    {
        list.push_back (&choice->box);
        list.push_back (&choice->label);
    }
    for (auto* knob : { &bandThreshold, &range, &bandRatio, &knee, &bandAttack, &bandRelease, &bandLink, &out, &look })
    {
        list.push_back (&knob->slider);
        list.push_back (&knob->label);
    }
    return list;
}

void CompPage::showBand (int newBand)
{
    band = newBand;
    bandOnAttachment.reset();
    for (auto* choice : { &mode, &stereo, &bandCharacter, &slope })
        choice->attachment.reset();
    for (auto* knob : { &bandThreshold, &range, &bandRatio, &knee, &bandAttack, &bandRelease, &bandLink, &out, &look })
        knob->attachment.reset();
    processor.setMultibandSolo (-1);
    for (auto* component : bandComponents())
        component->setVisible (band >= 0);
    if (band >= 0)
    {
        auto& state = processor.getParameters();
        const auto parameter = [&] (const char* what) -> juce::RangedAudioParameter& { return *state.getParameter (ParamIDs::mbBand (band, what)); };
        bandOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter ("On"), bandOn);
        for (auto* choice : { &mode, &stereo, &bandCharacter, &slope })
            choice->attachment = std::make_unique<juce::ComboBoxParameterAttachment> (parameter (choice->what), choice->box);
        for (auto* knob : { &bandThreshold, &range, &bandRatio, &knee, &bandAttack, &bandRelease, &bandLink, &out, &look })
        {
            auto& p = parameter (knob->what);
            knob->attachment = std::make_unique<juce::SliderParameterAttachment> (p, knob->slider);
            knob->slider.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
        }
        timerCallback();
    }
    repaint();
}

void CompPage::step (int direction)
{
    const auto settings = processor.getMultibandSettings();
    std::vector<int> order;
    for (int i = 0; i < mb::maxBands; ++i)
        if (settings.bands[(size_t) i].used)
            order.push_back (i);
    if (order.empty())
        return;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return centreOf (settings.bands[(size_t) a]) < centreOf (settings.bands[(size_t) b]); });
    const auto at = std::find (order.begin(), order.end(), band);
    const auto index = at == order.end() ? 0 : (int) std::distance (order.begin(), at) + direction;
    display.select (order[(size_t) ((index + (int) order.size()) % (int) order.size())]);
}

void CompPage::timerCallback()
{
    preset.refresh();   // its name, with * once COMP's settings have been changed

    // LINK matters only on both channels; LOOK-AHEAD only while it's on.
    if (band >= 0)
    {
        const auto b = processor.getMultibandSettings().bands[(size_t) band];
        bandLink.slider.setEnabled (b.stereo == mb::Stereo::stereo);
        look.slider.setEnabled (processor.getParameters().getRawParameterValue (ParamIDs::mbLookahead)->load() > 0.5f);
        repaint (controlsArea.withWidth (150));
    }
    repaint (meterArea);
}

void CompPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (92);
    area.removeFromTop (8);
    sidePanel = area.removeFromRight (290);
    area.removeFromRight (8);
    controlsArea = area.removeFromBottom (112);
    area.removeFromBottom (8);
    display.setBounds (area);

    const auto place = [] (juce::Label& label, juce::Slider& slider, juce::Rectangle<int> column)
    {
        label.setBounds (column.removeFromTop (14));
        slider.setBounds (column);
    };

    // Top bar: the name, ON, LOOK-AHEAD, MIX; BACK on the right.
    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (190);   // the name, drawn
    onButton.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    bar.removeFromLeft (16);
    lookahead.setBounds (bar.removeFromLeft (130).withSizeKeepingCentre (130, 26));
    bar.removeFromLeft (16);
    place (mix.label, mix.slider, bar.removeFromLeft (92));
    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));
    bar.removeFromRight (24);
    auto presetColumn = bar.removeFromRight (220);
    presetLabel.setBounds (presetColumn.removeFromTop (14));
    preset.setBounds (presetColumn.withSizeKeepingCentre (220, 30));

    // The side panel: the full-band compressor.
    auto side = sidePanel.reduced (12, 10);
    side.removeFromTop (22);   // its title, drawn
    auto row = side.removeFromTop (32);
    wide.setBounds (row.removeFromLeft (120).withSizeKeepingCentre (120, 26));
    auto characterColumn = row.withTrimmedLeft (8);
    character.setBounds (characterColumn.withSizeKeepingCentre (characterColumn.getWidth(), 26));
    side.removeFromTop (6);
    const auto column = side.getWidth() / 3;
    auto knobs1 = side.removeFromTop (88);
    for (auto* knob : { &threshold, &ratio, &makeUp })
        place (knob->label, knob->slider, knobs1.removeFromLeft (column));
    side.removeFromTop (6);
    auto knobs2 = side.removeFromTop (88);
    place (attack.label, attack.slider, knobs2.removeFromLeft (column));
    place (release.label, release.slider, knobs2.removeFromLeft (column));
    auto toggles = knobs2;
    autoRelease.setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (80, 26));
    link.setBounds (toggles.withSizeKeepingCentre (80, 26));
    side.removeFromTop (10);
    meterArea = side.removeFromTop (44);

    // The selected band.
    auto controls = controlsArea.reduced (12, 8);
    controls.removeFromLeft (120);   // its name, drawn
    auto arrows = controls.removeFromLeft (64);
    previous.setBounds (arrows.removeFromLeft (30).withSizeKeepingCentre (30, 26));
    next.setBounds (arrows.removeFromRight (30).withSizeKeepingCentre (30, 26));
    controls.removeFromLeft (8);
    bandOn.setBounds (controls.removeFromLeft (52).withSizeKeepingCentre (52, 26));
    controls.removeFromLeft (6);
    // MODE over STEREO, CHARACTER over SLOPE.
    const std::array<std::array<BandChoice*, 2>, 2> pairs { { { &mode, &stereo }, { &bandCharacter, &slope } } };
    for (const auto& pair : pairs)
    {
        auto choices = controls.removeFromLeft (108);
        const auto half = choices.getHeight() / 2;
        for (auto* choice : pair)
        {
            auto cell = choices.removeFromTop (half);
            choice->label.setBounds (cell.removeFromTop (13));
            choice->box.setBounds (cell.withSizeKeepingCentre (100, 24));
        }
        controls.removeFromLeft (4);
    }
    auto buttons = controls.removeFromRight (78);
    listen.setBounds (buttons.removeFromTop (buttons.getHeight() / 2).withSizeKeepingCentre (76, 28));
    remove.setBounds (buttons.withSizeKeepingCentre (76, 28));
    controls.removeFromRight (4);
    const auto width = controls.getWidth() / 9;
    for (auto* knob : { &bandThreshold, &range, &bandRatio, &knee, &bandAttack, &bandRelease, &bandLink, &out, &look })
        place (knob->label, knob->slider, controls.removeFromLeft (width));
}

void CompPage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, sidePanel, controlsArea })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient, across its letters.
    auto title = topBar.reduced (14, 8).removeFromLeft (190);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "CONSTRICTOR");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("CONSTRICTOR", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Compressor and bands", title, juce::Justification::topLeft, false);

    // The side panel's title, and the full band's gain reduction.
    auto side = sidePanel.reduced (12, 10);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("FULL BAND", side.withHeight (18), juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (side.getY() + 19, (float) side.getX(), (float) side.getRight());
    {
        auto meter = meterArea;
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("GAIN REDUCTION", meter.removeFromTop (14), juce::Justification::centredLeft, false);
        const auto reduction = juce::jmax (processor.getCompressorReductionDb (0), processor.getCompressorReductionDb (1));
        const auto bar = meter.removeFromTop (12).toFloat();
        g.setColour (juce::Colour (0xff09070a));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (bar));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, reduction / 20.0f)), 3.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (-reduction, 1) + " dB", meter, juce::Justification::centredLeft, false);
    }

    auto controls = controlsArea.reduced (12, 8);
    auto name = controls.removeFromLeft (120);
    if (band < 0)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Click a band's dot to change it, or double-click the graph to add one (6 at most).", controlsArea,
                    juce::Justification::centred, false);
        return;
    }
    const auto b = processor.getMultibandSettings().bands[(size_t) band];
    auto top = name.removeFromTop (name.getHeight() / 2);
    g.setColour (DominionDisplay::colourAt (centreOf (b)));
    g.fillEllipse (top.removeFromLeft (20).toFloat().withSizeKeepingCentre (13.0f, 13.0f));
    top.removeFromLeft (6);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (band + 1), top, juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ((b.hasLowEdge() ? hzText (b.lowHz) : juce::String ("bottom")) + " to " + (b.hasHighEdge() ? hzText (b.highHz) : juce::String ("top"))
                          + "\nhears " + juce::String (processor.getMultibandReading (band).levelDb, 1) + " dB",
                      name, juce::Justification::centredLeft, 2);
}
