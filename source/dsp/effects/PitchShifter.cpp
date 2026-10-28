#include "PitchShifter.h"

namespace effects
{
    namespace
    {
        // The read head jumps by about 25 ms at a time (long enough that the
        // joins are infrequent, short enough to keep the delay small),
        // searching 12.5 ms either side for the best-matching point (a full
        // cycle of the lowest guitar notes), then cross-fades over 8 ms.
        constexpr double jumpSeconds = 0.025, searchSeconds = 0.0125, fadeSeconds = 0.008;
        constexpr int maxRatio = 4;   // two octaves up
    }

    void PitchShifter::prepare (double sampleRate)
    {
        jumpSamples = (int) std::round (jumpSeconds * sampleRate);
        searchSamples = (int) std::round (searchSeconds * sampleRate);
        fadeSamples = (int) std::round (fadeSeconds * sampleRate);
        // Room for the head to run ahead while fading at the fastest speed,
        // plus the longest jump back.
        maxDelay = minDelay + (float) (fadeSamples * maxRatio + jumpSamples + searchSamples + 8);
        history.assign ((size_t) juce::nextPowerOfTwo ((int) maxDelay + fadeSamples * maxRatio + 16), 0.0f);
        historyMask = (int) history.size() - 1;
        reset();
    }

    void PitchShifter::reset() noexcept
    {
        std::fill (history.begin(), history.end(), 0.0f);
        writeIndex = 0;
        headDelay = fadingDelay = minDelay + (float) jumpSamples * 0.5f;
        fadeRemaining = 0;
    }

    float PitchShifter::readAt (float delay) const noexcept
    {
        // Cubic (Hermite) interpolation between the samples either side.
        const auto position = (float) writeIndex - delay;
        const auto whole = (int) std::floor (position);
        const auto t = position - (float) whole;
        const auto y0 = history[(size_t) ((whole - 1) & historyMask)], y1 = history[(size_t) (whole & historyMask)],
                   y2 = history[(size_t) ((whole + 1) & historyMask)], y3 = history[(size_t) ((whole + 2) & historyMask)];
        const auto c1 = 0.5f * (y2 - y0);
        const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float PitchShifter::findSplice (float fromDelay, float direction, float ratioNow) const noexcept
    {
        // Where to jump to: about a jump's length away (`direction` +1 is
        // further back in time, -1 more recent), at the point where the
        // guitar's waveform best matches what the current head is about to
        // play. Both heads then play the same shape during the cross-fade,
        // so they reinforce rather than cancel, and the pitch stays clean.
        // Scores each candidate by normalised correlation, every other sample.
        // Only guitar that has already arrived can be compared: the stretch
        // just before the current head (what it has just played) and as much
        // ahead of it as has been recorded.
        const auto ahead = juce::jmin (384, juce::jmax (0, (int) fromDelay - 2), (int) ((float) fadeSamples * ratioNow) + 1);
        constexpr int behind = 256;
        const auto from = writeIndex - (int) std::round (fromDelay);
        const auto score = [&] (int candidateDelay)
        {
            const auto start = writeIndex - candidateDelay;
            float product = 0.0f, energy = 1.0e-9f;
            for (int m = -behind; m < ahead; m += 2)
            {
                const auto b = history[(size_t) ((start + m) & historyMask)];
                product += history[(size_t) ((from + m) & historyMask)] * b;
                energy += b * b;
            }
            return product / std::sqrt (energy);
        };

        const auto centre = fromDelay + direction * (float) jumpSamples;
        const auto lowest = juce::jmax ((int) minDelay + 1, (int) (centre - (float) searchSamples));
        const auto highest = juce::jmin ((int) maxDelay - 1, (int) (centre + (float) searchSamples));

        // Coarse search in steps of 2, then refine either side of the best.
        auto best = juce::jlimit (lowest, highest, (int) centre);
        auto bestScore = -1.0e30f;
        for (auto candidate = lowest; candidate <= highest; candidate += 2)
            if (const auto value = score (candidate); value > bestScore)
            {
                bestScore = value;
                best = candidate;
            }
        for (const auto candidate : { best - 1, best + 1 })
            if (candidate >= lowest && candidate <= highest)
                if (const auto value = score (candidate); value > bestScore)
                {
                    bestScore = value;
                    best = candidate;
                }

        // Keep the fraction, so the new head starts exactly in step.
        return (float) best + (fromDelay - std::floor (fromDelay));
    }

    float PitchShifter::processSample (float input, float ratio) noexcept
    {
        history[(size_t) writeIndex] = input;
        writeIndex = (writeIndex + 1) & historyMask;

        const auto step = ratio - 1.0f;   // how much the delay shrinks each sample
        headDelay -= step;
        if (fadeRemaining > 0)
            fadingDelay -= step;

        // Jump before the head (and the old head, still sounding while it
        // fades) runs out of room.
        if (fadeRemaining == 0)
        {
            const auto runway = (float) fadeSamples * std::abs (step) + 2.0f;
            if (step > 0.0f && headDelay < minDelay + runway)
            {
                fadingDelay = headDelay;
                headDelay = findSplice (headDelay, 1.0f, ratio);
                fadeRemaining = fadeSamples;
            }
            else if (step < 0.0f && headDelay > maxDelay - runway)
            {
                fadingDelay = headDelay;
                headDelay = findSplice (headDelay, -1.0f, ratio);
                fadeRemaining = fadeSamples;
            }
        }
        headDelay = juce::jlimit (minDelay, maxDelay, headDelay);

        auto output = readAt (headDelay);
        if (fadeRemaining > 0)
        {
            // Raised-cosine fade: equal gain, as the two heads are in step.
            const auto t = 1.0f - (float) fadeRemaining / (float) fadeSamples;
            const auto in = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t);
            output = output * in + readAt (juce::jlimit (minDelay, maxDelay, fadingDelay)) * (1.0f - in);
            --fadeRemaining;
        }
        return output;
    }
}
