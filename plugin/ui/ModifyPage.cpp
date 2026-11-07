#include "ModifyPage.h"

#include <cmath>

#include "../Parameters.h"
#include "Theme.h"

namespace magnetika::plugin {

namespace {
constexpr float kPi = 3.14159265f;
constexpr int kLow = 48, kHigh = 84;
constexpr int kGlassHigh = 96;  // the glass is drawn C3-C7, the bars C3-C6
const juce::Identifier materialProp{"material"};

juce::String noteName(int m) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[m % 12]) + juce::String(m / 12 - 1);
}
constexpr float kRevBDiam[25] = {147, 143, 139, 135, 131, 127, 123, 120, 117, 113, 110, 107, 104,
                                 101, 98, 95, 93, 90, 87, 85, 82, 80, 78, 76, 73};
constexpr float kRevBLen[25] = {415, 403, 392, 381, 370, 359, 349, 339, 329, 320, 311, 302, 293,
                                285, 277, 269, 261, 254, 247, 240, 233, 226, 220, 214, 207};
float diamOf(int m) {  // rev B C4-C6, extrapolated at sqrt 2 per octave beyond
    return m > 84 ? kRevBDiam[std::min(24, m - 72)] / 1.41421356f
                  : m >= 60 ? kRevBDiam[m - 60] : kRevBDiam[std::max(0, m - 48)] * 1.41421356f;
}
float lenOf(int m) { return m >= 60 ? kRevBLen[std::min(24, m - 60)] : kRevBLen[std::max(0, m - 48)] * 1.41421356f; }
float f0Of(int m) { return 440.0f * std::pow(2.0f, (static_cast<float>(m) - 69.0f) / 12.0f); }
float levelFromEnv(float env) {
    if (env <= 1e-6f) return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(env) + 48.0f) / 36.0f);
}
juce::String dbText(float env) { return env <= 1e-6f ? juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e dB") : juce::String(juce::roundToInt(20.0f * std::log10(env))) + " dB"; }

void label(juce::Graphics& g, const juce::String& t, float x, float y, float size = 12.0f, juce::Colour c = juce::Colour(0xffd9c6ab), bool semibold = false) {
    g.setColour(c);
    g.setFont(theme::sans(size, semibold).withExtraKerningFactor(0.12f));
    g.drawSingleLineText(t, juce::roundToInt(x), juce::roundToInt(y));
}
void copperCoil(juce::Graphics& g, juce::Point<float> c, float r) {
    auto e = juce::Rectangle<float>(2 * r, 2 * r).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff1b37a), e.getX(), e.getY(), juce::Colour(0xff6b3413), e.getRight(), e.getBottom(), false));
    g.fillEllipse(e);
    g.setColour(juce::Colour(0xff3a1c0a));
    g.drawEllipse(e, 1.0f);
    g.setColour(juce::Colour(0xb35a2a0e));
    g.drawEllipse(e.reduced(r * 0.33f), 1.0f);
    g.setColour(juce::Colour(0xff2a1408));
    g.fillEllipse(e.reduced(r * 0.67f));
}
} // namespace

// ------------------------------------------------------------------ RangeStrip

void RangeStrip::paint(juce::Graphics& g) {
    const auto lo = kb_.getRectangleForKey(kLow);
    const auto hi = kb_.getRectangleForKey(high);
    if (lo.isEmpty() || hi.isEmpty()) return;
    const float x0 = lo.getX() + static_cast<float>(kb_.getX() - getX());
    const float x1 = hi.getRight() + static_cast<float>(kb_.getX() - getX());
    auto bar = juce::Rectangle<float>(x0, 1.0f, x1 - x0, static_cast<float>(getHeight()) - 2.0f);
    g.setColour(theme::amber.withAlpha(0.28f));
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(theme::amber.withAlpha(0.6f));
    g.drawRoundedRectangle(bar, 3.0f, 1.0f);
    g.setColour(juce::Colour(0xfff6dcae));
    g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.14f));
    g.drawText(juce::String::fromUTF8("INSTRUMENT RANGE C3 \xe2\x80\x93 ") + noteName(high), bar, juce::Justification::centred);
}

// ------------------------------------------------------------------ Cutaway

void Cutaway::timerCallback() {
    d_ = proc_.readDisplay();
    const bool glass = shownInstrument == 0;
    float best = 0;
    int pick = -1;
    for (int i = 0; i < d_.count; ++i) {
        const auto& v = d_.voices[static_cast<size_t>(i)];
        if (v.glass != glass) continue;
        if (v.level > best) { best = v.level; pick = i; }
    }
    if (pick >= 0) {
        const auto& v = d_.voices[static_cast<size_t>(pick)];
        selected_ = v.midi;
        level_ = levelFromEnv(v.level);
        pattern_ = v.pattern;
    } else {
        level_ *= 0.85f;
        if (selected_ < 0 || level_ < 0.01f) selected_ = glass ? 72 : 69;
    }
    phase_ += 0.033f * 2 * kPi * 1.6f;  // a slowed-down picture of the vibration
    repaint();
}

void Cutaway::paint(juce::Graphics& g) {
    theme::drawViewport(g, getLocalBounds().toFloat());
    if (shownInstrument == 0) paintGlass(g);
    else paintBars(g);
}

