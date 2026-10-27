#include "EqPage.h"

#include "DockPanels.h"
#include "Icons.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
    void styleKnob (juce::Slider& knob, int textWidth)
    {
        knob.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setTextBoxStyle (knob.getTextBoxPosition(), false, textWidth, 18);
    }

    void styleCombo (juce::ComboBox& box, const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff09070a));
        box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        box.setColour (juce::ComboBox::textColourId, ParagonColours::text);
        box.setColour (juce::ComboBox::arrowColourId, ParagonColours::text);
    }

    void styleLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setJustificationType (juce::Justification::centredLeft);
    }

    void styleButton (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    }
}

//==============================================================================
EqPage::EqPage (ParagonX1AudioProcessor& p)
    : processor (p),
      display (p),
      onAttachment (p.getParameters(), ParamIDs::eqOn, onButton),
      autoGainAttachment (p.getParameters(), ParamIDs::eqAutoGain, autoGain),
      outputAttachment (p.getParameters(), ParamIDs::eqOutput, output),
      gainScaleAttachment (p.getParameters(), ParamIDs::eqGainScale, gainScale)
{
    addAndMakeVisible (display);
    display.onSelect = [this] (int selected) { showBand (selected); };

    onButton.setTooltip ("Switch ECHELON on or off (the same as stomp 4)");
    autoGain.setTooltip ("Turn the output down (or up) by about as much as the bands make it louder (or quieter), "
                         "an estimate from the curve, so you can compare by ear without being fooled by level");
    output.setTooltip ("ECHELON's output level");
    gainScale.setTooltip ("Scales every band's gain: 0% flat, 100% as set, 200% twice as much");
    output.setDoubleClickReturnValue (true, 0.0);
    gainScale.setDoubleClickReturnValue (true, 1.0);
    styleKnob (output, 64);
    styleKnob (gainScale, 52);
    showInput.setToggleState (true, juce::dontSendNotification);
    showOutput.setToggleState (true, juce::dontSendNotification);
    showInput.setTooltip ("Show the spectrum going into ECHELON (shaded)");
    showOutput.setTooltip ("Show the spectrum coming out of ECHELON (a line)");
    showInput.onClick = [this] { display.showInput = showInput.getToggleState(); };
    showOutput.onClick = [this] { display.showOutput = showOutput.getToggleState(); };
    const auto plusMinus = juce::String::fromUTF8 ("\xc2\xb1");
    styleCombo (range, { plusMinus + "3 dB", plusMinus + "6 dB", plusMinus + "12 dB", plusMinus + "30 dB" });
    range.setSelectedItemIndex (2, juce::dontSendNotification);
    range.onChange = [this] { display.setRangeDb (std::array<double, 4> { 3.0, 6.0, 12.0, 30.0 }[(size_t) juce::jmax (0, range.getSelectedItemIndex())]); };
    styleButton (back);
    back.onClick = [this] { if (onClose) onClose(); };
    styleButton (match);
    match.setClickingTogglesState (false);
    match.setColour (juce::TextButton::buttonOnColourId, ParagonColours::accentDim.withAlpha (0.8f));
    match.setColour (juce::TextButton::textColourOnId, ParagonColours::accent);
    match.setTooltip ("MATCH: listen to your track and a reference (another part of the song, a song file, or a saved one), "
                      "then add bands that make your track's tone like the reference's");
    match.onClick = [this] { showMatch (! matching); };
    matchPanel.onClose = [this] { showMatch (false); };
    addChildComponent (matchPanel);
    for (auto* label : { &outputLabel, &gainScaleLabel, &analyserLabel, &rangeLabel })
        styleLabel (*label);

    for (auto* component : std::initializer_list<juce::Component*> { &onButton, &autoGain, &showInput, &showOutput, &output, &gainScale, &range,
                                                                      &back, &match, &outputLabel, &gainScaleLabel, &analyserLabel, &rangeLabel })
        addAndMakeVisible (component);

    // The band controls.
    styleCombo (shape, eq::shapeNames);
    styleCombo (slope, eq::slopeNames);
    styleCombo (placement, eq::placementNames);
    for (auto* knob : { &frequency, &gain, &q, &dynRange, &threshold, &attack, &release })
        styleKnob (*knob, 70);
    frequency.setDoubleClickReturnValue (true, 1000.0);
    gain.setDoubleClickReturnValue (true, 0.0);
    q.setDoubleClickReturnValue (true, 1.0);
    const eq::Band defaults;
    dynRange.setDoubleClickReturnValue (true, 0.0);
    threshold.setDoubleClickReturnValue (true, defaults.thresholdDb);
    attack.setDoubleClickReturnValue (true, defaults.attack);
    release.setDoubleClickReturnValue (true, defaults.release);
    dynRange.setTooltip ("Makes the band dynamic: how far its gain moves when its part of the sound gets loud "
                         "(below 0 it cuts, above 0 it boosts). 0: an ordinary band");
    dynAuto.setTooltip ("The threshold follows the band's own level, so it acts on what stands out above its usual level");
    threshold.setTooltip ("How loud the band's part must get before the band moves (with AUTO off)");
    attack.setTooltip ("How quickly it acts: 50% is automatic (set from the band's frequency); lower is faster, higher slower");
    release.setTooltip ("How quickly it lets go: 50% is automatic (set from the band's frequency); lower is faster, higher slower");
    for (auto* label : { &frequencyLabel, &gainLabel, &qLabel, &shapeLabel, &slopeLabel, &placementLabel,
                         &dynRangeLabel, &thresholdLabel, &attackLabel, &releaseLabel })
    {
        styleLabel (*label);
        label->setJustificationType (juce::Justification::centred);
    }
    for (auto* button : { &previous, &next, &solo, &remove })
        styleButton (*button);
    previous.setTooltip ("The band before (left of) this one");
    next.setTooltip ("The band after (right of) this one");
    solo.setTooltip ("Hold to hear only the part of the sound this band works on");
    remove.setTooltip ("Delete this band (Delete key)");
    placement.setTooltip ("What the band works on: both channels (Stereo), one, or the middle or sides of the stereo picture");
    previous.onClick = [this] { step (-1); };
    next.onClick = [this] { step (1); };
    solo.onStateChange = [this] { processor.setEqSolo (solo.isDown() && band >= 0 ? band : -1); };
    remove.onClick = [this]
    {
        if (band >= 0)
            EqBands (processor).remove (band);
        display.select (-1);
    };
    for (auto* component : std::initializer_list<juce::Component*> { &bandOn, &shape, &slope, &placement, &frequency, &gain, &q,
                                                                      &frequencyLabel, &gainLabel, &qLabel, &shapeLabel, &slopeLabel,
                                                                      &placementLabel, &previous, &next, &solo, &remove,
                                                                      &dynRange, &dynRangeLabel })
        addChildComponent (component);
    for (auto* component : dynamicRowComponents())
        addChildComponent (component);

    showBand (-1);
    startTimerHz (30);
}

