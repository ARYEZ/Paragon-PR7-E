#pragma once

#include "Rig.h"

#include <juce_core/juce_core.h>

#include <vector>

// Starter sounds that ship with the plugin.
std::vector<Rig> makeFactoryRigs();

// Signature sounds (0.19.0), at the owner's ask: each inspired by how a
// particular record sounds, from what's published about its production and
// its measured loudness. Named for the sound, never the artist or record
// (no real names on the plugin). Each has six rigs (vocal, drums, guitars,
// bass, mix, master, at x31 to x38 in their banks) and presets of the same
// name in CONSTRICTOR, DOMINION and the LIMITER ("Signature Sounds/<name>").
inline const juce::StringArray signatureSounds { "Ethereal Djent", "Dark Synth Metal", "Raw 90s Nu Metal", "Clean Prog Djent",
                                                 "Cinematic Post-Hardcore", "Swedish Melodic Death", "Early 2000s Metalcore",
                                                 "Polished Nu Metal" };
