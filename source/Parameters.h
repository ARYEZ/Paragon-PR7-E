#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
//
// IDs that are gone, never to be reused (a project or rig that still has them
// loads fine: they're ignored):
//   - before 0.1.0 shipped: PR7-B's ampOn, ampNormalise, cabOn, and the bass
//     drives' drive*, gotham* and dual*;
//   - in 0.2.0: tunerMute;
//   - in 0.3.0, with PR7-B's effects (they were mono, and PR7-X is a stereo
//     utility): comp*, gateThreshold, fp*, glitch*, lofi*, mod*, delay*,
//     reverb*, pitchTune, oct*, freeze*;
//   - in 0.4.0, with the graphic EQ the new EQ replaced: eq33, eq80, eq150,
//     eq300, eq500, eq900, eq2k, eq5k, eq9k, eqLevel, eqUltraLo, eqUltraHi.
namespace ParamIDs
{
    inline constexpr auto inputGain  = "inputGain";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto presence   = "presence";    // the PRESENCE knob on the face
    inline constexpr auto rigLevel   = "rigLevel";    // the RIG LEVEL knob on the face
    inline constexpr auto bypass     = "bypass";

    inline constexpr auto tunerOn          = "tunerOn";            // stomp 1
    inline constexpr auto expression       = "expression";        // the pedal's position, 0 heel to 1 toe
    inline constexpr auto expressionTarget = "expressionTarget";  // what the pedal controls
    inline constexpr auto expressionMin    = "expressionMin";     // the target's setting at the heel...
    inline constexpr auto expressionMax    = "expressionMax";     // ...and at the toe

    // Stomp 4, the EQ (inspired by FabFilter Pro-Q 4; see dsp/eq). eqOn is
    // the stomp's switch (before 0.4.0 it switched the graphic EQ).
    inline constexpr auto eqOn        = "eqOn";
    inline constexpr auto eqOutput    = "eqOutput";
    inline constexpr auto eqGainScale = "eqGainScale";
    inline constexpr auto eqAutoGain  = "eqAutoGain";
    inline constexpr int eqNumBands = 24;
    // Each band's settings: "eqB1Used", "eqB1Freq"... `what` is one of the names below.
    // From 0.5.0, its dynamics: Dyn (the range), DynAuto, Thresh, Attack, Release.
    inline juce::String eqBand (int band, const char* what) { return "eqB" + juce::String (band + 1) + what; }
    inline constexpr const char* eqBandSettings[] = { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place",
                                                      "Dyn", "DynAuto", "Thresh", "Attack", "Release" };

    // Stomp 2, COMP (0.9.0): PR7-C's compressor, ported. After the EQ.
    inline constexpr auto compOn        = "compOn";
    inline constexpr auto compThreshold = "compThreshold";   // dBFS
    inline constexpr auto compRatio     = "compRatio";       // 1 = SOFT, 30 = infinite
    inline constexpr auto compAttack    = "compAttack";      // ms
    inline constexpr auto compRelease   = "compRelease";     // ms
    inline constexpr auto compAuto      = "compAuto";        // AUTO release
    inline constexpr auto compMakeUp    = "compMakeUp";      // dB
    inline constexpr auto compCharacter = "compCharacter";   // choice: VCA, OPTO, FET
    inline constexpr auto compLink      = "compLink";        // both channels together
    // COMP's bands (0.13.0): multiband compression and expansion, inspired by
    // FabFilter Pro-MB (our own design, see dsp/effects/MultibandComp.h).
    // The bands come first, then the full-band compressor above, which
    // compWide switches (on by default, so older rigs sound as they did).
    inline constexpr auto compWide    = "compWide";
    inline constexpr auto mbLookahead = "mbLookahead";   // COMP 20 ms late, the bands hearing ahead
    inline constexpr auto mbMix       = "mbMix";         // %, 0 to 200
    inline constexpr int mbNumBands = 6;
    // "mbB1Used", "mbB1Low"... `what` is one of the names below.
    inline juce::String mbBand (int band, const char* what) { return "mbB" + juce::String (band + 1) + what; }
    inline constexpr const char* mbBandSettings[] = { "Used", "On", "Low", "High", "Slope", "Thresh", "Range", "Mode", "Ratio",
                                                      "Knee", "Attack", "Release", "Char", "Stereo", "Link", "Out", "Look" };

