#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

// A stomp's own presets, apart from PR7-X's rigs (DOMINION's from 0.10.3,
// COMP's from 0.14.0): a preset holds only that stomp's settings, never its
// on switch or anything else in the rig. The factory ones are built in,
// filed by category; yours are small files in a folder beside the rigs.
struct StompPreset
{
    juce::String name, category;
    // The stomp's parameters, in their real units, by ID. Applying a preset
    // sets every one of the stomp's settings: those it doesn't mention go to
    // their defaults (so a preset sounds the same whatever was there before).
    std::map<juce::String, float> parameters;
    bool isFactory = false;
    juce::File file;   // for your own
};

// `Traits` names the stomp's presets:
//   static constexpr const char* fileExtension, xmlTag;
//   static bool holds (const juce::String& parameterId);   // its settings (not its on switch)
//   static std::vector<StompPreset> makeFactory();         // the first is "Default"
template <typename Traits>
class StompPresets
{
public:
    explicit StompPresets (juce::File presetsFolder) : folder (std::move (presetsFolder)) {}

    static constexpr const char* fileExtension = Traits::fileExtension;

    static bool holds (const juce::String& parameterId) { return Traits::holds (parameterId); }

    static const std::vector<StompPreset>& factory()
    {
        static const auto presets = Traits::makeFactory();
        return presets;
    }

    // Read from the folder, by name.
    std::vector<StompPreset> user() const
    {
        std::vector<StompPreset> presets;
        for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        {
            const auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr || ! xml->hasTagName (Traits::xmlTag))
                continue;
            StompPreset preset;
            preset.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
            preset.file = file;
            for (const auto* p : xml->getChildWithTagNameIterator ("Param"))
                if (const auto id = p->getStringAttribute ("id"); holds (id))
                    preset.parameters[id] = (float) p->getDoubleAttribute ("value");
            presets.push_back (std::move (preset));
        }
        std::sort (presets.begin(), presets.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
        return presets;
    }

    const StompPreset* findFactory (const juce::String& name) const
    {
        for (const auto& preset : factory())
            if (preset.name == name)
                return &preset;
        return nullptr;
    }

    // The stomp's settings as they are now.
    static StompPreset capture (juce::AudioProcessorValueTreeState& state, const juce::String& name)
    {
        StompPreset preset;
        preset.name = name;
        for (auto* parameter : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter); ranged != nullptr && holds (ranged->paramID))
                preset.parameters[ranged->paramID] = ranged->convertFrom0to1 (ranged->getValue());
        return preset;
    }

    // Sets the stomp's settings to the preset's (each change a gesture, so it's one undo step).
    static void apply (const StompPreset& preset, juce::AudioProcessorValueTreeState& state)
    {
        for (auto* parameter : state.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            if (ranged == nullptr || ! holds (ranged->paramID))
                continue;
            const auto normalised = wanted (preset, *ranged);
            if (juce::approximatelyEqual (ranged->getValue(), normalised))
                continue;
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (normalised);
            ranged->endChangeGesture();
        }
    }

    // Whether the stomp's settings are still the preset's.
    static bool matches (const StompPreset& preset, juce::AudioProcessorValueTreeState& state)
    {
        for (auto* parameter : state.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            if (ranged != nullptr && holds (ranged->paramID) && std::abs (ranged->getValue() - wanted (preset, *ranged)) > 1.0e-4f)
                return false;
        }
        return true;
    }

    // As yours, by its name (replacing one of the same name).
    bool save (const StompPreset& preset) const
    {
        if (preset.name.trim().isEmpty() || ! folder.createDirectory())
            return false;
        juce::XmlElement xml (Traits::xmlTag);
        xml.setAttribute ("version", 1);
        xml.setAttribute ("name", preset.name.trim());
        for (const auto& [id, value] : preset.parameters)
        {
            auto* p = xml.createNewChildElement ("Param");
            p->setAttribute ("id", id);
            p->setAttribute ("value", value);
        }
        return xml.writeTo (folder.getChildFile (juce::File::createLegalFileName (preset.name.trim()) + fileExtension));
    }

    bool remove (const StompPreset& preset) const
    {
        return ! preset.isFactory && preset.file.existsAsFile() && preset.file.deleteFile();
    }

    const juce::File& getFolder() const noexcept { return folder; }
    void setFolder (juce::File newFolder) { folder = std::move (newFolder); }

private:
    static float wanted (const StompPreset& preset, const juce::RangedAudioParameter& parameter)
    {
        const auto found = preset.parameters.find (parameter.paramID);
        return found != preset.parameters.end() ? parameter.convertTo0to1 (found->second) : parameter.getDefaultValue();
    }

    juce::File folder;
};
