#include "Theme.h"

#include "BinaryData.h"

namespace magnetika::plugin::theme {

namespace {
juce::Typeface::Ptr load(const void* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size)); }
juce::Typeface::Ptr regular() {
    static auto t = load(MagnetikaBinary::IBMPlexSansCondensedRegular_ttf, MagnetikaBinary::IBMPlexSansCondensedRegular_ttfSize);
    return t;
}
juce::Typeface::Ptr semibold() {
    static auto t = load(MagnetikaBinary::IBMPlexSansCondensedSemiBold_ttf, MagnetikaBinary::IBMPlexSansCondensedSemiBold_ttfSize);
    return t;
}
juce::Typeface::Ptr boldFace() {
    static auto t = load(MagnetikaBinary::IBMPlexSansCondensedBold_ttf, MagnetikaBinary::IBMPlexSansCondensedBold_ttfSize);
    return t;
}
juce::Typeface::Ptr monoFace() {
    static auto t = load(MagnetikaBinary::IBMPlexMonoMedium_ttf, MagnetikaBinary::IBMPlexMonoMedium_ttfSize);
    return t;
}
} // namespace

juce::Font sans(float height, bool sb, bool b) {
    return juce::Font(juce::FontOptions(b ? boldFace() : sb ? semibold() : regular()).withHeight(height));
}
juce::Font mono(float height) { return juce::Font(juce::FontOptions(monoFace()).withHeight(height)); }

void drawLed(juce::Graphics& g, juce::Rectangle<float> r, bool on) {
    const auto c = r.getCentre();
    const float d = std::min(r.getWidth(), r.getHeight());
    auto dot = juce::Rectangle<float>(d, d).withCentre(c);
    if (on) {
        for (int i = 3; i >= 1; --i) {
            g.setColour(amber.withAlpha(0.12f * static_cast<float>(4 - i)));
            g.fillEllipse(dot.expanded(static_cast<float>(i) * 2.5f));
        }
        juce::ColourGradient grad(juce::Colour(0xfffff2c8), c.x - d * 0.15f, c.y - d * 0.2f, amber, c.x + d * 0.5f, c.y + d * 0.5f, true);
        g.setGradientFill(grad);
        g.fillEllipse(dot);
    } else {
        g.setColour(ledOff);
        g.fillEllipse(dot);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawEllipse(dot.reduced(0.5f), 1.0f);
    }
}

void drawWell(juce::Graphics& g, juce::Rectangle<float> r) {
    g.setColour(wellBg);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine(static_cast<int>(r.getBottom()), r.getX() + 6, r.getRight() - 6);
}

void drawBackdrop(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::ColourGradient grad(walnutLight, r.getCentreX(), r.getY() + r.getHeight() * 0.55f, walnutDark, r.getX(), r.getY(), true);
    grad.addColour(0.5, walnutMid);
    g.setGradientFill(grad);
    g.fillRect(r);
}

void drawNameplate(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::ColourGradient grad(brassHi, r.getX(), r.getY(), brassLo, r.getX(), r.getBottom(), false);
    grad.addColour(0.45, brassMid);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 3, r.getRight() - 3);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    auto t = r.reduced(14, 6);
    g.setColour(juce::Colour(0x99fff0c8));
    g.setFont(sans(20.0f, false, true).withExtraKerningFactor(0.3f));
    g.drawText("MAGNETIKA", t.removeFromTop(t.getHeight() * 0.62f).translated(0, 1), juce::Justification::centredLeft);
    g.setColour(brassInk);
    auto t2 = r.reduced(14, 6);
    g.drawText("MAGNETIKA", t2.removeFromTop(t2.getHeight() * 0.62f), juce::Justification::centredLeft);
    g.setFont(sans(10.0f).withExtraKerningFactor(0.16f));
    g.setColour(juce::Colour(0xff3a2a12));
    g.drawText(juce::String::fromUTF8("ROTATING-MODE GLASS \xc2\xb7 EDDY BARS"), t2, juce::Justification::centredLeft);
}