void EqPage::setAnalyser (bool on)
{
    showInput.setToggleState (on, juce::dontSendNotification);
    showOutput.setToggleState (on, juce::dontSendNotification);
    display.showInput = display.showOutput = on;
}

void EqPage::showMatch (bool shouldShow)
{
    if (shouldShow == matching)
        return;
    matching = shouldShow;
    match.setToggleState (matching, juce::dontSendNotification);
    matchPanel.setVisible (matching);
    showBand (band);   // the band controls hide while matching, and come back after
    resized();
    repaint();
}

std::vector<juce::Component*> EqPage::dynamicRowComponents() noexcept
{
    return { &dynAuto, &threshold, &attack, &release, &thresholdLabel, &attackLabel, &releaseLabel };
}

EqPage::~EqPage()
{
    processor.setEqSolo (-1);
}

void EqPage::showBand (int newBand)
{
    band = newBand;
    bandOnAttachment.reset();
    shapeAttachment.reset();
    slopeAttachment.reset();
    placementAttachment.reset();
    frequencyAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    dynRangeAttachment.reset();
    thresholdAttachment.reset();
    attackAttachment.reset();
    releaseAttachment.reset();
    dynAutoAttachment.reset();
    processor.setEqSolo (-1);

    const auto visible = band >= 0 && ! matching;
    for (auto* component : std::initializer_list<juce::Component*> { &bandOn, &shape, &slope, &placement, &frequency, &gain, &q,
                                                                      &frequencyLabel, &gainLabel, &qLabel, &shapeLabel, &slopeLabel,
                                                                      &placementLabel, &previous, &next, &solo, &remove,
                                                                      &dynRange, &dynRangeLabel })
        component->setVisible (visible);
    if (! visible)
    {
        timerCallback();   // hides the dynamics row
        repaint();
        return;
    }

    const EqBands bands (processor);
    bandOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (bands.parameter (band, "On"), bandOn);
    shapeAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (bands.parameter (band, "Shape"), shape);
    slopeAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (bands.parameter (band, "Slope"), slope);
    placementAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (bands.parameter (band, "Place"), placement);
    frequencyAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Freq"), frequency);
    gainAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Gain"), gain);
    qAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Q"), q);
    dynRangeAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Dyn"), dynRange);
    thresholdAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Thresh"), threshold);
    attackAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Attack"), attack);
    releaseAttachment = std::make_unique<juce::SliderParameterAttachment> (bands.parameter (band, "Release"), release);
    dynAutoAttachment = std::make_unique<juce::ButtonParameterAttachment> (bands.parameter (band, "DynAuto"), dynAuto);
    timerCallback();
    repaint();
}

