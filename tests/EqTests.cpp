#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/eq/EqDesign.h"
#include "rigs/FactoryRigs.h"
#include "ui/EqDisplay.h"
#include "ui/EqPage.h"
#include "ui/Footswitch.h"

#include <complex>
#include <optional>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    eq::Band band (eq::Shape shape, double hz, double gainDb = 0.0, double q = 1.0, int slope = eq::defaultSlope)
    {
        eq::Band b;
        b.used = true;
        b.shape = shape;
        b.frequency = hz;
        b.gainDb = gainDb;
        b.q = q;
        b.slope = slope;
        return b;
    }

    int slopeIndex (int dbPerOctave)
    {
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == dbPerOctave)
                return i;
        FAIL ("No slope " << dbPerOctave);
        return 0;
    }

    double at (const eq::Band& b, double hz, double sampleRate = 48000.0)
    {
        return eq::responseDb (eq::design (b, sampleRate), hz, sampleRate);
    }

    // The analog bell the EQ aims for, written out here independently:
    // (s^2 + s A w/Q + w^2) / (s^2 + s w/(A Q) + w^2), A = 10^(gain/40).
    double analogBellDb (double hz, double f0, double gainDb, double q)
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * pi * f0;
        const std::complex<double> s (0.0, 2.0 * pi * hz);
        return 20.0 * std::log10 (std::abs ((s * s + s * A * w0 / q + w0 * w0) / (s * s + s * w0 / (A * q) + w0 * w0)));
    }

    // The worst difference from the analog bell over 20 Hz to 20 kHz.
    double worstBellError (const eq::Design& design, double f0, double gainDb, double q, double sampleRate)
    {
        double worst = 0.0;
        for (auto hz = 20.0; hz <= juce::jmin (20000.0, 0.499 * sampleRate); hz *= 1.01)
            worst = std::max (worst, std::abs (eq::responseDb (design, hz, sampleRate) - analogBellDb (hz, f0, gainDb, q)));
        return worst;
    }

    // The textbook digital bell (the bilinear transform, as most DAW EQs),
    // for comparison: it squashes curves near the top of the range.
    eq::Design bilinearBell (double f0, double gainDb, double q, double sampleRate)
    {
        const auto A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f0 / sampleRate, alpha = std::sin (w) / (2.0 * q);
        const auto a0 = 1.0 + alpha / A;
        eq::Design d;
        d.numSections = 1;
        d.sections[0] = { (1.0 + alpha * A) / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha * A) / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha / A) / a0 };
        return d;
    }

    //==========================================================================
    // The plugin, with stereo in and out, playing signals through the EQ.
    struct EqRig
    {
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 256;

        explicit EqRig (int inputs = 2)
        {
            processor.setPlayConfigDetails (inputs, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            set (ParamIDs::eqOn, 1.0f);
        }

        void set (const juce::String& id, float plain)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }

        void setBand (int index, const eq::Band& b)
        {
            set (ParamIDs::eqBand (index, "Shape"), (float) b.shape);
            set (ParamIDs::eqBand (index, "Freq"), (float) b.frequency);
            set (ParamIDs::eqBand (index, "Gain"), (float) b.gainDb);
            set (ParamIDs::eqBand (index, "Q"), (float) b.q);
            set (ParamIDs::eqBand (index, "Slope"), (float) b.slope);
            set (ParamIDs::eqBand (index, "Place"), (float) b.placement);
            set (ParamIDs::eqBand (index, "On"), b.on ? 1.0f : 0.0f);
            set (ParamIDs::eqBand (index, "Used"), b.used ? 1.0f : 0.0f);
        }

        // Plays left and right (functions of the sample index) for `seconds`; returns both outputs.
        std::array<std::vector<float>, 2> play (double seconds, const std::function<float (int)>& left,
                                                const std::function<float (int)>& right)
        {
            std::array<std::vector<float>, 2> out;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int start = 0; start < (int) (seconds * sampleRate); start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    buffer.setSample (0, i, left (played + i));
                    buffer.setSample (1, i, right (played + i));
                }
                played += blockSize;
                processor.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out[(size_t) ch].insert (out[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + blockSize);
            }
            return out;
        }

        ParagonX1AudioProcessor processor;
        int played = 0;
    };

    std::function<float (int)> tone (double hz, float level = 0.25f)
    {
        return [hz, level] (int n) { return level * (float) std::sin (2.0 * pi * hz * n / EqRig::sampleRate); };
    }

    std::function<float (int)> silence() { return [] (int) { return 0.0f; }; }

    // The level of `hz` in the last half of `samples`, in dB (full scale
    // sine = 0 dB), over a whole number of its cycles so nothing leaks.
    double levelDb (const std::vector<float>& samples, double hz)
    {
        const auto cycles = std::floor ((double) (samples.size() / 2) * hz / EqRig::sampleRate);
        const auto from = samples.size() - (size_t) std::round (cycles * EqRig::sampleRate / hz);
        const auto coefficient = 2.0 * std::cos (2.0 * pi * hz / EqRig::sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto amplitude = 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) (samples.size() - from);
        return 20.0 * std::log10 (std::max (amplitude, 1.0e-12));
    }

    float largestStep (const std::vector<float>& samples)
    {
        float step = 0.0f;
        for (size_t i = 1; i < samples.size(); ++i)
            step = std::max (step, std::abs (samples[i] - samples[i - 1]));
        return step;
    }
}

//==============================================================================
TEST_CASE ("EQ curves: a bell matches the analog curve, far closer than an ordinary digital EQ near the top", "[eq]")
{
    SECTION ("Exactly its gain at its own frequency, and flat far away")
    {
        for (const auto hz : { 50.0, 1000.0, 10000.0, 16000.0 })
        {
            INFO (hz << " Hz");
            CHECK_THAT (at (band (eq::Shape::bell, hz, 12.0), hz), WithinAbs (12.0, 0.01));
            CHECK_THAT (at (band (eq::Shape::bell, hz, -9.0, 4.0), hz), WithinAbs (-9.0, 0.01));
        }
        CHECK_THAT (at (band (eq::Shape::bell, 1000.0, 12.0, 2.0), 50.0), WithinAbs (0.0, 0.05));
    }

    SECTION ("Within a tenth of a dB of analog in the lows and mids")
    {
        for (const auto hz : { 100.0, 1000.0, 3000.0 })
            CHECK (worstBellError (eq::design (band (eq::Shape::bell, hz, 12.0), 48000.0), hz, 12.0, 1.0, 48000.0) < 0.12);
    }

    SECTION ("At 15 kHz, 44.1 kHz: within about a dB, where the bilinear transform is 7 dB off")
    {
        const auto matched = worstBellError (eq::design (band (eq::Shape::bell, 15000.0, 12.0), 44100.0), 15000.0, 12.0, 1.0, 44100.0);
        const auto ordinary = worstBellError (bilinearBell (15000.0, 12.0, 1.0, 44100.0), 15000.0, 12.0, 1.0, 44100.0);
        WARN ("15 kHz bell at 44.1 kHz: matched " << matched << " dB, bilinear " << ordinary << " dB from analog");
        CHECK (matched < 1.2);
        CHECK (ordinary > 5.0);
    }

    SECTION ("At 96 kHz: within 0.3 dB for bands up to 20 kHz, half a dB for one at 25 kHz")
    {
        for (const auto hz : { 5000.0, 15000.0, 20000.0 })
            CHECK (worstBellError (eq::design (band (eq::Shape::bell, hz, 12.0), 96000.0), hz, 12.0, 1.0, 96000.0) < 0.3);
        CHECK (worstBellError (eq::design (band (eq::Shape::bell, 25000.0, 12.0), 96000.0), 25000.0, 12.0, 1.0, 96000.0) < 0.55);
    }
}

