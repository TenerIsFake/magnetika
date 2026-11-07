#include "PlayPage.h"

#include "../Parameters.h"
#include "Theme.h"

namespace magnetika::plugin {

namespace {
constexpr int kLow = 48;
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
    const float w = a.getWidth() / static_cast<float>(high - kLow + 1);
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
    lampBox(r.removeFromRight(44), above && above(), noteName(high) + juce::String::fromUTF8(" \xe2\x96\xb6"));

    g.setFont(theme::mono(10.5f));
    const bool narrow = chipBounds(kLow).getWidth() < 17.0f;  // too narrow to name the sharps (glass, 49 chips)
    for (int m = kLow; m <= high; ++m) {
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
        if (!(narrow && isSharp(m))) g.drawText(noteName(m), c, juce::Justification::centred);
    }
}

void NoteStrip::mouseDown(const juce::MouseEvent& e) {
    for (int m = kLow; m <= high; ++m) {
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

void NoteStrip::releaseLatches() {
    for (int m : latched_) proc_.pushUiEvent({MagnetikaProcessor::UiEvent::NoteOff, m, 0});
    clearLatches();
}

// ------------------------------------------------------------------ PlayPage

namespace {
const juce::Identifier barPresetProp{"barPreset"};
// Mallet setups for the bars: strike force (N), contact time (ms), damper tail (s).
struct BarPreset {
    const char* name;
    double strikeF, contact, damper;
};
const std::vector<BarPreset>& barPresets() {
    static const std::vector<BarPreset> p = {
        {"Medium mallet", 25, 2.0, 2.5},
        {"Soft mallet, long tail", 18, 4.5, 5.0},
        {"Hard mallet, bright", 45, 0.8, 2.0},
        {"Felt, coil-bowed feel", 8, 6.0, 6.0},
        {"Brake-short staccato", 30, 1.5, 0.4},
    };
    return p;
}
} // namespace

PlayPage::PlayPage(MagnetikaProcessor& p, std::function<void()> onModify)
    : proc_(p), glass_(p), bars_(p), strip_(p) {
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
    mpeBtn_.setClickingTogglesState(true);
    mpeBtn_.setTooltip("MPE: per-note pitch bend, pressure (finger load) and slide on member channels. "
                       "Switches on by itself when the controller sends an MPE configuration message.");
    addAndMakeVisible(mpeBtn_);
    mpeAtt_ = std::make_unique<BA>(st, id::mpe, mpeBtn_);
    addAndMakeVisible(speed_);
    speedAtt_ = std::make_unique<SA>(st, "g_spin", speed_);
    speed_.setTitle("Spindle speed");
    addChildComponent(damper_);
    damperAtt_ = std::make_unique<SA>(st, "b_damper", damper_);
    damper_.setTitle("Bar damper tail");
    modifyBtn_.onClick = std::move(onModify);
    addAndMakeVisible(modifyBtn_);

    for (auto* b : {&prevBtn_, &nextBtn_, &saveBtn_}) addAndMakeVisible(b);
    prevBtn_.setTitle("Previous preset");
    nextBtn_.setTitle("Next preset");
    prevBtn_.onClick = [this] { stepPreset(-1); };
    nextBtn_.onClick = [this] { stepPreset(+1); };
    saveBtn_.setTooltip("Save the whole setup as a user preset (Documents/Magnetika/Presets)");
    saveBtn_.onClick = [this] { savePreset(); };

    rotation_.setTitle("Pattern rotation rate");
    vibrato_.setTitle("Vibrato depth");
    addAndMakeVisible(rotation_);
    addAndMakeVisible(vibrato_);
    rotAtt_ = std::make_unique<SA>(st, id::rotation, rotation_);
    vibAtt_ = std::make_unique<SA>(st, id::vibDepth, vibrato_);

    addAndMakeVisible(glass_);
    addChildComponent(bars_);
    glass_.setMaterial(juce::jlimit(0, static_cast<int>(materials().size()) - 1, static_cast<int>(st.state.getProperty(materialProp, 3))));
    strip_.levelOf = [this](int m) { return instrument() == 0 ? glass_.levelOf(m) : bars_.levelOf(m); };
    strip_.below = [this] { return instrument() == 0 ? glass_.outsideBelow() : bars_.outsideBelow(); };
    strip_.above = [this] { return instrument() == 0 ? glass_.outsideAbove() : bars_.outsideAbove(); };
    addAndMakeVisible(strip_);
    allOffBtn_.onClick = [this] {
        glass_.releaseAllTouches();  // forget any finger or mallet still down
        bars_.releaseAllTouches();
        proc_.pushUiEvent({MagnetikaProcessor::UiEvent::AllOff, 0, 0});
        strip_.clearLatches();
    };
    addAndMakeVisible(allOffBtn_);

    // master level for the instrument shown (on top of the instrument views)
    for (auto* s : {&levelGlass_, &levelBars_}) {
        s->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s->setTitle("Level");
        addChildComponent(s);
    }
    levelGlassAtt_ = std::make_unique<SA>(st, "g_master", levelGlass_);
    levelBarsAtt_ = std::make_unique<SA>(st, "b_master", levelBars_);
    levelGlass_.setVisible(true);
    userNames_ = store_.names();
    startTimerHz(15);
}

PlayPage::~PlayPage() { stopTimer(); }

int PlayPage::instrument() const { return static_cast<int>(proc_.state().getRawParameterValue(id::instrument)->load()); }

void PlayPage::setParam(const char* pid, double v) {
    if (auto* prm = proc_.state().getParameter(pid)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(v)));
        prm->endChangeGesture();
    }
}

void PlayPage::setInstrument(int i) { setParam(id::instrument, i); }

namespace {
// A state property used as an index: clamp it, since a preset or host state from
// another version could hold anything.
int clampedProp(const juce::ValueTree& st, const juce::Identifier& prop, int def, int count) {
    return juce::jlimit(0, count - 1, static_cast<int>(st.getProperty(prop, def)));
}
} // namespace

// Steps through the factory entries (materials or mallet setups), then the user presets.
void PlayPage::stepPreset(int delta) {
    auto& st = proc_.state();
    userNames_ = store_.names();  // the folder may have changed
    if (userSel_ >= userNames_.size()) userSel_ = -1;
    const bool glass = instrument() == 0;
    const int factory = glass ? static_cast<int>(materials().size()) : static_cast<int>(barPresets().size());
    const int total = factory + userNames_.size();
    const int current = userSel_ >= 0 ? factory + userSel_
                                      : clampedProp(st.state, glass ? materialProp : barPresetProp, glass ? 3 : 0, factory);
    const int next = ((current + delta) % total + total) % total;
    if (next >= factory) {
        userSel_ = next - factory;
        if (!store_.load(userNames_[userSel_], st)) userSel_ = -1;
        glass_.setMaterial(clampedProp(st.state, materialProp, 3, static_cast<int>(materials().size())));
    } else {
        userSel_ = -1;
        applyFactory(next);
    }
    repaint();
}

void PlayPage::savePreset() {
    saveDialog_ = std::make_unique<juce::AlertWindow>("Save preset", "Name this setup. It is saved with every setting.", juce::MessageBoxIconType::NoIcon, this);
    saveDialog_->addTextEditor("name", "", "Preset name");
    saveDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    // The editor can be closed while the dialog is open; JUCE then runs this
    // callback after PlayPage is gone, so it must not touch a dead `this`.
    juce::Component::SafePointer<PlayPage> self(this);
    saveDialog_->enterModalState(true, juce::ModalCallbackFunction::create([self](int result) {
        if (self == nullptr) return;
        if (result == 1 && self->saveDialog_ != nullptr) {
            auto name = juce::File::createLegalFileName(self->saveDialog_->getTextEditorContents("name").trim());
            if (name.isEmpty()) name = "My setup";
            // never overwrite silently: "Warm", "Warm 2", "Warm 3", ...
            const auto existing = self->store_.names();
            juce::String unique = name;
            for (int n = 2; existing.contains(unique, true); ++n) unique = name + " " + juce::String(n);
            if (self->store_.save(unique, self->proc_.state())) {
                self->userNames_ = self->store_.names();
                self->userSel_ = self->userNames_.indexOf(unique, true);
            }
        }
        self->saveDialog_.reset();
        self->repaint();
    }), false);
}

void PlayPage::applyFactory(int idx) {
    auto& st = proc_.state();
    if (instrument() == 0) {
        idx = juce::jlimit(0, static_cast<int>(materials().size()) - 1, idx);
        st.state.setProperty(materialProp, idx, nullptr);
        const auto& m = materials()[static_cast<size_t>(idx)];
        setParam("g_Q2", m.Q2);
        setParam("g_Q3", m.Q3);
        setParam("g_split", m.split);
        glass_.setMaterial(idx);
    } else {
        idx = juce::jlimit(0, static_cast<int>(barPresets().size()) - 1, idx);
        st.state.setProperty(barPresetProp, idx, nullptr);
        const auto& b = barPresets()[static_cast<size_t>(idx)];
        setParam("b_strikeF", b.strikeF);
        setParam("b_contact", b.contact);
        setParam("b_damper", b.damper);
    }
    repaint();
}

void PlayPage::paint(juce::Graphics& g) {
    const bool glass = instrument() == 0;
    theme::drawBackdrop(g, getLocalBounds().toFloat());
    theme::drawNameplate(g, nameplate_);
    for (auto w : {speedWell_, presetWell_, rotWell_, vibWell_}) theme::drawWell(g, w);
    g.setFont(theme::mono(12.5f));
    g.setColour(theme::amberText);
    if (glass) g.drawText(juce::String(speed_.getValue(), 2) + " rev/s", speedText_, juce::Justification::centredLeft);
    else g.drawText(juce::String(damper_.getValue(), 1) + " s", damperText_, juce::Justification::centredLeft);
    const double rot = rotation_.getValue();
    if (glass) {
        g.setColour(theme::amberText);
        g.drawText((rot > 0 ? "+" : "") + juce::String(rot, 2) + " rev/s", rotText_, juce::Justification::centredRight);
    } else {
        g.setColour(theme::inkFaint);
        g.setFont(theme::sans(11.5f));
        g.drawText("glass only", rotText_, juce::Justification::centredRight);
        g.setFont(theme::mono(12.5f));
    }
    g.setColour(theme::amberText);
    g.drawText(juce::String(vibrato_.getValue(), 1) + juce::String::fromUTF8(" \xc2\xa2"), vibText_, juce::Justification::centredRight);

    // preset readout: a factory entry, or a user preset after them
    const auto& users = userNames_;
    const int factory = glass ? static_cast<int>(materials().size()) : static_cast<int>(barPresets().size());
    const bool user = userSel_ >= 0 && userSel_ < users.size();
    const int idx = user ? factory + userSel_
                         : clampedProp(proc_.state().state, glass ? materialProp : barPresetProp, glass ? 3 : 0, factory);
    const juce::String name = user ? users[userSel_]
                                   : glass ? juce::String(materials()[static_cast<size_t>(idx)].name)
                                           : juce::String(barPresets()[static_cast<size_t>(idx)].name);
    auto pr = presetText_;
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(10.5f).withExtraKerningFactor(0.16f));
    g.drawText(juce::String::fromUTF8(user ? "USER PRESET  \xc2\xb7  " : glass ? "MATERIAL  \xc2\xb7  " : "MALLET  \xc2\xb7  ") +
                   juce::String(idx + 1) + " / " + juce::String(factory + users.size()),
               pr.removeFromTop(pr.getHeight() * 0.45f), juce::Justification::bottomLeft);
    g.setColour(theme::amberText);
    g.setFont(theme::mono(13.0f));
    g.drawText(name, pr, juce::Justification::topLeft);