void EqPage::step (int direction)
{
    // Through the bands in the order they sit, low to high.
    const auto settings = processor.getEqSettings();
    std::vector<int> order;
    for (int i = 0; i < (int) settings.bands.size(); ++i)
        if (settings.bands[(size_t) i].used)
            order.push_back (i);
    if (order.empty())
        return;
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return settings.bands[(size_t) a].frequency < settings.bands[(size_t) b].frequency; });
    const auto at = std::find (order.begin(), order.end(), band);
    const auto index = at == order.end() ? 0 : (int) std::distance (order.begin(), at) + direction;
    display.select (order[(size_t) ((index + (int) order.size()) % (int) order.size())]);
}

void EqPage::timerCallback()
{
    auto wantDynamics = false;
    if (band >= 0)
    {
        // Only what the shape uses can be turned (a cut has no gain, a 6 dB/oct shelf no Q...).
        const auto settings = processor.getEqSettings();
        const auto& b = settings.bands[(size_t) band];
        gain.setEnabled (eq::usesGain (b.shape));
        dynRange.setEnabled (eq::usesGain (b.shape));
        slope.setEnabled (eq::usesSlope (b.shape));
        q.setEnabled (eq::usesQ (b.shape, b.slope));
        threshold.setEnabled (! b.autoThreshold);
        wantDynamics = ! matching && b.used && eq::usesGain (b.shape) && std::abs (b.dynamicRangeDb) >= 0.01;
    }
    if (wantDynamics != dynamicRow)
    {
        // The dynamics row comes and goes with the band's range, as in Pro-Q.
        dynamicRow = wantDynamics;
        for (auto* component : dynamicRowComponents())
            component->setVisible (dynamicRow);
        resized();
        repaint();
    }
    repaint (topBar);
    if (dynamicRow)
        repaint (meterArea);
}

void EqPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (46);
    area.removeFromTop (8);
    controlsArea = area.removeFromBottom (matching ? 150 : dynamicRow ? 206 : 112);
    matchPanel.setBounds (controlsArea);
    area.removeFromBottom (8);
    display.setBounds (area);
    auto rows = controlsArea;
    rows.removeFromTop (112);
    dynamicArea = rows;

    auto bar = topBar.reduced (12, 6);
    bar.removeFromLeft (titleWidth);   // the name, drawn
    onButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (14);
    outputLabel.setBounds (bar.removeFromLeft (52));
    output.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (12);
    gainScaleLabel.setBounds (bar.removeFromLeft (76));
    gainScale.setBounds (bar.removeFromLeft (98));
    bar.removeFromLeft (12);
    autoGain.setBounds (bar.removeFromLeft (104));
    bar.removeFromLeft (70);   // AUTO GAIN's correction, drawn
    match.setBounds (bar.removeFromLeft (86).reduced (0, 3));
    back.setBounds (bar.removeFromRight (140).reduced (0, 3));
    bar.removeFromRight (20);
    range.setBounds (bar.removeFromRight (92).reduced (0, 4));
    rangeLabel.setBounds (bar.removeFromRight (46));
    bar.removeFromRight (16);
    showOutput.setBounds (bar.removeFromRight (66));
    showInput.setBounds (bar.removeFromRight (52));
    analyserLabel.setBounds (bar.removeFromRight (70));

    auto controls = controlsArea.withHeight (112).reduced (14, 10);
    controls.removeFromLeft (118);   // the band's name, drawn
    auto arrows = controls.removeFromLeft (64);
    previous.setBounds (arrows.removeFromLeft (30).withSizeKeepingCentre (30, 26));
    next.setBounds (arrows.removeFromRight (30).withSizeKeepingCentre (30, 26));
    controls.removeFromLeft (14);
    bandOn.setBounds (controls.removeFromLeft (56).withSizeKeepingCentre (56, 26));
    controls.removeFromLeft (8);

    const auto labelled = [&] (juce::Label& label, juce::Component& component, int width, bool knob)
    {
        auto column = controls.removeFromLeft (width);
        label.setBounds (column.removeFromTop (16));
        component.setBounds (knob ? column : column.withSizeKeepingCentre (width - 8, 26));
        controls.removeFromLeft (8);
    };
    labelled (shapeLabel, shape, 128, false);
    labelled (slopeLabel, slope, 112, false);
    labelled (frequencyLabel, frequency, 92, true);
    labelled (gainLabel, gain, 92, true);
    labelled (dynRangeLabel, dynRange, 92, true);
    labelled (qLabel, q, 92, true);
    labelled (placementLabel, placement, 104, false);
    remove.setBounds (controls.removeFromRight (90).withSizeKeepingCentre (90, 28));
    controls.removeFromRight (10);
    solo.setBounds (controls.removeFromRight (90).withSizeKeepingCentre (90, 28));

    // The dynamics row: under the band's name, "DYNAMICS"; AUTO under ON.
    controls = dynamicArea.reduced (14, 0).withTrimmedBottom (10);
    controls.removeFromLeft (118 + 64 + 14);
    dynAuto.setBounds (controls.removeFromLeft (72).withSizeKeepingCentre (72, 26));
    controls.removeFromLeft (8);
    labelled (thresholdLabel, threshold, 92, true);
    labelled (attackLabel, attack, 92, true);
    labelled (releaseLabel, release, 92, true);
    controls.removeFromLeft (16);
    meterArea = controls.removeFromLeft (520).withSizeKeepingCentre (520, 64);
}

void EqPage::paint (juce::Graphics& g)
{
    // The page sits on the plugin's dark background, as PRESETS and SETTINGS do.
    g.fillAll (ParagonColours::background);
    DockStyle::drawPanel (g, topBar.toFloat());
    DockStyle::drawPanel (g, controlsArea.toFloat());

    // The name in the red to pink to orange gradient, as on DOMINION's, COMP's and the LIMITER's pages.
    auto title = topBar.reduced (12, 6).removeFromLeft (titleWidth);
    const auto titleFont = juce::FontOptions (17.0f, juce::Font::bold);
    const auto nameWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "ECHELON");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (title.toFloat().withWidth (nameWidth)));
    g.setFont (titleFont);
    g.drawText ("ECHELON", title, juce::Justification::centredLeft, false);

    // AUTO GAIN's current correction, beside its switch.
    if (autoGain.getToggleState())
    {
        const auto db = processor.getEqAutoGainDb();
        g.setColour (ParagonColours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ((db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB",
                    autoGain.getBounds().translated (autoGain.getWidth(), 0).withWidth (68), juce::Justification::centredLeft, false);
    }

    if (matching)
        return;   // EQ Match's panel is there
    auto controls = controlsArea.reduced (14, 10);
    auto name = controls.removeFromLeft (118);
    if (band < 0)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Click a band's dot to change it, or double-click the display to add one (24 at most).",
                    controlsArea, juce::Justification::centred, false);
        return;
    }
    const auto colour = EqBands::colourFor (processor.getEqSettings().bands[(size_t) band]);
    name = name.withHeight (92);
    g.setColour (colour);
    g.fillEllipse (name.removeFromLeft (22).toFloat().withSizeKeepingCentre (14.0f, 14.0f));
    name.removeFromLeft (6);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("BAND " + juce::String (band + 1), name, juce::Justification::centredLeft, false);

    if (dynamicRow)
        paintDynamics (g);
}

