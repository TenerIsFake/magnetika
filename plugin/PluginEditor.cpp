#include "PluginEditor.h"

namespace magnetika::plugin {

MagnetikaEditor::MagnetikaEditor(MagnetikaProcessor& p)
    : AudioProcessorEditor(&p), play_(p, [this] { showModify(true); }), modify_(p, [this] { showModify(false); }) {
    setLookAndFeel(&laf_);
    canvas_.addAndMakeVisible(play_);
    canvas_.addChildComponent(modify_);
    play_.setBounds(0, 0, kDesignW, kDesignH);
    modify_.setBounds(0, 0, kDesignW, kDesignH);
    canvas_.setSize(kDesignW, kDesignH);
    addAndMakeVisible(canvas_);
    // resizable, 3:2; the default fits a 200 %-scaled Surface Laptop screen (1128 x 752)
    setResizable(true, true);
    setResizeLimits(600, 400, 2400, 1600);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(static_cast<double>(kDesignW) / kDesignH);
    setSize(1050, 700);
}

MagnetikaEditor::~MagnetikaEditor() { setLookAndFeel(nullptr); }

void MagnetikaEditor::showModify(bool modify) {
    modify_.setVisible(modify);
    play_.setVisible(!modify);
}

void MagnetikaEditor::paint(juce::Graphics& g) { g.fillAll(theme::walnutDark); }

void MagnetikaEditor::resized() {
    const float s = std::min(getWidth() / static_cast<float>(kDesignW), getHeight() / static_cast<float>(kDesignH));
    canvas_.setTransform(juce::AffineTransform::scale(s).translated((getWidth() - kDesignW * s) * 0.5f,
                                                                    (getHeight() - kDesignH * s) * 0.5f));
}

} // namespace magnetika::plugin
