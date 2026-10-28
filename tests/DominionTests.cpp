// DOMINION (stomp 5, 0.10.0): what it does to the sound.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/dominion/Dominion.h"
#include "ui/DominionPage.h"
#include "ui/Footswitch.h"
#include "rigs/DominionPresets.h"
#include "dsp/eq/EqMatch.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = juce::MathConstants<double>::pi;

    // Paul Kellet's economy pink filter: equal energy per octave, as music roughly is.
    struct Pink
    {
        explicit Pink (int seed) : random (seed) {}
        juce::Random random;
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

    struct DominionRig
    {
        dominion::Processor processor;
        dominion::Settings settings;
        DominionRig()
        {
            settings.on = true;
            processor.prepare (sampleRate, 256);
        }
        // Plays `seconds` of left / right from the functions; returns the output.
        std::array<std::vector<float>, 2> play (double seconds, const std::function<float (int)>& left, const std::function<float (int)>& right)
        {
            std::array<std::vector<float>, 2> result;
            juce::AudioBuffer<float> buffer (2, 256);
            for (int start = 0; start < (int) (seconds * sampleRate); start += 256)
            {
                for (int i = 0; i < 256; ++i)
                {
                    buffer.setSample (0, i, left (played + i));
                    buffer.setSample (1, i, right (played + i));
                }
                played += 256;
                processor.process (juce::dsp::AudioBlock<float> (buffer), settings);
                for (int c = 0; c < 2; ++c)
                    result[(size_t) c].insert (result[(size_t) c].end(), buffer.getReadPointer (c), buffer.getReadPointer (c) + 256);
            }
            return result;
        }
        int played = 0;
    };

    // The level of `hz` over the second half of `samples` (Goertzel), dB, full-scale sine 0 dB.
    double toneDb (const std::vector<float>& samples, double hz)
    {
        const auto from = samples.size() / 2;
        const auto n = samples.size() - from;
        const auto c = 2.0 * std::cos (2.0 * pi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (std::max (1.0e-30, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) n);
    }

    // The spectrum of the second half, smoothed to a third of an octave, on eq::match::grid().
    std::vector<double> spectrum (const std::vector<float>& samples)
    {
        eq::match::Analyser analyser;
        analyser.add (samples.data() + samples.size() / 2, (int) (samples.size() / 2), sampleRate);
        auto db = analyser.spectrum().db;
        std::vector<double> smoothed (db.size());
        for (int i = 0; i < (int) db.size(); ++i)
        {
            double sum = 0.0;
            int n = 0;
            for (int j = std::max (0, i - 2); j <= std::min ((int) db.size() - 1, i + 2); ++j, ++n)
                sum += db[(size_t) j];
            smoothed[(size_t) i] = sum / n;
        }
        return smoothed;
    }

    double at (const std::vector<double>& onGrid, double hz)
    {
        const auto& g = eq::match::grid();
        size_t best = 0;
        for (size_t i = 1; i < g.size(); ++i)
            if (std::abs (std::log (g[i] / hz)) < std::abs (std::log (g[best] / hz)))
                best = i;
        return onGrid[best];
    }

    std::function<float (int)> sine (double hz, double db)
    {
        const auto amplitude = std::pow (10.0, db / 20.0);
        return [hz, amplitude] (int n) { return (float) (amplitude * std::sin (2.0 * pi * hz * n / sampleRate)); };
    }
}

namespace
{
    // Pink noise (as music) with a resonance: a 2.5 kHz tone at -20 dB, about
    // 12 dB above the noise around it. The same on both channels unless given.
    struct Resonant
    {
        explicit Resonant (double toneLevelDb = -20.0, double gainDb = 0.0, double hz = 2500.0)
            : tone (sine (hz, toneLevelDb + gainDb)), gain ((float) std::pow (10.0, gainDb / 20.0)) {}
        float operator() (int n)
        {
            while ((int) samples.size() <= n)
                samples.push_back (pink() * gain + tone ((int) samples.size()));
            return samples[(size_t) n];
        }
        Pink pink { 1 };
        std::function<float (int)> tone;
        float gain;
        std::vector<float> samples;
    };

    struct Result
    {
        double toneChange, noise800, noise8k, near;   // dB changes
    };

    Result run (dominion::Settings settings, double toneLevelDb = -20.0, double gainDb = 0.0)
    {
        Resonant left (toneLevelDb, gainDb), right (toneLevelDb, gainDb);
        DominionRig rig;
        settings.on = true;
        rig.settings = settings;
        const auto out = rig.play (6.0, [&] (int n) { return left (n); }, [&] (int n) { return right (n); });
        const auto in = spectrum (left.samples), after = spectrum (out[0]);
        return { toneDb (out[0], 2500.0) - toneDb (left.samples, 2500.0), at (after, 800.0) - at (in, 800.0),
                 at (after, 8000.0) - at (in, 8000.0), at (after, 2000.0) - at (in, 2000.0) };
    }
}

TEST_CASE ("DOMINION: switched off, it leaves the sound alone; there's no delay", "[dominion]")
{
    DominionRig rig;
    rig.settings.on = false;
    Resonant source;
    const auto out = rig.play (0.5, [&] (int n) { return source (n); }, [&] (int n) { return source (n); });
    for (size_t i = 0; i < out[0].size(); ++i)
        REQUIRE_THAT (out[0][i], WithinAbs (source.samples[i], 1.0e-6));
}

TEST_CASE ("DOMINION SOFT: a resonance is turned down; the rest of the sound hardly at all", "[dominion]")
{
    const auto result = run ({});
    WARN ("Default settings: the resonance " << result.toneChange << " dB; the noise at 800 Hz " << result.noise800
          << " dB, at 8 kHz " << result.noise8k << " dB");
    CHECK (result.toneChange < -6.0);
    CHECK (std::abs (result.noise800) < 0.4);
    CHECK (std::abs (result.noise8k) < 0.4);
}

TEST_CASE ("DOMINION: SOFT acts the same at any level; HARD acts on what's loud", "[dominion]")
{
    dominion::Settings soft;
    const auto loud = run (soft).toneChange, quiet = run (soft, -20.0, -24.0).toneChange;
    CHECK_THAT (quiet, WithinAbs (loud, 2.0));

    dominion::Settings hard;
    hard.mode = dominion::Mode::hard;
    const auto hardLoud = run (hard).toneChange, hardQuiet = run (hard, -20.0, -24.0).toneChange;
    WARN ("HARD: " << hardLoud << " dB loud, " << hardQuiet << " dB 24 dB quieter");
    CHECK (hardLoud < -3.0);
    CHECK (hardQuiet > hardLoud * 0.5);
}

TEST_CASE ("DOMINION: DEPTH sets how much, MAX CUT how far at most", "[dominion]")
{
    double previous = 1.0;
    for (const auto depth : { 0.0, 2.0, 5.0, 10.0 })
    {
        dominion::Settings settings;
        settings.depth = depth;
        const auto change = run (settings).toneChange;
        INFO ("depth " << depth);
        if (depth == 0.0)
            CHECK (std::abs (change) < 0.1);
        else
            CHECK (change < previous - 0.5);
        previous = change;
    }
    dominion::Settings capped;
    capped.depth = 20.0;
    capped.maxCutDb = 3.0;
    CHECK (run (capped).toneChange > -3.6);
}

TEST_CASE ("DOMINION: more DETAIL, narrower cuts", "[dominion]")
{
    dominion::Settings broad, narrow;
    broad.detail = 10.0;
    narrow.detail = 90.0;
    const auto b = run (broad), n = run (narrow);
    WARN ("Near the resonance (2 kHz): " << b.near << " dB with DETAIL 10, " << n.near << " dB with DETAIL 90");
    CHECK (n.near > b.near + 0.3);
    CHECK (n.toneChange < -4.0);
}

TEST_CASE ("DOMINION: the depth curve steers it", "[dominion]")
{
    const auto band = [] (dominion::Shape shape, double hz, double depthDb = 0.0, int slopeDb = 24)
    {
        dominion::Band b;
        b.used = true;
        b.shape = shape;
        b.frequency = hz;
        b.depthDb = depthDb;
        for (int i = 0; i < eq::numSlopes; ++i)
            if (eq::slopes[i] == slopeDb)
                b.slope = i;
        return b;
    };
    dominion::Settings flat, lowCut, boosted, bandPass;
    lowCut.bands[0] = band (dominion::Shape::lowCut, 6000.0);            // only above 6 kHz
    boosted.bands[0] = band (dominion::Shape::bell, 2500.0, 12.0);       // harder at 2.5 kHz
    bandPass.bands[0] = band (dominion::Shape::bandPass, 600.0);         // only round 600 Hz
    const auto plain = run (flat).toneChange;
    CHECK (std::abs (run (lowCut).toneChange) < 1.0);
    CHECK (run (boosted).toneChange < plain - 2.0);
    CHECK (std::abs (run (bandPass).toneChange) < 1.0);   // (within the measurement's own wobble)
}

TEST_CASE ("DOMINION: left/right or mid/side, LINK and FOCUS", "[dominion][stereo]")
{
    // The resonance only on the left (or only in the side).
    const auto play = [] (dominion::Settings settings, bool side)
    {
        Pink pinkL (3), pinkR (3);
        const auto tone = sine (2500.0, -20.0);
        std::vector<float> inL, inR;
        DominionRig rig;
        rig.settings = settings;
        rig.settings.on = true;
        const auto out = rig.play (6.0,
            [&] (int n) { const auto x = pinkL() + tone (n); inL.push_back (x); return x; },
            [&] (int n) { const auto x = pinkR() + (side ? -tone (n) : 0.0f); inR.push_back (x); return x; });
        return std::array<double, 2> { toneDb (out[0], 2500.0) - toneDb (inL, 2500.0),
                                        at (spectrum (out[1]), 2500.0) - at (spectrum (inR), 2500.0) };
    };

    dominion::Settings unlinked;
    unlinked.stereo = dominion::StereoMode::leftRight;
    unlinked.link = 0.0;
    auto r = play (unlinked, false);
    CHECK (r[0] < -6.0);              // the left is worked on
    CHECK (std::abs (r[1]) < 0.3);    // the right, without the resonance, isn't

    auto linked = unlinked;
    linked.link = 100.0;
    r = play (linked, false);
    CHECK (r[1] < -1.0);              // linked, the right follows the left

    auto focused = unlinked;
    focused.focus = 100.0;            // all on the right
    r = play (focused, false);
    CHECK (std::abs (r[0]) < 0.3);

    // Mid/side, unlinked: a resonance only in the side (opposite on left and right) is cut,
    // and the middle (the same on both) isn't touched.
    dominion::Settings midSide;
    midSide.link = 0.0;
    Pink pink (5);
    const auto tone = sine (2500.0, -20.0);
    std::vector<float> mid;
    DominionRig rig;
    rig.settings = midSide;
    rig.settings.on = true;
    const auto out = rig.play (6.0, [&] (int n) { const auto p = pink(); mid.push_back (p); return p + tone (n); },
                                    [&] (int n) { return mid[(size_t) n] - tone (n); });
    std::vector<float> outMid (out[0].size()), outSide (out[0].size());
    for (size_t i = 0; i < out[0].size(); ++i)
    {
        outMid[i] = 0.5f * (out[0][i] + out[1][i]);
        outSide[i] = 0.5f * (out[0][i] - out[1][i]);
    }
    CHECK (toneDb (outSide, 2500.0) < -26.0);
    CHECK (std::abs (at (spectrum (outMid), 2500.0) - at (spectrum (mid), 2500.0)) < 0.5);
}

TEST_CASE ("DOMINION: DELTA is exactly what it takes away; MIX, WET TRIM and OUT", "[dominion]")
{
    const auto play = [] (const std::function<void (dominion::Settings&)>& setUp)
    {
        Resonant source;
        DominionRig rig;
        setUp (rig.settings);
        auto out = rig.play (2.0, [&] (int n) { return source (n); }, [&] (int n) { return source (n); });
        return std::pair { out[0], source.samples };
    };
    const auto [wet, dry] = play ([] (auto&) {});
    const auto [delta, unused] = play ([] (auto& s) { s.delta = true; });
    for (size_t i = 4800; i < wet.size(); ++i)   // after switching on (its short crossfade)
        REQUIRE_THAT (wet[i] + delta[i], WithinAbs (dry[i], 1.0e-5));

    const auto [none, dry2] = play ([] (auto& s) { s.mix = 0.0; });
    for (size_t i = 24000; i < none.size(); ++i)
        REQUIRE_THAT (none[i], WithinAbs (dry2[i], 1.0e-6));

    const auto [trimmed, dry3] = play ([] (auto& s) { s.wetTrimDb = -6.0; s.outDb = 3.0; });
    CHECK_THAT (at (spectrum (trimmed), 400.0) - at (spectrum (dry3), 400.0), WithinAbs (-3.0, 0.3));
}

TEST_CASE ("DOMINION: ATTACK and RELEASE, and the tilts that shape them", "[dominion]")
{
    // How far it has cut 10 ms after the resonance starts.
    const auto after10ms = [] (double attack, double tiltHigh)
    {
        Pink pink (7);
        const auto tone = sine (2500.0, -20.0);
        DominionRig rig;
        rig.settings.attack = attack;
        rig.settings.attackTiltHigh = tiltHigh;
        rig.play (2.0, [&] (int) { return pink(); }, [&] (int) { return 0.0f; });
        rig.play (0.0107, [&] (int n) { return pink() + tone (n); }, [&] (int) { return 0.0f; });
        double most = 0.0;
        for (int k = 0; k < rig.processor.getNumFilters(); ++k)
            most = std::max (most, (double) rig.processor.getReductionDb (0, k));
        return most;
    };
    CHECK (after10ms (0.0, 0.0) > after10ms (100.0, 0.0) + 2.0);   // fast attack: there already
    CHECK (after10ms (50.0, -100.0) > after10ms (50.0, 100.0) + 1.0);   // the highs' attack tilted faster
}

TEST_CASE ("DOMINION: listening to a band plays only its part of what's taken away", "[dominion]")
{
    Resonant source;
    DominionRig rig;
    rig.settings.bands[0].used = true;
    rig.settings.bands[0].shape = dominion::Shape::bell;
    rig.settings.bands[0].frequency = 2500.0;
    rig.settings.listenBand = 0;
    const auto out = rig.play (4.0, [&] (int n) { return source (n); }, [&] (int n) { return source (n); });
    // The resonance's part, a few dB of it, and next to nothing of the lows.
    CHECK (toneDb (out[0], 2500.0) > -36.0);
    CHECK (at (spectrum (out[0]), 300.0) < at (spectrum (source.samples), 300.0) - 30.0);
}

TEST_CASE ("DOMINION: no clicks switching on; it runs in real time comfortably", "[dominion][performance]")
{
    DominionRig rig;
    rig.settings.on = false;
    const auto tone = sine (2500.0, -10.0);
    const auto steady = rig.play (0.5, tone, tone);
    rig.settings.on = true;
    const auto switching = rig.play (0.5, tone, tone);
    float normal = 0.0f, worst = 0.0f;
    for (size_t i = 1; i < steady[0].size(); ++i)
        normal = std::max (normal, std::abs (steady[0][i] - steady[0][i - 1]));
    for (size_t i = 1; i < switching[0].size(); ++i)
        worst = std::max (worst, std::abs (switching[0][i] - switching[0][i - 1]));
    CHECK (worst < normal * 1.2f);

    DominionRig busy;
    busy.settings.depth = 10.0;
    busy.settings.detail = 80.0;
    busy.settings.link = 50.0;
    Pink left (1), right (2);
    const auto start = juce::Time::getMillisecondCounterHiRes();
    busy.play (5.0, [&] (int) { return left(); }, [&] (int) { return right(); });
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - start) / 5000.0;
    WARN ("DOMINION, stereo, uses " << juce::String (fraction * 100.0, 1) << "% of one core in real time");
    CHECK (fraction < 0.3);
}

