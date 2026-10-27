#pragma once

#include "PluginProcessor.h"
#include "ui/RackUnit.h"

// The four rack units' specs (0.18.0): ECHELON, DOMINION, CONSTRICTOR and
// the LIMITER, in the signal chain's order: their keys, knobs, ladders and
// what their screens show.
struct RackActions
{
    std::function<void (int unit)> openPage;      // 0 ECHELON ... 3 LIMITER
    std::function<void()> openMatch;              // ECHELON's page with MATCH open
    std::function<bool()> analyserOn;             // ECHELON's analyser
    std::function<void (bool)> setAnalyser;
};

std::array<RackUnit::Spec, 4> makeRackSpecs (ParagonX1AudioProcessor&, RackActions);
