#pragma once

#include <array>
#include <atomic>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace effects
{
    // A loudness meter after ITU-R BS.1770-4 and EBU R128 (our own code, for
    // the LIMITER, 0.16.0): the signal is K-weighted (a high shelf for the
    // head, a low cut), and its power summed over the channels.
    //
    //   MOMENTARY   the last 400 ms
    //   SHORT TERM  the last 3 s
    //   INTEGRATED  everything since the last reset, gated as BS.1770-4 says
    //               (400 ms blocks every 100 ms; those under -70 LUFS left
    //               out, then those more than 10 LU under the rest)
    //   RANGE (LRA) as EBU Tech 3342: the spread between the 10th and 95th
    //               percentiles of the short-term readings (gated at -70
    //               LUFS and 20 LU under their mean)
    //
    // Readings are in LUFS (LU for the range); `silent` means nothing to show.
    // process() runs on the audio thread; the readings are safe to read from
    // any thread.
    class LoudnessMeter
    {
    public:
        static constexpr float silent = -100.0f;

        void prepare (double sampleRate);
        void reset() noexcept;
        // From the UI: the audio thread clears everything before its next block.
        void requestReset() noexcept { resetRequested.store (true); }

        // `right` may be null (mono).
        void process (const float* left, const float* right, int numSamples) noexcept;

        float getMomentary() const noexcept { return momentary.load(); }
        float getShortTerm() const noexcept { return shortTerm.load(); }
        float getIntegrated() const noexcept { return integrated.load(); }
        float getRange() const noexcept { return range.load(); }
        float getMaxMomentary() const noexcept { return maxMomentary.load(); }
        float getMaxShortTerm() const noexcept { return maxShortTerm.load(); }

        // BS.1770's K-weighting, as a pair of biquads (exposed for the tests).
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;
            double process (double x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };
        static std::array<Biquad, 2> kWeighting (double sampleRate);

    private:
        static constexpr int numBins = 1000;           // 0.1 LU each, from -70 LUFS up
        static constexpr float lowestLufs = -70.0f;
        static float lufsFor (double meanSquare) noexcept;
        static int binFor (float lufs) noexcept;
        void endOfHop() noexcept;
        void updateIntegrated() noexcept;
        void updateRange() noexcept;

        double sampleRate = 48000.0;
        int hopLength = 4800, inHop = 0, hopsSeen = 0;
        std::array<std::array<Biquad, 2>, 2> filters {};
        double hopSum = 0.0;
        std::array<double, 30> hops {};                 // the last 3 s, 100 ms each (summed power)
        int hopIndex = 0;

        // Gated blocks, by loudness: how many, and their power.
        std::vector<int> blockCounts, shortCounts;
        std::vector<double> blockPower, shortPower;

        std::atomic<bool> resetRequested { false };
        std::atomic<float> momentary { silent }, shortTerm { silent }, integrated { silent }, range { 0.0f },
                           maxMomentary { silent }, maxShortTerm { silent };
    };
}
