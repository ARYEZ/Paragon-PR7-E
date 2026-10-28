#include "Rig.h"

#include "Parameters.h"

namespace
{
    // Bump when the file layout changes in a way older code can't read.
    constexpr int fileVersion = 1;

    // Which plugin version's parameters the rig was saved with, for the
    // occasional parameter whose default changed (older plugins ignore it).
    // 15: the reverb's TYPE (from PR7-G, via PR7-B; gone in PR7-E 0.3.0).
    // 30: PR7-E 0.3.0, when the expression pedal's effect targets went.
    // 31: PR7-E 0.5.0, the dynamic EQ (nothing to migrate: new bands' defaults are static).
    // 32: PR7-E 0.8.0, the LIMITER (off by default: nothing to migrate).
    // 33: PR7-E 0.9.0, COMP (off by default: nothing to migrate).
    // 34: PR7-E 0.10.0, DOMINION (off by default: nothing to migrate).
    // 35: PR7-E 0.13.0, COMP's bands (none by default) and compWide (on by
    //     default, so an older rig, which doesn't mention it, sounds as it did).
    // 36: PR7-E 0.16.0, the LIMITER after Pro-L 2: THRESHOLD and MAKE UP
    //     become GAIN and OUTPUT (see convertOldLimiter).
    constexpr int parameterVersion = 36;

    // Which bank layout the rig's letter means: 2 since 0.21.0 (A drums ...
    // G master); 1 (no attribute) the five banks before.
    constexpr int bankLayout = 2;

    // A rig saved with the five banks: its bank in the seven. Bank C held
    // bass, guitars and keys; its tags say which (guitars if they don't).
    int bankFromOldLayout (int oldBank, const juce::StringArray& tags)
    {
        switch (oldBank)
        {
            case 0: return Rig::vocals;
            case 1: return Rig::drums;
            case 3: return Rig::mix;
            case 4: return Rig::master;
            default: break;
        }
        const auto tagged = [&tags] (std::initializer_list<const char*> words)
        {
            for (const auto* word : words)
                if (tags.contains (word, true))
                    return true;
            return false;
        };
        if (tagged ({ "Bass", "808" }))
            return Rig::bass;
        if (tagged ({ "Keys", "Synth", "Piano", "Pad", "Organ", "Strings" }))
            return Rig::synths;
        return Rig::guitars;
    }

    const juce::Identifier rigTag { "ParagonRig" };
    const juce::Identifier parameterTag { "Param" };
}

juce::String Rig::bankName (int bankIndex)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numBanks - 1, bankIndex)));
}

juce::String Rig::bankTitle (int bankIndex)
{
    static const juce::StringArray titles { "Drums", "Bass", "Guitars", "Synths", "Vocals", "Mix", "Master" };
    return titles[juce::jlimit (0, numBanks - 1, bankIndex)];
}

juce::String Rig::getSlotName() const
{
    return bankName (bank) + juce::String (number).paddedLeft ('0', 2);
}

std::unique_ptr<juce::XmlElement> Rig::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (rigTag);
    xml->setAttribute ("version", fileVersion);
    xml->setAttribute ("parameterVersion", parameterVersion);
    xml->setAttribute ("name", name);
    xml->setAttribute ("bank", bankName (bank));
    xml->setAttribute ("bankLayout", bankLayout);
    xml->setAttribute ("number", number);
    xml->setAttribute ("favourite", favourite);
    xml->setAttribute ("tags", tags.joinIntoString (","));
    if (notes.isNotEmpty())
        xml->setAttribute ("notes", notes);

    for (const auto& [id, value] : parameters)
    {
        auto* parameter = xml->createNewChildElement (parameterTag);
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }
    return xml;
}

std::optional<Rig> Rig::fromXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (rigTag) || xml.getIntAttribute ("version", 0) > fileVersion)
        return std::nullopt;

    Rig rig;
    rig.name = xml.getStringAttribute ("name", "Untitled").trim();
    if (rig.name.isEmpty())
        rig.name = "Untitled";

    const auto bankLetter = xml.getStringAttribute ("bank", "A").toUpperCase();
    rig.bank = bankLetter.isEmpty() ? 0 : juce::jlimit (0, numBanks - 1, (int) (bankLetter[0] - 'A'));
    rig.number = juce::jlimit (1, maxNumber, xml.getIntAttribute ("number", 1));
    rig.favourite = xml.getBoolAttribute ("favourite");
    rig.tags = juce::StringArray::fromTokens (xml.getStringAttribute ("tags"), ",", {});
    rig.tags.trim();
    rig.tags.removeEmptyStrings();
    rig.notes = xml.getStringAttribute ("notes");
    if (xml.getIntAttribute ("bankLayout", 1) < 2)   // saved with the five banks (before 0.21.0)
        rig.bank = bankFromOldLayout (bankLetter.isEmpty() ? 0 : juce::jlimit (0, 4, (int) (bankLetter[0] - 'A')), rig.tags);

    for (const auto* parameter : xml.getChildWithTagNameIterator (parameterTag))
        if (const auto id = parameter->getStringAttribute ("id"); id.isNotEmpty())
            rig.parameters[id] = (float) parameter->getDoubleAttribute ("value");

    const auto savedWith = xml.getIntAttribute ("parameterVersion", 0);
    // Saved before 0.3.0 with the expression pedal on one of PR7-B's effects
    // (2 to 8, all gone): unassigned instead (see ExpressionTargets).
    if (const auto target = rig.parameters.find ("expressionTarget");
        savedWith < 30 && target != rig.parameters.end() && target->second > 1.5f)
        target->second = 0.0f;

    // Saved before 0.16.0 with the LIMITER set: its THRESHOLD and MAKE UP as GAIN and OUTPUT.
    if (const auto& p = rig.parameters; savedWith < 36 && (p.count ("limitThreshold") > 0 || p.count ("limitMakeUp") > 0))
    {
        const auto value = [&p] (const char* id, float otherwise) { const auto found = p.find (id); return found != p.end() ? found->second : otherwise; };
        const auto converted = convertOldLimiter (value ("limitThreshold", -1.0f), value ("limitMakeUp", 0.0f), value ("limitTrim", 0.0f));
        rig.parameters["limitGain"] = converted.gain;
        rig.parameters["limitCeiling"] = converted.ceiling;
        rig.parameters["limitTrim"] = converted.trim;
    }

    return rig;
}
