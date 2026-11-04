#include "BarsView.h"

#include <cmath>

#include "Theme.h"

namespace magnetika::plugin {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDesignW = 1200.0f, kDesignH = 624.0f;
constexpr float kYaw = -20.0f * kPi / 180.0f, kPitch = 34.0f * kPi / 180.0f;
constexpr float kScale = 0.84f, kCX = 236.0f, kCY = 364.0f;
constexpr float kNodeFrac = 0.224f;

// rev B arched lengths (mm) C4..C6; C3..B3 extrapolated at sqrt(2) per octave (f ∝ 1/L²)
constexpr float kRevB[25] = {415, 403, 392, 381, 370, 359, 349, 339, 329, 320, 311, 302, 293,
                             285, 277, 269, 261, 254, 247, 240, 233, 226, 220, 214, 207};
float lengthOf(int m) { return m >= 60 ? kRevB[m - 60] : kRevB[m - 48] * 1.41421356f; }
bool isSharp(int m) { const int k = m % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
juce::String noteName(int m) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[m % 12]) + juce::String(m / 12 - 1);
}
juce::Path polyPath(const std::vector<juce::Point<float>>& pts, bool close) {
    juce::Path p;
    if (pts.empty()) return p;
    p.startNewSubPath(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) p.lineTo(pts[i]);
    if (close) p.closeSubPath();
    return p;
}
float levelFromEnv(float env) {
    if (env <= 1e-6f) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(env) + 48.0f) / 36.0f);
}

} // namespace

BarsView::BarsView(MagnetikaProcessor& p) : proc_(p) { startTimerHz(30); }
BarsView::~BarsView() {
    stopTimer();
    releaseAllTouches();
}

float BarsView::currentScale() const {
    return juce::jlimit(1.0f, 2.5f, juce::Component::getApproximateScaleFactorForComponent(this));
}

void BarsView::visibilityChanged() {
    if (!isVisible()) releaseAllTouches();  // a hidden view gets no mouseUp
}

void BarsView::releaseAllTouches() {
    for (const auto& [midi, count] : malletsOn_) {
        juce::ignoreUnused(count);
        proc_.pushUiEvent({MagnetikaProcessor::UiEvent::NoteOff, midi, 0});
    }
    mallets_.clear();
    malletsOn_.clear();
}

juce::Point<float> BarsView::project(float x, float y, float z) const {
    const float ca = std::cos(kYaw), sa = std::sin(kYaw), cb = std::cos(kPitch), sb = std::sin(kPitch);
    const float X1 = x * ca - z * sa, Z1 = x * sa + z * ca, Y2 = y * cb - Z1 * sb;
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    const float ox = (getWidth() - kDesignW * k) * 0.5f, oy = (getHeight() - kDesignH * k) * 0.5f;
    return {ox + k * (kCX + kScale * X1), oy + k * (kCY - kScale * Y2)};
}

void BarsView::resized() {
    build();
    renderStatic();
}

