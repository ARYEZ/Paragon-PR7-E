#include "Parameters.h"

#include "dsp/effects/MultibandComp.h"

#include "dsp/dominion/Dominion.h"
#include "dsp/effects/MasterLimiter.h"
#include "dsp/effects/Mars.h"
#include "dsp/eq/Dynamics.h"
#include "dsp/eq/EqDesign.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asFrequency = [] (float hz, int)
    {
        return hz < 1000.0f ? juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    };
    const ToText asQ = [] (float q, int) { return juce::String (q, q < 10.0f ? 2 : 1); };

    // A range whose knob travel is logarithmic (frequency, Q): each octave
    // takes the same distance.
    Range logRange (double min, double max)
    {
        return { (float) min, (float) max,
                 [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                 [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
    }

    // From PR7-C (Parameters.cpp), for COMP's RATIO: a knob whose printed
    // marks sit evenly round it: between two marks the travel is even in log
    // (or straight, from a mark of 0). `snap` tidies a value (the SOFT stretch).
    Range markedRange (std::vector<float> marks, std::function<float (float)> snap = {})
    {
        const auto steps = (float) (marks.size() - 1);
        auto between = [] (float low, float high, float t) { return low > 0.0f ? low * std::pow (high / low, t) : low + (high - low) * t; };
        auto within = [] (float low, float high, float value) { return low > 0.0f ? std::log (value / low) / std::log (high / low) : (value - low) / (high - low); };
        return { marks.front(), marks.back(),
                 [marks, steps, between] (float, float, float proportion)
                 {
                     const auto position = juce::jlimit (0.0f, 1.0f, proportion) * steps;
                     const auto below = juce::jmin ((int) position, (int) steps - 1);
                     return between (marks[(size_t) below], marks[(size_t) below + 1], position - (float) below);
                 },
                 [marks, steps, within] (float, float, float value)
                 {
                     for (size_t i = 1; i < marks.size(); ++i)
                         if (value <= marks[i])
                             return ((float) (i - 1) + within (marks[i - 1], marks[i], juce::jmax (value, marks[i - 1]))) / steps;
                     return 1.0f;
                 },
                 [marks, snap] (float, float, float value)
                 {
                     value = juce::jlimit (marks.front(), marks.back(), value);
                     return snap ? snap (value) : value;
                 } };
    }

    // RATIO: SOFT, then 1.5:1 to infinite. Below 1.25 is the SOFT position.
    Range ratioRange()
    {
        using ParamRanges::compRatioMarks;
        return markedRange ({ std::begin (compRatioMarks), std::end (compRatioMarks) },
                            [] (float ratio) { return ratio < 1.25f ? 1.0f : ratio; });
    }

    const ToText asRatio = [] (float ratio, int)
    {
        if (ratio < 1.25f)
            return juce::String ("Soft");
        if (ratio >= 29.5f)
            return juce::String ("Inf:1");
        return juce::String (ratio, ratio < 10.0f ? 1 : 0) + ":1";
    };

    const ToText asMs = [] (float ms, int)
    {
        return ms < 1000.0f ? juce::String (ms, ms < 10.0f ? 1 : 0) + " ms" : juce::String (ms / 1000.0f, 2) + " s";
    };

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const juce::String& name, Range range,
                                                          float defaultValue, int versionHint, const ToText& toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, versionHint }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
    }

    // Version hint 1: tells hosts which plugin version introduced the parameter.
    std::unique_ptr<juce::AudioParameterFloat> makeGainParameter (const char* id, const juce::String& name)
    {
        return makeFloat (id, name, { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, 1, asDb);
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const char* id, const juce::String& name, int versionHint,
                                                          bool defaultValue = true)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, versionHint }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* id, const juce::String& name, int versionHint,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, versionHint }, name, choices, defaultIndex);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    // Version hints are the plugin version (in PR7-G's numbering, which
    // PR7-B and PR7-X carried on) that introduced each parameter.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeGainParameter (ParamIDs::inputGain, "Input"));
    layout.add (makeGainParameter (ParamIDs::outputGain, "Output"));

    layout.add (makeFloat (ParamIDs::presence, "Presence", { -10.0f, 10.0f, 0.1f }, 0.0f, 3, asDb));

    // The RIG LEVEL knob on the pedal face, saved with each rig to balance rigs' volumes.
    layout.add (makeFloat (ParamIDs::rigLevel, "Rig Level", { -24.0f, 12.0f, 0.1f }, 0.0f, 4, asDb));

    // The pedal's BYPASS button. It's also the plugin's official bypass, so
    // the DAW's bypass uses it too.
    layout.add (makeSwitch (ParamIDs::bypass, "Bypass", 5, false));

    // The tuner and the expression pedal.
    constexpr int v6 = 6;
    layout.add (makeSwitch (ParamIDs::tunerOn, "Tuner", v6, false));
    // Heel down (0%) by default, where a real pedal rests: assigned to
    // VOLUME it's silent until rocked forward, as a real volume pedal is.
    layout.add (makeFloat (ParamIDs::expression, "Expression", { 0.0f, 1.0f, 0.001f }, 0.0f, v6, asPercent));
    layout.add (makeChoice (ParamIDs::expressionTarget, "Expression Target", v6, ExpressionTargets::names, ExpressionTargets::off));
    layout.add (makeFloat (ParamIDs::expressionMin, "Expression Min", { 0.0f, 1.0f, 0.01f }, 0.0f, v6, asPercent));
    layout.add (makeFloat (ParamIDs::expressionMax, "Expression Max", { 0.0f, 1.0f, 0.01f }, 1.0f, v6, asPercent));

    // Stomp 4, the EQ (PR7-E 0.4.0): 24 band slots, each unused until a
    // band is created in it, then OUTPUT, GAIN SCALE and AUTO GAIN. eqOn
    // (version 8) was the graphic EQ's switch; it's the stomp's now.
    constexpr int v8 = 8, v30 = 30;
    layout.add (makeSwitch (ParamIDs::eqOn, "Echelon On", v8, false));
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "Echelon Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("Used"), v30 }, name ("Used"), false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("On"), v30 }, name ("On"), true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Shape"), v30 }, name ("Shape"), eq::shapeNames, 0));
        layout.add (makeFloat (id ("Freq").toRawUTF8(), name ("Frequency"), logRange (eq::minFrequency, eq::maxFrequency),
                               (float) defaults.frequency, v30, asFrequency));
        layout.add (makeFloat (id ("Gain").toRawUTF8(), name ("Gain"), { (float) -eq::maxGainDb, (float) eq::maxGainDb, 0.01f },
                               0.0f, v30, asDb));
        layout.add (makeFloat (id ("Q").toRawUTF8(), name ("Q"), logRange (eq::minQ, eq::maxQ), (float) defaults.q, v30, asQ));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Slope"), v30 }, name ("Slope"), eq::slopeNames, eq::defaultSlope));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id ("Place"), v30 }, name ("Placement"), eq::placementNames, 0));
    }
    // Dynamic EQ (0.5.0), each band's, after all the bands' settings above
    // so those keep their places in the host's list.
    constexpr int v31 = 31;
    for (int band = 0; band < ParamIDs::eqNumBands; ++band)
    {
        const auto id = [band] (const char* what) { return ParamIDs::eqBand (band, what); };
        const auto name = [band] (const juce::String& what) { return "Echelon Band " + juce::String (band + 1) + " " + what; };
        const eq::Band defaults;
        layout.add (makeFloat (id ("Dyn").toRawUTF8(), name ("Dynamic Range"),
                               { (float) -eq::dynamics::maxRangeDb, (float) eq::dynamics::maxRangeDb, 0.01f }, 0.0f, v31, asDb));
        layout.add (makeSwitch (id ("DynAuto").toRawUTF8(), name ("Auto Threshold"), v31, defaults.autoThreshold));
        layout.add (makeFloat (id ("Thresh").toRawUTF8(), name ("Threshold"),
                               { (float) eq::dynamics::minThresholdDb, (float) eq::dynamics::maxThresholdDb, 0.1f },
                               (float) defaults.thresholdDb, v31, asDb));
        layout.add (makeFloat (id ("Attack").toRawUTF8(), name ("Attack"), { 0.0f, 1.0f, 0.01f }, (float) defaults.attack, v31, asPercent));
        layout.add (makeFloat (id ("Release").toRawUTF8(), name ("Release"), { 0.0f, 1.0f, 0.01f }, (float) defaults.release, v31, asPercent));
    }
    // Stomp 2, COMP (0.9.0): ranges and defaults as on PR7-C.
    {
        using namespace ParamRanges;
        constexpr int v33 = 33;
        layout.add (makeSwitch (ParamIDs::compOn, "Constrictor", v33, false));
        layout.add (makeFloat (ParamIDs::compThreshold, "Constrictor Threshold", { compThresholdMin, compThresholdMax, 0.1f }, -18.0f, v33, asDb));
        layout.add (makeFloat (ParamIDs::compRatio, "Constrictor Ratio", ratioRange(), 4.0f, v33, asRatio));
        layout.add (makeFloat (ParamIDs::compAttack, "Constrictor Attack", logRange (compAttackMin, compAttackMax), 10.0f, v33, asMs));
        layout.add (makeFloat (ParamIDs::compRelease, "Constrictor Release", logRange (compReleaseMin, compReleaseMax), 300.0f, v33, asMs));
        layout.add (makeSwitch (ParamIDs::compAuto, "Constrictor Auto Release", v33, false));
        layout.add (makeFloat (ParamIDs::compMakeUp, "Constrictor Make Up", { 0.0f, compMakeUpMax, 0.1f }, 0.0f, v33, asDb));
        layout.add (makeChoice (ParamIDs::compCharacter, "Constrictor Character", v33, compCharacters, 0));
        layout.add (makeSwitch (ParamIDs::compLink, "Constrictor Link", v33, true));
    }
    // COMP's bands (0.13.0).
    {
        namespace mb = effects::multiband;
        constexpr int v35 = 35;
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText ratio = [] (float r, int) { return r >= 29.5f ? juce::String ("Inf:1") : juce::String (r, r < 10.0f ? 1 : 0) + ":1"; };
        const ToText edge = [] (float hz, int)
        {
            if (hz <= mb::minHz * 1.001f || hz >= mb::maxHz * 0.999f)
                return juce::String ("End");
            return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
        };
        juce::StringArray slopeNames;
        for (int i = 0; i < mb::numSlopes; ++i)
            slopeNames.add (eq::slopeNames[i]);
        layout.add (makeSwitch (ParamIDs::compWide, "Constrictor Full Band", v35, true));
        layout.add (makeSwitch (ParamIDs::mbLookahead, "Constrictor Look-ahead", v35, false));
        layout.add (makeFloat (ParamIDs::mbMix, "Constrictor Bands Mix", { 0.0f, 200.0f, 1.0f }, 100.0f, v35, percent));
        for (int band = 0; band < ParamIDs::mbNumBands; ++band)
        {
            const auto id = [band] (const char* what) { return ParamIDs::mbBand (band, what); };
            const auto name = [band] (const juce::String& what) { return "Constrictor Band " + juce::String (band + 1) + " " + what; };
            const mb::Band defaults;
            layout.add (makeSwitch (id ("Used").toRawUTF8(), name ("Used"), v35, false));
            layout.add (makeSwitch (id ("On").toRawUTF8(), name ("On"), v35, true));
            layout.add (makeFloat (id ("Low").toRawUTF8(), name ("Low"), logRange (mb::minHz, mb::maxHz), defaults.lowHz, v35, edge));
            layout.add (makeFloat (id ("High").toRawUTF8(), name ("High"), logRange (mb::minHz, mb::maxHz), defaults.highHz, v35, edge));
            layout.add (makeChoice (id ("Slope").toRawUTF8(), name ("Slope"), v35, slopeNames, mb::defaultSlope));
            layout.add (makeFloat (id ("Thresh").toRawUTF8(), name ("Threshold"), { -60.0f, 0.0f, 0.1f }, defaults.thresholdDb, v35, asDb));
            layout.add (makeFloat (id ("Range").toRawUTF8(), name ("Range"), { -mb::maxRangeDb, mb::maxRangeDb, 0.1f }, defaults.rangeDb, v35, asDb));
            layout.add (makeChoice (id ("Mode").toRawUTF8(), name ("Mode"), v35, mb::modeNames, 0));
            layout.add (makeFloat (id ("Ratio").toRawUTF8(), name ("Ratio"), logRange (1.0, 30.0), defaults.ratio, v35, ratio));
            layout.add (makeFloat (id ("Knee").toRawUTF8(), name ("Knee"), { 0.0f, 30.0f, 0.1f }, defaults.kneeDb, v35, asDb));
            layout.add (makeFloat (id ("Attack").toRawUTF8(), name ("Attack"), logRange (0.1, 100.0), defaults.attackMs, v35, asMs));
            layout.add (makeFloat (id ("Release").toRawUTF8(), name ("Release"), logRange (5.0, 3000.0), defaults.releaseMs, v35, asMs));
            layout.add (makeChoice (id ("Char").toRawUTF8(), name ("Character"), v35, ParamRanges::compCharacters, 0));
            layout.add (makeChoice (id ("Stereo").toRawUTF8(), name ("Stereo"), v35, mb::stereoNames, 0));
            layout.add (makeFloat (id ("Link").toRawUTF8(), name ("Link"), { 0.0f, 100.0f, 1.0f }, 100.0f, v35, percent));
            layout.add (makeFloat (id ("Out").toRawUTF8(), name ("Output"), { -24.0f, 24.0f, 0.1f }, 0.0f, v35, asDb));
            layout.add (makeFloat (id ("Look").toRawUTF8(), name ("Look-ahead"), { 0.0f, 20.0f, 0.1f }, 0.0f, v35, asMs));
        }
    }

    // Stomp 5, DOMINION (0.10.0).
    {
        constexpr int v34 = 34;
        const ToText plain = [] (float v, int) { return juce::String (v, 1); };
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText tilt = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)); };
        const ToText maxCut = [] (float v, int) { return v >= (float) dominion::maxCutLimitDb - 0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; };
        layout.add (makeSwitch (ParamIDs::domOn, "Dominion", v34, false));
        layout.add (makeChoice (ParamIDs::domMode, "Dominion Mode", v34, dominion::modeNames, 0));
        layout.add (makeFloat (ParamIDs::domDepth, "Dominion Depth", { 0.0f, 20.0f, 0.01f }, 5.0f, v34, plain));
        layout.add (makeFloat (ParamIDs::domDetail, "Dominion Detail", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domAttack, "Dominion Attack", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domRelease, "Dominion Release", { 0.0f, 100.0f, 0.1f }, 50.0f, v34, percent));
        layout.add (makeChoice (ParamIDs::domStereo, "Dominion Stereo", v34, dominion::stereoNames, 1));
        layout.add (makeFloat (ParamIDs::domLink, "Dominion Link", { 0.0f, 100.0f, 1.0f }, 100.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domFocus, "Dominion Focus", { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        for (const auto* id : { ParamIDs::domDetailLow, ParamIDs::domDetailHigh, ParamIDs::domAttackLow, ParamIDs::domAttackHigh,
                                ParamIDs::domReleaseLow, ParamIDs::domReleaseHigh })
        {
            const auto name = juce::String ("Dominion ") + juce::String (id).fromFirstOccurrenceOf ("dom", false, false);
            layout.add (makeFloat (id, name, { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        }
        layout.add (makeFloat (ParamIDs::domMaxCut, "Dominion Max Cut", { 0.0f, (float) dominion::maxCutLimitDb, 0.1f },
                               (float) dominion::maxCutLimitDb, v34, maxCut));
        layout.add (makeFloat (ParamIDs::domWetTrim, "Dominion Wet Trim", { -12.0f, 12.0f, 0.1f }, 0.0f, v34, asDb));
        layout.add (makeFloat (ParamIDs::domMix, "Dominion Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, v34, percent));
        layout.add (makeFloat (ParamIDs::domOut, "Dominion Out", { -12.0f, 12.0f, 0.1f }, 0.0f, v34, asDb));
        layout.add (makeSwitch (ParamIDs::domDelta, "Dominion Delta", v34, false));
        for (int band = 0; band < ParamIDs::domNumBands; ++band)
        {
            const auto id = [band] (const char* what) { return ParamIDs::domBand (band, what); };
            const auto name = [band] (const juce::String& what) { return "Dominion Band " + juce::String (band + 1) + " " + what; };
            const dominion::Band defaults;
            layout.add (makeSwitch (id ("Used").toRawUTF8(), name ("Used"), v34, false));
            layout.add (makeSwitch (id ("On").toRawUTF8(), name ("On"), v34, true));
            layout.add (makeChoice (id ("Shape").toRawUTF8(), name ("Shape"), v34, dominion::shapeNames, (int) dominion::Shape::bell));
            layout.add (makeFloat (id ("Freq").toRawUTF8(), name ("Frequency"), logRange (20.0, 20000.0), (float) defaults.frequency, v34, asFrequency));
            layout.add (makeFloat (id ("Depth").toRawUTF8(), name ("Depth"), { (float) -dominion::maxDepthDb, (float) dominion::maxDepthDb, 0.01f },
                                   0.0f, v34, asDb));
            layout.add (makeFloat (id ("Q").toRawUTF8(), name ("Q"), logRange (0.1, 10.0), (float) defaults.q, v34, asQ));
            layout.add (makeChoice (id ("Slope").toRawUTF8(), name ("Slope"), v34, eq::slopeNames, eq::defaultSlope));
            layout.add (makeFloat (id ("Focus").toRawUTF8(), name ("Focus"), { -100.0f, 100.0f, 1.0f }, 0.0f, v34, tilt));
        }
    }

    // Stomp 7, the LIMITER (0.8.0): ranges and defaults as on PR7-C.
    constexpr int v32 = 32;
    layout.add (makeSwitch (ParamIDs::limitOn, "Guillotine", v32, false));
    layout.add (makeFloat (ParamIDs::limitThreshold, "Guillotine Threshold", { ParamRanges::limitThresholdMin, 0.0f, 0.1f }, -1.0f, v32, asDb));
    layout.add (makeFloat (ParamIDs::limitMakeUp, "Guillotine Make Up", { 0.0f, ParamRanges::limitMakeUpMax, 0.1f }, 0.0f, v32, asDb));
    layout.add (makeChoice (ParamIDs::limitMode, "Guillotine Style", v32, ParamRanges::limitModes, ParamRanges::limitDefaultMode));
    layout.add (makeFloat (ParamIDs::limitMix, "Guillotine Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, v32,
                           [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
    layout.add (makeSwitch (ParamIDs::limitLink, "Guillotine Link", v32, true));
    layout.add (makeFloat (ParamIDs::limitTrim, "Guillotine Output Trim", { ParamRanges::limitTrimMin, ParamRanges::limitTrimMax, 0.1f }, 0.0f, v32, asDb));

    // ...and from 0.16.0, after Pro-L 2 (see effects::MasterLimiter).
    {
        constexpr int v36 = 36;
        const ToText percent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        const ToText ms = [] (float v, int) { return v < 10.0f ? juce::String (v, 2) + " ms" : v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms"
                                                                                                : juce::String (v / 1000.0f, 2) + " s"; };
        layout.add (makeFloat (ParamIDs::limitGain, "Guillotine Gain", { 0.0f, ParamRanges::limitGainMax, 0.01f }, 0.0f, v36, asDb));
        layout.add (makeFloat (ParamIDs::limitCeiling, "Guillotine Output", { ParamRanges::limitCeilingMin, 0.0f, 0.01f }, -1.0f, v36, asDb));
        layout.add (makeFloat (ParamIDs::limitLookahead, "Guillotine Lookahead", { 0.0f, effects::limiter::maxLookaheadMs, 0.01f, 0.5f }, 1.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitAttack, "Guillotine Attack", logRange (0.5, 500.0), 20.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitRelease, "Guillotine Release", logRange (10.0, 5000.0), 250.0f, v36, ms));
        layout.add (makeFloat (ParamIDs::limitLinkTransient, "Guillotine Link Transients", { 0.0f, 100.0f, 1.0f }, 75.0f, v36, percent));
        layout.add (makeFloat (ParamIDs::limitLinkRelease, "Guillotine Link Release", { 0.0f, 100.0f, 1.0f }, 100.0f, v36, percent));
        layout.add (makeSwitch (ParamIDs::limitTruePeak, "Guillotine True Peak", v36, false));
        layout.add (makeChoice (ParamIDs::limitOversampling, "Guillotine Oversampling", v36, effects::limiter::oversamplingNames, 0));
        layout.add (makeChoice (ParamIDs::limitDither, "Guillotine Dither", v36, effects::limiter::ditherNames, 0));
        layout.add (makeChoice (ParamIDs::limitShaping, "Guillotine Noise Shaping", v36, effects::limiter::shapingNames, 0));
        layout.add (makeSwitch (ParamIDs::limitUnity, "Guillotine Unity Gain", v36, false));
        layout.add (makeSwitch (ParamIDs::limitDelta, "Guillotine Delta", v36, false));
        layout.add (makeSwitch (ParamIDs::limitDcFilter, "Guillotine DC Filter", v36, false));
    }

    // Added in 0.22.0 (version hint 37): MARS, PR7-V's saturation (its
    // parameters as there, its bands renamed marsB...), and STEREO, PR7-C's
    // mono maker and width. Both off by default: nothing changes for old projects.
    {
        constexpr int v37 = 37;
        const auto skewed = [] (float min, float max, float centre, float step)
        {
            Range range { min, max, step };
            range.setSkewForCentre (centre);
            return range;
        };
        const ToText asCrossover = [] (float hz, int) { return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz"; };
        const ToText asDynamics = [] (float value, int)
        {
            const auto amount = juce::roundToInt (std::abs (value) * 100.0f);
            return amount == 0 ? juce::String ("Off") : (value < 0.0f ? "Gate " : "Comp ") + juce::String (amount) + "%";
        };
        const ToText asMarsLevel = [] (float db, int) { return db <= effects::Mars::minLevelDb ? juce::String ("Off") : juce::String (db, 1) + " dB"; };
        const ToText asSignedDb = [] (float db, int) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; };
        layout.add (makeSwitch (ParamIDs::marsOn, "Mars", v37, false));
        layout.add (makeChoice (ParamIDs::marsBands, "Mars Bands", v37, { "1", "2", "3", "4" }, 0));
        layout.add (makeFloat (ParamIDs::marsCross1, "Mars Crossover 1", skewed (30.0f, 16000.0f, 1000.0f, 1.0f), 250.0f, v37, asCrossover));
        layout.add (makeFloat (ParamIDs::marsCross2, "Mars Crossover 2", skewed (30.0f, 16000.0f, 1000.0f, 1.0f), 2500.0f, v37, asCrossover));
        layout.add (makeFloat (ParamIDs::marsCross3, "Mars Crossover 3", skewed (30.0f, 16000.0f, 1000.0f, 1.0f), 7000.0f, v37, asCrossover));
        layout.add (makeChoice (ParamIDs::marsSlope, "Mars Crossover Slope", v37, effects::Mars::slopeNames, (int) effects::Mars::Slope::db24));
        layout.add (makeFloat (ParamIDs::marsInput, "Mars Input", { -24.0f, 24.0f, 0.1f }, 0.0f, v37, asSignedDb));
        layout.add (makeFloat (ParamIDs::marsOutput, "Mars Output", { -24.0f, 24.0f, 0.1f }, 0.0f, v37, asSignedDb));
        layout.add (makeFloat (ParamIDs::marsMix, "Mars Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v37, asPercent));
        layout.add (makeChoice (ParamIDs::marsQuality, "Mars Quality", v37, effects::Mars::qualityNames, (int) effects::Mars::Quality::good));
        for (int band = 0; band < effects::Mars::maxBands; ++band)
        {
            const auto id = [band] (const char* part) { return ParamIDs::marsBand (band, part); };
            const auto name = "Mars Band " + juce::String (band + 1) + " ";
            layout.add (makeSwitch (id ("On").toRawUTF8(), name + "On", v37, true));
            layout.add (makeChoice (id ("Style").toRawUTF8(), name + "Style", v37, effects::Mars::styleNames, (int) effects::Mars::Style::warmTape));
            layout.add (makeFloat (id ("Drive").toRawUTF8(), name + "Drive", { 0.0f, 1.0f, 0.01f }, 0.3f, v37, asPercent));
            layout.add (makeFloat (id ("Dynamics").toRawUTF8(), name + "Dynamics", { -1.0f, 1.0f, 0.01f }, 0.0f, v37, asDynamics));
            layout.add (makeFloat (id ("Feedback").toRawUTF8(), name + "Feedback", { 0.0f, 1.0f, 0.01f }, 0.0f, v37, asPercent));
            layout.add (makeFloat (id ("FbFreq").toRawUTF8(), name + "Feedback Frequency", skewed (50.0f, 10000.0f, 1000.0f, 1.0f), 1000.0f, v37, asCrossover));
            for (const auto* tone : { "Bass", "Mid", "Treble", "Presence" })
                layout.add (makeFloat (id (tone).toRawUTF8(), name + tone, { -12.0f, 12.0f, 0.1f }, 0.0f, v37, asSignedDb));
            layout.add (makeFloat (id ("Mix").toRawUTF8(), name + "Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v37, asPercent));
            layout.add (makeFloat (id ("Level").toRawUTF8(), name + "Level", { effects::Mars::minLevelDb, 12.0f, 0.1f }, 0.0f, v37, asMarsLevel));
        }

        const ToText wholePercent = [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; };
        layout.add (makeSwitch (ParamIDs::stereoOn, "Binary", v37, false));
        layout.add (makeSwitch (ParamIDs::stereoMonoOn, "Binary Mono Maker", v37, true));
        layout.add (makeFloat (ParamIDs::stereoMonoFreq, "Binary Mono Maker Frequency", logRange (20.0, 500.0), 100.0f, v37, asFrequency));
        layout.add (makeSwitch (ParamIDs::stereoWidthOn, "Binary Width On", v37, true));
        layout.add (makeFloat (ParamIDs::stereoWidth, "Binary Width", { 0.0f, 200.0f, 1.0f }, 100.0f, v37, wholePercent));
        layout.add (makeSwitch (ParamIDs::stereoListenMono, "Binary Listen Mono", v37, false));
    }

    layout.add (makeFloat (ParamIDs::eqOutput, "Echelon Output", { -36.0f, 36.0f, 0.01f }, 0.0f, v30, asDb));
    layout.add (makeFloat (ParamIDs::eqGainScale, "Echelon Gain Scale", { 0.0f, 2.0f, 0.01f }, 1.0f, v30, asPercent));
    layout.add (makeSwitch (ParamIDs::eqAutoGain, "Echelon Auto Gain", v30, false));
    return layout;
}