void drawFaceplate(juce::Graphics& g, juce::Rectangle<float> r) {
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(r.translated(0, 6).expanded(2), 12.0f);
    juce::ColourGradient al(juce::Colour(0xffdfe2e5), r.getX(), r.getY(), juce::Colour(0xffa9aeb3), r.getX(), r.getBottom(), false);
    al.addColour(0.5, juce::Colour(0xffc3c7cb));
    g.setGradientFill(al);
    g.fillRoundedRectangle(r, 10.0f);
    {   // brushing: fine vertical streaks
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, 10.0f);
        g.reduceClipRegion(clip);
        juce::Random rng(11);
        for (float x = r.getX(); x < r.getRight(); x += 1.5f) {
            g.setColour(rng.nextBool() ? juce::Colours::white.withAlpha(0.05f + 0.04f * rng.nextFloat())
                                       : juce::Colours::black.withAlpha(0.025f + 0.02f * rng.nextFloat()));
            g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        }
    }
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.drawHorizontalLine(juce::roundToInt(r.getY()) + 1, r.getX() + 8, r.getRight() - 8);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    juce::Random rng(3);
    for (auto c : {r.getTopLeft().translated(14, 14), r.getTopRight().translated(-14, 14), r.getBottomLeft().translated(14, -14),
                   r.getBottomRight().translated(-14, -14)}) {
        auto s = juce::Rectangle<float>(11, 11).withCentre(c);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff2f3f4), s.getX() + 3, s.getY() + 3, juce::Colour(0xff8a9095), s.getRight(), s.getBottom(), true));
        g.fillEllipse(s);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawEllipse(s, 1.0f);
        const float a = rng.nextFloat() * juce::MathConstants<float>::pi;
        g.setColour(juce::Colour(0xff555c62));
        g.drawLine(c.x - 4.5f * std::cos(a), c.y - 4.5f * std::sin(a), c.x + 4.5f * std::cos(a), c.y + 4.5f * std::sin(a), 2.0f);
    }
}

void drawEngraved(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float size) {
    g.setFont(sans(size, true, true).withExtraKerningFactor(0.18f));
    g.setColour(juce::Colours::white.withAlpha(0.65f));
    g.drawText(text, r.translated(0, 1), j);
    g.setColour(juce::Colour(0xff2a2e32));
    g.drawText(text, r, j);
}

void drawReadout(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size) {
    g.setColour(juce::Colour(0xff121719));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
    g.setColour(amberText);
    g.setFont(mono(size));
    g.drawText(text, r, juce::Justification::centred);
}

