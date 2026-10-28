#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The PR7-X mark: heavy, wide, forward-slanted block letters with two "speed"
// cuts through their lower half, in the spirit of late-'90s floor
// processors. Drawn from vector outlines (not a font), so it's the same
// shape everywhere: the header and the unit's badge.
namespace Logo
{
    struct Style
    {
        juce::Colour letters;                              // P, R and "-E"
        juce::Colour seven;                                // the 7, usually the accent colour
        juce::Colour highlight = juce::Colours::transparentBlack;   // a light edge underneath, to look stamped into metal
        juce::Colour outline = juce::Colours::transparentBlack;     // an engraved outline
        float outlineWidth = 1.0f;
        bool glow = false;                                 // a soft neon glow behind the 7
    };

    // Width of the mark for a given letter height.
    float widthFor (float height) noexcept;

    // Draws the mark as large as fits in `area`, placed by `justification`.
    // Returns where it was drawn.
    juce::Rectangle<float> drawMark (juce::Graphics&, juce::Rectangle<float> area, juce::Justification justification, const Style&);
}
