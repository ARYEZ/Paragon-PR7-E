#include <catch2/catch_test_macros.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/EqDisplay.h"
#include "ui/DominionPage.h"
#include "ui/EqPage.h"
#include "ui/OraclePage.h"
#include "ui/CompPage.h"
#include "ui/LimiterPage.h"
#include "ui/Finish.h"
#include "ui/Footswitch.h"
#include "ui/RackUnit.h"
#include "ui/ParagonLookAndFeel.h"

namespace
{
    juce::Component* findById (juce::Component& parent, const juce::String& id)
    {
        if (parent.getComponentID() == id)
            return &parent;
        for (auto* child : parent.getChildren())
            if (auto* found = findById (*child, id))
                return found;
        return nullptr;
    }
}

// Hidden test (the "[.]" tag): renders the editor to a PNG for reviewing the
// UI without a DAW. Run with:
//   PARAGON_SNAPSHOT=editor.png ./ParagonX1Tests "[snapshot]"
// Optionally set PARAGON_SNAPSHOT_EFFECTS=1 to switch on the tuner and the
// EQ (the stomps that work so far). PARAGON_SNAPSHOT_PAGE=presets shows
// the rig browser, with a couple of example user rigs, and =settings the SETTINGS page. PARAGON_SNAPSHOT_WIDTH
// sets the window width (default 1280).
template <typename Type>
Type* findType (juce::Component& parent)
{
    if (auto* match = dynamic_cast<Type*> (&parent))
        return match;
    for (auto* child : parent.getChildren())
        if (auto* found = findType<Type> (*child))
            return found;
    return nullptr;
}