void Cutaway::paintGlass(juce::Graphics& g) {
    const int m = selected_ < 0 ? 72 : selected_;
    const juce::Point<float> c{320, 205};
    const float R = 134, L = level_, psi = pattern_;
    const auto accent = theme::amber;
    // spindle behind, soft shadow
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe5e8ea), 0, 198, juce::Colour(0xff4b5156), 0, 212, false));
    g.setOpacity(0.55f);
    g.fillRect(0.0f, 198.0f, 640.0f, 14.0f);
    g.setOpacity(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(juce::Rectangle<float>(300, 300).withCentre(c.translated(4, 9)));
    // the (2,0) envelope and drive axis, at the engine's pattern angle
    auto ring = [&](std::function<float(float)> rad) {
        juce::Path p;
        for (int i = 0; i <= 144; ++i) {
            const float t = static_cast<float>(i) / 144.0f * 2 * kPi, r = rad(t);
            const juce::Point<float> q{c.x + r * std::cos(t), c.y - r * std::sin(t)};
            if (i == 0) p.startNewSubPath(q); else p.lineTo(q);
        }
        p.closeSubPath();
        return p;
    };
    const auto env = ring([&](float t) { return R + 26 * L * std::abs(std::cos(2 * (t - psi))); });
    g.setColour(accent.withAlpha(0.16f));
    g.fillPath(env);
    g.setColour(accent.withAlpha(0.85f));
    g.strokePath(env, juce::PathStrokeType(1.5f));
    {
        juce::Path axis;
        axis.startNewSubPath(c.x - 175 * std::cos(psi), c.y + 175 * std::sin(psi));
        axis.lineTo(c.x + 175 * std::cos(psi), c.y - 175 * std::sin(psi));
        juce::Path dashed;
        const float dash[] = {9, 7};
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, axis, dash, 2);
        g.setColour(juce::Colour(0xff3fb6c9));
        g.fillPath(dashed);
    }
    // glass
    g.setColour(juce::Colour(0xff1d1814));
    g.drawEllipse(juce::Rectangle<float>(280, 280).withCentre(c), 7.0f);  // fired ferrite band
    juce::ColourGradient glass(juce::Colour(0xffcfeef5).withAlpha(0.22f), c.x - 15, c.y - 30, juce::Colour(0xffe6fbff).withAlpha(0.45f), c.x + R, c.y, true);
    glass.addColour(0.55, juce::Colour(0xff7fb7c6).withAlpha(0.1f));
    glass.addColour(0.88, juce::Colour(0xff9fd3df).withAlpha(0.22f));
    g.setGradientFill(glass);
    g.fillEllipse(juce::Rectangle<float>(2 * R, 2 * R).withCentre(c));
    g.setColour(juce::Colour(0xffcfeef5).withAlpha(0.18f));
    g.drawEllipse(juce::Rectangle<float>(144, 144).withCentre(c), 1.0f);
    g.drawEllipse(juce::Rectangle<float>(44, 44).withCentre(c), 1.0f);
    g.setColour(juce::Colour(0xff9ea6ab));
    g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre(c));
    // the rim, instantaneous: the standing wave, slowed
    const auto rim = ring([&](float t) { return R + 12 * L * std::sin(phase_) * std::cos(2 * (t - psi)) + 3 * L * std::cos(3 * (t - psi) + 0.7f); });
    g.setColour(juce::Colour(0xffbfe9f3).withAlpha(0.95f));
    g.strokePath(rim, juce::PathStrokeType(2.5f));
    // highlights
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    juce::Path hl;
    hl.addCentredArc(c.x, c.y, 128, 128, 0, -1.25f, -0.35f, true);
    g.strokePath(hl, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // finger at the top
    g.setColour(juce::Colour(0xffc9957a));
    g.fillEllipse(juce::Rectangle<float>(34, 24).withCentre({320, 66}));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.fillEllipse(juce::Rectangle<float>(12, 6).withCentre({315, 62}));
    label(g, "FINGER", 344, 60);
    // coils at 180, 225, 270, 315 deg; IR sensors at 202.5 and 247.5 deg
    for (auto p : {juce::Point<float>{160, 205}, {207, 318}, {320, 365}, {433, 318}}) copperCoil(g, p, 15);
    const float blink = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(phase_ * 0.8f));
    for (auto p : {juce::Point<float>{172, 266}, {259, 353}}) {
        g.setColour(juce::Colour(0xff1b1f22));
        g.fillRoundedRectangle(juce::Rectangle<float>(16, 16).withCentre(p), 2);
        g.setColour(juce::Colour(0xff555d63));
        g.drawRoundedRectangle(juce::Rectangle<float>(16, 16).withCentre(p), 2, 1);
        g.setColour(juce::Colour(0xffd14a5c).withAlpha(blink));
        g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre(p));
    }
    // ears
    g.setColour(juce::Colour(0xffd0607a));
    g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre({503, 157}));
    g.fillEllipse(juce::Rectangle<float>(14, 14).withCentre({503, 253}));
    label(g, "EARS L / R", 518, 211);
    // titles
    label(g, "BOWL " + juce::String(m - kLow + 1) + juce::String::fromUTF8("  \xc2\xb7  ") + noteName(m), 24, 34, 15.0f, theme::ink, true);
    label(g, juce::String::fromUTF8("\xc3\x98") + juce::String(juce::roundToInt(diamOf(m))) + juce::String::fromUTF8(" mm  \xc2\xb7  (2,0) ") +
             juce::String(f0Of(m), 1) + " Hz", 24, 52, 12.0f, theme::inkFaint);
    label(g, juce::String::fromUTF8("coils 180\xc2\xb0 225\xc2\xb0 270\xc2\xb0 315\xc2\xb0  \xc2\xb7  IR 202.5\xc2\xb0 247.5\xc2\xb0"), 24, 400, 12.0f, theme::inkFaint);
    // readouts
    const juce::String vals[] = {juce::String(juce::roundToInt(psi * 180 / kPi)) + juce::String::fromUTF8("\xc2\xb0"),
                                 level_ > 0.02f ? dbText(std::pow(10.0f, (level_ * 36.0f - 48.0f) / 20.0f)) : juce::String::fromUTF8("\xe2\x80\x94"),
                                 noteName(m), juce::String(f0Of(m), 1) + " Hz"};
    const char* names[] = {"PATTERN", "LEVEL", "NOTE", "FUNDAMENTAL"};
    for (int i = 0; i < 4; ++i) {
        const float x = 20 + static_cast<float>(i) * 152;
        label(g, names[i], x, 428, 11.0f, theme::inkFaint);
        theme::drawReadout(g, vals[i], {x, 434, 140, 26}, 14.0f);
    }
    // the spindle: 49 bowls, sounding ones lit
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe5e8ea), 0, 513, juce::Colour(0xff6c7379), 0, 519, false));
    g.fillRoundedRectangle(20, 513, 600, 6, 3);
    constexpr float pitch = 11.8f;
    for (int n = kLow; n <= kGlassHigh; ++n) {
        const float x = 36 + static_cast<float>(n - kLow) * pitch, h = diamOf(n) * 0.34f;
        bool lit = false;
        for (int i = 0; i < d_.count; ++i) {
            const auto& v = d_.voices[static_cast<size_t>(i)];
            lit = lit || (v.glass && v.midi == n && levelFromEnv(v.level) > 0.04f);
        }
        auto e = juce::Rectangle<float>(6.5f, h).withCentre({x, 516});
        if (lit) {
            g.setColour(accent.withAlpha(0.35f));
            g.fillEllipse(e.expanded(4));
        }
        g.setColour(juce::Colour(0x4dcfeef5));
        g.fillEllipse(e);
        g.setColour(lit ? accent : juce::Colour(0x73cfeef5));
        g.drawEllipse(e, 1.0f);
    }
    for (auto [n, s] : {std::pair<int, const char*>{48, "C3"}, {60, "C4"}, {72, "C5"}, {84, "C6"}, {96, "C7"}})
        label(g, s, 30 + static_cast<float>(n - kLow) * pitch, 550, 11.0f, theme::inkFaint);
}

