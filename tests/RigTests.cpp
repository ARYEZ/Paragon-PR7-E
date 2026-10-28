#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/CompPresets.h"
#include "rigs/DominionPresets.h"
#include "rigs/FactoryRigs.h"
#include "rigs/LimiterPresets.h"
#include "rigs/MarsPresets.h"

using Catch::Matchers::WithinAbs;

namespace
{
    // A fresh, empty rigs folder for each test.
    struct TempRigFolder
    {
        TempRigFolder() { folder.deleteRecursively(); }
        ~TempRigFolder() { folder.deleteRecursively(); }
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1RigTests")
                                .getChildFile (juce::Uuid().toString());
    };

    struct RigRig   // a processor with its rigs in a temporary folder
    {
        RigRig()
        {
            processor.setPlayConfigDetails (1, 2, 48000.0, 256);
            processor.prepareToPlay (48000.0, 256);
            processor.useRigFolder (temp.folder);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        float get (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); }

        const RigLibrary::Entry& entryNamed (const juce::String& name)
        {
            for (const auto& entry : processor.getRigLibrary().getEntries())
                if (entry.rig.name == name)
                    return entry;
            FAIL ("No rig called " << name);
            return processor.getRigLibrary().getEntries().front();
        }

        TempRigFolder temp;
        ParagonX1AudioProcessor processor;
    };
}

TEST_CASE ("Rigs saved with the five banks (before 0.21.0) move to their bank in the seven", "[rigs]")
{
    // Before: A vocals, B drums, C bass, guitars and keys, D mix, E mastering.
    // Now: A drums, B bass, C guitars, D synths and keys, E vocals, F mix, G master.
    const auto load = [] (const char* bank, const char* tags)
    {
        juce::XmlElement xml ("ParagonRig");
        xml.setAttribute ("version", 1);
        xml.setAttribute ("name", "Old");
        xml.setAttribute ("bank", bank);
        xml.setAttribute ("number", 9);
        xml.setAttribute ("tags", tags);
        const auto rig = Rig::fromXml (xml);
        REQUIRE (rig.has_value());
        CHECK (rig->number == 9);
        return rig->getSlotName();
    };
    CHECK (load ("A", "Vocal") == "E09");
    CHECK (load ("B", "Drums") == "A09");
    CHECK (load ("C", "Bass,Mine") == "B09");
    CHECK (load ("C", "808") == "B09");
    CHECK (load ("C", "Keys,Piano") == "D09");
    CHECK (load ("C", "Synth") == "D09");
    CHECK (load ("C", "Guitar") == "C09");
    CHECK (load ("C", "") == "C09");   // no tags: guitars, the most common
    CHECK (load ("D", "") == "F09");
    CHECK (load ("E", "") == "G09");

    // Saved now: it stays where it is.
    Rig rig;
    rig.bank = Rig::mix;
    rig.number = 4;
    const auto again = Rig::fromXml (*rig.toXml());
    REQUIRE (again.has_value());
    CHECK (again->getSlotName() == "F04");
    CHECK (Rig::bankTitle (Rig::drums) == "Drums");
    CHECK (Rig::bankTitle (Rig::master) == "Master");
}

TEST_CASE ("A rig survives being written to a file and read back", "[rigs]")
{
    Rig rig;
    rig.name = "AC/DC: \"Back in Black\" & <more>";
    rig.bank = 2;
    rig.number = 17;
    rig.favourite = true;
    rig.tags = { "Rock", "High Gain" };
    rig.parameters = { { "eqOutput", 6.5f }, { "presence", -4.0f } };

    const auto restored = Rig::fromXml (*juce::XmlDocument::parse (rig.toXml()->toString()));
    REQUIRE (restored.has_value());
    CHECK (restored->name == rig.name);
    CHECK (restored->getSlotName() == "C17");
    CHECK (restored->favourite);
    CHECK (restored->tags == rig.tags);
    CHECK (restored->parameters == rig.parameters);
}