TEST_CASE ("EQ curves: shelves, cuts and the other shapes", "[eq]")
{
    SECTION ("Shelves: the full gain one side, half way at the frequency, steeper with the slope")
    {
        const auto shelf12 = band (eq::Shape::lowShelf, 200.0, 12.0);
        CHECK_THAT (at (shelf12, 15.0), WithinAbs (12.0, 0.15));
        CHECK_THAT (at (shelf12, 200.0), WithinAbs (6.0, 0.05));
        CHECK (std::abs (at (shelf12, 4000.0)) < 0.1);

        const auto high = band (eq::Shape::highShelf, 4000.0, -8.0);
        CHECK_THAT (at (high, 18000.0), WithinAbs (-8.0, 0.4));
        CHECK_THAT (at (high, 4000.0), WithinAbs (-4.0, 0.1));
        CHECK (std::abs (at (high, 100.0)) < 0.05);

        // An octave above a 24 dB/oct low shelf, nearly nothing is left of it; a 6 dB/oct one is still lifting.
        CHECK (at (band (eq::Shape::lowShelf, 200.0, 12.0, 1.0, slopeIndex (24)), 400.0) < 0.3);
        CHECK (at (band (eq::Shape::lowShelf, 200.0, 12.0, 1.0, slopeIndex (6)), 400.0) > 2.0);
    }

    SECTION ("Cuts: -3 dB at the frequency, then exactly their slope")
    {
        for (const auto slope : { 6, 12, 24, 48, 96 })
        {
            INFO (slope << " dB/oct");
            const auto low = band (eq::Shape::lowCut, 400.0, 0.0, 1.0, slopeIndex (slope));
            CHECK_THAT (at (low, 400.0), WithinAbs (-3.01, 0.05));
            CHECK_THAT (at (low, 50.0) - at (low, 100.0), WithinAbs (-(double) slope, 0.5));   // an octave, well below
            const auto high = band (eq::Shape::highCut, 1000.0, 0.0, 1.0, slopeIndex (slope));
            CHECK_THAT (at (high, 1000.0), WithinAbs (-3.01, 0.1));
            CHECK (std::abs (at (high, 40.0)) < 0.1);
        }
        // Q above 1 makes a resonant peak at the corner (about 9 dB at Q 4, 12 dB/oct).
        const auto resonant = band (eq::Shape::lowCut, 400.0, 0.0, 4.0, slopeIndex (12));
        double peak = -100.0;
        for (auto hz = 300.0; hz < 800.0; hz += 2.0)
            peak = std::max (peak, at (resonant, hz));
        CHECK_THAT (peak, WithinAbs (9.2, 0.3));
    }

    SECTION ("Notch, band pass, all pass, tilts")
    {
        CHECK (at (band (eq::Shape::notch, 1000.0, 0.0, 4.0), 1000.0) < -40.0);
        CHECK (std::abs (at (band (eq::Shape::notch, 1000.0, 0.0, 4.0), 4000.0)) < 0.2);

        CHECK_THAT (at (band (eq::Shape::bandPass, 1000.0), 1000.0), WithinAbs (0.0, 0.05));
        CHECK (at (band (eq::Shape::bandPass, 1000.0), 100.0) < -18.0);

        for (const auto hz : { 30.0, 300.0, 3000.0, 15000.0 })
            CHECK (std::abs (at (band (eq::Shape::allPass, 1000.0, 0.0, 2.0), hz)) < 0.01);   // only the phase turns

        const auto tilt = band (eq::Shape::tiltShelf, 1000.0, 12.0);
        CHECK_THAT (at (tilt, 20.0), WithinAbs (-6.0, 0.2));
        CHECK_THAT (at (tilt, 1000.0), WithinAbs (0.0, 0.05));
        CHECK_THAT (at (tilt, 18000.0), WithinAbs (6.0, 0.4));

        // FLAT TILT +10: 1 dB per octave through its frequency.
        const auto flat = band (eq::Shape::flatTilt, 1000.0, 10.0);
        CHECK_THAT (at (flat, 1000.0), WithinAbs (0.0, 0.01));
        CHECK_THAT (at (flat, 125.0), WithinAbs (-3.0, 0.4));
        CHECK_THAT (at (flat, 8000.0), WithinAbs (3.0, 0.4));
    }

    SECTION ("Every shape at every slope, and bands above the top of the range, are stable")
    {
        for (int shape = 0; shape < eq::shapeNames.size(); ++shape)
            for (int slope = 0; slope < eq::numSlopes; ++slope)
                for (const auto hz : { 10.0, 1000.0, 21000.0, 30000.0 })
                    for (const auto sampleRate : { 44100.0, 96000.0 })
                    {
                        const auto d = eq::design (band ((eq::Shape) shape, hz, 12.0, 8.0, slope), sampleRate);
                        for (int i = 0; i < d.numSections; ++i)
                        {
                            const auto& s = d.sections[(size_t) i];
                            INFO (eq::shapeNames[shape] << " " << eq::slopeNames[slope] << " " << hz << " Hz at " << sampleRate);
                            // Poles inside the unit circle: |a2| < 1 and |a1| < 1 + a2.
                            REQUIRE (std::abs (s.a2) < 1.0);
                            REQUIRE (std::abs (s.a1) < 1.0 + s.a2);
                            REQUIRE (std::isfinite (s.b0 + s.b1 + s.b2));
                        }
                    }
    }
}

//==============================================================================
TEST_CASE ("EQ sound: bands change the level of what they're set to, and nothing else", "[eq]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 6.0));
    const auto out = rig.play (0.6, [] (int n) { return tone (1000.0) (n) + tone (100.0) (n); },
                                    [] (int n) { return tone (1000.0) (n) + tone (100.0) (n); });
    for (const auto& channel : out)
    {
        CHECK_THAT (levelDb (channel, 1000.0), WithinAbs (20.0 * std::log10 (0.25) + 6.0, 0.1));
        CHECK_THAT (levelDb (channel, 100.0), WithinAbs (20.0 * std::log10 (0.25) + at (band (eq::Shape::bell, 1000.0, 6.0), 100.0), 0.1));
    }
}

TEST_CASE ("EQ sound: switched off, or with no bands, it leaves the sound exactly as it was", "[eq]")
{
    for (const auto on : { false, true })
    {
        EqRig rig;
        rig.set (ParamIDs::eqOn, on ? 1.0f : 0.0f);
        if (! on)
            rig.setBand (0, band (eq::Shape::bell, 1000.0, 12.0));   // off: even a band does nothing
        const auto out = rig.play (0.2, tone (440.0), tone (660.0, 0.2f));
        // Within 1e-6 (-120 dB), not bit for bit: on Apple's arm64 compiler the
        // test's own tone() can come out one float step (1 ulp) apart from the
        // samples the rig fed in, as it fuses multiply-adds differently.
        for (int i = 0; i < 2000; ++i)
        {
            REQUIRE_THAT (out[0][(size_t) i], WithinAbs (tone (440.0) (i), 1.0e-6));
            REQUIRE_THAT (out[1][(size_t) i], WithinAbs (tone (660.0, 0.2f) (i), 1.0e-6));
        }
    }
}

TEST_CASE ("EQ sound: stereo, left, right, mid and side bands work on just that", "[eq][stereo]")
{
    const auto boost = [] (eq::Placement placement)
    {
        auto b = band (eq::Shape::bell, 1000.0, 12.0);
        b.placement = placement;
        return b;
    };
    const auto base = 20.0 * std::log10 (0.25);

    SECTION ("Left only: the right channel is untouched")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::left));
        const auto out = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (base, 0.05));
    }

    SECTION ("Mid: a sound in the middle (the same both sides) is boosted on both")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::mid));
        const auto out = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (base + 12.0, 0.1));
    }

    SECTION ("Side: a sound in the middle is untouched; what differs between the sides is boosted")
    {
        EqRig rig;
        rig.setBand (0, boost (eq::Placement::side));
        const auto middle = rig.play (0.5, tone (1000.0), tone (1000.0));
        CHECK_THAT (levelDb (middle[0], 1000.0), WithinAbs (base, 0.05));
        CHECK_THAT (levelDb (middle[1], 1000.0), WithinAbs (base, 0.05));

        // Opposite polarity on each side: all side, no mid.
        const auto wide = rig.play (0.5, tone (1000.0), [] (int n) { return -tone (1000.0) (n); });
        CHECK_THAT (levelDb (wide[0], 1000.0), WithinAbs (base + 12.0, 0.1));
        CHECK_THAT (levelDb (wide[1], 1000.0), WithinAbs (base + 12.0, 0.1));
    }

    SECTION ("A mono track: stereo and mid bands work, left, right and side ones do nothing")
    {
        EqRig rig (1);
        rig.setBand (0, boost (eq::Placement::mid));
        rig.setBand (1, band (eq::Shape::bell, 300.0, 6.0));
        auto sideBand = boost (eq::Placement::side);
        sideBand.frequency = 3000.0;
        rig.setBand (2, sideBand);
        const auto out = rig.play (0.5, [] (int n) { return tone (1000.0) (n) + tone (300.0) (n) + tone (3000.0) (n); }, silence());
        for (const auto& channel : out)   // the mono input, played on both sides
        {
            CHECK_THAT (levelDb (channel, 1000.0), WithinAbs (base + 12.0 + at (band (eq::Shape::bell, 300.0, 6.0), 1000.0), 0.15));
            CHECK_THAT (levelDb (channel, 300.0), WithinAbs (base + 6.0 + at (boost (eq::Placement::stereo), 300.0), 0.15));
            CHECK_THAT (levelDb (channel, 3000.0),
                        WithinAbs (base + at (boost (eq::Placement::stereo), 3000.0) + at (band (eq::Shape::bell, 300.0, 6.0), 3000.0), 0.15));
        }
    }
}