void Cutaway::paintBars(juce::Graphics& g) {
    const int m = selected_ < 0 ? 69 : selected_;
    const auto accent = theme::amber;
    label(g, juce::String::fromUTF8("37 BARS  \xc2\xb7  C3 \xe2\x80\x93 C6"), 20, 32, 15.0f, theme::ink, true);
    label(g, "arched underside, 4 : 1 partial  -  suspended at the fundamental nodes", 20, 50, 12.0f, theme::inkFaint);
    // top view
    const float mid = 150;
    for (int sign : {-1, 1}) {
        juce::Path cord;
        for (int n = kLow; n <= kHigh; ++n) {
            const float x = 30 + static_cast<float>(n - kLow) * 15.6f + 5.5f, h = lenOf(n) * 0.33f;
            const juce::Point<float> q{x, mid + static_cast<float>(sign) * (h / 2 - h * 0.224f)};
            if (n == kLow) cord.startNewSubPath(q); else cord.lineTo(q);
        }
        g.setColour(juce::Colour(0xff8a2f3c));
        g.strokePath(cord, juce::PathStrokeType(2.0f));
    }
    for (int n = kLow; n <= kHigh; ++n) {
        const float x = 30 + static_cast<float>(n - kLow) * 15.6f, h = lenOf(n) * 0.33f;
        auto r = juce::Rectangle<float>(x, mid - h / 2, 11, h);
        bool lit = false;
        for (int i = 0; i < d_.count; ++i) {
            const auto& v = d_.voices[static_cast<size_t>(i)];
            lit = lit || (!v.glass && v.midi == n && levelFromEnv(v.level) > 0.04f);
        }
        if (lit) {
            g.setColour(accent.withAlpha(0.4f));
            g.fillRoundedRectangle(r.expanded(4), 4);
        }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8f969c), r.getX(), 0, juce::Colour(0xff7e858b), r.getRight(), 0, false));
        g.fillRoundedRectangle(r, 2);
        g.setColour(juce::Colour(0xffe8ebed).withAlpha(0.8f));
        g.fillRect(r.getX() + 3.5f, r.getY() + 2, 3.0f, r.getHeight() - 4);
        if (lit) {
            g.setColour(accent);
            g.drawRoundedRectangle(r, 2, 1.2f);
        }
    }
    // side view of the selected bar, flexing in its first free-free mode
    const float bx0 = 80, bx1 = 560, by = 380, amp = 9 * level_ * std::sin(phase_);
    auto shape = [&](float a) {
        juce::Path p;
        for (int i = 0; i <= 60; ++i) {
            const float u = static_cast<float>(i) / 60.0f, s = u - 0.5f;
            const float w = (s * s - 0.276f * 0.276f) / 0.17f;
            const juce::Point<float> q{bx0 + u * (bx1 - bx0), by + a * w};
            if (i == 0) p.startNewSubPath(q); else p.lineTo(q);
        }
        return p;
    };
    g.setColour(juce::Colour(0xff3a3430));
    g.drawHorizontalLine(272, 20, 620);
    label(g, "BAR " + juce::String(m - kLow + 1) + juce::String::fromUTF8("  \xc2\xb7  ") + noteName(m), 20, 296, 15.0f, theme::ink, true);
    label(g, juce::String(f0Of(m), 1) + " Hz  -  L " + juce::String(juce::roundToInt(lenOf(m))) + " mm  -  nodes " +
             juce::String(juce::roundToInt(lenOf(m) * 0.224f)) + " mm from each end", 20, 314, 12.0f, theme::inkFaint);
    // mallet
    g.setColour(juce::Colour(0xffb08a5a));
    g.drawLine(238, 316, 152, 282, 4.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb4707f), 240, 334, juce::Colour(0xff4a2530), 262, 352, true));
    g.fillEllipse(juce::Rectangle<float>(34, 30).withCentre({248, 340}));
    // nodes
    const float n1 = bx0 + 0.224f * (bx1 - bx0), n2 = bx1 - 0.224f * (bx1 - bx0);
    for (float nx : {n1, n2}) {
        g.setColour(juce::Colour(0xff8a2f3c));
        g.fillEllipse(juce::Rectangle<float>(8, 8).withCentre({nx, by + 6}));
        g.drawLine(nx, by + 6, nx, by + 34, 2.0f);
        label(g, "NODE", nx - 14, by + 74, 11.0f, juce::Colour(0xffc98a95));
    }
    // the bar: rest outline, then the flexing bar
    g.setColour(juce::Colour(0x595b6267));
    g.strokePath(shape(0), juce::PathStrokeType(12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff1f3f4), 0, by - 6, juce::Colour(0xff6e767c), 0, by + 6, false));
    g.strokePath(shape(amp), juce::PathStrokeType(12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (level_ > 0.04f) {
        g.setColour(accent.withAlpha(0.35f * level_));
        g.strokePath(shape(amp), juce::PathStrokeType(18.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // magnet, coil, sensor
    g.setColour(juce::Colour(0xff2a2d30));
    g.fillRect(308.0f, by + 12, 24.0f, 5.0f);
    auto coil = juce::Rectangle<float>(297, by + 30, 46, 34);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff1b37a), coil.getX(), coil.getY(), juce::Colour(0xff6b3413), coil.getRight(), coil.getBottom(), false));
    g.fillRoundedRectangle(coil, 3);
    g.setColour(juce::Colour(0x995a2a0e));
    for (int i = 1; i <= 4; ++i) g.drawHorizontalLine(juce::roundToInt(coil.getY() + 6.0f * static_cast<float>(i)), coil.getX() + 3, coil.getRight() - 3);
    g.setColour(juce::Colour(0xff1b1f22));
    g.fillRoundedRectangle(262, by + 28, 14, 14, 2);
    g.setColour(juce::Colour(0xffd14a5c).withAlpha(0.6f + 0.4f * std::abs(std::sin(phase_ * 0.4f))));
    g.fillEllipse(266, by + 32, 6, 6);
    label(g, juce::String::fromUTF8("NdFeB \xc3\x98") + "10 x 2, BONDED", 352, by + 18, 11.0f);
    label(g, "AIR COIL  -  337 t  -  1.5 mm GAP", 352, by + 52, 11.0f);
    label(g, "IR SENSOR", 236, by + 98, 11.0f);
    // readouts
    const auto ratio = proc_.state().getRawParameterValue("b_ratio")->load();
    const auto damper = proc_.state().getRawParameterValue("b_damper")->load();
    const juce::String vals[] = {juce::String(f0Of(m), 1) + " Hz", juce::String(juce::roundToInt(f0Of(m) * ratio)) + " Hz",
                                 level_ > 0.02f ? dbText(std::pow(10.0f, (level_ * 36.0f - 48.0f) / 20.0f)) : juce::String::fromUTF8("\xe2\x80\x94"),
                                 juce::String(damper, 1) + " s"};
    const char* names[] = {"FUNDAMENTAL", "PARTIAL", "LEVEL", "TAIL (T60)"};
    for (int i = 0; i < 4; ++i) {
        const float x = 20 + static_cast<float>(i) * 152;
        label(g, names[i], x, 508, 11.0f, theme::inkFaint);
        theme::drawReadout(g, vals[i], {x, 514, 140, 26}, 14.0f);
    }
}

// ------------------------------------------------------------------ Faceplate

Faceplate::Faceplate(MagnetikaProcessor& p, std::vector<SwitchSpec> switches, std::vector<Section> sections)
    : proc_(p), sections_(std::move(sections)) {
    auto& st = proc_.state();
    for (const auto& s : switches) {
        auto b = std::make_unique<juce::TextButton>(s.label);
        b->setClickingTogglesState(true);
        b->getProperties().set("mgStacked", true);
        addAndMakeVisible(*b);
        switchAtts_.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, s.paramId, *b));
        switches_.push_back(std::move(b));
    }
    for (const auto& sec : sections_)
        for (const auto& k : sec.knobs) {
            Knob kn;
            kn.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            kn.slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            kn.slider->setTitle(k.label);
            kn.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, k.paramId, *kn.slider);
            kn.slider->onValueChange = [this] { repaint(); };
            kn.label = k.label;
            kn.param = st.getParameter(k.paramId);
            addAndMakeVisible(*kn.slider);
            knobs_.push_back(std::move(kn));
        }
    advancedBtn_.onClick = [this] { if (onAdvanced) onAdvanced(); };
    addAndMakeVisible(advancedBtn_);
    levelsBtn_.setTooltip("Bowl levels: even out the bowls and trim each one");
    levelsBtn_.onClick = [this] { if (onLevels) onLevels(); };
    addChildComponent(levelsBtn_);
}

