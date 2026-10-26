#include "dsp/effects/Loudness.h"

namespace effects
{
    std::array<LoudnessMeter::Biquad, 2> LoudnessMeter::kWeighting (double rate)
    {
        // BS.1770's two stages, re-derived for any sample rate from their
        // analog prototypes (at 48 kHz they give the standard's coefficients).
        std::array<Biquad, 2> stages;
        {
            const auto f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto vh = std::pow (10.0, gainDb / 20.0);
            const auto vb = std::pow (vh, 0.4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            auto& s = stages[0];
            s.b0 = (vh + vb * k / q + k * k) / a0;
            s.b1 = 2.0 * (k * k - vh) / a0;
            s.b2 = (vh - vb * k / q + k * k) / a0;
            s.a1 = 2.0 * (k * k - 1.0) / a0;
            s.a2 = (1.0 - k / q + k * k) / a0;
        }
        {
            const auto f0 = 38.13547087602444, q = 0.5003270373238773;
            const auto k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const auto a0 = 1.0 + k / q + k * k;
            auto& s = stages[1];
            s.b0 = 1.0;
            s.b1 = -2.0;
            s.b2 = 1.0;
            s.a1 = 2.0 * (k * k - 1.0) / a0;
            s.a2 = (1.0 - k / q + k * k) / a0;
        }
        return stages;
    }

    void LoudnessMeter::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        hopLength = juce::jmax (1, (int) std::lround (0.1 * sampleRate));
        blockCounts.assign ((size_t) numBins, 0);
        shortCounts.assign ((size_t) numBins, 0);
        blockPower.assign ((size_t) numBins, 0.0);
        shortPower.assign ((size_t) numBins, 0.0);
        reset();
    }

    void LoudnessMeter::reset() noexcept
    {
        const auto weighting = kWeighting (sampleRate);
        for (auto& channel : filters)
            channel = weighting;
        inHop = hopsSeen = hopIndex = 0;
        hopSum = 0.0;
        hops.fill (0.0);
        std::fill (blockCounts.begin(), blockCounts.end(), 0);
        std::fill (shortCounts.begin(), shortCounts.end(), 0);
        std::fill (blockPower.begin(), blockPower.end(), 0.0);
        std::fill (shortPower.begin(), shortPower.end(), 0.0);
        for (auto* reading : { &momentary, &shortTerm, &integrated, &maxMomentary, &maxShortTerm })
            reading->store (silent);
        range.store (0.0f);
    }

    float LoudnessMeter::lufsFor (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : silent;
    }

    int LoudnessMeter::binFor (float lufs) noexcept
    {
        return juce::jlimit (0, numBins - 1, (int) std::floor ((lufs - lowestLufs) * 10.0f));
    }

    void LoudnessMeter::process (const float* left, const float* right, int numSamples) noexcept
    {
        if (resetRequested.exchange (false))
            reset();
        for (int i = 0; i < numSamples; ++i)
        {
            const auto l = filters[0][1].process (filters[0][0].process ((double) left[i]));
            hopSum += l * l;
            if (right != nullptr)
            {
                const auto r = filters[1][1].process (filters[1][0].process ((double) right[i]));
                hopSum += r * r;
            }
            if (++inHop == hopLength)
                endOfHop();
        }
    }

    void LoudnessMeter::endOfHop() noexcept
    {
        hops[(size_t) hopIndex] = hopSum / (double) hopLength;
        hopIndex = (hopIndex + 1) % (int) hops.size();
        hopSum = 0.0;
        inHop = 0;
        ++hopsSeen;

        const auto meanOf = [this] (int count)
        {
            double sum = 0.0;
            for (int i = 1; i <= count; ++i)
                sum += hops[(size_t) ((hopIndex - i + (int) hops.size()) % (int) hops.size())];
            return sum / count;
        };

        // A 400 ms block every 100 ms: the momentary reading, and a gated block for INTEGRATED.
        if (hopsSeen >= 4)
        {
            const auto power = meanOf (4);
            const auto lufs = lufsFor (power);
            momentary.store (lufs);
            maxMomentary.store (juce::jmax (maxMomentary.load(), lufs));
            if (lufs > lowestLufs)
            {
                const auto bin = (size_t) binFor (lufs);
                ++blockCounts[bin];
                blockPower[bin] += power;
                updateIntegrated();
            }
        }
        // Every 100 ms once there are 3 s: the short-term reading, and one for the RANGE.
        if (hopsSeen >= (int) hops.size())
        {
            const auto power = meanOf ((int) hops.size());
            const auto lufs = lufsFor (power);
            shortTerm.store (lufs);
            maxShortTerm.store (juce::jmax (maxShortTerm.load(), lufs));
            if (lufs > lowestLufs)
            {
                const auto bin = (size_t) binFor (lufs);
                ++shortCounts[bin];
                shortPower[bin] += power;
                updateRange();
            }
        }
    }

    void LoudnessMeter::updateIntegrated() noexcept
    {
        // The mean power of every block above -70 LUFS sets the relative
        // gate, 10 LU under it; INTEGRATED is the mean of the blocks above both.
        double power = 0.0;
        long count = 0;
        for (int bin = 0; bin < numBins; ++bin)
        {
            power += blockPower[(size_t) bin];
            count += blockCounts[(size_t) bin];
        }
        if (count == 0)
            return;
        const auto gate = lufsFor (power / (double) count) - 10.0f;
        power = 0.0;
        count = 0;
        for (int bin = binFor (gate); bin < numBins; ++bin)
        {
            // The bin the gate falls in counts if its middle is above the gate.
            if (bin == binFor (gate) && lowestLufs + ((float) bin + 0.5f) * 0.1f < gate)
                continue;
            power += blockPower[(size_t) bin];
            count += blockCounts[(size_t) bin];
        }
        if (count > 0)
            integrated.store (lufsFor (power / (double) count));
    }

    void LoudnessMeter::updateRange() noexcept
    {
        // EBU Tech 3342: gated 20 LU under the short-term readings' mean, then
        // the 95th percentile less the 10th.
        double power = 0.0;
        long count = 0;
        for (int bin = 0; bin < numBins; ++bin)
        {
            power += shortPower[(size_t) bin];
            count += shortCounts[(size_t) bin];
        }
        if (count == 0)
            return;
        const auto first = binFor (lufsFor (power / (double) count) - 20.0f);
        long total = 0;
        for (int bin = first; bin < numBins; ++bin)
            total += shortCounts[(size_t) bin];
        if (total < 2)
        {
            range.store (0.0f);
            return;
        }
        const auto percentile = [&] (double fraction)
        {
            const auto wanted = fraction * (double) (total - 1);
            long seen = 0;
            for (int bin = first; bin < numBins; ++bin)
            {
                seen += shortCounts[(size_t) bin];
                if ((double) seen > wanted)
                    return lowestLufs + ((float) bin + 0.5f) * 0.1f;
            }
            return lowestLufs + (float) numBins * 0.1f;
        };
        range.store (juce::jmax (0.0f, percentile (0.95) - percentile (0.10)));
    }
}
