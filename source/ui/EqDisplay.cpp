#include "EqDisplay.h"

#include "Parameters.h"
#include "dsp/eq/EqMatch.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr double lowestHz = eq::minFrequency, highestHz = eq::maxFrequency;
    constexpr float analyserFloorDb = -90.0f, analyserTopDb = 6.0f;

    bool isCut (eq::Shape shape) { return shape == eq::Shape::lowCut || shape == eq::Shape::highCut; }

    juce::String frequencyText (double hz)
    {
        return hz < 1000.0 ? juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz" : juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
    }

    juce::String noteText (double hz)
    {
        const auto note = PitchDetector::toNote (hz);
        const auto cents = juce::roundToInt (note.cents);
        return note.name + juce::String (note.octave) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents));
    }
}

//==============================================================================
EqBands::EqBands (ParagonX1AudioProcessor& p) : processor (p) {}

juce::RangedAudioParameter& EqBands::parameter (int band, const char* what) const
{
    return *processor.getParameters().getParameter (ParamIDs::eqBand (band, what));
}

float EqBands::get (int band, const char* what) const
{
    return processor.getParameters().getRawParameterValue (ParamIDs::eqBand (band, what))->load();
}

void EqBands::set (int band, const char* what, float plainValue) const
{
    begin (band, what);
    change (band, what, plainValue);
    end (band, what);
}

void EqBands::begin (int band, const char* what) const { parameter (band, what).beginChangeGesture(); }
void EqBands::end (int band, const char* what) const { parameter (band, what).endChangeGesture(); }

void EqBands::change (int band, const char* what, float plainValue) const
{
    auto& p = parameter (band, what);
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
}

eq::ParametricEq::Settings EqBands::live() const
{
    auto result = settings();
    for (int band = 0; band < (int) result.bands.size(); ++band)
        if (auto& b = result.bands[(size_t) band]; b.isDynamic())
            b.gainDb = juce::jlimit (-eq::maxGainDb, eq::maxGainDb, b.gainDb + processor.getEqDynamics (band).changeDb);
    return result;
}

int EqBands::create (eq::Shape shape, double frequency, double gainDb, double dynamicRangeDb) const
{
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        if (get (band, "Used") > 0.5f)
            continue;
        // Everything set before "Used", so the band comes in as it's meant.
        set (band, "Shape", (float) shape);
        set (band, "Freq", (float) frequency);
        set (band, "Gain", (float) gainDb);
        set (band, "Q", 1.0f);
        set (band, "Slope", (float) eq::defaultSlope);
        set (band, "Place", (float) eq::Placement::stereo);
        // A slot used before may have kept a dynamic band's settings.
        const eq::Band defaults;
        set (band, "Dyn", (float) dynamicRangeDb);
        set (band, "DynAuto", defaults.autoThreshold ? 1.0f : 0.0f);
        set (band, "Thresh", (float) defaults.thresholdDb);
        set (band, "Attack", (float) defaults.attack);
        set (band, "Release", (float) defaults.release);
        set (band, "On", 1.0f);
        set (band, "Used", 1.0f);
        return band;
    }
    return -1;
}

void EqBands::remove (int band) const
{
    // The slot is free again; bands aren't renumbered, so automation
    // written for the others still finds them (as in Pro-Q).
    set (band, "Used", 0.0f);
}

juce::Colour EqBands::colourFor (const eq::Band& b)
{
    const auto proportion = std::log (juce::jlimit (20.0, 20000.0, (double) b.frequency) / 20.0) / std::log (1000.0);
    return ParagonLookAndFeel::menuHighlight ({ 0.0f, 0.0f, 1.0f, 1.0f }).getColourAtPosition (proportion);
}

juce::Colour EqBands::colourFor (eq::Placement placement)
{
    switch (placement)
    {
        // In the menus' red to pink to orange family (0.16.1), apart from each other and the white whole.
        case eq::Placement::left:  return juce::Colour (0xffff3d5a);   // red
        case eq::Placement::right: return juce::Colour (0xffff8a3d);   // orange
        case eq::Placement::mid:   return juce::Colour (0xffff5fb0);   // pink
        case eq::Placement::side:  return juce::Colour (0xffffc04a);   // amber
        case eq::Placement::stereo: break;
    }
    return juce::Colours::white;   // the whole curve, white as on DOMINION's and COMP's pages
}