void Faceplate::resized() {
    auto r = getLocalBounds().toFloat().reduced(20, 18);
    auto sw = r.removeFromTop(52);
    const float w = sw.getWidth() / static_cast<float>(std::max<size_t>(1, switches_.size()));
    for (size_t i = 0; i < switches_.size(); ++i)
        switches_[i]->setBounds(juce::Rectangle<float>(sw.getX() + w * static_cast<float>(i), sw.getY(), w, sw.getHeight()).reduced(4, 0).toNearestInt());
    r.removeFromTop(12);
    size_t k = 0;
    for (size_t s = 0; s < sections_.size(); ++s) {
        auto sec = r.removeFromTop(134);
        sec.removeFromTop(20);
        const float cw = sec.getWidth() / 4.0f;
        for (size_t j = 0; j < sections_[s].knobs.size() && k < knobs_.size(); ++j, ++k)
            knobs_[k].slider->setBounds(juce::Rectangle<float>(sec.getX() + cw * static_cast<float>(j), sec.getY(), cw, 70).withSizeKeepingCentre(70, 70).toNearestInt());
    }
    advancedBtn_.setBounds(getWidth() - 20 - 112, getHeight() - 18 - 30, 112, 30);
    levelsBtn_.setBounds(advancedBtn_.getX() - 8 - 96, advancedBtn_.getY(), 96, 30);
    levelsBtn_.setVisible(onLevels != nullptr);
}

void Faceplate::paint(juce::Graphics& g) {
    theme::drawFaceplate(g, getLocalBounds().toFloat());
    auto r = getLocalBounds().toFloat().reduced(20, 18);
    r.removeFromTop(52 + 12);
    size_t k = 0;
    for (size_t s = 0; s < sections_.size(); ++s) {
        auto sec = r.removeFromTop(134);
        auto head = sec.removeFromTop(20);
        theme::drawEngraved(g, sections_[s].title, head, juce::Justification::centredLeft, 12.0f);
        const float tw = juce::GlyphArrangement::getStringWidth(theme::sans(12.0f, true, true).withExtraKerningFactor(0.18f), sections_[s].title) + 12;
        g.setColour(juce::Colour(0xff8b9095));
        g.drawHorizontalLine(juce::roundToInt(head.getCentreY()), head.getX() + tw, head.getRight());
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(head.getCentreY()) + 1, head.getX() + tw, head.getRight());
        const float cw = sec.getWidth() / 4.0f;
        for (size_t j = 0; j < sections_[s].knobs.size() && k < knobs_.size(); ++j, ++k) {
            const auto cell = juce::Rectangle<float>(sec.getX() + cw * static_cast<float>(j), sec.getY() + 72, cw, 18);
            theme::drawEngraved(g, knobs_[k].label, cell, juce::Justification::centred, 11.0f);
            juce::String v;
            if (knobs_[k].param != nullptr) {
                v = knobs_[k].param->getCurrentValueAsText();
                if (knobs_[k].param->getLabel().isNotEmpty()) v << " " << knobs_[k].param->getLabel();
            }
            theme::drawReadout(g, v, cell.translated(0, 20).withSizeKeepingCentre(std::min(cw - 8, 92.0f), 19), 11.5f);
        }
    }
}

// ------------------------------------------------------------------ AdvancedPanel

AdvancedPanel::AdvancedPanel(MagnetikaProcessor& p) {
    auto& st = p.state();
    auto sectionOf = [](const juce::String& pid) {
        if (pid.startsWith("g_")) return juce::String("Glass bowls");
        if (pid.startsWith("b_")) return juce::String("Bars");
        return juce::String("Performance");
    };
    for (auto* param : p.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (ranged == nullptr) continue;
        Row r;
        r.section = sectionOf(ranged->paramID);
        r.label = std::make_unique<juce::Label>();
        r.label->setText(ranged->getName(64), juce::dontSendNotification);
        r.label->setColour(juce::Label::textColourId, theme::ink);
        r.label->setFont(theme::sans(12.5f));
        if (dynamic_cast<juce::AudioParameterBool*>(ranged) != nullptr) {
            auto t = std::make_unique<juce::ToggleButton>();
            r.button = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, ranged->paramID, *t);
            r.control = std::move(t);
        } else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(ranged)) {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList(c->choices, 1);
            r.combo = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(st, ranged->paramID, *box);
            r.control = std::move(box);
        } else {
            auto s = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::TextBoxRight);
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 20);
            s->setColour(juce::Slider::trackColourId, theme::amber.withAlpha(0.55f));
            s->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff120c08));
            s->setTextValueSuffix(" " + ranged->getLabel());
            r.slider = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, ranged->paramID, *s);
            r.control = std::move(s);
        }
        content_.addAndMakeVisible(*r.label);
        content_.addAndMakeVisible(*r.control);
        rows_.push_back(std::move(r));
    }
    std::stable_sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) {
        auto rank = [](const juce::String& s) { return s == "Performance" ? 0 : s == "Glass bowls" ? 1 : 2; };
        return rank(a.section) < rank(b.section);
    });
    for (const char* name : {"Performance", "Glass bowls", "Bars"}) {
        auto h = std::make_unique<juce::Label>();
        h->setText(juce::String(name).toUpperCase(), juce::dontSendNotification);
        h->setFont(theme::sans(13.0f, true).withExtraKerningFactor(0.16f));
        h->setColour(juce::Label::textColourId, theme::amber);
        content_.addAndMakeVisible(*h);
        headers_.push_back(std::move(h));
    }
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    closeBtn_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn_);
}

