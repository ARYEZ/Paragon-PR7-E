#include "StereoPage.h"

#include "DockPanels.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace
{
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
}

StereoPage::Knob::Knob (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : label ({}, text), attachment (state, id, slider)
{
    styleKnob (slider);
    styleLabel (label);
    slider.setTooltip (tooltip);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

StereoPage::Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, const juce::String& tooltip)
    : button (text), attachment (state, id, button)
{
    button.setTooltip (tooltip);
}

StereoPage::StereoPage (ParagonX1AudioProcessor& p)
    : processor (p),
      on (p.getParameters(), ParamIDs::stereoOn, "ON", "Switch BINARY on or off (the same as its POWER switch)"),
      monoOn (p.getParameters(), ParamIDs::stereoMonoOn, "MONO MAKER",
              "Folds everything below its frequency to the middle: tight, mono-safe lows (for vinyl, clubs and phones)"),
      widthOn (p.getParameters(), ParamIDs::stereoWidthOn, "WIDTH", "Narrows or widens the stereo image"),
      listenMono (p.getParameters(), ParamIDs::stereoListenMono, "LISTEN IN MONO",
                  "Hear the whole result in mono, as a phone or club speaker might play it (for checking; not saved in rigs)"),
      monoFrequency (p.getParameters(), ParamIDs::stereoMonoFreq, "MONO BELOW", "Below this, left and right become one (100 to 150 Hz is usual)"),
      width (p.getParameters(), ParamIDs::stereoWidth, "WIDTH",
             "0% mono, 100% as it was, 200% twice as wide. Only the sides change, so the mono sum stays the same")
{
    back.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
    back.setColour (juce::TextButton::textColourOffId, ParagonColours::text);
    back.onClick = [this] { if (onClose) onClose(); };
    for (auto* component : std::initializer_list<juce::Component*> { &on.button, &monoOn.button, &widthOn.button, &listenMono.button, &back })
        addAndMakeVisible (component);
    for (auto* knob : { &monoFrequency, &width })
    {
        addAndMakeVisible (knob->slider);
        addAndMakeVisible (knob->label);
    }
    startTimerHz (30);
}

void StereoPage::timerCallback()
{
    if (! isVisible())   // only its page reads the scope
        return;
    haveScope = processor.readImage (left.data(), right.data(), scopeSamples);
    const auto correlation = processor.getStereoCorrelation();
    shownCorrelation += (correlation - shownCorrelation) * 0.3f;
    lowestCorrelation = juce::jmin (lowestCorrelation + 0.002f, correlation);   // the lowest lately, creeping back
    repaint (scopeArea);
    repaint (meterArea);
}

void StereoPage::resized()
{
    auto area = getLocalBounds().reduced (12);
    topBar = area.removeFromTop (64);
    area.removeFromTop (8);
    controlsArea = area.removeFromBottom (150);
    area.removeFromBottom (8);
    meterArea = area.removeFromRight (360);
    area.removeFromRight (8);
    scopeArea = area;

    auto bar = topBar.reduced (14, 8);
    bar.removeFromLeft (200);
    on.button.setBounds (bar.removeFromLeft (60).withSizeKeepingCentre (60, 26));
    back.setBounds (bar.removeFromRight (140).withSizeKeepingCentre (140, 30));

    auto row = controlsArea.reduced (12, 8);
    row.removeFromTop (16);
    const auto section = [&row] (int sectionWidth)
    {
        auto s = row.removeFromLeft (sectionWidth);
        row.removeFromLeft (24);
        return s;
    };
    auto mono = section (260);
    monoOn.button.setBounds (mono.removeFromLeft (130).withSizeKeepingCentre (126, 26));
    monoFrequency.label.setBounds (mono.removeFromTop (14));
    monoFrequency.slider.setBounds (mono);
    auto wide = section (260);
    widthOn.button.setBounds (wide.removeFromLeft (130).withSizeKeepingCentre (126, 26));
    width.label.setBounds (wide.removeFromTop (14));
    width.slider.setBounds (wide);
    listenMono.button.setBounds (section (180).withSizeKeepingCentre (170, 26));
}

void StereoPage::paint (juce::Graphics& g)
{
    g.fillAll (ParagonColours::background);
    for (auto area : { topBar, scopeArea, meterArea, controlsArea })
        DockStyle::drawPanel (g, area.toFloat());

    // The name in the red to pink to orange gradient.
    auto title = topBar.reduced (14, 8).removeFromLeft (200);
    const auto titleFont = juce::FontOptions (19.0f, juce::Font::bold);
    const auto titleArea = title.removeFromTop (title.getHeight() / 2 + 6);
    const auto titleWidth = (float) juce::GlyphArrangement::getStringWidthInt (titleFont, "BINARY");
    g.setGradientFill (ParagonLookAndFeel::menuHighlight (titleArea.toFloat().withWidth (titleWidth)));
    g.setFont (titleFont);
    g.drawText ("BINARY", titleArea, juce::Justification::bottomLeft, false);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("Stereo image and mono maker", title, juce::Justification::topLeft, false);

    {
        auto names = controlsArea.reduced (12, 8).removeFromTop (14);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("LOWS", names.removeFromLeft (260), juce::Justification::centredLeft, false);
        names.removeFromLeft (24);
        g.drawText ("IMAGE", names.removeFromLeft (260), juce::Justification::centredLeft, false);
        names.removeFromLeft (24);
        g.drawText ("CHECK", names, juce::Justification::centredLeft, false);
    }

    // The scope: a half disc, mono straight up, left and right on the diagonals.
    {
        auto inner = scopeArea.toFloat().reduced (16.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("IMAGE (AFTER BINARY)", inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        const auto radius = juce::jmin (inner.getWidth() * 0.5f, inner.getHeight() - 24.0f);
        const auto centre = juce::Point<float> (inner.getCentreX(), inner.getY() + 10.0f + radius);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        g.drawLine (centre.x - radius, centre.y, centre.x + radius, centre.y);
        const auto diagonal = radius * std::sqrt (0.5f);
        g.drawLine (centre.x, centre.y, centre.x, centre.y - radius);
        g.drawLine (centre.x, centre.y, centre.x - diagonal, centre.y - diagonal);
        g.drawLine (centre.x, centre.y, centre.x + diagonal, centre.y - diagonal);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("MONO", juce::Rectangle<float> (centre.x - 30.0f, centre.y - radius - 14.0f, 60.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("L", juce::Rectangle<float> (centre.x - diagonal - 20.0f, centre.y - diagonal - 16.0f, 16.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (centre.x + diagonal + 4.0f, centre.y - diagonal - 16.0f, 16.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("OUT OF PHASE", juce::Rectangle<float> (centre.x - radius, centre.y + 4.0f, 100.0f, 12.0f), juce::Justification::centredLeft, false);

        if (haveScope)
        {
            // Scaled to the loudest moment shown, so quiet music still fills it.
            float peak = 1.0e-4f;
            for (int i = 0; i < scopeSamples; ++i)
                peak = juce::jmax (peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]));
            const auto scale = radius / (peak * std::sqrt (2.0f));
            const auto gradient = ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f });
            for (int i = 0; i < scopeSamples; ++i)
            {
                const auto l = left[(size_t) i], r = right[(size_t) i];
                auto x = (r - l) * scale, y = (l + r) * scale;
                if (y < 0.0f)   // fold the lower half up, as a half-disc scope does
                {
                    x = -x;
                    y = -y;
                }
                g.setColour (gradient.getColourAtPosition ((double) i / scopeSamples).withAlpha (0.85f));
                g.fillRect (centre.x + x - 1.1f, centre.y - y - 1.1f, 2.2f, 2.2f);
            }
        }
    }

    // The correlation meter, and what it means.
    {
        auto inner = meterArea.toFloat().reduced (16.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("CORRELATION", inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        inner.removeFromTop (10.0f);
        g.setColour (ParagonColours::accent);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText ((shownCorrelation >= 0.0f ? "+" : "") + juce::String (shownCorrelation, 2), inner.removeFromTop (36.0f), juce::Justification::centredLeft, false);
        inner.removeFromTop (8.0f);

        auto bar = inner.removeFromTop (18.0f);
        const auto xFor = [&bar] (float c) { return bar.getX() + (juce::jlimit (-1.0f, 1.0f, c) + 1.0f) * 0.5f * bar.getWidth(); };
        g.setColour (juce::Colour (0xff09070a));
        g.fillRoundedRectangle (bar, 3.0f);
        // Under 0 the sides fight (red); the more positive, the more mono.
        g.setColour (juce::Colour (0xffe8223f).withAlpha (0.25f));
        g.fillRect (bar.withRight (xFor (0.0f)));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (bar));
        const auto from = xFor (0.0f), to = xFor (shownCorrelation);
        g.fillRect (juce::Rectangle<float> (juce::jmin (from, to), bar.getY() + 3.0f, std::abs (to - from), bar.getHeight() - 6.0f));
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (xFor (lowestCorrelation) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        auto scale = inner.removeFromTop (14.0f);
        g.drawText ("-1", scale, juce::Justification::centredLeft, false);
        g.drawText ("0", scale, juce::Justification::centred, false);
        g.drawText ("+1", scale, juce::Justification::centredRight, false);

        inner.removeFromTop (14.0f);
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText ("+1: mono, the same on both sides.\n"
                          "0 to +1: a healthy stereo mix; most music sits here.\n"
                          "Around 0: very wide, with little in common.\n"
                          "Under 0: left and right working against each other: it will sound thin, or lose parts, in mono. "
                          "The white mark holds the lowest reading lately.",
                          inner.toNearestInt(), juce::Justification::topLeft, 10, 1.0f);
    }
}