//==============================================================================
// In the plugin
TEST_CASE ("DOMINION in PR7-X: its rack unit (row 2) switches it, and through the plugin a resonance is cut", "[dominion][plugin]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, 256);
    processor.prepareToPlay (sampleRate, 256);
    CHECK (processor.getLatencySamples() == 0);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Footswitch*> stomps;
    std::vector<DominionPage*> pages;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
    {
        if (auto* stomp = dynamic_cast<Footswitch*> (&c))
            stomps.push_back (stomp);
        if (auto* page = dynamic_cast<DominionPage*> (&c))
            pages.push_back (page);
        for (auto* child : c.getChildren())
            collect (*child);
    };
    collect (*editor);
    REQUIRE (stomps.size() == 4);   // the rack units' POWER switches, in the chain's order
    REQUIRE (pages.size() == 1);
    CHECK_FALSE (stomps[1]->isPlaceholder());
    stomps[1]->toggle();
    CHECK (processor.getDominionSettings().on);
    CHECK_FALSE (pages[0]->isVisible());
    stomps[1]->onSelect();
    CHECK (pages[0]->isVisible());

    Resonant source;
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int start = 0; start < 48000 * 4; start += 256)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto x = source (start + i);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        processor.processBlock (buffer, midi);
        out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
    }
    CHECK (toneDb (out, 2500.0) - toneDb (source.samples, 2500.0) < -6.0);
}

