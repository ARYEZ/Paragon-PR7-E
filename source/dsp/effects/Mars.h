#pragma once

#include "EffectUtils.h"
#include "MarsModulation.h"
#include "PitchShifter.h"

namespace effects
{
    // Brought over unchanged from PR7-V 0.36.0 in PR7-X 0.22.0, where it is
    // a rack unit of its own and runs one engine per side (PR7-X is stereo
    // throughout); the notes below are PR7-V's.
    //
    // MARS: the SATURATION block, a multi-band saturator (the pedal models
    // PR7-G had went in 0.14.0; their settings are converted to MARS).
    // Inspired by FabFilter Saturn 2 (the owner's reference, from its user
    // manual): our own design, following what the manual describes.
    //
    //   INPUT -> split into 1 to 4 BANDS at the CROSSOVERS (Linkwitz-Riley,
    //   6 to 48 dB per octave, so the bands add back up flat) -> each band:
    //
    //       + FEEDBACK (the band's output back into its input, through a
    //         resonance at FEEDBACK FREQ: the ring of a mic near its speaker)
    //       -> DYNAMICS (left: a gate/expander; right: pumping compression)
    //       -> DRIVE into the STYLE's saturation, the level kept even
    //          automatically as DRIVE goes up
    //       -> TONE: BASS, MID, TREBLE, PRESENCE on what the saturation made
    //       -> MIX (the band's clean signal against the saturated one) -> LEVEL
    //
    //   -> the bands added back together -> OUTPUT -> MIX (with the input).
    //
    // The 21 STYLES, in Saturn 2's families: TUBE (clean, warm, saturated,
    // broken), TAPE (clean, warm, saturated, extreme), TRANSFORMER (subtle,
    // warm, lush), AMP (American, British, smooth, crunchy, screaming) and
    // FX (Smudge, Breakdown, Foldback, Rectify, Destroy). The manual says what
    // each family is for, not how it's made, so every curve here is ours.
    //
    // All of it runs oversampled (QUALITY: Off 1x, Good 4x, Superb 8x) with
    // IIR filters, so there's no latency. The SATURATION block is in the
    // mono part of the chain, so there are no pan or mid/side controls.
    //
    // Stage 2: MODULATION (MarsModulation.h): XLFOs, envelope generators and
    // followers, MIDI, XY pads and the expression pedal, moving the knobs
    // through 16 slots.
    class Mars
    {
    public:
        static constexpr int maxBands = 4;

        // Saved by number in rigs and projects: only ever add to the end.
        enum class Style { cleanTube, warmTube, saturatedTube, brokenTube,
                           cleanTape, warmTape, saturatedTape, extremeTape,
                           subtleTransformer, warmTransformer, lushTransformer,
                           americanAmp, britishAmp, smoothAmp, crunchyAmp, screamingAmp,
                           smudge, breakdown, foldback, rectify, destroy };
        static inline const juce::StringArray styleNames {
            "Clean Tube", "Warm Tube", "Saturated Tube", "Broken Tube",
            "Clean Tape", "Warm Tape", "Saturated Tape", "Extreme Tape",
            "Subtle Transformer", "Warm Transformer", "Lush Transformer",
            "American Amp", "British Amp", "Smooth Amp", "Crunchy Amp", "Screaming Amp",
            "Smudge", "Breakdown", "Foldback", "Rectify", "Destroy" };
        static constexpr int numStyles = 21;

        enum class Slope { db6, db12, db24, db48 };
        static inline const juce::StringArray slopeNames { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" };

        enum class Quality { off, good, superb };
        static inline const juce::StringArray qualityNames { "Off (1x)", "Good (4x)", "Superb (8x)" };

        struct Band
        {
            bool on = true;
            Style style = Style::warmTape;
            float drive = 0.3f;        // 0 to 1
            float dynamics = 0.0f;     // -1 (gate) to 1 (compress)
            float feedback = 0.0f;     // 0 to 1
            float feedbackHz = 1000.0f;
            float bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f, presenceDb = 0.0f;
            float mix = 1.0f;          // 0 (clean) to 1 (saturated)
            float levelDb = 0.0f;
        };

        struct Settings
        {
            bool on = false;
            int numBands = 1;
            std::array<float, maxBands - 1> crossoverHz { 250.0f, 2500.0f, 7000.0f };
            Slope slope = Slope::db24;
            float inputDb = 0.0f, outputDb = 0.0f;
            float mix = 1.0f;
            Quality quality = Quality::good;
            std::array<Band, maxBands> bands;
            MarsModulation::Settings modulation;
        };

        static constexpr float maxDriveDb = 36.0f;
        static constexpr float minLevelDb = -36.0f;   // LEVEL at (or below) this: silent

        Mars();
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings,
                      const MarsModulation::Context& context = {}) noexcept;

