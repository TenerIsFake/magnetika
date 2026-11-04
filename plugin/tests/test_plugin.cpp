// Tests of the plugin layer: MIDI timing, block-size independence, controller
// mapping, state, bus layouts. The engine itself is covered by engine/tests and
// the reference comparison; pluginval covers host-facing conformance.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "../PluginEditor.h"
#include "../PluginProcessor.h"

using magnetika::plugin::MagnetikaProcessor;

namespace {

int failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::printf("FAIL %s:%d  %s\n  ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

constexpr double SR = 48000;

void setParam(MagnetikaProcessor& p, const char* pid, float value) {
    auto* prm = p.state().getParameter(pid);
    prm->setValueNotifyingHost(prm->convertTo0to1(value));
}

struct Event {
    int at;  // absolute sample
    juce::MidiMessage msg;
};

// Renders `total` samples in blocks of `block`, delivering each event inside the
// block that contains it, at its offset. Returns interleaved-free L and R.
struct Render {
    std::vector<float> L, R;
};
Render render(MagnetikaProcessor& p, int total, int block, const std::vector<Event>& events, int channels = 2) {
    p.setPlayConfigDetails(0, channels, SR, block);
    p.prepareToPlay(SR, block);
    Render out;
    out.L.assign(static_cast<size_t>(total), 0.0f);
    out.R.assign(static_cast<size_t>(total), 0.0f);
    juce::AudioBuffer<float> buf(channels, block);
    for (int start = 0; start < total; start += block) {
        const int n = std::min(block, total - start);
        buf.setSize(channels, n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.at >= start && e.at < start + n) midi.addEvent(e.msg, e.at - start);
        p.processBlock(buf, midi);
        for (int i = 0; i < n; ++i) {
            out.L[static_cast<size_t>(start + i)] = buf.getSample(0, i);
            out.R[static_cast<size_t>(start + i)] = buf.getSample(channels > 1 ? 1 : 0, i);
        }
    }
    return out;
}

double rmsDb(const std::vector<float>& x, double a, double b) {
    double s = 0;
    const size_t i0 = static_cast<size_t>(a * SR), i1 = static_cast<size_t>(b * SR);
    for (size_t i = i0; i < i1; ++i) s += static_cast<double>(x[i]) * x[i];
    return 10 * std::log10(s / static_cast<double>(i1 - i0) + 1e-30);
}

void testSampleAccurateNoteOn() {
    MagnetikaProcessor p;
    setParam(p, "instrument", 1);  // bars: the mallet sounds at once
    const int at = 300;
    auto r = render(p, 1024, 512, {{at, juce::MidiMessage::noteOn(1, 69, 0.9f)}});
    float before = 0, after = 0;
    for (int i = 0; i < at; ++i) before = std::max(before, std::fabs(r.L[static_cast<size_t>(i)]));
    for (int i = at; i < at + 64; ++i) after = std::max(after, std::fabs(r.L[static_cast<size_t>(i)]));
    CHECK(before == 0.0f, "sound before the note-on sample: %g", before);
    CHECK(after > 1e-4f, "no sound within 64 samples of the note-on: %g", after);
}

void testBlockSizeIndependence() {
    // Same events, different host block sizes: bit-identical output. Events sit
    // at awkward offsets so they fall mid-block for every block size.
    const std::vector<Event> ev = {
        {37, juce::MidiMessage::noteOn(1, 72, 0.8f)},
        {1001, juce::MidiMessage::controllerEvent(1, 64, 127)},
        {5003, juce::MidiMessage::noteOn(1, 67, 0.6f)},
        {20011, juce::MidiMessage::noteOff(1, 72)},
        {24007, juce::MidiMessage::pitchWheel(1, 12000)},
        {30001, juce::MidiMessage::controllerEvent(1, 74, 100)},
        {40009, juce::MidiMessage::controllerEvent(1, 64, 0)},
        {40009, juce::MidiMessage::noteOff(1, 67)},
    };
    const int total = 48000;
    MagnetikaProcessor a, b, c;
    const auto ra = render(a, total, 512, ev);
    const auto rb = render(b, total, 64, ev);
    const auto rc = render(c, total, 1000, ev);  // not a power of two
    CHECK(ra.L == rb.L && ra.R == rb.R, "block 512 vs 64 differ");
    CHECK(ra.L == rc.L && ra.R == rc.R, "block 512 vs 1000 differ");
}

void testSustainPedalViaMidi() {
    const int total = static_cast<int>(2.5 * SR);
    const std::vector<Event> plain = {{0, juce::MidiMessage::noteOn(1, 72, 0.8f)},
                                      {48000, juce::MidiMessage::noteOff(1, 72)}};
    auto pedal = plain;
    pedal.insert(pedal.begin(), {0, juce::MidiMessage::controllerEvent(1, 64, 127)});
    MagnetikaProcessor a, b;
    const auto free = render(a, total, 256, plain);
    const auto held = render(b, total, 256, pedal);
    // With the default damper (1.5 s), the free note is far down by 2.2 s; held is not.
    const double f = rmsDb(free.L, 2.2, 2.5), h = rmsDb(held.L, 2.2, 2.5);
    CHECK(h > f + 20, "CC64 sustain did not hold the note: held %.1f dB vs free %.1f dB", h, f);
}

void testCcNotOverwrittenByParameters() {
    // A CC sets the performance value; the (unmoved) parameter must not reset it
    // on the next block. CC1 = full vibrato must still be audible a second later.
    const int total = static_cast<int>(2.0 * SR);
    const std::vector<Event> base = {{0, juce::MidiMessage::controllerEvent(1, 64, 127)},
                                     {0, juce::MidiMessage::noteOn(1, 72, 0.8f)}};
    auto vib = base;
    vib.push_back({0, juce::MidiMessage::controllerEvent(1, 1, 127)});
    MagnetikaProcessor a, b;
    const auto plain = render(a, total, 128, base);
    const auto wobble = render(b, total, 128, vib);
    size_t diff = 0;
    for (size_t i = static_cast<size_t>(1.5 * SR); i < static_cast<size_t>(2.0 * SR); ++i)
        diff += plain.L[i] != wobble.L[i];
    CHECK(diff > 1000, "CC1 vibrato had no effect late in the note (%zu samples differ)", diff);
}

void testProgramChangeSelectsBars() {
    MagnetikaProcessor a;
    const auto r = render(a, 4800, 128,
                          {{0, juce::MidiMessage::programChange(1, 1)}, {0, juce::MidiMessage::noteOn(1, 69, 0.9f)}});
    // bars ring at once from the mallet; glass would still be building from the finger
    CHECK(rmsDb(r.L, 0.0, 0.05) > -50, "program 1 did not select the bars (level %.1f dB)", rmsDb(r.L, 0.0, 0.05));
}

void testStateRoundTrip() {
    MagnetikaProcessor a;
    setParam(a, "g_Q2", 12345.0f);
    setParam(a, "coilsArmed", 0.0f);
    setParam(a, "instrument", 1);
    juce::MemoryBlock mb;
    a.getStateInformation(mb);
    MagnetikaProcessor b;
    b.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    auto val = [](MagnetikaProcessor& p, const char* pid) { return p.state().getRawParameterValue(pid)->load(); };
    CHECK(std::fabs(val(b, "g_Q2") - val(a, "g_Q2")) < 1e-3f, "g_Q2 not restored: %f vs %f", val(b, "g_Q2"), val(a, "g_Q2"));
    CHECK(val(b, "coilsArmed") == 0.0f, "coilsArmed not restored");
    CHECK(val(b, "instrument") == 1.0f, "instrument not restored");
}

void testMonoBus() {
    MagnetikaProcessor p;
    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(p.setBusesLayout(mono), "mono output layout refused");
    const auto r = render(p, 9600, 300, {{0, juce::MidiMessage::programChange(1, 1)},
                                         {10, juce::MidiMessage::noteOn(1, 69, 0.9f)}}, 1);
    CHECK(rmsDb(r.L, 0.0, 0.2) > -60, "mono output silent");
}

void testProcessBeforePrepareIsSilent() {
    MagnetikaProcessor p;
    juce::AudioBuffer<float> buf(2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 256; ++i) buf.setSample(ch, i, 1.0f);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    p.processBlock(buf, midi);
    CHECK(buf.getMagnitude(0, 256) == 0.0f, "processBlock before prepareToPlay must output silence");
}

void testDefaults() {
    MagnetikaProcessor p;
    auto val = [&](const char* pid) { return p.state().getRawParameterValue(pid)->load(); };
    CHECK(val("coilsArmed") == 1.0f, "coils armed should default on");
    CHECK(val("massScaling") == 0.0f, "mass scaling should default off");
    CHECK(std::fabs(val("g_damper") - 1.5f) < 1e-6f, "glass damper default %.2f, want 1.5", val("g_damper"));
    CHECK(std::fabs(val("b_damper") - 2.5f) < 1e-6f, "bar damper default %.2f, want 2.5", val("b_damper"));
}

// ---------------------------------------------------------------- editor bridge
using UiEvent = MagnetikaProcessor::UiEvent;

void testUiNoteSoundsAndIsDisplayed() {
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    p.prepareToPlay(SR, 256);
    CHECK(p.pushUiEvent({UiEvent::NoteOn, 72, 0.8f}), "UI event queue refused a note");
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    float peak = 0;
    for (int b = 0; b < 100; ++b) {
        buf.clear();
        p.processBlock(buf, midi);
        peak = std::max(peak, buf.getMagnitude(0, 256));
    }
    const auto d = p.readDisplay();
    bool shown = false;
    for (int i = 0; i < d.count; ++i) shown = shown || (d.voices[static_cast<size_t>(i)].midi == 72 && d.voices[static_cast<size_t>(i)].level > 0);
    CHECK(peak > 1e-3f, "a touched bowl made no sound (peak %g)", peak);
    CHECK(shown, "the touched note is missing from the display snapshot (%d voices)", d.count);
}

void testDisplayShowsMidiPedal() {
    MagnetikaProcessor p;
    auto r = render(p, 2048, 256, {{0, juce::MidiMessage::controllerEvent(1, 64, 127)}});
    juce::ignoreUnused(r);
    const auto d = p.readDisplay();
    CHECK(d.sustain, "CC64 down is not reflected in the display (the SUSTAIN light would stay dark)");
    CHECK(d.midiCount >= 1, "MIDI activity counter did not move");
}

void testSpindleOffStillFingerFades() {
    // Spindle off: a touched bowl whose finger stays still must fade; one whose
    // finger keeps rubbing keeps sounding.
    auto run = [](bool rub) {
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        auto* prm = p.state().getParameter("spindleOn");
        prm->setValueNotifyingHost(0.0f);
        p.prepareToPlay(SR, 256);
        p.pushUiEvent({UiEvent::NoteOn, 72, 0.8f});
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        double late = 0;
        for (int b = 0; b < 470; ++b) {  // about 2.5 s
            if (b == 2) p.pushUiEvent({UiEvent::FingerSpeed, 72, rub ? 3.8f : 0.0f});
            buf.clear();
            p.processBlock(buf, midi);
            if (b >= 375) late += buf.getRMSLevel(0, 0, 256);
        }
        return late;
    };
    const double rubbing = run(true), still = run(false);
    CHECK(still < rubbing * 0.3, "still finger on a stopped spindle kept sounding (%.4g vs rubbing %.4g)", still, rubbing);
}

// A mouse event on `c` at `pos`, from the desktop's main pointer.
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> pos, bool dragged) {
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, pos, juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure,
                            0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, pos, now, 1, dragged);
}