TEST_CASE ("Render the editor to a PNG", "[.][snapshot]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "editor.png");

    ParagonX1AudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    const auto showPresets = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "presets";

    // PARAGON_SNAPSHOT_FINISH=black or =neon: the pearl black unit, or pearl
    // white with neon keys (not saved as the preference).
    const auto finish = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_FINISH", {});
    Finish::set (finish == "black" ? Finish::Type::pearlBlack : finish == "neon" ? Finish::Type::pearlWhiteNeon : Finish::Type::pearlWhite, false);
    const auto rigFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1SnapshotRigs");
    rigFolder.deleteRecursively();
    processor.useRigFolder (rigFolder);

    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EFFECTS", {}) == "1")
        for (const auto* id : { "tunerOn", "eqOn" })
            processor.getParameters().getParameter (id)->setValueNotifyingHost (1.0f);

    // PARAGON_SNAPSHOT_RIG=1: a saved rig loaded on the main page, as in the mockup.
    if (showPresets || juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_RIG", {}) == "1")
    {
        processor.saveCurrentRigAs ("Mix Bus");
        processor.saveCurrentRigAs ("Vocal Check");
        processor.loadRig (*processor.getRigLibrary().find (rigFolder.getChildFile (juce::String ("Mix Bus") + Rig::fileExtension)));
        processor.getParameters().getParameter ("eqOutput")->setValueNotifyingHost (0.6f);   // shows the * marker
    }

    // PARAGON_SNAPSHOT_FACTORY=name: that factory rig loaded (and selected on the PRESETS page).
    if (const auto factory = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_FACTORY", {}); factory.isNotEmpty())
        if (const auto* entry = processor.getRigLibrary().findFactory (factory))
            processor.loadRig (*entry);

    // PARAGON_SNAPSHOT_TUNER=1: the tuner on, after hearing a short sung
    // phrase (some notes sharp, some flat, some with vibrato) that ends on a
    // held A3 4 cents sharp, and the expression pedal assigned to the wah.
    // With PARAGON_SNAPSHOT_BLOCK=9 the TUNER panel shows its trace.
    const auto showTuner = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_TUNER", {}) == "1";
    if (showTuner)
    {
        auto set = [&] (const char* id, float value)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set ("tunerOn", 1.0f);
        set ("expressionTarget", 2.0f);
        set ("expression", 0.6f);
        set ("expressionMin", 0.2f);

        struct SungNote { double hz, cents, seconds, vibratoCents; };   // hz 0: a breath
        const SungNote phrase[] = { { 220.0, 6, 1.2, 0 }, { 0, 0, 0.25, 0 }, { 261.63, 14, 1.5, 20 }, { 0, 0, 0.2, 0 },
                                    { 293.66, -4, 1.0, 0 }, { 329.63, 22, 0.8, 0 }, { 293.66, -8, 1.6, 25 }, { 0, 0, 0.4, 0 },
                                    { 261.63, 10, 1.0, 0 }, { 220.0, 3, 2.0, 15 }, { 0, 0, 0.3, 0 }, { 196.0, -12, 1.2, 0 },
                                    { 220.0, 4, 2.5, 10 } };
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;
        int block = 0;
        for (const auto& sung : phrase)
        {
            for (int b = 0; b < (int) (sung.seconds * 48000.0 / 256.0); ++b, ++block)
            {
                buffer.clear();
                for (int i = 0; i < 256; ++i)
                {
                    const auto t = (double) (block * 256 + i) / 48000.0;
                    const auto cents = sung.cents + sung.vibratoCents * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t);
                    phase += juce::MathConstants<double>::twoPi * sung.hz * std::pow (2.0, cents / 1200.0) / 48000.0;
                    if (sung.hz > 0.0)
                        buffer.setSample (0, i, (float) (0.25 * std::sin (phase) + 0.1 * std::sin (2.0 * phase)));
                }
                processor.processBlock (buffer, midi);
                if (block % 6 == 5)
                    processor.updateTuner();   // about 30 times a second, as its timer does
            }
        }
    }

    // PARAGON_SNAPSHOT_EQ=1: the EQ on with a typical mix-bus set of bands
    // (a low cut, a mud cut, a dynamic presence lift, air, a side-only high shelf), after
    // a few seconds of noise shaped like music (falling 3 dB an octave), so
    // the analyser has something to show. PARAGON_SNAPSHOT_PAGE=eq opens
    // the EQ's page; PARAGON_SNAPSHOT_EQ_BAND=n selects band n (from 1).
    const auto showEq = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EQ", {}) == "1";
    juce::Random random (7);
    double pinkState[2][3] {};
    auto referenceTone = false;   // EQ Match's snapshot: brighter and thinner than the track
    double lowState[2] {};
    const auto playNoise = [&] (int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < blocks; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                {
                    // Paul Kellet's economy pink filter on white noise.
                    auto& b = pinkState[ch];
                    const auto white = random.nextFloat() * 2.0 - 1.0;
                    b[0] = 0.99765 * b[0] + white * 0.0990460;
                    b[1] = 0.96300 * b[1] + white * 0.2965164;
                    b[2] = 0.57000 * b[2] + white * 1.0526913;
                    auto sample = 0.05 * (b[0] + b[1] + b[2] + white * 0.1848);
                    if (referenceTone)
                    {
                        lowState[ch] += 0.03 * (sample - lowState[ch]);   // the lows (below about 230 Hz)...
                        sample = sample - 0.6 * lowState[ch] + 0.012 * white;   // ...turned down, and more top
                    }
                    buffer.setSample (ch, i, (float) sample);
                }
            processor.processBlock (buffer, midi);
        }
    };
    if (showEq)
    {
        EqBands bands (processor);
        processor.getParameters().getParameter ("eqOn")->setValueNotifyingHost (1.0f);
        bands.create (eq::Shape::lowCut, 32.0, 0.0);
        bands.set (0, "Slope", 3.0f);   // 24 dB/oct
        bands.create (eq::Shape::bell, 280.0, -3.5);
        bands.set (1, "Q", 1.6f);
        bands.create (eq::Shape::bell, 3200.0, 2.5, -5.0);   // dynamic: pulls back when the presence gets loud
        bands.set (2, "DynAuto", 0.0f);
        bands.set (2, "Thresh", -42.0f);
        bands.create (eq::Shape::highShelf, 11000.0, 3.0);
        bands.set (3, "Q", 0.7f);
        bands.create (eq::Shape::highShelf, 6000.0, 2.0);
        bands.set (4, "Place", (float) eq::Placement::side);
        bands.create (eq::Shape::notch, 120.0, 0.0);
        bands.set (5, "Q", 8.0f);
        bands.set (5, "On", 0.0f);   // switched off: shown hollow

        playNoise (300);
    }

    // PARAGON_SNAPSHOT_EXPRESSION=0..1 sets the expression pedal's position.
    if (const auto pedal = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EXPRESSION", {}); pedal.isNotEmpty())
        processor.getParameters().getParameter ("expression")->setValueNotifyingHost (pedal.getFloatValue());

    // PARAGON_SNAPSHOT_PARAMS=id=value,id=value sets any parameters (plain values, e.g. modType=8).
    for (const auto& setting : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PARAMS", {}), ",", {}))
        if (auto* p = processor.getParameters().getParameter (setting.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (showTuner)
        for (int i = 0; i < 12; ++i)   // let the editor's timers run: the tuner reads a few times
        {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep (40);
        }
    // PARAGON_SNAPSHOT_RACK=1: the four rack units at work (0.18.0): the
    // "Modern Metal Mix" rig (ECHELON, DOMINION and CONSTRICTOR) with the
    // LIMITER on (MODERN, 8 dB in), a drum loop over a noise bed through it,
    // and the editor's timers run so the ladders and screens read it.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_RACK", {}) == "1")
    {
        processor.setPlayConfigDetails (2, 2, 48000.0, 256);
        processor.prepareToPlay (48000.0, 256);
        if (const auto* rig = processor.getRigLibrary().findFactory ("Modern Metal Mix"))
            processor.loadRig (*rig);
        const auto set = [&] (const juce::String& id, float value)
        {
            auto* p = processor.getParameters().getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set (ParamIDs::limitOn, 1.0f);
        set (ParamIDs::limitMode, 8.0f);   // Modern
        set (ParamIDs::limitTruePeak, 1.0f);
        set (ParamIDs::limitGain, 8.0f);
        juce::Random random (9);
        double b[3] {};
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        for (int block = 0, n = 0; block < 1500; ++block)
        {
            for (int i = 0; i < 256; ++i, ++n)
            {
                const auto white = random.nextFloat() * 2.0 - 1.0;
                b[0] = 0.99765 * b[0] + white * 0.0990460;
                b[1] = 0.96300 * b[1] + white * 0.2965164;
                b[2] = 0.57000 * b[2] + white * 1.0526913;
                const auto t = n / 48000.0;
                const auto beat = std::fmod (t, 0.5), half = std::fmod (t + 0.25, 1.0);
                const auto kick = 0.5 * std::exp (-beat * 14.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 60.0 * std::exp (-beat * 40.0)) * beat);
                const auto snare = half < 0.2 ? 0.35 * std::exp (-half * 22.0) * white : 0.0;
                const auto bed = 0.08 * (b[0] + b[1] + b[2] + white * 0.1848);
                const auto x = kick + snare + bed;
                buffer.setSample (0, i, (float) x);
                buffer.setSample (1, i, (float) (x * 0.9));
            }
            processor.processBlock (buffer, midi);
            if (block > 1400 && block % 10 == 0)   // the timers (30 Hz) fire only when they're due
            {
                juce::Thread::sleep (35);
                juce::Timer::callPendingTimersSynchronously();
            }
        }
    }
    if (showPresets)
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "presetsTab")))
            tab->onClick();
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "settings")
        if (auto* tab = dynamic_cast<juce::Button*> (findById (*editor, "settingsTab")))
            tab->onClick();
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "eq")
    {
        // Its rack unit's EDIT opens its page.
        std::vector<RackUnit*> racks;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
        {
            if (auto* rack = dynamic_cast<RackUnit*> (&c))
                racks.push_back (rack);
            for (auto* child : c.getChildren())
                collect (*child);
        };
        collect (*editor);
        REQUIRE (racks.size() == 4);
        racks[0]->getEdit().onClick();
    }
    if (auto* display = findType<EqDisplay> (*editor))
    {
        if (const auto band = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_EQ_BAND", {}).getIntValue(); band > 0)
            display->select (band - 1);
        if (showEq)
            for (int i = 0; i < 20; ++i)   // audio and the analyser's updates, as they come in a DAW
            {
                playNoise (6);
                display->refresh();
            }
    }
    // PARAGON_SNAPSHOT_PAGE=dominion opens DOMINION's page (stomp 5's name).
    // PARAGON_SNAPSHOT_DOMINION=1: DOMINION on, a few depth-curve bands, and
    // pink noise with three resonances (180 Hz boom, 2.5 kHz ring, 6.3 kHz
    // whistle) through it, so the graph shows its cuts.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "dominion")
    {
        // Its rack unit's EDIT opens its page.
        std::vector<RackUnit*> racks;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
        {
            if (auto* rack = dynamic_cast<RackUnit*> (&c))
                racks.push_back (rack);
            for (auto* child : c.getChildren())
                collect (*child);
        };
        collect (*editor);
        REQUIRE (racks.size() == 4);
        racks[1]->getEdit().onClick();
        if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_DOMINION", {}) == "1")
            if (auto* display = findType<DominionDisplay> (*editor))
            {
                processor.getParameters().getParameter (ParamIDs::domOn)->setValueNotifyingHost (1.0f);
                // PARAGON_SNAPSHOT_DOMINION_PRESET=name loads one of its factory presets instead of the three bands.
                const auto presetName = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_DOMINION_PRESET", {});
                if (const auto* preset = processor.getDominionPresets().findFactory (presetName))
                    processor.loadDominionPreset (*preset);
                else
                {
                    display->createBand (dominion::Shape::lowCut, 60.0, 0.0);
                    display->createBand (dominion::Shape::bell, 2800.0, 6.0);
                    display->createBand (dominion::Shape::highShelf, 9000.0, -6.0);
                }
                juce::Random random (3);
                double b[3] {};
                juce::AudioBuffer<float> buffer (2, 256);
                juce::MidiBuffer midi;
                for (int block = 0, n = 0; block < 900; ++block)
                {
                    for (int i = 0; i < 256; ++i, ++n)
                    {
                        const auto white = random.nextFloat() * 2.0 - 1.0;
                        b[0] = 0.99765 * b[0] + white * 0.0990460;
                        b[1] = 0.96300 * b[1] + white * 0.2965164;
                        b[2] = 0.57000 * b[2] + white * 1.0526913;
                        const auto t = n / 48000.0;
                        const auto x = 0.05 * (b[0] + b[1] + b[2] + white * 0.1848)
                                     + 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * 180.0 * t)
                                     + 0.10 * std::sin (2.0 * juce::MathConstants<double>::pi * 2500.0 * t)
                                     + 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 6300.0 * t);
                        buffer.setSample (0, i, (float) x);
                        buffer.setSample (1, i, (float) x);
                    }
                    processor.processBlock (buffer, midi);
                }
                display->select (1);
                display->refresh();
            }
    }

    // PARAGON_SNAPSHOT_PAGE=comp opens COMP's page (stomp 2's name).
    // PARAGON_SNAPSHOT_COMP=1: COMP on with four bands (the lows tightened,
    // mud tamed, harshness tamed, the air brought up) and pink noise
    // with a loud kick-like 60 Hz and a harsh 3 kHz through it, band 3 selected.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "comp")
    {
        // Its rack unit's EDIT opens its page.
        std::vector<RackUnit*> racks;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
        {
            if (auto* rack = dynamic_cast<RackUnit*> (&c))
                racks.push_back (rack);
            for (auto* child : c.getChildren())
                collect (*child);
        };
        collect (*editor);
        REQUIRE (racks.size() == 4);
        racks[2]->getEdit().onClick();
        if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_COMP", {}) == "1")
            if (auto* page = findType<CompPage> (*editor))
            {
                auto& display = page->getDisplay();
                const auto set = [&] (const juce::String& id, float value)
                {
                    auto* p = processor.getParameters().getParameter (id);
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                };
                set (ParamIDs::compOn, 1.0f);
                // PARAGON_SNAPSHOT_COMP_PRESET=name: one of COMP's factory presets instead of the four bands.
                const auto presetName = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_COMP_PRESET", {});
                const auto* preset = processor.getCompPresets().findFactory (presetName);
                if (preset != nullptr)
                    processor.loadCompPreset (*preset);
                struct Spec { float low, high, threshold, range; int mode, stereo; };
                const Spec specs[] = { { 20.0f, 120.0f, -30.0f, -8.0f, 0, 0 }, { 180.0f, 500.0f, -36.0f, -5.0f, 0, 0 },
                                       { 2000.0f, 6000.0f, -40.0f, -9.0f, 0, 0 }, { 9000.0f, 20000.0f, -40.0f, 4.0f, 0, 0 } };
                for (const auto& spec : specs)
                    if (const auto band = preset != nullptr ? -1 : display.createBand (spec.low, spec.high); band >= 0)
                    {
                        display.setBand (band, "Thresh", spec.threshold);
                        display.setBand (band, "Range", spec.range);
                        display.setBand (band, "Mode", (float) spec.mode);
                        display.setBand (band, "Stereo", (float) spec.stereo);
                    }
                juce::Random random (5);
                double b[3] {};
                juce::AudioBuffer<float> buffer (2, 256);
                juce::MidiBuffer midi;
                for (int block = 0, n = 0; block < 400; ++block)
                {
                    for (int i = 0; i < 256; ++i, ++n)
                    {
                        const auto white = random.nextFloat() * 2.0 - 1.0;
                        b[0] = 0.99765 * b[0] + white * 0.0990460;
                        b[1] = 0.96300 * b[1] + white * 0.2965164;
                        b[2] = 0.57000 * b[2] + white * 1.0526913;
                        const auto t = n / 48000.0;
                        const auto x = 0.05 * (b[0] + b[1] + b[2] + white * 0.1848)
                                     + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t)
                                     + 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t);
                        buffer.setSample (0, i, (float) x);
                        buffer.setSample (1, i, (float) (x * 0.6));
                    }
                    processor.processBlock (buffer, midi);
                }
                display.select (preset != nullptr ? 0 : 2);
                display.refresh();
            }
    }

    // PARAGON_SNAPSHOT_PAGE=limiter opens the LIMITER's page (stomp 7's name).
    // PARAGON_SNAPSHOT_LIMITER=1: on (MODERN, TRUE PEAK, -1 dBTP), with eight
    // seconds of a drum loop over a noise bed driven 8 dB into it.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "limiter")
    {
        // Its rack unit's EDIT opens its page.
        std::vector<RackUnit*> racks;
        std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
        {
            if (auto* rack = dynamic_cast<RackUnit*> (&c))
                racks.push_back (rack);
            for (auto* child : c.getChildren())
                collect (*child);
        };
        collect (*editor);
        REQUIRE (racks.size() == 4);
        racks[3]->getEdit().onClick();
        if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_LIMITER", {}) == "1")
            if (auto* page = findType<LimiterPage> (*editor))
            {
                const auto set = [&] (const juce::String& id, float value)
                {
                    auto* p = processor.getParameters().getParameter (id);
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                };
                set (ParamIDs::limitOn, 1.0f);
                set (ParamIDs::limitMode, 8.0f);   // Modern
                set (ParamIDs::limitTruePeak, 1.0f);
                set (ParamIDs::limitGain, 8.0f);
                set (ParamIDs::limitCeiling, -1.0f);
                juce::Random random (9);
                double b[3] {};
                juce::AudioBuffer<float> buffer (2, 256);
                juce::MidiBuffer midi;
                for (int block = 0, n = 0; block < 1500; ++block)
                {
                    for (int i = 0; i < 256; ++i, ++n)
                    {
                        const auto white = random.nextFloat() * 2.0 - 1.0;
                        b[0] = 0.99765 * b[0] + white * 0.0990460;
                        b[1] = 0.96300 * b[1] + white * 0.2965164;
                        b[2] = 0.57000 * b[2] + white * 1.0526913;
                        const auto t = n / 48000.0;
                        const auto beat = std::fmod (t, 0.5), half = std::fmod (t + 0.25, 1.0);
                        const auto kick = 0.6 * std::exp (-beat * 14.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 60.0 * std::exp (-beat * 40.0)) * beat);
                        const auto snare = half < 0.2 ? 0.45 * std::exp (-half * 22.0) * white : 0.0;
                        const auto bed = 0.04 * (1.0 + 0.5 * std::sin (t * 0.9)) * (b[0] + b[1] + b[2] + white * 0.1848);
                        const auto x = kick + snare + bed;
                        buffer.setSample (0, i, (float) x);
                        buffer.setSample (1, i, (float) (x * 0.9));
                    }
                    processor.processBlock (buffer, midi);
                    if (block % 40 == 0)
                    {
                        page->getDisplay().refresh();
                        page->refresh();
                    }
                }
                page->getDisplay().refresh();
                page->refresh();
            }
    }

    // PARAGON_SNAPSHOT_PAGE=oracle opens ORACLE's page (stomp 1's name).
    // PARAGON_SNAPSHOT_ORACLE=1: ORACLE on, having heard a sung line as a
    // DAW played it at 100 BPM from bar 2 (scoops into some notes, vibrato
    // on the long ones, some sharp or flat), with one note selected.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_PAGE", {}) == "oracle")
    {
        if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_ORACLE", {}) == "1")
        {
            struct Transport final : juce::AudioPlayHead
            {
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setTimeInSamples (sample);
                    info.setBpm (100.0);
                    info.setPpqPosition ((double) sample / 48000.0 * 100.0 / 60.0);
                    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                    info.setIsPlaying (true);
                    return info;
                }
                juce::int64 sample = 0;
            } transport;
            processor.setPlayHead (&transport);
            processor.getParameters().getParameter (ParamIDs::tunerOn)->setValueNotifyingHost (1.0f);

            // beats, MIDI note, cents off, vibrato (cents), scoop from below (semitones); note 0 is a rest.
            struct Sung { double beats, note, cents, vibrato, scoop; };
            const Sung line[] = { { 1.0, 0, 0, 0, 0 }, { 0.5, 64, 4, 0, 0 }, { 0.5, 67, -6, 0, 1.0 }, { 1.0, 69, 8, 0, 0 },
                                  { 0.5, 67, 0, 0, 0 }, { 1.5, 64, -14, 30, 0 }, { 0.5, 0, 0, 0, 0 }, { 0.5, 62, 2, 0, 0 },
                                  { 0.5, 64, 6, 0, 0 }, { 0.5, 67, 18, 0, 2.0 }, { 0.5, 67, 10, 0, 0 }, { 2.0, 72, -4, 40, 1.0 },
                                  { 0.5, 0, 0, 0, 0 }, { 0.5, 71, -8, 0, 0 }, { 0.5, 69, 0, 0, 0 }, { 1.0, 67, 12, 20, 0 },
                                  { 0.5, 64, -3, 0, 0 }, { 2.5, 62, -10, 35, 0.5 }, { 1.0, 0, 0, 0, 0 } };
            transport.sample = (juce::int64) (60.0 / 100.0 * 4.0 * 48000.0);   // bar 2
            juce::AudioBuffer<float> buffer (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0;
            int block = 0;
            for (const auto& sung : line)
            {
                const auto length = sung.beats * 0.6;
                const auto samples = (int) (length * 48000.0);
                for (int done = 0; done < samples; done += 256, ++block)
                {
                    buffer.clear();
                    for (int i = 0; i < 256; ++i)
                    {
                        const auto t = (done + i) / 48000.0;
                        if (sung.note <= 0.0 || done + i >= samples)
                            continue;
                        const auto scoop = sung.scoop * std::exp (-t / 0.04);
                        const auto vibrato = sung.vibrato * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t) * juce::jmin (1.0, t / 0.3);
                        const auto pitch = sung.note + (sung.cents + vibrato) / 100.0 - scoop;
                        phase += juce::MathConstants<double>::twoPi * 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0) / 48000.0;
                        const auto envelope = juce::jmin (1.0, t / 0.01, (length - t) / 0.03);
                        buffer.setSample (0, i, (float) (envelope * (0.22 * std::sin (phase) + 0.1 * std::sin (2.0 * phase) + 0.04 * std::sin (3.0 * phase))));
                    }
                    processor.processBlock (buffer, midi);
                    transport.sample += 256;
                    if (block % 6 == 5)
                    {
                        processor.updateTuner();
                        processor.updateOracle();
                    }
                }
            }
            processor.updateOracle();
            processor.setPlayHead (nullptr);
        }

        // ORACLE's key, beside BYPASS, opens its page.
        if (auto* key = dynamic_cast<juce::Button*> (findById (*editor, "oracleKey")))
            key->onClick();
        if (auto* page = findType<OraclePage> (*editor))
        {
            page->getRoll().fit();
            const auto& notes = processor.getOracle().getNotes();
            if (notes.size() > 5)
                page->getRoll().setSelection ({ notes[5].id });   // the long, flat one with vibrato
        }
    }

    // PARAGON_SNAPSHOT_MATCH=1 (with PAGE=eq): EQ Match open, having heard
    // pink noise as the track and a brighter, thinner noise as the reference.
    if (juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_MATCH", {}) == "1")
        if (auto* page = findType<EqPage> (*editor))
        {
            page->showMatch (true);
            auto& match = processor.getEqMatch();
            const auto listen = [&] (eq::match::Session::Recording what, bool reference)
            {
                match.setRecording (what);
                referenceTone = reference;
                for (int i = 0; i < 28; ++i)   // 5 seconds, read as the timer would
                {
                    playNoise (32);
                    match.update (processor.getEqInputTap());
                }
            };
            listen (eq::match::Session::Recording::track, false);
            listen (eq::match::Session::Recording::reference, true);
            match.setRecording (eq::match::Session::Recording::none);
            referenceTone = false;
            page->getMatchPanel().refresh();
            if (auto* display = findType<EqDisplay> (*editor))
                display->refresh();
        }

    // PARAGON_SNAPSHOT_BLOCK=n opens block n's panel in the dock (0 = the EQ overview, 1 = DOMINION's page, 2 = COMP, 3 = the LIMITER, 4 = the TUNER panel).
    if (const auto block = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_BLOCK", {}); block.isNotEmpty())
        if (auto* chain = findType<SignalChain> (*editor))
            chain->onSelect (block.getIntValue());
    if (const auto width = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_WIDTH", {}).getIntValue(); width > 0)
        editor->setSize (width, width * 9 / 16);
    editor->resized();
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

// PARAGON_SNAPSHOT=menu.png ./ParagonX1Tests "[snapshot-menu]": a drop-down
// menu as the plugin draws it (menus open in their own window, so the editor
// snapshot can't show one): DOMINION's band shapes, Bell ticked, one highlighted.
TEST_CASE ("Render a drop-down menu to a PNG", "[.][snapshot-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    ParagonLookAndFeel look;
    const juce::StringArray items { "Low Cut", "Low Shelf", "High Shelf", "High Cut", "Bell", "Band Pass", "Band Reject", "Tilt" };
    constexpr int scale = 2, width = 150, itemHeight = 26;
    juce::Image image (juce::Image::ARGB, width * scale, (itemHeight * items.size() + 8) * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    look.drawPopupMenuBackground (g, width, itemHeight * items.size() + 8);
    for (int i = 0; i < items.size(); ++i)
        look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, i == 2, items[i] == "Bell", false,
                                items[i], {}, nullptr, nullptr);

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

// PARAGON_SNAPSHOT=menu.png ./ParagonX1Tests "[snapshot-preset-menu]": COMP's
// PRESET menu from its GENRES header on (0.15.0), with one genre's submenu
// open beside it, as the plugin's look-and-feel draws them.
TEST_CASE ("Render COMP's genre preset submenus to a PNG", "[.][snapshot-preset-menu]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT", "menu.png");
    const auto openGenre = juce::SystemStats::getEnvironmentVariable ("PARAGON_SNAPSHOT_GENRE", "Djent");
    ParagonLookAndFeel look;
    juce::StringArray genres, inGenre;
    for (const auto& preset : CompPresets::factory())
        if (preset.category.startsWith ("Genres/"))
        {
            genres.addIfNotAlreadyThere (preset.category.fromFirstOccurrenceOf ("/", false, false));
            if (preset.category == "Genres/" + openGenre)
                inGenre.add (preset.name);
        }

    constexpr int scale = 2, width = 190, itemHeight = 26, headerHeight = 26;
    const auto mainHeight = headerHeight + itemHeight * genres.size() + 8;
    juce::Image image (juce::Image::ARGB, width * 2 * scale, mainHeight * scale, true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale ((float) scale));
    g.fillAll (juce::Colours::darkgrey);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (0, 0, width, mainHeight);
        look.drawPopupMenuBackground (g, width, mainHeight);
        look.drawPopupMenuSectionHeader (g, { 0, 4, width, headerHeight }, "GENRES");
        for (int i = 0; i < genres.size(); ++i)
            look.drawPopupMenuItem (g, { 0, 4 + headerHeight + i * itemHeight, width, itemHeight }, false, true,
                                    genres[i] == openGenre, false, true, genres[i], {}, nullptr, nullptr);
    }
    {
        const auto row = genres.indexOf (openGenre);
        const auto subHeight = itemHeight * inGenre.size() + 8;
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (width, juce::jmin (headerHeight + row * itemHeight, mainHeight - subHeight));
        g.reduceClipRegion (0, 0, width, subHeight);   // the background fills its clip
        look.drawPopupMenuBackground (g, width, subHeight);
        for (int i = 0; i < inGenre.size(); ++i)
            look.drawPopupMenuItem (g, { 0, 4 + i * itemHeight, width, itemHeight }, false, true, false, i == 0, false,
                                    inGenre[i], {}, nullptr, nullptr);
    }

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    REQUIRE (stream.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
