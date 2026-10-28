#include "RackUnit.h"

#include "Finish.h"
#include "LedDisplay.h"
#include "ParagonLookAndFeel.h"

RackUnit::RackUnit (juce::AudioProcessorValueTreeState& parameters, Spec s) : state (parameters), spec (std::move (s))
{
    setTitle (spec.name + " rack unit");

    // POWER: the stomp's footswitch; its name, like the unit's, opens the big page.
    power = std::make_unique<Footswitch> (spec.number, "POWER", state, spec.onParameterIds);
    power->onSelect = [this] { if (spec.onEdit) spec.onEdit(); };
    power->setTooltip (spec.powerTooltip);
    addAndMakeVisible (*power);

    for (size_t i = 0; i < keys.size(); ++i)
    {
        auto& key = keys[i];
        key.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
        key.setTooltip (spec.keys[i].tooltip);
        key.onClick = [this, i] { if (spec.keys[i].press) spec.keys[i].press(); refresh(); };
        addAndMakeVisible (key);
    }
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.getProperties().set (ParagonLookAndFeel::pedalKnobProperty, true);
        knob.setPopupDisplayEnabled (true, true, nullptr);
        knobAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, spec.knobs[i].parameterId, knob));
        if (auto* p = state.getParameter (spec.knobs[i].parameterId))
        {
            knob.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            knob.setTooltip (p->getName (64));
        }
        knob.onValueChange = [this] { repaint(); };
        addAndMakeVisible (knob);
    }
    edit.getProperties().set (ParagonLookAndFeel::pedalButtonProperty, true);
    edit.setTooltip ("Open " + spec.name + "'s own page: everything it can do");
    edit.onClick = [this] { if (spec.onEdit) spec.onEdit(); };
    addAndMakeVisible (edit);

    startTimerHz (30);
}

RackUnit::~RackUnit() = default;

juce::Rectangle<float> RackUnit::at (float x, float y, float w, float h) const noexcept
{
    const auto sx = (float) getWidth() / referenceWidth, sy = (float) getHeight() / referenceHeight;
    return { x * sx, y * sy, w * sx, h * sy };
}

void RackUnit::resized()
{
    power->setBounds (at (30.0f, 4.0f, 172.0f, 124.0f).toNearestInt());
    for (size_t i = 0; i < keys.size(); ++i)
        keys[i].setBounds (at (874.0f + 60.0f * (float) i, 56.0f, 42.0f, 20.0f).toNearestInt());
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].setBounds (at (1064.0f + 82.0f * (float) i, 34.0f, 60.0f, 60.0f).toNearestInt());
    edit.setBounds (at (1232.0f, 28.0f, 70.0f, 22.0f).toNearestInt());
}

void RackUnit::mouseUp (const juce::MouseEvent& e)
{
    if (at (214.0f, 26.0f, 196.0f, 80.0f).contains (e.position) && spec.onEdit)
        spec.onEdit();
}

void RackUnit::timerCallback()
{
    // The ladder holds a peak and falls back about 15 dB a second, as a meter's lights do.
    if (spec.ladderReading)
        ladderHeld = juce::jmax (spec.ladderReading(), ladderHeld - 0.5f);
    for (size_t i = 0; i < keys.size(); ++i)
        keyLit[i] = spec.keys[i].isOn && spec.keys[i].isOn();
    repaint();
}