TEST_CASE ("Files that aren't rigs, or are from a newer version, are refused", "[rigs]")
{
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<SomethingElse/>")).has_value());
    CHECK_FALSE (Rig::fromXml (*juce::XmlDocument::parse ("<ParagonRig version=\"99\" name=\"x\"/>")).has_value());
}

TEST_CASE ("Every factory rig is valid", "[rigs]")
{
    ParagonX1AudioProcessor processor;
    juce::StringArray names, slots;

    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        names.addIfNotAlreadyThere (rig.name);
        slots.addIfNotAlreadyThere (rig.getSlotName());

        for (const auto& [id, value] : rig.parameters)
        {
            INFO (id);
            auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters().getParameter (id));
            REQUIRE (parameter != nullptr);                          // no typos in parameter IDs
            CHECK_FALSE (ParamIDs::notInRigs.contains (id));        // rigs don't touch global settings
            const auto range = parameter->getNormalisableRange();
            CHECK (value >= range.start);
            CHECK (value <= range.end);
        }
    }

    CHECK (names.size() == (int) makeFactoryRigs().size());   // unique names
    CHECK (slots.size() == (int) makeFactoryRigs().size());   // no two in the same slot
}

TEST_CASE ("PR7-X's factory rigs: in seven banks (A drums ... G master), Init first at E01; PR7-B's are gone", "[rigs]")
{
    const auto rigs = makeFactoryRigs();
    REQUIRE (rigs.size() > 25);
    CHECK (rigs.front().name == "Init");
    CHECK (rigs.front().getSlotName() == "E01");   // where it was, among the vocals, before 0.21.0 (A01)
    CHECK (rigs.front().parameters.empty());   // everything at its default: every block off

    // Each bank has at least four, each says what it's for, and each (but
    // Init) switches the EQ on.
    std::array<int, Rig::numBanks> perBank {};
    for (const auto& rig : rigs)
    {
        INFO (rig.name);
        ++perBank[(size_t) rig.bank];
        CHECK (rig.notes.isNotEmpty());
        CHECK_FALSE (rig.tags.isEmpty());
        if (rig.name != "Init")
            CHECK (rig.parameters.at (ParamIDs::eqOn) == 1.0f);
    }
    for (const auto count : perBank)
        CHECK (count >= 4);

    RigRig r;
    for (const auto* bassRig : { "Clean DI", "Studio Bass", "Chorus Bass", "Dub Delay", "Hysteria (Muse)", "Absolomb (Periphery)" })
        CHECK (r.processor.getRigLibrary().findFactory (bassRig) == nullptr);
}

