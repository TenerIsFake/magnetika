#pragma once
// Phase 2 editor: every parameter as a plain control, a material picker, and an
// on-screen keyboard with the real instrument's range marked. The bowl-view
// editor is phase 3.

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

namespace magnetika::plugin {

// A strip above the keyboard marking the notes the hardware design covers
// (rev B: C4-C6, extendable to C3). Outside it the model is extrapolating.
class RangeStrip : public juce::Component {
public:
    explicit RangeStrip(juce::MidiKeyboardComponent& kb) : kb_(kb) {}
    void paint(juce::Graphics& g) override;

private:
    juce::MidiKeyboardComponent& kb_;
};

class MagnetikaEditor : public juce::AudioProcessorEditor {
public:
    explicit MagnetikaEditor(MagnetikaProcessor&);
    ~MagnetikaEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void applyMaterial(int index);

    MagnetikaProcessor& proc_;
    juce::Label title_;
    juce::Label materialLabel_;
    juce::ComboBox material_;

    // generated controls, one row per parameter
    struct Row {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> combo;
        juce::String section;
    };
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<juce::Label>> headers_;  // one per section, in order
    juce::Component content_;
    juce::Viewport viewport_;

    juce::MidiKeyboardComponent keyboard_;
    RangeStrip range_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaEditor)
};

} // namespace magnetika::plugin
