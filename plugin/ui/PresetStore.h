#pragma once
// User presets: the whole plugin state as XML files with a .magnetika extension,
// in Documents/Magnetika/Presets (the folder can be overridden, for tests).

#include <juce_audio_processors/juce_audio_processors.h>

namespace magnetika::plugin {

class PresetStore {
public:
    explicit PresetStore(juce::File dir = defaultDirectory()) : dir_(std::move(dir)) {}
    static juce::File defaultDirectory() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Magnetika").getChildFile("Presets");
    }

    // Names of the saved presets, sorted, without the extension.
    juce::StringArray names() const {
        juce::StringArray out;
        for (const auto& f : dir_.findChildFiles(juce::File::findFiles, false, "*.magnetika")) out.add(f.getFileNameWithoutExtension());
        out.sortNatural();
        return out;
    }

    // Saves the state under `name` (characters a file name cannot hold are replaced).
    bool save(const juce::String& name, juce::AudioProcessorValueTreeState& st) const {
        const auto clean = juce::File::createLegalFileName(name.trim());
        if (clean.isEmpty() || !dir_.createDirectory()) return false;
        auto xml = st.copyState().createXml();
        return xml != nullptr && xml->writeTo(dir_.getChildFile(clean + ".magnetika"));
    }

    // Loads `name` into the state; false if missing or not a Magnetika state.
    bool load(const juce::String& name, juce::AudioProcessorValueTreeState& st) const {
        const auto xml = juce::XmlDocument::parse(dir_.getChildFile(name + ".magnetika"));
        if (xml == nullptr || !xml->hasTagName(st.state.getType())) return false;
        auto state = juce::ValueTree::fromXml(*xml);
        upgradeState(state);  // older presets keep their sound
        st.replaceState(state);
        return true;
    }

    const juce::File& directory() const { return dir_; }

private:
    juce::File dir_;
};

} // namespace magnetika::plugin
