#include "GlassView.h"

#include <cmath>

#include "Theme.h"

namespace magnetika::plugin {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDesignW = 1200.0f, kDesignH = 624.0f;
// camera: yaw about the vertical, then pitch down; design-space scale and origin
constexpr float kYaw = -30.0f * kPi / 180.0f, kPitch = 20.0f * kPi / 180.0f;
constexpr float kScale = 1.32f, kCX = 224.0f, kCY = 300.0f;
// case, mm: spindle along x at y = 0, z = 0; rims 18 mm apart
constexpr float x0 = -70, x1 = 36 * 18 + 80, yb = -125, yt = -8, zf = 120, zk = -120, wall = 14;
constexpr int kRimPts = 72;

// rev B diameters C4..C6; C3..B3 extrapolated at sqrt(2) per octave (rim-ring model, D ∝ 1/sqrt(f))
constexpr float kRevB[25] = {147, 143, 139, 135, 131, 127, 123, 120, 117, 113, 110, 107, 104,
                             101, 98, 95, 93, 90, 87, 85, 82, 80, 78, 76, 73};
float diameterOf(int midi) { return midi >= 60 ? kRevB[midi - 60] : kRevB[midi - 48] * 1.41421356f; }

juce::String noteName(int m) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[m % 12]) + juce::String(m / 12 - 1);
}

// Optics per glass material (plugin::materials() order).
struct Optics {
    juce::Colour edge;
    float clear, sparkle, body;
    juce::Colour bodyColour;
};
Optics opticsFor(int i) {
    switch (i) {
        case 0: case 1: return {juce::Colour(0xffcfe9d8), 1.0f, 0.9f, 0.0f, {}};   // soda-lime: green edge
        case 2: return {juce::Colours::white, 0.9f, 1.35f, 0.0f, {}};               // lead crystal: brilliant
        case 6: return {juce::Colour(0xfff4fbfd), 0.72f, 1.1f, 0.0f, {}};           // fused silica: barely there
        case 7: return {juce::Colour(0xfff4fbfd), 0.68f, 1.15f, 0.0f, {}};
        case 8: return {juce::Colour(0xff5e6467), 0.5f, 0.4f, 0.82f, juce::Colour(0xff23272a)};  // glass-ceramic
        case 9: return {juce::Colour(0xffc3c8cc), 0.3f, 1.2f, 0.9f, juce::Colour(0xff7d858b)};   // metallic glass
        default: return {juce::Colour(0xffdff3f8), 1.0f, 1.0f, 0.0f, {}};           // borosilicate
    }
}

juce::Path polyPath(const std::vector<juce::Point<float>>& pts, bool close) {
    juce::Path p;
    if (pts.empty()) return p;
    p.startNewSubPath(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) p.lineTo(pts[i]);
    if (close) p.closeSubPath();
    return p;
}

// A blurred stroke: the soft reflections and glows of the glass.
void softStroke(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, int radius) {
    juce::Path s;
    juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(s, p);
    juce::DropShadow(c, radius, {}).drawForPath(g, s);
}

float levelFromEnv(float env) {
    if (env <= 1e-6f) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(env) + 48.0f) / 36.0f);
}

} // namespace

GlassView::GlassView(MagnetikaProcessor& p) : proc_(p) {
    setOpaque(false);
    startTimerHz(30);
}

GlassView::~GlassView() { stopTimer(); }

bool GlassView::spindleOn() const { return proc_.state().getRawParameterValue(id::spindleOn)->load() > 0.5f; }

juce::Point<float> GlassView::project(float x, float y, float z) const {
    const float ca = std::cos(kYaw), sa = std::sin(kYaw), cb = std::cos(kPitch), sb = std::sin(kPitch);
    const float X1 = x * ca - z * sa, Z1 = x * sa + z * ca, Y2 = y * cb - Z1 * sb;
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    const float ox = (getWidth() - kDesignW * k) * 0.5f, oy = (getHeight() - kDesignH * k) * 0.5f;
    return {ox + k * (kCX + kScale * X1), oy + k * (kCY - kScale * Y2)};
}