TEST_CASE ("Genre rigs (0.15.0): six parts in nine genres, at the genre's number in each bank, with the genre's presets", "[rigs][genres]")
{
    const juce::StringArray genres { "Modern Metal", "Metalcore", "Djent", "Nu Metal", "Argent Metal",
                                     "Hard Rock", "Grunge", "Symphonic Metal", "Industrial" };
    const auto rigs = makeFactoryRigs();
    const auto find = [&rigs] (const juce::String& name) -> const Rig*
    {
        for (const auto& rig : rigs)
            if (rig.name == name)
                return &rig;
        return nullptr;
    };
    // Every parameter a preset sets is the rig's, and the rest of that stomp's at its default (not mentioned).
    const auto matches = [] (const Rig& rig, const std::vector<StompPreset>& presets, const juce::String& genre, const juce::String& presetName,
                             bool (*holds) (const juce::String&))
    {
        const auto preset = std::find_if (presets.begin(), presets.end(), [&] (const StompPreset& p) { return p.name == presetName; });
        REQUIRE (preset != presets.end());
        CHECK (preset->category == "Genres/" + genre);
        for (const auto& [id, value] : rig.parameters)
            if (holds (id))
            {
                INFO (id);
                REQUIRE (preset->parameters.count (id) == 1);
                CHECK (preset->parameters.at (id) == value);
            }
        for (const auto& [id, value] : preset->parameters)
            CHECK (rig.parameters.count (id) == 1);
    };

    for (int g = 0; g < genres.size(); ++g)
    {
        const auto genre = genres[g];
        INFO (genre);
        struct Part { const char* name; const char* slot; const char* comp; const char* dominion; };
        const Part parts[] = { { "Vocal", "E", "Vocal", "Vocal" }, { "Drums", "A", "Drums", nullptr }, { "Guitars", "C", nullptr, "Guitars" },
                               { "Bass", "B", "Bass", nullptr }, { "Mix", "F", "Mix Bus", "Mix" }, { "Master", "G", "Master", nullptr } };
        for (const auto& part : parts)
        {
            INFO (part.name);
            const auto* rig = find (genre + " " + part.name);
            REQUIRE (rig != nullptr);
            const auto number = 11 + g;
            CHECK (rig->getSlotName() == juce::String (part.slot) + juce::String (number));
            CHECK (rig->tags.contains (genre));
            CHECK (rig->tags.contains ("Genre"));

            CHECK ((rig->parameters.count (ParamIDs::compOn) == 1) == (part.comp != nullptr));
            if (part.comp != nullptr)
            {
                CHECK (rig->parameters.at (ParamIDs::compOn) == 1.0f);
                matches (*rig, CompPresets::factory(), genre, genre + " " + part.comp, &CompPresets::holds);
                CHECK (rig->notes.contains (genre + " " + part.comp));
            }
            // The master has DOMINION too since 0.22.1 (the whole rack): the gentle "Master Polish".
            CHECK ((rig->parameters.count (ParamIDs::domOn) == 1) == (part.dominion != nullptr || juce::String (part.name) == "Master"));
            if (part.dominion == nullptr && juce::String (part.name) == "Master")
                CHECK (rig->notes.contains ("DOMINION: its \"Master Polish\" preset."));
            if (part.dominion != nullptr)
            {
                CHECK (rig->parameters.at (ParamIDs::domOn) == 1.0f);
                matches (*rig, DominionPresets::factory(), genre, genre + " " + part.dominion, &DominionPresets::holds);
            }
            if (juce::String (part.name) == "Master")
                CHECK (rig->parameters.at (ParamIDs::limitOn) == 1.0f);
        }
    }
}

TEST_CASE ("A rig's notes are saved with it and kept by Save As", "[rigs]")
{
    Rig rig;
    rig.name = "Noted";
    rig.notes = "For the mix bus.\nTrim first, then limit.";
    const auto loaded = Rig::fromXml (*rig.toXml());
    REQUIRE (loaded.has_value());
    CHECK (loaded->notes == rig.notes);

    // Saving a copy of a factory rig keeps its notes.
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("My Init"));
    const auto* saved = r.processor.getRigLibrary().find (r.processor.getCurrentRig().file);
    REQUIRE (saved != nullptr);
    CHECK (saved->rig.notes == r.entryNamed ("Init").rig.notes);
}

TEST_CASE ("The rig library saves, finds, renames and deletes rigs", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);
    const auto factoryCount = library.getEntries().size();

    Rig rig;
    rig.name = "Cyber Drive";
    const auto first = library.saveAsNew (rig);
    REQUIRE (first.existsAsFile());
    REQUIRE (library.find (first) != nullptr);

    SECTION ("A taken name gets a number")
    {
        const auto second = library.saveAsNew (rig);
        REQUIRE (library.find (second) != nullptr);
        CHECK (library.find (second)->rig.name == "Cyber Drive 2");
        CHECK (second != first);
    }

    SECTION ("Names that can't be file names still save")
    {
        rig.name = "AC/DC: Live?";
        const auto file = library.saveAsNew (rig);
        REQUIRE (file.existsAsFile());
        CHECK (library.find (file)->rig.name == "AC/DC: Live?");
    }

    SECTION ("Overwrite and delete")
    {
        auto changed = library.find (first)->rig;
        changed.tags = { "Lead" };
        REQUIRE (library.overwrite (first, changed));
        CHECK (library.find (first)->rig.tags == juce::StringArray { "Lead" });

        REQUIRE (library.remove (first));
        CHECK (library.find (first) == nullptr);
        CHECK (library.getEntries().size() == factoryCount);
    }

    SECTION ("A corrupt rig file is skipped, not fatal")
    {
        temp.folder.getChildFile (juce::String ("broken") + Rig::fileExtension).replaceWithText ("<ParagonRig name=");
        library.rescan();
        CHECK (library.getEntries().size() == factoryCount + 1);
    }
}