TEST_CASE ("EQ sound: no clicks when a band changes shape or switches on, no zipper when dragging", "[eq]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 200.0, 12.0, 2.0));
    rig.play (0.3, tone (200.0), tone (200.0));
    const auto steady = rig.play (0.2, tone (200.0), tone (200.0));
    const auto normal = largestStep (steady[0]);

    SECTION ("A bell becomes a 48 dB/oct low cut, mid-note: it crossfades")
    {
        rig.set (ParamIDs::eqBand (0, "Slope"), (float) slopeIndex (48));
        rig.set (ParamIDs::eqBand (0, "Shape"), (float) eq::Shape::lowCut);
        const auto changing = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (changing[0]) < normal * 1.2f);
    }

    SECTION ("Switching the band off and on")
    {
        rig.set (ParamIDs::eqBand (0, "On"), 0.0f);
        const auto off = rig.play (0.1, tone (200.0), tone (200.0));
        rig.set (ParamIDs::eqBand (0, "On"), 1.0f);
        const auto on = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (off[0]) < normal * 1.2f);
        CHECK (largestStep (on[0]) < normal * 1.2f);
    }

    SECTION ("Dragging the gain from +12 to -12 dB in one go glides")
    {
        rig.set (ParamIDs::eqBand (0, "Gain"), -12.0f);
        const auto moving = rig.play (0.1, tone (200.0), tone (200.0));
        CHECK (largestStep (moving[0]) < normal * 1.2f);
        CHECK_THAT (levelDb (rig.play (0.3, tone (200.0), tone (200.0))[0], 200.0), WithinAbs (20.0 * std::log10 (0.25) - 12.0, 0.1));
    }
}

TEST_CASE ("EQ SOLO plays only the band's part of the spectrum", "[eq]")
{
    EqRig rig;
    rig.setBand (3, band (eq::Shape::bell, 2000.0, -6.0, 4.0));
    rig.processor.setEqSolo (3);
    const auto out = rig.play (0.5, [] (int n) { return tone (2000.0) (n) + tone (200.0) (n); },
                                    [] (int n) { return tone (2000.0) (n) + tone (200.0) (n); });
    // The 2 kHz the band works on, as it comes in (not cut: solo lets you hear it); 200 Hz far quieter.
    CHECK_THAT (levelDb (out[0], 2000.0), WithinAbs (20.0 * std::log10 (0.25), 0.2));
    CHECK (levelDb (out[0], 200.0) < 20.0 * std::log10 (0.25) - 30.0);

    rig.processor.setEqSolo (-1);
    const auto back = rig.play (0.5, tone (200.0), tone (200.0));
    CHECK_THAT (levelDb (back[0], 200.0), WithinAbs (20.0 * std::log10 (0.25) + at (band (eq::Shape::bell, 2000.0, -6.0, 4.0), 200.0), 0.1));
}

TEST_CASE ("EQ OUTPUT, GAIN SCALE and AUTO GAIN", "[eq]")
{
    const auto base = 20.0 * std::log10 (0.25);

    SECTION ("GAIN SCALE: 0% flat, 50% half, 200% double the bands' gain")
    {
        for (const auto [scale, expected] : { std::pair { 0.0f, 0.0 }, std::pair { 0.5f, 3.0 }, std::pair { 2.0f, 12.0 } })
        {
            EqRig rig;
            rig.setBand (0, band (eq::Shape::bell, 1000.0, 6.0));
            rig.set (ParamIDs::eqGainScale, scale);
            CHECK_THAT (levelDb (rig.play (0.5, tone (1000.0), tone (1000.0))[0], 1000.0), WithinAbs (base + expected, 0.1));
        }
    }

    SECTION ("OUTPUT")
    {
        EqRig rig;
        rig.set (ParamIDs::eqOutput, -9.0f);
        CHECK_THAT (levelDb (rig.play (0.3, tone (500.0), tone (500.0))[1], 500.0), WithinAbs (base - 9.0, 0.05));
    }

    SECTION ("AUTO GAIN takes off the curve's average lift, an octave at a time")
    {
        // +6 dB above 1 kHz is about +6 over the top five octaves of ten, plus the shelf's slope.
        EqRig rig;
        rig.setBand (0, band (eq::Shape::highShelf, 1000.0, 6.0));
        rig.set (ParamIDs::eqAutoGain, 1.0f);
        const auto out = rig.play (0.5, tone (100.0), tone (100.0));
        auto settings = rig.processor.getEqSettings();
        const auto expected = -eq::ParametricEq::averageChangeDb (settings, EqRig::sampleRate);
        CHECK (expected < -2.0);
        CHECK (expected > -4.0);
        CHECK_THAT (rig.processor.getEqAutoGainDb(), WithinAbs (expected, 0.01));
        CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (base + expected + at (band (eq::Shape::highShelf, 1000.0, 6.0), 100.0), 0.1));
    }
}

TEST_CASE ("EQ: zero latency, 24 bands run in real time comfortably, and unused slots do nothing", "[eq][performance]")
{
    EqRig rig;
    CHECK (rig.processor.getLatencySamples() == 0);

    // Unused slots, however they're set, do nothing.
    for (int i = 0; i < ParamIDs::eqNumBands; ++i)
    {
        auto b = band (eq::Shape::bell, 300.0 + 100.0 * i, 12.0);
        b.used = false;
        rig.setBand (i, b);
    }
    const auto untouched = rig.play (0.2, tone (700.0), tone (700.0));
    CHECK_THAT (levelDb (untouched[0], 700.0), WithinAbs (20.0 * std::log10 (0.25), 0.01));

    // All 24, the costliest way: 96 dB/oct cuts and shelves, on mid and side.
    for (int i = 0; i < ParamIDs::eqNumBands; ++i)
    {
        auto b = band (i % 2 == 0 ? eq::Shape::lowShelf : eq::Shape::highCut, 100.0 + 400.0 * i, 3.0, 1.0, slopeIndex (96));
        b.placement = (eq::Placement) (i % 5);
        rig.setBand (i, b);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = rig.play (3.0, tone (440.0), tone (550.0));
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 3000.0;
    WARN ("24 bands at 96 dB/oct use " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.5);
    for (const auto& channel : out)
        for (const auto sample : channel)
            REQUIRE (std::isfinite (sample));
}

TEST_CASE ("EQ: projects from before 0.4.0, with the graphic EQ on and boosted, come back flat", "[eq][state]")
{
    // 0.3.0's eqOn switched the graphic EQ (whose bands are gone); it's the
    // new EQ's switch now. On, with no bands, it's flat.
    juce::MemoryBlock saved;
    {
        EqRig old;
        old.processor.getStateInformation (saved);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    for (const auto& [id, value] : { std::pair { "eq80", 12.0 }, std::pair { "eqLevel", 6.0 }, std::pair { "eqUltraLo", 1.0 } })
    {
        auto* param = xml->createNewChildElement ("PARAM");
        param->setAttribute ("id", id);
        param->setAttribute ("value", value);
    }
    juce::MemoryBlock oldProject;
    juce::AudioProcessor::copyXmlToBinary (*xml, oldProject);

    EqRig rig;
    rig.processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    const auto out = rig.play (0.3, tone (80.0), tone (80.0));
    CHECK_THAT (levelDb (out[0], 80.0), WithinAbs (20.0 * std::log10 (0.25), 0.01));
}

//==============================================================================
TEST_CASE ("EQ display: double-click adds a band there, drag moves it, the wheel sets Q, Delete removes it", "[eq][ui]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    EqDisplay display (processor);
    display.setBounds (0, 0, 1000, 400);
    EqBands bands (processor);

    const auto event = [&] (juce::Point<float> at, juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &display, &display, now, at, now, clicks, false);
    };

    // A bell at 1 kHz, +6 dB.
    const juce::Point<float> where { display.xForFrequency (1000.0), display.yForDb (6.0) };
    display.mouseDoubleClick (event (where, {}, 2));
    REQUIRE (bands.get (0, "Used") > 0.5f);
    CHECK ((eq::Shape) juce::roundToInt (bands.get (0, "Shape")) == eq::Shape::bell);
    CHECK_THAT (bands.get (0, "Freq"), WithinAbs (1000.0, 15.0));
    CHECK_THAT (bands.get (0, "Gain"), WithinAbs (6.0, 0.1));
    CHECK (display.getSelected() == 0);

    // At the far left: a low cut.
    display.mouseDoubleClick (event ({ display.xForFrequency (15.0), display.yForDb (0.0) }, {}, 2));
    CHECK ((eq::Shape) juce::roundToInt (bands.get (1, "Shape")) == eq::Shape::lowCut);

    // Drag band 1 to 2 kHz, -3 dB.
    display.refresh();
    display.mouseDown (event (where));
    const juce::Point<float> to { display.xForFrequency (2000.0), display.yForDb (-3.0) };
    const auto now = juce::Time::getCurrentTime();
    display.mouseDrag (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), to, juce::ModifierKeys::leftButtonModifier,
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display, &display, now, where, now, 1, true));
    display.mouseUp (event (to));
    CHECK_THAT (bands.get (0, "Freq"), WithinAbs (2000.0, 30.0));
    CHECK_THAT (bands.get (0, "Gain"), WithinAbs (-3.0, 0.1));

    // The wheel narrows it.
    display.refresh();
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 0.5f;
    display.mouseMove (event (to));
    display.mouseWheelMove (event (to), wheel);
    CHECK (bands.get (0, "Q") > 1.5f);

    // Delete.
    CHECK (display.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (bands.get (0, "Used") < 0.5f);
    CHECK (display.getSelected() == -1);
}