void drawViewport(juce::Graphics& g, juce::Rectangle<float> r) {
    juce::ColourGradient vg(juce::Colour(0xff142226), r.getCentreX(), r.getY() + r.getHeight() * 0.38f, juce::Colour(0xff05090a),
                            r.getX(), r.getBottom(), true);
    vg.addColour(0.55, juce::Colour(0xff0b1316));
    g.setGradientFill(vg);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.5f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine(juce::roundToInt(r.getBottom()), r.getX() + 10, r.getRight() - 10);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) {
    const auto box = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
    const float d = std::min(box.getWidth(), box.getHeight());
    const auto c = box.getCentre();
    // engraved ticks
    g.setColour(juce::Colour(0xff3b4045));
    for (int i = 0; i <= 10; ++i) {
        const float a = start + (end - start) * static_cast<float>(i) / 10.0f;
        const float r0 = d * 0.43f, r1 = d * 0.5f;
        g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), 1.6f);
    }
    // bakelite skirt
    auto skirt = juce::Rectangle<float>(d * 0.68f, d * 0.68f).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(skirt.translated(0, d * 0.06f).expanded(1));
    juce::ColourGradient sk(juce::Colour(0xff575b61), skirt.getX() + skirt.getWidth() * 0.38f, skirt.getY() + skirt.getHeight() * 0.3f,
                            juce::Colour(0xff0b0b0c), skirt.getRight(), skirt.getBottom(), true);
    sk.addColour(0.6, juce::Colour(0xff1c1d1f));
    g.setGradientFill(sk);
    g.fillEllipse(skirt);
    // knurling on the skirt edge
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i < 48; ++i) {
        const float a = static_cast<float>(i) / 48.0f * juce::MathConstants<float>::twoPi;
        const float r0 = skirt.getWidth() * 0.44f, r1 = skirt.getWidth() * 0.5f;
        g.drawLine(c.x + r0 * std::cos(a), c.y + r0 * std::sin(a), c.x + r1 * std::cos(a), c.y + r1 * std::sin(a), 1.0f);
    }
    // machined aluminium cap: radial base plus spun-metal sheen wedges
    auto cap = juce::Rectangle<float>(d * 0.45f, d * 0.45f).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff4f5f6), cap.getX() + cap.getWidth() * 0.4f, cap.getY() + cap.getHeight() * 0.35f,
                                           juce::Colour(0xff8d9196), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse(cap);
    for (int i = 0; i < 4; ++i) {
        juce::Path wedge;
        const float a0 = 0.35f + static_cast<float>(i) * juce::MathConstants<float>::halfPi;
        wedge.addPieSegment(cap, a0, a0 + 0.55f, 0.0f);
        g.setColour(juce::Colours::white.withAlpha(i % 2 == 0 ? 0.35f : 0.15f));
        g.fillPath(wedge);
    }
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(cap, 1.0f);
    // pointer
    const float a = start + (end - start) * pos;
    juce::Path ptr;
    ptr.addRoundedRectangle(-1.6f, -skirt.getHeight() * 0.47f, 3.2f, skirt.getHeight() * 0.25f, 1.5f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillPath(ptr, juce::AffineTransform::rotation(a).translated(c.x + 0.6f, c.y + 0.8f));
    g.setColour(juce::Colour(0xfffbf6ec));
    g.fillPath(ptr, juce::AffineTransform::rotation(a).translated(c.x, c.y));
}

