#pragma once

#include "EffectUtils.h"

namespace effects
{
    // MARS's modulation (stage 2): sources that move MARS's knobs on their
    // own, through modulation slots. Inspired by FabFilter Saturn 2's
    // modulation (from its user manual); our own design where the manual
    // doesn't say how something works.
    //
    // Saturn adds sources as you need them. A plugin has to name all its
    // parameters up front, so MARS has a fixed set:
    //
    //   2 XLFOs      step LFOs: 1-16 steps, each with its own glide, curve
    //                and random option; RATE (or tempo sync), BALANCE, GLIDE,
    //                PHASE, restart on a MIDI note. Output -1 to 1.
    //   2 EGs        envelope generators: delay, attack, decay, sustain, hold,
    //                release, curved; set off by the voice, a band, or MIDI.
    //   2 EFs        envelope followers: how loud the voice (or a band) is,
    //                or only its attacks (TRANSIENT).
    //   2 MIDI       velocity, pitch bend, mod wheel, aftertouch or any CC,
    //                through a response curve.
    //   2 XY pads    two hands-on controls each (X and Y), or one (SLIDER).
    //   the PEDAL    the PR7's expression pedal (our addition).
    //
    // 16 SLOTS each connect a source to a TARGET by an AMOUNT (-100% to
    // 100%: at 100%, a source at full swing turns the knob through its whole
    // range). Targets: almost every MARS knob, the sources' own settings
    // (one XLFO can speed up another) and other slots' amounts (an XY pad
    // that sets how much an LFO does).
    //
    // It runs at a control rate of about 3 kHz (every 16 samples at 48 kHz):
    // smooth for anything up to a fast tremolo. Saturn's XLFO goes to 500 Hz;
    // above about 200 Hz ours gets coarser than Saturn's.
    class MarsModulation
    {
    public:
        static constexpr int numLfos = 2, numEgs = 2, numEfs = 2, numMidis = 2, numXys = 2;
        static constexpr int numSlots = 16, maxSteps = 16;

        // Saved by number: only ever add to the end.
        enum Source { none, lfo1, lfo2, eg1, eg2, ef1, ef2, midi1, midi2, xy1x, xy1y, xy2x, xy2y, pedal, numSources };
        static inline const juce::StringArray sourceNames { "None", "XLFO 1", "XLFO 2", "EG 1", "EG 2", "EF 1", "EF 2",
                                                            "MIDI 1", "MIDI 2", "XY 1 X", "XY 1 Y", "XY 2 X", "XY 2 Y",
                                                            "Expression Pedal" };

        // Targets, by number (saved: only ever add to the end).
        //   0          none
        //   1-40       band 1-4: DRIVE, DYNAMICS, FEEDBACK, FB FREQ, BASS, MID, TREBLE, PRESENCE, MIX, LEVEL
        //   41-46      CROSSOVER 1-3, INPUT, OUTPUT, MIX
        //   47-54      XLFO 1-2: RATE (OFFSET when synced), BALANCE, GLIDE, PHASE
        //   55-68      EG 1-2: THRESHOLD, DELAY, ATTACK, DECAY, SUSTAIN, HOLD, RELEASE
        //   69-72      EF 1-2: ATTACK, RELEASE
        //   73-88      SLOT 1-16's AMOUNT
        static constexpr int bandParts = 10;
        enum BandPart { drive, dynamics, feedback, feedbackFreq, bass, mid, treble, presence, mix, level };
        static inline const juce::StringArray bandPartNames { "Drive", "Dynamics", "Feedback", "FB Freq", "Bass", "Mid",
                                                              "Treble", "Presence", "Mix", "Level" };
        static constexpr int firstBandTarget = 1;
        static constexpr int firstGlobalTarget = 41;   // crossovers 1-3, input, output, mix
        static constexpr int firstLfoTarget = 47, lfoParts = 4;
        static constexpr int firstEgTarget = 55, egParts = 7;
        static constexpr int firstEfTarget = 69, efParts = 2;
        static constexpr int firstSlotTarget = 73;
        static constexpr int numTargets = firstSlotTarget + numSlots;
        enum GlobalPart { crossover1, crossover2, crossover3, input, output, globalMix };
        enum LfoPart { lfoRate, lfoBalance, lfoGlide, lfoPhase };
        enum EgPart { egThreshold, egDelay, egAttack, egDecay, egSustain, egHold, egRelease };
        enum EfPart { efAttack, efRelease };