void EqPage::paintDynamics (juce::Graphics& g)
{
    // A line between the rows, and the row's name.
    g.setColour (ParagonColours::panelEdge);
    g.drawHorizontalLine (dynamicArea.getY(), (float) dynamicArea.getX() + 14.0f, (float) dynamicArea.getRight() - 14.0f);
    auto title = dynamicArea.reduced (14, 0).withTrimmedBottom (10).removeFromLeft (118);
    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("DYNAMICS", title.withTrimmedLeft (28), juce::Justification::centredLeft, false);

    // What it hears against its threshold, and how far its gain has moved
    // (of its range).
    const auto settings = processor.getEqSettings();
    const auto& b = settings.bands[(size_t) band];
    const auto reading = processor.getEqDynamics (band);
    const auto colour = EqBands::colourFor (b);
    auto area = meterArea;
    const auto rowHeight = area.getHeight() / 2;

    const auto bar = [&] (juce::Rectangle<int> row, const juce::String& name, const juce::String& text)
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (name, row.removeFromLeft (78), juce::Justification::centredLeft, false);
        g.setColour (ParagonColours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (text, row.removeFromRight (200), juce::Justification::centredLeft, false);
        row.removeFromRight (10);
        const auto track = row.toFloat().withSizeKeepingCentre ((float) row.getWidth(), 10.0f);
        g.setColour (juce::Colour (0xff09070a));
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (track, 3.0f, 1.0f);
        return track;
    };

    // HEARS: -60 to 0 dB, with the threshold marked.
    using namespace eq::dynamics;
    const auto levelText = reading.levelDb <= floorDb + 1.0f ? juce::String ("-") : juce::String (reading.levelDb, 1) + " dB";
    const auto hears = bar (area.removeFromTop (rowHeight), "HEARS",
                            levelText + "  (threshold " + juce::String (reading.thresholdDb, 1) + (b.autoThreshold ? ", auto)" : ")"));
    const auto xForLevel = [&] (double db) { return hears.getX() + (float) ((juce::jlimit (minThresholdDb, maxThresholdDb, db) - minThresholdDb) / (maxThresholdDb - minThresholdDb)) * hears.getWidth(); };
    g.setColour (colour.withAlpha (0.8f));
    g.fillRoundedRectangle (hears.withRight (xForLevel (reading.levelDb)).reduced (1.5f), 2.0f);
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (2.0f, hears.getHeight() + 6.0f).withCentre ({ xForLevel (reading.thresholdDb), hears.getCentreY() }));

    // MOVING: 0 to the range, from the left.
    const auto rangeText = (b.dynamicRangeDb > 0.0 ? "+" : "") + juce::String (b.dynamicRangeDb, 1);
    const auto moving = bar (area, "MOVING", (reading.changeDb > 0.05f ? "+" : "") + juce::String (reading.changeDb, 1) + " of " + rangeText + " dB");
    const auto fraction = std::abs (b.dynamicRangeDb) > 0.01 ? juce::jlimit (0.0, 1.0, reading.changeDb / b.dynamicRangeDb) : 0.0;
    g.setColour (colour);
    g.fillRoundedRectangle (moving.withWidth (moving.getWidth() * (float) fraction).reduced (1.5f), 2.0f);
}