TEST_CASE ("Signature sounds (0.19.0): six parts each, at x31 to x38 (bass C41 to C48), with their presets in all three stomps", "[rigs][signatures]")
{
    const auto rigs = makeFactoryRigs();
    const auto find = [&rigs] (const juce::String& name) -> const Rig*
    {
        for (const auto& rig : rigs)
            if (rig.name == name)
                return &rig;
        return nullptr;
    };
    const auto hasPreset = [] (const std::vector<StompPreset>& presets, const juce::String& name, const juce::String& sound)
    {
        return std::any_of (presets.begin(), presets.end(), [&] (const StompPreset& p) { return p.name == name && p.category == "Signature Sounds/" + sound; });
    };
    REQUIRE (signatureSounds.size() == 8);
    for (int i = 0; i < signatureSounds.size(); ++i)
    {
        const auto sound = signatureSounds[i];
        INFO (sound);
        struct Part { const char* name; const char* slot; };
        for (const auto& part : { Part { "Vocal", "E" }, Part { "Drums", "A" }, Part { "Guitars", "C" }, Part { "Bass", "B" },
                                  Part { "Mix", "F" }, Part { "Master", "G" } })
        {
            INFO (part.name);
            const auto* rig = find (sound + " " + part.name);
            REQUIRE (rig != nullptr);
            CHECK (rig->getSlotName() == juce::String (part.slot) + juce::String (31 + i));
            CHECK (rig->tags.contains (sound));
            CHECK (rig->tags.contains ("Signature"));
        }
        for (const auto* part : { "Vocal", "Drums", "Bass", "Mix Bus", "Master" })
            CHECK (hasPreset (CompPresets::factory(), sound + " " + part, sound));
        for (const auto* part : { "Vocal", "Guitars", "Mix" })
            CHECK (hasPreset (DominionPresets::factory(), sound + " " + part, sound));
        CHECK (hasPreset (LimiterPresets::factory(), sound + " Master", sound));
        CHECK (find (sound + " Master")->parameters.at (ParamIDs::limitOn) == 1.0f);

        // No real names: not the artist or record, in any name, tag or note.
        for (const auto* name : { "Spiritbox", "Eternal Blue", "Bad Omens", "Peace of Mind", "Korn", "Periphery", "Select Difficulty",
                                  "Crown the Empire", "Resistance", "Soilwork", "Stabbing", "Killswitch", "Heartache", "Linkin", "Hybrid Theory" })
            for (const auto& rig : rigs)
            {
                CHECK_FALSE (rig.name.containsIgnoreCase (name));
                CHECK_FALSE (rig.notes.containsIgnoreCase (name));
                CHECK_FALSE (rig.tags.joinIntoString (" ").containsIgnoreCase (name));
            }
    }
}

