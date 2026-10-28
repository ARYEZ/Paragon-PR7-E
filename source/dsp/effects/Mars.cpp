#include "Mars.h"

namespace effects
{
    namespace
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        constexpr int factors[] = { 1, 4, 8 };   // QUALITY's oversampling

        double onePoleFor (double rate, double hz) { return 1.0 - std::exp (-2.0 * pi * juce::jmin (hz, rate * 0.45) / rate); }
        double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

        // How far DRIVE turns each style up, at full: the clean styles less,
        // the extreme ones more.
        constexpr float styleMaxDb[Mars::numStyles] = {
            18, 24, 30, 36,          // tube: clean, warm, saturated, broken
            12, 24, 30, 42,          // tape: clean, warm, saturated, extreme
            12, 18, 24,              // transformer: subtle, warm, lush
            30, 36, 40, 36, 48,      // amp: American, British, smooth, crunchy, screaming
            24, 36, 30, 24, 30 };    // FX: smudge, breakdown, foldback, rectify, destroy

        double softSign (double u) { return u / (1.0 + std::abs (u)); }

        // A triangle fold: past +-1 the wave turns back on itself.
        double fold (double u)
        {
            const auto t = 0.25 * u + 0.25;
            return 4.0 * std::abs (t - std::floor (t + 0.5)) - 1.0;
        }
    }

    //==============================================================================
    void Mars::Svf::set (double sampleRate, double hz, double q) noexcept
    {
        g = std::tan (pi * juce::jlimit (5.0, sampleRate * 0.45, hz) / sampleRate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Mars::Svf::Out Mars::Svf::process (double x) noexcept
    {
        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0 * v1 - s1;
        s2 = 2.0 * v2 - s2;
        return { v2, v1, x - k * v1 - v2 };
    }

    void Mars::Crossover::set (double sampleRate, double hz, Slope newSlope) noexcept
    {
        slope = newSlope;
        onePole = onePoleFor (sampleRate, hz);
        // Linkwitz-Riley: a Butterworth filter twice over. 12 dB: two
        // critically damped poles (the high half flipped, so the halves add
        // up flat); 24 dB: two 2nd-order Butterworths; 48 dB: two 4th-order.
        constexpr double bw4[] = { 0.5412, 1.3066 };
        for (size_t i = 0; i < low.size(); ++i)
        {
            const auto q = slope == Slope::db12 ? 0.5 : slope == Slope::db24 ? 0.7071 : bw4[i % 2];
            low[i].set (sampleRate, hz, q);
            high[i].set (sampleRate, hz, q);
        }
    }

    void Mars::Crossover::reset() noexcept
    {
        for (auto& filter : low) filter.reset();
        for (auto& filter : high) filter.reset();
        onePoleState = 0.0;
    }

    std::pair<double, double> Mars::Crossover::process (double x) noexcept
    {
        switch (slope)
        {
            case Slope::db6:
                onePoleState += onePole * (x - onePoleState);
                return { onePoleState, x - onePoleState };
            case Slope::db12:
                return { low[0].process (x).low, -high[0].process (x).high };
            case Slope::db24:
                return { low[1].process (low[0].process (x).low).low, high[1].process (high[0].process (x).high).high };
            case Slope::db48:
            {
                auto l = x, h = x;
                for (size_t i = 0; i < low.size(); ++i)
                {
                    l = low[i].process (l).low;
                    h = high[i].process (h).high;
                }
                return { l, h };
            }
        }
        return { x, 0.0 };
    }

    double Mars::Biquad::process (double x) noexcept
    {
        const auto y = (c[0] * x + z1) / c[3];
        z1 = (c[1] * x - c[4] * y) + z2;
        z2 = (c[2] * x - c[5] * y);
        return y;
    }

    //==============================================================================
    float Mars::preGain (Style style, float drive) noexcept
    {
        return (float) dbToGain (juce::jlimit (0.0f, 1.0f, drive) * styleMaxDb[(int) style]);
    }

    float Mars::curve (Style style, float x, float drive) noexcept
    {
        return (float) shape (style, (double) x * preGain (style, drive), juce::jlimit (0.0f, 1.0f, drive));
    }

    // The curves, on the signal already turned up by DRIVE (`u`).
    double Mars::shape (Style style, double u, float drive) noexcept
    {
        double y = 0.0;
        switch (style)
        {
            // Tubes: a triode's lopsided curve (second harmonics), more of it
            // and harder as they go; Broken adds a dead zone round zero (a
            // tube starved of voltage, splattering) and a hard ceiling.
            case Style::cleanTube:      y = std::tanh (u + 0.1) - std::tanh (0.1); break;
            case Style::warmTube:       y = std::tanh (u + 0.25) - std::tanh (0.25); break;
            case Style::saturatedTube:  y = std::tanh (1.5 * (std::tanh (1.5 * u + 0.2) - std::tanh (0.2))); break;
            case Style::brokenTube:
            {
                const auto dead = std::abs (u) < 0.05 ? 0.0 : u - std::copysign (0.05, u);
                y = juce::jlimit (-0.8, 0.8, std::tanh (dead + 0.6) - std::tanh (0.6)) / 0.8;
                break;
            }
            // Tape: a round, symmetrical curve that leans in gently, more so
            // as it goes (its highs are driven harder: see saturate()).
            case Style::cleanTape:      y = softSign (u); break;
            case Style::warmTape:       y = std::tanh (u + 0.05) - std::tanh (0.05); break;
            case Style::saturatedTape:  y = std::tanh (1.3 * u); break;
            case Style::extremeTape:    y = std::tanh (2.0 * u); break;
            // Transformers: mostly even harmonics, and gentle.
            case Style::subtleTransformer: y = std::tanh (u + 0.08 * u * u); break;
            case Style::warmTransformer:   y = std::tanh (u + 0.15 * u * u); break;
            case Style::lushTransformer:   y = std::tanh (u + 0.1 * u * u + 0.1 * u * u * u); break;
            // Amps: gain stages one after another (their tone and cabinet in saturate()).
            case Style::americanAmp:    y = std::tanh (1.5 * std::tanh (2.0 * u) + 0.1) - std::tanh (0.1); break;
            case Style::britishAmp:     y = std::tanh (2.0 * (std::tanh (3.0 * u + 0.2) - std::tanh (0.2))); break;
            case Style::smoothAmp:      y = std::tanh (4.0 * softSign (u)); break;
            case Style::crunchyAmp:     y = u / std::pow (1.0 + std::pow (std::abs (1.5 * u), 4.0), 0.25) * 1.5; break;
            case Style::screamingAmp:   y = std::tanh (3.0 * std::tanh (5.0 * u)); break;
            // FX.
            case Style::smudge:         y = std::tanh (u); break;
            case Style::breakdown:      y = juce::jlimit (-1.0, 1.0, 2.0 * u); break;
            case Style::foldback:       y = fold (u); break;
            case Style::rectify:
            {
                const auto a = 0.3 + 0.7 * drive;   // more of the wave flipped up as DRIVE goes up
                y = std::tanh ((1.0 - a) * u + a * std::abs (u));
                break;
            }
            case Style::destroy:
            {
                const auto steps = std::pow (2.0, 15.0 - 12.0 * drive);   // 16 bits down to 4
                y = std::round (juce::jlimit (-1.0, 1.0, u) * steps) / steps;
                break;
            }
        }
        return y;
    }

    Mars::Mars()
    {
        for (size_t i = 1; i < oversamplers.size(); ++i)
            oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>> (1, i == 1 ? 2 : 3,
                                                                                 juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        for (int target = 1; target < numKnobTargets; ++target)
            knobRanges[(size_t) target] = MarsModulation::targetRange (target);
    }

    const Mars::LevelTable& Mars::measuredLevels()
    {
        // What each style, filters and all, does to a -10 dB, 330 Hz sine's
        // level at each DRIVE (its DC taken out), so it can be undone.
        static const LevelTable table = []
        {
            LevelTable measured {};
            Mars calibrator;
            calibrator.prepareCore (48000.0, 512);
            const auto rate = calibrator.rate;
            const auto period = rate / 330.0;
            const auto settle = (int) (0.02 * rate), length = (int) std::round (4.0 * period);
            for (int style = 0; style < numStyles; ++style)
                for (int step = 0; step < 33; ++step)
                {
                    const auto drive = (float) step / 32.0f;
                    calibrator.resetBands();
                    auto& state = calibrator.bandStates[0];
                    const auto gain = (double) preGain ((Style) style, drive);
                    double in = 0.0, sum = 0.0, squares = 0.0;
                    for (int i = 0; i < settle + length; ++i)
                    {
                        const auto x = 0.316 * std::sin (2.0 * pi * i / period);
                        const auto y = calibrator.saturate (state, (Style) style, x * gain, drive);
                        if (i >= settle)
                        {
                            in += x * x;
                            sum += y;
                            squares += y * y;
                        }
                    }
                    const auto mean = sum / length;
                    const auto ac = juce::jmax (1.0e-12, squares / length - mean * mean);
                    measured[(size_t) style][(size_t) step] = (float) juce::jlimit (0.05, 20.0, std::sqrt ((in / length) / ac));
                }
            return measured;
        }();
        return table;
    }

    float Mars::autoGain (Style style, float drive) const noexcept
    {
        const auto position = juce::jlimit (0.0f, 1.0f, drive) * 32.0f;
        const auto index = juce::jmin (31, (int) position);
        const auto& table = levelMatch[(size_t) style];
        return table[(size_t) index] + (table[(size_t) index + 1] - table[(size_t) index]) * (position - (float) index);
    }

    void Mars::prepare (double sampleRate, int maxBlockSize)
    {
        levelMatch = measuredLevels();
        prepareCore (sampleRate, maxBlockSize);
    }

    void Mars::prepareCore (double sampleRate, int maxBlockSize)
    {
        baseRate = sampleRate;
        maxBlock = maxBlockSize;
        for (size_t i = 1; i < oversamplers.size(); ++i)
        {
            oversamplers[i]->initProcessing ((size_t) maxBlockSize);
            oversamplers[i]->reset();
        }
        for (auto& state : bandStates)
        {
            state.smudgeLine.assign (16384, 0.0f);
            for (size_t q = 0; q < state.breakdownShifters.size(); ++q)
                state.breakdownShifters[q].prepare (sampleRate * factors[q]);
        }
        dryBuffer.setSize (1, maxBlockSize * factors[2]);
        bypass.prepare (sampleRate, maxBlockSize);
        // The modulation moves the knobs about 3000 times a second.
        tickLength = juce::jmax (1, juce::roundToInt (sampleRate / 3000.0));
        untilTick = 0;
        wasModulated = knobsSet = false;
        modulation.prepare (sampleRate);
        currentQuality = -1;
        setRate ((int) Quality::good);
    }

    void Mars::setRate (int qualityIndex) noexcept
    {
        currentQuality = qualityIndex;
        rate = baseRate * factors[qualityIndex];
        auto& c = coefficients;
        c.hz20 = onePoleFor (rate, 20.0);     c.hz30 = onePoleFor (rate, 30.0);     c.hz120 = onePoleFor (rate, 120.0);
        c.hz200 = onePoleFor (rate, 200.0);   c.hz300 = onePoleFor (rate, 300.0);   c.hz1500 = onePoleFor (rate, 1500.0);
        c.hz3000 = onePoleFor (rate, 3000.0); c.hz4000 = onePoleFor (rate, 4000.0); c.hz5500 = onePoleFor (rate, 5500.0);
        c.hz6000 = onePoleFor (rate, 6000.0); c.hz8000 = onePoleFor (rate, 8000.0); c.hz9000 = onePoleFor (rate, 9000.0);
        c.hz11000 = onePoleFor (rate, 11000.0); c.hz12000 = onePoleFor (rate, 12000.0); c.hz16000 = onePoleFor (rate, 16000.0);
        c.attack = 1.0 - std::exp (-1.0 / (0.003 * rate));    // DYNAMICS: 3 ms in,
        c.release = 1.0 - std::exp (-1.0 / (0.15 * rate));    // 150 ms out (it pumps)
        modulation.setListenRate (rate);
        // The glides land where they were going (their lengths were counted at the old rate).
        for (auto& state : bandStates)
            for (auto* ramp : { &state.preGain, &state.makeUp, &state.mix, &state.level })
                *ramp = { ramp->target, ramp->target, 0.0, 0 };
        for (auto* ramp : { &inputGain, &outputGain, &mix })
            *ramp = { ramp->target, ramp->target, 0.0, 0 };
        // Everything that depends on the rate is set again on the next block.
        std::fill (splitShown.begin(), splitShown.end(), -1.0f);
        bandsShown = -1;
        for (auto& state : bandStates)
            state.toneShown.fill (99.0f);
        resetBands();
    }

    void Mars::resetBands() noexcept
    {
        for (auto& split : splits) split.reset();
        for (auto& split : compensation) split.reset();
        for (auto& state : bandStates)
        {
            state.feedbackFilter.reset();
            state.feedbackOut = state.dynamicsEnvelope = state.dynamicsGainDb = 0.0;
            for (auto& filter : state.tone) filter.reset();
            state.tapeEnvelope = state.previous = state.preEmphasis = state.deEmphasis = 0.0;
            state.filters.fill (0.0);
            std::fill (state.smudgeLine.begin(), state.smudgeLine.end(), 0.0f);
            state.smudgeWrite = 0;
            state.smudgeEnvelope = 0.0;
            for (auto& shifter : state.breakdownShifters) shifter.reset();
            state.holdValue = 0.0;
            state.holdCount = 0;
        }
    }

    double Mars::saturate (BandState& state, Style style, double u, float drive) noexcept
    {
        auto& f = state.filters;
        const auto& c = coefficients;
        switch (style)
        {
            case Style::cleanTape:
            case Style::warmTape:
            case Style::saturatedTape:
            case Style::extremeTape:
            {
                // Tape saturates its highs first: they're boosted on the way
                // to the tape and cut on the way back (pre- and de-emphasis),
                // so loud highs are what squash, and the top end rounds off.
                const auto emphasis = style == Style::cleanTape ? 0.5 : style == Style::warmTape ? 1.0 : 1.5;
                state.preEmphasis += c.hz3000 * (u - state.preEmphasis);
                const auto y = shape (style, u + emphasis * (u - state.preEmphasis), drive);
                state.deEmphasis += c.hz3000 * (y - state.deEmphasis);
                const auto restored = (y + emphasis * state.deEmphasis) / (1.0 + emphasis);
                // Warmer tape loses more top end.
                const auto top = style == Style::cleanTape ? c.hz16000 : style == Style::warmTape ? c.hz11000 : c.hz8000;
                f[0] += top * (restored - f[0]);
                return f[0];
            }
            case Style::warmTube:
            case Style::saturatedTube:
            case Style::brokenTube:
            {
                const auto y = shape (style, u, drive);
                f[0] += (style == Style::warmTube ? c.hz12000 : c.hz9000) * (y - f[0]);   // the output transformer's top
                return f[0];
            }
            case Style::subtleTransformer:
            case Style::warmTransformer:
            case Style::lushTransformer:
            {
                // A transformer's core saturates in the lows: they're driven harder.
                f[0] += c.hz200 * (u - f[0]);
                const auto lowsExtra = style == Style::subtleTransformer ? 1.0 : 2.0;
                auto y = shape (style, (u + lowsExtra * f[0]) / (1.0 + 0.5 * lowsExtra), drive);
                if (style == Style::lushTransformer)
                {
                    f[1] += c.hz6000 * (y - f[1]);
                    y += 0.25 * (y - f[1]);   // a little sheen on top
                }
                return y;
            }
            case Style::americanAmp:
            case Style::britishAmp:
            case Style::smoothAmp:
            case Style::crunchyAmp:
            case Style::screamingAmp:
            {
                // An amp: tightened lows in, the gain stages, a tone stack
                // (American scoops the mids, British pushes them), then a
                // speaker cabinet's narrow band.
                f[0] += c.hz120 * (u - f[0]);
                auto y = shape (style, u - f[0], drive);
                f[1] += c.hz1500 * (y - f[1]);
                f[2] += c.hz300 * (y - f[2]);
                const auto mids = f[1] - f[2];
                if (style == Style::americanAmp) y -= 0.4 * mids;
                if (style == Style::britishAmp)  y += 0.4 * mids;
                const auto cabinet = style == Style::smoothAmp ? c.hz4000 : c.hz5500;
                f[3] += cabinet * (y - f[3]);
                f[4] += cabinet * (f[3] - f[4]);
                return f[4];
            }
            case Style::smudge:
            {
                // The sound read back from a few milliseconds ago, at a delay
                // the sound itself keeps moving: it smears and stretches.
                auto& line = state.smudgeLine;
                const auto mask = (int) line.size() - 1;
                line[(size_t) state.smudgeWrite] = (float) u;
                state.smudgeEnvelope += c.hz30 * (std::abs (u) - state.smudgeEnvelope);
                const auto delayMs = 0.5 + 8.0 * drive * (0.5 + 0.5 * std::tanh (3.0 * u) + juce::jmin (1.0, state.smudgeEnvelope));
                const auto delay = juce::jlimit (1.0, (double) mask - 2.0, delayMs * 0.001 * rate);
                const auto position = (double) state.smudgeWrite - delay;
                const auto whole = (int) std::floor (position);
                const auto t = position - whole;
                const auto a = (double) line[(size_t) (whole & mask)], b = (double) line[(size_t) ((whole + 1) & mask)];
                state.smudgeWrite = (state.smudgeWrite + 1) & mask;
                return shape (style, 0.5 * u + 0.75 * (a + (b - a) * t), drive);
            }
            case Style::breakdown:
            {
                // Pitched down (up to an octave as DRIVE goes up), then clipped hard.
                const auto ratio = (float) std::pow (2.0, -(double) drive);
                const auto down = (double) state.breakdownShifters[(size_t) currentQuality].processSample ((float) u, ratio);
                return shape (style, 0.35 * u + 0.65 * down, drive);
            }
            case Style::rectify:
            {
                const auto y = shape (style, u, drive);
                f[0] += c.hz20 * (y - f[0]);   // the rectified wave's DC taken out
                return std::tanh (1.2 * (y - f[0]));
            }
            case Style::destroy:
            {
                // Fewer bits (in shape()) and a lower sample rate: hold each
                // value for up to 32 samples at the base rate.
                const auto hold = juce::jmax (1, (int) std::round ((1.0 + 31.0 * drive * drive) * rate / baseRate));
                if (--state.holdCount <= 0)
                {
                    state.holdValue = u;
                    state.holdCount = hold;
                }
                return shape (style, state.holdValue, drive);
            }
            case Style::cleanTube:
            case Style::foldback:
                break;
        }
        return shape (style, u, drive);
    }

    double Mars::processBand (int band, double x, const Band& settings) noexcept
    {
        auto& state = bandStates[(size_t) band];
        const auto preGainNow = state.preGain.next();
        const auto makeUpNow = state.makeUp.next();
        const auto mixNow = state.mix.next();
        const auto levelNow = state.level.next();
        if (! settings.on)
            return x * levelNow;

        // FEEDBACK: last sample's output, rung through FEEDBACK FREQ.
        auto in = x + state.feedbackOut;

        // DYNAMICS: right, a pumping compressor; left, a gate/expander.
        if (std::abs (settings.dynamics) > 0.001f)
        {
            const auto level = std::abs (in);
            const auto& c = coefficients;
            state.dynamicsEnvelope += (level > state.dynamicsEnvelope ? c.attack : c.release) * (level - state.dynamicsEnvelope);
            const auto envelopeDb = 20.0 * std::log10 (state.dynamicsEnvelope + 1.0e-6);
            const auto amount = (double) std::abs (settings.dynamics);
            double gainDb = 0.0;
            if (settings.dynamics > 0.0f)
            {
                const auto threshold = -10.0 - 30.0 * amount, ratio = 1.0 + 9.0 * amount;
                if (envelopeDb > threshold)
                    gainDb = (threshold - envelopeDb) * (1.0 - 1.0 / ratio);
                gainDb += -threshold * (1.0 - 1.0 / ratio) * 0.5;   // make-up, so it pumps rather than just quietens
            }
            else
            {
                const auto threshold = -60.0 + 40.0 * amount, ratio = 1.0 + 3.0 * amount;
                if (envelopeDb < threshold)
                    gainDb = juce::jmax (-60.0, (envelopeDb - threshold) * (ratio - 1.0));
            }
            // The compressor clamps fast and lets go slowly; the gate opens
            // fast and closes slowly: both move quickly to let sound through.
            const auto quick = settings.dynamics > 0.0f ? gainDb < state.dynamicsGainDb : gainDb > state.dynamicsGainDb;
            state.dynamicsGainDb += (quick ? c.attack : c.release) * (gainDb - state.dynamicsGainDb);
            in *= std::exp (state.dynamicsGainDb * 0.11512925464970229);   // dB to gain
        }

        // DRIVE into the STYLE, with the level matched.
        auto y = saturate (state, settings.style, in * preGainNow, juce::jlimit (0.0f, 1.0f, settings.drive)) * makeUpNow;

        // The feedback's ring: a resonance at FEEDBACK FREQ.
        if (settings.feedback > 0.001f)
        {
            const auto rung = state.feedbackFilter.process (y).band * state.feedbackFilter.k;
            state.feedbackOut = juce::jlimit (-2.0, 2.0, rung * 0.95 * settings.feedback);
        }
        else
        {
            state.feedbackOut = 0.0;
        }

        // TONE on what the saturation made.
        for (auto& filter : state.tone)
            y = filter.process (y);

        return (x * (1.0 - mixNow) + y * mixNow) * levelNow;
    }

    void Mars::setKnobs (const Settings& settings, bool modulated) noexcept
    {
        // Each knob as set, or (modulated) glided towards its setting over
        // about 20 ms and then moved by the modulation. Knobs that aren't
        // being moved skip the range sums.
        std::array<float, numKnobTargets> knobs {};
        for (int b = 0; b < maxBands; ++b)
        {
            const auto& band = settings.bands[(size_t) b];
            const float values[] = { band.drive, band.dynamics, band.feedback, band.feedbackHz, band.bassDb, band.midDb,
                                     band.trebleDb, band.presenceDb, band.mix, band.levelDb };
            for (int part = 0; part < MarsModulation::bandParts; ++part)
                knobs[(size_t) MarsModulation::bandTarget (b, part)] = values[part];
        }
        const float globals[] = { settings.crossoverHz[0], settings.crossoverHz[1], settings.crossoverHz[2],
                                  settings.inputDb, settings.outputDb, settings.mix };
        for (int part = 0; part < 6; ++part)
            knobs[(size_t) MarsModulation::globalTarget (part)] = globals[part];

        // The first time after prepare, the knobs start where they're set
        // (not gliding up from 0, as they did when a slot was busy from the start).
        const auto first = ! knobsSet;
        if (first)
            smoothedKnobs = knobs;
        knobsSet = true;
        if (modulated)
        {
            const auto glide = (float) (1.0 - std::exp (-(double) tickLength / (0.02 * baseRate)));
            for (size_t t = 1; t < knobs.size(); ++t)
            {
                smoothedKnobs[t] += glide * (knobs[t] - smoothedKnobs[t]);
                knobs[t] = smoothedKnobs[t];
                if (const auto amount = modulation.offset ((int) t); ! juce::exactlyEqual (amount, 0.0f))
                {
                    const auto& range = knobRanges[t];
                    knobs[t] = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (juce::jlimit (range.start, range.end, knobs[t])) + amount));
                }
            }
        }
        else
        {
            smoothedKnobs = knobs;
        }

        for (int b = 0; b < maxBands; ++b)
        {
            auto& band = live[(size_t) b];
            band = settings.bands[(size_t) b];
            const auto knob = [&] (int part) { return knobs[(size_t) MarsModulation::bandTarget (b, part)]; };
            band.drive = knob (MarsModulation::drive);
            band.dynamics = knob (MarsModulation::dynamics);
            band.feedback = knob (MarsModulation::feedback);
            band.feedbackHz = knob (MarsModulation::feedbackFreq);
            band.bassDb = knob (MarsModulation::bass);
            band.midDb = knob (MarsModulation::mid);
            band.trebleDb = knob (MarsModulation::treble);
            band.presenceDb = knob (MarsModulation::presence);
            band.mix = knob (MarsModulation::mix);
            band.levelDb = knob (MarsModulation::level);
        }

        // The glides: one tick long while modulated, so the gains follow it; 50 ms otherwise.
        const auto factor = factors[juce::jlimit (0, 2, currentQuality)];
        const auto steps = modulated ? tickLength * factor : juce::roundToInt (0.05 * rate);
        const auto numBands = juce::jlimit (1, maxBands, settings.numBands);
        liveBands = numBands;

        // The crossovers, kept in order and apart, set again when they move.
        std::array<float, maxBands - 1> crossovers {};
        auto lowest = 30.0f;
        for (size_t i = 0; i < crossovers.size(); ++i)
        {
            crossovers[i] = juce::jlimit (lowest, (float) baseRate * 0.4f, knobs[(size_t) MarsModulation::globalTarget ((int) i)]);
            lowest = crossovers[i] * 1.25f;
        }
        if (crossovers != splitShown || settings.slope != slopeShown || numBands != bandsShown)
        {
            for (size_t i = 0; i < splits.size(); ++i)
                splits[i].set (rate, crossovers[i], settings.slope);
            // The lower bands go through the later splits' all-passes:
            // band 1 through splits 2 and 3, band 2 through split 3.
            compensation[0].set (rate, crossovers[1], settings.slope);
            compensation[1].set (rate, crossovers[2], settings.slope);
            compensation[2].set (rate, crossovers[2], settings.slope);
            if (settings.slope != slopeShown || numBands != bandsShown)
                resetBands();
            splitShown = crossovers;
            slopeShown = settings.slope;
            bandsShown = numBands;
        }

        for (int b = 0; b < maxBands; ++b)
        {
            const auto& band = live[(size_t) b];
            auto& state = bandStates[(size_t) b];
            state.preGain.set (preGain (band.style, band.drive), steps);
            state.makeUp.set (autoGain (band.style, band.drive), steps);
            state.mix.set (juce::jlimit (0.0f, 1.0f, band.mix), steps);
            state.level.set (band.levelDb <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (band.levelDb), steps);
            state.feedbackFilter.set (rate, juce::jlimit (50.0f, 10000.0f, band.feedbackHz), 4.0);
            const std::array<float, 4> tone { band.bassDb, band.midDb, band.trebleDb, band.presenceDb };
            if (tone != state.toneShown)
            {
                using Make = juce::dsp::IIR::ArrayCoefficients<double>;
                const auto gain = [] (float db) { return juce::Decibels::decibelsToGain ((double) db); };
                const std::array<std::array<double, 6>, 4> made {
                    Make::makeLowShelf (rate, 110.0, 0.7, gain (tone[0])),
                    Make::makePeakFilter (rate, 1000.0, 0.7, gain (tone[1])),
                    Make::makeHighShelf (rate, 8000.0, 0.7, gain (tone[2])),
                    Make::makePeakFilter (rate, 4000.0, 1.0, gain (tone[3])) };
                for (size_t i = 0; i < 4; ++i)
                    state.tone[i].c = made[i];
                state.toneShown = tone;
            }
        }
        const auto global = [&] (int part) { return knobs[(size_t) MarsModulation::globalTarget (part)]; };
        inputGain.set (juce::Decibels::decibelsToGain (global (MarsModulation::input)), steps);
        outputGain.set (juce::Decibels::decibelsToGain (global (MarsModulation::output)), steps);
        mix.set (juce::jlimit (0.0f, 1.0f, global (MarsModulation::globalMix)), steps);

        // ...and the gains too: no fade up from 0 the first time.
        if (first)
        {
            for (auto& state : bandStates)
                for (auto* ramp : { &state.preGain, &state.makeUp, &state.mix, &state.level })
                    ramp->jump();
            for (auto* ramp : { &inputGain, &outputGain, &mix })
                ramp->jump();
        }
    }

    void Mars::runSegment (juce::dsp::AudioBlock<float> block) noexcept
    {
        const auto quality = currentQuality;
        const auto numBands = liveBands;

        // Up to the working rate, process, back down.
        auto working = quality == 0 ? block : oversamplers[(size_t) quality]->processSamplesUp (block);
        auto* samples = working.getChannelPointer (0);
        for (size_t i = 0; i < working.getNumSamples(); ++i)
        {
            const auto dry = (double) samples[i];
            const auto in = dry * inputGain.next();

            // Split into bands, keeping the lower ones in phase with later splits.
            std::array<double, maxBands> bandIn {};
            auto rest = in;
            for (int k = 0; k < numBands - 1; ++k)
            {
                const auto [low, high] = splits[(size_t) k].process (rest);
                bandIn[(size_t) k] = low;
                rest = high;
                if (k == 1)
                    bandIn[0] = compensation[0].allPass (bandIn[0]);
                if (k == 2)
                {
                    bandIn[0] = compensation[1].allPass (bandIn[0]);
                    bandIn[1] = compensation[2].allPass (bandIn[1]);
                }
            }
            bandIn[(size_t) numBands - 1] = rest;
            modulation.listen (in, bandIn);

            auto sum = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                drivePeak[(size_t) b] = juce::jmax (drivePeak[(size_t) b],
                                                    (float) (std::abs (bandIn[(size_t) b]) * bandStates[(size_t) b].preGain.getCurrentValue()));
                sum += processBand (b, bandIn[(size_t) b], live[(size_t) b]);
            }

            const auto wet = sum * outputGain.next();
            const auto mixNow = mix.next();
            samples[i] = (float) (dry * (1.0 - mixNow) + wet * mixNow);
        }
        if (quality != 0)
            oversamplers[(size_t) quality]->processSamplesDown (block);
    }

    void Mars::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings, const MarsModulation::Context& context) noexcept
    {
        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            const auto quality = juce::jlimit (0, 2, (int) settings.quality);
            if (quality != currentQuality)
                setRate (quality);
            drivePeak.fill (0.0f);

            // A MIDI note starts the sources once, at the first tick of the block.
            MarsModulation::MidiState laterMidi;
            if (context.midi != nullptr)
            {
                laterMidi = *context.midi;
                laterMidi.blockDone();
            }
            auto tickContext = context;
            const auto numSamples = block.getNumSamples();

            if (! MarsModulation::isActive (settings.modulation))
            {
                // Nothing to move: the sources still run (the panel shows
                // them), once a block, and the knobs are as set.
                modulation.tick (settings.modulation, context, (double) numSamples / baseRate);
                wasModulated = false;
                setKnobs (settings, false);
                runSegment (block);
            }
            else
            {
                if (! wasModulated)
                    untilTick = 0;
                wasModulated = true;
                size_t done = 0;
                while (done < numSamples)
                {
                    if (untilTick <= 0)
                    {
                        if (context.songBeats >= 0.0)
                            tickContext.songBeats = context.songBeats + (double) done / baseRate * context.bpm / 60.0;
                        modulation.tick (settings.modulation, tickContext, (double) tickLength / baseRate);
                        if (context.midi != nullptr)
                            tickContext.midi = &laterMidi;
                        setKnobs (settings, true);
                        untilTick = tickLength;
                    }
                    const auto length = juce::jmin ((size_t) untilTick, numSamples - done);
                    runSegment (block.getSubBlock (done, length));
                    done += length;
                    untilTick -= (int) length;
                }
            }

            for (int b = 0; b < maxBands; ++b)
                bandDriveMeter[(size_t) b].store (b < liveBands ? drivePeak[(size_t) b] : 0.0f);
            for (int source = 0; source < MarsModulation::numSources; ++source)
                shownSources[(size_t) source].store (modulation.sourceValue (source));
            for (int target = 0; target < MarsModulation::numTargets; ++target)
                shownOffsets[(size_t) target].store (modulation.offset (target));
            for (int l = 0; l < MarsModulation::numLfos; ++l)
                shownLfoPositions[(size_t) l].store (modulation.lfoPosition (l));
            for (int e = 0; e < MarsModulation::numEgs; ++e)
                shownEgLevels[(size_t) e].store (modulation.egLevel (e));
            for (int t = 0; t < 5; ++t)
                shownTriggerDb[(size_t) t].store (modulation.triggerLevelDb (t));
        });
    }
}
