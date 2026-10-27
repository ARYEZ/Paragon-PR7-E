#include "RackSpecs.h"

#include "EqDisplay.h"
#include "LedDisplay.h"
#include "Parameters.h"
#include "ParagonLookAndFeel.h"
#include "dsp/eq/EqDesign.h"

namespace
{
    const auto orange = LedDisplay::ledOrange;

    juce::RangedAudioParameter& parameterOf (ParagonX1AudioProcessor& p, const char* id)
    {
        return *p.getParameters().getParameter (id);
    }

    float valueOf (ParagonX1AudioProcessor& p, const char* id)
    {
        return p.getParameters().getRawParameterValue (id)->load();
    }

    juce::String textOf (ParagonX1AudioProcessor& p, const char* id)
    {
        return parameterOf (p, id).getCurrentValueAsText();
    }

    // Sets a parameter as one gesture (one undo step).
    void setValue (ParagonX1AudioProcessor& p, const char* id, float plain)
    {
        auto& parameter = parameterOf (p, id);
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost (parameter.convertTo0to1 (plain));
        parameter.endChangeGesture();
    }

    void toggle (ParagonX1AudioProcessor& p, const char* id)
    {
        setValue (p, id, valueOf (p, id) > 0.5f ? 0.0f : 1.0f);
    }

    // A number in the screen's seven-segment digits ("-4.2", "5.0"), `h`
    // high, from `topLeft`; returns where it ends.
    float drawNumber (juce::Graphics& g, const juce::String& text, juce::Point<float> topLeft, float h)
    {
        auto x = topLeft.x;
        const auto w = h * 0.62f;
        for (auto c : text)
        {
            if (c == '.')
            {
                g.setColour (orange);
                g.fillRect (juce::Rectangle<float> (h * 0.12f, h * 0.12f).withPosition (x - h * 0.02f, topLeft.y + h * 0.86f));
                x += h * 0.16f;
                continue;
            }
            LedDisplay::drawDigit (g, c, { x, topLeft.y, w, h }, orange);
            x += w + h * 0.08f;
        }
        return x;
    }

    void line (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size)
    {
        g.setColour (orange);
        g.setFont (juce::FontOptions (size));
        g.drawText (text, area, juce::Justification::centredLeft, false);
    }

    // The right-hand column of a readout: lines of text from the top.
    void column (juce::Graphics& g, juce::Rectangle<float> area, const juce::StringArray& lines, float size)
    {
        for (const auto& text : lines)
            line (g, text, area.removeFromTop (size * 1.55f), size);
    }

    // A bar across the bottom of a readout, filled in the menus' gradient to `fraction`.
    void bar (juce::Graphics& g, juce::Rectangle<float> area, float fraction)
    {
        g.setColour (orange.withAlpha (0.15f));
        g.fillRect (area);
        const auto filled = area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, fraction));
        g.setGradientFill (ParagonLookAndFeel::menuHighlight (area));
        g.fillRect (filled);
    }

    float xForFrequency (juce::Rectangle<float> area, double hz)
    {
        return area.getX() + (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)) * area.getWidth();
    }
}

