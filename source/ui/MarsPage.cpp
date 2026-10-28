#include "MarsPage.h"

#include "DockPanels.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    using Mars = effects::Mars;
    constexpr float minHz = 20.0f, maxHz = 20000.0f;

    void styleKnob (juce::Slider& knob)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
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

    // A band's colour: the menus' gradient at its middle frequency, as ECHELON's bands are coloured.
    juce::Colour colourAt (float hz)
    {
        const auto position = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
        return ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f }).getColourAtPosition ((double) position);
    }

    const char* crossoverIds[] = { ParamIDs::marsCross1, ParamIDs::marsCross2, ParamIDs::marsCross3 };
}

//==============================================================================
MarsDisplay::MarsDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTitle ("MARS bands");
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

float MarsDisplay::value (const juce::String& id) const
{
    return processor.getParameters().getRawParameterValue (id)->load();
}

void MarsDisplay::set (const juce::String& id, float plain)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
}

int MarsDisplay::numBands() const
{
    return juce::jlimit (1, Mars::maxBands, juce::roundToInt (value (ParamIDs::marsBands)) + 1);
}

std::array<float, Mars::maxBands + 1> MarsDisplay::edges() const
{
    std::array<float, Mars::maxBands + 1> e {};
    e[0] = minHz;
    const auto n = numBands();
    for (int i = 1; i < n; ++i)
        e[(size_t) i] = juce::jmax (e[(size_t) i - 1], value (crossoverIds[i - 1]));
    for (int i = n; i <= Mars::maxBands; ++i)
        e[(size_t) i] = maxHz;
    return e;
}

float MarsDisplay::frequencyAt (float x) const
{
    const auto position = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
    return minHz * std::pow (maxHz / minHz, position);
}

float MarsDisplay::xFor (float hz) const
{
    return plot.getX() + std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz) * plot.getWidth();
}

int MarsDisplay::bandAt (float x) const
{
    const auto hz = frequencyAt (x);
    const auto e = edges();
    for (int b = 0; b < numBands(); ++b)
        if (hz <= e[(size_t) b + 1])
            return b;
    return numBands() - 1;
}

int MarsDisplay::crossoverAt (float x) const
{
    const auto e = edges();
    for (int i = 1; i < numBands(); ++i)
        if (std::abs (xFor (e[(size_t) i]) - x) < 6.0f)
            return i - 1;
    return -1;
}

void MarsDisplay::setSelected (int band)
{
    selected = juce::jlimit (0, Mars::maxBands - 1, band);
    repaint();
}

void MarsDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (10.0f, 10.0f).withTrimmedBottom (16.0f);
}

void MarsDisplay::timerCallback()
{
    // The drive readings glide, so the glow breathes rather than flickers.
    for (int b = 0; b < Mars::maxBands; ++b)
    {
        const auto now = processor.getMarsDrive (b);
        auto& shown = shownDrive[(size_t) b];
        shown += (now - shown) * (now > shown ? 0.5f : 0.12f);
    }
    if (selected >= numBands())
    {
        setSelected (numBands() - 1);
        if (onSelect)
            onSelect (selected);
    }
    repaint();
}

void MarsDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    // The frequency grid.
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const auto x = xFor (hz);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ParagonColours::textDim);
        g.drawText (hz < 1000.0f ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0f)) + "k",
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    const auto on = value (ParamIDs::marsOn) > 0.5f;
    const auto e = edges();
    for (int b = 0; b < numBands(); ++b)
    {
        const auto band = juce::Rectangle<float> (xFor (e[(size_t) b]), plot.getY(), xFor (e[(size_t) b + 1]) - xFor (e[(size_t) b]), plot.getHeight());
        const auto bandOn = value (ParamIDs::marsBand (b, "On")) > 0.5f;
        const auto colour = colourAt (std::sqrt (e[(size_t) b] * e[(size_t) b + 1]));
        const auto drive = value (ParamIDs::marsBand (b, "Drive"));

        // The band's DRIVE as a level from the bottom, and its glow as it works.
        const auto lit = on && bandOn;
        g.setColour (colour.withAlpha (lit ? 0.18f : 0.06f));
        g.fillRect (band.withTop (band.getBottom() - band.getHeight() * drive).reduced (1.0f, 0.0f));
        if (lit)
        {
            const auto glow = juce::jlimit (0.0f, 1.0f, shownDrive[(size_t) b]);
            const auto centre = juce::Point<float> (band.getCentreX(), band.getBottom() - band.getHeight() * drive);
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f * glow), centre, colour.withAlpha (0.0f),
                                                     centre.translated (juce::jmax (band.getWidth(), band.getHeight()) * 0.5f, 0.0f), true));
            g.fillRect (band.reduced (1.0f, 0.0f));
        }
        g.setColour (lit ? colour : colour.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (band.getX() + 1.0f, band.getBottom() - band.getHeight() * drive - 1.5f, band.getWidth() - 2.0f, 3.0f));

        if (b == selected)
        {
            g.setColour (colour);
            g.drawRect (band.reduced (1.0f), 2.0f);
        }

        // Its name, style and drive.
        auto text = band.reduced (8.0f, 8.0f);
        g.setColour (lit ? ParagonColours::text : ParagonColours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText ("BAND " + juce::String (b + 1), text.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (lit ? colour : colour.withAlpha (0.4f));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        const auto styleName = bandOn ? Mars::styleNames[juce::jlimit (0, Mars::numStyles - 1, juce::roundToInt (value (ParamIDs::marsBand (b, "Style"))))].toUpperCase()
                                      : juce::String ("CLEAN (OFF)");
        g.drawFittedText (styleName, text.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("DRIVE " + juce::String (juce::roundToInt (drive * 100.0f)) + "%", text.removeFromTop (16.0f), juce::Justification::centredLeft, false);
    }

    // The crossovers.
    for (int i = 1; i < numBands(); ++i)
    {
        const auto x = xFor (e[(size_t) i]);
        g.setColour (juce::Colours::white.withAlpha (i - 1 == draggingCrossover ? 0.95f : 0.6f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.6f);
        const auto hz = e[(size_t) i];
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz",
                    juce::Rectangle<float> (x - 34.0f, plot.getBottom() - 18.0f, 68.0f, 14.0f), juce::Justification::centred, false);
    }

    if (! on)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("MARS IS OFF", plot.withHeight (24.0f).withY (plot.getCentreY() - 12.0f), juce::Justification::centred, false);
    }
}

void MarsDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (crossoverAt ((float) e.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
}

void MarsDisplay::mouseDown (const juce::MouseEvent& e)
{
    draggingCrossover = crossoverAt ((float) e.x);
    if (draggingCrossover >= 0)
    {
        processor.getParameters().getParameter (crossoverIds[draggingCrossover])->beginChangeGesture();
        return;
    }
    setSelected (bandAt ((float) e.x));
    if (onSelect)
        onSelect (selected);
    draggingDriveOf = selected;
    dragStartDrive = value (ParamIDs::marsBand (selected, "Drive"));
    processor.getParameters().getParameter (ParamIDs::marsBand (selected, "Drive"))->beginChangeGesture();
}

void MarsDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingCrossover >= 0)
    {
        // Between its neighbours, a third of an octave clear of each.
        const auto edgesNow = edges();
        const auto low = draggingCrossover > 0 ? edgesNow[(size_t) draggingCrossover] * 1.26f : 30.0f;
        const auto high = draggingCrossover + 2 < numBands() ? edgesNow[(size_t) draggingCrossover + 2] / 1.26f : 16000.0f;
        set (crossoverIds[draggingCrossover], juce::jlimit (low, juce::jmax (low, high), frequencyAt ((float) e.x)));
    }
    else if (draggingDriveOf >= 0)
    {
        set (ParamIDs::marsBand (draggingDriveOf, "Drive"),
             juce::jlimit (0.0f, 1.0f, dragStartDrive - (float) e.getDistanceFromDragStartY() / juce::jmax (1.0f, plot.getHeight())));
    }
}

void MarsDisplay::mouseUp (const juce::MouseEvent&)
{
    if (draggingCrossover >= 0)
        processor.getParameters().getParameter (crossoverIds[draggingCrossover])->endChangeGesture();
    if (draggingDriveOf >= 0)
        processor.getParameters().getParameter (ParamIDs::marsBand (draggingDriveOf, "Drive"))->endChangeGesture();
    draggingCrossover = draggingDriveOf = -1;
    repaint();
}

