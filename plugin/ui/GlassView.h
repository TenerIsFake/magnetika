#pragma once
// The glass armonica in oblique 3-D: 37 nested bowls (C3-C6) on the spindle in a
// walnut case, drawn from the rev B tables in millimetres. Sounding bowls glow and
// show their (2,0) pattern turning; fingers press, drag across rims (glissando)
// and, with the spindle stopped, must rub along a rim to sound it.

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <vector>

#include "../PluginProcessor.h"

namespace magnetika::plugin {

class GlassView : public juce::Component, private juce::Timer {
public:
    static constexpr int kLow = 48, kHigh = 84;  // C3..C6
    explicit GlassView(MagnetikaProcessor& p);
    ~GlassView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Glass optics follow the material preset (index into plugin::materials()).
    void setMaterial(int index);
    // Level 0..1 per note from the latest display, for the note strip.
    float levelOf(int midi) const;
    bool outsideBelow() const { return below_; }
    bool outsideAbove() const { return above_; }
    // Read the processor's display state now (normally a 30 Hz timer does it).
    void poll() { timerCallback(); }
    // The top of a bowl's rim, in component coordinates (where a finger plays it).
    juce::Point<float> rimTopOf(int midi) const {
        return midi >= kLow && midi <= kHigh && !bowls_.empty() ? bowls_[static_cast<size_t>(midi - kLow)].tip : juce::Point<float>{};
    }

private:
    struct Bowl {
        int midi = 0;
        float r = 0;  // rim radius, mm
        std::vector<juce::Point<float>> rim;
        juce::Path body, mouth, inner, wall, band, bandHi, streak, streakWide, window, window2, warm, ao, aoIn,
            lipHi, lipHi2, lipHi3;
        juce::Point<float> centre, tip;
        float R = 0;  // projected radius, px
    };
    struct Finger {
        int midi = -1;
        juce::Point<float> pos, last;
        double lastTime = 0, lastMoveTime = 0;
        float pressure = 0.85f;
    };

    juce::Point<float> project(float x, float y, float z) const;
    void build();
    void renderStatic();
    juce::Path patternPath(const Bowl& b, float angle, float level) const;
    int hit(juce::Point<float> p, juce::Point<float>* onRim) const;
    void timerCallback() override;
    void send(MagnetikaProcessor::UiEvent::Type t, int midi, float v);
    bool spindleOn() const;

    MagnetikaProcessor& proc_;
    std::vector<Bowl> bowls_;  // index midi - kLow; drawn far (high) to near (low)
    juce::Path front_, frontTop_, frontLip_, nearEnd_, nearTop_, plinthFront_, plinthEnd_, backInner_, farInner_,
        floorInner_, backTop_, farBearing_, shadow_, bearingTop_, bearingFront_, bearingEnd_, motorTop_, motorFront_,
        motorEnd_, pulleyBig_, pulleySmall_, occluders_;
    juce::Line<float> rod_, stub_, beltA_, beltB_;
    juce::Point<float> brass_;
    float brassAngle_ = 0;

    juce::Image cache_;
    int material_ = 3;
    std::map<int, Finger> fingers_;  // by touch source index
    std::array<float, 128> level_{};
    std::array<float, 128> pattern_{};
    bool below_ = false, above_ = false;
    float beltPhase_ = 0;
};

} // namespace magnetika::plugin