LookAndFeel::LookAndFeel() {
    setColour(juce::TextButton::textColourOffId, buttonText);
    setColour(juce::TextButton::textColourOnId, buttonText);
    setColour(juce::ToggleButton::textColourId, buttonText);
    setColour(juce::Slider::textBoxTextColourId, amberText);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool segment = b.getProperties()[segmentProperty];
    const bool lit = b.getToggleState() || down;
    if (segment && b.getToggleState()) {
        juce::ColourGradient grad(juce::Colour(0xfff0d9a7), r.getX(), r.getY(), juce::Colour(0xffd4ad62), r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(amber.withAlpha(0.25f));
        g.drawRoundedRectangle(r.expanded(1.5f), 6.0f, 2.0f);
        return;
    }
    if (segment) {
        juce::ColourGradient grad(juce::Colour(0xff3b2c20), r.getX(), r.getY(), juce::Colour(0xff2a1f16), r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, 5.0f);
        return;
    }
    // bakelite push-button: raised, or sunk when pressed / latched
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(r.translated(0, 2), 6.0f);
    juce::ColourGradient grad(lit ? juce::Colour(0xff1e2023) : buttonHi.brighter(over ? 0.08f : 0.0f), r.getX(), r.getY(),
                              lit ? juce::Colour(0xff2c2f33) : buttonLo, r.getX(), r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(lit ? 0.06f : 0.18f));
    g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 5, r.getRight() - 5);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const bool segment = b.getProperties()[segmentProperty];
    g.setFont(sans(12.5f, true).withExtraKerningFactor(0.14f));
    g.setColour(segment && b.getToggleState() ? brassInk : segment ? juce::Colour(0xffcdbba0) : buttonText);
    auto r = b.getLocalBounds();
    if (b.getClickingTogglesState() && !segment) {  // a lamp on the left
        // "mgLamp" lets the lamp follow live state (a MIDI pedal) rather than the button
        const auto& props = b.getProperties();
        const bool lamp = props.contains("mgLamp") ? static_cast<bool>(props["mgLamp"]) : b.getToggleState();
        if (props["mgStacked"]) {  // faceplate switch: a lamp bar on top, the label beneath
            auto bar = r.removeFromTop(r.getHeight() / 2).toFloat().withSizeKeepingCentre(22, 6).translated(0, 4);
            if (lamp) {
                g.setColour(amber.withAlpha(0.3f));
                g.fillRoundedRectangle(bar.expanded(4), 5);
                g.setColour(amber);
            } else {
                g.setColour(ledOff);
            }
            g.fillRoundedRectangle(bar, 3);
            g.setColour(buttonText);
            g.setFont(sans(11.5f, true).withExtraKerningFactor(0.1f));
            g.drawText(b.getButtonText(), r.translated(0, -2), juce::Justification::centred);
            return;
        }
        drawLed(g, r.removeFromLeft(30).toFloat().reduced(10.0f, 0).withSizeKeepingCentre(9, 9), lamp);
        r.removeFromRight(10);
        g.drawText(b.getButtonText(), r, juce::Justification::centredLeft);
        return;
    }
    g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool down) {
    drawButtonBackground(g, b, {}, over, down);
    auto r = b.getLocalBounds();
    drawLed(g, r.removeFromLeft(30).toFloat().reduced(10.0f, 0).withSizeKeepingCentre(9, 9), b.getToggleState());
    g.setFont(sans(12.5f, true).withExtraKerningFactor(0.14f));
    g.setColour(buttonText);
    g.drawText(b.getButtonText(), r, juce::Justification::centredLeft);
}

// A touch strip: a recessed groove with an amber fill from the centre (bipolar)
// or the left (unipolar), and a glowing puck.
void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   juce::Slider::SliderStyle, juce::Slider& s) {
    auto r = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h))
                 .withSizeKeepingCentre(static_cast<float>(w), 26.0f);
    juce::ColourGradient groove(juce::Colour(0xff090605), r.getX(), r.getY(), juce::Colour(0xff1a1109), r.getX(), r.getBottom(), false);
    g.setGradientFill(groove);
    g.fillRoundedRectangle(r, 13.0f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(r.reduced(0.5f), 13.0f, 1.0f);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? static_cast<float>(s.getPositionOfValue(0.0)) : r.getX() + 8.0f;
    if (bipolar) {
        g.setColour(juce::Colour(0xff5a4a36));
        g.fillRect(zero - 1.0f, r.getY() + 4.0f, 2.0f, r.getHeight() - 8.0f);
    }
    g.setColour(juce::Colour(0xff3a2d20));
    g.fillRect(r.getX() + 8.0f, r.getCentreY() - 1.0f, r.getWidth() - 16.0f, 2.0f);
    const float a = std::min(zero, pos), b = std::max(zero, pos);
    juce::ColourGradient fill(juce::Colour(0xffb9762a), a, 0, amber, b, 0, false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(a, r.getCentreY() - 3.0f, std::max(0.0f, b - a), 6.0f, 3.0f);
    auto puck = juce::Rectangle<float>(22.0f, 22.0f).withCentre({pos, r.getCentreY()});
    g.setColour(amber.withAlpha(0.3f));
    g.fillEllipse(puck.expanded(4.0f));
    juce::ColourGradient pg(juce::Colour(0xfffff2c8), puck.getX() + 7, puck.getY() + 6, juce::Colour(0xff9a5f14), puck.getRight(), puck.getBottom(), true);
    pg.addColour(0.55, amber);
    g.setGradientFill(pg);
    g.fillEllipse(puck);
}

} // namespace magnetika::plugin::theme
