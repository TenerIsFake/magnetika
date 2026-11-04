#pragma once
// Look of the Magnetika editor: walnut, brass, aluminium, amber-on-black.
// Fonts are IBM Plex (SIL Open Font License, see ui/fonts/OFL.txt), embedded.

#include <juce_gui_basics/juce_gui_basics.h>

namespace magnetika::plugin::theme {

// palette
inline const juce::Colour walnutLight{0xff3a2a1d}, walnutMid{0xff22170f}, walnutDark{0xff120c07};
inline const juce::Colour brassHi{0xffe2c27c}, brassMid{0xffc09a52}, brassLo{0xff9b7737}, brassInk{0xff2b1d0c};
inline const juce::Colour amber{0xffe8a33d}, amberText{0xfff3b65e}, amberPale{0xfff6d39a};
inline const juce::Colour ink{0xffefe3d0}, inkMute{0xffb9a891}, inkFaint{0xffa99a86};
inline const juce::Colour wellBg{0xff170f09}, ledOff{0xff4a3418};
inline const juce::Colour buttonHi{0xff3d4146}, buttonLo{0xff222528}, buttonText{0xffe9e2d6};

juce::Font sans(float height, bool semibold = false, bool bold = false);
juce::Font mono(float height);

// An amber indicator lamp, lit or dark, centred in `r`.
void drawLed(juce::Graphics& g, juce::Rectangle<float> r, bool on);
// The recessed dark well that groups a control.
void drawWell(juce::Graphics& g, juce::Rectangle<float> r);
// The warm background vignette behind the instrument.
void drawBackdrop(juce::Graphics& g, juce::Rectangle<float> r);
// The brass nameplate.
void drawNameplate(juce::Graphics& g, juce::Rectangle<float> r);
// Brushed aluminium faceplate with corner screws.
void drawFaceplate(juce::Graphics& g, juce::Rectangle<float> r);
// Engraved label on aluminium (dark text with a light lower edge).
void drawEngraved(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float size);
// Amber-on-black readout window.
void drawReadout(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size = 12.5f);
// The recessed viewing window onto the instrument.
void drawViewport(juce::Graphics& g, juce::Rectangle<float> r);

// Push-buttons: dark bakelite with a lamp (toggle) or plain; the instrument
// switch draws as a brass segment when selected.
class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    // bakelite knob with a machined aluminium cap, white pointer, engraved ticks
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return sans(12.5f, true); }
};

// Property set on a TextButton to draw it as a brass "selected segment".
inline const juce::Identifier segmentProperty{"mgSegment"};

} // namespace magnetika::plugin::theme