//==============================================================================
void SpectrumAnalyser::update (const InputTap& tap)
{
    constexpr float fallPerUpdate = 1.5f;   // dB each update (about 45 dB a second), as an analyser's release
    if (tap.getSampleRate() <= 0.0)
        return;
    sampleRate = tap.getSampleRate();

    const auto total = tap.getTotalWritten();
    if (total == lastTotal)
    {
        // No new audio (the DAW has stopped): let the spectrum fall away.
        for (auto& db : smoothedDb)
            db = std::max (-150.0f, db - fallPerUpdate);
        return;
    }
    lastTotal = total;

    std::fill (buffer.begin(), buffer.end(), 0.0f);
    tap.readLatest (buffer.data(), size);
    window.multiplyWithWindowingTable (buffer.data(), (size_t) size);
    fft.performFrequencyOnlyForwardTransform (buffer.data());

    // A sine at full scale reads 0 dB: the window (Hann) keeps half the energy.
    const auto scale = 4.0f / (float) size;
    for (size_t bin = 1; bin < smoothedDb.size(); ++bin)
    {
        const auto hz = (double) bin * sampleRate / size;
        const auto db = juce::Decibels::gainToDecibels (buffer[bin] * scale, -150.0f)
                      + (float) (4.5 * std::log2 (hz / 1000.0));
        // Rises part of the way each update (so noise reads as a level, not
        // grass), falls slowly.
        const auto previous = smoothedDb[bin];
        smoothedDb[bin] = db > previous ? previous + (db - previous) * 0.5f : std::max (db, previous - fallPerUpdate);
    }
}

float SpectrumAnalyser::levelDb (double hz, double toHz) const noexcept
{
    const auto binOf = [this] (double f) { return f * size / sampleRate; };
    const auto from = binOf (hz), to = binOf (toHz);
    const auto last = (double) smoothedDb.size() - 1.0;
    if (from >= last)
        return -150.0f;
    if (to - from < 1.0)
    {
        // Narrower than a bin: between the two nearest.
        const auto below = juce::jlimit (1.0, last, std::floor (from));
        const auto above = juce::jlimit (1.0, last, below + 1.0);
        const auto t = (float) juce::jlimit (0.0, 1.0, from - below);
        return smoothedDb[(size_t) below] + (smoothedDb[(size_t) above] - smoothedDb[(size_t) below]) * t;
    }
    // Wider than a bin (the highs): the average power of the bins there.
    double power = 0.0;
    int count = 0;
    for (auto bin = (size_t) juce::jmax (1.0, std::floor (from)); bin <= (size_t) juce::jmin (last, std::ceil (to)); ++bin, ++count)
        power += std::pow (10.0, smoothedDb[bin] / 10.0);
    return (float) (10.0 * std::log10 (std::max (power / std::max (count, 1), 1.0e-15)));
}

//==============================================================================
EqDisplay::EqDisplay (ParagonX1AudioProcessor& p) : bands (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("ECHELON display");
    setDescription ("Double-click to add a band; drag a dot to move it; mouse wheel for Q");
    shown = bands.settings();
    startTimerHz (30);
}

void EqDisplay::refresh()
{
    if (showInput)
        input.update (bands.processor.getEqInputTap());
    if (showOutput)
        output.update (bands.processor.getEqOutputTap());
    shown = bands.settings();
    for (int band = 0; band < (int) dynamics.size(); ++band)
        dynamics[(size_t) band] = bands.processor.getEqDynamics (band);

    // A band removed elsewhere (undo, a rig, the DAW) can't stay selected.
    if (selected >= 0 && ! shown.bands[(size_t) selected].used)
        select (-1);
    repaint();
}

void EqDisplay::select (int band)
{
    if (band == selected)
        return;
    selected = band;
    if (onSelect)
        onSelect (band);
    repaint();
}

void EqDisplay::setMatchPreview (std::vector<double> wantedDb, std::vector<double> proposedDb)
{
    matchWanted = std::move (wantedDb);
    matchProposed = std::move (proposedDb);
    repaint();
}

void EqDisplay::setRangeDb (double newRange)
{
    rangeDb = newRange;
    repaint();
}

void EqDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
}