        // The STYLE's saturation curve with no memory (its static part): for
        // tests, and for the automatic level matching. `drive` 0 to 1.
        static float curve (Style style, float x, float drive) noexcept;
        // DRIVE's gain into the curve (its range depends on the style).
        static float preGain (Style style, float drive) noexcept;

        // How much each band is being turned down by DYNAMICS, in dB (0 or
        // less), and how hard it's driven (its peak into the saturation,
        // 0 to 1+), for the panel. Read from the message thread.
        float getBandDrive (int band) const noexcept { return bandDriveMeter[(size_t) juce::jlimit (0, maxBands - 1, band)].load(); }

        // The modulation as it was at the end of the last block, for the
        // panel: each source's output, how far each target is moved (a
        // fraction of its range), where the XLFOs are, the EGs' levels and
        // the trigger signals' levels (dB: 0 input, 1-4 bands).
        float getSourceValue (int source) const noexcept { return shownSources[(size_t) juce::jlimit (0, MarsModulation::numSources - 1, source)].load(); }
        float getTargetOffset (int target) const noexcept { return shownOffsets[(size_t) juce::jlimit (0, MarsModulation::numTargets - 1, target)].load(); }
        float getLfoPosition (int lfo) const noexcept { return shownLfoPositions[(size_t) juce::jlimit (0, MarsModulation::numLfos - 1, lfo)].load(); }
        float getEgLevel (int eg) const noexcept { return shownEgLevels[(size_t) juce::jlimit (0, MarsModulation::numEgs - 1, eg)].load(); }
        float getTriggerLevelDb (int trigger) const noexcept { return shownTriggerDb[(size_t) juce::jlimit (0, 4, trigger)].load(); }

        // How often the modulation moves the knobs, in samples at the plugin's rate.
        int getTickLength() const noexcept { return tickLength; }

    private:
        // A state-variable filter in double precision: its low, band and
        // high outputs at once. Double, because at 8x the sample rate a 100 Hz
        // filter's numbers are too fine for single precision.
        struct Svf
        {
            double g = 0.0, k = 1.4142, a1 = 0.0, a2 = 0.0, a3 = 0.0, s1 = 0.0, s2 = 0.0;
            void set (double sampleRate, double hz, double q) noexcept;
            void reset() noexcept { s1 = s2 = 0.0; }
            struct Out { double low, band, high; };
            Out process (double x) noexcept;
        };

        // One crossover: its low and high halves, Linkwitz-Riley, so they add
        // up to an all-pass (flat). 6 dB: one pole, the high half the rest.
        struct Crossover
        {
            std::array<Svf, 4> low, high;
            double onePole = 0.0, onePoleState = 0.0;
            Slope slope = Slope::db24;
            void set (double sampleRate, double hz, Slope newSlope) noexcept;
            void reset() noexcept;
            std::pair<double, double> process (double x) noexcept;
            double allPass (double x) noexcept { const auto [l, h] = process (x); return l + h; }
        };

        struct Biquad
        {
            std::array<double, 6> c { 1, 0, 0, 1, 0, 0 };   // b0 b1 b2 a0 a1 a2, as JUCE makes them
            double z1 = 0.0, z2 = 0.0;
            void reset() noexcept { z1 = z2 = 0.0; }
            double process (double x) noexcept;
        };

        // A straight-line glide to a new value over a set number of samples.
        // (Not juce::SmoothedValue: its length can't change without a jump,
        // and MARS's glides are short while modulated, longer otherwise.)
        struct Ramp
        {
            double current = 0.0, target = 0.0, step = 0.0;
            int left = 0;
            void set (double value, int steps) noexcept
            {
                if (juce::exactlyEqual (value, target))
                    return;
                target = value;
                left = juce::jmax (1, steps);
                step = (target - current) / left;
            }
            double next() noexcept
            {
                if (left > 0 && --left == 0)
                    current = target;
                else if (left > 0)
                    current += step;
                return current;
            }
            double getCurrentValue() const noexcept { return current; }
            void jump() noexcept { current = target; left = 0; }
        };