std::set<int> soundingNotes(MagnetikaProcessor& p, int blocks) {
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        buf.clear();
        p.processBlock(buf, midi);
    }
    std::set<int> out;
    const auto d = p.readDisplay();
    for (int i = 0; i < d.count; ++i)
        if (d.voices[static_cast<size_t>(i)].held) out.insert(d.voices[static_cast<size_t>(i)].midi);
    return out;
}

void testGlassTouchAndGlissando() {
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    p.prepareToPlay(SR, 256);
    magnetika::plugin::GlassView view(p);
    view.setSize(1200, 624);
    // press the C5 rim: C5 is held
    const auto c5 = view.rimTopOf(72), e5 = view.rimTopOf(76);
    view.mouseDown(mouseAt(view, c5, false));
    auto held = soundingNotes(p, 20);
    CHECK(held.count(72) == 1 && held.size() == 1, "pressing the C5 rim should hold exactly C5 (held %zu notes)", held.size());
    // slide to the E5 rim: glissando, C5 released, E5 held
    view.mouseDrag(mouseAt(view, e5, true));
    held = soundingNotes(p, 20);
    CHECK(held.count(76) == 1 && held.count(72) == 0, "sliding to E5 should release C5 and hold E5");
    view.mouseUp(mouseAt(view, e5, true));
    held = soundingNotes(p, 5);
    CHECK(held.empty(), "lifting the finger should release every note (%zu still held)", held.size());
    // a press far from any rim plays nothing
    view.mouseDown(mouseAt(view, {5.0f, 5.0f}, false));
    held = soundingNotes(p, 5);
    CHECK(held.empty(), "a press away from the rims must not play a note");
}