TEST_CASE ("EQ page: its rack unit's POWER name opens it; BACK returns to the pedal", "[eq][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    std::vector<Footswitch*> stomps;
    std::vector<EqPage*> pages;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        if (auto* stomp = dynamic_cast<Footswitch*> (&c))
            stomps.push_back (stomp);
        if (auto* page = dynamic_cast<EqPage*> (&c))
            pages.push_back (page);
        for (auto* child : c.getChildren())
            collect (*child);
    };
    collect (*editor);
    REQUIRE (stomps.size() == 6);   // the rack units' POWER switches, in the chain's order
    REQUIRE (pages.size() == 1);
    CHECK_FALSE (pages[0]->isVisible());

    stomps[0]->onSelect();
    CHECK (pages[0]->isVisible());
    CHECK_FALSE (stomps[0]->getParentComponent()->isVisible());   // its rack unit, with the pedal, is behind it

    pages[0]->onClose();
    CHECK_FALSE (pages[0]->isVisible());
    CHECK (stomps[0]->getParentComponent()->isVisible());
}

//==============================================================================
// Dynamic EQ (0.5.0)
namespace
{
    // Makes band `index` dynamic: its range, and a manual threshold (or AUTO).
    void setDynamic (EqRig& rig, int index, double rangeDb, std::optional<double> thresholdDb, double attack = 0.5, double release = 0.5)
    {
        rig.set (ParamIDs::eqBand (index, "Dyn"), (float) rangeDb);
        rig.set (ParamIDs::eqBand (index, "DynAuto"), thresholdDb.has_value() ? 0.0f : 1.0f);
        if (thresholdDb.has_value())
            rig.set (ParamIDs::eqBand (index, "Thresh"), (float) *thresholdDb);
        rig.set (ParamIDs::eqBand (index, "Attack"), (float) attack);
        rig.set (ParamIDs::eqBand (index, "Release"), (float) release);
    }

    // A tone at a level in dB (a full-scale sine is 0 dB, as on the analyser).
    std::function<float (int)> toneDb (double hz, double db) { return tone (hz, (float) std::pow (10.0, db / 20.0)); }

    double outDb (EqRig& rig, double seconds, double hz, double db)
    {
        return levelDb (rig.play (seconds, toneDb (hz, db), toneDb (hz, db))[0], hz);
    }
}

TEST_CASE ("Dynamic EQ: the gain curve, with its soft knee and range", "[eq][dynamic]")
{
    using eq::dynamics::gainChangeDb;
    CHECK (gainChangeDb (-40.0, -30.0, -12.0) == 0.0);                    // well under: nothing
    CHECK_THAT (gainChangeDb (-33.0, -30.0, -12.0), WithinAbs (0.0, 1.0e-9));   // the knee starts 3 dB under
    CHECK_THAT (gainChangeDb (-30.0, -30.0, -12.0), WithinAbs (-0.75, 1.0e-9));
    CHECK_THAT (gainChangeDb (-27.0, -30.0, -12.0), WithinAbs (-3.0, 1.0e-9)); // then a dB for each dB over...
    CHECK_THAT (gainChangeDb (-22.0, -30.0, -12.0), WithinAbs (-8.0, 1.0e-9));
    CHECK_THAT (gainChangeDb (0.0, -30.0, -12.0), WithinAbs (-12.0, 1.0e-9));  // ...up to the range
    CHECK_THAT (gainChangeDb (0.0, -30.0, 6.0), WithinAbs (6.0, 1.0e-9));      // a positive range boosts

    // Automatic timing follows the band's frequency; the knobs scale it 1/4 to 4 times.
    CHECK_THAT (eq::dynamics::attackSeconds (1000.0, 0.5), WithinAbs (0.004, 1.0e-9));
    CHECK_THAT (eq::dynamics::attackSeconds (50.0, 0.5), WithinAbs (0.05, 1.0e-9));
    CHECK_THAT (eq::dynamics::attackSeconds (1000.0, 0.0), WithinAbs (0.001, 1.0e-9));
    CHECK_THAT (eq::dynamics::releaseSeconds (1000.0, 1.0), WithinAbs (0.32, 1.0e-9));
}

TEST_CASE ("Dynamic EQ: a cut works only when the band's part is loud, by as much as it's over, up to its range", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, -30.0);

    // 10 dB under the threshold: untouched.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-40.0, 0.05));
    // 6 dB over: 6 dB down.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -24.0), WithinAbs (-30.0, 0.3));
    // 24 dB over: the whole range, 12 dB.
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-18.0, 0.3));
    // And back when it's quiet again.
    CHECK_THAT (outDb (rig, 1.0, 1000.0, -40.0), WithinAbs (-40.0, 0.05));

    // With a gain of its own: +6 dB when quiet, back to level (+6 - 6) when loud.
    rig.set (ParamIDs::eqBand (0, "Gain"), 6.0f);
    rig.set (ParamIDs::eqBand (0, "Dyn"), -6.0f);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-34.0, 0.1));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-6.0, 0.3));
}

TEST_CASE ("Dynamic EQ: a positive range boosts when loud", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, 6.0, -30.0);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -40.0), WithinAbs (-40.0, 0.05));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -10.0), WithinAbs (-4.0, 0.3));
}

TEST_CASE ("Dynamic EQ: a band listens only to its own part of the spectrum", "[eq][dynamic]")
{
    // A 5 kHz de-esser-like band; a loud bass note doesn't set it off.
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 5000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, -30.0);
    const auto quietHighs = [] (int n) { return toneDb (100.0, -6.0) (n) + toneDb (5000.0, -40.0) (n); };
    auto out = rig.play (0.5, quietHighs, quietHighs);
    CHECK_THAT (levelDb (out[0], 5000.0), WithinAbs (-40.0, 0.1));
    CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (-6.0, 0.05));
    CHECK (rig.processor.getEqDynamics (0).changeDb > -0.1f);

    // Loud highs do.
    const auto loudHighs = [] (int n) { return toneDb (100.0, -6.0) (n) + toneDb (5000.0, -6.0) (n); };
    out = rig.play (0.5, loudHighs, loudHighs);
    CHECK_THAT (levelDb (out[0], 5000.0), WithinAbs (-18.0, 0.5));
    CHECK_THAT (levelDb (out[0], 100.0), WithinAbs (-6.0, 0.05));
}

TEST_CASE ("Dynamic EQ: AUTO's threshold settles above the band's usual level, and acts on what stands out", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    setDynamic (rig, 0, -12.0, std::nullopt);

    // A steady part: left alone.
    rig.play (4.0, toneDb (1000.0, -30.0), toneDb (1000.0, -30.0));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -30.0), WithinAbs (-30.0, 0.2));
    CHECK_THAT (rig.processor.getEqDynamics (0).thresholdDb, WithinAbs (-24.0, 0.5));

    // A passage 18 dB louder is pulled down at once...
    rig.play (0.15, toneDb (1000.0, -12.0), toneDb (1000.0, -12.0));
    CHECK (rig.processor.getEqDynamics (0).changeDb < -8.0f);

    // ...and as it carries on, the threshold follows it up and lets go.
    rig.play (15.0, toneDb (1000.0, -12.0), toneDb (1000.0, -12.0));
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -12.0), WithinAbs (-12.0, 0.5));
}

