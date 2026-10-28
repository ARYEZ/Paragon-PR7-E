#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>

// A rig: one complete sound. Every effect setting, and how it's filed (name, bank, number, tags).
//
// The INPUT and OUTPUT knobs at the sides aren't part of a rig: they suit
// the player's instrument and monitoring, not a particular sound, so switching
// rigs leaves them alone (as on hardware modellers). Each rig has its own
// RIG LEVEL for balancing rigs against each other instead.
struct Rig
{
    // The banks (0.21.0, the owner's layout): A drums, B bass, C guitars,
    // D synths and keys, E vocals, F the mix, G mastering. (Before, five: A
    // vocals, B drums, C bass, guitars and keys, D mix, E mastering; rigs
    // saved then move to their new bank as they load: see fromXml.)
    enum Bank { drums, bass, guitars, synths, vocals, mix, master };
    static constexpr int numBanks = 7;          // A to G
    static constexpr int maxNumber = 99;

    juce::String name { "New Rig" };
    int bank = 0;                               // 0 = A
    int number = 1;                             // 1 to maxNumber
    bool favourite = false;
    juce::StringArray tags;

    // Parameter values in their real units (dB, ms...), by parameter ID.
    // Storing real values rather than 0-1 positions means a rig still
    // loads correctly if a later version changes a knob's range. Loading a
    // rig resets any parameter it doesn't mention to its default, so rigs
    // saved before a parameter existed get that parameter's neutral setting.
    std::map<juce::String, float> parameters;

    // Free text shown with the rig: what it's for.
    juce::String notes;

    juce::String getSlotName() const;           // e.g. "A01"
    static juce::String bankName (int bank);    // "A".."G"
    static juce::String bankTitle (int bank);   // "Drums", "Bass" ...

    std::unique_ptr<juce::XmlElement> toXml() const;
    // Returns nothing if the XML isn't a rig.
    static std::optional<Rig> fromXml (const juce::XmlElement&);

    static constexpr const char* fileExtension = ".pr7rig";
};
