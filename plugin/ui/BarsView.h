#pragma once
// The eddy vibraphone in oblique 3-D: naturals in front, sharps raised behind,
// each bar at its rev B arched length (C3-B3 extrapolated), hung on node cords
// over angled rails. Tap to strike (where along the bar sets the strike
// position), drag across bars for a roll; holding keeps that bar's damper off.

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <vector>

#include "../PluginProcessor.h"

namespace magnetika::plugin {

class BarsView : public juce::Component, private juce::Timer {
public:
    static constexpr int kLow = 48, kHigh = 84;
    explicit BarsView(MagnetikaProcessor& p);
    ~BarsView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Lift every mallet now (view hidden or closed, ALL OFF): no bar stays counted as held.
    void releaseAllTouches();

    float levelOf(int midi) const;
    bool outsideBelow() const { return below_; }
    bool outsideAbove() const { return above_; }
    juce::String lastStrike() const { return lastStrike_; }
    void poll() { timerCallback(); }
    // centre of a bar's top face, in component coordinates
    juce::Point<float> barCentreOf(int midi) const;

private:
    friend struct BarsViewTestAccess;  // plugin/tests: two mallets without a touch screen
    struct Bar {
        int midi = 0;
        bool sharp = false;
        float x = 0, zc = 0, len = 0, yt = 0;
        juce::Path top, front, end, sheen;
        std::array<juce::Point<float>, 4> quad;
        juce::Point<float> a, b, tag;  // long axis end points (top face), tag anchor
    };
    struct Mallet {
        int midi = -1;
        juce::Point<float> pos, last;
        double lastTime = 0;
    };

    juce::Point<float> project(float x, float y, float z) const;
    void build();
    void renderStatic();
    int hit(juce::Point<float> p, float* pos) const;
    void strike(int midi, float vel, float pos);
    void lift(int midi);  // a mallet leaves: the damper returns when the last one has gone
    const juce::Image& glowSprite(size_t barIndex);
    void timerCallback() override;

    MagnetikaProcessor& proc_;
    std::vector<Bar> bars_;                     // midi order
    std::vector<int> drawBack_, drawFront_;     // indices, far to near
    juce::Path railsBack_, cordsBack_, railsFront_, cordsFront_, shadow_, frontRowShape_;
    float currentScale() const;
    juce::Image cache_;
    float cacheScale_ = 0;  // physical scale the cache was rendered at
    float pendingScale_ = 0;  // a new scale is re-rendered once it holds for a timer tick
    std::vector<juce::Image> glow_;  // per bar, a pre-blurred top face (single channel), built on demand
    std::vector<juce::Rectangle<float>> glowBounds_;
    std::map<int, Mallet> mallets_;
    std::map<int, int> malletsOn_;  // midi -> mallets resting on it
    std::array<float, 128> level_{}, paintedLevel_{};
    bool below_ = false, above_ = false;
    juce::String lastStrike_;
};

} // namespace magnetika::plugin