void testBarsStrikeAndRoll() {
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    setParam(p, "instrument", 1.0f);
    p.prepareToPlay(SR, 256);
    magnetika::plugin::BarsView view(p);
    view.setSize(1200, 624);
    const auto a4 = view.barCentreOf(69), b4 = view.barCentreOf(71);
    view.mouseDown(mouseAt(view, a4, false));
    auto held = soundingNotes(p, 20);
    CHECK(held.count(69) == 1 && held.size() == 1, "striking the A4 bar should hold exactly A4 (held %zu)", held.size());
    CHECK(view.lastStrike().contains("A4 at 0.0"), "a strike at the bar centre should report position ~0: %s", view.lastStrike().toRawUTF8());
    view.mouseDrag(mouseAt(view, b4, true));
    held = soundingNotes(p, 20);
    CHECK(held.count(71) == 1 && held.count(69) == 0, "rolling onto B4 should strike B4 and release A4");
    view.mouseUp(mouseAt(view, b4, true));
    held = soundingNotes(p, 5);
    CHECK(held.empty(), "lifting the mallet should release every bar (%zu still held)", held.size());
}

} // namespace

// `test-plugin --snapshot out.png` paints the editor offscreen into a PNG, for
// looking at the UI without a desktop session.
int snapshot(const char* path, const juce::String& view) {
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    p.prepareToPlay(SR, 256);
    // play a sustained chord (C4 E4 G4 C5) for a second and a half, rotating the pattern
    setParam(p, "sustain", 1.0f);
    setParam(p, "rotation", 0.35f);
    if (view.contains("bars")) setParam(p, "instrument", 1.0f);
    for (int m : {60, 64, 67, 72}) p.pushUiEvent({UiEvent::NoteOn, m, 0.85f});
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 280; ++b) {
        buf.clear();
        p.processBlock(buf, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* me = dynamic_cast<magnetika::plugin::MagnetikaEditor*>(ed.get());
    ed->setSize(1200, 800);
    if (me) {
        if (view.contains("modify")) me->showModify(true);
        me->poll();
    }
    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(path);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream(img, os);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(),
                img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;  // message manager for parameter notifications
    if (argc >= 3 && juce::String(argv[1]) == "--snapshot") return snapshot(argv[2], argc >= 4 ? juce::String(argv[3]) : "glass");
    testDefaults();
    testSampleAccurateNoteOn();
    testBlockSizeIndependence();
    testSustainPedalViaMidi();
    testCcNotOverwrittenByParameters();
    testProgramChangeSelectsBars();
    testStateRoundTrip();
    testMonoBus();
    testProcessBeforePrepareIsSilent();
    testUiNoteSoundsAndIsDisplayed();
    testDisplayShowsMidiPedal();
    testSpindleOffStillFingerFades();
    testGlassTouchAndGlissando();
    testBarsStrikeAndRoll();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all plugin tests passed\n");
    return 0;
}
