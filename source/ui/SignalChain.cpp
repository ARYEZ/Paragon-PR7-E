#include "SignalChain.h"

#include "ParagonLookAndFeel.h"

SignalChain::SignalChain (juce::AudioProcessorValueTreeState& parametersToUse, std::vector<Block> blocksToShow)
    : parameters (parametersToUse), blocks (std::move (blocksToShow)), lastOn (blocks.size(), false)
{
    // On/off can change from footswitches or DAW automation, so poll, and
    // read them now so the chain is right the moment the window opens.
    startTimerHz (10);
    timerCallback();
}

bool SignalChain::isOn (size_t index) const
{
    // A block with more than one switch (PITCH: "octOn,freezeOn") is on if any is.
    for (const auto& id : juce::StringArray::fromTokens (blocks[index].onParameterId, ",", {}))
        if (parameters.getRawParameterValue (id)->load() > 0.5f)
            return true;
    return false;
}

void SignalChain::timerCallback()
{
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        if (isOn (i) != lastOn[i])
        {
            lastOn[i] = isOn (i);
            repaint();
        }
    }
}

void SignalChain::setSelected (int blockIndex)
{
    selected = blockIndex;
    repaint();
}

void SignalChain::resized()
{
    auto area = getLocalBounds().toFloat().reduced (16.0f, 8.0f);
    area.removeFromTop (18.0f);   // "SIGNAL CHAIN" title

    guitarArea = area.removeFromLeft (70.0f);
    outputArea = area.removeFromRight (110.0f);

    // Blocks spread evenly with gaps for the dotted connectors, from the
    // left, never wider than nine across would make them: a short chain
    // (PR7-X's grows as its stomps are built) stays in proportion.
    const auto count = (float) blocks.size();
    const auto gap = 16.0f;
    const auto widthFor = [&] (float n) { return (area.getWidth() - gap * (n + 1.0f)) / n; };
    const auto blockWidth = juce::jmin (widthFor (count), widthFor (9.0f));
    blockAreas.clear();
    area.removeFromLeft (gap);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        blockAreas.push_back (area.removeFromLeft (blockWidth));
        area.removeFromLeft (gap);
    }
}

void SignalChain::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (ParagonColours::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SIGNAL CHAIN", bounds.reduced (16.0f, 6.0f).withHeight (14.0f), juce::Justification::centredLeft, false);

    Icons::draw (g, Icons::Type::guitar, guitarArea.reduced (14.0f, 6.0f), ParagonColours::textDim, 1.6f);

    // Dotted connectors between everything, glowing where signal is flowing
    // through a switched-on block.
    const auto drawConnector = [&] (float x1, float x2, float y, juce::Colour colour)
    {
        for (auto x = x1 + 6.0f; x < x2 - 4.0f; x += 7.0f)
        {
            g.setColour (colour);
            g.fillEllipse (x - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
    };

    const auto midY = blockAreas.empty() ? bounds.getCentreY() : blockAreas.front().getCentreY();
    auto previousRight = guitarArea.getRight();

    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& block = blocks[i];
        const auto area = blockAreas[i];
        const auto on = lastOn[i];
        const auto isSelected = (int) i == selected;

        drawConnector (previousRight, area.getX(), midY, (on ? block.colour : ParagonColours::glow).withAlpha (0.55f));
        previousRight = area.getRight();

        if (on)
        {
            // Glow behind a lit block.
            for (int ring = 3; ring >= 1; --ring)
            {
                g.setColour (block.colour.withAlpha (0.06f * (float) (4 - ring)));
                g.drawRoundedRectangle (area.expanded ((float) ring * 2.0f), 6.0f + (float) ring, 2.0f);
            }
        }

        g.setColour (isSelected ? juce::Colour (0xff1f1d22) : juce::Colour (0xff121114));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (on ? block.colour : block.colour.withAlpha (0.28f));
        g.drawRoundedRectangle (area, 6.0f, isSelected ? 2.0f : 1.2f);

        // Selected block: a bar under it, matching the edit panel above.
        if (isSelected)
        {
            g.setColour (block.colour);
            g.fillRoundedRectangle (area.withTop (area.getBottom() + 3.0f).withHeight (3.0f).reduced (area.getWidth() * 0.2f, 0.0f), 1.5f);
        }

        auto content = area.reduced (10.0f, 6.0f);
        Icons::draw (g, block.icon, content.removeFromLeft (content.getHeight()).reduced (2.0f),
                     on ? block.colour : block.colour.withAlpha (0.35f), 1.6f, on);
        content.removeFromLeft (6.0f);
        g.setColour (on ? ParagonColours::text : ParagonColours::textDim);
        // Eleven blocks share the strip: names a touch smaller, and the
        // longest (MODULATION) squeezed a little rather than cut off.
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawFittedText (block.name, content.toNearestInt(), juce::Justification::centredLeft, 2, 0.55f);
    }

    drawConnector (previousRight, outputArea.getX(), midY, ParagonColours::glow.withAlpha (0.55f));

    auto output = outputArea.reduced (6.0f, 6.0f);
    g.setColour (ParagonColours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("OUTPUT", output.removeFromLeft (56.0f), juce::Justification::centred, false);
    Icons::draw (g, Icons::Type::speaker, output.reduced (4.0f), ParagonColours::text, 1.6f);
}

int SignalChain::blockAt (juce::Point<int> position) const
{
    for (size_t i = 0; i < blockAreas.size(); ++i)
        if (blockAreas[i].contains (position.toFloat()))
            return (int) i;
    return -1;
}

void SignalChain::mouseUp (const juce::MouseEvent& event)
{
    if (const auto index = blockAt (event.getPosition()); index >= 0 && event.mouseWasClicked())
    {
        setSelected (index);
        if (onSelect)
            onSelect (index);
    }
}

void SignalChain::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto index = blockAt (event.getPosition());
    if (index < 0 || blocks[(size_t) index].onParameterId.isEmpty())
        return;

    // On: switch everything in it off. Off: switch its first switch on.
    const auto ids = juce::StringArray::fromTokens (blocks[(size_t) index].onParameterId, ",", {});
    const auto turnOff = isOn ((size_t) index);
    for (const auto& id : ids)
    {
        auto* parameter = parameters.getParameter (id);
        if (! turnOff && id != ids[0])
            continue;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (turnOff ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }
    timerCallback();
}
