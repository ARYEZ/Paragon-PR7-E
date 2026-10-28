#include "MarsModulation.h"

namespace effects
{
    namespace
    {
        using Range = juce::NormalisableRange<float>;

        Range skewed (float min, float max, float centre, float step = 0.0f)
        {
            Range range { min, max, step };
            range.setSkewForCentre (centre);
            return range;
        }

        double fraction (double x) noexcept { return x - std::floor (x); }
        double toDb (double level) noexcept { return 20.0 * std::log10 (level + 1.0e-9); }

        float curveOf (MarsModulation::Curve curve, float t) noexcept
        {
            switch (curve)
            {
                case MarsModulation::Curve::linear: return t;
                case MarsModulation::Curve::sqr:    return t * t;
                case MarsModulation::Curve::sqrt:   return std::sqrt (t);
                case MarsModulation::Curve::sine:   break;
            }
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
        }

        // The XLFO's OFFSET when synced: speed x0.5 to x2.
        Range offsetRange() { return skewed (0.5f, 2.0f, 1.0f); }
    }

    //==============================================================================
    MarsModulation::Lfo::Lfo()
    {
        // Two steps that make a sine, as Saturn's XLFO starts.
        steps[0] = { 1.0f, 1.0f, Curve::sine, false };
        steps[1] = { -1.0f, 1.0f, Curve::sine, false };
    }

    juce::String MarsModulation::targetName (int target)
    {
        if (target <= 0 || target >= numTargets)
            return "None";
        if (target < firstGlobalTarget)
            return "Band " + juce::String ((target - firstBandTarget) / bandParts + 1) + " " + bandPartNames[(target - firstBandTarget) % bandParts];
        if (target < firstLfoTarget)
        {
            const char* names[] = { "Crossover 1", "Crossover 2", "Crossover 3", "Input", "Output", "Mix" };
            return names[target - firstGlobalTarget];
        }
        if (target < firstEgTarget)
        {
            const char* names[] = { "Rate", "Balance", "Glide", "Phase" };
            return "XLFO " + juce::String ((target - firstLfoTarget) / lfoParts + 1) + " " + names[(target - firstLfoTarget) % lfoParts];
        }
        if (target < firstEfTarget)
        {
            const char* names[] = { "Threshold", "Delay", "Attack", "Decay", "Sustain", "Hold", "Release" };
            return "EG " + juce::String ((target - firstEgTarget) / egParts + 1) + " " + names[(target - firstEgTarget) % egParts];
        }
        if (target < firstSlotTarget)
        {
            const char* names[] = { "Attack", "Release" };
            return "EF " + juce::String ((target - firstEfTarget) / efParts + 1) + " " + names[(target - firstEfTarget) % efParts];
        }
        return "Slot " + juce::String (target - firstSlotTarget + 1) + " Amount";
    }

    juce::StringArray MarsModulation::targetNames()
    {
        juce::StringArray names;
        for (int target = 0; target < numTargets; ++target)
            names.add (targetName (target));
        return names;
    }

    juce::NormalisableRange<float> MarsModulation::targetRange (int target)
    {
        if (target >= firstBandTarget && target < firstGlobalTarget)
        {
            switch ((target - firstBandTarget) % bandParts)
            {
                case dynamics:     return { -1.0f, 1.0f, 0.01f };
                case feedbackFreq: return skewed (50.0f, 10000.0f, 1000.0f, 1.0f);
                case bass: case mid: case treble: case presence:
                                   return { -12.0f, 12.0f, 0.1f };
                case level:        return { -36.0f, 12.0f, 0.1f };
                default:           return { 0.0f, 1.0f, 0.01f };   // drive, feedback, mix
            }
        }
        if (target >= firstGlobalTarget && target < firstLfoTarget)
        {
            switch (target - firstGlobalTarget)
            {
                case input: case output: return { -24.0f, 24.0f, 0.1f };
                case globalMix:          return { 0.0f, 1.0f, 0.01f };
                default:                 return skewed (30.0f, 16000.0f, 1000.0f, 1.0f);   // crossovers
            }
        }
        if (target >= firstLfoTarget && target < firstEgTarget)
        {
            switch ((target - firstLfoTarget) % lfoParts)
            {
                case lfoRate:  return skewed (0.02f, 500.0f, 2.0f);
                case lfoPhase: return { 0.0f, 1.0f, 0.001f };
                default:       return { -1.0f, 1.0f, 0.01f };   // balance, glide
            }
        }
        if (target >= firstEgTarget && target < firstEfTarget)
        {
            switch ((target - firstEgTarget) % egParts)
            {
                case egThreshold: return { -60.0f, 0.0f, 0.1f };
                case egDelay:     return skewed (0.0f, 2000.0f, 200.0f);
                case egAttack:    return skewed (0.1f, 5000.0f, 100.0f);
                case egDecay:     return skewed (1.0f, 5000.0f, 300.0f);
                case egSustain:   return { 0.0f, 1.0f, 0.01f };
                case egHold:      return skewed (0.0f, 2000.0f, 200.0f);
                default:          return skewed (1.0f, 10000.0f, 400.0f);   // release
            }
        }
        if (target >= firstEfTarget && target < firstSlotTarget)
            return (target - firstEfTarget) % efParts == efAttack ? skewed (0.1f, 1000.0f, 30.0f) : skewed (1.0f, 5000.0f, 300.0f);
        return { -1.0f, 1.0f, 0.01f };   // slot amounts (and none)
    }