TEST_CASE ("Dynamic EQ: ATTACK and RELEASE set how quickly it acts and lets go", "[eq][dynamic]")
{
    const auto block = EqRig::blockSize / EqRig::sampleRate;   // 5.3 ms

    // The first 5 ms of a loud note.
    const auto afterOnset = [&] (double attack)
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
        setDynamic (rig, 0, -12.0, -30.0, attack);
        rig.play (0.2, toneDb (1000.0, -60.0), toneDb (1000.0, -60.0));
        rig.play (block, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
        return rig.processor.getEqDynamics (0).changeDb;
    };
    CHECK (afterOnset (0.0) < -8.0f);   // fast: most of the way already
    CHECK (afterOnset (1.0) > -5.0f);   // slow: still moving

    // 64 ms after a loud note stops.
    const auto afterStop = [&] (double release)
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
        setDynamic (rig, 0, -12.0, -30.0, 0.5, release);
        rig.play (0.5, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
        rig.play (12 * block, toneDb (1000.0, -60.0), toneDb (1000.0, -60.0));
        return rig.processor.getEqDynamics (0).changeDb;
    };
    CHECK (afterStop (0.0) > -1.5f);    // fast: let go
    CHECK (afterStop (1.0) < -7.0f);    // slow: still holding most of it
}

TEST_CASE ("Dynamic EQ: a band on one channel listens to and works on just that", "[eq][dynamic][stereo]")
{
    EqRig rig;
    auto b = band (eq::Shape::bell, 1000.0, 0.0, 2.0);
    b.placement = eq::Placement::left;
    rig.setBand (0, b);
    setDynamic (rig, 0, -12.0, -30.0);
    const auto out = rig.play (0.5, toneDb (1000.0, -6.0), toneDb (1000.0, -6.0));
    CHECK_THAT (levelDb (out[0], 1000.0), WithinAbs (-18.0, 0.3));
    CHECK_THAT (levelDb (out[1], 1000.0), WithinAbs (-6.0, 0.01));

    // Loud on the right only: the left band stays put.
    const auto quietLeft = rig.play (1.0, toneDb (1000.0, -40.0), toneDb (1000.0, -6.0));
    CHECK_THAT (levelDb (quietLeft[0], 1000.0), WithinAbs (-40.0, 0.05));
}

TEST_CASE ("Dynamic EQ: becoming dynamic, and static again, glides; cuts and other shapes can't be dynamic", "[eq][dynamic]")
{
    EqRig rig;
    rig.setBand (0, band (eq::Shape::bell, 1000.0, 0.0, 2.0));
    const auto loud = toneDb (1000.0, -6.0);
    rig.play (0.3, loud, loud);
    const auto normal = largestStep (rig.play (0.1, loud, loud)[0]);

    setDynamic (rig, 0, -12.0, -30.0);
    CHECK (largestStep (rig.play (0.2, loud, loud)[0]) <= normal * 1.01f);   // it only turns down, smoothly
    CHECK (rig.processor.getEqDynamics (0).active);

    rig.set (ParamIDs::eqBand (0, "Dyn"), 0.0f);
    CHECK (largestStep (rig.play (0.5, loud, loud)[0]) < normal * 1.05f);
    CHECK_THAT (outDb (rig, 0.5, 1000.0, -6.0), WithinAbs (-6.0, 0.01));
    CHECK_FALSE (rig.processor.getEqDynamics (0).active);

    // A low cut with a range set is just a low cut.
    EqRig cut;
    cut.setBand (0, band (eq::Shape::lowCut, 100.0));
    setDynamic (cut, 0, -12.0, -60.0);
    CHECK_THAT (outDb (cut, 0.5, 1000.0, -6.0), WithinAbs (-6.0 + at (band (eq::Shape::lowCut, 100.0), 1000.0), 0.01));
    CHECK_FALSE (cut.processor.getEqDynamics (0).active);
}

TEST_CASE ("Dynamic EQ: 24 dynamic bands still run in real time comfortably", "[eq][dynamic][performance]")
{
    EqRig rig;
    for (int i = 0; i < ParamIDs::eqNumBands; ++i)
    {
        auto b = band (i % 2 == 0 ? eq::Shape::bell : eq::Shape::highShelf, 100.0 + 400.0 * i, 3.0, 1.0, slopeIndex (96));
        b.placement = (eq::Placement) (i % 5);
        rig.setBand (i, b);
        setDynamic (rig, i, i % 2 == 0 ? -12.0 : 6.0, std::nullopt);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto out = rig.play (3.0, tone (440.0), tone (550.0));
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 3000.0;
    WARN ("24 dynamic bands use " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.5);
    for (const auto& channel : out)
        for (const auto sample : channel)
            REQUIRE (std::isfinite (sample));
}

TEST_CASE ("Dynamic EQ: cutting a held low note doesn't distort it", "[eq][dynamic]")
{
    // A gain that moved within each cycle would add harmonics; measuring
    // over a couple of cycles keeps it steady. 6 dB over the threshold, so
    // the gain follows the level (at full range it couldn't move at all).
    for (const auto attack : { 0.0, 0.5 })
    {
        EqRig rig;
        rig.setBand (0, band (eq::Shape::bell, 60.0, 0.0, 1.0));
        setDynamic (rig, 0, -12.0, -30.0, attack);
        const auto out = rig.play (1.5, toneDb (60.0, -24.0), toneDb (60.0, -24.0));
        const auto fundamental = levelDb (out[0], 60.0);
        CHECK_THAT (fundamental, WithinAbs (-30.0, 0.5));
        for (const auto harmonic : { 120.0, 180.0, 240.0 })
        {
            INFO ("attack " << attack << ", " << harmonic << " Hz: " << levelDb (out[0], harmonic) - fundamental << " dB");
            CHECK (levelDb (out[0], harmonic) - fundamental < -60.0);
        }
    }
}

TEST_CASE ("Dynamic EQ display and page: Alt+double-click, the range handle, Alt+wheel, the dynamics row", "[eq][dynamic][ui]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    EqPage page (processor);
    page.setBounds (0, 0, 1422, 750);
    auto& display = page.getDisplay();
    EqBands bands (processor);

    const auto event = [&] (juce::Point<float> at, juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &display, &display, now, at, now, clicks, false);
    };

    // A slot once used by a dynamic band comes back static.
    bands.create (eq::Shape::bell, 500.0, 0.0, -9.0);
    bands.remove (0);
    REQUIRE (bands.create (eq::Shape::bell, 500.0, 3.0) == 0);
    CHECK_THAT (bands.get (0, "Dyn"), WithinAbs (0.0, 1.0e-4));   // 0 dB can come back a hair off (it's stored as 0 to 1)
    bands.remove (0);

    // Alt+double-click: a flat bell whose range goes to where you clicked.
    display.mouseDoubleClick (event ({ display.xForFrequency (2000.0), display.yForDb (-8.0) }, juce::ModifierKeys::altModifier, 2));
    REQUIRE (bands.get (0, "Used") > 0.5f);
    CHECK_THAT (bands.get (0, "Gain"), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (bands.get (0, "Dyn"), WithinAbs (-8.0, 0.1));

    // The dynamics row shows for it, and goes when it's static.
    page.resized();
    page.refreshControls();
    CHECK (page.isShowingDynamics());

    // Drag the range handle up to +4 dB.
    display.refresh();
    const auto handle = display.rangeHandleFor (processor.getEqSettings().bands[0]);
    display.mouseDown (event (handle));
    const juce::Point<float> to { handle.x, display.yForDb (4.0) };
    const auto now = juce::Time::getCurrentTime();
    display.mouseDrag (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), to, juce::ModifierKeys::leftButtonModifier,
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display, &display, now, handle, now, 1, true));
    display.mouseUp (event (to));
    CHECK_THAT (bands.get (0, "Dyn"), WithinAbs (4.0, 0.1));
    CHECK_THAT (bands.get (0, "Freq"), WithinAbs (2000.0, 30.0));   // the band itself stays put

    // Alt+wheel turns the range; down to 0 makes it an ordinary band again.
    display.refresh();
    const auto dot = display.dotFor (processor.getEqSettings().bands[0]);
    display.mouseMove (event (dot));
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = -0.4f;
    display.mouseWheelMove (event (dot, juce::ModifierKeys::altModifier), wheel);
    CHECK_THAT (bands.get (0, "Dyn"), WithinAbs (0.0, 0.1));
    page.refreshControls();
    CHECK_FALSE (page.isShowingDynamics());
}

