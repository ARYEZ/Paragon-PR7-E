#include "LimiterPage.h"

#include "DockPanels.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    using Style = effects::limiter::Style;

    void styleKnob (juce::Slider& knob)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
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

    juce::String dbText (float db, int decimals = 1)
    {
        return db <= -99.0f ? juce::String ("-inf") : (db > 0.05f ? "+" : "") + juce::String (db, decimals);
    }

    const std::array<float, 5> targets { -9.0f, -14.0f, -16.0f, -23.0f, -24.0f };
    const juce::StringArray targetNames { "-9 (loud CD)", "-14 (streaming)", "-16 (podcasts)", "-23 (EBU R128)", "-24 (ATSC A/85)" };
    const juce::StringArray timeScaleNames { "Momentary", "Short term", "Integrated" };
}

//==============================================================================
LimiterDisplay::LimiterDisplay (ParagonX1AudioProcessor& p) : processor (p)
{
    setTitle ("GUILLOTINE levels");
    incoming.resize (4096);
    startTimerHz (30);
}

float LimiterDisplay::yForDb (float db) const noexcept
{
    return plot.getY() + juce::jlimit (0.0f, 1.0f, -db / rangeDb) * plot.getHeight();
}

void LimiterDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (8.0f, 10.0f).withTrimmedRight (30.0f);
}

void LimiterDisplay::timerCallback()
{
    const auto read = processor.getLimiter().readFrames (incoming.data(), (int) incoming.size());
    if (read == 0 && ! history.empty())
        return;
    history.insert (history.end(), incoming.begin(), incoming.begin() + read);
    const auto keep = (size_t) juce::jmax (1, (int) (plot.getWidth() / 2.0f) + 2);
    if (history.size() > keep)
        history.erase (history.begin(), history.end() - (std::ptrdiff_t) keep);
    repaint();
}

void LimiterDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto db : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -30.0f })
    {
        const auto y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (db == 0.0f ? 0.16f : 0.05f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ParagonColours::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 4.0f, y - 6.0f, 26.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    const auto& state = processor.getParameters();
    const auto gainDb = state.getRawParameterValue (ParamIDs::limitGain)->load();
    const auto ceilingDb = state.getRawParameterValue (ParamIDs::limitCeiling)->load();

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());
    const auto column = 2.0f;
    const auto count = (int) history.size();
    const auto xFor = [&] (int i) { return plot.getRight() - (float) (count - 1 - i) * column; };

    // Going in (with GAIN), grey; coming out, the gradient.
    juce::Path in, out;
    in.startNewSubPath (xFor (0), plot.getBottom());
    out.startNewSubPath (xFor (0), plot.getBottom());
    for (int i = 0; i < count; ++i)
    {
        const auto& f = history[(size_t) i];
        in.lineTo (xFor (i), yForDb (f.inputDb + gainDb));
        out.lineTo (xFor (i), yForDb (f.outputDb));
    }
    in.lineTo (plot.getRight(), plot.getBottom());
    out.lineTo (plot.getRight(), plot.getBottom());
    in.closeSubPath();
    out.closeSubPath();
    g.setColour (juce::Colour (0xff4a4452));
    g.fillPath (in);
    auto fill = ParagonLookAndFeel::menuHighlight (plot);
    fill.multiplyOpacity (0.55f);
    g.setGradientFill (fill);
    g.fillPath (out);

    // Turned down, red from the top; the momentary loudness, a thin line.
    juce::Path loudness;
    bool started = false;
    for (int i = 0; i < count; ++i)
    {
        const auto& f = history[(size_t) i];
        if (f.reductionDb > 0.01f)
        {
            g.setColour (juce::Colour (0xffe8223f).withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (xFor (i) - column, plot.getY(), column, yForDb (-f.reductionDb) - plot.getY()));
        }
        if (f.momentaryLufs > effects::LoudnessMeter::silent + 1.0f)
        {
            const auto y = yForDb (f.momentaryLufs);
            if (! started)
                loudness.startNewSubPath (xFor (i), y);
            else
                loudness.lineTo (xFor (i), y);
            started = true;
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.strokePath (loudness, juce::PathStrokeType (1.2f));

    // The ceiling.
    {
        const auto y = yForDb (ceilingDb);
        const float dashes[] { 6.0f, 4.0f };
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawDashedLine (juce::Line<float> (plot.getX(), y, plot.getRight(), y), dashes, 2, 1.0f);
    }

    // The biggest dips, labelled: the most in each second, if it's 0.5 dB or more.
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (int start = count % 100; start < count; start += 100)
    {
        int most = -1;
        for (int i = start; i < juce::jmin (count, start + 100); ++i)
            if (most < 0 || history[(size_t) i].reductionDb > history[(size_t) most].reductionDb)
                most = i;
        if (most >= 0 && history[(size_t) most].reductionDb >= 0.5f)
        {
            const auto x = xFor (most);
            const auto y = yForDb (-history[(size_t) most].reductionDb);
            auto label = juce::Rectangle<float> (x - 22.0f, y + 2.0f, 44.0f, 15.0f);
            g.setColour (juce::Colour (0xcc0b090c));
            g.fillRoundedRectangle (label, 3.0f);
            g.setColour (juce::Colour (0xffffc04a));
            g.drawText (juce::String (-history[(size_t) most].reductionDb, 1), label, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
LimiterPage::Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : label ({}, text), attachment (state, id, slider)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

LimiterPage::Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : button (text), attachment (state, id, button)
{
    button.setTooltip (tooltip);
}

LimiterPage::Choice::Choice (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::StringArray& items,
                             const juce::String& tooltip)
    : label ({}, text), attachment (state, id, [this, &items] () -> juce::ComboBox& { styleCombo (box); box.addItemList (items, 1); return box; }())
{
    styleLabel (label);
    box.setTooltip (tooltip);
}

LimiterPage::LimiterPage (ParagonX1AudioProcessor& p)
    : processor (p),
      display (p),
      on (p.getParameters(), ParamIDs::limitOn, "ON", "Switch GUILLOTINE on or off (the same as its POWER switch)"),
      style (p.getParameters(), ParamIDs::limitMode, "STYLE", ParamRanges::limitModes,
             "How it limits. CLEAR stays out of the way; PUNCHY pumps a little, for punch; DYNAMIC lifts the hits first, keeping "
             "their attack; ALL-ROUND in between; AGGRESSIVE clips the top softly, loud and edgy; MODERN lets go fast after a lone "
             "peak, the best start for most music; GLUE starts under the ceiling, like a bus compressor; SAFE never distorts. "
             "BRICK, SOFT and AUTO are the earlier limiter, for older projects"),
      preset (PresetButton::Hooks {
          "GUILLOTINE",
          [] () -> const std::vector<StompPreset>& { return LimiterPresets::factory(); },
          [&p] { return p.getLimiterPresets().user(); },
          [&p] { return p.getLimiterPresetName(); },
          [&p] { return p.isLimiterPresetModified(); },
          [&p] (const StompPreset& chosen) { p.loadLimiterPreset (chosen); },
          [&p] (const juce::String& name)
          {
              // Yours keep the loudness TARGET too, as the mastering ones do.
              auto saved = LimiterPresets::capture (p.getParameters(), name);
              saved.parameters[LimiterPresetTraits::targetKey] = (float) p.getParameters().state.getProperty (targetKey, -14.0f);
              if (! p.getLimiterPresets().save (saved))
                  return false;
              p.rememberLimiterPreset (saved);
              return true;
          },
          [&p] (const StompPreset& gone)
          {
              if (! p.getLimiterPresets().remove (gone))
                  return false;
              p.rememberLimiterPreset (LimiterPresets::capture (p.getParameters(), "Untitled"));
              return true;
          },
          [&p] { return p.getLimiterPresets().getFolder(); } }),
      gainAttachment (p.getParameters(), ParamIDs::limitGain, gain),
      lookahead (p.getParameters(), ParamIDs::limitLookahead, "LOOKAHEAD",
                 "How far ahead it sees each peak coming (it's this much late, which your DAW makes up for). Longer is cleaner but "
                 "softer on the hits; 0 acts at once, close to clipping"),
      attack (p.getParameters(), ParamIDs::limitAttack, "ATTACK", "How soon the slower release stage sets in: shorter is safer and cleaner, "
                                                                  "longer is louder"),
      release (p.getParameters(), ParamIDs::limitRelease, "RELEASE", "How long it holds the level down: longer is cleaner but can pump; "
                                                                     "shorter is louder but can distort"),
      linkTransients (p.getParameters(), ParamIDs::limitLinkTransient, "LINK PEAKS",
                      "How much a short peak on one side turns down the other too (under 100% often works best)"),
      linkRelease (p.getParameters(), ParamIDs::limitLinkRelease, "LINK RELEASE", "How much both sides share the slower stage (100% keeps the stereo image)"),
      mix (p.getParameters(), ParamIDs::limitMix, "MIX", "BRICK, SOFT and AUTO: the limited sound blended with the dry"),
      ceiling (p.getParameters(), ParamIDs::limitCeiling, "OUTPUT", "The ceiling: nothing comes out louder (dBTP with TRUE PEAK). "
                                                                    "-1 dBTP is what streaming services ask for"),
      trim (p.getParameters(), ParamIDs::limitTrim, "TRIM", "A level change after the limiter (it moves the ceiling with it)"),
      legacyLink (p.getParameters(), ParamIDs::limitLink, "LINK", "BRICK, SOFT and AUTO: both channels limited together"),
      truePeak (p.getParameters(), ParamIDs::limitTruePeak, "TRUE PEAK",
                "Keeps the peaks a converter or a streaming encoder rebuilds between the samples under the ceiling too "
                "(adds about 2.3 ms of delay)"),
      unity (p.getParameters(), ParamIDs::limitUnity, "UNITY", "Hear it at the level it came in, so louder doesn't fool you (for setting it; not saved in rigs)"),
      delta (p.getParameters(), ParamIDs::limitDelta, "DELTA", "Hear only what it takes away (for setting it; not saved in rigs)"),
      dcFilter (p.getParameters(), ParamIDs::limitDcFilter, "DC FILTER", "Takes out a DC offset first, so it doesn't limit one side of the wave more"),
      oversampling (p.getParameters(), ParamIDs::limitOversampling, "OVERSAMPLING", effects::limiter::oversamplingNames,
                    "Limits at a higher rate, for less distortion and fewer peaks between samples, at more CPU (4x is plenty)"),
      dither (p.getParameters(), ParamIDs::limitDither, "DITHER", effects::limiter::ditherNames,
              "For a final CD-style master at a lower bit depth: last of all. Rarely heard on loud music"),
      shaping (p.getParameters(), ParamIDs::limitShaping, "NOISE SHAPING", effects::limiter::shapingNames,
               "Moves the dither noise away from where the ear is most sensitive (WEIGHTED is designed for 44.1 kHz)")
{
    addAndMakeVisible (display);
    gain.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
    gain.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    gain.setColour (juce::Slider::trackColourId, ParagonColours::accent);
    gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    gain.setTooltip ("Drives the sound into the ceiling: louder, with more limiting");
    gain.setDoubleClickReturnValue (true, 0.0);
    styleLabel (gainLabel);
    back.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    back.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    back.onClick = [this] { if (onClose) onClose(); };
    reset.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    reset.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    reset.setTooltip ("Start the loudness and the held peaks afresh");
    reset.onClick = [this] { resetReadings(); };

    styleCombo (targetBox);
    targetBox.addItemList (targetNames, 1);
    targetBox.setTooltip ("The loudness to aim for: set GAIN so INTEGRATED ends up here");
    targetBox.onChange = [this]
    {
        processor.getParameters().state.setProperty (targetKey, targets[(size_t) juce::jlimit (0, 4, targetBox.getSelectedItemIndex())], nullptr);
        repaint (meterArea);
    };
    styleCombo (timeScaleBox);
    timeScaleBox.addItemList (timeScaleNames, 1);
    timeScaleBox.setTooltip ("The big loudness bar: MOMENTARY (the last 0.4 s), SHORT TERM (3 s) or INTEGRATED (everything since RESET)");
    timeScaleBox.onChange = [this]
    {
        processor.getParameters().state.setProperty (timeScaleKey, juce::jmax (0, timeScaleBox.getSelectedItemIndex()), nullptr);
        repaint (meterArea);
    };
    for (auto* label : { &targetLabel, &timeScaleLabel, &presetLabel })
        styleLabel (*label);

    for (auto* component : std::initializer_list<juce::Component*> { &on.button, &style.box, &style.label, &back, &gain, &gainLabel,
                                                                    &truePeak.button, &unity.button, &delta.button, &dcFilter.button,
                                                                    &oversampling.box, &oversampling.label, &dither.box, &dither.label,
                                                                    &shaping.box, &shaping.label, &targetBox, &timeScaleBox, &targetLabel,
                                                                    &timeScaleLabel, &reset, &preset, &presetLabel })
        addAndMakeVisible (component);
    for (auto* knob : { &lookahead, &attack, &release, &linkTransients, &linkRelease, &ceiling, &trim })
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }
    addChildComponent (mix.slider);
    addChildComponent (mix.label);
    addChildComponent (legacyLink.button);

    timerCallback();
    startTimerHz (20);
}

float LimiterPage::target() const
{
    return (float) processor.getParameters().state.getProperty (targetKey, -14.0f);
}

int LimiterPage::timeScale() const
{
    return juce::jlimit (0, 2, (int) processor.getParameters().state.getProperty (timeScaleKey, 2));
}

void LimiterPage::resetReadings()
{
    heldTruePeakDb = -100.0f;
    heldReductionDb = 0.0f;
    processor.getLimiter().getLoudness().requestReset();
    repaint (meterArea);
}

void LimiterPage::showLegacyControls()
{
    // BRICK, SOFT and AUTO are the earlier limiter: no look-ahead, release
    // stage, oversampling or true peak; MIX and LINK instead of the links.
    const auto legacy = effects::limiter::isLegacy ((Style) juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::limitMode)->load()));
    for (auto* knob : { &lookahead, &attack, &release })
        knob->slider.setEnabled (! legacy);
    for (auto* knob : { &linkTransients, &linkRelease })
    {
        knob->slider.setVisible (! legacy);
        knob->label.setVisible (! legacy);
    }
    mix.slider.setVisible (legacy);
    mix.label.setVisible (legacy);
    legacyLink.button.setVisible (legacy);
    truePeak.button.setEnabled (! legacy);
    oversampling.box.setEnabled (! legacy);
    shaping.box.setEnabled (juce::roundToInt (processor.getParameters().getRawParameterValue (ParamIDs::limitDither)->load()) > 0);
}

void LimiterPage::timerCallback()
{
    auto& limiter = processor.getLimiter();
    heldTruePeakDb = juce::jmax (heldTruePeakDb, limiter.getOutputTruePeakDb());
    heldReductionDb = juce::jmax (heldReductionDb, limiter.getReductionDb (0), limiter.getReductionDb (1));
    const auto targetIndex = (int) std::distance (targets.begin(), std::find_if (targets.begin(), targets.end(),
                                                                                [this] (float t) { return std::abs (t - target()) < 0.05f; }));
    if (targetBox.getSelectedItemIndex() != targetIndex && targetIndex < (int) targets.size())
        targetBox.setSelectedItemIndex (targetIndex, juce::dontSendNotification);
    if (timeScaleBox.getSelectedItemIndex() != timeScale())
        timeScaleBox.setSelectedItemIndex (timeScale(), juce::dontSendNotification);
    showLegacyControls();
    repaint (meterArea);
    repaint (gainArea);
}

void LimiterPage::mouseDown (const juce::MouseEvent& e)
{
    // Click the held readings to start them afresh, as on Pro-L 2.
    if (peakReadout.contains (e.getPosition()))
        resetReadings();
}

void LimiterPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (64);
    area.removeFromTop (8);
    controlsArea = area.removeFromBottom (118);
    area.removeFromBottom (8);
    gainArea = area.removeFromLeft (96);
    area.removeFromLeft (8);
    meterArea = area.removeFromRight (300);
    area.removeFromRight (8);
    display.setBounds (area);

    // Top bar: the name (drawn), ON, STYLE; BACK on the right.
    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (200);
    on.button.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    bar.removeFromLeft (16);
    auto styleColumn = bar.removeFromLeft (170);
    style.label.setBounds (styleColumn.removeFromTop (14));
    style.box.setBounds (styleColumn.withSizeKeepingCentre (170, 26));
    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));
    bar.removeFromRight (24);
    auto presetColumn = bar.removeFromRight (220);
    presetLabel.setBounds (presetColumn.removeFromTop (14));
    preset.setBounds (presetColumn.withSizeKeepingCentre (220, 26));

    // GAIN, tall.
    auto gainColumn = gainArea.reduced (8, 10);
    gainLabel.setBounds (gainColumn.removeFromTop (16));
    gain.setBounds (gainColumn);

    // The meters' controls, at the bottom of their panel.
    auto meters = meterArea.reduced (12, 10);
    auto bottom = meters.removeFromBottom (82);
    auto row1 = bottom.removeFromTop (40);
    const auto halfWidth = (row1.getWidth() - 8) / 2;
    auto targetColumn = row1.removeFromLeft (halfWidth);
    row1.removeFromLeft (8);
    targetLabel.setBounds (targetColumn.removeFromTop (14));
    targetBox.setBounds (targetColumn.withSizeKeepingCentre (halfWidth, 24));
    timeScaleLabel.setBounds (row1.removeFromTop (14));
    timeScaleBox.setBounds (row1.withSizeKeepingCentre (row1.getWidth(), 24));
    bottom.removeFromTop (8);
    reset.setBounds (bottom.removeFromTop (28).removeFromRight (90));

    // Underneath: the advanced settings; OUTPUT and TRIM; the switches; the output options.
    const auto place = [] (juce::Label& label, juce::Component& control, juce::Rectangle<int> column)
    {
        label.setBounds (column.removeFromTop (14));
        control.setBounds (column);
    };
    auto controls = controlsArea.reduced (12, 8);
    controls.removeFromTop (16);   // the section names, drawn
    auto advanced = controls.removeFromLeft (440);
    const auto knob = advanced.getWidth() / 5;
    place (lookahead.label, lookahead.slider, advanced.removeFromLeft (knob));
    place (attack.label, attack.slider, advanced.removeFromLeft (knob));
    place (release.label, release.slider, advanced.removeFromLeft (knob));
    auto links = advanced;
    place (linkTransients.label, linkTransients.slider, links.removeFromLeft (knob));
    place (linkRelease.label, linkRelease.slider, links.removeFromLeft (knob));
    auto legacyColumns = advanced;
    place (mix.label, mix.slider, legacyColumns.removeFromLeft (knob));
    legacyLink.button.setBounds (legacyColumns.removeFromLeft (knob).withSizeKeepingCentre (knob, 26));
    controls.removeFromLeft (16);
    outputArea = controls.removeFromLeft (180);
    auto output = outputArea;
    place (ceiling.label, ceiling.slider, output.removeFromLeft (90));
    place (trim.label, trim.slider, output);
    controls.removeFromLeft (16);
    auto switches = controls.removeFromLeft (220);
    const auto half = switches.getHeight() / 2;
    auto top = switches.removeFromTop (half);
    truePeak.button.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (108, 26));
    unity.button.setBounds (top.withSizeKeepingCentre (100, 26));
    dcFilter.button.setBounds (switches.removeFromLeft (110).withSizeKeepingCentre (108, 26));
    delta.button.setBounds (switches.withSizeKeepingCentre (100, 26));
    controls.removeFromLeft (16);
    auto options = controls;
    const auto third = options.getWidth() / 3;
    for (auto* choice : { &oversampling, &dither, &shaping })
    {
        auto column = options.removeFromLeft (third).reduced (4, 0);
        choice->label.setBounds (column.removeFromTop (14));
        choice->box.setBounds (column.withSizeKeepingCentre (column.getWidth(), 26));
    }
}