    // status cluster: level knob (a child), output meters, limiter and MIDI lamps, voices
    theme::drawWell(g, statusWell_);
    g.setColour(theme::inkFaint);
    g.setFont(theme::sans(10.5f).withExtraKerningFactor(0.14f));
    g.drawText("LEVEL", juce::Rectangle<float>(statusWell_.getX() + 6, statusWell_.getY() + 60, 56, 12), juce::Justification::centred);
    for (int ch = 0; ch < 2; ++ch) {
        auto bar = juce::Rectangle<float>(statusWell_.getX() + 74 + 14.0f * static_cast<float>(ch), statusWell_.getY() + 8, 8, 62);
        g.setColour(juce::Colour(0xff0b0806));
        g.fillRoundedRectangle(bar, 3);
        const float pk = ch == 0 ? meterL_ : meterR_;
        const float frac = pk <= 1e-5f ? 0.0f : juce::jlimit(0.0f, 1.0f, (20.0f * std::log10(pk) + 48.0f) / 48.0f);
        auto fill = bar.withTrimmedTop(bar.getHeight() * (1.0f - frac));
        g.setGradientFill(juce::ColourGradient(theme::amberText, 0, bar.getY(), juce::Colour(0xff9a5f14), 0, bar.getBottom(), false));
        g.fillRoundedRectangle(fill, 3);
        g.setColour(theme::inkFaint);
        g.drawText(ch == 0 ? "L" : "R", juce::Rectangle<float>(bar.getX() - 2, bar.getBottom() + 1, 12, 12), juce::Justification::centred);
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    auto lampRow = statusWell_.withTrimmedTop(84).withHeight(14).translated(8, 0);
    theme::drawLed(g, lampRow.removeFromLeft(10).withSizeKeepingCentre(8, 8), d_.limiting);
    g.setColour(theme::inkFaint);
    g.drawText("LIMIT", lampRow.removeFromLeft(44).translated(4, 0), juce::Justification::centredLeft);
    theme::drawLed(g, lampRow.removeFromLeft(10).withSizeKeepingCentre(8, 8), now < midiLampUntil_);
    g.setColour(theme::inkFaint);
    g.drawText("MIDI", lampRow.removeFromLeft(40).translated(4, 0), juce::Justification::centredLeft);
    g.setColour(theme::amberText);
    g.setFont(theme::mono(11.0f));
    g.drawText("VOICES " + juce::String(d_.count) + "/16", statusWell_.withTrimmedTop(100).withHeight(14), juce::Justification::centred);

    g.setFont(theme::sans(12.5f, true).withExtraKerningFactor(0.14f));
    g.setColour(glass ? theme::buttonText : theme::buttonText.withAlpha(0.35f));
    g.drawText("ROTATION", rotWell_.withWidth(90).translated(12, 0), juce::Justification::centredLeft);
    g.setColour(theme::buttonText);
    g.drawText("VIBRATO", vibWell_.withWidth(80).translated(12, 0), juce::Justification::centredLeft);
    if (!glass) g.drawText("DAMPER", speedWell_.withWidth(90).translated(14, 0), juce::Justification::centredLeft);
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
    damper_.setBounds(880, 12, 126, 36);
    damperText_ = {1010, 8, 66, 44};
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
    bars_.setBounds(0, 106, 1200, 624);
    strip_.setBounds(20, 736, 1060, 52);
    allOffBtn_.setBounds(1092, 740, 88, 44);

    statusWell_ = {1068, 116, 112, 152};
    mpeBtn_.setBounds(1076, 236, 96, 26);
    levelGlass_.setBounds(1074, 120, 56, 56);
    levelBars_.setBounds(1074, 120, 56, 56);
    levelGlass_.toFront(false);
    levelBars_.toFront(false);
}

void PlayPage::timerCallback() {
    const auto d = proc_.readDisplay();
    d_ = d;
    // meters: fast attack, slow release; MIDI lamp: 150 ms per burst of messages
    meterL_ = std::max(d.peakL, meterL_ * 0.8f);
    meterR_ = std::max(d.peakR, meterR_ * 0.8f);
    if (d.midiCount != lastMidiCount_) {
        lastMidiCount_ = d.midiCount;
        midiLampUntil_ = juce::Time::getMillisecondCounterHiRes() + 150.0;
    }
    // latched notes belong to the instrument they were latched on, so release them on a switch
    // (buttons, automation, or a MIDI program change, which moves the parameter too)
    const int shown = instrument();
    if (shown != shownInstrument_) {
        if (shownInstrument_ >= 0) strip_.releaseLatches();
        shownInstrument_ = shown;
    }
    levelGlass_.setVisible(instrument() == 0);
    levelBars_.setVisible(instrument() != 0);
    repaint(statusWell_.toNearestInt());
    // lamps follow the live pedal state, so a MIDI sustain pedal lights SUSTAIN too
    sustainBtn_.getProperties().set("mgLamp", d.sustain);
    if (static_cast<bool>(mpeBtn_.getProperties()["mgLamp"]) != d.mpe || !mpeBtn_.getProperties().contains("mgLamp")) {
        mpeBtn_.getProperties().set("mgLamp", d.mpe);  // live state: a configuration message lights it at once
        mpeBtn_.repaint();
    }
    brakeBtn_.getProperties().set("mgLamp", d.brake);
    sustainBtn_.repaint();
    brakeBtn_.repaint();
    const bool glass = instrument() == 0;
    glassBtn_.setToggleState(glass, juce::dontSendNotification);
    barsBtn_.setToggleState(!glass, juce::dontSendNotification);
    if (glass_.isVisible() != glass) {
        glass_.setVisible(glass);
        bars_.setVisible(!glass);
        spindleBtn_.setVisible(glass);
        speed_.setVisible(glass);
        damper_.setVisible(!glass);
        rotation_.setEnabled(glass);
        rotation_.setAlpha(glass ? 1.0f : 0.35f);
        repaint();
    }
    if (glass)
        strip_.hint = !spindleBtn_.getToggleState() ? "Spindle stopped: rub along a rim to sound it."
                      : proc_.state().getRawParameterValue(id::revBSizes)->load() > 0.5f &&
                                proc_.state().getRawParameterValue("g_spin")->load() > 0.6f
                          ? juce::String::fromUTF8("Rev B sizes: the low bowls speak only below \xe2\x89\x88 0.6 rev/s.")
                          : "Press a rim to sound it. Drag across rims for a glissando.";
    else
        strip_.hint = bars_.lastStrike().isNotEmpty() ? bars_.lastStrike()
                                                      : "Tap a bar to strike it. Drag across bars for a roll; hold to keep the damper off.";
    strip_.high = glass ? GlassView::kHigh : BarsView::kHigh;
    strip_.repaint();
    repaint(speedWell_.toNearestInt());
    repaint(rotWell_.toNearestInt());
    repaint(vibWell_.toNearestInt());
    repaint(presetWell_.toNearestInt());
}

} // namespace magnetika::plugin
