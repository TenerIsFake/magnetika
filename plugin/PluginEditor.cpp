#include "PluginEditor.h"

namespace magnetika::plugin {

namespace {
constexpr int kRangeLow = 48;   // C3: rev B, "extendable to 37 bowls by adding an octave below"
constexpr int kRangeHigh = 84;  // C6
constexpr int kRowHeight = 26;
constexpr int kColumns = 2;

juce::String sectionOf(const juce::String& pid) {
    if (pid.startsWith("g_")) return "Glass bowls";
    if (pid.startsWith("b_")) return "Bars";
    return "Performance";
}
} // namespace

void RangeStrip::paint(juce::Graphics& g) {
    const auto lo = kb_.getRectangleForKey(kRangeLow);
    const auto hi = kb_.getRectangleForKey(kRangeHigh);
    if (lo.isEmpty() || hi.isEmpty()) return;
    const float x0 = lo.getX() + static_cast<float>(kb_.getX() - getX());
    const float x1 = hi.getRight() + static_cast<float>(kb_.getX() - getX());
    auto bar = juce::Rectangle<float>(x0, 2.0f, x1 - x0, static_cast<float>(getHeight()) - 4.0f);
    g.setColour(juce::Colour(0xffb77a22).withAlpha(0.35f));
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(12.0f);
    g.drawText("instrument range C3-C6: the model is calibrated here, extrapolating outside",
               bar.toNearestInt(), juce::Justification::centred, true);
}

MagnetikaEditor::MagnetikaEditor(MagnetikaProcessor& p)
    : AudioProcessorEditor(&p), proc_(p),
      keyboard_(p.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard), range_(keyboard_) {
    title_.setText("Magnetika", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    materialLabel_.setText("Glass material", juce::dontSendNotification);
    addAndMakeVisible(materialLabel_);
    int idx = 1;
    for (const auto& m : materials()) material_.addItem(m.name, idx++);
    material_.setTextWhenNothingSelected("choose to set Q and split");
    material_.onChange = [this] { applyMaterial(material_.getSelectedItemIndex()); };
    addAndMakeVisible(material_);

    auto& st = proc_.state();
    for (auto* param : proc_.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (ranged == nullptr) continue;
        Row r;
        r.section = sectionOf(ranged->paramID);
        r.label = std::make_unique<juce::Label>();
        r.label->setText(ranged->getName(64), juce::dontSendNotification);
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(ranged)) {
            juce::ignoreUnused(b);
            auto t = std::make_unique<juce::ToggleButton>();
            r.button = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, ranged->paramID, *t);
            r.control = std::move(t);
        } else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(ranged)) {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList(c->choices, 1);
            r.combo = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(st, ranged->paramID, *box);
            r.control = std::move(box);
        } else {
            auto s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
            s->setTextValueSuffix(" " + ranged->getLabel());
            r.slider = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, ranged->paramID, *s);
            r.control = std::move(s);
        }
        content_.addAndMakeVisible(*r.label);
        content_.addAndMakeVisible(*r.control);
        rows_.push_back(std::move(r));
    }
    // group by section, keeping parameter order within a section
    std::stable_sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) {
        auto rank = [](const juce::String& s) { return s == "Performance" ? 0 : s == "Glass bowls" ? 1 : 2; };
        return rank(a.section) < rank(b.section);
    });
    for (const char* name : {"Performance", "Glass bowls", "Bars"}) {
        auto h = std::make_unique<juce::Label>();
        h->setText(name, juce::dontSendNotification);
        h->setFont(juce::FontOptions(16.0f, juce::Font::bold));
        h->setColour(juce::Label::textColourId, juce::Colour(0xffb77a22));
        content_.addAndMakeVisible(*h);
        headers_.push_back(std::move(h));
    }

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    keyboard_.setAvailableRange(36, 96);  // C2-C7
    keyboard_.setOctaveForMiddleC(4);     // MIDI 60 = C4, matching the range strip and rev B
    addAndMakeVisible(keyboard_);
    range_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(range_);

    setSize(980, 720);
}

void MagnetikaEditor::applyMaterial(int index) {
    if (index < 0 || index >= static_cast<int>(materials().size())) return;
    const auto& m = materials()[static_cast<size_t>(index)];
    auto set = [this](const char* pid, double v) {
        if (auto* prm = proc_.state().getParameter(pid)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(v)));
            prm->endChangeGesture();
        }
    };
    set("g_Q2", m.Q2);
    set("g_Q3", m.Q3);
    set("g_split", m.split);
}

void MagnetikaEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff18242a));
}

void MagnetikaEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(32);
    title_.setBounds(top.removeFromLeft(200));
    materialLabel_.setBounds(top.removeFromLeft(110));
    material_.setBounds(top.removeFromLeft(300).reduced(0, 3));

    auto kb = area.removeFromBottom(90);
    range_.setBounds(kb.removeFromTop(20));
    keyboard_.setBounds(kb);
    keyboard_.setKeyWidth(static_cast<float>(kb.getWidth()) / 36.0f);  // 36 white keys in C2-C7
    range_.repaint();

    area.removeFromBottom(6);
    viewport_.setBounds(area);

    // two columns of label + control, a header row before each section
    const int width = area.getWidth() - viewport_.getScrollBarThickness();
    const int colW = width / kColumns;
    int y = 0, col = 0;
    size_t header = 0;
    juce::String current;
    for (auto& r : rows_) {
        if (r.section != current) {
            if (col != 0) { y += kRowHeight; col = 0; }
            current = r.section;
            y += 8;
            if (header < headers_.size()) headers_[header++]->setBounds(0, y, width, kRowHeight);
            y += kRowHeight;
        }
        const int x = col * colW;
        r.label->setBounds(x, y, colW * 2 / 5, kRowHeight - 2);
        r.control->setBounds(x + colW * 2 / 5, y, colW * 3 / 5 - 10, kRowHeight - 2);
        if (++col == kColumns) { col = 0; y += kRowHeight; }
    }
    if (col != 0) y += kRowHeight;
    content_.setSize(width, y + 10);
}

} // namespace magnetika::plugin
