#include "PlayPage.h"

#include "../Parameters.h"
#include "Theme.h"

namespace magnetika::plugin {

namespace {
constexpr int kLow = 48, kHigh = 84;
juce::String noteName(int m) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[m % 12]) + juce::String(m / 12 - 1);
}
bool isSharp(int m) { const int k = m % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
const juce::Identifier materialProp{"material"};
} // namespace

// ------------------------------------------------------------------ NoteStrip

juce::Rectangle<float> NoteStrip::chipArea() const {
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft(140 + 44);
    r.removeFromRight(44);
    return r;
}

juce::Rectangle<float> NoteStrip::chipBounds(int midi) const {
    const auto a = chipArea();
    const float w = a.getWidth() / static_cast<float>(kHigh - kLow + 1);
    return {a.getX() + w * static_cast<float>(midi - kLow) + 1.5f, a.getY() + 2, w - 3, a.getHeight() - 4};
}

void NoteStrip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(theme::inkMute);
    g.setFont(theme::sans(11.5f));
    g.drawFittedText(hint, r.removeFromLeft(136).toNearestInt(), juce::Justification::centredLeft, 3, 1.0f);
    auto lampBox = [&](juce::Rectangle<float> box, bool on, const juce::String& label) {
        theme::drawLed(g, box.removeFromTop(box.getHeight() * 0.55f).withSizeKeepingCentre(9, 9), on);
        g.setColour(theme::inkFaint);
        g.setFont(theme::sans(10.5f));
        g.drawText(label, box, juce::Justification::centredTop);
    };
    lampBox(r.removeFromLeft(44), below && below(), juce::String::fromUTF8("\xe2\x97\x80 C3"));
    lampBox(r.removeFromRight(44), above && above(), juce::String::fromUTF8("C6 \xe2\x96\xb6"));

    g.setFont(theme::mono(10.5f));
    for (int m = kLow; m <= kHigh; ++m) {
        const auto c = chipBounds(m);
        const float L = levelOf ? levelOf(m) : 0.0f;
        if (L > 0.04f) {
            g.setColour(theme::amber.withAlpha(0.3f * L));
            g.fillRoundedRectangle(c.expanded(2 + 5 * L), 5);
            g.setGradientFill(juce::ColourGradient(theme::amberPale, c.getX(), c.getY(), theme::amber, c.getX(), c.getBottom(), false));
            g.fillRoundedRectangle(c, 3);
            g.setColour(theme::brassInk);
        } else if (isSharp(m)) {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2d2621), c.getX(), c.getY(), juce::Colour(0xff1a1511), c.getX(), c.getBottom(), false));
            g.fillRoundedRectangle(c, 3);
            g.setColour(juce::Colour(0xffd9c6ab));
        } else {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff2ebdf), c.getX(), c.getY(), juce::Colour(0xffd8ccb8), c.getX(), c.getBottom(), false));
            g.fillRoundedRectangle(c, 3);
            g.setColour(juce::Colour(0xff3a2f26));
        }
        if (latched_.count(m)) {
            g.drawRoundedRectangle(c.reduced(1), 3, 1.5f);
        }
        g.drawText(noteName(m), c, juce::Justification::centred);
    }
}

void NoteStrip::mouseDown(const juce::MouseEvent& e) {
    for (int m = kLow; m <= kHigh; ++m) {
        if (!chipBounds(m).contains(e.position)) continue;
        using T = MagnetikaProcessor::UiEvent;
        if (latched_.erase(m)) proc_.pushUiEvent({T::NoteOff, m, 0});
        else {
            latched_.insert(m);
            proc_.pushUiEvent({T::NoteOn, m, 0.85f});
        }
        repaint();
        return;
    }
}

void NoteStrip::clearLatches() { latched_.clear(); repaint(); }

// ------------------------------------------------------------------ PlayPage