TEST_CASE ("Every preset a factory rig names in its notes exists, and the rig holds exactly its settings", "[rigs][presets]")
{
    // "CONSTRICTOR: its "X" preset." (or COMP, DOMINION, GUILLOTINE): the
    // stomp's PRESET menu then holds what the rig has.
    struct Stomp { const char* label; const std::vector<StompPreset>& presets; bool (*holds) (const juce::String&); };
    const Stomp stomps[] = { { "CONSTRICTOR", CompPresets::factory(), &CompPresets::holds }, { "COMP", CompPresets::factory(), &CompPresets::holds },
                             { "DOMINION", DominionPresets::factory(), &DominionPresets::holds },
                             { "GUILLOTINE", LimiterPresets::factory(), &LimiterPresets::holds },
                             { "MARS", MarsPresets::factory(), &MarsPresets::holds } };
    int checked = 0;
    for (const auto& rig : makeFactoryRigs())
        for (const auto& stomp : stomps)
        {
            const auto marker = juce::String (stomp.label) + ": its \"";
            if (! rig.notes.contains (marker))
                continue;
            const auto name = rig.notes.fromFirstOccurrenceOf (marker, false, false).upToFirstOccurrenceOf ("\"", false, false);
            INFO (rig.name << " / " << stomp.label << " " << name);
            const auto preset = std::find_if (stomp.presets.begin(), stomp.presets.end(), [&] (const StompPreset& p) { return p.name == name; });
            REQUIRE (preset != stomp.presets.end());
            for (const auto& [id, value] : preset->parameters)
                if (id != LimiterPresetTraits::targetKey)
                {
                    INFO (id);
                    REQUIRE (rig.parameters.count (id) == 1);
                    CHECK (rig.parameters.at (id) == value);
                }
            for (const auto& [id, value] : rig.parameters)
                if (stomp.holds (id) && id != ParamIDs::limitOn)
                    CHECK (preset->parameters.count (id) == 1);
            ++checked;
        }
    CHECK (checked > 150);
}

TEST_CASE ("Favourites stick, including on factory rigs", "[rigs]")
{
    TempRigFolder temp;
    {
        RigLibrary library (temp.folder);
        library.setFavourite (*library.findFactory ("Init"), true);
    }
    {
        RigLibrary reopened (temp.folder);
        CHECK (reopened.findFactory ("Init")->rig.favourite);
        reopened.setFavourite (*reopened.findFactory ("Init"), false);
    }

    RigLibrary reopenedAgain (temp.folder);
    CHECK_FALSE (reopenedAgain.findFactory ("Init")->rig.favourite);
}

TEST_CASE ("Rigs are sorted by bank and number, and < > wrap round", "[rigs]")
{
    TempRigFolder temp;
    RigLibrary library (temp.folder);

    Rig late;
    late.name = "Late";
    late.bank = Rig::numBanks - 1;   // the last bank, after every factory rig
    late.number = Rig::maxNumber;   // after every factory rig (G59 since 0.21.0)
    library.saveAsNew (late);

    const auto& entries = library.getEntries();
    for (size_t i = 1; i < entries.size(); ++i)
        CHECK (entries[i - 1].rig.bank * 100 + entries[i - 1].rig.number <= entries[i].rig.bank * 100 + entries[i].rig.number);

    CHECK (entries.back().rig.name == "Late");
    CHECK (library.step (&entries.back(), 1) == &entries.front());
    CHECK (library.step (&entries.front(), -1) == &entries.back());
    CHECK (library.step (nullptr, 1) == &entries.front());

    CHECK (library.nextFreeNumber (Rig::drums) == 7);    // the drum rigs fill A01 to A06
    CHECK (library.nextFreeNumber (Rig::vocals) == 8);   // Init and the vocal rigs fill E01 to E07
}

TEST_CASE ("Saving and loading a rig restores the sound but not the global knobs", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::eqOn, 1.0f);
    r.set (ParamIDs::eqOutput, 7.5f);
    r.set (ParamIDs::presence, 6.0f);
    r.set (ParamIDs::rigLevel, -3.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("My Sound"));

    // Change everything, including the global input and output knobs.
    r.set (ParamIDs::eqOn, 0.0f);
    r.set (ParamIDs::eqOutput, 1.0f);
    r.set (ParamIDs::presence, -2.0f);
    r.set (ParamIDs::rigLevel, 0.0f);
    r.set (ParamIDs::inputGain, 5.0f);
    r.set (ParamIDs::outputGain, -4.0f);

    r.processor.loadRig (r.entryNamed ("My Sound"));

    CHECK (r.get (ParamIDs::eqOn) > 0.5f);
    CHECK_THAT (r.get (ParamIDs::eqOutput), WithinAbs (7.5, 0.01));
    CHECK_THAT (r.get (ParamIDs::presence), WithinAbs (6.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::rigLevel), WithinAbs (-3.0, 0.01));
    // Global knobs are left alone.
    CHECK_THAT (r.get (ParamIDs::inputGain), WithinAbs (5.0, 0.01));
    CHECK_THAT (r.get (ParamIDs::outputGain), WithinAbs (-4.0, 0.01));
}