        static constexpr int bandTarget (int band, int part) noexcept { return firstBandTarget + band * bandParts + part; }
        static constexpr int globalTarget (int part) noexcept { return firstGlobalTarget + part; }
        static constexpr int lfoTarget (int lfo, int part) noexcept { return firstLfoTarget + lfo * lfoParts + part; }
        static constexpr int egTarget (int eg, int part) noexcept { return firstEgTarget + eg * egParts + part; }
        static constexpr int efTarget (int ef, int part) noexcept { return firstEfTarget + ef * efParts + part; }
        static constexpr int slotTarget (int slot) noexcept { return firstSlotTarget + slot; }
        static juce::String targetName (int target);
        static juce::StringArray targetNames();

        // Each target's range, the same as its knob's: AMOUNT works in
        // fractions of it. A test checks these match the parameters.
        static juce::NormalisableRange<float> targetRange (int target);

        //==============================================================================
        // XLFO
        enum class Curve { linear, sqr, sqrt, sine };
        static inline const juce::StringArray curveNames { "Linear", "Sqr", "Sqrt", "Sine" };
        // SYNC: free (RATE in Hz), or a cycle in bars of 4 beats.
        static inline const juce::StringArray syncNames { "Free", "16 bars", "8 bars", "4 bars", "2 bars", "1 bar",
                                                          "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        static double syncBars (int sync) noexcept;   // 0 for Free
        enum class MidiRestart { off, retrigger, legato };
        static inline const juce::StringArray midiRestartNames { "Off", "Retrigger", "Legato" };

        struct Step
        {
            float value = 0.0f;   // -1 to 1
            float glide = 0.0f;   // 0 to 1: how much of the step slides into the next
            Curve curve = Curve::sine;
            bool random = false;
        };
        struct Lfo
        {
            int numSteps = 2;
            float rateHz = 1.0f;
            int sync = 0;
            float offset = 1.0f;      // synced: speed x0.5 to x2
            float balance = 0.0f;     // -1 (first half quicker) to 1
            float glide = 0.0f;       // -1 to 1, added to every step's
            float phase = 0.0f;       // 0 to 1: where the cycle starts
            MidiRestart midi = MidiRestart::off;
            std::array<Step, maxSteps> steps;
            Lfo();
        };

        //==============================================================================
        // EG and EF
        enum class Trigger { input, band1, band2, band3, band4, midi };
        static inline const juce::StringArray triggerNames { "Input", "Band 1", "Band 2", "Band 3", "Band 4", "MIDI Notes" };
        static inline const juce::StringArray egRangeNames { "0 to 1", "Centred" };
        struct Eg
        {
            Trigger trigger = Trigger::input;
            float thresholdDb = -30.0f;
            float delayMs = 0.0f, attackMs = 10.0f, decayMs = 200.0f, sustain = 0.5f, holdMs = 0.0f, releaseMs = 300.0f;
            float attackSlope = 0.0f, decaySlope = 0.0f, releaseSlope = 0.0f;   // -1 to 1, 0 straight
            bool centred = false;
        };
        static inline const juce::StringArray efModeNames { "Envelope", "Transient" };
        struct Ef
        {
            Trigger trigger = Trigger::input;   // not MIDI
            float attackMs = 10.0f, releaseMs = 150.0f;
            bool transient = false;
            float gainDb = 0.0f;
        };

        //==============================================================================
        // MIDI, XY
        enum class MidiInput { velocity, pitchBend, modWheel, aftertouch, controller };
        static inline const juce::StringArray midiInputNames { "Velocity", "Pitch Bend", "Mod Wheel", "Aftertouch", "Controller" };
        enum class Response { linear, exponential, logarithmic, square, squareRoot, sine };
        static inline const juce::StringArray responseNames { "Linear", "Exponential", "Logarithmic", "Square", "Square Root", "Sine" };
        static float respond (Response, float x) noexcept;   // 0-1 in, 0-1 out
        struct Midi
        {
            MidiInput input = MidiInput::modWheel;
            int controller = 1;
            Response response = Response::linear;
        };
        static inline const juce::StringArray xyModeNames { "XY", "Slider" };
        static inline const juce::StringArray xyRangeNames { "Bipolar", "Unipolar" };
        struct Xy
        {
            float x = 0.5f, y = 0.5f;   // 0 to 1
            bool slider = false, unipolar = false;
        };

        struct Slot
        {
            int source = none, target = 0;
            float amount = 0.0f;   // -1 to 1
        };

        struct Settings
        {
            std::array<Lfo, numLfos> lfos;
            std::array<Eg, numEgs> egs;
            std::array<Ef, numEfs> efs;
            std::array<Midi, numMidis> midis;
            std::array<Xy, numXys> xys;
            std::array<Slot, numSlots> slots;
        };

        // What MIDI has said so far (kept by the processor, block to block).
        struct MidiState
        {
            int notesHeld = 0;
            bool noteStarted = false;          // a note-on in this block
            bool firstNoteStarted = false;     // ...with none held before it
            float velocity = 0.0f, pitchBend = 0.0f, aftertouch = 0.0f;   // pitch bend -1 to 1
            std::array<float, 128> controllers {};
            void read (const juce::MidiBuffer&) noexcept;
            void blockDone() noexcept { noteStarted = firstNoteStarted = false; }
        };

        struct Context
        {
            double bpm = 120.0;
            double songBeats = -1.0;   // where the song is, in beats (-1: not playing)
            float pedal = 0.0f;        // the expression pedal, 0 to 1
            const MidiState* midi = nullptr;
        };

        //==============================================================================
        void prepare (double sampleRate);
        void reset() noexcept;
        void setListenRate (double workingRate) noexcept;

        // Any slot that does something. An AMOUNT under 0.01% counts as 0:
        // a knob set to 0% can be stored a hair off it after rounding (it
        // was, on macOS), and that mustn't switch the modulation on.
        static constexpr float minimumAmount = 1.0e-4f;
        static bool isActive (const Settings&) noexcept;

        // Called for every sample MARS works on: the input and the bands, for
        // the envelopes to hear.
        void listen (double in, const std::array<double, 4>& bands) noexcept
        {
            hear (0, in);
            for (size_t b = 0; b < bands.size(); ++b)
                hear ((int) b + 1, bands[b]);
        }

        // Moves the sources on by `seconds` and works out every target's offset.
        void tick (const Settings&, const Context&, double seconds) noexcept;

        // How far a target is moved now, as a fraction of its range (-2 to 2).
        float offset (int target) const noexcept { return offsets[(size_t) target]; }
        float sourceValue (int source) const noexcept { return sources[(size_t) juce::jlimit (0, numSources - 1, source)]; }
        // Where an XLFO is in its cycle (0 to 1, after BALANCE and PHASE).
        float lfoPosition (int lfo) const noexcept { return (float) lfoStates[(size_t) lfo].position; }
        // An EG's level (0 to 1, before RANGE), and whether its trigger is held.
        float egLevel (int eg) const noexcept { return (float) egStates[(size_t) eg].level; }
        bool egGate (int eg) const noexcept { return egStates[(size_t) eg].gate; }
        // How loud a trigger signal is (dB): 0 input, 1-4 bands.
        float triggerLevelDb (int trigger) const noexcept;

        // The value a step-LFO's waveform has at `position` (0 to 1), using
        // each step's own value (random steps: their last value). For tests
        // and the panel's drawing.
        static float lfoShape (const Lfo&, double position, const std::array<float, maxSteps>& stepValues) noexcept;
        static double warp (double cyclePosition, float balance) noexcept;
        static double egCurve (double progress, float slope) noexcept;

    private:
        void hear (int signal, double x) noexcept
        {
            const auto amplitude = std::abs (x);
            auto& f = fast[(size_t) signal];
            f = amplitude > f ? amplitude : f + fastRelease * (amplitude - f);
            auto& s = slow[(size_t) signal];
            s += (amplitude > s ? slowAttack : slowRelease) * (amplitude - s);
        }
        float targetValue (const Settings&, int target, float base) const noexcept;   // base plus its offset, clamped
        double triggerDb (Trigger) const noexcept;

        struct LfoState
        {
            double cycle = 0.0, position = 0.0, lockBeats = 0.0;
            int step = -1;
            std::array<float, maxSteps> values {};
        };
        struct EgState
        {
            enum class Stage { idle, delay, attack, decay, sustain, hold, release };
            Stage stage = Stage::idle;
            double time = 0.0, level = 0.0, from = 0.0;
            bool gate = false;
        };

        double baseRate = 48000.0;
        double fastRelease = 0.0, slowAttack = 0.0, slowRelease = 0.0;
        std::array<double, 5> fast {}, slow {};
        std::array<LfoState, numLfos> lfoStates;
        std::array<EgState, numEgs> egStates;
        std::array<double, numEfs> efOut {};
        std::array<float, numSources> sources {};
        std::array<float, numTargets> offsets {};
        juce::Random random;
    };
}