        struct BandState
        {
            Svf feedbackFilter;
            double feedbackOut = 0.0;
            double dynamicsEnvelope = 0.0, dynamicsGainDb = 0.0;
            std::array<Biquad, 4> tone;                 // bass, mid, treble, presence
            std::array<float, 4> toneShown { 99, 99, 99, 99 };
            // Styles with memory.
            double tapeEnvelope = 0.0, previous = 0.0, preEmphasis = 0.0, deEmphasis = 0.0;
            std::array<double, 6> filters {};          // the amps' and transformers' one-pole filters
            std::vector<float> smudgeLine;
            int smudgeWrite = 0;
            double smudgeEnvelope = 0.0;
            std::array<PitchShifter, 3> breakdownShifters;   // one per QUALITY's rate
            double holdValue = 0.0;
            int holdCount = 0;
            Ramp preGain, makeUp, mix, level;   // linear gains, smoothed
        };

        // One-pole filter coefficients at the working rate, worked out when it changes.
        struct Coefficients
        {
            double hz20 = 0, hz30 = 0, hz120 = 0, hz200 = 0, hz300 = 0, hz1500 = 0, hz3000 = 0, hz4000 = 0, hz5500 = 0,
                   hz6000 = 0, hz8000 = 0, hz9000 = 0, hz11000 = 0, hz12000 = 0, hz16000 = 0, attack = 0, release = 0;
        };

        void setRate (int qualityIndex) noexcept;
        // The knobs as MARS plays them now: as set, or moved by the modulation.
        void setKnobs (const Settings&, bool modulated) noexcept;
        void runSegment (juce::dsp::AudioBlock<float> block) noexcept;
        void resetBands() noexcept;
        double processBand (int band, double x, const Band& settings) noexcept;
        double saturate (BandState& state, Style style, double u, float drive) noexcept;
        static double shape (Style style, double u, float drive) noexcept;
        float autoGain (Style style, float drive) const noexcept;

        double baseRate = 48000.0, rate = 48000.0;
        Coefficients coefficients;
        int maxBlock = 512;
        int currentQuality = -1;
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;   // Off (unused), Good, Superb
        std::array<Crossover, maxBands - 1> splits;
        std::array<Crossover, 3> compensation;   // keeps the lower bands in phase with the later splits
        std::array<BandState, maxBands> bandStates;
        std::array<float, maxBands - 1> splitShown { -1, -1, -1 };
        Slope slopeShown = Slope::db24;
        int bandsShown = -1;
        Ramp inputGain, outputGain, mix;
        juce::AudioBuffer<float> dryBuffer;

        // The modulation, and the knobs it moves.
        MarsModulation modulation;
        int tickLength = 16, untilTick = 0;
        bool wasModulated = false, knobsSet = false;   // knobsSet: setKnobs has run since prepare
        int liveBands = 1;
        std::array<Band, maxBands> live;
        static constexpr int numKnobTargets = MarsModulation::firstLfoTarget;   // the bands' and the global knobs
        std::array<juce::NormalisableRange<float>, numKnobTargets> knobRanges;
        std::array<float, numKnobTargets> smoothedKnobs {};
        std::array<float, maxBands> drivePeak {};
        std::array<std::atomic<float>, MarsModulation::numSources> shownSources {};
        std::array<std::atomic<float>, MarsModulation::numTargets> shownOffsets {};
        std::array<std::atomic<float>, MarsModulation::numLfos> shownLfoPositions {};
        std::array<std::atomic<float>, MarsModulation::numEgs> shownEgLevels {};
        std::array<std::atomic<float>, 5> shownTriggerDb {};
        BypassFade bypass;
        juce::Random random;

        // Automatic level matching: for each style, the gain that brings a
        // -10 dB sine back to its level, at 33 DRIVE settings. Measured once
        // (the first time any MARS is prepared) by running the styles
        // themselves, filters and all, and shared.
        using LevelTable = std::array<std::array<float, 33>, numStyles>;
        static const LevelTable& measuredLevels();
        void prepareCore (double sampleRate, int maxBlockSize);
        LevelTable levelMatch {};
        std::array<std::atomic<float>, maxBands> bandDriveMeter {};
    };
}