void AdvancedPanel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(r.translated(0, 6), 12.0f);
    g.setColour(juce::Colour(0xff18120d));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(theme::amber.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(theme::ink);
    g.setFont(theme::sans(14.0f, true).withExtraKerningFactor(0.2f));
    g.drawText("ADVANCED  -  EVERY PARAMETER", r.reduced(18, 14).removeFromTop(24), juce::Justification::centredLeft);
}

void AdvancedPanel::resized() {
    auto area = getLocalBounds().reduced(16, 14);
    auto top = area.removeFromTop(28);
    closeBtn_.setBounds(top.removeFromRight(84));
    area.removeFromTop(8);
    viewport_.setBounds(area);
    const int width = area.getWidth() - viewport_.getScrollBarThickness();
    constexpr int rowH = 26;
    int y = 0;
    size_t header = 0;
    juce::String current;
    for (auto& r : rows_) {
        if (r.section != current) {
            current = r.section;
            y += 6;
            if (header < headers_.size()) headers_[header++]->setBounds(0, y, width, rowH);
            y += rowH;
        }
        r.label->setBounds(0, y, width * 2 / 5, rowH - 2);
        r.control->setBounds(width * 2 / 5, y, width * 3 / 5 - 6, rowH - 2);
        y += rowH;
    }
    content_.setSize(width, y + 10);
}

// ------------------------------------------------------------------ BowlLevelsPanel

namespace {
constexpr float kGraphTopDb = MagnetikaProcessor::kTrimMaxDb, kGraphBottomDb = MagnetikaProcessor::kTrimMinDb;
bool isBlack(int midi) {
    const int pc = midi % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
juce::String dbString(float db) {
    return (db > 0.005f ? "+" : db < -0.005f ? juce::String::fromUTF8("\xe2\x88\x92") : "") + juce::String(std::abs(db), 1) + " dB";
}
} // namespace

BowlLevelsPanel::BowlLevelsPanel(MagnetikaProcessor& p) : proc_(p) {
    setTitle("Bowl levels");
    evenKnob_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    evenKnob_.setTitle("Bowl level evening");
    evenKnob_.setTooltip("Bowl level evening: how much of the built-in correction applies. 100% plays every bowl at "
                         "C4's level; 0% leaves the bowls as modelled (higher bowls louder, about 5.6 dB an octave).");
    evenAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), id::levelEven, evenKnob_);
    evenKnob_.onValueChange = [this] { repaint(); };
    addAndMakeVisible(evenKnob_);
    resetBtn_.setTooltip("Set every bowl's trim back to 0 dB");
    resetBtn_.onClick = [this] {
        proc_.resetBowlTrims();
        repaint();
    };
    addAndMakeVisible(resetBtn_);
    closeBtn_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn_);
}

float BowlLevelsPanel::xOf(int midi) const {
    const float w = graph_.getWidth() / static_cast<float>(kHigh - kLow + 1);
    return graph_.getX() + w * (static_cast<float>(midi - kLow) + 0.5f);
}

float BowlLevelsPanel::yOf(float db) const {
    return juce::jmap(juce::jlimit(kGraphBottomDb, kGraphTopDb, db), kGraphTopDb, kGraphBottomDb, graph_.getY(), graph_.getBottom());
}

int BowlLevelsPanel::bowlAt(float x) const {
    const float w = graph_.getWidth() / static_cast<float>(kHigh - kLow + 1);
    return juce::jlimit(kLow, kHigh, kLow + static_cast<int>(std::floor((x - graph_.getX()) / w)));
}

float BowlLevelsPanel::dbAt(float y) const {
    const float db = juce::jmap(y, graph_.getY(), graph_.getBottom(), kGraphTopDb, kGraphBottomDb);
    return std::abs(db) < 0.6f ? 0.0f : std::round(db * 2.0f) / 2.0f;  // half-dB steps, with a catch at 0
}

void BowlLevelsPanel::setTrim(int midi, float db) {
    proc_.setBowlTrimDb(midi, db);
    repaint();
}

void BowlLevelsPanel::mouseDown(const juce::MouseEvent& e) {
    if (!graph_.expanded(0, 8).contains(e.position)) return;
    lastDrag_ = bowlAt(e.position.x);
    setTrim(lastDrag_, dbAt(e.position.y));
}

void BowlLevelsPanel::mouseDrag(const juce::MouseEvent& e) {
    if (lastDrag_ < 0) return;
    // a sweep sets every bowl it crosses, on the straight line between the two points
    const int now = bowlAt(e.position.x);
    const float dbNow = dbAt(e.position.y), dbLast = proc_.bowlTrimDb(lastDrag_);
    const int step = now >= lastDrag_ ? 1 : -1;
    for (int m = lastDrag_ + step; m != now + step; m += step) {
        const float t = static_cast<float>(m - lastDrag_) / static_cast<float>(now - lastDrag_);
        proc_.setBowlTrimDb(m, std::round((dbLast + (dbNow - dbLast) * t) * 2.0f) / 2.0f);
    }
    proc_.setBowlTrimDb(now, dbNow);
    lastDrag_ = hover_ = now;
    repaint();
}

void BowlLevelsPanel::mouseUp(const juce::MouseEvent&) { lastDrag_ = -1; }

void BowlLevelsPanel::mouseMove(const juce::MouseEvent& e) {
    const int h = graph_.expanded(0, 8).contains(e.position) ? bowlAt(e.position.x) : -1;
    if (h != hover_) {
        hover_ = h;
        repaint();
    }
}

void BowlLevelsPanel::mouseExit(const juce::MouseEvent&) {
    hover_ = -1;
    repaint();
}

void BowlLevelsPanel::mouseDoubleClick(const juce::MouseEvent& e) {
    if (graph_.expanded(0, 8).contains(e.position)) setTrim(bowlAt(e.position.x), 0.0f);
}