void GlassView::resized() {
    build();
    renderStatic();
}

void GlassView::setMaterial(int index) {
    if (index == material_) return;
    material_ = index;
    renderStatic();
    repaint();
}

void GlassView::build() {
    auto P = [this](float x, float y, float z) { return project(x, y, z); };
    auto quad = [&](std::initializer_list<std::array<float, 3>> pts) {
        std::vector<juce::Point<float>> v;
        for (auto& q : pts) v.push_back(P(q[0], q[1], q[2]));
        return polyPath(v, true);
    };
    auto ring = [&](float x, float r, int n, float t0, float t1) {
        std::vector<juce::Point<float>> v;
        for (int k = 0; k <= n; ++k) {
            const float t = t0 + (t1 - t0) * static_cast<float>(k) / static_cast<float>(n);
            v.push_back(P(x, r * std::cos(t), r * std::sin(t)));
        }
        return v;
    };
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);

    bowls_.clear();
    for (int m = kLow; m <= kHigh; ++m) {
        Bowl b;
        b.midi = m;
        const int i = m - kLow;
        const float r = diameterOf(m) / 2, x = static_cast<float>(i) * 18.0f;
        b.r = r;
        b.centre = P(x, 0, 0);
        b.R = k * kScale * r;
        const float step = 2 * kPi / kRimPts;
        b.rim = ring(x, r, kRimPts - 1, 0, step * (kRimPts - 1));
        // hemisphere silhouette: half circle on the +x side, half rim ellipse on the -x side
        const auto U = P(x + 1, 0, 0);
        const float ux = U.x - b.centre.x, uy = U.y - b.centre.y;
        size_t iMax = 0;
        float dMax = 0;
        for (size_t j = 0; j < b.rim.size(); ++j) {
            const float d = b.rim[j].getDistanceFrom(b.centre);
            if (d > dMax) { dMax = d; iMax = j; }
        }
        const size_t N = b.rim.size();
        const auto P1 = b.rim[iMax], P2 = b.rim[(iMax + N / 2) % N];
        const auto midA = b.rim[(iMax + 3 * N / 4) % N];
        const bool dirA = ((midA.x - b.centre.x) * ux + (midA.y - b.centre.y) * uy) < 0;
        // the far half-circle, sampled (JUCE paths take arcs by angle, so sample it)
        const float a1 = std::atan2(P1.y - b.centre.y, P1.x - b.centre.x);
        const float vx = P1.x - b.centre.x, vy = P1.y - b.centre.y;
        const float sweepSign = ((-vy * ux + vx * uy) > 0) ? 1.0f : -1.0f;
        b.body.startNewSubPath(P1);
        for (int s = 1; s <= 36; ++s) {
            const float a = a1 + sweepSign * kPi * static_cast<float>(s) / 36.0f;
            b.body.lineTo(b.centre.x + b.R * std::cos(a), b.centre.y + b.R * std::sin(a));
        }
        if (dirA) { for (size_t s = N / 2; s <= N; ++s) b.body.lineTo(b.rim[(iMax + s) % N]); }
        else { for (size_t s = N / 2 + 1; s-- > 0;) b.body.lineTo(b.rim[(iMax + s) % N]); }
        b.body.closeSubPath();

        b.mouth = polyPath(b.rim, true);
        const auto innerPts = ring(x, r - 2, kRimPts - 1, 0, step * (kRimPts - 1));
        b.inner = polyPath(innerPts, true);
        b.wall = b.mouth;
        b.wall.addPath(polyPath(innerPts, true));
        b.wall.setUsingNonZeroWinding(false);
        b.band = polyPath(ring(x + 5.5f, r * 0.996f, kRimPts - 1, 0, step * (kRimPts - 1)), true);
        b.bandHi = polyPath(ring(x + 3.2f, r * 0.998f, 18, -1.6f, 0.2f), false);
        auto lat = [&](float frac) { return std::make_pair(x + r * frac, r * std::sqrt(1 - frac * frac)); };
        const auto l1 = lat(0.42f), l2 = lat(0.6f), l3 = lat(0.3f), l4 = lat(0.52f), l5 = lat(0.12f);
        b.streak = polyPath(ring(l1.first, l1.second, 14, -1.25f, -0.25f), false);
        b.streakWide = polyPath(ring(l2.first, l2.second, 14, -1.0f, 0.1f), false);
        b.window = polyPath(ring(l3.first, l3.second, 6, -1.18f, -0.84f), false);
        b.window2 = polyPath(ring(l5.first, l5.second, 5, -1.1f, -0.92f), false);
        b.warm = polyPath(ring(l4.first, l4.second, 16, 0.55f, 1.75f), false);
        b.ao = polyPath(ring(x + r * 0.2f, r * 0.98f, 14, 0.75f, 1.55f), false);
        b.aoIn = polyPath(ring(x + 2, r - 5, 14, -2.5f, -0.9f), false);
        b.lipHi = polyPath(ring(x, r, 10, -1.45f, -0.55f), false);
        b.lipHi2 = polyPath(ring(x, r, 6, 2.35f, 2.75f), false);
        b.lipHi3 = polyPath(ring(x, r, 5, 0.35f, 0.7f), false);
        b.tip = P(x, r, 0);
        bowls_.push_back(std::move(b));
    }

    // case
    shadow_ = quad({{x0 - 30, yb - 4, zk - 30}, {x1 + 30, yb - 4, zk - 30}, {x1 + 30, yb - 4, zf + 40}, {x0 - 30, yb - 4, zf + 40}});
    backInner_ = quad({{x0, yb, zk + wall}, {x1, yb, zk + wall}, {x1, yt, zk + wall}, {x0, yt, zk + wall}});
    farInner_ = quad({{x1 - wall, yb, zk}, {x1 - wall, yb, zf}, {x1 - wall, yt, zf}, {x1 - wall, yt, zk}});
    floorInner_ = quad({{x0, yb + 10, zk}, {x1, yb + 10, zk}, {x1, yb + 10, zf}, {x0, yb + 10, zf}});
    backTop_ = quad({{x0, yt, zk}, {x1, yt, zk}, {x1, yt, zk + wall}, {x0, yt, zk + wall}});
    farBearing_ = quad({{x1 - wall - 2, yt, -16}, {x1 + 2, yt, -16}, {x1 + 2, 14, -16}, {x1 - wall - 2, 14, -16}});
    front_ = quad({{x0, yb, zf}, {x1, yb, zf}, {x1, yt, zf}, {x0, yt, zf}});
    frontTop_ = quad({{x0, yt, zf - wall}, {x1, yt, zf - wall}, {x1, yt, zf}, {x0, yt, zf}});
    frontLip_ = polyPath({P(x0, yt, zf), P(x1, yt, zf)}, false);
    nearEnd_ = quad({{x0, yb, zk}, {x0, yb, zf}, {x0, yt, zf}, {x0, yt, zk}});
    nearTop_ = quad({{x0, yt, zk}, {x0 + wall, yt, zk}, {x0 + wall, yt, zf}, {x0, yt, zf}});
    auto box = [&](float xa, float xb, float ya, float yb2, float za, float zb, juce::Path& top, juce::Path& fr, juce::Path& en) {
        top = quad({{xa, yb2, za}, {xb, yb2, za}, {xb, yb2, zb}, {xa, yb2, zb}});
        fr = quad({{xa, ya, zb}, {xb, ya, zb}, {xb, yb2, zb}, {xa, yb2, zb}});
        en = quad({{xa, ya, za}, {xa, ya, zb}, {xa, yb2, zb}, {xa, yb2, za}});
    };
    juce::Path plinthTop;
    box(x0 - 8, x1 + 8, yb - 14, yb, zk - 8, zf + 8, plinthTop, plinthFront_, plinthEnd_);
    box(x0 - 2, x0 + wall + 2, yt, 14, -16, 16, bearingTop_, bearingFront_, bearingEnd_);
    box(-122, -84, -100, -54, -26, 26, motorTop_, motorFront_, motorEnd_);
    auto disc = [&](float x, float yc, float r) {
        std::vector<juce::Point<float>> v;
        for (int j = 0; j < 40; ++j) {
            const float t = static_cast<float>(j) / 40.0f * 2 * kPi;
            v.push_back(P(x, yc + r * std::cos(t), r * std::sin(t)));
        }
        return polyPath(v, true);
    };
    pulleyBig_ = disc(-104, 0, 16);
    pulleySmall_ = disc(-104, -44, 10);
    rod_ = {P(x0 - 20, 0, 0), P(x1 + 6, 0, 0)};
    stub_ = {P(x0 - 44, 0, 0), P(x0 - 4, 0, 0)};
    beltA_ = {P(-104, 0, 16), P(-104, -44, 10)};
    beltB_ = {P(-104, 0, -16), P(-104, -44, -10)};
    brass_ = P(x0 + 230, -60, zf);
    const auto along = P(x0 + 231, -60, zf);
    brassAngle_ = std::atan2(along.y - brass_.y, along.x - brass_.x);

    // what hides the lower bowls: overlays (glows) must not paint over it
    occluders_.clear();
    occluders_.addRectangle(getLocalBounds().toFloat().expanded(10));
    occluders_.addPath(front_);
    occluders_.addPath(nearEnd_);
    occluders_.addPath(plinthFront_);
    occluders_.setUsingNonZeroWinding(false);
}