float EqDisplay::xForFrequency (double hz) const noexcept
{
    return plot.getX() + (float) (std::log (hz / lowestHz) / std::log (highestHz / lowestHz)) * plot.getWidth();
}

double EqDisplay::frequencyForX (float x) const noexcept
{
    return juce::jlimit (lowestHz, highestHz, lowestHz * std::pow (highestHz / lowestHz, (double) ((x - plot.getX()) / plot.getWidth())));
}

float EqDisplay::yForDb (double db) const noexcept
{
    return plot.getCentreY() - (float) (db / rangeDb) * plot.getHeight() * 0.46f;
}

double EqDisplay::dbForY (float y) const noexcept
{
    return (double) ((plot.getCentreY() - y) / (plot.getHeight() * 0.46f)) * rangeDb;
}

juce::Point<float> EqDisplay::dotFor (const eq::Band& band) const noexcept
{
    const auto y = eq::usesGain (band.shape) ? yForDb (juce::jlimit (-rangeDb, rangeDb, band.gainDb)) : yForDb (0.0);
    return { xForFrequency (band.frequency), y };
}

juce::Point<float> EqDisplay::rangeHandleFor (const eq::Band& band) const noexcept
{
    return { xForFrequency (band.frequency), yForDb (juce::jlimit (-rangeDb, rangeDb, band.gainDb + band.dynamicRangeDb)) };
}

int EqDisplay::rangeHandleAt (juce::Point<float> position) const
{
    for (int band = 0; band < (int) shown.bands.size(); ++band)
        if (const auto& b = shown.bands[(size_t) band]; b.isDynamic() && rangeHandleFor (b).getDistanceFrom (position) < 9.0f)
            return band;
    return -1;
}

int EqDisplay::bandAt (juce::Point<float> position) const
{
    auto nearest = -1;
    auto best = 12.0f;   // pixels
    for (int band = 0; band < (int) shown.bands.size(); ++band)
        if (const auto& b = shown.bands[(size_t) band]; b.used)
            if (const auto distance = dotFor (b).getDistanceFrom (position); distance < best)
            {
                best = distance;
                nearest = band;
            }
    return nearest;
}