TEST_CASE ("DOMINION's page: double-click adds a band to the depth curve, LISTEN plays its part, Delete removes it", "[dominion][ui]")
{
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, 256);
    processor.prepareToPlay (sampleRate, 256);
    DominionPage page (processor);
    page.setBounds (0, 0, 1422, 750);
    auto& display = page.getDisplay();

    const auto event = [&] (juce::Point<float> at, int clicks)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &display, &display, now, at, now, clicks, false);
    };
    display.mouseDoubleClick (event ({ display.xForFrequency (3000.0), display.yForDb (9.0) }, 2));
    auto settings = processor.getDominionSettings();
    REQUIRE (settings.bands[0].used);
    CHECK (settings.bands[0].shape == dominion::Shape::bell);
    CHECK_THAT (settings.bands[0].frequency, WithinAbs (3000.0, 60.0));
    CHECK_THAT (settings.bands[0].depthDb, WithinAbs (9.0, 0.2));
    CHECK (display.getSelected() == 0);
    // The depth curve rises there.
    CHECK (dominion::depthCurveDb (settings, 3000.0, 0, sampleRate) > 8.0);

    // At the far left: a low cut.
    display.mouseDoubleClick (event ({ display.xForFrequency (25.0), display.yForDb (0.0) }, 2));
    CHECK (processor.getDominionSettings().bands[1].shape == dominion::Shape::lowCut);

    // LISTEN, held, plays only band 1's part of what's taken away.
    processor.setDominionListen (0);
    CHECK (processor.getDominionSettings().listenBand == 0);
    processor.setDominionListen (-1);

    display.select (0);
    CHECK (display.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK_FALSE (processor.getDominionSettings().bands[0].used);
}