void BowlLevelsPanel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(r.translated(0, 6), 12.0f);
    g.setColour(juce::Colour(0xff18120d));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(theme::amber.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(theme::ink);
    g.setFont(theme::sans(14.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(juce::String::fromUTF8("BOWL LEVELS  \xc2\xb7  C3\xe2\x80\x93" "C7"), r.reduced(18, 14).removeFromTop(24),
               juce::Justification::centredLeft);

    // evening readout beside its knob
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.16f));
    g.drawText("EVENING", knobCell_.withTrimmedLeft(52).removeFromTop(knobCell_.getHeight() / 2), juce::Justification::bottomLeft);
    theme::drawReadout(g, juce::String(juce::roundToInt(evenKnob_.getValue() * 100)) + "%",
                       knobCell_.withTrimmedLeft(52).withTrimmedTop(knobCell_.getHeight() / 2 + 2).withWidth(64).withHeight(20), 12.0f);

    // graph: key-coloured columns, dB grid, the trims as bars from 0, how loud each bowl sounds as a line
    theme::drawWell(g, graph_.expanded(6, 6));
    const float w = graph_.getWidth() / static_cast<float>(kHigh - kLow + 1);
    for (int m = kLow; m <= kHigh; ++m)
        if (isBlack(m)) {
            g.setColour(juce::Colours::black.withAlpha(0.28f));
            g.fillRect(xOf(m) - w / 2, graph_.getY(), w, graph_.getHeight());
        }
    g.setFont(theme::mono(10.0f));
    for (float db = kGraphBottomDb; db <= kGraphTopDb + 0.1f; db += 6.0f) {
        const float y = yOf(db);
        g.setColour(db == 0.0f ? theme::inkMute.withAlpha(0.7f) : juce::Colours::white.withAlpha(0.08f));
        g.drawHorizontalLine(juce::roundToInt(y), graph_.getX(), graph_.getRight());
        g.setColour(theme::inkFaint);
        g.drawText(db == 0.0f ? juce::String("0") : (db > 0 ? "+" : "") + juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>(graph_.getX() - 40, y - 7, 32, 14), juce::Justification::centredRight);
    }
    for (int m = kLow; m <= kHigh; ++m) {
        const float t = proc_.bowlTrimDb(m), x = xOf(m);
        if (t != 0.0f) {
            const float y0 = yOf(0), y1 = yOf(t);
            g.setColour(m == hover_ ? theme::amberPale : theme::amber.withAlpha(0.85f));
            g.fillRect(x - w * 0.32f, std::min(y0, y1), w * 0.64f, std::max(2.0f, std::abs(y1 - y0)));
        } else {
            g.setColour(theme::amber.withAlpha(m == hover_ ? 0.9f : 0.35f));
            g.fillRect(x - w * 0.32f, yOf(0) - 1.0f, w * 0.64f, 2.0f);
        }
    }
    juce::Path plays;
    for (int m = kLow; m <= kHigh; ++m) {
        const auto pt = juce::Point<float>(xOf(m), yOf(proc_.bowlGainDb(m) + glassLevelReC4Db(m)));
        if (m == kLow) plays.startNewSubPath(pt);
        else plays.lineTo(pt);
    }
    g.setColour(juce::Colour(0xff8fc6d8).withAlpha(0.8f));  // glass blue: how loud each bowl sounds, re C4
    g.strokePath(plays, juce::PathStrokeType(1.5f));
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.1f));
    for (int m = kLow; m <= kHigh; m += 12)
        g.drawText(noteName(m), juce::Rectangle<float>(xOf(m) - 20, graph_.getBottom() + 8, 40, 14), juce::Justification::centred);

    // legend / readout
    auto foot = juce::Rectangle<float>(graph_.getX(), graph_.getBottom() + 28, graph_.getWidth(), 18);
    g.setFont(theme::sans(11.5f));
    if (hover_ >= 0) {
        g.setColour(theme::amberText);
        g.drawText(noteName(hover_) + juce::String::fromUTF8("   trim ") + dbString(proc_.bowlTrimDb(hover_)) +
                       juce::String::fromUTF8("   sounds ") + dbString(proc_.bowlGainDb(hover_) + glassLevelReC4Db(hover_)) + " re C4",
                   foot, juce::Justification::centredLeft);
    } else {
        g.setColour(theme::inkFaint);
        g.drawText(juce::String::fromUTF8("Drag to trim a bowl, sweep for several, double-click to clear. "
                                          "Line: loudness re C4."),
                   foot, juce::Justification::centredLeft);
    }
}

void BowlLevelsPanel::resized() {
    auto area = getLocalBounds().reduced(16, 14);
    auto top = area.removeFromTop(28);
    closeBtn_.setBounds(top.removeFromRight(84));
    area.removeFromTop(12);
    auto row = area.removeFromTop(48);
    knobCell_ = row.removeFromLeft(140).toFloat();
    evenKnob_.setBounds(knobCell_.withWidth(48).toNearestInt());
    resetBtn_.setBounds(row.removeFromRight(120).withSizeKeepingCentre(120, 30));
    area.removeFromTop(18);
    graph_ = area.withTrimmedLeft(40).withTrimmedRight(6).withTrimmedBottom(56).toFloat();
}

// ------------------------------------------------------------------ ModifyPage