void BarsView::build() {
    auto P = [this](float x, float y, float z) { return project(x, y, z); };
    std::map<int, float> xOf;
    int k = 0;
    for (int m = kLow; m <= kHigh; ++m) if (!isSharp(m)) xOf[m] = static_cast<float>(k++) * 44.0f;
    for (int m = kLow; m <= kHigh; ++m) if (isSharp(m)) xOf[m] = xOf[m - 1] + 22.0f;

    bars_.clear();
    for (int m = kLow; m <= kHigh; ++m) {
        Bar b;
        b.midi = m;
        b.sharp = isSharp(m);
        b.zc = b.sharp ? -215.0f : 215.0f;
        b.yt = b.sharp ? 30.0f : 0.0f;
        b.len = lengthOf(m);
        b.x = xOf[m];
        const float xa = b.x - 19, xb = b.x + 19, yb = b.yt - 12, z0 = b.zc - b.len / 2, z1 = b.zc + b.len / 2;
        b.quad = {P(xa, b.yt, z0), P(xb, b.yt, z0), P(xb, b.yt, z1), P(xa, b.yt, z1)};
        b.top = polyPath({b.quad.begin(), b.quad.end()}, true);
        b.front = polyPath({P(xa, yb, z1), P(xb, yb, z1), P(xb, b.yt, z1), P(xa, b.yt, z1)}, true);
        b.end = polyPath({P(xa, yb, z0), P(xa, yb, z1), P(xa, b.yt, z1), P(xa, b.yt, z0)}, true);
        b.sheen = polyPath({P(xa + 9, b.yt, z0 + 10), P(xa + 9, b.yt, z1 - 10)}, false);
        b.a = P(b.x, b.yt, z0);
        b.b = P(b.x, b.yt, z1);
        b.tag = P(b.x, b.yt, z0 + 30);
        bars_.push_back(std::move(b));
    }
    auto row = [&](bool sharp, std::vector<int>& order, juce::Path& rails, juce::Path& cords) {
        std::vector<int> asc;
        for (int i = 0; i < static_cast<int>(bars_.size()); ++i) if (bars_[static_cast<size_t>(i)].sharp == sharp) asc.push_back(i);
        order.assign(asc.rbegin(), asc.rend());  // far (right) first
        auto nodeZ = [&](const Bar& b, float sign) { return b.zc + sign * (b.len / 2 - b.len * kNodeFrac); };
        rails.clear();
        cords.clear();
        for (float sign : {-1.0f, 1.0f}) {
            const Bar& f = bars_[static_cast<size_t>(asc.front())];
            const Bar& l = bars_[static_cast<size_t>(asc.back())];
            rails.addPath(polyPath({P(f.x - 40, f.yt - 40, nodeZ(f, sign)), P(l.x + 40, l.yt - 40, nodeZ(l, sign))}, false));
            std::vector<juce::Point<float>> c;
            for (int i : asc) { const Bar& b = bars_[static_cast<size_t>(i)]; c.push_back(P(b.x, b.yt - 5, nodeZ(b, sign))); }
            cords.addPath(polyPath(c, false));
        }
    };
    row(true, drawBack_, railsBack_, cordsBack_);
    row(false, drawFront_, railsFront_, cordsFront_);
    shadow_ = polyPath({P(-60, -120, -540), P(990, -120, -540), P(990, -120, 540), P(-60, -120, 540)}, true);
    frontRowShape_.clear();
    for (int i : drawFront_) {
        frontRowShape_.addPath(bars_[static_cast<size_t>(i)].top);
        frontRowShape_.addPath(bars_[static_cast<size_t>(i)].front);
    }
}

void BarsView::renderStatic() {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // the editor scales this view with a transform; render at the physical scale (capped)
    const float scale = currentScale();
    cacheScale_ = pendingScale_ = scale;
    glow_.assign(bars_.size(), {});
    glowBounds_.assign(bars_.size(), {});
    cache_ = juce::Image(juce::Image::ARGB, juce::roundToInt(getWidth() * scale), juce::roundToInt(getHeight() * scale), true);
    juce::Graphics g(cache_);
    g.addTransform(juce::AffineTransform::scale(scale));
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), juce::roundToInt(20 * k), {}).drawForPath(g, shadow_);
    auto drawRow = [&](const std::vector<int>& order, const juce::Path& rails, const juce::Path& cords) {
        g.setColour(juce::Colour(0xff2c3135));
        g.strokePath(rails, juce::PathStrokeType(9 * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff6b7378));
        g.strokePath(rails, juce::PathStrokeType(2 * k), juce::AffineTransform::translation(0, -3 * k));
        g.setColour(juce::Colour(0xff9a3444));
        g.strokePath(cords, juce::PathStrokeType(2.5f * k));
        for (int i : order) {
            const Bar& b = bars_[static_cast<size_t>(i)];
            g.setColour(juce::Colour(0xff4f565b));
            g.fillPath(b.end);
            g.setColour(juce::Colour(0xff6f777c));
            g.fillPath(b.front);
            const auto bb = b.top.getBounds();
            juce::ColourGradient alu(juce::Colour(0xff9aa1a7), bb.getX(), bb.getCentreY(), juce::Colour(0xff8a9197), bb.getRight(), bb.getCentreY(), false);
            alu.addColour(0.4, juce::Colour(0xffeef1f3));
            g.setGradientFill(alu);
            g.fillPath(b.top);
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.strokePath(b.top, juce::PathStrokeType(1.0f * k));
            g.strokePath(b.sheen, juce::PathStrokeType(1.5f * k));
        }
    };
    drawRow(drawBack_, railsBack_, cordsBack_);
    drawRow(drawFront_, railsFront_, cordsFront_);
}

const juce::Image& BarsView::glowSprite(size_t i) {
    if (i < glow_.size() && !glow_[i].isValid()) {
        const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
        const int radius = juce::roundToInt(9 * k) + 1;
        const auto bounds = bars_[i].top.getBounds().expanded(static_cast<float>(radius) * 2 + 2);
        glowBounds_[i] = bounds;
        const float s = cacheScale_ > 0 ? cacheScale_ : 1.0f;
        juce::Image img(juce::Image::SingleChannel, std::max(1, juce::roundToInt(bounds.getWidth() * s)),
                        std::max(1, juce::roundToInt(bounds.getHeight() * s)), true, juce::SoftwareImageType());
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::translation(-bounds.getX(), -bounds.getY()).scaled(s));
        juce::DropShadow(juce::Colours::white, radius, {}).drawForPath(g, bars_[i].top);
        glow_[i] = img;
    }
    return glow_[i];
}