void GlassView::renderStatic() {
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const float scale = juce::Component::getApproximateScaleFactorForComponent(this);
    cache_ = juce::Image(juce::Image::ARGB, juce::roundToInt(getWidth() * scale), juce::roundToInt(getHeight() * scale), true);
    juce::Graphics g(cache_);
    g.addTransform(juce::AffineTransform::scale(scale));
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    const auto o = opticsFor(material_);
    const float c = o.clear, sp = o.sparkle;

    // ---- the case behind the bowls
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), juce::roundToInt(18 * k), {}).drawForPath(g, shadow_);
    {
        const auto b = backInner_.getBounds();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a1a0f), b.getTopLeft(), juce::Colour(0xff150d07), b.getBottomRight(), false));
        g.fillPath(backInner_);
    }
    g.setColour(juce::Colour(0xff1b110a));
    g.fillPath(farInner_);
    g.setColour(juce::Colour(0xff120b06));
    g.fillPath(floorInner_);
    g.setColour(juce::Colour(0xff6b4a30));
    g.fillPath(backTop_);
    g.setColour(juce::Colour(0xff8d949a));
    g.fillPath(farBearing_);
    auto steel = [&](juce::Line<float> l, float w) {
        juce::Path p;
        p.addLineSegment(l, w);
        const auto bb = p.getBounds();
        juce::ColourGradient grad(juce::Colour(0xffeef1f3), bb.getX(), bb.getY(), juce::Colour(0xff3f454a), bb.getX(), bb.getBottom(), false);
        grad.addColour(0.55, juce::Colour(0xff8d949a));
        g.setGradientFill(grad);
        g.strokePath(polyPath({l.getStart(), l.getEnd()}, false), juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    steel(rod_, k * kScale * 12);

    // ---- bowls, far (small) to near (large)
    for (auto it = bowls_.rbegin(); it != bowls_.rend(); ++it) {
        const Bowl& b = *it;
        if (o.body > 0) {
            g.setColour(o.bodyColour.withAlpha(o.body));
            g.fillPath(b.body);
        }
        juce::ColourGradient shell(juce::Colour(0xffeaf8fb).withAlpha(0.03f * c), b.centre, juce::Colours::white.withAlpha(0.7f * c),
                                   b.centre.translated(b.R, 0), true);
        shell.addColour(0.55, juce::Colour(0xffcfeaf2).withAlpha(0.05f * c));
        shell.addColour(0.84, o.edge.withAlpha(0.17f * c));
        shell.addColour(0.95, o.edge.withAlpha(0.42f * c));
        g.setGradientFill(shell);
        g.fillPath(b.body);
        g.setColour(o.edge.withAlpha(0.6f));
        g.strokePath(b.body, juce::PathStrokeType(1.2f * k));

        softStroke(g, b.warm, juce::Colour(0xffc98a52).withAlpha(0.2f * c), 12 * k, juce::roundToInt(7 * k));
        softStroke(g, b.ao, juce::Colour(0xff06151a).withAlpha(0.38f), 6 * k, juce::roundToInt(4 * k));
        softStroke(g, b.streakWide, juce::Colours::white.withAlpha(std::min(1.0f, 0.16f * sp)), 12 * k, juce::roundToInt(4 * k));
        softStroke(g, b.streak, juce::Colours::white.withAlpha(std::min(1.0f, 0.5f * sp)), 2.5f * k, juce::roundToInt(1.5f * k) + 1);
        softStroke(g, b.window, juce::Colours::white.withAlpha(std::min(1.0f, 0.85f * sp)), 5 * k, juce::roundToInt(1.5f * k) + 1);
        g.setColour(juce::Colours::white.withAlpha(std::min(1.0f, 0.45f * sp)));
        g.strokePath(b.window2, juce::PathStrokeType(2 * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(juce::Colour(0xff1d1611).withAlpha(0.95f));
        g.strokePath(b.band, juce::PathStrokeType(k * kScale * 5));
        g.setColour(juce::Colour(0xfff3e2c8).withAlpha(0.18f));
        g.strokePath(b.bandHi, juce::PathStrokeType(1.0f * k));

        juce::ColourGradient mouth(juce::Colour(0xff061013).withAlpha(0.5f), b.centre, o.edge.withAlpha(0.34f), b.centre.translated(b.R, 0), true);
        mouth.addColour(0.62, juce::Colour(0xff0d2127).withAlpha(0.3f));
        mouth.addColour(0.9, juce::Colour(0xff6fa9b6).withAlpha(0.16f));
        g.setGradientFill(mouth);
        g.fillPath(b.mouth);
        softStroke(g, b.aoIn, juce::Colour(0xff04110f).withAlpha(0.45f), 7 * k, juce::roundToInt(4 * k));

        g.setColour(o.edge.withAlpha(0.5f * c));
        g.fillPath(b.wall);
        g.setColour(juce::Colour(0xb378b4c0));
        g.strokePath(b.inner, juce::PathStrokeType(1.0f * k));
        g.setColour(juce::Colour(0xebf6fdff));
        g.strokePath(b.mouth, juce::PathStrokeType(1.6f * k));
        softStroke(g, b.lipHi, juce::Colours::white.withAlpha(std::min(1.0f, 0.95f * sp)), 2.8f * k, juce::roundToInt(1.5f * k) + 1);
        g.setColour(juce::Colours::white.withAlpha(std::min(1.0f, 0.55f * sp)));
        g.strokePath(b.lipHi2, juce::PathStrokeType(2 * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath(b.lipHi3, juce::PathStrokeType(1.4f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- the case in front
    {
        const auto b = front_.getBounds();
        juce::ColourGradient wood(juce::Colour(0xff5b3b23), b.getX(), b.getY(), juce::Colour(0xff3a2414), b.getRight(), b.getBottom(), false);
        wood.addColour(0.45, juce::Colour(0xff4a2f1b));
        g.setGradientFill(wood);
        g.fillPath(front_);
        // grain: faint long curves, clipped to the panel
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(front_);
        juce::Random rng(7);
        for (int i = 0; i < 26; ++i) {
            juce::Path grain;
            const float y = b.getY() + b.getHeight() * static_cast<float>(i) / 26.0f;
            grain.startNewSubPath(b.getX(), y);
            grain.cubicTo(b.getX() + b.getWidth() * 0.3f, y - 6 * k + rng.nextFloat() * 12 * k, b.getX() + b.getWidth() * 0.65f,
                          y - 6 * k + rng.nextFloat() * 12 * k, b.getRight(), y - b.getHeight() * 0.06f);
            g.setColour(i % 3 == 0 ? juce::Colours::white.withAlpha(0.04f) : juce::Colours::black.withAlpha(0.12f));
            g.strokePath(grain, juce::PathStrokeType(1.0f * k));
        }
    }
    g.setColour(juce::Colour(0xff8a6141));
    g.fillPath(frontTop_);
    g.setColour(juce::Colour(0xffc49a6c).withAlpha(0.5f));
    g.strokePath(frontLip_, juce::PathStrokeType(1.5f * k));
    {
        const auto b = nearEnd_.getBounds();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff6a4529), b.getX(), b.getY(), juce::Colour(0xff3b2515), b.getX(), b.getBottom(), false));
        g.fillPath(nearEnd_);
    }
    g.setColour(juce::Colour(0xff8a6141));
    g.fillPath(nearTop_);
    g.setColour(juce::Colour(0xff241609));
    g.fillPath(plinthFront_);
    g.setColour(juce::Colour(0xff1c1108));
    g.fillPath(plinthEnd_);
    g.setColour(juce::Colour(0xffc9ced2));
    g.fillPath(bearingTop_);
    g.setColour(juce::Colour(0xff8d949a));
    g.fillPath(bearingFront_);
    g.setColour(juce::Colour(0xff6b7277));
    g.fillPath(bearingEnd_);
    steel(stub_, k * kScale * 12);
    g.setColour(juce::Colour(0xff5d6368));
    g.fillPath(motorTop_);
    g.setColour(juce::Colour(0xff33383c));
    g.fillPath(motorFront_);
    g.setColour(juce::Colour(0xff262a2d));
    g.fillPath(motorEnd_);
    for (auto* pl : {&pulleyBig_, &pulleySmall_}) {
        g.setColour(juce::Colour(0xff9aa1a6));
        g.fillPath(*pl);
        g.setColour(juce::Colour(0xff3f454a));
        g.strokePath(*pl, juce::PathStrokeType(1.0f));
    }
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::rotation(brassAngle_, brass_.x, brass_.y));
        g.setColour(juce::Colour(0xffd8b46a));
        g.setFont(theme::sans(13.0f * k, true).withExtraKerningFactor(0.3f));
        g.drawSingleLineText(juce::String::fromUTF8("MAGNETIKA  \xc2\xb7  No. 1"), juce::roundToInt(brass_.x), juce::roundToInt(brass_.y));
    }
}

juce::Path GlassView::patternPath(const Bowl& b, float angle, float level) const {
    // the (2,0) pattern: four lobes in the mouth, oriented by the engine's pattern angle
    const int i = b.midi - kLow;
    const float x = static_cast<float>(i) * 18.0f;
    std::vector<juce::Point<float>> pts;
    for (int k = 0; k <= 96; ++k) {
        const float t = static_cast<float>(k) / 96.0f * 2 * kPi;
        const float rr = b.r * (0.5f + 0.34f * level * std::abs(std::cos(2 * (t - angle))));
        pts.push_back(project(x, rr * std::cos(t), rr * std::sin(t)));
    }
    return polyPath(pts, true);
}

void GlassView::paint(juce::Graphics& g) {
    if (cache_.isValid()) g.drawImage(cache_, getLocalBounds().toFloat());
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    const auto accent = theme::amber;

    // moving belt while the spindle runs
    {
        const float dashes[] = {6 * k, 6 * k};
        const float off = std::fmod(beltPhase_ * k, 12 * k);
        juce::PathStrokeType st(3 * k);
        juce::Path da, db;
        st.createDashedStroke(da, polyPath({beltA_.getPointAlongLine(-off), beltA_.getEnd()}, false), dashes, 2);
        st.createDashedStroke(db, polyPath({beltB_.getPointAlongLine(-off), beltB_.getEnd()}, false), dashes, 2);
        g.setColour(juce::Colour(0xff141414));
        g.fillPath(da);
        g.fillPath(db);
    }

    // sounding bowls: tint, the turning pattern, the glow. Clipped so they never
    // paint over the case front that hides the lower bowls.
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(occluders_);
        for (auto it = bowls_.rbegin(); it != bowls_.rend(); ++it) {
            const Bowl& b = *it;
            const float L = level_[static_cast<size_t>(b.midi)];
            if (L < 0.04f) continue;
            g.setColour(accent.withAlpha(0.16f * L));
            g.fillPath(b.body);
            g.setColour(accent.withAlpha(0.14f * L));
            g.fillPath(b.mouth);
            const auto pat = patternPath(b, pattern_[static_cast<size_t>(b.midi)], L);
            g.setColour(accent.withAlpha(0.22f * L));
            g.fillPath(pat);
            g.setColour(accent.withAlpha(0.9f * L));
            g.strokePath(pat, juce::PathStrokeType(1.5f * k));
            softStroke(g, b.mouth, accent.withAlpha(0.9f * L), 10 * k, juce::roundToInt(6 * k) + 1);
            g.setColour(juce::Colour(0xffffe9c2).withAlpha(0.5f + 0.5f * L));
            g.strokePath(b.mouth, juce::PathStrokeType(1.6f * k));
        }
    }

    // fingers on the rims
    for (const auto& [idx, f] : fingers_) {
        juce::ignoreUnused(idx);
        auto r = juce::Rectangle<float>(30 * k, 22 * k).withCentre(f.pos.translated(0, -3 * k));
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(r.translated(0, 3 * k));
        juce::ColourGradient skin(juce::Colour(0xfff0c3a8), r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.35f,
                                  juce::Colour(0xff9c654d), r.getRight(), r.getBottom(), true);
        skin.addColour(0.7, juce::Colour(0xffc98d70));
        g.setGradientFill(skin);
        g.fillEllipse(r);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse(r.getX() + 7 * k, r.getY() + 4 * k, 10 * k, 5 * k);
    }

    // note tags over the sounding bowls
    g.setFont(theme::mono(13.0f * k));
    for (const auto& b : bowls_) {
        const float L = level_[static_cast<size_t>(b.midi)];
        if (L < 0.04f) continue;
        const auto name = noteName(b.midi);
        const float w = (14.0f + 8.0f * static_cast<float>(name.length())) * k, h = 21 * k;
        auto tag = juce::Rectangle<float>(w, h).withCentre({b.tip.x, b.tip.y - 26 * k});
        const float alpha = std::min(1.0f, 0.35f + L);
        g.setColour(accent.withAlpha(0.35f * alpha));
        g.fillRoundedRectangle(tag.expanded(3 * k), 6 * k);
        g.setGradientFill(juce::ColourGradient(theme::amberPale.withAlpha(alpha), tag.getX(), tag.getY(), accent.withAlpha(alpha), tag.getX(), tag.getBottom(), false));
        g.fillRoundedRectangle(tag, 4 * k);
        g.setColour(accent.withAlpha(alpha));
        g.drawLine(b.tip.x, tag.getBottom(), b.tip.x, b.tip.y - 2 * k, 1.0f);
        g.setColour(theme::brassInk.withAlpha(alpha));
        g.drawText(name, tag, juce::Justification::centred);
    }
}

int GlassView::hit(juce::Point<float> p, juce::Point<float>* onRim) const {
    const float k = std::min(getWidth() / kDesignW, getHeight() / kDesignH);
    float best = 15.0f * k;
    int m = -1;
    for (const auto& b : bowls_)
        for (const auto& q : b.rim) {
            const float d = q.getDistanceFrom(p);
            if (d < best) {
                best = d;
                m = b.midi;
                if (onRim) *onRim = q;
            }
        }
    return m;
}

void GlassView::send(MagnetikaProcessor::UiEvent::Type t, int midi, float v) { proc_.pushUiEvent({t, midi, v}); }

void GlassView::mouseDown(const juce::MouseEvent& e) {
    juce::Point<float> rim;
    const int m = hit(e.position, &rim);
    if (m < 0) return;
    Finger f;
    f.midi = m;
    f.pos = rim;
    f.last = e.position;
    f.lastTime = f.lastMoveTime = juce::Time::getMillisecondCounterHiRes();
    f.pressure = e.isPressureValid() ? juce::jlimit(0.2f, 1.6f, 0.4f + 1.2f * e.pressure) : 1.0f;
    using T = MagnetikaProcessor::UiEvent;
    send(T::NoteOn, m, 0.8f);
    send(T::Touch, m, f.pressure);
    if (!spindleOn()) send(T::FingerSpeed, m, 0.0f);  // a still finger on a still bowl: silent until it rubs
    fingers_[e.source.getIndex()] = f;
    repaint();
}

void GlassView::mouseDrag(const juce::MouseEvent& e) {
    auto it = fingers_.find(e.source.getIndex());
    if (it == fingers_.end()) return;
    Finger& f = it->second;
    using T = MagnetikaProcessor::UiEvent;
    const double now = juce::Time::getMillisecondCounterHiRes();
    juce::Point<float> rim;
    const int m = hit(e.position, &rim);
    if (m >= 0 && m != f.midi) {  // slid onto the next rim: glissando
        send(T::NoteOff, f.midi, 0);
        send(T::NoteOn, m, 0.8f);
        send(T::Touch, m, f.pressure);
        if (!spindleOn()) send(T::FingerSpeed, m, 0.0f);
        f.midi = m;
    }
    if (m >= 0) f.pos = rim;
    if (e.isPressureValid()) {
        f.pressure = juce::jlimit(0.2f, 1.6f, 0.4f + 1.2f * e.pressure);
        send(T::Touch, f.midi, f.pressure);
    }
    if (!spindleOn()) {
        // rubbing: angular speed along this rim, rad/s, from the pointer speed
        const float dt = static_cast<float>(std::max(1.0, now - f.lastTime)) / 1000.0f;
        const auto& b = bowls_[static_cast<size_t>(f.midi - kLow)];
        const float speed = e.position.getDistanceFrom(f.last) / dt / std::max(1.0f, b.R * 0.6f);
        send(T::FingerSpeed, f.midi, juce::jlimit(0.0f, 12.0f, speed));
        if (e.position.getDistanceFrom(f.last) > 0.5f) f.lastMoveTime = now;
    }
    f.last = e.position;
    f.lastTime = now;
    repaint();
}

void GlassView::mouseUp(const juce::MouseEvent& e) {
    auto it = fingers_.find(e.source.getIndex());
    if (it == fingers_.end()) return;
    send(MagnetikaProcessor::UiEvent::NoteOff, it->second.midi, 0);
    fingers_.erase(it);
    repaint();
}

float GlassView::levelOf(int midi) const { return midi >= 0 && midi < 128 ? level_[static_cast<size_t>(midi)] : 0.0f; }

void GlassView::timerCallback() {
    const auto d = proc_.readDisplay();
    std::array<float, 128> lvl{};
    below_ = above_ = false;
    for (int i = 0; i < d.count; ++i) {
        const auto& v = d.voices[static_cast<size_t>(i)];
        if (!v.glass || v.midi < 0 || v.midi > 127) continue;
        const float L = levelFromEnv(v.level);
        lvl[static_cast<size_t>(v.midi)] = std::max(lvl[static_cast<size_t>(v.midi)], L);
        pattern_[static_cast<size_t>(v.midi)] = v.pattern;
        if (L > 0.04f && v.midi < kLow) below_ = true;
        if (L > 0.04f && v.midi > kHigh) above_ = true;
    }
    level_ = lvl;
    // a finger that has stopped on a stopped spindle stops exciting its bowl
    if (!spindleOn()) {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto& [idx, f] : fingers_) {
            juce::ignoreUnused(idx);
            if (now - f.lastMoveTime > 80.0 && now - f.lastTime > 80.0) {
                send(MagnetikaProcessor::UiEvent::FingerSpeed, f.midi, 0.0f);
                f.lastTime = now;
            }
        }
    }
    if (spindleOn()) beltPhase_ += 12.0f * 0.033f / 0.6f * proc_.state().getRawParameterValue("g_spin")->load();
    repaint();
}

} // namespace magnetika::plugin