ModifyPage::ModifyPage(MagnetikaProcessor& p, std::function<void()> onDone)
    : proc_(p), cutaway_(p),
      glassPlate_(p,
                  {{id::sustain, "SUSTAIN"}, {id::brake, "BRAKE"}, {id::coilsArmed, "ARMED"}, {id::trimOn, "TRIM"}, {id::n3ctl, "(3,0)"}, {id::revBSizes, "REV B"}},
                  {{"COILS", {{"g_G", "SUSTAIN"}, {"g_Gb", "BRAKE"}, {"g_Fmax", "FORCE"}, {"g_vset", "SETPOINT"}}},
                   {juce::CharPointer_UTF8("SPRING \xc2\xb7 SPIN"), {{id::vibDepth, "VIBRATO"}, {"g_vibRate", "RATE"}, {id::rotation, "ROTATION"}, {"g_kappa", "BRYAN"}}},
                   {juce::CharPointer_UTF8("FINGER \xc2\xb7 TAIL"), {{"g_N", "LOAD"}, {"g_dmu", "WETNESS"}, {"g_spin", "SPINDLE"}, {"g_damper", "DAMPER"}}}}),
      barsPlate_(p,
                 {{id::sustain, "SUSTAIN"}, {id::brake, "BRAKE"}, {id::coilsArmed, "ARMED"}, {id::massScaling, "MASS"}},
                 {{"COILS", {{"b_G", "SUSTAIN"}, {"b_Gb", "BRAKE"}, {"b_Fmax", "FORCE"}, {"b_vset", "SETPOINT"}}},
                  {"MALLET", {{"b_strikeF", "STRIKE"}, {"b_contact", "CONTACT"}, {"b_strikePos", "POSITION"}, {"b_ratio", "PARTIAL"}}},
                  {juce::CharPointer_UTF8("SPRING \xc2\xb7 TAIL"), {{id::vibDepth, "VIBRATO"}, {"b_vibRate", "RATE"}, {"b_Q1", "Q FUND."}, {"b_damper", "DAMPER"}}}}),
      advanced_(p), levels_(p),
      keyboard_(p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard), range_(keyboard_) {
    for (auto* b : {&glassBtn_, &barsBtn_}) {
        b->getProperties().set(theme::segmentProperty, true);
        addAndMakeVisible(b);
    }
    glassBtn_.onClick = [this] { setInstrument(0); };
    barsBtn_.onClick = [this] { setInstrument(1); };
    doneBtn_.getProperties().set(theme::segmentProperty, true);
    doneBtn_.setToggleState(true, juce::dontSendNotification);  // brass, like a selected segment
    doneBtn_.onClick = std::move(onDone);
    addAndMakeVisible(doneBtn_);
    serialDown_.onClick = [this] { stepSerial(-1); };
    serialUp_.onClick = [this] { stepSerial(+1); };
    serialDown_.setTooltip("Previous instrument: another pattern of build variation");
    serialUp_.setTooltip("Next instrument: another pattern of build variation");
    addAndMakeVisible(serialDown_);
    addAndMakeVisible(serialUp_);
    variationKnob_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    variationKnob_.setTitle("Build variation");
    variationKnob_.setTooltip("Build variation: how much this instrument's bowls and bars differ from the drawings "
                              "(tuning, Q, split, axis angle). Simulated, fixed per instrument number.");
    variationAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), id::variation, variationKnob_);
    variationKnob_.onValueChange = [this] { repaint(buildWell_.toNearestInt()); };
    addAndMakeVisible(variationKnob_);
    addAndMakeVisible(cutaway_);
    addAndMakeVisible(glassPlate_);
    addChildComponent(barsPlate_);
    glassPlate_.onAdvanced = barsPlate_.onAdvanced = [this] {
        levels_.setVisible(false);
        advanced_.setVisible(true);
    };
    addChildComponent(advanced_);
    advanced_.onClose = [this] { advanced_.setVisible(false); };
    glassPlate_.onLevels = [this] { showLevels(true); };
    addChildComponent(levels_);
    levels_.onClose = [this] { levels_.setVisible(false); };
    keyboard_.setAvailableRange(36, 96);
    keyboard_.setOctaveForMiddleC(4);
    addAndMakeVisible(keyboard_);
    range_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(range_);
    startTimerHz(15);
}

ModifyPage::~ModifyPage() { stopTimer(); }

int ModifyPage::instrument() const { return static_cast<int>(proc_.state().getRawParameterValue(id::instrument)->load()); }

void ModifyPage::setInstrument(int i) {
    if (auto* prm = proc_.state().getParameter(id::instrument)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(i)));
        prm->endChangeGesture();
    }
}

void ModifyPage::stepSerial(int delta) {
    if (auto* prm = proc_.state().getParameter(id::serialNo)) {
        const int now = static_cast<int>(proc_.state().getRawParameterValue(id::serialNo)->load());
        const int next = (now - 1 + delta + 999) % 999 + 1;  // 1..999, wrapping
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(next)));
        prm->endChangeGesture();
    }
    repaint(buildWell_.toNearestInt());
}

bool ModifyPage::revBHint() const {
    auto& st = proc_.state();
    return instrument() == 0 && st.getRawParameterValue(id::revBSizes)->load() > 0.5f &&
           st.getRawParameterValue(id::spindleOn)->load() > 0.5f && st.getRawParameterValue("g_spin")->load() > 0.6f;
}