PlayPage::PlayPage(MagnetikaProcessor& p, std::function<void()> onModify) : proc_(p), glass_(p), strip_(p) {
    auto& st = proc_.state();
    for (auto* b : {&glassBtn_, &barsBtn_}) {
        b->getProperties().set(theme::segmentProperty, true);
        addAndMakeVisible(b);
    }
    glassBtn_.onClick = [this] { setInstrument(0); };
    barsBtn_.onClick = [this] { setInstrument(1); };

    for (auto* b : {&sustainBtn_, &brakeBtn_, &spindleBtn_}) {
        b->setClickingTogglesState(true);
        addAndMakeVisible(b);
    }
    sustainAtt_ = std::make_unique<BA>(st, id::sustain, sustainBtn_);
    brakeAtt_ = std::make_unique<BA>(st, id::brake, brakeBtn_);
    spindleAtt_ = std::make_unique<BA>(st, id::spindleOn, spindleBtn_);
    addAndMakeVisible(speed_);
    speedAtt_ = std::make_unique<SA>(st, "g_spin", speed_);
    speed_.setTitle("Spindle speed");
    modifyBtn_.onClick = std::move(onModify);
    addAndMakeVisible(modifyBtn_);

    for (auto* b : {&prevBtn_, &nextBtn_, &saveBtn_}) addAndMakeVisible(b);
    prevBtn_.setTitle("Previous material");
    nextBtn_.setTitle("Next material");
    prevBtn_.onClick = [this] { stepMaterial(-1); };
    nextBtn_.onClick = [this] { stepMaterial(+1); };
    saveBtn_.setEnabled(false);
    saveBtn_.setTooltip("Saving user presets arrives with the preset browser");

    rotation_.setTitle("Pattern rotation rate");
    vibrato_.setTitle("Vibrato depth");
    addAndMakeVisible(rotation_);
    addAndMakeVisible(vibrato_);
    rotAtt_ = std::make_unique<SA>(st, id::rotation, rotation_);
    vibAtt_ = std::make_unique<SA>(st, id::vibDepth, vibrato_);

    addAndMakeVisible(glass_);
    glass_.setMaterial(static_cast<int>(st.state.getProperty(materialProp, 3)));
    strip_.levelOf = [this](int m) { return glass_.levelOf(m); };
    strip_.below = [this] { return glass_.outsideBelow(); };
    strip_.above = [this] { return glass_.outsideAbove(); };
    addAndMakeVisible(strip_);
    allOffBtn_.onClick = [this] {
        proc_.pushUiEvent({MagnetikaProcessor::UiEvent::AllOff, 0, 0});
        strip_.clearLatches();
    };
    addAndMakeVisible(allOffBtn_);
    startTimerHz(15);
}

PlayPage::~PlayPage() { stopTimer(); }

int PlayPage::instrument() const { return static_cast<int>(proc_.state().getRawParameterValue(id::instrument)->load()); }

void PlayPage::setInstrument(int i) {
    if (auto* prm = proc_.state().getParameter(id::instrument)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(i)));
        prm->endChangeGesture();
    }
}

void PlayPage::stepMaterial(int delta) {
    auto& st = proc_.state();
    const int n = static_cast<int>(materials().size());
    const int idx = (static_cast<int>(st.state.getProperty(materialProp, 3)) + delta + n) % n;
    st.state.setProperty(materialProp, idx, nullptr);
    const auto& m = materials()[static_cast<size_t>(idx)];
    for (auto [pid, v] : {std::pair<const char*, double>{"g_Q2", m.Q2}, {"g_Q3", m.Q3}, {"g_split", m.split}}) {
        if (auto* prm = st.getParameter(pid)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(v)));
            prm->endChangeGesture();
        }
    }
    glass_.setMaterial(idx);
    repaint();
}