//==============================================================================
// Factory rigs (0.6.0): they do what their notes say.
namespace
{
    void loadFactory (EqRig& rig, const juce::String& name)
    {
        const auto* entry = rig.processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        rig.processor.loadRig (*entry);
        rig.play (0.3, silence(), silence());   // the EQ settles on the new settings
    }

    // The stereo curve's largest boost and cut from `fromHz` to `toHz`.
    std::pair<double, double> curveExtremes (const eq::ParametricEq::Settings& settings, double fromHz, double toHz,
                                             eq::Placement which = eq::Placement::stereo)
    {
        double most = -1000.0, least = 1000.0;
        for (auto hz = fromHz; hz <= toHz; hz *= 1.02)
        {
            const auto db = eq::ParametricEq::curveDb (settings, hz, EqRig::sampleRate, which);
            most = std::max (most, db);
            least = std::min (least, db);
        }
        return { most, least };
    }
}

TEST_CASE ("Factory rigs: none boosts wildly, mastering ones move 2 dB at most, and all play cleanly", "[eq][rigs]")
{
    juce::Random random (3);
    for (const auto& factory : makeFactoryRigs())
    {
        INFO (factory.name);
        EqRig rig;
        loadFactory (rig, factory.name);
        const auto settings = rig.processor.getEqSettings();
        CHECK (settings.on == (factory.name != "Init"));

        for (const auto placement : { eq::Placement::stereo, eq::Placement::mid, eq::Placement::side })
        {
            const auto [most, least] = curveExtremes (settings, 40.0, 16000.0, placement);
            CHECK (most <= 4.5);   // starting points, not drastic
            if (factory.bank == Rig::master && placement == eq::Placement::stereo)
            {
                CHECK (most <= 2.0);
                CHECK (least >= -2.0);
            }
        }

        // Noise through it: nothing blows up or jumps out.
        const auto noise = [&random] (int) { return (random.nextFloat() * 2.0f - 1.0f) * 0.25f; };
        const auto out = rig.play (0.5, noise, noise);
        float peak = 0.0f;
        for (const auto& channel : out)
            for (const auto sample : channel)
            {
                REQUIRE (std::isfinite (sample));
                peak = std::max (peak, std::abs (sample));
            }
        // (Mastering rigs whose LIMITER drives it louder may go up to their ceiling.)
        const auto limited = factory.parameters.count (ParamIDs::limitGain) > 0 && factory.parameters.at (ParamIDs::limitGain) > 0.0f;
        CHECK (peak < (limited ? juce::Decibels::decibelsToGain (factory.parameters.at (ParamIDs::limitCeiling)) + 0.001f : 0.8f));
    }
}

TEST_CASE ("Factory rigs: Kick Punch, Mix Brighter and Master Gentle Polish change the levels they say", "[eq][rigs]")
{
    const auto level = [] (const juce::String& name, double hz)
    {
        EqRig rig;
        loadFactory (rig, name);
        return outDb (rig, 0.5, hz, -20.0) + 20.0;   // the change in dB
    };
    // Kick: weight at 60 Hz up, the box at 350 Hz down.
    CHECK (level ("Kick Punch", 60.0) > 2.0);
    CHECK (level ("Kick Punch", 350.0) < -3.0);
    // 0.6 dB an octave: 8 kHz (three octaves up) +1.8 dB, 125 Hz -1.8 dB.
    CHECK_THAT (level ("Mix Brighter", 8000.0), WithinAbs (1.8, 0.3));
    CHECK_THAT (level ("Mix Brighter", 125.0), WithinAbs (-1.8, 0.3));
    // A master's EQ barely moves the middle. (Its EQ alone: since 0.22.1 a
    // master is the whole rack, and DOMINION turns a lone test tone down,
    // as it's meant to; RIG LEVEL makes up what the other units take.)
    {
        EqRig rig;
        loadFactory (rig, "Master Gentle Polish");
        for (const auto* id : { ParamIDs::domOn, ParamIDs::compOn, ParamIDs::marsOn, ParamIDs::stereoOn })
            rig.set (id, 0.0f);
        rig.set (ParamIDs::rigLevel, 0.0f);
        rig.play (0.3, silence(), silence());
        CHECK_THAT (outDb (rig, 0.5, 1000.0, -20.0) + 20.0, WithinAbs (0.0, 0.5));
    }
}

TEST_CASE ("Genre rigs (0.15.0): each genre's EQ does what its notes say", "[eq][rigs][genres]")
{
    // The EQ's curve (stereo, mid or side) at `hz`, as the rig sets it.
    const auto curve = [] (const juce::String& name, double hz, eq::Placement which = eq::Placement::stereo)
    {
        EqRig rig;
        loadFactory (rig, name);
        return eq::ParametricEq::curveDb (rig.processor.getEqSettings(), hz, EqRig::sampleRate, which);
    };
    // Hard rock's guitars are mid-forward; nu metal's are scooped.
    CHECK (curve ("Hard Rock Guitars", 1000.0) > 1.5);
    CHECK (curve ("Nu Metal Guitars", 700.0) < -3.0);
    CHECK (curve ("Hard Rock Guitars", 800.0) - curve ("Nu Metal Guitars", 800.0) > 4.0);
    // Extended-range guitars keep their low strings; symphonic ones leave the lows to the orchestra.
    CHECK (curve ("Djent Guitars", 85.0) - curve ("Symphonic Metal Guitars", 85.0) > 6.0);
    // Argent metal's sub weight; grunge's darker mix and master.
    CHECK (curve ("Argent Metal Mix", 50.0) > 1.5);
    CHECK (curve ("Argent Metal Bass", 45.0) > 1.5);
    CHECK (curve ("Grunge Mix", 12000.0) < -0.5);
    CHECK (curve ("Grunge Master", 100.0) > 0.4);
    CHECK (curve ("Grunge Master", 10000.0) < -0.4);
    // Argent metal's choir sits darker; djent's bass clanks; industrial's vocal is mid-heavy and rolled off on top.
    CHECK (curve ("Argent Metal Vocal", 12000.0) < -1.5);
    CHECK (curve ("Djent Bass", 2800.0) > 2.5);
    CHECK (curve ("Industrial Vocal", 1200.0) > 2.5);
    CHECK (curve ("Industrial Vocal", 16000.0) < -1.0);
    // The symphonic mix: wider on top (the sides lifted, not the middle), the sides' lows cut.
    CHECK (curve ("Symphonic Metal Mix", 10000.0, eq::Placement::side) - curve ("Symphonic Metal Mix", 10000.0, eq::Placement::mid) > 1.0);
    CHECK (curve ("Symphonic Metal Mix", 60.0, eq::Placement::side) < -6.0);
    // Djent's master keeps the low strings in the middle.
    CHECK (curve ("Djent Master", 60.0, eq::Placement::side) < -6.0);
    CHECK_THAT (curve ("Djent Master", 60.0, eq::Placement::mid), WithinAbs (0.0, 0.5));
}

TEST_CASE ("Genre rigs (0.15.0): the guitars' palm-mute boom comes down only when it blooms", "[eq][rigs][genres][dynamic]")
{
    for (const auto* name : { "Modern Metal Guitars", "Djent Guitars", "Argent Metal Guitars" })
    {
        INFO (name);
        EqRig rig;
        loadFactory (rig, name);
        const auto boomHz = (double) rig.processor.getEqSettings().bands[1].frequency;
        // Quiet and steady: AUTO settles and leaves it alone...
        rig.play (3.0, toneDb (boomHz, -40.0), toneDb (boomHz, -40.0));
        CHECK (rig.processor.getEqDynamics (1).changeDb > -0.5f);
        // ...a loud chug is turned down.
        rig.play (0.2, toneDb (boomHz, -10.0), toneDb (boomHz, -10.0));
        CHECK (rig.processor.getEqDynamics (1).changeDb < -2.0f);
    }
}

