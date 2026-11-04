#pragma once
// The editor: a play page (the instrument in oblique 3-D, performance controls)
// and a modify page (every parameter). Both are laid out in a 1200 x 800 design
// canvas that the editor scales to the window, which keeps a 3:2 aspect ratio.

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "ui/PlayPage.h"
#include "ui/Theme.h"

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

// Every parameter as a plain control, the material picker, and a keyboard.
// (Phase 3 step 4 replaces this with the faceplate.)
class ModifyPage : public juce::Component {
public:
    ModifyPage(MagnetikaProcessor&, std::function<void()> onDone);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void applyMaterial(int index);

    MagnetikaProcessor& proc_;
    juce::Label title_;
    juce::Label materialLabel_;
    juce::ComboBox material_;
    juce::TextButton doneBtn_{"DONE"};

    struct Row {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> combo;
        juce::String section;
    };
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<juce::Label>> headers_;
    juce::Component content_;
    juce::Viewport viewport_;

    juce::MidiKeyboardComponent keyboard_;
    RangeStrip range_;
};

class MagnetikaEditor : public juce::AudioProcessorEditor {
public:
    static constexpr int kDesignW = 1200, kDesignH = 800;
    explicit MagnetikaEditor(MagnetikaProcessor&);
    ~MagnetikaEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void showModify(bool modify);
    void poll() { play_.poll(); }  // for offscreen snapshots

private:
    theme::LookAndFeel laf_;
    juce::Component canvas_;
    PlayPage play_;
    ModifyPage modify_;
    juce::TooltipWindow tooltips_{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaEditor)
};

} // namespace magnetika::plugin
