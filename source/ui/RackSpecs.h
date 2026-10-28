#pragma once

#include "PluginProcessor.h"
#include "ui/RackUnit.h"

// The rack units' specs (four in 0.18.0, six since 0.22.0): ECHELON,
// DOMINION, CONSTRICTOR, MARS, BINARY and GUILLOTINE (the LIMITER), in the
// signal chain's order: their keys, knobs, ladders and what their screens show.
struct RackActions
{
    std::function<void (int unit)> openPage;      // 0 ECHELON ... 5 GUILLOTINE
    std::function<void()> openMatch;              // ECHELON's page with MATCH open
    std::function<bool()> analyserOn;             // ECHELON's analyser
    std::function<void (bool)> setAnalyser;
};

std::array<RackUnit::Spec, 6> makeRackSpecs (ParagonX1AudioProcessor&, RackActions);
