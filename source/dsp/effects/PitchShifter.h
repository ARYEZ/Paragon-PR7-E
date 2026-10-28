#pragma once

#include "EffectUtils.h"

namespace effects
{
    // A real-time pitch shifter for one channel: shifts by any ratio from two
    // octaves down (0.25) to two up (4). Used by the FILTER / PITCH block and
    // the SHIMMER delay.
    //
    // A read head moves through the last few tens of milliseconds of audio
    // at the new speed: faster than it was played to go up, slower to go
    // down. Moving faster, it catches up with the present and has to jump
    // back; moving slower, it falls behind and has to jump forward. Each
    // jump lands where the waveform matches what's playing (by correlation)
    // and cross-fades from the old position, so the joins are in phase and
    // the shifted pitch stays clean rather than warbling.
    class PitchShifter
    {
    public:
        void prepare (double sampleRate);
        void reset() noexcept;
        float processSample (float input, float ratio) noexcept;

        // How far behind the input the head starts (it drifts from there as it
        // shifts): the shifter's own delay, in samples.
        float getRestingDelay() const noexcept { return minDelay + (float) jumpSamples * 0.5f; }

    private:
        float readAt (float delay) const noexcept;
        float findSplice (float fromDelay, float direction, float ratioNow) const noexcept;

        std::vector<float> history;   // a circular buffer, a power of two long
        int historyMask = 0, writeIndex = 0;
        float headDelay = 0.0f, fadingDelay = 0.0f;
        int fadeRemaining = 0;
        int jumpSamples = 0, searchSamples = 0, fadeSamples = 0;
        float minDelay = 4.0f, maxDelay = 0.0f;
    };
}