void BarsView::paint(juce::Graphics& g) {
    if (!cache_.isValid()) renderStatic();  // a changed scale is re-rendered from the timer, once it settles
    if (cache_.isValid()) g.drawImage(cache_, getLocalBounds().toFloat());
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    const auto accent = theme::amber;
    auto overlay = [&](size_t bi, float L) {
        const Bar& b = bars_[bi];
        const auto& sprite = glowSprite(bi);  // blurred once, tinted per frame
        g.setColour(accent.withAlpha(0.8f * L));
        g.drawImage(sprite, glowBounds_[bi], juce::RectanglePlacement::stretchToFit, true);
        g.setColour(accent.withAlpha(0.55f * L));
        g.fillPath(b.top);
        g.setColour(juce::Colour(0xff8a5a26).withAlpha(L));
        g.fillPath(b.front);
        g.setColour(juce::Colour(0xffffe6b8).withAlpha(0.4f + 0.6f * L));
        g.strokePath(b.top, juce::PathStrokeType(1.2f * k));
    };
    {   // back row: never paint over the front row
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path clip;
        clip.addRectangle(getLocalBounds().toFloat().expanded(10));
        clip.addPath(frontRowShape_);
        clip.setUsingNonZeroWinding(false);
        g.reduceClipRegion(clip);
        for (int i : drawBack_) {
            const float L = level_[static_cast<size_t>(bars_[static_cast<size_t>(i)].midi)];
            if (L >= 0.04f) overlay(static_cast<size_t>(i), L);
        }
    }
    for (int i : drawFront_) {
        const float L = level_[static_cast<size_t>(bars_[static_cast<size_t>(i)].midi)];
        if (L >= 0.04f) overlay(static_cast<size_t>(i), L);
    }
    // mallets in play
    for (const auto& [idx, ml] : mallets_) {
        juce::ignoreUnused(idx);
        const auto p = ml.pos;
        juce::Path shaft;
        shaft.addLineSegment({p.x + 4 * k, p.y - 8 * k, p.x + 58 * k, p.y - 108 * k}, 5 * k);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd7b582), p.x, p.y, juce::Colour(0xff9b7445), p.x + 6 * k, p.y, false));
        g.fillPath(shaft);
        auto head = juce::Rectangle<float>(30 * k, 28 * k).withCentre(p.translated(0, -12 * k));
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(head.translated(0, 4 * k));
        juce::ColourGradient yarn(juce::Colour(0xffb4707f), head.getX() + head.getWidth() * 0.38f, head.getY() + head.getHeight() * 0.32f,
                                  juce::Colour(0xff4f2a35), head.getRight(), head.getBottom(), true);
        yarn.addColour(0.6, juce::Colour(0xff7d4655));
        g.setGradientFill(yarn);
        g.fillEllipse(head);
    }
    // tags
    g.setFont(theme::mono(13.0f * k));
    for (const auto& b : bars_) {
        const float L = level_[static_cast<size_t>(b.midi)];
        if (L < 0.04f) continue;
        const auto name = noteName(b.midi);
        const float alpha = std::min(1.0f, 0.35f + L);
        auto tag = juce::Rectangle<float>((14.0f + 8.0f * static_cast<float>(name.length())) * k, 21 * k).withCentre({b.tag.x, b.tag.y - 24 * k});
        g.setGradientFill(juce::ColourGradient(theme::amberPale.withAlpha(alpha), tag.getX(), tag.getY(), accent.withAlpha(alpha), tag.getX(), tag.getBottom(), false));
        g.fillRoundedRectangle(tag, 4 * k);
        g.setColour(accent.withAlpha(alpha));
        g.drawLine(b.tag.x, tag.getBottom(), b.tag.x, b.tag.y, 1.0f);
        g.setColour(theme::brassInk.withAlpha(alpha));
        g.drawText(name, tag, juce::Justification::centred);
    }
}

juce::Point<float> BarsView::barCentreOf(int midi) const {
    if (midi < kLow || midi > kHigh || bars_.empty()) return {};
    const auto& b = bars_[static_cast<size_t>(midi - kLow)];
    return (b.a + b.b) * 0.5f;
}

