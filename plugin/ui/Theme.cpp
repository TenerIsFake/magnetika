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