TEST_CASE ("A rig that doesn't mention a parameter sets it to its default", "[rigs]")
{
    // E.g. a rig saved by an older version, before that parameter existed.
    RigRig r;
    r.set (ParamIDs::eqAutoGain, 1.0f);
    r.set (ParamIDs::presence, 6.0f);

    Rig old;
    old.name = "Old Rig";
    old.parameters = { { ParamIDs::eqOn, 1.0f } };
    r.processor.getRigLibrary().saveAsNew (old);
    r.processor.loadRig (r.entryNamed ("Old Rig"));

    CHECK (r.get (ParamIDs::eqOn) > 0.5f);
    CHECK (r.get (ParamIDs::eqAutoGain) < 0.5f);
    CHECK_THAT (r.get (ParamIDs::presence), WithinAbs (0.0, 0.01));
}

TEST_CASE ("The modified marker tracks changes since loading or saving", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    CHECK_FALSE (r.processor.isRigModified());

    SECTION ("Turning a knob marks it modified")
    {
        r.set (ParamIDs::eqOutput, 9.0f);
        CHECK (r.processor.isRigModified());
    }

    SECTION ("The global knobs don't count")
    {
        r.set (ParamIDs::inputGain, 3.0f);
        r.set (ParamIDs::outputGain, -3.0f);
        CHECK_FALSE (r.processor.isRigModified());
    }

    SECTION ("Saving clears it")
    {
        r.set (ParamIDs::eqOutput, 9.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Saved"));
        CHECK_FALSE (r.processor.isRigModified());
        r.set (ParamIDs::eqOutput, 2.0f);
        CHECK (r.processor.isRigModified());
        REQUIRE (r.processor.saveCurrentRig());   // a user rig now, so plain save works
        CHECK_FALSE (r.processor.isRigModified());
    }
}

TEST_CASE ("Factory rigs can't be saved over", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    r.set (ParamIDs::presence, 9.0f);
    CHECK_FALSE (r.processor.saveCurrentRig());   // the UI then asks for a new name
    CHECK (r.entryNamed ("Init").rig.parameters.count (ParamIDs::presence) == 0);
}

TEST_CASE ("< and > step through the rigs in order", "[rigs]")
{
    RigRig r;
    r.processor.loadRig (r.entryNamed ("Init"));
    REQUIRE (r.processor.saveCurrentRigAs ("Second"));   // filed after bank E's factory rigs (Init's bank), at E08
    REQUIRE (r.processor.saveCurrentRigAs ("Third"));    // E09
    CHECK (r.entryNamed ("Second").rig.getSlotName() == "E08");
    r.processor.loadRig (r.entryNamed ("Vocal Mud Control"));   // E07
    r.processor.stepRig (1);
    CHECK (r.processor.getCurrentRig().name == "Second");
    r.processor.stepRig (1);
    r.processor.stepRig (1);   // on to bank E's genre rigs (E11)
    CHECK (r.processor.getCurrentRig().name == "Modern Metal Vocal");
    r.processor.loadRig (r.entryNamed ("Industrial Vocal"));   // E19
    r.processor.stepRig (1);   // on to bank E's signature sounds (E31)
    CHECK (r.processor.getCurrentRig().name == "Ethereal Djent Vocal");
    r.processor.loadRig (r.entryNamed ("Fix: Harsh Vocal"));   // E73, bank E's last
    r.processor.stepRig (1);   // on into bank F, the mix
    CHECK (r.processor.getCurrentRig().name == "Mix Bus Polish");
    r.processor.loadRig (r.entryNamed ("Kick Punch"));   // A01, the first
    r.processor.stepRig (-1);   // wraps round to the last rig
    CHECK (r.processor.getCurrentRig().name == "Master Broadcast A/85");   // G59
}

TEST_CASE ("Deleting the loaded rig keeps the sound, now unsaved", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::eqOutput, 8.0f);
    REQUIRE (r.processor.saveCurrentRigAs ("Temporary"));
    REQUIRE (r.processor.deleteRig (r.entryNamed ("Temporary")));

    CHECK_FALSE (r.processor.getCurrentRig().isUserRig());
    CHECK (r.processor.isRigModified());
    CHECK_THAT (r.get (ParamIDs::eqOutput), WithinAbs (8.0, 0.01));
}

