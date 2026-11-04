#pragma once
// The editor: a play page (the instrument in oblique 3-D, performance controls)
// and a modify page (cut-away, faceplate, every parameter). Both are laid out in
// a 1200 x 800 design canvas that the editor scales to the window (3:2).

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/ModifyPage.h"
#include "ui/PlayPage.h"
#include "ui/Theme.h"

namespace magnetika::plugin {

class MagnetikaEditor : public juce::AudioProcessorEditor {
public:
    static constexpr int kDesignW = 1200, kDesignH = 800;
    explicit MagnetikaEditor(MagnetikaProcessor&);
    ~MagnetikaEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void showModify(bool modify);
    void poll() { play_.poll(); modify_.poll(); }  // for offscreen snapshots

private:
    theme::LookAndFeel laf_;
    juce::Component canvas_;
    PlayPage play_;
    ModifyPage modify_;
    juce::TooltipWindow tooltips_{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaEditor)
};

} // namespace magnetika::plugin