void RackUnit::paint (juce::Graphics& g)
{
    // For fonts and strokes: the smaller of the two stretches, so a shorter
    // unit (six rows since 0.22.0) keeps its lettering inside its height.
    const auto scale = juce::jmin ((float) getWidth() / referenceWidth, (float) getHeight() / referenceHeight);
    const auto ink = Finish::ink(), red = Finish::printedRed();

    // Rack screws at both ends: chrome, slotted, set into the panel.
    for (const auto& centre : { juce::Point<float> (14.0f, 22.0f), { 14.0f, 110.0f }, { 1318.0f, 22.0f }, { 1318.0f, 110.0f } })
    {
        const auto c = at (centre.x, centre.y, 0.0f, 0.0f).getPosition();
        const auto r = 5.5f * scale;
        const auto head = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4eef1), head.getX(), head.getY(),
                                                 juce::Colour (0xff5d4f57), head.getRight(), head.getBottom(), false));
        g.fillEllipse (head);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (head, 1.0f);
        const auto slot = juce::Line<float> (c.translated (-r * 0.7f, r * 0.45f), c.translated (r * 0.7f, -r * 0.45f));
        g.setColour (juce::Colour (0xff2a2226));
        g.drawLine (slot, 1.6f * scale);
    }

    // The name plate: the name in the red to pink to orange gradient, in the
    // badge's heavy italic; what it is, stamped; the series line in red.
    {
        const auto plate = at (214.0f, 26.0f, 196.0f, 80.0f);
        auto name = plate.withHeight (plate.getHeight() * 0.48f);
        auto font = juce::FontOptions (31.0f * scale, juce::Font::bold | juce::Font::italic);
        auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, spec.name);
        if (width > name.getWidth())   // a long name (CONSTRICTOR) set a little smaller to fit
        {
            font = font.withHeight (font.getHeight() * name.getWidth() / width);
            width = name.getWidth();
        }
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (name.withWidth (width)));
        g.setFont (font);
        g.drawText (spec.name, name, juce::Justification::bottomLeft, false);
        g.setFont (juce::FontOptions (10.5f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.kind, at (214.0f, 66.0f, 196.0f, 14.0f), juce::Justification::centredLeft, ink);
        g.setFont (juce::FontOptions (9.0f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.series, at (214.0f, 86.0f, 196.0f, 12.0f), juce::Justification::centredLeft, red);
    }

    // The ladder: the unit's LED bars, lit up to the reading.
    {
        g.setFont (juce::FontOptions (8.5f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.ladderTitle, at (418.0f, 18.0f, 48.0f, 12.0f), juce::Justification::centredLeft, red);
        for (size_t i = 0; i < spec.ladderMarks.size(); ++i)
        {
            const auto y = 34.0f + 17.0f * (float) i;
            ParagonLookAndFeel::drawNeonBar (g, at (418.0f, y, 18.0f, 7.0f), ladderHeld >= spec.ladderMarks[i]);
            ParagonLookAndFeel::drawStampedText (g, spec.ladderLabels[i], at (440.0f, y - 3.0f, 26.0f, 13.0f), juce::Justification::centredLeft, ink);
        }
    }

    // The screen: the PR7's own, with the unit's readout behind its glass.
    {
        const auto glass = LedDisplay::paintScreen (g, at (476.0f, 12.0f, 380.0f, 108.0f), 0.8f * scale);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (glass.toNearestInt());
            if (spec.paintReadout)
                spec.paintReadout (g, glass);
        }
        LedDisplay::paintGlassOverlay (g, glass, 0.8f * scale);
    }

    // The keys' LED bars and labels.
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto x = 874.0f + 60.0f * (float) i;
        ParagonLookAndFeel::drawNeonBar (g, at (x + 9.0f, 38.0f, 24.0f, 7.0f), keyLit[i]);
        g.setFont (juce::FontOptions (9.0f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.keys[i].label, at (x - 10.0f, 82.0f, 62.0f, 13.0f), juce::Justification::centred, ink);
    }

    // The knobs' names above and values below.
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto x = 1064.0f + 82.0f * (float) i;
        g.setFont (juce::FontOptions (9.5f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.knobs[i].label, at (x - 14.0f, 16.0f, 88.0f, 14.0f), juce::Justification::centred, ink);
        if (auto* p = state.getParameter (spec.knobs[i].parameterId))
        {
            g.setFont (juce::FontOptions (10.5f * scale, juce::Font::bold));
            ParagonLookAndFeel::drawStampedText (g, p->getCurrentValueAsText(), at (x - 14.0f, 98.0f, 88.0f, 14.0f), juce::Justification::centred, red);
        }
    }

    // Under EDIT: its preset or style, on a small screen of its own.
    {
        g.setFont (juce::FontOptions (8.5f * scale, juce::Font::bold));
        ParagonLookAndFeel::drawStampedText (g, spec.footTitle, at (1224.0f, 60.0f, 86.0f, 12.0f), juce::Justification::centred, red);
        const auto glass = LedDisplay::paintScreen (g, at (1222.0f, 74.0f, 90.0f, 34.0f), 0.4f * scale);
        g.setColour (LedDisplay::ledOrange);
        g.setFont (juce::FontOptions (glass.getHeight() * 0.5f));
        g.drawFittedText (spec.footText ? spec.footText().toUpperCase() : juce::String(), glass.reduced (4.0f * scale, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
        LedDisplay::paintGlassOverlay (g, glass, 0.4f * scale);
    }
}