void LimiterPage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, gainArea, meterArea, controlsArea })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient.
    auto title = topBar.reduced (14, 8).removeFromLeft (200);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "GUILLOTINE");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("GUILLOTINE", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Limiter and loudness", title, juce::Justification::topLeft, false);

    // The delay it adds, under STYLE.
    {
        const auto latency = processor.getLimiter().getLatencySamples (processor.getLimiterSettings());
        const auto rate = processor.getLimiter().getSampleRate();
        auto note = topBar.reduced (14, 8).withTrimmedLeft (200 + 60 + 16 + 170 + 16).withTrimmedRight (160 + 244);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (latency > 0 ? "Delay " + juce::String (latency * 1000.0 / rate, 2) + " ms (your DAW makes up for it)" : juce::String ("No delay"),
                    note, juce::Justification::centredLeft, false);
    }

    // Section names underneath.
    {
        auto names = controlsArea.reduced (12, 8).removeFromTop (14);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("ADVANCED", names.removeFromLeft (440), juce::Justification::centredLeft, false);
        names.removeFromLeft (16);
        g.drawText ("OUTPUT", names.removeFromLeft (180), juce::Justification::centredLeft, false);
        names.removeFromLeft (16);
        g.drawText ("OPTIONS", names.removeFromLeft (220), juce::Justification::centredLeft, false);
        names.removeFromLeft (16);
        g.drawText ("FINISHING", names, juce::Justification::centredLeft, false);
    }

    // The meters: output (peak and true peak), gain reduction, loudness.
    auto& limiter = processor.getLimiter();
    const auto& state = processor.getParameters();
    const auto ceilingDb = state.getRawParameterValue (ParamIDs::limitCeiling)->load();
    auto meters = meterArea.reduced (12, 10);
    meters.removeFromBottom (82);
    peakReadout = meters.removeFromTop (40);
    {
        auto readout = peakReadout;
        auto left = readout.removeFromLeft (readout.getWidth() / 2);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (ParagonColours::textDim);
        g.drawText ("TRUE PEAK", left.removeFromTop (14), juce::Justification::centredLeft, false);
        g.drawText ("REDUCTION", readout.removeFromTop (14), juce::Justification::centredLeft, false);
        // Green under the ceiling, orange over it, red over 0 dB.
        g.setColour (heldTruePeakDb > 0.0f ? juce::Colour (0xffff3b3b) : heldTruePeakDb > ceilingDb + 0.05f ? juce::Colour (0xffffa53b)
                                                                                                           : juce::Colour (0xff57d68d));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (dbText (heldTruePeakDb), left, juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::text);
        g.drawText (dbText (-heldReductionDb), readout, juce::Justification::centredLeft, false);
    }
    meters.removeFromTop (6);

    const auto meterBar = [&g] (juce::Rectangle<float> bar, float fraction, bool fromTop, juce::ColourGradient fill)
    {
        g.setColour (juce::Colour (0xff09070a));
        g.fillRoundedRectangle (bar, 3.0f);
        fraction = juce::jlimit (0.0f, 1.0f, fraction);
        const auto filled = fromTop ? bar.withHeight (bar.getHeight() * fraction) : bar.withTrimmedTop (bar.getHeight() * (1.0f - fraction));
        g.setGradientFill (fill);
        g.fillRoundedRectangle (filled, 3.0f);
    };
    auto bars = meters;
    const auto barScale = 30.0f;
    auto outputBar = bars.removeFromLeft (26).toFloat();
    bars.removeFromLeft (6);
    auto reductionBar = bars.removeFromLeft (26).toFloat();
    bars.removeFromLeft (14);
    const auto outputDb = limiter.getOutputPeakDb();
    meterBar (outputBar.withTrimmedBottom (16), 1.0f + outputDb / barScale, false, ParagonLookAndFeel::menuHighlight (outputBar));
    {
        juce::ColourGradient red (juce::Colour (0xffe8223f), reductionBar.getTopLeft(), juce::Colour (0xffff6b3b), reductionBar.getBottomLeft(), false);
        const auto reduction = juce::jmax (limiter.getReductionDb (0), limiter.getReductionDb (1));
        meterBar (reductionBar.withTrimmedBottom (16), reduction / 12.0f, true, red);
    }
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("OUT", outputBar.withTop (outputBar.getBottom() - 14.0f), juce::Justification::centred, false);
    g.drawText ("GR", reductionBar.withTop (reductionBar.getBottom() - 14.0f), juce::Justification::centred, false);

    // Loudness: the big bar for the chosen reading, the target marked, all four readings beside it.
    auto& loudness = limiter.getLoudness();
    const std::array<float, 3> readings { loudness.getMomentary(), loudness.getShortTerm(), loudness.getIntegrated() };
    const auto shown = readings[(size_t) timeScale()];
    auto loudnessBar = bars.removeFromLeft (40).toFloat().withTrimmedBottom (16);
    const auto lufsTop = target() + 9.0f, lufsBottom = target() - 27.0f;   // the meter: 9 LU over the target to 27 under
    const auto yForLufs = [&] (float lufs) { return loudnessBar.getY() + juce::jlimit (0.0f, 1.0f, (lufsTop - lufs) / (lufsTop - lufsBottom)) * loudnessBar.getHeight(); };
    {
        g.setColour (juce::Colour (0xff09070a));
        g.fillRoundedRectangle (loudnessBar, 3.0f);
        if (shown > effects::LoudnessMeter::silent + 1.0f)
        {
            g.setGradientFill (ParagonLookAndFeel::menuHighlight (loudnessBar));
            g.fillRoundedRectangle (loudnessBar.withTop (yForLufs (shown)), 3.0f);
        }
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (loudnessBar.getX() - 4.0f, yForLufs (target()) - 1.0f, loudnessBar.getWidth() + 8.0f, 2.0f));
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("LUFS", loudnessBar.withY (loudnessBar.getBottom() + 2.0f).withHeight (14.0f), juce::Justification::centred, false);
    }
    bars.removeFromLeft (12);
    {
        auto text = bars;
        const auto line = [&g, &text] (const juce::String& name, const juce::String& value, bool big)
        {
            auto row = text.removeFromTop (big ? 40 : 22);
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (name, row.removeFromTop (12), juce::Justification::centredLeft, false);
            g.setColour (big ? ParagonColours::accent : ParagonColours::text);
            g.setFont (juce::FontOptions (big ? 22.0f : 13.0f, juce::Font::bold));
            g.drawText (value, row, juce::Justification::centredLeft, false);
        };
        const auto lufsText = [] (float lufs) { return lufs <= effects::LoudnessMeter::silent + 1.0f ? juce::String ("-") : juce::String (lufs, 1); };
        line (timeScaleNames[timeScale()].toUpperCase(), lufsText (shown), true);
        if (shown > effects::LoudnessMeter::silent + 1.0f)
        {
            const auto off = shown - target();
            g.setColour (ParagonColours::textDim);
            g.setFont (juce::FontOptions (10.5f));
            g.drawText ((off >= 0.0f ? "+" : "") + juce::String (off, 1) + " LU from target", text.removeFromTop (16), juce::Justification::centredLeft, false);
        }
        text.removeFromTop (6);
        line ("MOMENTARY", lufsText (readings[0]), false);
        line ("SHORT TERM", lufsText (readings[1]), false);
        line ("INTEGRATED", lufsText (readings[2]), false);
        line ("RANGE (LRA)", juce::String (loudness.getRange(), 1) + " LU", false);
    }
}