TEST_CASE ("The DAW project remembers which rig was loaded", "[rigs][state]")
{
    juce::MemoryBlock saved;
    juce::File rigFile;
    {
        RigRig r;
        r.set (ParamIDs::eqOutput, 8.0f);
        REQUIRE (r.processor.saveCurrentRigAs ("Project Rig"));
        r.set (ParamIDs::eqOutput, 3.0f);   // tweaked after saving
        rigFile = r.processor.getCurrentRig().file;
        r.processor.getStateInformation (saved);
    }

    ParagonX1AudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (restored.getCurrentRig().name == "Project Rig");
    CHECK (restored.getCurrentRig().file == rigFile);
    CHECK (restored.isRigModified());   // the tweak is remembered as unsaved
    CHECK_THAT (restored.getParameters().getRawParameterValue (ParamIDs::eqOutput)->load(), WithinAbs (3.0, 0.01));
}

TEST_CASE ("Each rig's RIG LEVEL adds to the master output", "[rigs]")
{
    RigRig r;
    r.set (ParamIDs::rigLevel, 6.0f);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 20; ++block)
    {
        buffer.clear();
        for (int i = 0; i < 256; ++i)
            buffer.setSample (0, i, 0.1f * std::sin (0.1f * (float) (block * 256 + i)));
        r.processor.processBlock (buffer, midi);
    }
    CHECK_THAT (buffer.getMagnitude (0, 0, 256), WithinAbs (0.1 * 1.9953, 0.005));
}

TEST_CASE ("Rigs live in their own Paragon PR7-X folder, apart from its siblings'", "[rigs]")
{
    const auto folder = RigLibrary::defaultFolder();
    CHECK (folder.getFileName() == "Rigs");
    CHECK (folder.getParentDirectory().getFileName() == "Paragon PR7-X");
    CHECK (folder.getParentDirectory().getParentDirectory().getFileName() == "Paragon");

    // PR7-B's (bass) and PR7-G's (guitar) rigs folders sit next to it, and are different folders.
    for (const auto* sibling : { "Paragon PR7-B", "Paragon PR7-G" })
    {
        const auto siblingFolder = folder.getParentDirectory().getSiblingFile (sibling).getChildFile ("Rigs");
        CHECK (folder != siblingFolder);
        CHECK_FALSE (folder.isAChildOf (siblingFolder.getParentDirectory()));
    }
}

TEST_CASE ("PR7-E became PR7-X (0.20.0): its folder of rigs and presets is copied over the first time, once", "[rigs]")
{
    TempRigFolder temp;
    const auto oldBase = temp.folder.getChildFile ("Paragon PR7-E"), newBase = temp.folder.getChildFile ("Paragon PR7-X");
    REQUIRE (oldBase.getChildFile ("Rigs").createDirectory());
    REQUIRE (oldBase.getChildFile ("Rigs").getChildFile ("My Rig.pr7rig").replaceWithText ("<Rig/>"));
    REQUIRE (oldBase.getChildFile ("Comp Presets").createDirectory());
    REQUIRE (oldBase.getChildFile ("Comp Presets").getChildFile ("Mine.pr7comp").replaceWithText ("<CompPreset/>"));

    CHECK (RigLibrary::carryOver (oldBase, newBase));
    CHECK (newBase.getChildFile ("Rigs").getChildFile ("My Rig.pr7rig").existsAsFile());
    CHECK (newBase.getChildFile ("Comp Presets").getChildFile ("Mine.pr7comp").existsAsFile());
    CHECK (oldBase.getChildFile ("Rigs").getChildFile ("My Rig.pr7rig").existsAsFile());   // PR7-E's left as it was

    // Only once: after that, PR7-X's own folder is never overwritten.
    REQUIRE (newBase.getChildFile ("Rigs").getChildFile ("My Rig.pr7rig").replaceWithText ("<Rig name=\"changed\"/>"));
    CHECK_FALSE (RigLibrary::carryOver (oldBase, newBase));
    CHECK (newBase.getChildFile ("Rigs").getChildFile ("My Rig.pr7rig").loadFileAsString().contains ("changed"));
    // Nothing to copy from: nothing happens.
    CHECK_FALSE (RigLibrary::carryOver (temp.folder.getChildFile ("Paragon PR7-Q"), temp.folder.getChildFile ("Paragon PR7-Z")));
}