//==============================================================================
void EqDisplay::paintGrid (juce::Graphics& g)
{
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (const auto hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const auto x = xForFrequency (hz);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ParagonColours::textDim);
        g.drawText (hz < 1000.0 ? juce::String ((int) hz) : juce::String ((int) (hz / 1000.0)) + "k",
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 13.0f), juce::Justification::centred, false);
    }

    const auto step = rangeDb <= 3.0 ? 1.0 : rangeDb <= 6.0 ? 2.0 : rangeDb <= 12.0 ? 3.0 : 10.0;
    for (auto db = -rangeDb; db <= rangeDb + 0.001; db += step)
    {
        const auto y = yForDb (db);
        g.setColour (juce::Colours::white.withAlpha (std::abs (db) < 0.001 ? 0.16f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ParagonColours::accent.withAlpha (0.8f));
        g.drawText ((db > 0.0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 32.0f, y - 6.0f, 28.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }
}

void EqDisplay::paintSpectrum (juce::Graphics& g, const SpectrumAnalyser& analyser, bool filled)
{
    if (! analyser.hasSignal())
        return;
    const auto yFor = [this] (float db)
    {
        return plot.getBottom() - (juce::jlimit (analyserFloorDb, analyserTopDb, db) - analyserFloorDb)
                                  / (analyserTopDb - analyserFloorDb) * plot.getHeight();
    };

    juce::Path path;
    path.startNewSubPath (plot.getX(), yFor (analyser.levelDb (frequencyForX (plot.getX()), frequencyForX (plot.getX() + 2.0f))));
    for (auto x = plot.getX() + 2.0f; x <= plot.getRight(); x += 2.0f)
        path.lineTo (x, yFor (analyser.levelDb (frequencyForX (x), frequencyForX (x + 2.0f))));
    if (filled)
    {
        auto area = path;
        area.lineTo (plot.getRight(), plot.getBottom());
        area.lineTo (plot.getX(), plot.getBottom());
        area.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillPath (area);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.32f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }
}

void EqDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff060508));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    paintGrid (g);
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (2.0f).toNearestInt());

    if (showInput)
        paintSpectrum (g, input, true);
    if (showOutput)
        paintSpectrum (g, output, false);

    // Dynamic bands are drawn where their gain is now.
    const auto sampleRate = bands.processor.getSampleRate() > 0.0 ? bands.processor.getSampleRate() : 48000.0;
    auto now = shown;
    for (size_t i = 0; i < now.bands.size(); ++i)
        if (auto& b = now.bands[i]; b.isDynamic())
            b.gainDb = juce::jlimit (-eq::maxGainDb, eq::maxGainDb, b.gainDb + dynamics[i].changeDb);
    std::array<eq::Design, eq::ParametricEq::numBands> designs;
    for (size_t i = 0; i < designs.size(); ++i)
        designs[i] = eq::design (now.bands[i], sampleRate, now.gainScale);

    const auto curvePath = [&] (auto&& dbAt)
    {
        juce::Path path;
        const auto yAt = [&] (float x) { return yForDb (juce::jlimit (-rangeDb * 1.15, rangeDb * 1.15, (double) dbAt (frequencyForX (x)))); };
        path.startNewSubPath (plot.getX(), yAt (plot.getX()));
        for (auto x = plot.getX() + 1.5f; x <= plot.getRight() + 1.0f; x += 1.5f)
            path.lineTo (x, yAt (x));
        return path;
    };

    // Each band's own curve, shaded down to 0 dB.
    const auto zero = yForDb (0.0);
    for (int band = 0; band < (int) designs.size(); ++band)
    {
        const auto& b = shown.bands[(size_t) band];
        if (! b.isActive())
            continue;
        auto path = curvePath ([&] (double hz) { return eq::responseDb (designs[(size_t) band], hz, sampleRate); });
        auto area = path;
        area.lineTo (plot.getRight() + 1.0f, zero);
        area.lineTo (plot.getX(), zero);
        area.closeSubPath();
        const auto colour = EqBands::colourFor (shown.bands[(size_t) band]);
        g.setColour (colour.withAlpha (band == selected ? 0.22f : 0.08f));
        g.fillPath (area);
        g.setColour (colour.withAlpha (band == selected ? 0.8f : 0.35f));
        g.strokePath (path, juce::PathStrokeType (1.0f));

        if (b.isDynamic())
        {
            // Where the whole range takes it: dashed.
            auto full = b;
            full.gainDb = juce::jlimit (-eq::maxGainDb, eq::maxGainDb, b.gainDb + b.dynamicRangeDb);
            const auto fullDesign = eq::design (full, sampleRate, shown.gainScale);
            const auto fullPath = curvePath ([&] (double hz) { return eq::responseDb (fullDesign, hz, sampleRate); });
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, fullPath, dashes, 2);
            g.setColour (colour.withAlpha (band == selected ? 0.9f : 0.5f));
            g.fillPath (dashed);
        }
    }

    // The overall curve, and one for each channel (or mid, or side) that has bands of its own.
    const auto dimmed = ! shown.on;
    for (const auto placement : { eq::Placement::left, eq::Placement::right, eq::Placement::mid, eq::Placement::side, eq::Placement::stereo })
    {
        const auto hasBands = placement == eq::Placement::stereo
                           || std::any_of (shown.bands.begin(), shown.bands.end(), [placement] (const eq::Band& b) { return b.isActive() && b.placement == placement; });
        if (! hasBands)
            continue;
        const auto path = curvePath ([&] (double hz)
        {
            double total = 0.0;
            for (size_t i = 0; i < designs.size(); ++i)
                if (shown.bands[i].isActive() && (shown.bands[i].placement == eq::Placement::stereo || shown.bands[i].placement == placement))
                    total += eq::responseDb (designs[i], hz, sampleRate);
            return total;
        });
        const auto colour = EqBands::colourFor (placement).withAlpha (dimmed ? 0.35f : 1.0f);
        g.setColour (colour.withAlpha (0.25f * colour.getFloatAlpha()));
        g.strokePath (path, juce::PathStrokeType (placement == eq::Placement::stereo ? 6.0f : 4.0f));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (placement == eq::Placement::stereo ? 2.4f : 1.8f));
    }

    // EQ Match: the curve that would match, and the one the proposed bands give.
    if (! matchWanted.empty())
    {
        const auto& grid = eq::match::grid();
        const auto matchPath = [&] (const std::vector<double>& db)
        {
            juce::Path path;
            for (size_t i = 0; i < grid.size() && i < db.size(); ++i)
            {
                const juce::Point<float> point { xForFrequency (grid[i]), yForDb (juce::jlimit (-rangeDb * 1.15, rangeDb * 1.15, db[i])) };
                if (i == 0)
                    path.startNewSubPath (point);
                else
                    path.lineTo (point);
            }
            return path;
        };
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (matchPath (matchWanted), juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (! matchProposed.empty())
        {
            juce::Path dashed;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (2.2f).createDashedStroke (dashed, matchPath (matchProposed), dashes, 2);
            g.setColour (ParagonColours::accent);
            g.fillPath (dashed);
        }
    }

    // Dynamic bands: a line from the dot to the range handle, and a ring where the gain is now.
    for (int band = 0; band < (int) shown.bands.size(); ++band)
    {
        const auto& b = shown.bands[(size_t) band];
        if (! b.isDynamic())
            continue;
        const auto colour = EqBands::colourFor (shown.bands[(size_t) band]);
        const auto dot = dotFor (b), handle = rangeHandleFor (b);
        g.setColour (colour.withAlpha (0.55f));
        g.drawLine ({ dot, handle }, 2.0f);
        const auto pill = juce::Rectangle<float> (18.0f, 6.0f).withCentre (handle);
        g.setColour (colour);
        g.fillRoundedRectangle (pill, 3.0f);
        if (band == selected || band == hovered)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawRoundedRectangle (pill.expanded (1.0f), 3.5f, 1.0f);
        }
        if (dynamics[(size_t) band].active)
        {
            const auto nowY = yForDb (juce::jlimit (-rangeDb, rangeDb, b.gainDb + dynamics[(size_t) band].changeDb));
            g.setColour (juce::Colours::white);
            g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ dot.x, nowY }), 1.6f);
        }
    }

    // The dots, numbered as the bands are (numbers stay put when one is removed).
    for (int band = 0; band < (int) shown.bands.size(); ++band)
    {
        const auto& b = shown.bands[(size_t) band];
        if (! b.used)
            continue;
        const auto centre = dotFor (b);
        const auto colour = EqBands::colourFor (shown.bands[(size_t) band]);
        const auto dot = juce::Rectangle<float> (16.0f, 16.0f).withCentre (centre);
        if (band == selected || band == hovered)
        {
            g.setColour (colour.withAlpha (0.3f));
            g.fillEllipse (dot.expanded (5.0f));
        }
        g.setColour (b.on ? colour : juce::Colour (0xff3a3540));
        g.fillEllipse (dot);
        if (band == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (dot.expanded (1.5f), 1.5f);
        }
        g.setColour (b.on ? juce::Colour (0xff120d10) : ParagonColours::textDim);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (band + 1), dot, juce::Justification::centred, false);
    }

    // What's under the mouse: a band's settings, or the frequency (and note) there.
    const auto readoutBand = dragging >= 0 ? dragging : hovered;
    juce::String readout;
    juce::Point<float> anchor;
    if (readoutBand >= 0)
    {
        const auto& b = shown.bands[(size_t) readoutBand];
        readout << (readoutBand + 1) << "  " << eq::shapeNames[(int) b.shape] << "  " << frequencyText (b.frequency);
        if (eq::usesGain (b.shape))
            readout << "  " << (b.gainDb > 0.0 ? "+" : "") << juce::String (b.gainDb, 1) << " dB";
        if (b.isDynamic())
            readout << "  dynamic " << (b.dynamicRangeDb > 0.0 ? "+" : "") << juce::String (b.dynamicRangeDb, 1) << " dB";
        if (eq::usesQ (b.shape, b.slope))
            readout << "  Q " << juce::String (b.q, 2);
        if (eq::usesSlope (b.shape))
            readout << "  " << eq::slopeNames[eq::slopeFor (b.shape, b.slope)];
        if (b.placement != eq::Placement::stereo)
            readout << "  " << eq::placementNames[(int) b.placement];
        if (! b.on)
            readout << "  (off)";
        anchor = dotFor (b).translated (0.0f, -26.0f);
    }
    else if (plot.contains (hoverPoint))
    {
        const auto hz = frequencyForX (hoverPoint.x);
        readout << frequencyText (hz) << "  " << noteText (hz);
        anchor = { hoverPoint.x, plot.getBottom() - 14.0f };
    }
    if (readout.isNotEmpty())
    {
        const auto font = juce::FontOptions (11.0f, juce::Font::bold);
        const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, readout) + 14.0f;
        auto box = juce::Rectangle<float> (width, 18.0f).withCentre (anchor);
        box = box.constrainedWithin (plot);
        g.setColour (juce::Colour (0xe0141016));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (ParagonColours::text);
        g.setFont (font);
        g.drawText (readout, box, juce::Justification::centred, false);
    }

    if (matchWanted.empty() && std::none_of (shown.bands.begin(), shown.bands.end(), [] (const eq::Band& b) { return b.used; }))
    {
        g.setColour (ParagonColours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Double-click to add a band (Alt: a dynamic one). Drag a dot to move it; mouse wheel for Q; right-click for its menu.",
                    plot.withTrimmedTop (plot.getHeight() * 0.62f).withHeight (20.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
void EqDisplay::mouseMove (const juce::MouseEvent& event)
{
    hoverPoint = event.position;
    const auto onHandle = rangeHandleAt (event.position);
    const auto band = onHandle >= 0 ? onHandle : bandAt (event.position);
    if (band != hovered)
        hovered = band;
    repaint();
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    hoverPoint = {};
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    // A dynamic band's range handle (unless its dot is right there too).
    if (const auto handle = rangeHandleAt (event.position); handle >= 0 && bandAt (event.position) < 0 && ! event.mods.isPopupMenu())
    {
        select (handle);
        dragging = handle;
        draggingRange = true;
        dragStartRange = shown.bands[(size_t) handle].dynamicRangeDb;
        bands.begin (handle, "Dyn");
        return;
    }

    const auto band = bandAt (event.position);
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
    if (event.mods.isAltDown())
    {
        bands.set (band, "On", bands.get (band, "On") > 0.5f ? 0.0f : 1.0f);
        return;
    }

    const auto& b = shown.bands[(size_t) band];
    dragging = band;
    dragStartFrequency = b.frequency;
    dragStartGain = b.gainDb;
    dragStartQ = b.q;
    dragHasGain = eq::usesGain (b.shape);
    bands.begin (band, "Freq");
    bands.begin (band, dragHasGain ? "Gain" : "Q");
}

void EqDisplay::mouseDrag (const juce::MouseEvent& event)
{
    hoverPoint = event.position;
    if (dragging < 0)
        return;
    const auto fine = event.mods.isShiftDown() ? 0.1f : 1.0f;
    const auto dx = (float) event.getDistanceFromDragStartX() * fine;
    const auto dy = (float) event.getDistanceFromDragStartY() * fine;

    if (draggingRange)
    {
        bands.change (dragging, "Dyn", (float) juce::jlimit (-eq::dynamics::maxRangeDb, eq::dynamics::maxRangeDb,
                                                             dragStartRange - (double) dy / (plot.getHeight() * 0.46) * rangeDb));
        shown = bands.settings();
        repaint();
        return;
    }

    bands.change (dragging, "Freq", (float) frequencyForX (xForFrequency (dragStartFrequency) + dx));
    if (dragHasGain)
        bands.change (dragging, "Gain", (float) juce::jlimit (-eq::maxGainDb, eq::maxGainDb,
                                                              dragStartGain - (double) dy / (plot.getHeight() * 0.46) * rangeDb));
    else
        bands.change (dragging, "Q", (float) juce::jlimit (eq::minQ, eq::maxQ, dragStartQ * std::pow (2.0, (double) -dy / 60.0)));
    shown = bands.settings();
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    if (draggingRange)
    {
        bands.end (dragging, "Dyn");
        draggingRange = false;
        dragging = -1;
        return;
    }
    bands.end (dragging, "Freq");
    bands.end (dragging, dragHasGain ? "Gain" : "Q");
    dragging = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (bandAt (event.position) >= 0 || ! plot.contains (event.position))
        return;
    // Where you click decides the shape: the far ends make cuts.
    const auto hz = frequencyForX (event.position.x);
    const auto shape = hz < 30.0 ? eq::Shape::lowCut : hz > 15000.0 ? eq::Shape::highCut : eq::Shape::bell;
    const auto db = juce::jlimit (-eq::maxGainDb, eq::maxGainDb, dbForY (event.position.y));
    const auto band = [&]
    {
        // With Alt, a dynamic bell: flat until its part gets loud, then as
        // far as where you clicked (a 6 dB cut if that's about level).
        if (event.mods.isAltDown() && shape == eq::Shape::bell)
            return bands.create (shape, hz, 0.0, std::abs (db) < 1.0 ? -6.0 : db);
        return bands.create (shape, hz, shape == eq::Shape::bell ? db : 0.0);
    }();
    shown = bands.settings();
    if (band >= 0)
        select (band);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto band = hovered >= 0 ? hovered : selected;
    if (band < 0 || std::abs (wheel.deltaY) + std::abs (wheel.deltaX) < 1.0e-4f)
        return;
    const auto& b = shown.bands[(size_t) band];
    if (event.mods.isAltDown())
    {
        // The dynamic range (shapes with a gain). Some systems turn Alt+wheel sideways.
        if (eq::usesGain (b.shape))
        {
            const auto delta = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
            bands.set (band, "Dyn", (float) juce::jlimit (-eq::dynamics::maxRangeDb, eq::dynamics::maxRangeDb,
                                                          b.dynamicRangeDb + (double) delta * (event.mods.isShiftDown() ? 2.0 : 10.0)));
        }
    }
    else if (isCut (b.shape))
    {
        // Cuts: the slope, a step at a time.
        const auto step = wheel.deltaY > 0.0f ? 1 : -1;
        bands.set (band, "Slope", (float) juce::jlimit (eq::minSlope (b.shape), eq::numSlopes - 1, eq::slopeFor (b.shape, b.slope) + step));
    }
    else if (eq::usesQ (b.shape, b.slope))
    {
        const auto amount = event.mods.isShiftDown() ? 0.25 : 1.5;
        bands.set (band, "Q", (float) juce::jlimit (eq::minQ, eq::maxQ, b.q * std::exp ((double) wheel.deltaY * amount)));
    }
    shown = bands.settings();
    repaint();
}

bool EqDisplay::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected >= 0)
    {
        bands.remove (selected);
        select (-1);
        return true;
    }
    return false;
}

void EqDisplay::showMenu (int band)
{
    const auto& b = shown.bands[(size_t) band];
    juce::PopupMenu menu, shapes, slopes, placements;
    for (int i = 0; i < eq::shapeNames.size(); ++i)
        shapes.addItem (100 + i, eq::shapeNames[i], true, (int) b.shape == i);
    for (int i = eq::minSlope (b.shape); i < eq::numSlopes; ++i)
        slopes.addItem (200 + i, eq::slopeNames[i], true, eq::slopeFor (b.shape, b.slope) == i);
    for (int i = 0; i < eq::placementNames.size(); ++i)
        placements.addItem (300 + i, eq::placementNames[i], true, (int) b.placement == i);

    menu.addSectionHeader ("Band " + juce::String (band + 1));
    menu.addSubMenu ("Shape", shapes);
    menu.addSubMenu ("Slope", slopes, eq::usesSlope (b.shape));
    menu.addSubMenu ("Stereo placement", placements);
    menu.addSeparator();
    if (eq::usesGain (b.shape))
        menu.addItem (3, std::abs (b.dynamicRangeDb) >= 0.01 ? "Make static" : "Make dynamic");
    menu.addItem (1, b.on ? "Switch off" : "Switch on");
    menu.addItem (2, "Delete");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [this, band] (int chosen)
                        {
                            if (chosen >= 300)
                                bands.set (band, "Place", (float) (chosen - 300));
                            else if (chosen >= 200)
                                bands.set (band, "Slope", (float) (chosen - 200));
                            else if (chosen >= 100)
                                bands.set (band, "Shape", (float) (chosen - 100));
                            else if (chosen == 1)
                                bands.set (band, "On", bands.get (band, "On") > 0.5f ? 0.0f : 1.0f);
                            else if (chosen == 3)   // dynamic: a 6 dB cut when its part gets loud
                                bands.set (band, "Dyn", std::abs (bands.get (band, "Dyn")) >= 0.01f ? 0.0f : -6.0f);
                            else if (chosen == 2)
                            {
                                bands.remove (band);
                                select (-1);
                            }
                        });
}
