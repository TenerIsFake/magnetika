#pragma once
// The modify page: the instrument cut away (bowl seen along the spindle with its
// band, coils and sensors; or a bar in side view with magnet, coil and sensor),
// an aluminium faceplate of lit switches and knobs, an ADVANCED drawer with every
// parameter, and pedals, keyboard and meters along the bottom.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <vector>

#include "../PluginProcessor.h"

namespace magnetika::plugin {

// Marks the notes the drawn instrument covers above the keyboard: glass C3-C7 (rev B is
// C4-C6; C3 and the top octave are extrapolated), bars C3-C6.
class RangeStrip : public juce::Component {
public:
    explicit RangeStrip(juce::MidiKeyboardComponent& kb) : kb_(kb) {}
    void paint(juce::Graphics& g) override;
    int high = 84;  // the top note marked

private:
    juce::MidiKeyboardComponent& kb_;
};

// The live cut-away of the selected bowl or bar.
class Cutaway : public juce::Component, private juce::Timer {
public:
    explicit Cutaway(MagnetikaProcessor& p) : proc_(p) { startTimerHz(30); }
    ~Cutaway() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void poll() { timerCallback(); }
    int shownInstrument = 0;  // set by the page (buttons or MIDI program change)

private:
    void timerCallback() override;
    void paintGlass(juce::Graphics&);
    void paintBars(juce::Graphics&);
    MagnetikaProcessor& proc_;
    MagnetikaProcessor::Display d_;
    int selected_ = -1;
    float level_ = 0, pattern_ = 0, phase_ = 0;
};

// Lit switches and a 4 x 3 grid of knobs, for one instrument.
class Faceplate : public juce::Component {
public:
    struct KnobSpec { const char* paramId; const char* label; };
    struct Section { juce::String title; std::vector<KnobSpec> knobs; };
    struct SwitchSpec { const char* paramId; const char* label; };
    Faceplate(MagnetikaProcessor& p, std::vector<SwitchSpec> switches, std::vector<Section> sections);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onAdvanced;
    std::function<void()> onLevels;  // set (glass) to show the LEVELS button

private:
    struct Knob {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label;
        juce::RangedAudioParameter* param = nullptr;
    };
    MagnetikaProcessor& proc_;
    std::vector<Section> sections_;
    std::vector<std::unique_ptr<juce::TextButton>> switches_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> switchAtts_;
    std::vector<Knob> knobs_;
    juce::TextButton advancedBtn_{"ADVANCED"};
    juce::TextButton levelsBtn_{"LEVELS"};
};

// Every parameter as a plain control (the phase-2 editor, kept as a drawer).
class AdvancedPanel : public juce::Component {
public:
    explicit AdvancedPanel(MagnetikaProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
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
    juce::TextButton closeBtn_{"CLOSE"};
};

// Glass: each bowl's level, C3-C7. Drag a bar to trim that bowl (sweep across bowls to
// draw a curve, double-click to clear one); the evening knob sets how much of the
// built-in correction applies. Bars show the trim; the line, how loud each bowl sounds re C4.
class BowlLevelsPanel : public juce::Component {
public:
    static constexpr int kLow = 48, kHigh = 96;  // C3..C7
    explicit BowlLevelsPanel(MagnetikaProcessor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::function<void()> onClose;
    // the graph's geometry, for the editor and its tests
    juce::Rectangle<float> graph() const { return graph_; }
    float xOf(int midi) const;
    float yOf(float db) const;

private:
    int bowlAt(float x) const;
    float dbAt(float y) const;
    void setTrim(int midi, float db);
    MagnetikaProcessor& proc_;
    juce::Slider evenKnob_{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> evenAtt_;
    juce::TextButton resetBtn_{"RESET TRIMS"}, closeBtn_{"CLOSE"};
    juce::Rectangle<float> graph_, knobCell_;
    int hover_ = -1, lastDrag_ = -1;
};

class ModifyPage : public juce::Component, private juce::Timer {
public:
    ModifyPage(MagnetikaProcessor&, std::function<void()> onDone);
    ~ModifyPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void poll() { cutaway_.poll(); timerCallback(); }
    void showLevels(bool on) {
        if (on) advanced_.setVisible(false);
        levels_.setVisible(on);
    }
    BowlLevelsPanel& levelsPanel() { return levels_; }

private:
    void timerCallback() override;
    int instrument() const;
    void setInstrument(int i);

    MagnetikaProcessor& proc_;
    juce::TextButton glassBtn_{"GLASS"}, barsBtn_{"BARS"}, doneBtn_{"DONE"};
    // the instrument as built: which one (Instrument No.) and how much its parts differ
    juce::TextButton serialDown_{juce::String::fromUTF8("\xe2\x80\xb9")}, serialUp_{juce::String::fromUTF8("\xe2\x80\xba")};
    juce::Slider variationKnob_{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> variationAtt_;
    void stepSerial(int delta);
    bool revBHint() const;  // rev B sizes on, glass, spindle too fast for the low bowls
    Cutaway cutaway_;
    Faceplate glassPlate_, barsPlate_;
    AdvancedPanel advanced_;
    BowlLevelsPanel levels_;
    juce::MidiKeyboardComponent keyboard_;
    RangeStrip range_;
    MagnetikaProcessor::Display d_;
    juce::Rectangle<float> nameplate_, infoWell_, buildWell_, pedals_, meters_;
};

} // namespace magnetika::plugin