TEST_CASE ("DOMINION in the factory rigs: it tames a resonance and leaves the rest of the sound", "[dominion][rigs]")
{
    for (const auto* name : { "Lead Vocal", "Overheads", "Acoustic Guitar", "Mix Harshness Control" })
    {
        INFO (name);
        ParagonX1AudioProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, 256);
        processor.prepareToPlay (sampleRate, 256);
        const auto* entry = processor.getRigLibrary().findFactory (name);
        REQUIRE (entry != nullptr);
        processor.loadRig (*entry);
        REQUIRE (processor.getDominionSettings().on);

        // The rig's EQ changes the sound too: compare with the same rig, DOMINION off. COMP
        // off for both (with the resonance gone it would squeeze the rest less, lifting it).
        processor.getParameters().getParameter (ParamIDs::compOn)->setValueNotifyingHost (0.0f);
        const auto play = [&] (bool on)
        {
            processor.getParameters().getParameter (ParamIDs::domOn)->setValueNotifyingHost (on ? 1.0f : 0.0f);
            processor.reset();
            Resonant source (-20.0, 0.0, 4000.0);
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            std::vector<float> out;
            for (int start = 0; start < 48000 * 5; start += 256)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const auto x = source (start + i);
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }
                processor.processBlock (buffer, midi);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
            }
            return out;
        };
        const auto off = play (false), on = play (true);
        CHECK (toneDb (on, 4000.0) - toneDb (off, 4000.0) < -3.0);
        CHECK (std::abs (at (spectrum (on), 800.0) - at (spectrum (off), 800.0)) < 1.0);
    }
}