//==============================================================================
MarsPage::Knob::Knob (const juce::String& text, const juce::String& tooltip) : label ({}, text)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
}

void MarsPage::Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

MarsPage::Choice::Choice (const juce::String& text, const juce::StringArray& items, const juce::String& tooltip) : label ({}, text)
{
    styleLabel (label);
    styleCombo (box);
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
}

void MarsPage::Choice::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment.reset();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box);
}

MarsPage::MarsPage (ParagonX1AudioProcessor& p)
    : processor (p),
      display (p),
      onAttachment (p.getParameters(), ParamIDs::marsOn, on),
      bands ("BANDS", { "1", "2", "3", "4" }, "How many bands: each its own saturation, split at the crossovers (drag them on the display)"),
      quality ("QUALITY", Mars::qualityNames, "How much it oversamples, to keep the saturation's fold-back (aliasing) down: Superb for mastering"),
      slope ("SLOPE", Mars::slopeNames, "How steeply the bands are split: the bands always add back up flat"),
      style ("STYLE", Mars::styleNames, "The kind of saturation: TUBE, TAPE, TRANSFORMER, AMP, or the effects (SMUDGE to DESTROY)"),
      preset (PresetButton::Hooks {
          "MARS",
          [] () -> const std::vector<StompPreset>& { return MarsPresets::factory(); },
          [&p] { return p.getMarsPresets().user(); },
          [&p] { return p.getMarsPresetName(); },
          [&p] { return p.isMarsPresetModified(); },
          [&p] (const StompPreset& chosen) { p.loadMarsPreset (chosen); },
          [&p] (const juce::String& name)
          {
              const auto saved = MarsPresets::capture (p.getParameters(), name);
              if (! p.getMarsPresets().save (saved))
                  return false;
              p.rememberMarsPreset (saved);
              return true;
          },
          [&p] (const StompPreset& gone)
          {
              if (! p.getMarsPresets().remove (gone))
                  return false;
              p.rememberMarsPreset (MarsPresets::capture (p.getParameters(), "Untitled"));
              return true;
          },
          [&p] { return p.getMarsPresets().getFolder(); } }),
      input ("INPUT", "Into all the bands: more drives them harder"),
      output ("OUTPUT", "After the bands"),
      mix ("MIX", "All of MARS against the sound as it came in (parallel saturation)"),
      drive ("DRIVE", "How hard the band is driven (its level kept even, so it changes the colour more than the loudness). Or drag up and down on the band"),
      dynamics ("DYNAMICS", "Right: squashes the band as it saturates. Left: a gate that takes its quiet parts down"),
      feedback ("FEEDBACK", "The band's output back into it, through a resonance at FB FREQ: it rings and howls"),
      feedbackFreq ("FB FREQ", "Where FEEDBACK rings"),
      bass ("BASS", "Tone after the saturation: the lows"),
      mid ("MID", "Tone after the saturation: the middle"),
      treble ("TREBLE", "Tone after the saturation: the highs"),
      presence ("PRESENCE", "Tone after the saturation: the bite around 4 to 6 kHz"),
      bandMix ("MIX", "The band's saturated sound against its clean sound"),
      level ("LEVEL", "The band's level: all the way down, it's silent")
{
    auto& state = p.getParameters();
    addAndMakeVisible (display);
    display.onSelect = [this] (int band) { selectBand (band); };

    bands.attach (state, ParamIDs::marsBands);
    quality.attach (state, ParamIDs::marsQuality);
    slope.attach (state, ParamIDs::marsSlope);
    input.attach (state, ParamIDs::marsInput);
    output.attach (state, ParamIDs::marsOutput);
    mix.attach (state, ParamIDs::marsMix);

    on.setTooltip ("Switch MARS on or off (the same as its POWER switch)");
    bandOn.setTooltip ("Off, the band passes clean: saturate only part of the sound (a clean low end under a driven top, say)");
    back.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    back.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    back.onClick = [this] { if (onClose) onClose(); };
    styleLabel (presetLabel);

    for (auto* component : std::initializer_list<juce::Component*> { &on, &bandOn, &back, &preset, &presetLabel })
        addAndMakeVisible (component);
    for (auto* choice : { &bands, &quality, &slope, &style })
    {
        addAndMakeVisible (choice->box);
        addAndMakeVisible (choice->label);
    }
    for (auto* knob : { &input, &output, &mix })
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }
    for (auto* knob : bandKnobs())
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }

    selectBand (0);
    startTimerHz (10);
}

