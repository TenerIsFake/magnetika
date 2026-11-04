#pragma once
// The play page: the instrument on an oblique, with the performance controls a
// player reaches for (pedals, spindle, presets, rotation and vibrato strips) and a
// note strip. Laid out in a 1200 x 800 design space; the editor scales it.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <set>

#include "../PluginProcessor.h"
#include "BarsView.h"
#include "GlassView.h"

namespace magnetika::plugin {

// 37 latching note chips (C3..C6) with lamps for notes sounding outside the drawn range.
class NoteStrip : public juce::Component {
public:
    explicit NoteStrip(MagnetikaProcessor& p) : proc_(p) {}
    std::function<float(int)> levelOf;
    std::function<bool()> below, above;
    juce::String hint;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void clearLatches();

private:
    juce::Rectangle<float> chipBounds(int midi) const;
    juce::Rectangle<float> chipArea() const;
    MagnetikaProcessor& proc_;
    std::set<int> latched_;
};

// A touch strip; the bipolar one has a centre detent.
class StripSlider : public juce::Slider {
public:
    explicit StripSlider(bool detent) : juce::Slider(LinearHorizontal, NoTextBox), detent_(detent) {}
    double snapValue(double v, DragMode) override { return detent_ && std::abs(v) < 0.06 ? 0.0 : v; }

private:
    bool detent_;
};

class PlayPage : public juce::Component, private juce::Timer {
public:
    PlayPage(MagnetikaProcessor& p, std::function<void()> onModify);
    ~PlayPage() override;
    void poll() { glass_.poll(); bars_.poll(); timerCallback(); }  // for offscreen snapshots
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepPreset(int delta);
    void setInstrument(int i);
    int instrument() const;
    void setParam(const char* pid, double v);

    MagnetikaProcessor& proc_;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;

    juce::TextButton glassBtn_{"GLASS"}, barsBtn_{"BARS"};
    juce::TextButton sustainBtn_{"SUSTAIN"}, brakeBtn_{"BRAKE"}, spindleBtn_{"SPINDLE"}, modifyBtn_{"MODIFY"};
    juce::Slider speed_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::TextButton prevBtn_{"<"}, nextBtn_{">"}, saveBtn_{"SAVE"};
    StripSlider rotation_{true}, vibrato_{false};
    std::unique_ptr<BA> sustainAtt_, brakeAtt_, spindleAtt_;
    std::unique_ptr<SA> speedAtt_, rotAtt_, vibAtt_;

    juce::Slider damper_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    std::unique_ptr<SA> damperAtt_;

    GlassView glass_;
    BarsView bars_;
    NoteStrip strip_;
    juce::TextButton allOffBtn_{"ALL OFF"};

    juce::Rectangle<float> nameplate_, speedWell_, presetWell_, rotWell_, vibWell_, speedText_, presetText_, rotText_, vibText_,
        damperText_;
};

} // namespace magnetika::plugin