    double MarsModulation::syncBars (int sync) noexcept
    {
        constexpr double bars[] = { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625 };
        return bars[juce::jlimit (0, (int) std::size (bars) - 1, sync)];
    }

    float MarsModulation::respond (Response response, float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        constexpr float e3 = 20.085536923f;   // e^3
        switch (response)
        {
            case Response::linear:      return x;
            case Response::exponential: return (std::exp (3.0f * x) - 1.0f) / (e3 - 1.0f);
            case Response::logarithmic: return std::log (1.0f + (e3 - 1.0f) * x) / 3.0f;
            case Response::square:      return x * x;
            case Response::squareRoot:  return std::sqrt (x);
            case Response::sine:        break;
        }
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
    }

    //==============================================================================
    void MarsModulation::MidiState::read (const juce::MidiBuffer& buffer) noexcept
    {
        for (const auto metadata : buffer)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                firstNoteStarted = firstNoteStarted || notesHeld == 0;
                noteStarted = true;
                ++notesHeld;
                velocity = message.getFloatVelocity();
            }
            else if (message.isNoteOff())
            {
                notesHeld = juce::jmax (0, notesHeld - 1);
            }
            else if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                notesHeld = 0;
            }
            else if (message.isPitchWheel())
            {
                pitchBend = juce::jlimit (-1.0f, 1.0f, (float) (message.getPitchWheelValue() - 8192) / 8191.0f);
            }
            else if (message.isChannelPressure())
            {
                aftertouch = (float) message.getChannelPressureValue() / 127.0f;
            }
            else if (message.isController())
            {
                controllers[(size_t) juce::jlimit (0, 127, message.getControllerNumber())] = (float) message.getControllerValue() / 127.0f;
            }
        }
    }

    //==============================================================================
    void MarsModulation::prepare (double sampleRate)
    {
        baseRate = sampleRate;
        setListenRate (sampleRate);
        reset();
    }

    void MarsModulation::setListenRate (double workingRate) noexcept
    {
        // The envelopes' ears: a peak follower (30 ms to let go) and a slower
        // average (40 ms in, 300 ms out). Their difference is an attack.
        fastRelease = 1.0 - std::exp (-1.0 / (0.03 * workingRate));
        slowAttack = 1.0 - std::exp (-1.0 / (0.04 * workingRate));
        slowRelease = 1.0 - std::exp (-1.0 / (0.3 * workingRate));
    }

    void MarsModulation::reset() noexcept
    {
        fast.fill (0.0);
        slow.fill (0.0);
        for (auto& state : lfoStates)
        {
            state = {};
            for (auto& value : state.values)
                value = random.nextFloat() * 2.0f - 1.0f;
        }
        for (auto& state : egStates)
            state = {};
        efOut.fill (0.0);
        sources.fill (0.0f);
        offsets.fill (0.0f);
    }

    bool MarsModulation::isActive (const Settings& settings) noexcept
    {
        for (const auto& slot : settings.slots)
            if (slot.source > none && slot.source < numSources && slot.target > 0 && slot.target < numTargets && std::abs (slot.amount) >= minimumAmount)
                return true;
        return false;
    }

    float MarsModulation::triggerLevelDb (int trigger) const noexcept
    {
        return (float) toDb (fast[(size_t) juce::jlimit (0, 4, trigger)]);
    }

    double MarsModulation::triggerDb (Trigger trigger) const noexcept
    {
        return toDb (fast[(size_t) juce::jlimit (0, 4, trigger == Trigger::midi ? 0 : (int) trigger)]);
    }

    float MarsModulation::targetValue (const Settings&, int target, float base) const noexcept
    {
        const auto amount = offsets[(size_t) target];
        if (juce::exactlyEqual (amount, 0.0f))
            return base;
        const auto range = targetRange (target);
        return range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (juce::jlimit (range.start, range.end, base)) + amount));
    }

    double MarsModulation::warp (double cyclePosition, float balance) noexcept
    {
        // BALANCE: where the cycle's halfway point falls in time. Left, the
        // first half goes by quickly; right, slowly.
        const auto split = 0.5 * (1.0 + 0.9 * juce::jlimit (-1.0, 1.0, (double) balance));
        return cyclePosition < split ? 0.5 * cyclePosition / split
                                     : 0.5 + 0.5 * (cyclePosition - split) / (1.0 - split);
    }

    float MarsModulation::lfoShape (const Lfo& lfo, double position, const std::array<float, maxSteps>& stepValues) noexcept
    {
        const auto count = juce::jlimit (1, maxSteps, lfo.numSteps);
        const auto x = fraction (position) * count;
        const auto index = juce::jmin (count - 1, (int) x);
        const auto within = (float) (x - index);
        const auto& step = lfo.steps[(size_t) index];
        const auto here = stepValues[(size_t) index], next = stepValues[(size_t) ((index + 1) % count)];
        // GLIDE: how much of the step's end slides into the next step.
        const auto glide = juce::jlimit (0.0f, 1.0f, step.glide + lfo.glide);
        if (glide <= 0.0f || within < 1.0f - glide)
            return here;
        return here + (next - here) * curveOf (step.curve, (within - (1.0f - glide)) / glide);
    }

    double MarsModulation::egCurve (double progress, float slope) noexcept
    {
        // Straight at 0; bowed one way or the other towards the ends.
        progress = juce::jlimit (0.0, 1.0, progress);
        const auto k = 5.0 * juce::jlimit (-1.0, 1.0, (double) slope);
        if (std::abs (k) < 1.0e-3)
            return progress;
        return (std::exp (k * progress) - 1.0) / (std::exp (k) - 1.0);
    }

    void MarsModulation::tick (const Settings& settings, const Context& context, double seconds) noexcept
    {
        const auto* midi = context.midi;

        //==============================================================================
        // XLFOs
        for (int l = 0; l < numLfos; ++l)
        {
            auto lfo = settings.lfos[(size_t) l];
            auto& state = lfoStates[(size_t) l];
            lfo.balance = targetValue (settings, lfoTarget (l, lfoBalance), lfo.balance);
            lfo.glide = targetValue (settings, lfoTarget (l, lfoGlide), lfo.glide);
            const auto phase = targetValue (settings, lfoTarget (l, lfoPhase), lfo.phase);
            const auto restart = midi != nullptr && ((lfo.midi == MidiRestart::retrigger && midi->noteStarted)
                                                     || (lfo.midi == MidiRestart::legato && midi->firstNoteStarted));
            if (lfo.sync <= 0)
            {
                const auto rate = targetValue (settings, lfoTarget (l, lfoRate), lfo.rateHz);
                state.cycle = restart ? 0.0 : fraction (state.cycle + rate * seconds);
            }
            else
            {
                // Synced: RATE's knob is OFFSET, a speed from x0.5 to x2.
                const auto range = offsetRange();
                const auto speed = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (juce::jlimit (0.5f, 2.0f, lfo.offset))
                                                                                     + offsets[(size_t) lfoTarget (l, lfoRate)]));
                const auto cycleBeats = syncBars (lfo.sync) * 4.0 / speed;
                if (context.songBeats >= 0.0)
                {
                    // Locked to the song (from the last restart, if any).
                    if (restart)
                        state.lockBeats = context.songBeats;
                    state.cycle = fraction ((context.songBeats - state.lockBeats) / cycleBeats);
                }
                else
                {
                    state.cycle = restart ? 0.0 : fraction (state.cycle + context.bpm / 60.0 / cycleBeats * seconds);
                }
            }
            state.position = fraction (warp (state.cycle, lfo.balance) + phase);

            // A random step gets its new value as the step before it starts,
            // so the glide into it knows where it's going.
            const auto count = juce::jlimit (1, maxSteps, lfo.numSteps);
            const auto step = juce::jmin (count - 1, (int) (state.position * count));
            if (step != state.step)
            {
                state.step = step;
                const auto next = (size_t) ((step + 1) % count);
                if (lfo.steps[next].random)
                    state.values[next] = random.nextFloat() * 2.0f - 1.0f;
            }
            auto values = state.values;
            for (size_t i = 0; i < (size_t) maxSteps; ++i)
                if (! lfo.steps[i].random)
                    values[i] = juce::jlimit (-1.0f, 1.0f, lfo.steps[i].value);
            state.values = values;
            sources[(size_t) (lfo1 + l)] = lfoShape (lfo, state.position, values);
        }

        //==============================================================================
        // EGs
        for (int e = 0; e < numEgs; ++e)
        {
            const auto& eg = settings.egs[(size_t) e];
            auto& state = egStates[(size_t) e];
            const auto moved = [&] (int part, float base) { return targetValue (settings, egTarget (e, part), base); };
            const auto threshold = moved (egThreshold, eg.thresholdDb);
            const auto delay = moved (egDelay, eg.delayMs), attack = moved (egAttack, eg.attackMs), decay = moved (egDecay, eg.decayMs);
            const double sustain = moved (egSustain, eg.sustain);
            const auto hold = moved (egHold, eg.holdMs), release = moved (egRelease, eg.releaseMs);

            bool gate = false, start = false;
            if (eg.trigger == Trigger::midi)
            {
                gate = midi != nullptr && midi->notesHeld > 0;
                start = midi != nullptr && midi->noteStarted;
            }
            else
            {
                // Over the THRESHOLD, and 3 dB under it to let go, so it doesn't chatter.
                const auto db = triggerDb (eg.trigger);
                gate = state.gate ? db > threshold - 3.0 : db > threshold;
                start = gate && ! state.gate;
            }
            using Stage = EgState::Stage;
            if (start)
            {
                state.stage = Stage::delay;
                state.time = 0.0;
            }
            else if (! gate && state.gate)
            {
                // Let go: HOLD where it is, then RELEASE.
                state.from = state.level;
                state.stage = state.stage == Stage::idle ? Stage::idle : Stage::hold;
                state.time = 0.0;
            }
            state.gate = gate;

            state.time += seconds * 1000.0;
            for (int guard = 0; guard < 8; ++guard)
            {
                if (state.stage == Stage::delay && state.time >= delay)
                {
                    state.time -= delay;
                    state.from = state.level;
                    state.stage = Stage::attack;
                }
                else if (state.stage == Stage::attack && state.time >= attack)
                {
                    state.time -= attack;
                    state.stage = Stage::decay;
                }
                else if (state.stage == Stage::decay && state.time >= decay)
                {
                    state.stage = Stage::sustain;
                }
                else if (state.stage == Stage::hold && state.time >= hold)
                {
                    state.time -= hold;
                    state.stage = Stage::release;
                }
                else if (state.stage == Stage::release && state.time >= release)
                {
                    state.stage = Stage::idle;
                }
                else
                {
                    break;
                }
            }
            switch (state.stage)
            {
                case Stage::idle:    state.level = 0.0; break;
                case Stage::delay:   break;
                case Stage::attack:  state.level = state.from + (1.0 - state.from) * egCurve (state.time / juce::jmax (0.01f, attack), eg.attackSlope); break;
                case Stage::decay:   state.level = 1.0 + (sustain - 1.0) * egCurve (state.time / juce::jmax (0.01f, decay), eg.decaySlope); break;
                case Stage::sustain: state.level = sustain; break;
                case Stage::hold:    state.level = state.from; break;
                case Stage::release: state.level = state.from * (1.0 - egCurve (state.time / juce::jmax (0.01f, release), eg.releaseSlope)); break;
            }
            // RANGE centred: 0 at the sustain level, so it swings both ways.
            sources[(size_t) (eg1 + e)] = (float) (eg.centred ? state.level - sustain : state.level);
        }

        //==============================================================================
        // EFs
        for (int f = 0; f < numEfs; ++f)
        {
            const auto& ef = settings.efs[(size_t) f];
            const auto attack = targetValue (settings, efTarget (f, efAttack), ef.attackMs);
            const auto release = targetValue (settings, efTarget (f, efRelease), ef.releaseMs);
            const auto signal = (size_t) juce::jlimit (0, 4, ef.trigger == Trigger::midi ? 0 : (int) ef.trigger);
            double raw;
            if (ef.transient)
            {
                // How far the peak jumps over the average: an attack. 2 dB is
                // ignored (a steady note's own ripple); 20 dB is full.
                const auto jump = toDb (fast[signal]) - toDb (slow[signal]);
                raw = juce::jlimit (0.0, 1.0, (jump - 2.0) / 18.0 * juce::Decibels::decibelsToGain ((double) ef.gainDb));
            }
            else
            {
                // -48 dB to 0 dB (moved by GAIN) as 0 to 1.
                raw = juce::jlimit (0.0, 1.0, (toDb (fast[signal]) + ef.gainDb + 48.0) / 48.0);
            }
            auto& out = efOut[(size_t) f];
            const auto time = (raw > out ? attack : release) * 0.001;
            out += (1.0 - std::exp (-seconds / juce::jmax (1.0e-4, (double) time))) * (raw - out);
            sources[(size_t) (ef1 + f)] = (float) out;
        }

        //==============================================================================
        // MIDI, XY, the pedal
        for (int m = 0; m < numMidis; ++m)
        {
            const auto& source = settings.midis[(size_t) m];
            auto raw = 0.0f;
            if (midi != nullptr)
            {
                switch (source.input)
                {
                    case MidiInput::velocity:   raw = midi->velocity; break;
                    case MidiInput::pitchBend:  raw = midi->pitchBend; break;
                    case MidiInput::modWheel:   raw = midi->controllers[1]; break;
                    case MidiInput::aftertouch: raw = midi->aftertouch; break;
                    case MidiInput::controller: raw = midi->controllers[(size_t) juce::jlimit (0, 127, source.controller)]; break;
                }
            }
            sources[(size_t) (midi1 + m)] = (raw < 0.0f ? -1.0f : 1.0f) * respond (source.response, std::abs (raw));
        }
        for (int p = 0; p < numXys; ++p)
        {
            const auto& xy = settings.xys[(size_t) p];
            const auto out = [&xy] (float v) { v = juce::jlimit (0.0f, 1.0f, v); return xy.unipolar ? v : v * 2.0f - 1.0f; };
            sources[(size_t) (xy1x + 2 * p)] = xy.slider ? 0.0f : out (xy.x);
            sources[(size_t) (xy1y + 2 * p)] = out (xy.y);
        }
        sources[pedal] = juce::jlimit (0.0f, 1.0f, context.pedal);
        sources[none] = 0.0f;

        //==============================================================================
        // The slots. First, slots that turn other slots up or down (one
        // level: a slot's own AMOUNT, when it is someone's target, isn't
        // itself changed by a third slot).
        const auto valid = [] (const Slot& slot)
        {
            return slot.source > none && slot.source < numSources && slot.target > 0 && slot.target < numTargets;
        };
        std::array<float, numSlots> amounts {};
        for (size_t s = 0; s < (size_t) numSlots; ++s)
            amounts[s] = settings.slots[s].amount;
        for (const auto& slot : settings.slots)
            if (valid (slot) && slot.target >= firstSlotTarget)
                amounts[(size_t) (slot.target - firstSlotTarget)] += slot.amount * sources[(size_t) slot.source];

        offsets.fill (0.0f);
        for (size_t s = 0; s < (size_t) numSlots; ++s)
        {
            const auto& slot = settings.slots[s];
            if (! valid (slot))
                continue;
            const auto amount = slot.target >= firstSlotTarget ? slot.amount : juce::jlimit (-1.0f, 1.0f, amounts[s]);
            offsets[(size_t) slot.target] += amount * sources[(size_t) slot.source];
        }
        for (auto& offset : offsets)
            offset = juce::jlimit (-2.0f, 2.0f, offset);
    }
}