    // Stomp 5, DOMINION (0.10.0): dynamic resonance suppression, inspired by
    // oeksound soothe 3 (our own design, see dsp/dominion/Dominion.h). After
    // the EQ, before COMP.
    inline constexpr auto domOn      = "domOn";
    inline constexpr auto domMode    = "domMode";      // choice: Soft, Hard
    inline constexpr auto domDepth   = "domDepth";     // 0 to 20
    inline constexpr auto domDetail  = "domDetail";    // 0 to 100
    inline constexpr auto domAttack  = "domAttack";    // 0 to 100
    inline constexpr auto domRelease = "domRelease";   // 0 to 100
    inline constexpr auto domStereo  = "domStereo";    // choice: L/R, M/S
    inline constexpr auto domLink    = "domLink";      // %
    inline constexpr auto domFocus   = "domFocus";     // -100 to 100
    inline constexpr auto domDetailLow = "domDetailLow", domDetailHigh = "domDetailHigh",
                          domAttackLow = "domAttackLow", domAttackHigh = "domAttackHigh",
                          domReleaseLow = "domReleaseLow", domReleaseHigh = "domReleaseHigh";   // the tilts, -100 to 100
    inline constexpr auto domMaxCut  = "domMaxCut";    // dB (40: no limit)
    inline constexpr auto domWetTrim = "domWetTrim";   // dB
    inline constexpr auto domMix     = "domMix";       // %
    inline constexpr auto domOut     = "domOut";       // dB
    inline constexpr auto domDelta   = "domDelta";
    inline constexpr int domNumBands = 8;
    // The depth curve's bands: "domB1Used", "domB1Freq"... `what` is one of the names below.
    inline juce::String domBand (int band, const char* what) { return "domB" + juce::String (band + 1) + what; }
    inline constexpr const char* domBandSettings[] = { "Used", "On", "Shape", "Freq", "Depth", "Q", "Slope", "Focus" };

    // Stomp 7, the LIMITER (0.8.0): PR7-C's limiter and output trim, ported.
    // The last thing in the chain, after OUTPUT, RIG LEVEL and the volume pedal.
    inline constexpr auto limitOn        = "limitOn";
    inline constexpr auto limitThreshold = "limitThreshold";   // dBFS
    inline constexpr auto limitMakeUp    = "limitMakeUp";      // dB
    inline constexpr auto limitMode      = "limitMode";        // choice: Brick, Soft, Auto
    inline constexpr auto limitMix       = "limitMix";         // %
    inline constexpr auto limitLink      = "limitLink";        // both channels together
    inline constexpr auto limitTrim      = "limitTrim";        // OUTPUT TRIM, dB, after the limiter
    // From 0.16.0, inspired by Pro-L 2 (version hint 36): GAIN drives into the
    // OUTPUT ceiling. limitThreshold and limitMakeUp became GAIN and OUTPUT
    // (projects and rigs from before are converted when they load); they're
    // kept only so old automation and files still find them.
    inline constexpr auto limitGain          = "limitGain";          // dB
    inline constexpr auto limitCeiling       = "limitCeiling";       // OUTPUT, dBFS (dBTP with TRUE PEAK)
    inline constexpr auto limitLookahead     = "limitLookahead";     // ms
    inline constexpr auto limitAttack        = "limitAttack";        // ms, the release stage
    inline constexpr auto limitRelease       = "limitRelease";       // ms
    inline constexpr auto limitLinkTransient = "limitLinkTransient"; // %
    inline constexpr auto limitLinkRelease   = "limitLinkRelease";   // %
    inline constexpr auto limitTruePeak      = "limitTruePeak";
    inline constexpr auto limitOversampling  = "limitOversampling";  // choice: Off, 2x, 4x, 8x
    inline constexpr auto limitDither        = "limitDither";        // choice: Off, 24 ... 16 bits
    inline constexpr auto limitShaping       = "limitShaping";       // choice: None, Basic, Optimized, Weighted
    inline constexpr auto limitUnity         = "limitUnity";         // listening aids: not in rigs
    inline constexpr auto limitDelta         = "limitDelta";
    inline constexpr auto limitDcFilter      = "limitDcFilter";