TEST_CASE ("Factory rigs: Vocal De-Esser acts only on loud sibilance; Mix Wider Top leaves the middle alone", "[eq][rigs][dynamic]")
{
    {
        EqRig rig;
        loadFactory (rig, "Vocal De-Esser");
        // Steady, quiet highs: left alone once AUTO has settled...
        rig.play (3.0, toneDb (6500.0, -40.0), toneDb (6500.0, -40.0));
        CHECK_THAT (outDb (rig, 0.5, 6500.0, -40.0), WithinAbs (-40.0, 0.3));
        // ...a loud S is pulled down at once, and the rest of the voice isn't.
        const auto sibilant = [] (int n) { return toneDb (6500.0, -12.0) (n) + toneDb (300.0, -12.0) (n); };
        const auto out = rig.play (0.1, sibilant, sibilant);
        CHECK (rig.processor.getEqDynamics (0).changeDb < -4.0f);
        CHECK_THAT (levelDb (out[0], 300.0), WithinAbs (-12.0, 0.1));
    }
    {
        EqRig rig;
        loadFactory (rig, "Mix Wider Top");
        // The same on both sides (all middle): untouched, so a mono playback sounds the same.
        CHECK_THAT (outDb (rig, 0.5, 10000.0, -20.0), WithinAbs (-20.0, 0.01));
        CHECK_THAT (outDb (rig, 0.5, 60.0, -20.0), WithinAbs (-20.0, 0.01));
        // Opposite on each side (all sides): the top lifted, by the side curve (1.5 dB at 10 kHz, 2 dB above).
        const auto side = toneDb (10000.0, -20.0);
        const auto out = rig.play (0.5, side, [&side] (int n) { return -side (n); });
        const auto lift = eq::ParametricEq::curveDb (rig.processor.getEqSettings(), 10000.0, EqRig::sampleRate, eq::Placement::side);
        CHECK (lift > 1.0);
        CHECK_THAT (levelDb (out[0], 10000.0), WithinAbs (-20.0 + lift, 0.05));
    }
}

//==============================================================================
// EQ Match (0.7.0)
namespace
{
    // Paul Kellet's economy pink filter: falls 3 dB an octave.
    struct Pink
    {
        juce::Random random { 11 };
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;
        float operator()()
        {
            const auto white = random.nextFloat() * 2.0 - 1.0;
            b0 = 0.99765 * b0 + white * 0.0990460;
            b1 = 0.96300 * b1 + white * 0.2965164;
            b2 = 0.57000 * b2 + white * 1.0526913;
            return (float) (0.05 * (b0 + b1 + b2 + white * 0.1848));
        }
    };

    eq::match::Spectrum analyse (const std::function<float()>& source, double seconds, double sampleRate = 48000.0)
    {
        eq::match::Analyser analyser;
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (auto& s : samples)
            s = source();
        analyser.add (samples.data(), (int) samples.size(), sampleRate);
        return analyser.spectrum();
    }

    double atHz (const std::vector<double>& onGrid, double hz)
    {
        const auto& g = eq::match::grid();
        const auto i = (size_t) std::distance (g.begin(), std::min_element (g.begin(), g.end(), [hz] (double a, double b) { return std::abs (std::log (a / hz)) < std::abs (std::log (b / hz)); }));
        return onGrid[i];
    }

    const std::vector<eq::Band> knownCurve { band (eq::Shape::lowShelf, 120.0, -3.0, 0.7), band (eq::Shape::bell, 1000.0, 4.0, 1.0),
                                             band (eq::Shape::bell, 3000.0, -2.5, 2.0), band (eq::Shape::highShelf, 8000.0, 3.0, 0.7) };
}

TEST_CASE ("EQ Match: the analyser hears a spectrum's shape and ignores silence", "[eq][match]")
{
    juce::Random random (5);
    const auto white = analyse ([&random] { return random.nextFloat() * 0.5f - 0.25f; }, 8.0);
    Pink pink;
    const auto pinkSpectrum = analyse ([&pink] { return pink(); }, 8.0);
    REQUIRE (white.isValid());
    REQUIRE (pinkSpectrum.isValid());

    // Pink falls 3 dB an octave against white: 6 octaves, 100 Hz to 6.4 kHz, 18 dB.
    const auto tilt = (atHz (pinkSpectrum.db, 6400.0) - atHz (white.db, 6400.0)) - (atHz (pinkSpectrum.db, 100.0) - atHz (white.db, 100.0));
    CHECK_THAT (tilt, WithinAbs (-18.0, 1.5));

    // 2 seconds of silence, then 4 of noise: 4 seconds heard.
    int n = 0;
    const auto withPause = analyse ([&] { return n++ < 96000 ? 0.0f : random.nextFloat() * 0.5f - 0.25f; }, 6.0);
    CHECK_THAT (withPause.seconds, WithinAbs (4.0, 0.3));

    // Saved and loaded again, it's the same.
    const auto loaded = eq::match::Spectrum::fromXml (*pinkSpectrum.toXml ("Pink"));
    REQUIRE (loaded.has_value());
    for (size_t i = 0; i < loaded->db.size(); ++i)
        REQUIRE_THAT (loaded->db[i], WithinAbs (pinkSpectrum.db[i], 1.0e-6));
}

TEST_CASE ("EQ Match: the target is the tone difference, not the level, minus what the EQ already does", "[eq][match]")
{
    juce::Random random (6);
    auto track = analyse ([&random] { return random.nextFloat() * 0.5f - 0.25f; }, 5.0);
    const auto bell = eq::match::curveOnGrid ({ band (eq::Shape::bell, 1000.0, 4.0, 1.0) }, 48000.0);

    // The same sound, 6 dB louder: nothing to match.
    auto louder = track;
    for (auto& db : louder.db)
        db += 6.0;
    for (const auto value : eq::match::target (track, louder, {}, 0.5))
        REQUIRE (std::abs (value) < 0.01);
    CHECK (eq::match::fit (eq::match::target (track, louder, {}, 0.5), {}).empty());

    // A reference with a 1 kHz lift, when the EQ already has that lift: nothing left to do.
    auto lifted = track;
    for (size_t i = 0; i < lifted.db.size(); ++i)
        lifted.db[i] += bell[i];
    CHECK (atHz (eq::match::target (track, lifted, {}, 0.5), 1000.0) > 2.0);
    for (const auto value : eq::match::target (track, lifted, bell, 0.5))
        REQUIRE (std::abs (value) < 0.3);

    // Where the reference has nothing (below 60 Hz, 80 dB down), it isn't followed.
    auto noLows = track;
    for (size_t i = 0; i < noLows.db.size(); ++i)
        if (eq::match::grid()[i] < 60.0)
            noLows.db[i] -= 80.0;
    CHECK (std::abs (atHz (eq::match::target (track, noLows, {}, 0.5), 25.0)) < 1.0);
}

TEST_CASE ("EQ Match: the bands it adds follow the difference; DETAIL and AMOUNT", "[eq][match]")
{
    const auto target = eq::match::curveOnGrid (knownCurve, 48000.0);
    const auto worstMiss = [&] (const std::vector<eq::Band>& bands, double scale)
    {
        const auto curve = eq::match::curveOnGrid (bands, 48000.0);
        double worst = 0.0;
        for (size_t i = 0; i < curve.size(); ++i)
            if (eq::match::grid()[i] <= 16000.0)
                worst = std::max (worst, std::abs (curve[i] - target[i] * scale));
        return worst;
    };

    eq::match::Options options;
    const auto bands = eq::match::fit (target, options);
    WARN ("Matched with " << bands.size() << " bands, within " << worstMiss (bands, 1.0) << " dB");
    CHECK (bands.size() <= 8);
    CHECK (worstMiss (bands, 1.0) < 1.0);

    // Less detail: fewer, broader bands, still the overall shape.
    options.detail = 0.0;
    const auto broad = eq::match::fit (target, options);
    CHECK (broad.size() <= 4);
    CHECK (worstMiss (broad, 1.0) < 2.5);

    // Half the amount: half the curve.
    options.detail = 0.5;
    options.amount = 0.5;
    CHECK (worstMiss (eq::match::fit (target, options), 0.5) < 0.75);

    // No more bands than there are free slots.
    options.amount = 1.0;
    options.maxBands = 2;
    CHECK (eq::match::fit (target, options).size() <= 2);
}