//==============================================================================
EqOverview::EqOverview (ParagonX1AudioProcessor& p) : processor (p)
{
    open.setColour (juce::TextButton::buttonColourId, ParagonColours::accentDim.withAlpha (0.6f));
    open.setColour (juce::TextButton::textColourOffId, ParagonColours::accent);
    open.setTooltip ("Open ECHELON's page: its display, bands and spectrum");
    open.onClick = [this] { if (onOpen) onOpen(); };
    addAndMakeVisible (open);
    startTimerHz (15);
}

void EqOverview::timerCallback()
{
    if (isShowing())
        repaint();
}

void EqOverview::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    open.setBounds (area.removeFromTop (26).removeFromRight (110));
    area.removeFromTop (8);
    graph = area.toFloat();
}

void EqOverview::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onOpen)
        onOpen();
}

void EqOverview::paint (juce::Graphics& g)
{
    DockStyle::drawPanel (g, getLocalBounds().toFloat());
    const auto settings = EqBands (processor).live();   // dynamic bands where they are now
    const auto numBands = std::count_if (settings.bands.begin(), settings.bands.end(), [] (const eq::Band& b) { return b.used; });

    auto header = getLocalBounds().reduced (16, 10).removeFromTop (26).toFloat();
    const auto nameFont = juce::FontOptions (14.0f, juce::Font::bold);
    const auto nameArea = header.removeFromLeft ((float) juce::GlyphArrangement::getStringWidthInt (nameFont, "ECHELON") + 10.0f);
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (nameArea.withTrimmedRight (10.0f)));
    g.setFont (nameFont);
    g.drawText ("ECHELON", nameArea, juce::Justification::centredLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ((numBands == 0 ? juce::String ("No bands yet") : juce::String (numBands) + (numBands == 1 ? " band" : " bands"))
                    + (settings.on ? juce::String() : juce::String ("  (stomp 4 is off)")),
                header, juce::Justification::centredLeft, false);

    // The curve, +-12 dB, 10 Hz to 30 kHz, as on the page.
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (graph, 5.0f);
    const auto xFor = [this] (double hz) { return graph.getX() + (float) (std::log (hz / eq::minFrequency) / std::log (eq::maxFrequency / eq::minFrequency)) * graph.getWidth(); };
    const auto yFor = [this] (double db) { return graph.getCentreY() - (float) juce::jlimit (-13.0, 13.0, db) / 12.0f * graph.getHeight() * 0.44f; };
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0)), graph.getX(), graph.getRight());

    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    std::array<eq::Design, eq::ParametricEq::numBands> designs;
    for (size_t i = 0; i < designs.size(); ++i)
        designs[i] = eq::design (settings.bands[i], sampleRate, settings.gainScale);
    juce::Path curve;
    for (auto x = graph.getX(); x <= graph.getRight(); x += 2.0f)
    {
        const auto hz = eq::minFrequency * std::pow (eq::maxFrequency / eq::minFrequency, (double) ((x - graph.getX()) / graph.getWidth()));
        double db = 0.0;
        for (size_t i = 0; i < designs.size(); ++i)
            if (settings.bands[i].isActive() && settings.bands[i].placement == eq::Placement::stereo)
                db += eq::responseDb (designs[i], hz, sampleRate);
        if (x <= graph.getX())
            curve.startNewSubPath (x, yFor (db));
        else
            curve.lineTo (x, yFor (db));
    }
    g.setColour (juce::Colours::white.withAlpha (settings.on ? 1.0f : 0.4f));
    g.strokePath (curve, juce::PathStrokeType (2.0f));
    for (int band = 0; band < (int) settings.bands.size(); ++band)
        if (const auto& b = settings.bands[(size_t) band]; b.used)
        {
            g.setColour (b.on ? EqBands::colourFor (b) : ParagonColours::textDim);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ xFor (b.frequency), yFor (eq::usesGain (b.shape) ? b.gainDb : 0.0) }));
        }
}