TEST_CASE ("Only .pr7rig files are rigs: the old Paragon X1 .x1rig files aren't read", "[rigs]")
{
    TempRigFolder temp;
    REQUIRE (temp.folder.createDirectory());
    Rig old;
    old.name = "From X1";
    REQUIRE (old.toXml()->writeTo (temp.folder.getChildFile ("From X1.x1rig")));

    RigLibrary library (temp.folder);
    CHECK (std::none_of (library.getEntries().begin(), library.getEntries().end(),
                         [] (const auto& entry) { return entry.rig.name == "From X1"; }));
}

TEST_CASE ("A rig file from before 0.3.0 with the pedal on an effect that's gone loads with it unassigned", "[rigs]")
{
    // WAH (2): one of PR7-B's effects, gone in 0.3.0.
    auto xml = juce::XmlDocument::parse (R"(<ParagonRig version="1" parameterVersion="15" name="Old"><Param id="expressionTarget" value="2"/></ParagonRig>)");
    REQUIRE (xml != nullptr);
    const auto rig = Rig::fromXml (*xml);
    REQUIRE (rig.has_value());
    CHECK (rig->parameters.at ("expressionTarget") == 0.0f);

    // VOLUME (1) stays VOLUME, and a rig saved now is read back as it is.
    auto volume = juce::XmlDocument::parse (R"(<ParagonRig version="1" parameterVersion="15" name="Old"><Param id="expressionTarget" value="1"/></ParagonRig>)");
    CHECK (Rig::fromXml (*volume)->parameters.at ("expressionTarget") == 1.0f);
    Rig current;
    current.parameters["expressionTarget"] = 1.0f;
    CHECK (Rig::fromXml (*current.toXml())->parameters.at ("expressionTarget") == 1.0f);
}

TEST_CASE ("Master rigs are the whole rack: all six units on; MARS and BINARY on no other rig", "[rigs][mastering]")
{
    // 0.22.1, the owner: MARS and BINARY belong in a full mastering chain.
    const char* units[] = { ParamIDs::eqOn, ParamIDs::domOn, ParamIDs::compOn, ParamIDs::marsOn, ParamIDs::stereoOn, ParamIDs::limitOn };
    const auto on = [] (const Rig& rig, const char* id) { return rig.parameters.count (id) == 1 && rig.parameters.at (id) > 0.5f; };
    int masters = 0;
    for (const auto& rig : makeFactoryRigs())
    {
        INFO (rig.name);
        if (rig.bank == Rig::master)
        {
            ++masters;
            for (const auto* id : units)
            {
                INFO (id);
                CHECK (on (rig, id));
            }
            CHECK (on (rig, ParamIDs::stereoMonoOn));   // BINARY folds the lows to mono...
            CHECK (rig.parameters.at (ParamIDs::stereoMonoFreq) >= 100.0f);
            CHECK (rig.parameters.at (ParamIDs::stereoMonoFreq) <= 150.0f);
            CHECK (rig.parameters.count (ParamIDs::stereoWidth) == 0);   // ...and leaves the image's width as it is
            CHECK (rig.notes.contains ("MARS: its \""));
            CHECK (rig.notes.contains ("BINARY: mono below"));
        }
        else
        {
            CHECK_FALSE (on (rig, ParamIDs::marsOn));
            CHECK_FALSE (on (rig, ParamIDs::stereoOn));
        }
    }
    CHECK (masters == 32);
}