TEST_CASE ("EQ Match: your track, matched to a reference file, comes out with the reference's tone", "[eq][match]")
{
    // The reference: the same kind of noise through a known EQ, as a WAV file.
    juce::TemporaryFile wav (".wav");
    {
        EqRig shaper;
        for (size_t i = 0; i < knownCurve.size(); ++i)
            shaper.setBand ((int) i, knownCurve[i]);
        shaper.play (0.3, silence(), silence());
        juce::Random random (8);
        const auto noise = [&random] (int) { return random.nextFloat() * 0.4f - 0.2f; };
        const auto out = shaper.play (8.0, noise, noise);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav.getFile()), 48000.0, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        const float* channels[] = { out[0].data(), out[1].data() };
        writer->writeFromFloatArrays (channels, 2, (int) out[0].size());
    }

    EqRig rig;
    auto& match = rig.processor.getEqMatch();
    juce::Random random (9);
    const auto noise = [&random] (int) { return random.nextFloat() * 0.4f - 0.2f; };

    // Your track: plain noise, recorded from the EQ's input as it plays.
    match.setRecording (eq::match::Session::Recording::track);
    for (int i = 0; i < 60; ++i)
    {
        rig.play (0.1, noise, noise);
        match.update (rig.processor.getEqInputTap());
    }
    match.setRecording (eq::match::Session::Recording::none);
    CHECK (match.getTrack().seconds > 5.0);

    match.loadFile (wav.getFile());
    for (int i = 0; i < 500 && match.isLoading(); ++i)
    {
        juce::Thread::sleep (10);
        match.update (rig.processor.getEqInputTap());
    }
    REQUIRE (match.getReference().isValid());
    CHECK (match.getReferenceName() == wav.getFile().getFileNameWithoutExtension());

    const auto bands = eq::match::fit (eq::match::target (match.getTrack(), match.getReference(), {}, 0.5), {});
    REQUIRE (! bands.empty());
    for (size_t i = 0; i < bands.size(); ++i)
        rig.setBand ((int) i, bands[i]);
    rig.play (0.3, noise, noise);

    // What comes out now, against the reference: the same tone within a dB or so (smoothed to a third of an octave).
    eq::match::Analyser out;
    const auto played = rig.play (8.0, noise, noise);
    out.add (played[0].data(), (int) played[0].size(), 48000.0);
    const auto difference = eq::match::target (out.spectrum(), match.getReference(), {}, 0.75);
    double worst = 0.0;
    for (size_t i = 0; i < difference.size(); ++i)
        if (eq::match::grid()[i] >= 50.0 && eq::match::grid()[i] <= 15000.0)
            worst = std::max (worst, std::abs (difference[i]));
    WARN ("After matching, the output is within " << worst << " dB of the reference's tone");
    CHECK (worst < 1.5);
}

TEST_CASE ("EQ Match: references save and load, and a file that isn't audio is refused", "[eq][match]")
{
    const auto folder = juce::File::createTempFile ("refs");
    eq::match::Session session (folder);
    Pink pink;
    const auto spectrum = analyse ([&pink] { return pink(); }, 5.0);
    REQUIRE (session.save (spectrum, "Album Master"));
    REQUIRE (session.getSavedReferences().size() == 1);
    CHECK (session.useSaved (session.getSavedReferences()[0]));
    CHECK (session.getReferenceName() == "Album Master");
    CHECK_THAT (session.getReference().db[50], WithinAbs (spectrum.db[50], 1.0e-6));

    juce::TemporaryFile notAudio (".wav");
    notAudio.getFile().replaceWithText ("not audio");
    session.loadFile (notAudio.getFile());
    InputTap tap;
    for (int i = 0; i < 300 && session.isLoading(); ++i)
    {
        juce::Thread::sleep (10);
        session.update (tap);
    }
    CHECK (session.getProblem().isNotEmpty());
    CHECK (session.getReferenceName() == "Album Master");   // the reference it had is kept
    folder.deleteRecursively();
}

TEST_CASE ("EQ Match on the page: MATCH opens it in place of the band controls; ADD BANDS adds them, switches the EQ on, and one undo takes them back", "[eq][match][ui]")
{
    EqRig rig;
    rig.set (ParamIDs::eqOn, 0.0f);
    EqPage page (rig.processor);
    page.setBounds (0, 0, 1422, 750);
    page.getDisplay().select (-1);

    page.showMatch (true);
    CHECK (page.isShowingMatch());
    CHECK (page.getMatchPanel().isVisible());
    auto& match = rig.processor.getEqMatch();
    CHECK (match.getRecording() == eq::match::Session::Recording::track);   // opening it starts listening

    // Your track: pink noise. The reference: white noise (much brighter).
    Pink pink;
    juce::Random random (4);
    for (int i = 0; i < 50; ++i)
    {
        rig.play (0.1, [&pink] (int) { return pink(); }, silence());
        match.update (rig.processor.getEqInputTap());
    }
    match.setRecording (eq::match::Session::Recording::reference);
    for (int i = 0; i < 50; ++i)
    {
        rig.play (0.1, [&random] (int) { return random.nextFloat() * 0.2f - 0.1f; }, silence());
        match.update (rig.processor.getEqInputTap());
    }
    match.setRecording (eq::match::Session::Recording::none);
    auto& panel = page.getMatchPanel();
    panel.refresh();
    REQUIRE_FALSE (panel.getProposal().empty());
    CHECK (page.getDisplay().hasMatchPreview());

    // White is brighter than pink: the bands lift the top against the bottom.
    const auto proposal = panel.getProposal();
    const auto curve = eq::match::curveOnGrid (proposal, 48000.0);
    CHECK (atHz (curve, 8000.0) - atHz (curve, 100.0) > 9.0);

    rig.processor.flushUndoStep();
    panel.addBands();
    CHECK_FALSE (page.isShowingMatch());
    CHECK_FALSE (page.getDisplay().hasMatchPreview());
    const auto settings = rig.processor.getEqSettings();
    CHECK (settings.on);
    CHECK ((size_t) std::count_if (settings.bands.begin(), settings.bands.end(), [] (const eq::Band& b) { return b.used; }) == proposal.size());

    rig.processor.flushUndoStep();
    rig.processor.undo();
    const auto undone = rig.processor.getEqSettings();
    CHECK (std::none_of (undone.bands.begin(), undone.bands.end(), [] (const eq::Band& b) { return b.used; }));
    CHECK_FALSE (undone.on);
}

TEST_CASE ("EQ Match: fitting is quick enough to follow DETAIL and AMOUNT as they turn", "[eq][match][performance]")
{
    Pink pink;
    juce::Random random (12);
    const auto track = analyse ([&pink] { return pink(); }, 5.0);
    const auto reference = analyse ([&random] { return random.nextFloat() * 0.2f - 0.1f; }, 5.0);
    // The worst case: a jagged difference (a random walk, +-8 dB) that needs many bands.
    auto jagged = eq::match::target (track, reference, {}, 1.0);
    double walk = 0.0;
    for (auto& value : jagged)
        value = (walk = juce::jlimit (-8.0, 8.0, walk + (random.nextFloat() - 0.5f) * 3.0));
    eq::match::Options options;
    options.detail = 1.0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto bands = eq::match::fit (jagged, options);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
    WARN ("At full detail: " << bands.size() << " bands in " << juce::String (ms, 1) << " ms");
    CHECK (ms < 150.0);
}

TEST_CASE ("Master rigs: the full chain (0.22.1) is as loud as the master was before", "[eq][rigs][mastering]")
{
    // MARS, BINARY, and the DOMINION and CONSTRICTOR some masters gained,
    // take some level; each rig's RIG LEVEL makes it up, before GUILLOTINE.
    // Checked on a mix-like noise against the same rig with them off.
    for (const auto& factory : makeFactoryRigs())
    {
        if (factory.bank != Rig::master)
            continue;
        double results[2] {};
        for (int version = 0; version < 2; ++version)
        {
            EqRig rig;
            loadFactory (rig, factory.name);
            if (version == 0)   // as before: the units this change added, off
            {
                rig.set (ParamIDs::marsOn, 0.0f);
                rig.set (ParamIDs::stereoOn, 0.0f);
                rig.set (ParamIDs::domOn, 0.0f);
                rig.set (ParamIDs::rigLevel, 0.0f);
                if (factory.notes.contains ("Master Four Bands"))
                    rig.set (ParamIDs::compOn, 0.0f);
            }
            // A mix-like signal: mostly the same on both sides (the middle), a little stereo difference.
            Pink middle, sides;
            sides.random.setSeed (99);
            float m = 0.0f, d = 0.0f;
            int at = -1;
            const auto next = [&] (int i) { if (i != at) { m = middle() * 2.0f; d = sides() * 0.6f; at = i; } };
            const auto l = [&] (int i) { next (i); return m + d; };
            const auto r = [&] (int i) { next (i); return m - d; };
            rig.play (1.0, l, r);
            const auto out = rig.play (2.0, l, r);
            double sum = 0.0;
            for (auto x : out[0]) sum += (double) x * x;
            results[version] = 10.0 * std::log10 (sum / (double) out[0].size());
        }
        INFO (factory.name);
        CHECK_THAT (results[1] - results[0], WithinAbs (0.0, 0.5));
    }
}