std::array<RackUnit::Spec, 4> makeRackSpecs (ParagonX1AudioProcessor& p, RackActions actions)
{
    using namespace ParamIDs;
    std::array<RackUnit::Spec, 4> specs;

    //==========================================================================
    // ECHELON
    {
        auto& s = specs[0];
        s.number = 1;
        s.name = "ECHELON";
        s.kind = "PARAMETRIC EQUALIZER";
        s.series = "PR7-E SERIES  24 BANDS";
        s.onParameterIds = { eqOn };
        s.powerTooltip = "ECHELON on or off. Click POWER (the word) or EDIT for its page";
        s.keys[0] = { "AUTO GAIN", "Keeps the level the same as you EQ, so louder doesn't fool you",
                      [&p] { return valueOf (p, eqAutoGain) > 0.5f; }, [&p] { toggle (p, eqAutoGain); } };
        s.keys[1] = { "ANALYSER", "The spectrum in and out, on ECHELON's page",
                      [actions] { return actions.analyserOn && actions.analyserOn(); },
                      [actions] { if (actions.setAnalyser) actions.setAnalyser (! actions.analyserOn()); } };
        s.keys[2] = { "MATCH", "Opens MATCH on ECHELON's page: make your track's tone like a reference's",
                      [] { return false; }, [actions] { if (actions.openMatch) actions.openMatch(); } };
        s.knobs = { RackUnit::Knob { eqOutput, "OUTPUT" }, RackUnit::Knob { eqGainScale, "GAIN SCALE" } };
        s.ladderTitle = "DYN";
        s.ladderMarks = { 12.0f, 6.0f, 3.0f, 1.0f, 0.5f };
        s.ladderLabels = { "12", "6", "3", "1", ".5" };
        s.ladderReading = [&p]
        {
            // How far the dynamic bands are moving now.
            const auto settings = p.getEqSettings();
            float most = -100.0f;
            for (int band = 0; band < eq::ParametricEq::numBands; ++band)
                if (settings.bands[(size_t) band].isActive() && settings.bands[(size_t) band].isDynamic())
                    most = juce::jmax (most, std::abs (p.getEqDynamics (band).changeDb));
            return most;
        };
        s.paintReadout = [&p] (juce::Graphics& g, juce::Rectangle<float> glass)
        {
            auto content = glass.reduced (glass.getWidth() * 0.035f, glass.getHeight() * 0.12f);
            auto graph = content.removeFromLeft (content.getWidth() * 0.62f);
            const auto settings = EqBands (p).live();
            const auto rate = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
            std::array<eq::Design, eq::ParametricEq::numBands> designs;
            for (size_t i = 0; i < designs.size(); ++i)
                designs[i] = eq::design (settings.bands[i], rate, settings.gainScale);
            const auto yFor = [&graph] (double db) { return graph.getCentreY() - (float) juce::jlimit (-13.0, 13.0, db) / 12.0f * graph.getHeight() * 0.46f; };

            g.setColour (orange.withAlpha (0.14f));
            g.drawHorizontalLine (juce::roundToInt (graph.getCentreY()), graph.getX(), graph.getRight());
            for (const auto hz : { 100.0, 1000.0, 10000.0 })
                g.drawVerticalLine (juce::roundToInt (xForFrequency (graph, hz)), graph.getY(), graph.getBottom());

            juce::Path curve;
            for (auto x = graph.getX(); x <= graph.getRight(); x += 1.5f)
            {
                const auto hz = 20.0 * std::pow (1000.0, (double) ((x - graph.getX()) / graph.getWidth()));
                double db = 0.0;
                for (size_t i = 0; i < designs.size(); ++i)
                    if (settings.bands[i].isActive() && settings.bands[i].placement == eq::Placement::stereo)
                        db += eq::responseDb (designs[i], hz, rate);
                if (x <= graph.getX())
                    curve.startNewSubPath (x, yFor (db));
                else
                    curve.lineTo (x, yFor (db));
            }
            g.setColour (juce::Colours::white.withAlpha (settings.on ? 0.95f : 0.4f));
            g.strokePath (curve, juce::PathStrokeType (1.6f));
            int count = 0;
            for (const auto& b : settings.bands)
                if (b.used)
                {
                    ++count;
                    g.setColour (b.on ? EqBands::colourFor (b) : orange.withAlpha (0.3f));
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xForFrequency (graph, b.frequency),
                                                                                   yFor (eq::usesGain (b.shape) ? b.gainDb : 0.0) }));
                }

            content.removeFromLeft (content.getWidth() * 0.08f);
            const auto h = content.getHeight() * 0.36f;
            const auto end = drawNumber (g, juce::String (count), content.getTopLeft(), h);
            line (g, "BANDS", { end + 4.0f, content.getY() + h * 0.45f, content.getRight() - end, h * 0.55f }, h * 0.32f);
            column (g, content.withTrimmedTop (h + h * 0.25f),
                    { "OUT " + textOf (p, eqOutput), "SCALE " + textOf (p, eqGainScale) }, h * 0.33f);
        };
        s.footTitle = "AUTO GAIN";
        s.footText = [&p]
        {
            if (valueOf (p, eqAutoGain) < 0.5f)
                return juce::String ("OFF");
            const auto db = p.getEqAutoGainDb();
            return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
        };
        s.onEdit = [actions] { actions.openPage (0); };
    }

    //==========================================================================
    // DOMINION
    {
        auto& s = specs[1];
        s.number = 2;
        s.name = "DOMINION";
        s.kind = "RESONANCE SUPPRESSOR";
        s.series = "PR7-E SERIES  SOFT / HARD";
        s.onParameterIds = { domOn };
        s.powerTooltip = "DOMINION on or off. Click POWER (the word) or EDIT for its page";
        s.keys[0] = { "HARD", "HARD: turns down what's loud, at any frequency; off (SOFT): what sticks out of the sound around it",
                      [&p] { return valueOf (p, domMode) > 0.5f; }, [&p] { toggle (p, domMode); } };
        s.keys[1] = { "M/S", "Works on the middle and sides (lit) or on left and right",
                      [&p] { return valueOf (p, domStereo) > 0.5f; }, [&p] { toggle (p, domStereo); } };
        s.keys[2] = { "DELTA", "Hear only what it takes away",
                      [&p] { return valueOf (p, domDelta) > 0.5f; }, [&p] { toggle (p, domDelta); } };
        s.knobs = { RackUnit::Knob { domDepth, "DEPTH" }, RackUnit::Knob { domDetail, "DETAIL" } };
        s.ladderTitle = "CUT";
        s.ladderMarks = { 12.0f, 6.0f, 3.0f, 1.0f, 0.5f };
        s.ladderLabels = { "12", "6", "3", "1", ".5" };
        const auto deepest = [&p]
        {
            const auto& d = p.getDominion();
            float most = 0.0f;
            for (int f = 0; f < d.getNumFilters(); ++f)
                most = juce::jmax (most, d.getReductionDb (0, f), d.getReductionDb (1, f));
            return most;
        };
        s.ladderReading = [&p, deepest] { return valueOf (p, domOn) > 0.5f ? deepest() : -100.0f; };
        s.paintReadout = [&p, deepest] (juce::Graphics& g, juce::Rectangle<float> glass)
        {
            auto content = glass.reduced (glass.getWidth() * 0.035f, glass.getHeight() * 0.12f);
            auto graph = content.removeFromLeft (content.getWidth() * 0.62f);
            const auto& d = p.getDominion();
            const auto top = graph.getY() + graph.getHeight() * 0.25f;
            const auto depthFor = [&] (float db) { return top + juce::jlimit (0.0f, 12.0f, db) / 12.0f * (graph.getBottom() - top); };
            juce::Path cut;
            cut.startNewSubPath (graph.getX(), top);
            for (int f = 0; f < d.getNumFilters(); ++f)
                cut.lineTo (xForFrequency (graph, d.getFrequency (f)), depthFor (juce::jmax (d.getReductionDb (0, f), d.getReductionDb (1, f))));
            cut.lineTo (graph.getRight(), top);
            cut.closeSubPath();
            g.setGradientFill (ParagonLookAndFeel::menuHighlight (graph));
            g.fillPath (cut);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawHorizontalLine (juce::roundToInt (top), graph.getX(), graph.getRight());

            content.removeFromLeft (content.getWidth() * 0.08f);
            const auto h = content.getHeight() * 0.36f;
            drawNumber (g, juce::String (valueOf (p, domDepth), 1), content.getTopLeft(), h);
            column (g, content.withTrimmedTop (h + h * 0.25f),
                    { "DEPTH   " + textOf (p, domMode).toUpperCase() + " MODE", "CUT -" + juce::String (deepest(), 1) + " dB" }, h * 0.33f);
        };
        s.footTitle = "PRESET";
        s.footText = [&p] { return p.getDominionPresetName() + (p.isDominionPresetModified() ? " *" : ""); };   // * as on its page: changed since
        s.onEdit = [actions] { actions.openPage (1); };
    }

    //==========================================================================
    // CONSTRICTOR
    {
        auto& s = specs[2];
        s.number = 3;
        s.name = "CONSTRICTOR";
        s.kind = "COMPRESSOR  6 BANDS";
        s.series = "PR7-E SERIES  VCA OPTO FET";
        s.onParameterIds = { compOn };
        s.powerTooltip = "CONSTRICTOR on or off. Click POWER (the word) or EDIT for its page";
        const auto character = [&p] (int which) { return [&p, which] { return juce::roundToInt (valueOf (p, compCharacter)) == which; }; };
        const auto choose = [&p] (int which) { return [&p, which] { setValue (p, compCharacter, (float) which); }; };
        s.keys[0] = { "VCA", "The full band's character: clean and precise", character (0), choose (0) };
        s.keys[1] = { "OPTO", "The full band's character: slower and smooth", character (1), choose (1) };
        s.keys[2] = { "FET", "The full band's character: fast, with a little grit", character (2), choose (2) };
        s.knobs = { RackUnit::Knob { compThreshold, "THRESHOLD" }, RackUnit::Knob { compRatio, "RATIO" } };
        s.ladderTitle = "GR";
        s.ladderMarks = { 20.0f, 10.0f, 6.0f, 3.0f, 1.0f };
        s.ladderLabels = { "20", "10", "6", "3", "1" };
        const auto reduction = [&p]
        {
            // The full band's gain reduction, or the deepest a band is turning its part down.
            auto most = juce::jmax (p.getCompressorReductionDb (0), p.getCompressorReductionDb (1));
            const auto bands = p.getMultibandSettings();
            for (int band = 0; band < effects::multiband::maxBands; ++band)
                if (bands.bands[(size_t) band].used)
                    most = juce::jmax (most, -p.getMultibandReading (band).changeDb);
            return most;
        };
        s.ladderReading = [&p, reduction] { return valueOf (p, compOn) > 0.5f ? reduction() : -100.0f; };
        s.paintReadout = [&p, reduction] (juce::Graphics& g, juce::Rectangle<float> glass)
        {
            auto content = glass.reduced (glass.getWidth() * 0.045f, glass.getHeight() * 0.14f);
            auto left = content.removeFromLeft (content.getWidth() * 0.55f);
            const auto gr = valueOf (p, compOn) > 0.5f ? reduction() : 0.0f;
            const auto h = left.getHeight() * 0.56f;
            const auto end = drawNumber (g, "-" + juce::String (gr, 1), left.getTopLeft(), h);
            line (g, "dB GR", { end + 6.0f, left.getY(), left.getRight() - end, h * 0.5f }, h * 0.24f);
            bar (g, left.withTop (left.getBottom() - left.getHeight() * 0.14f).withTrimmedRight (left.getWidth() * 0.08f), gr / 12.0f);

            int bands = 0;
            for (const auto& b : p.getMultibandSettings().bands)
                bands += b.used ? 1 : 0;
            column (g, content, { "THRESH " + textOf (p, compThreshold), "RATIO " + textOf (p, compRatio),
                                  textOf (p, compCharacter).toUpperCase() + "  " + juce::String (bands) + (bands == 1 ? " BAND" : " BANDS"),
                                  juce::String ("LOOK-AHEAD ") + (valueOf (p, mbLookahead) > 0.5f ? "ON" : "OFF") },
                    content.getHeight() * 0.16f);
        };
        s.footTitle = "PRESET";
        s.footText = [&p] { return p.getCompPresetName() + (p.isCompPresetModified() ? " *" : ""); };
        s.onEdit = [actions] { actions.openPage (2); };
    }

    //==========================================================================
    // LIMITER
    {
        auto& s = specs[3];
        s.number = 4;
        s.name = "LIMITER";
        s.kind = "TRUE PEAK LIMITER";
        s.series = "PR7-E SERIES  LOUDNESS";
        s.onParameterIds = { limitOn };
        s.powerTooltip = "The LIMITER on or off. Click POWER (the word) or EDIT for its page";
        s.keys[0] = { "TRUE PEAK", "Keeps the peaks between the samples under the ceiling too",
                      [&p] { return valueOf (p, limitTruePeak) > 0.5f; }, [&p] { toggle (p, limitTruePeak); } };
        s.keys[1] = { "UNITY", "Hear it at the level it came in",
                      [&p] { return valueOf (p, limitUnity) > 0.5f; }, [&p] { toggle (p, limitUnity); } };
        s.keys[2] = { "DELTA", "Hear only what it takes away",
                      [&p] { return valueOf (p, limitDelta) > 0.5f; }, [&p] { toggle (p, limitDelta); } };
        s.knobs = { RackUnit::Knob { limitGain, "GAIN" }, RackUnit::Knob { limitCeiling, "OUTPUT" } };
        s.ladderTitle = "OUT";
        s.ladderMarks = { 0.0f, -3.0f, -6.0f, -12.0f, -24.0f };
        s.ladderLabels = { "0", "-3", "-6", "-12", "-24" };
        s.ladderReading = [&p] { return valueOf (p, limitOn) > 0.5f ? p.getLimiter().getOutputPeakDb() : -100.0f; };
        auto heldPeak = std::make_shared<std::pair<float, double>> (-100.0f, 0.0);   // the true peak shown, and when it was set
        s.paintReadout = [&p, heldPeak] (juce::Graphics& g, juce::Rectangle<float> glass)
        {
            auto content = glass.reduced (glass.getWidth() * 0.045f, glass.getHeight() * 0.12f);
            auto& limiter = p.getLimiter();
            const auto on = valueOf (p, limitOn) > 0.5f;
            // The true peak, held for two seconds so it can be read.
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (const auto peak = on ? limiter.getOutputTruePeakDb() : -100.0f; peak >= heldPeak->first || now - heldPeak->second > 2000.0)
                *heldPeak = { peak, now };
            const auto integrated = limiter.getLoudness().getIntegrated();

            const auto h = content.getHeight() * 0.4f;
            const auto label = content.getHeight() * 0.13f;
            auto first = content.removeFromLeft (content.getWidth() * 0.36f);
            line (g, "TRUE PEAK", first.withHeight (label * 1.4f), label);
            drawNumber (g, heldPeak->first > -99.0f ? juce::String (heldPeak->first, 1) : juce::String ("--"), first.getTopLeft().translated (0.0f, label * 1.5f), h);
            auto second = content.removeFromLeft (content.getWidth() * 0.6f);
            line (g, "INTEGRATED", second.withHeight (label * 1.4f), label);
            drawNumber (g, integrated > effects::LoudnessMeter::silent + 1.0f ? juce::String (integrated, 1) : juce::String ("--"),
                        second.getTopLeft().translated (0.0f, label * 1.5f), h);
            column (g, content, { textOf (p, limitMode).toUpperCase(), "LOOK " + textOf (p, limitLookahead), "OVERSAMPLE " + textOf (p, limitOversampling).toUpperCase() },
                    label);
            const auto reduction = on ? juce::jmax (limiter.getReductionDb (0), limiter.getReductionDb (1)) : 0.0f;
            bar (g, glass.reduced (glass.getWidth() * 0.045f, 0.0f).withTop (glass.getBottom() - glass.getHeight() * 0.18f).withHeight (glass.getHeight() * 0.06f),
                 reduction / 12.0f);
        };
        s.footTitle = "STYLE";
        s.footText = [&p] { return textOf (p, limitMode); };
        s.onEdit = [actions] { actions.openPage (3); };
    }
    return specs;
}