// Topmost bar under the point (front row and near bars first), and where along it.
int BarsView::hit(juce::Point<float> p, float* pos) const {
    auto test = [&](int i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        if (!b.top.contains(p)) return false;
        const auto ab = b.b - b.a;
        const float u = juce::jlimit(0.0f, 1.0f, ((p - b.a).x * ab.x + (p - b.a).y * ab.y) / (ab.x * ab.x + ab.y * ab.y));
        if (pos) *pos = std::abs(u - 0.5f) * 2.0f;
        return true;
    };
    for (auto it = drawFront_.rbegin(); it != drawFront_.rend(); ++it) if (test(*it)) return bars_[static_cast<size_t>(*it)].midi;
    for (auto it = drawBack_.rbegin(); it != drawBack_.rend(); ++it) if (test(*it)) return bars_[static_cast<size_t>(*it)].midi;
    return -1;
}

void BarsView::strike(int midi, float vel, float pos) {
    using T = MagnetikaProcessor::UiEvent;
    ++malletsOn_[midi];  // every strike sounds; the damper stays off while any mallet rests on the bar
    proc_.pushUiEvent({T::NoteOn, midi, vel});
    proc_.pushUiEvent({T::StrikePos, midi, pos});
    lastStrike_ = "Struck " + noteName(midi) + " at " + juce::String(pos, 2) + " (centre 0, end 1), velocity " + juce::String(vel, 2) + ".";
}

void BarsView::lift(int midi) {
    auto it = malletsOn_.find(midi);
    if (it == malletsOn_.end()) return;
    if (--it->second <= 0) {
        malletsOn_.erase(it);
        proc_.pushUiEvent({MagnetikaProcessor::UiEvent::NoteOff, midi, 0});
    }
}

void BarsView::mouseDown(const juce::MouseEvent& e) {
    float pos = 0;
    const int m = hit(e.position, &pos);
    if (m < 0) return;
    const float vel = e.isPressureValid() ? juce::jlimit(0.1f, 1.0f, 0.3f + e.pressure) : 0.9f;
    strike(m, vel, pos);
    Mallet ml;
    ml.midi = m;
    ml.pos = ml.last = e.position;
    ml.lastTime = juce::Time::getMillisecondCounterHiRes();
    mallets_[e.source.getIndex()] = ml;
    repaint();
}

void BarsView::mouseDrag(const juce::MouseEvent& e) {
    auto it = mallets_.find(e.source.getIndex());
    if (it == mallets_.end()) return;
    Mallet& ml = it->second;
    const double now = juce::Time::getMillisecondCounterHiRes();
    float pos = 0;
    const int m = hit(e.position, &pos);
    if (m >= 0 && m != ml.midi) {  // dragged onto the next bar: a roll, louder when faster
        const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
        const float speed = e.position.getDistanceFrom(ml.last) / static_cast<float>(std::max(1.0, now - ml.lastTime) / 1000.0) / k;
        lift(ml.midi);
        strike(m, juce::jlimit(0.1f, 1.0f, 0.45f + speed / 2000.0f), pos);
        ml.midi = m;
    }
    ml.pos = e.position;
    ml.last = e.position;
    ml.lastTime = now;
    repaint();
}

void BarsView::mouseUp(const juce::MouseEvent& e) {
    auto it = mallets_.find(e.source.getIndex());
    if (it == mallets_.end()) return;
    lift(it->second.midi);
    mallets_.erase(it);
    repaint();
}

float BarsView::levelOf(int midi) const { return midi >= 0 && midi < 128 ? level_[static_cast<size_t>(midi)] : 0.0f; }

void BarsView::timerCallback() {
    const auto d = proc_.readDisplay();
    std::array<float, 128> lvl{};
    bool below = false, above = false;
    for (int i = 0; i < d.count; ++i) {
        const auto& v = d.voices[static_cast<size_t>(i)];
        if (v.glass || v.midi < 0 || v.midi > 127) continue;
        const float L = levelFromEnv(v.level);
        lvl[static_cast<size_t>(v.midi)] = std::max(lvl[static_cast<size_t>(v.midi)], L);
        if (L > 0.04f && v.midi < kLow) below = true;
        if (L > 0.04f && v.midi > kHigh) above = true;
    }
    // repaint only when something visible changed (an idle view costs nothing)
    // (against the last painted frame, so a slow decay still repaints, and goes dark at the end)
    bool changed = below != below_ || above != above_;
    for (size_t m = 0; m < 128 && !changed; ++m)
        changed = std::abs(lvl[m] - paintedLevel_[m]) > 0.004f || (lvl[m] >= 0.04f) != (paintedLevel_[m] >= 0.04f);
    const float scale = currentScale();
    if (std::abs(scale - cacheScale_) > 0.05f) {  // window resized: re-render once the scale settles
        if (std::abs(scale - pendingScale_) < 0.01f) {
            renderStatic();
            changed = true;
        }
        pendingScale_ = scale;
    }
    level_ = lvl;
    below_ = below;
    above_ = above;
    if (changed) {
        paintedLevel_ = lvl;
        repaint();
    }
}

} // namespace magnetika::plugin
