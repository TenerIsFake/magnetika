#pragma once
// The glass armonica in oblique 3-D: 49 nested bowls (C3-C7) on the spindle in a
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
    static constexpr int kLow = 48, kHigh = 96;  // C3..C7
    explicit GlassView(MagnetikaProcessor& p);
    ~GlassView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Lift every finger now (view hidden or closed, ALL OFF): no rim stays counted as held.
    void releaseAllTouches();

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
        return midi >= low_ && midi <= high_ && !bowls_.empty() ? bowls_[static_cast<size_t>(midi - low_)].tip : juce::Point<float>{};
    }
    // The instrument drawn: Magnetika (C3-C7) or Franklin's armonica (37 glasses G3-G6,
    // painted rims, mahogany, treadle-driven). Follows the glassModel parameter.
    bool franklin() const { return franklin_; }
    int lowNote() const { return low_; }
    int highNote() const { return high_; }

private:
    friend struct GlassViewTestAccess;  // plugin/tests: multi-touch without a touch screen
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
        float sentSpeed = -1, sentTouch = -1;  // last values sent, to avoid flooding the queue
        bool zeroSent = false;
    };
    void press(int midi, float pressure);    // per-note finger count: note on at the first finger
    void release(int midi);                  // note off when the last finger leaves
    void sendSpeed(Finger& f, float speed);
    void sendTouch(Finger& f, float touch);
    const juce::Image& glowSprite(size_t bowlIndex);

    juce::Point<float> project(float x, float y, float z) const;
    void build();
    void renderStatic();
    juce::Path patternPath(const Bowl& b, float angle, float level) const;
    int hit(juce::Point<float> p, juce::Point<float>* onRim) const;
    void timerCallback() override;
    void send(MagnetikaProcessor::UiEvent::Type t, int midi, float v);
    bool spindleOn() const;

    MagnetikaProcessor& proc_;
    std::vector<Bowl> bowls_;  // index midi - low_; drawn far (high) to near (low)
    bool franklin_ = false;
    int low_ = kLow, high_ = kHigh;
    float xOff_ = 0;  // mm along the spindle: a shorter instrument is centred where the long one sits
    void setModel(bool franklin);
    juce::Path front_, frontTop_, frontLip_, nearEnd_, nearTop_, plinthFront_, plinthEnd_, backInner_, farInner_,
        floorInner_, backTop_, farBearing_, shadow_, bearingTop_, bearingFront_, bearingEnd_, motorTop_, motorFront_,
        motorEnd_, pulleyBig_, pulleySmall_, occluders_;
    juce::Line<float> rod_, stub_, beltA_, beltB_;
    juce::Point<float> brass_;
    float brassAngle_ = 0;

    float currentScale() const;  // physical pixels per component pixel, capped for the cache
    juce::Image cache_;
    float cacheScale_ = 0;  // physical scale the cache was rendered at
    float pendingScale_ = 0;  // a new scale is re-rendered once it holds for a timer tick (resize drags)
    std::vector<juce::Image> glow_;          // per bowl, a pre-blurred rim (single channel), built on demand
    std::vector<juce::Rectangle<float>> glowBounds_;
    int material_ = 3;
    std::map<int, Finger> fingers_;  // by touch source index
    std::map<int, int> fingersOn_;   // midi -> fingers holding it
    std::array<float, 128> level_{};
    std::array<float, 128> pattern_{};
    std::array<float, 128> paintedLevel_{}, paintedPattern_{};  // as last painted
    bool below_ = false, above_ = false;
    float beltPhase_ = 0;
};

} // namespace magnetika::plugin