void PlayPage::paint(juce::Graphics& g) {
    theme::drawBackdrop(g, getLocalBounds().toFloat());
    theme::drawNameplate(g, nameplate_);
    for (auto w : {speedWell_, presetWell_, rotWell_, vibWell_}) theme::drawWell(g, w);
    g.setColour(theme::amberText);
    g.setFont(theme::mono(12.5f));
    g.drawText(juce::String(speed_.getValue(), 2) + " rev/s", speedText_, juce::Justification::centredLeft);
    const double rot = rotation_.getValue();
    g.drawText((rot > 0 ? "+" : "") + juce::String(rot, 2) + " rev/s", rotText_, juce::Justification::centredRight);
    g.drawText(juce::String(vibrato_.getValue(), 1) + juce::String::fromUTF8(" \xc2\xa2"), vibText_, juce::Justification::centredRight);
    // preset readout
    const int idx = static_cast<int>(proc_.state().state.getProperty(materialProp, 3));
    auto pr = presetText_;
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(10.5f).withExtraKerningFactor(0.16f));
    g.drawText(juce::String::fromUTF8("MATERIAL  \xc2\xb7  ") + juce::String(idx + 1) + " / " + juce::String(static_cast<int>(materials().size())),
               pr.removeFromTop(pr.getHeight() * 0.45f), juce::Justification::bottomLeft);
    g.setColour(theme::amberText);
    g.setFont(theme::mono(13.0f));
    g.drawText(materials()[static_cast<size_t>(idx)].name, pr, juce::Justification::topLeft);
    // well labels
    g.setColour(theme::buttonText);
    g.setFont(theme::sans(12.5f, true).withExtraKerningFactor(0.14f));
    g.drawText("ROTATION", rotWell_.withWidth(90).translated(12, 0), juce::Justification::centredLeft);
    g.drawText("VIBRATO", vibWell_.withWidth(80).translated(12, 0), juce::Justification::centredLeft);
}

void PlayPage::resized() {
    // design space 1200 x 800 (the editor scales this component)
    nameplate_ = {20, 8, 210, 44};
    glassBtn_.setBounds(246, 12, 70, 36);
    barsBtn_.setBounds(320, 12, 66, 36);
    sustainBtn_.setBounds(560, 8, 112, 44);
    brakeBtn_.setBounds(680, 8, 100, 44);
    speedWell_ = {788, 8, 290, 44};
    spindleBtn_.setBounds(792, 12, 108, 36);
    speed_.setBounds(906, 12, 100, 36);
    speedText_ = {1010, 8, 66, 44};
    modifyBtn_.setBounds(1088, 8, 92, 44);

    presetWell_ = {20, 63, 360, 40};
    prevBtn_.setBounds(26, 68, 30, 30);
    presetText_ = {64, 63, 230, 40};
    nextBtn_.setBounds(296, 68, 30, 30);
    saveBtn_.setBounds(330, 68, 46, 30);
    rotWell_ = {392, 63, 520, 40};
    rotation_.setBounds(490, 66, 320, 34);
    rotText_ = {812, 63, 92, 40};
    vibWell_ = {924, 63, 256, 40};
    vibrato_.setBounds(1004, 66, 120, 34);
    vibText_ = {1124, 63, 48, 40};

    glass_.setBounds(0, 106, 1200, 624);
    strip_.setBounds(20, 736, 1060, 52);
    allOffBtn_.setBounds(1092, 740, 88, 44);
}

void PlayPage::timerCallback() {
    const auto d = proc_.readDisplay();
    // lamps follow the live pedal state, so a MIDI sustain pedal lights SUSTAIN too
    sustainBtn_.getProperties().set("mgLamp", d.sustain);
    brakeBtn_.getProperties().set("mgLamp", d.brake);
    sustainBtn_.repaint();
    brakeBtn_.repaint();
    const int inst = instrument();
    glassBtn_.setToggleState(inst == 0, juce::dontSendNotification);
    barsBtn_.setToggleState(inst == 1, juce::dontSendNotification);
    glass_.setVisible(inst == 0);
    strip_.hint = !spindleBtn_.getToggleState() ? "Spindle stopped: rub along a rim to sound it."
                                                : "Press a rim to sound it. Drag across rims for a glissando.";
    strip_.repaint();
    repaint(speedWell_.toNearestInt());
    repaint(rotWell_.toNearestInt());
    repaint(vibWell_.toNearestInt());
}

} // namespace magnetika::plugin