    // MARS (0.22.0): saturation, PR7-V's MARS (inspired by FabFilter Saturn
    // 2) brought over, one engine per side. After CONSTRICTOR, before STEREO
    // and the LIMITER. (Its modulation isn't exposed yet.)
    inline constexpr auto marsOn      = "marsOn";
    inline constexpr auto marsBands   = "marsBands";     // choice: 1 to 4 bands, by index
    inline constexpr auto marsCross1  = "marsCross1";    // Hz
    inline constexpr auto marsCross2  = "marsCross2";
    inline constexpr auto marsCross3  = "marsCross3";
    inline constexpr auto marsSlope   = "marsSlope";     // choice: effects::Mars::slopeNames
    inline constexpr auto marsInput   = "marsInput";     // dB
    inline constexpr auto marsOutput  = "marsOutput";    // dB
    inline constexpr auto marsMix     = "marsMix";       // 0 to 1
    inline constexpr auto marsQuality = "marsQuality";   // choice: effects::Mars::qualityNames
    // Each of the four bands: "marsB1Drive", "marsB3Style"... (PR7-V's are
    // "mb1Drive": here "mb" is CONSTRICTOR's bands, and its presets hold
    // every "mb" parameter).
    inline const juce::StringArray marsBandParts { "On", "Style", "Drive", "Dynamics", "Feedback", "FbFreq",
                                                   "Bass", "Mid", "Treble", "Presence", "Mix", "Level" };
    inline juce::String marsBand (int band, const juce::String& part) { return "marsB" + juce::String (band + 1) + part; }

    // BINARY (0.22.0; "stereo" in IDs and code): PR7-C's MONO MAKER and WIDTH, after MARS, before the
    // LIMITER; MONO is for listening (not in rigs).
    inline constexpr auto stereoOn       = "stereoOn";
    inline constexpr auto stereoMonoOn   = "stereoMonoOn";
    inline constexpr auto stereoMonoFreq = "stereoMonoFreq";   // Hz
    inline constexpr auto stereoWidthOn  = "stereoWidthOn";
    inline constexpr auto stereoWidth    = "stereoWidth";      // %: 100 = as it was
    inline constexpr auto stereoListenMono = "stereoListenMono";

    // Global settings: kept when switching rigs. Everything else is part of a rig.
    // The expression pedal's position is global for the same reason a real
    // pedal's is: it's where your foot is, not part of the sound. What the
    // pedal is assigned to, and its range, are saved with each rig.
    // The LIMITER's UNITY and DELTA are for listening while you set it, not part of a sound.
    inline const juce::StringArray notInRigs { inputGain, outputGain, bypass, tunerOn, expression, limitUnity, limitDelta, stereoListenMono };
}

namespace ExpressionTargets
{
    // Saved by number: only ever add to the end. Before 0.3.0 the list went
    // on with PR7-B's effects (2 to 8); projects and rigs from then that used
    // those load with the pedal unassigned (see setStateInformation and Rig::fromXml).
    enum { off, volume };
    inline const juce::StringArray names { "Off", "Volume" };
}

namespace ParamRanges
{
    // COMP's, as on PR7-C.
    inline constexpr float compThresholdMin = -50.0f, compThresholdMax = 0.0f;
    inline constexpr float compAttackMin = 0.1f, compAttackMax = 30.0f;          // ms
    inline constexpr float compReleaseMin = 50.0f, compReleaseMax = 3000.0f;     // ms
    inline constexpr float compMakeUpMax = 20.0f;
    inline const juce::StringArray compCharacters { "VCA", "OPTO", "FET" };    // saved by index: only append
    inline constexpr float compRatioMarks[] = { 1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 30.0f };   // SOFT, then to infinite

    // The LIMITER's, as on PR7-C.
    inline constexpr float limitThresholdMin = -20.0f, limitMakeUpMax = 12.0f;
    inline constexpr float limitTrimMin = -12.0f, limitTrimMax = 12.0f;
    // Its styles: PR7-C's three, then (0.16.0) eight of our own after Pro-L 2's (effects::limiter::Style).
    inline const juce::StringArray limitModes { "Brick", "Soft", "Auto",   // saved by index: only append
                                                "Clear", "Punchy", "Dynamic", "All-round", "Aggressive", "Modern", "Glue", "Safe" };
    inline constexpr int limitDefaultMode = 8;   // Modern
    inline constexpr float limitGainMax = 30.0f, limitCeilingMin = -30.0f;

    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 24.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Projects and rigs from before 0.16.0 set the LIMITER with PR7-C's THRESHOLD
// and MAKE UP. Limiting at THRESHOLD then adding MAKE UP sounds exactly like
// driving GAIN (= MAKE UP) into an OUTPUT ceiling at THRESHOLD + MAKE UP; a
// ceiling that would be above 0 dB goes into OUTPUT TRIM instead.
struct ConvertedLimiter { float gain, ceiling, trim; };
inline ConvertedLimiter convertOldLimiter (float threshold, float makeUp, float trim)
{
    auto ceiling = threshold + makeUp;
    if (ceiling > 0.0f)
    {
        trim += ceiling;
        ceiling = 0.0f;
    }
    return { makeUp, juce::jmax (ParamRanges::limitCeilingMin, ceiling), juce::jlimit (ParamRanges::limitTrimMin, ParamRanges::limitTrimMax, trim) };
}