std::vector<MarsPage::Knob*> MarsPage::bandKnobs()
{
    return { &drive, &dynamics, &feedback, &feedbackFreq, &bass, &mid, &treble, &presence, &bandMix, &level };
}

void MarsPage::selectBand (int band)
{
    selected = juce::jlimit (0, Mars::maxBands - 1, band);
    display.setSelected (selected);
    auto& state = processor.getParameters();
    bandOnAttachment.reset();
    bandOnAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ParamIDs::marsBand (selected, "On"), bandOn);
    style.attach (state, ParamIDs::marsBand (selected, "Style"));
    const char* parts[] = { "Drive", "Dynamics", "Feedback", "FbFreq", "Bass", "Mid", "Treble", "Presence", "Mix", "Level" };
    auto knobs = bandKnobs();
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->attach (state, ParamIDs::marsBand (selected, parts[i]));
    repaint (bandArea);
}

void MarsPage::timerCallback()
{
    // The crossovers only matter with more than one band; the slope too.
    const auto split = juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::marsBands)->load()) > 0;
    slope.box.setEnabled (split);
}

void MarsPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (64);
    area.removeFromTop (8);
    bandArea = area.removeFromBottom (150);
    area.removeFromBottom (8);
    globalArea = area.removeFromRight (120);
    area.removeFromRight (8);
    display.setBounds (area);

    // Top bar: the name (drawn), ON, BANDS, QUALITY, SLOPE; PRESET and BACK on the right.
    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (200);
    on.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    const auto column = [&bar] (Choice& choice, int width)
    {
        bar.removeFromLeft (16);
        auto c = bar.removeFromLeft (width);
        choice.label.setBounds (c.removeFromTop (14));
        choice.box.setBounds (c.withSizeKeepingCentre (width, 26));
    };
    column (bands, 70);
    column (quality, 130);
    column (slope, 110);
    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));
    bar.removeFromRight (24);
    auto presetColumn = bar.removeFromRight (220);
    presetLabel.setBounds (presetColumn.removeFromTop (14));
    preset.setBounds (presetColumn.withSizeKeepingCentre (220, 26));

    const auto place = [] (Knob& knob, juce::Rectangle<int> c)
    {
        knob.label.setBounds (c.removeFromTop (14));
        knob.slider.setBounds (c);
    };

    // INPUT, OUTPUT and MIX, one above the other.
    auto global = globalArea.reduced (10, 26);
    const auto third = global.getHeight() / 3;
    for (auto* knob : { &input, &output, &mix })
        place (*knob, global.removeFromTop (third).withSizeKeepingCentre (90, juce::jmin (third, 96)));

    // The picked band: its name (drawn), BAND ON and STYLE, then its knobs.
    auto row = bandArea.reduced (12, 8);
    row.removeFromTop (16);
    auto first = row.removeFromLeft (190);
    bandOn.setBounds (first.removeFromTop (34).withSizeKeepingCentre (110, 26));
    style.label.setBounds (first.removeFromTop (14));
    style.box.setBounds (first.removeFromTop (30).withSizeKeepingCentre (180, 26));
    row.removeFromLeft (12);
    auto knobs = bandKnobs();
    const auto width = row.getWidth() / (int) knobs.size();
    for (auto* knob : knobs)
        place (*knob, row.removeFromLeft (width));
}

void MarsPage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, globalArea, bandArea })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient.
    auto title = topBar.reduced (14, 8).removeFromLeft (200);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "MARS");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("MARS", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Multiband saturation", title, juce::Justification::topLeft, false);

    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("GLOBAL", globalArea.reduced (10, 8).removeFromTop (14), juce::Justification::centredLeft, false);
    g.drawText ("BAND " + juce::String (selected + 1) + "  (click a band on the display to pick it)", bandArea.reduced (12, 8).removeFromTop (14),
                juce::Justification::centredLeft, false);
}