void ModifyPage::paint(juce::Graphics& g) {
    // walnut ground
    auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b2919), 0, 0, juce::Colour(0xff1f150d), 0, r.getBottom(), false));
    g.fillRect(r);
    juce::Random rng(5);
    for (float x = 0; x < r.getWidth(); x += 3.0f + 6.0f * rng.nextFloat()) {
        g.setColour(rng.nextBool() ? juce::Colours::white.withAlpha(0.02f) : juce::Colours::black.withAlpha(0.05f));
        g.drawLine(x, 0, x + 26, r.getBottom(), 1.0f);
    }
    theme::drawNameplate(g, nameplate_);
    // material / bar stock and voices
    theme::drawWell(g, infoWell_);
    const bool glass = instrument() == 0;
    auto info = infoWell_.reduced(12, 0);
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.16f));
    g.drawText(glass ? "MATERIAL" : "BAR STOCK", info.removeFromLeft(80), juce::Justification::centredLeft);
    const int mat = juce::jlimit(0, static_cast<int>(materials().size()) - 1, static_cast<int>(proc_.state().state.getProperty(materialProp, 3)));
    theme::drawReadout(g, glass ? juce::String(materials()[static_cast<size_t>(mat)].name) : juce::String::fromUTF8("6061-T6 \xc2\xb7 38 \xc3\x97 12"),
                       info.removeFromLeft(196).reduced(0, 9), 12.5f);
    info.removeFromLeft(18);
    g.setColour(theme::inkFaint);
    g.drawText("VOICES", info.removeFromLeft(56), juce::Justification::centredLeft);
    theme::drawReadout(g, juce::String(d_.count) + " / 16", info.removeFromLeft(76).reduced(0, 9), 12.5f);

    // the instrument as built
    theme::drawWell(g, buildWell_);
    {
        auto b = buildWell_.reduced(10, 0);
        g.setColour(theme::inkFaint);
        g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.16f));
        g.drawText("No.", b.removeFromLeft(22), juce::Justification::centredLeft);
        b.removeFromLeft(24);  // the down button
        const int serial = static_cast<int>(proc_.state().getRawParameterValue(id::serialNo)->load());
        theme::drawReadout(g, juce::String(serial).paddedLeft('0', 3), b.removeFromLeft(44).reduced(0, 9), 12.5f);
        b.removeFromLeft(24 + 6);  // the up button, a gap
        b.removeFromLeft(36);      // the knob
        g.setColour(theme::amberText);
        g.setFont(theme::mono(11.0f));
        g.drawText("VAR " + juce::String(juce::roundToInt(variationKnob_.getValue() * 100)) + "%", b,
                   juce::Justification::centredLeft);
    }
    if (revBHint()) {  // rev B sizes: the big bowls' rims outrun this friction model above ~0.6 rev/s
        g.setColour(theme::amberText);
        g.setFont(theme::sans(11.5f, true).withExtraKerningFactor(0.06f));
        g.drawText(juce::String::fromUTF8("REV B SIZES: the low bowls speak only below \xe2\x89\x88 0.6 rev/s (spindle now ") +
                       juce::String(proc_.state().getRawParameterValue("g_spin")->load(), 2) + ")",
                   juce::Rectangle<float>(24, 56, 640, 18), juce::Justification::centredLeft);
    }

    // pedals
    const bool lit[] = {d_.sustain, d_.brake, glass ? proc_.state().getRawParameterValue(id::spindleOn)->load() > 0.5f : false};
    const char* names[] = {"SUSTAIN", "BRAKE", "SPINDLE"};
    for (int i = 0; i < (glass ? 3 : 2); ++i) {  // bars have no spindle pedal
        auto cell = juce::Rectangle<float>(pedals_.getX() + static_cast<float>(i) * 78, pedals_.getY(), 70, pedals_.getHeight());
        auto pedal = cell.withSizeKeepingCentre(58, 84).withY(cell.getY() + 4);
        if (lit[i]) pedal = pedal.withTrimmedTop(6);  // a pressed pedal sits lower
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(pedal.translated(0, 4), 9);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4b4f54), 0, pedal.getY(), juce::Colour(0xff151719), 0, pedal.getBottom(), false));
        g.fillRoundedRectangle(pedal, 9);
        g.setColour(juce::Colour(0xff5c6268));
        for (int j = 0; j < 3; ++j) g.fillRoundedRectangle(pedal.getCentreX() - 20, pedal.getBottom() - 14 - 7.0f * static_cast<float>(j), 40, 3, 1.5f);
        theme::drawLed(g, {cell.getX() + 4, cell.getBottom() - 16, 9, 9}, lit[i]);
        g.setColour(juce::Colour(0xffd9c6ab));
        g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.14f));
        g.drawText(names[i], juce::Rectangle<float>(cell.getX() + 16, cell.getBottom() - 20, 60, 16), juce::Justification::centredLeft);
    }
    // meters: vibrato, rotation, output L/R, limiter lamp
    auto meter = [&](float y, const juce::String& name, float left, float width, const juce::String& value) {
        g.setColour(juce::Colour(0xffd9c6ab));
        g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.12f));
        g.drawText(name, juce::Rectangle<float>(meters_.getX(), y, 70, 14), juce::Justification::centredLeft);
        auto bar = juce::Rectangle<float>(meters_.getX() + 74, y + 3, 90, 8);
        g.setColour(juce::Colour(0xff120c08));
        g.fillRoundedRectangle(bar, 4);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb9762a), bar.getX(), 0, theme::amberText, bar.getRight(), 0, false));
        g.fillRoundedRectangle(bar.getX() + bar.getWidth() * left, bar.getY(), std::max(2.0f, bar.getWidth() * width), bar.getHeight(), 4);
        g.setColour(theme::amberText);
        g.setFont(theme::mono(11.0f));
        g.drawText(value, juce::Rectangle<float>(bar.getRight() + 4, y, 60, 14), juce::Justification::centredRight);
    };
    const float y0 = meters_.getY() + 10;
    meter(y0, "VIBRATO", 0, d_.vibDepth / 30.0f, juce::String(d_.vibDepth, 1) + juce::String::fromUTF8(" \xc2\xa2"));
    meter(y0 + 22, "ROTATION", std::min(0.5f, 0.5f + d_.rotation / 2), std::abs(d_.rotation) / 2, (d_.rotation > 0 ? "+" : "") + juce::String(d_.rotation, 2));
    auto outFrac = [](float pk) { return pk <= 1e-5f ? 0.0f : juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(pk) + 48.0f) / 48.0f); };
    meter(y0 + 44, "OUT L", 0, outFrac(d_.peakL), dbText(d_.peakL));
    meter(y0 + 66, "OUT R", 0, outFrac(d_.peakR), dbText(d_.peakR));
    theme::drawLed(g, {meters_.getX(), y0 + 92, 9, 9}, d_.limiting);
    g.setColour(juce::Colour(0xffd9c6ab));
    g.setFont(theme::sans(11.0f).withExtraKerningFactor(0.12f));
    g.drawText("LIMITER", juce::Rectangle<float>(meters_.getX() + 14, y0 + 89, 70, 14), juce::Justification::centredLeft);
}

void ModifyPage::resized() {
    nameplate_ = {20, 10, 210, 44};
    glassBtn_.setBounds(246, 14, 70, 36);
    barsBtn_.setBounds(320, 14, 66, 36);
    infoWell_ = {640, 12, 432, 40};
    buildWell_ = {398, 12, 232, 40};
    serialDown_.setBounds(430, 18, 24, 28);
    serialUp_.setBounds(498, 18, 24, 28);
    variationKnob_.setBounds(528, 14, 36, 36);
    doneBtn_.setBounds(1088, 10, 92, 44);
    cutaway_.setBounds(24, 76, 640, 560);
    glassPlate_.setBounds(688, 76, 488, 560);
    barsPlate_.setBounds(688, 76, 488, 560);
    advanced_.setBounds(688, 76, 488, 560);
    levels_.setBounds(688, 76, 488, 560);
    pedals_ = {24, 654, 236, 126};
    range_.setBounds(280, 656, 640, 18);
    keyboard_.setBounds(280, 678, 640, 102);
    keyboard_.setKeyWidth(640.0f / 36.0f);
    meters_ = {940, 652, 236, 130};
}

void ModifyPage::timerCallback() {
    d_ = proc_.readDisplay();
    cutaway_.shownInstrument = instrument();
    const bool glass = instrument() == 0;
    glassBtn_.setToggleState(glass, juce::dontSendNotification);
    barsBtn_.setToggleState(!glass, juce::dontSendNotification);
    glassPlate_.setVisible(glass);
    barsPlate_.setVisible(!glass);
    if (const int high = glass ? kGlassHigh : kHigh; range_.high != high) {
        range_.high = high;
        range_.repaint();
    }
    if (!glass) levels_.setVisible(false);  // glass bowls only
    repaint(infoWell_.toNearestInt());
    repaint(buildWell_.toNearestInt());
    repaint(juce::Rectangle<int>(24, 56, 640, 18));  // the rev B hint
    repaint(pedals_.toNearestInt());
    repaint(meters_.toNearestInt());
}

} // namespace magnetika::plugin