//==============================================================================
// DOMINION's own presets (0.10.3)
TEST_CASE ("DOMINION presets: the factory ones hold only DOMINION's settings, all in range", "[dominion][presets]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names;
    REQUIRE (DominionPresets::factory().front().name == "Default");
    for (const auto& preset : DominionPresets::factory())
    {
        INFO (preset.name);
        names.addIfNotAlreadyThere (preset.name);
        for (const auto& [id, value] : preset.parameters)
        {
            INFO (id);
            CHECK (DominionPresets::holds (id));
            CHECK (id != ParamIDs::domOn);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);
            CHECK (value >= parameter->getNormalisableRange().start);
            CHECK (value <= parameter->getNormalisableRange().end);
        }
    }
    CHECK (names.size() == (int) DominionPresets::factory().size());
    CHECK (names.size() >= 12);
}

TEST_CASE ("DOMINION presets: loading one sets DOMINION and nothing else, and one undo takes it back", "[dominion][presets]")
{
    ParagonX1AudioProcessor processor;
    auto& state = processor.getParameters();
    // Knob moves as the UI makes them: a gesture each, so undo records them.
    const auto set = [&] (const char* id, float value)
    {
        auto* p = state.getParameter (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    };
    const auto value = [&] (const juce::String& id) { return state.getRawParameterValue (id)->load(); };
    set (ParamIDs::eqOn, 1.0f);
    set (ParamIDs::domOn, 1.0f);
    set (ParamIDs::domFocus, 40.0f);   // not in the preset: back to its default
    processor.flushUndoStep();

    const auto* sibilance = processor.getDominionPresets().findFactory ("Vocal Sibilance");
    REQUIRE (sibilance != nullptr);
    processor.loadDominionPreset (*sibilance);
    processor.flushUndoStep();
    CHECK (value (ParamIDs::domDepth) == 7.0f);
    CHECK (value (ParamIDs::domFocus) == 0.0f);
    CHECK (value (ParamIDs::domBand (1, "Used")) > 0.5f);
    CHECK_THAT (value (ParamIDs::domBand (1, "Freq")), WithinAbs (7000.0, 1.0));
    CHECK (value (ParamIDs::domOn) > 0.5f);   // its switch, and the rest of the rig, are left alone
    CHECK (value (ParamIDs::eqOn) > 0.5f);
    CHECK (processor.getDominionPresetName() == "Vocal Sibilance");
    CHECK_FALSE (processor.isDominionPresetModified());
    set (ParamIDs::domDepth, 9.0f);
    CHECK (processor.isDominionPresetModified());

    processor.flushUndoStep();
    processor.undo();
    processor.undo();
    CHECK_THAT (value (ParamIDs::domFocus), WithinAbs (40.0, 0.5));
    CHECK (value (ParamIDs::domBand (0, "Used")) < 0.5f);
}

TEST_CASE ("DOMINION presets: yours are saved, listed, loaded and deleted", "[dominion][presets]")
{
    ParagonX1AudioProcessor processor;
    const auto folder = juce::File::createTempFile ("dominion-presets");
    processor.getDominionPresets().setFolder (folder);
    auto& state = processor.getParameters();
    state.getParameter (ParamIDs::domDetail)->setValueNotifyingHost (state.getParameter (ParamIDs::domDetail)->convertTo0to1 (83.0f));

    auto& presets = processor.getDominionPresets();
    REQUIRE (presets.save (DominionPresets::capture (state, "My Snare")));
    const auto yours = presets.user();
    REQUIRE (yours.size() == 1);
    CHECK (yours[0].name == "My Snare");
    CHECK (yours[0].parameters.count (ParamIDs::domOn) == 0);

    processor.loadDominionPreset (*presets.findFactory ("Default"));
    CHECK_THAT (state.getRawParameterValue (ParamIDs::domDetail)->load(), WithinAbs (50.0, 0.01));
    processor.loadDominionPreset (yours[0]);
    CHECK_THAT (state.getRawParameterValue (ParamIDs::domDetail)->load(), WithinAbs (83.0, 0.01));

    CHECK (presets.remove (yours[0]));
    CHECK (presets.user().empty());
    folder.deleteRecursively();
}

TEST_CASE ("DOMINION presets do what they're for: Vocal Sibilance and Kick Boom", "[dominion][presets]")
{
    // Two resonances, one where the preset should work and one where it shouldn't.
    const auto play = [] (const char* name, double workHz, double leaveHz)
    {
        dominion::Settings settings;
        ParagonX1AudioProcessor processor;
        processor.loadDominionPreset (*processor.getDominionPresets().findFactory (name));
        settings = processor.getDominionSettings();
        settings.on = true;
        Pink pink (9);
        const auto work = sine (workHz, -20.0), leave = sine (leaveHz, -20.0);
        std::vector<float> input;
        DominionRig rig;
        rig.settings = settings;
        const auto out = rig.play (6.0, [&] (int n) { const auto x = pink() + work (n) + leave (n); input.push_back (x); return x; },
                                        [&] (int n) { return input[(size_t) n]; });
        return std::pair { toneDb (out[0], workHz) - toneDb (input, workHz), toneDb (out[0], leaveHz) - toneDb (input, leaveHz) };
    };
    const auto [sibilance, voice] = play ("Vocal Sibilance", 7000.0, 1500.0);
    CHECK (sibilance < -4.0);
    CHECK (std::abs (voice) < 1.0);
    const auto [boom, click] = play ("Kick Boom", 120.0, 3000.0);
    CHECK (boom < -3.0);
    CHECK (std::abs (click) < 1.0);
}
