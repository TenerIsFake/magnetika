// Tests of the plugin layer: MIDI timing, block-size independence, controller
// mapping, state, bus layouts. The engine itself is covered by engine/tests and
// the reference comparison; pluginval covers host-facing conformance.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // and the parameter follows, so the editor's buttons and the host see it
    a.syncProgramChange();
    CHECK(a.state().getRawParameterValue("instrument")->load() == 1.0f, "program change did not move the instrument parameter");
    // the GLASS button then works (it was dead while the parameter still said glass);
    // no re-prepare here, which would reset the engine from the parameters
    juce::AudioBuffer<float> buf(2, 128);
    juce::MidiBuffer none, on;
    a.pushUiEvent({MagnetikaProcessor::UiEvent::AllOff, 0, 0});  // silence the bar
    for (int b = 0; b < 4; ++b) { buf.clear(); a.processBlock(buf, none); }
    auto* prm = a.state().getParameter("instrument");
    prm->setValueNotifyingHost(prm->convertTo0to1(0.0f));
    on.clear();
    on.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);
    double sum = 0;
    for (int b = 0; b < 18; ++b) {  // about 0.05 s
        buf.clear();
        a.processBlock(buf, b == 0 ? on : none);
        sum += static_cast<double>(buf.getRMSLevel(0, 0, 128)) * buf.getRMSLevel(0, 0, 128);
    }
    const double glassDb = 10 * std::log10(sum / 18 + 1e-30);
    CHECK(glassDb < rmsDb(r.L, 0.0, 0.05) - 10, "GLASS after a program change to bars still played bars (%.1f dB)", glassDb);
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

void testFullUiQueueNeverStrandsANote() {
    // a burst of gestures fills the queue; the release that does not fit must still release
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    p.prepareToPlay(SR, 256);
    p.pushUiEvent({UiEvent::NoteOn, 72, 0.8f});
    auto held = soundingNotes(p, 2);
    CHECK(held.count(72) == 1, "C5 should be held before the burst");
    int pushed = 0;
    while (pushed < 100000 && p.pushUiEvent({UiEvent::Touch, 72, 0.9f})) ++pushed;
    CHECK(!p.pushUiEvent({UiEvent::NoteOff, 72, 0}), "the queue never filled (%d events)", pushed);
    held = soundingNotes(p, 2);
    CHECK(held.empty(), "a NoteOff refused by a full queue left %zu note(s) held", held.size());
}

} // namespace

namespace magnetika::plugin {
struct GlassViewTestAccess {
    static void press(GlassView& v, int midi) { v.press(midi, 1.0f); }
    static void release(GlassView& v, int midi) { v.release(midi); }
};
struct BarsViewTestAccess {
    static void strike(BarsView& v, int midi) { v.strike(midi, 0.8f, 0.0f); }
    static void lift(BarsView& v, int midi) { v.lift(midi); }
};
} // namespace magnetika::plugin

namespace {

void testTwoFingersOnOneNote() {
    using magnetika::plugin::GlassViewTestAccess;
    using magnetika::plugin::BarsViewTestAccess;
    {   // two fingers on one rim: lifting one keeps the note, lifting both releases it
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        p.prepareToPlay(SR, 256);
        magnetika::plugin::GlassView view(p);
        view.setSize(1200, 624);
        GlassViewTestAccess::press(view, 72);
        GlassViewTestAccess::press(view, 72);
        GlassViewTestAccess::release(view, 72);
        auto held = soundingNotes(p, 5);
        CHECK(held.count(72) == 1, "lifting one of two fingers released the rim");
        GlassViewTestAccess::release(view, 72);
        held = soundingNotes(p, 5);
        CHECK(held.empty(), "lifting both fingers should release the rim");
        // hidden mid-touch (an instrument switch) there is no mouseUp: the rim must not stay counted
        view.setVisible(true);
        GlassViewTestAccess::press(view, 72);
        view.mouseDown(mouseAt(view, view.rimTopOf(76), false));
        view.setVisible(false);
        held = soundingNotes(p, 5);
        CHECK(held.empty(), "hiding the view should release every finger (%zu still held)", held.size());
        GlassViewTestAccess::press(view, 72);
        held = soundingNotes(p, 5);
        CHECK(held.count(72) == 1, "a press after the view was hidden mid-touch made no sound");
        GlassViewTestAccess::release(view, 72);
    }
    {   // two mallets on one bar: each strike sounds, the damper returns after the last
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        setParam(p, "instrument", 1.0f);
        p.prepareToPlay(SR, 256);
        magnetika::plugin::BarsView view(p);
        view.setSize(1200, 624);
        BarsViewTestAccess::strike(view, 69);
        BarsViewTestAccess::strike(view, 69);
        BarsViewTestAccess::lift(view, 69);
        auto held = soundingNotes(p, 5);
        CHECK(held.count(69) == 1, "lifting one of two mallets dropped the damper on the bar");
        BarsViewTestAccess::lift(view, 69);
        held = soundingNotes(p, 5);
        CHECK(held.empty(), "lifting both mallets should release the bar");
    }
}

// ---------------------------------------------------------------- MPE

// Runs `blocks` blocks of 256, delivering `first` at the start of the first one.
std::vector<float> playBlocks(MagnetikaProcessor& p, int blocks, const std::vector<juce::MidiMessage>& first = {}) {
    juce::AudioBuffer<float> buf(2, 256);
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        juce::MidiBuffer midi;
        if (b == 0) for (const auto& m : first) midi.addEvent(m, 0);
        buf.clear();
        p.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 256);
    }
    return out;
}

MagnetikaProcessor* mpeProcessor(MagnetikaProcessor& p, bool bars = false) {
    p.setPlayConfigDetails(0, 2, SR, 256);
    setParam(p, "mpe", 1.0f);
    if (bars) setParam(p, "instrument", 1.0f);
    p.prepareToPlay(SR, 256);
    return &p;
}

float voiceLevel(MagnetikaProcessor& p, int midi, bool* held = nullptr) {
    const auto d = p.readDisplay();
    for (int i = 0; i < d.count; ++i) {
        const auto& v = d.voices[static_cast<size_t>(i)];
        if (v.midi == midi) {
            if (held) *held = v.held;
            return v.level;
        }
    }
    if (held) *held = false;
    return 0;
}

juce::MidiMessage pressure(int ch, int v) { return juce::MidiMessage::channelPressureChange(ch, v); }

void testMpePressureIsPerNote() {
    // C5 on channel 2 and G5 on channel 3, both pressed (so pressure has taken over
    // the load), then one eased to a whisper: only that bowl stops speaking
    auto run = [](int p2, int p3) {
        MagnetikaProcessor p;
        setParam(p, "g_coilStart", 0.0f);  // the finger's own physics: no coil-start ring
        mpeProcessor(p);
        playBlocks(p, 1, {juce::MidiMessage::noteOn(2, 72, 0.8f), juce::MidiMessage::noteOn(3, 79, 0.8f),
                          pressure(2, 127), pressure(3, 127)});
        playBlocks(p, 280, {pressure(2, p2), pressure(3, p3)});
        return std::pair<float, float>(voiceLevel(p, 72), voiceLevel(p, 79));
    };
    const auto a = run(10, 120), b = run(120, 10);
    auto db = [](float x) { return 20 * std::log10(x + 1e-9f); };
    CHECK(db(b.first) > db(a.first) + 3, "C5 pressure on its channel did not raise it: %.1f vs %.1f dB", db(b.first), db(a.first));
    CHECK(db(a.second) > db(b.second) + 3, "G5 pressure on its channel did not raise it: %.1f vs %.1f dB", db(a.second), db(b.second));
}

void testMpeBendIsPerNoteAndClamped() {
    auto run = [](int bendCh, int wheel, float bendRangeCents) {
        MagnetikaProcessor p;
        setParam(p, "mpeGlide", 0.0f);  // the spring alone (with glide, these bends move to other bowls)
        mpeProcessor(p);
        setParam(p, "bendRange", bendRangeCents);
        return playBlocks(p, 200, {juce::MidiMessage::pitchWheel(bendCh, wheel), juce::MidiMessage::noteOn(2, 72, 0.8f)});
    };
    const auto plain = run(3, 8192, 40);
    CHECK(run(3, 12000, 40) == plain, "a bend on another member channel moved the note on channel 2");
    CHECK(run(2, 8400, 40) != plain, "a bend on the note's own channel did nothing");
    // a member bend beyond what the coils can do is clamped to the pitch-bend range:
    // 0.49 and 0.9999 of a 48-semitone wheel both exceed 20 c, so both give exactly 20 c
    CHECK(run(2, 12200, 20) == run(2, 16383, 20), "member bend not clamped to the pitch-bend range");
}

void testMpeSharedNoteAndAllOff() {
    MagnetikaProcessor p;
    mpeProcessor(p);
    playBlocks(p, 4, {juce::MidiMessage::noteOn(2, 72, 0.8f), juce::MidiMessage::noteOn(3, 72, 0.8f)});
    playBlocks(p, 4, {juce::MidiMessage::noteOff(2, 72)});
    bool held = false;
    voiceLevel(p, 72, &held);
    CHECK(held, "releasing one of two channels on C5 released the bowl");
    playBlocks(p, 4, {juce::MidiMessage::noteOff(3, 72)});
    voiceLevel(p, 72, &held);
    CHECK(!held, "releasing both channels should release C5");
    // ALL OFF from the editor while a channel holds C5: the next press must sound again
    playBlocks(p, 4, {juce::MidiMessage::noteOn(4, 72, 0.8f)});
    p.pushUiEvent({UiEvent::AllOff, 0, 0});
    playBlocks(p, 4);
    playBlocks(p, 4, {juce::MidiMessage::noteOn(5, 72, 0.8f)});
    voiceLevel(p, 72, &held);
    CHECK(held, "a note after ALL OFF did not sound");
    playBlocks(p, 4, {juce::MidiMessage::noteOff(5, 72)});
    voiceLevel(p, 72, &held);
    CHECK(!held, "the note after ALL OFF did not release");
}

void testMpeConfigurationMessage() {
    MagnetikaProcessor p;
    p.setPlayConfigDetails(0, 2, SR, 256);
    p.prepareToPlay(SR, 256);
    CHECK(!p.readDisplay().mpe, "MPE should be off by default");
    using M = juce::MidiMessage;
    // MCM: RPN 6 on channel 1 = lower zone with 7 member channels (2..8)
    playBlocks(p, 1, {M::controllerEvent(1, 101, 0), M::controllerEvent(1, 100, 6), M::controllerEvent(1, 6, 7)});
    CHECK(p.readDisplay().mpe, "an MPE Configuration Message did not switch MPE on");
    p.syncMpe();
    CHECK(p.state().getRawParameterValue("mpe")->load() > 0.5f, "the MPE switch did not follow the configuration message");
    // channel 9 is outside the zone: its pressure is the old global finger load, so it
    // moves a note on channel 2 too; channel 8 (a member) does not
    auto level = [](int pressCh) {
        MagnetikaProcessor q;
        q.setPlayConfigDetails(0, 2, SR, 256);
        q.prepareToPlay(SR, 256);
        playBlocks(q, 1, {M::controllerEvent(1, 101, 0), M::controllerEvent(1, 100, 6), M::controllerEvent(1, 6, 7)});
        playBlocks(q, 280, {pressure(pressCh, 0), M::noteOn(2, 72, 0.8f)});
        return voiceLevel(q, 72);
    };
    CHECK(level(9) < level(8) * 0.7f, "channel 9 should be global outside a 7-member zone (%.3g vs %.3g)", level(9), level(8));
    // zone off again
    playBlocks(p, 1, {M::controllerEvent(1, 101, 0), M::controllerEvent(1, 100, 6), M::controllerEvent(1, 6, 0)});
    CHECK(!p.readDisplay().mpe, "an MCM with 0 members did not switch MPE off");
}

void testMpeSlide() {
    using M = juce::MidiMessage;
    // fixed bowl (spindle stopped): slide at the bottom is a still finger, so the note fades;
    // slide at the centre rubs at the finger-speed parameter and keeps it going
    auto glass = [](int slide) {
        MagnetikaProcessor p;
        setParam(p, "spindleOn", 0.0f);
        mpeProcessor(p);
        playBlocks(p, 470, {M::controllerEvent(2, 74, slide), M::noteOn(2, 72, 0.8f)});
        return voiceLevel(p, 72);
    };
    CHECK(glass(0) < glass(64) * 0.3f, "slide 0 on a fixed bowl should stop the rub (%.3g vs %.3g)", glass(0), glass(64));
    // bars: slide picks where the mallet lands, so the strike sounds different
    auto bar = [](int slide) {
        MagnetikaProcessor p;
        mpeProcessor(p, true);
        return playBlocks(p, 40, {M::controllerEvent(2, 74, slide), M::noteOn(2, 69, 0.8f)});
    };
    CHECK(bar(0) != bar(127), "slide did not move the bars' strike position");
}

bool isHeld(MagnetikaProcessor& p, int midi, bool glass = true) {
    const auto d = p.readDisplay();
    for (int i = 0; i < d.count; ++i) {
        const auto& v = d.voices[static_cast<size_t>(i)];
        if (v.midi == midi && v.glass == glass && v.held) return true;
    }
    return false;
}

std::vector<juce::MidiMessage> mcm(int masterCh, int members) {
    using M = juce::MidiMessage;
    return {M::controllerEvent(masterCh, 101, 0), M::controllerEvent(masterCh, 100, 6), M::controllerEvent(masterCh, 6, members),
            M::controllerEvent(masterCh, 101, 127), M::controllerEvent(masterCh, 100, 127)};
}

void testMpeNoteBookkeeping() {
    using M = juce::MidiMessage;
    {   // a repeated note-on on one channel is a re-strike, not a second holder
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 4, {M::noteOn(2, 72, 0.8f), M::noteOn(2, 72, 0.8f)});
        playBlocks(p, 4, {M::noteOff(2, 72)});
        CHECK(!isHeld(p, 72), "a doubled note-on left C5 held after its note-off");
    }
    {   // a stray note-off from a channel that does not own the note leaves it alone
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 4, {M::noteOn(3, 72, 0.8f)});
        playBlocks(p, 4, {M::noteOff(2, 72)});
        CHECK(isHeld(p, 72), "a note-off on another channel released channel 3's C5");
        playBlocks(p, 4, {M::noteOff(3, 72)});
        CHECK(!isHeld(p, 72), "channel 3's own note-off should release C5");
    }
    {   // a note begun before MPE was switched on still releases
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        p.prepareToPlay(SR, 256);
        playBlocks(p, 4, {M::noteOn(2, 72, 0.8f)});
        setParam(p, "mpe", 1.0f);
        playBlocks(p, 4, {M::noteOff(2, 72)});
        CHECK(!isHeld(p, 72), "a note held across the MPE switch stuck");
    }
    {   // and one held while the zone layout changes
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 4, {M::noteOn(5, 72, 0.8f)});
        playBlocks(p, 1, mcm(1, 7));
        playBlocks(p, 4, {M::noteOff(5, 72)});
        CHECK(!isHeld(p, 72), "a note held across a zone change stuck");
    }
    {   // all-notes-off on a member channel releases that channel's note only
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 4, {M::noteOn(2, 72, 0.8f), M::noteOn(3, 76, 0.8f)});
        playBlocks(p, 4, {M::allNotesOff(2)});
        CHECK(!isHeld(p, 72) && isHeld(p, 76), "all-notes-off on channel 2 should release only its note");
    }
    {   // switching instrument mid-note: the new instrument's note sounds
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 4, {M::noteOn(2, 72, 0.8f)});
        setParam(p, "instrument", 1.0f);
        playBlocks(p, 4, {M::noteOn(3, 72, 0.8f)});
        CHECK(isHeld(p, 72, false), "a bars note on a pitch the glass still held did not sound");
    }
}

// Pressure on `pressCh` with C5 playing on `noteCh`: identical to no pressure at all when
// pressCh is a member channel with no note, different when it is global (finger load).
bool pressureIsGlobal(MagnetikaProcessor& p, int noteCh, int pressCh) {
    using M = juce::MidiMessage;
    juce::MemoryBlock st;
    p.getStateInformation(st);
    auto render = [&](bool press) {
        MagnetikaProcessor q;
        q.setStateInformation(st.getData(), static_cast<int>(st.getSize()));
        q.setPlayConfigDetails(0, 2, SR, 256);
        q.prepareToPlay(SR, 256);
        std::vector<M> first;
        if (press) first.push_back(pressure(pressCh, 0));
        first.push_back(M::noteOn(noteCh, 72, 0.8f));
        return playBlocks(q, 60, first);
    };
    return render(true) != render(false);
}

void testMpeZones() {
    {   // "lower 15, upper 0" (what many controllers send) keeps channel 16 a member
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        p.prepareToPlay(SR, 256);
        playBlocks(p, 1, mcm(1, 15));
        playBlocks(p, 1, mcm(16, 0));
        p.syncMpe();
        CHECK(!pressureIsGlobal(p, 2, 16), "channel 16 dropped out of a 15-member lower zone after 'upper 0'");
    }
    {   // an upper zone only: channels 9..15 are members, channel 2 is not
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        p.prepareToPlay(SR, 256);
        playBlocks(p, 1, mcm(16, 7));
        p.syncMpe();
        CHECK(pressureIsGlobal(p, 15, 2), "channel 2 acted as a member with only an upper zone configured");
        CHECK(!pressureIsGlobal(p, 15, 14), "channel 14 should be a member of a 7-channel upper zone");
        // pressureIsGlobal renders from the saved state: the zone layout survives it
        // (an upper-only zone restored as the default lower zone would make channel 2 a member)
    }
}

void testMpeBendRangeFromRpn() {
    using M = juce::MidiMessage;
    // RPN 0 = 1 semitone on a member channel: a quarter-wheel bend is then 25 c, as with
    // the parameter at 1; at the default 48 it would be clamped to the 40 c spring limit
    auto render = [](bool viaRpn, float rangeParam, float* paramAfter) {
        MagnetikaProcessor p;
        setParam(p, "mpeBendRange", rangeParam);
        mpeProcessor(p);
        std::vector<M> first;
        if (viaRpn) {
            first.push_back(M::controllerEvent(2, 101, 0));
            first.push_back(M::controllerEvent(2, 100, 0));
            first.push_back(M::controllerEvent(2, 6, 1));
        }
        first.push_back(M::pitchWheel(2, 8192 + 2048));
        first.push_back(M::noteOn(2, 72, 0.8f));
        auto out = playBlocks(p, 150, first);
        p.syncMpe();
        if (paramAfter) *paramAfter = p.state().getRawParameterValue("mpeBendRange")->load();
        return out;
    };
    float after = 0;
    const auto viaRpn = render(true, 48, &after);
    CHECK(viaRpn == render(false, 1, nullptr), "RPN 0 = 1 semitone did not act as a 1-semitone member bend range");
    CHECK(viaRpn != render(false, 48, nullptr), "the RPN 0 bend range had no effect");
    CHECK(after == 1.0f, "the MPE bend range parameter did not follow RPN 0 (%g)", after);
}

void testMpeSlideFollowsTheEffectiveSpin() {
    using M = juce::MidiMessage;
    // spindle switched on but at 0 rev/s is a fixed bowl, as the engine treats it:
    // centre slide rubs at the finger-speed setting (sounds), bottom is a still finger
    auto glass = [](int slide) {
        MagnetikaProcessor p;
        setParam(p, "g_spin", 0.0f);
        mpeProcessor(p);
        playBlocks(p, 470, {M::controllerEvent(2, 74, slide), M::noteOn(2, 72, 0.8f)});
        return voiceLevel(p, 72);
    };
    CHECK(glass(0) < glass(64) * 0.3f, "spin 0 with the spindle on: slide not read as rub speed (%.3g vs %.3g)", glass(0), glass(64));
}

// A member-channel pitch-wheel message for `cents`, with the default 48-semitone range.
juce::MidiMessage bendTo(int ch, double cents) {
    return juce::MidiMessage::pitchWheel(ch, juce::jlimit(0, 16383, 8192 + static_cast<int>(std::lround(cents / 4800.0 * 8192.0))));
}

void testMpeGlideAcrossBowls() {
    using M = juce::MidiMessage;
    MagnetikaProcessor p;
    mpeProcessor(p);
    playBlocks(p, 4, {M::noteOn(2, 72, 0.8f)});
    auto on = [&](double cents) {
        playBlocks(p, 2, {bendTo(2, cents)});
        int bowl = -1;
        for (int m = 60; m <= 84; ++m)
            if (isHeld(p, m)) bowl = bowl < 0 ? m : 999;  // 999: more than one held
        return bowl;
    };
    CHECK(on(30) == 72, "a 30 c bend should stay on the C5 bowl's coils");
    CHECK(on(52) == 72, "52 c is inside the hysteresis band: still C5");
    CHECK(on(60) == 73, "past 55 c the finger should slide to C#5");
    CHECK(on(100) == 73, "a full semitone should sit on C#5");
    CHECK(on(46) == 73, "46 c is inside the band from C#5: still C#5");
    CHECK(on(40) == 72, "back past the band: the finger should return to C5");
    CHECK(on(230) == 74, "a fast 2.3-semitone bend should land on D5");
    CHECK(voiceLevel(p, 72) > 0, "the bowl left behind should ring on");
    playBlocks(p, 2, {M::noteOff(2, 72)});
    bool anyHeld = false;
    for (int m = 60; m <= 84; ++m) anyHeld = anyHeld || isHeld(p, m);
    CHECK(!anyHeld, "the note-off for C5 should release the bowl the glide moved to");
    // a channel bent before its note-on starts on the nearest bowl
    playBlocks(p, 2, {bendTo(3, 200), M::noteOn(3, 72, 0.8f)});
    CHECK(isHeld(p, 74) && !isHeld(p, 72), "a pre-bent note should start on D5");
    playBlocks(p, 2, {M::noteOff(3, 72)});
    CHECK(!isHeld(p, 74), "and release from there");
    // glide switched off: the spring clamps, no hand-off
    {
        MagnetikaProcessor q;
        setParam(q, "mpeGlide", 0.0f);
        mpeProcessor(q);
        playBlocks(q, 2, {M::noteOn(2, 72, 0.8f), bendTo(2, 100)});
        CHECK(isHeld(q, 72) && !isHeld(q, 73), "with glide off a semitone bend must stay on C5");
    }
    // the bars cannot be glided onto: they clamp
    {
        MagnetikaProcessor q;
        mpeProcessor(q, true);
        playBlocks(q, 2, {M::noteOn(2, 72, 0.8f), bendTo(2, 100)});
        CHECK(isHeld(q, 72, false) && !isHeld(q, 73, false), "a bars bend must not strike the next bar");
    }
}

void testMpePressureLaw() {
    using M = juce::MidiMessage;
    // pressure is how firmly the finger presses: the glass catches at the same point of
    // the travel however hard the note was struck (velocity no longer moves it)
    auto level = [](float vel, int press) {
        MagnetikaProcessor p;
        setParam(p, "g_coilStart", 0.0f);  // the finger alone
        mpeProcessor(p);
        playBlocks(p, 1, {M::noteOn(2, 72, vel), pressure(2, 127)});  // pressure takes over at once
        playBlocks(p, 470, {pressure(2, press)});
        return voiceLevel(p, 72);
    };
    float spoke[2] = {};
    int i = 0;
    for (float vel : {0.2f, 1.0f}) {
        const float whisper = level(vel, 15), speaking = level(vel, 50);
        CHECK(speaking > whisper * 3, "velocity %.1f: 39%% pressure should speak, 12%% whisper (%.3g vs %.3g)", vel, speaking, whisper);
        spoke[i++] = speaking;
    }
    // and the same pressure gives the same tone whatever the velocity (within 4 dB)
    CHECK(spoke[0] > spoke[1] / 1.6f && spoke[0] < spoke[1] * 1.6f,
          "39%% pressure sounds different after a soft and a hard strike (%.3g vs %.3g)", spoke[0], spoke[1]);
    // a hard strike with the controller's pressure still at 0 keeps the strike's load...
    MagnetikaProcessor a;
    setParam(a, "g_coilStart", 0.0f);
    mpeProcessor(a);
    playBlocks(a, 470, {pressure(2, 0), M::noteOn(2, 72, 1.0f)});
    // ...until pressure overtakes it; from then on easing off goes down to a whisper
    MagnetikaProcessor b;
    setParam(b, "g_coilStart", 0.0f);
    mpeProcessor(b);
    playBlocks(b, 1, {pressure(2, 0), M::noteOn(2, 72, 1.0f), pressure(2, 127)});
    playBlocks(b, 470, {pressure(2, 0)});
    CHECK(voiceLevel(a, 72) > voiceLevel(b, 72) * 3, "a hard strike at pressure 0 collapsed to a whisper (%.3g vs %.3g)",
          voiceLevel(a, 72), voiceLevel(b, 72));
}

void testMpeSlideIsRubSpeed() {
    using M = juce::MidiMessage;
    // slide is the rub speed and so the loudness, from quiet up to just short of the
    // speed where the glass stops speaking
    // output RMS over the last second: one envelope reading swings by 2x as the two
    // (2,0) modes beat, which would hide a trend; the cliff itself is a 20 dB drop
    auto level = [](int slide, bool spindle, float spinRevS) {
        MagnetikaProcessor p;
        setParam(p, "spindleOn", spindle ? 1.0f : 0.0f);
        setParam(p, "g_spin", spinRevS);
        setParam(p, "variation", 0.0f);  // the measured bowl, not one hash pattern of it
        mpeProcessor(p);
        const auto out = playBlocks(p, 470, {M::controllerEvent(2, 74, slide), M::noteOn(2, 72, 0.8f)});
        double e = 0;
        const size_t from = out.size() - 48000;
        for (size_t i = from; i < out.size(); ++i) e += static_cast<double>(out[i]) * out[i];
        return static_cast<float>(std::sqrt(e / 48000));
    };
    for (bool spindle : {false, true}) {
        // 0.7 rev/s (4.4 rad/s at the rim): the top (5 rad/s) is above the centre, so it is exercised
        const float spin = spindle ? 0.7f : 1.0f;
        const float slow = level(16, spindle, spin), centre = level(64, spindle, spin), top = level(127, spindle, spin);
        CHECK(slow < centre * 0.5f, "%s: a slow rub should be quieter than centre (%.3g vs %.3g)",
              spindle ? "spindle" : "fixed bowl", slow, centre);
        CHECK(top > centre * 0.7f, "%s: the top of the slide fell off the cliff (%.3g vs centre %.3g)",
              spindle ? "spindle" : "fixed bowl", top, centre);
    }
    // a slow spindle leaves room above: rubbing against it is louder
    const float centreSlow = level(64, true, 0.5f), topSlow = level(127, true, 0.5f);
    CHECK(topSlow > centreSlow * 1.5f, "spindle 0.5 rev/s: rubbing against the glass should be louder (%.3g vs %.3g)",
          topSlow, centreSlow);
}

void testMpeGlideEdges() {
    using M = juce::MidiMessage;
    {   // the zones change mid-glide: the bowl the finger glided to is released (the
        // controller's note-off names the note it gave, which the glide had left)
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 2, {M::noteOn(2, 72, 0.8f), bendTo(2, 200)});
        CHECK(isHeld(p, 74), "the glide should have reached D5");
        playBlocks(p, 2, mcm(1, 7));
        CHECK(!isHeld(p, 74), "a zone change mid-glide left D5 held for good");
    }
    {   // reset-all-controllers mid-glide: neutral expression, but no new attack
        MagnetikaProcessor p;
        mpeProcessor(p);
        playBlocks(p, 2, {M::noteOn(2, 72, 0.8f), bendTo(2, 200)});
        playBlocks(p, 2, {M::controllerEvent(2, 121, 0)});
        CHECK(isHeld(p, 74) && !isHeld(p, 72), "CC121 mid-glide should stay on D5, not re-strike C5");
        playBlocks(p, 2, {M::noteOff(2, 72)});
        CHECK(!isHeld(p, 74), "and the note-off still releases D5");
    }
    {   // a repeated note-on on a held bowl strikes nothing, so pressure keeps ruling
        auto level = [](bool repeat) {
            MagnetikaProcessor p;
            mpeProcessor(p);
            playBlocks(p, 1, {M::noteOn(2, 72, 0.8f), pressure(2, 127)});
            playBlocks(p, 100, {pressure(2, 40)});
            if (repeat) playBlocks(p, 1, {M::noteOn(2, 72, 1.0f)});
            playBlocks(p, 470, {pressure(2, 5)});
            return voiceLevel(p, 72);
        };
        CHECK(level(true) < level(false) * 1.5f, "a repeated note-on stopped the bowl following pressure (%.3g vs %.3g)",
              level(true), level(false));
    }
    {   // gliding onto a bowl another finger holds joins it and leaves that finger's bend:
        // landing 10 c sharp or 10 c flat of D5 sounds the same
        auto render = [](double cents) {
            MagnetikaProcessor p;
            mpeProcessor(p);
            playBlocks(p, 20, {bendTo(3, 30), M::noteOn(3, 74, 0.8f)});
            return playBlocks(p, 100, {M::noteOn(2, 72, 0.8f), bendTo(2, cents)});
        };
        CHECK(render(210) == render(190), "a glide joining a held bowl overwrote the other finger's bend");
    }
    {   // the spindle stops mid-note: a sounding slide is re-read as the fixed bowl's rub speed
        auto level = [](bool slide) {
            MagnetikaProcessor p;
            mpeProcessor(p);
            std::vector<M> first;
            if (slide) first.push_back(M::controllerEvent(2, 74, 64));
            first.push_back(M::noteOn(2, 72, 0.8f));
            playBlocks(p, 50, first);
            setParam(p, "spindleOn", 0.0f);
            playBlocks(p, 470);
            return voiceLevel(p, 72);
        };
        CHECK(level(true) > level(false) * 3, "the slide was not re-applied when the spindle stopped (%.3g vs %.3g)",
              level(true), level(false));
    }
}

void testBuildParameters() {
    // Rev B sizes, Instrument No. and Build variation reach the engine (next note on)
    auto render = [](float revB, int serial, float variation, int midi) {
        MagnetikaProcessor p;
        p.setPlayConfigDetails(0, 2, SR, 256);
        setParam(p, "revBSizes", revB);
        setParam(p, "serialNo", static_cast<float>(serial));
        setParam(p, "variation", variation);
        p.prepareToPlay(SR, 256);
        return playBlocks(p, 100, {juce::MidiMessage::noteOn(1, midi, 0.8f)});
    };
    const auto base = render(0, 1, 0.5f, 72);
    CHECK(render(0, 1, 0.5f, 72) == base, "one instrument number must always sound the same");
    CHECK(render(0, 2, 0.5f, 72) != base, "Instrument No. had no effect");
    CHECK(render(0, 1, 0.0f, 72) != base, "Build variation had no effect");
    CHECK(render(1, 1, 0.5f, 60) != render(0, 1, 0.5f, 60), "Rev B bowl sizes had no effect on C4");
}

void testOldStateLoadsWithoutVariation() {
    // a project saved before build variation existed must sound as it did (variation 0);
    // one saved with it keeps its value
    MagnetikaProcessor src;
    setParam(src, "variation", 0.7f);
    juce::MemoryBlock withIt;
    src.getStateInformation(withIt);
    auto xml = juce::AudioProcessor::getXmlFromBinary(withIt.getData(), static_cast<int>(withIt.getSize()));
    CHECK(xml != nullptr, "state did not round-trip as XML");
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.removeChild(tree.getChildWithProperty("id", "variation"), nullptr);  // as an older version saved it
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), old);
    MagnetikaProcessor a, b;
    a.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    b.setStateInformation(withIt.getData(), static_cast<int>(withIt.getSize()));
    CHECK(a.state().getRawParameterValue("variation")->load() == 0.0f, "an old state should load with build variation 0 (got %g)",
          a.state().getRawParameterValue("variation")->load());
    CHECK(std::fabs(b.state().getRawParameterValue("variation")->load() - 0.7f) < 1e-4f, "a saved variation was not restored");
}

void testPresetStoreRoundTrip() {
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("magnetika-preset-test");
    dir.deleteRecursively();
    magnetika::plugin::PresetStore store(dir);
    MagnetikaProcessor p;
    setParam(p, "g_Q2", 9000.0f);
    setParam(p, "instrument", 1.0f);
    CHECK(store.save("Warm: bars / 9k", p.state()), "saving a preset failed");
    setParam(p, "g_Q2", 1500.0f);
    setParam(p, "instrument", 0.0f);
    const auto names = store.names();
    CHECK(names.size() == 1, "expected one saved preset, found %d", names.size());
    CHECK(names.size() == 1 && store.load(names[0], p.state()), "loading the saved preset failed");
    auto val = [&](const char* pid) { return p.state().getRawParameterValue(pid)->load(); };
    CHECK(std::fabs(val("g_Q2") - 9000.0f) < 1.0f, "g_Q2 not restored by the preset: %f", val("g_Q2"));
    CHECK(val("instrument") == 1.0f, "instrument not restored by the preset");
    CHECK(!store.load("no such preset", p.state()), "loading a missing preset must fail");
    dir.deleteRecursively();
}

void testBowlLevels() {
    // evening (default 1) plays every bowl at C4's level; a trim moves one bowl; both reach the sound
    MagnetikaProcessor p;
    CHECK(p.state().getRawParameterValue("levelEven")->load() == 1.0f, "Bowl level evening should default to 1");
    CHECK(p.bowlGainDb(60) == 0.0f, "C4 is the reference, got %g dB", p.bowlGainDb(60));
    CHECK(std::fabs(p.bowlGainDb(84) + 11.4f) < 0.05f, "C6 evening %g dB, want -11.4", p.bowlGainDb(84));
    CHECK(std::fabs(p.bowlGainDb(48) - 6.3f) < 0.05f, "C3 evening %g dB, want +6.3", p.bowlGainDb(48));
    CHECK(p.bowlGainDb(36) == 9.0f, "the low boost should stop at +9 dB, got %g", p.bowlGainDb(36));

    // what one held C6 measures over 1-2 s, at a level the limiter leaves linear
    auto level = [](float even, float trim) {
        MagnetikaProcessor q;
        setParam(q, "g_master", 0.1f);
        setParam(q, "levelEven", even);
        q.setBowlTrimDb(84, trim);
        const auto r = render(q, static_cast<int>(2 * SR), 256, {{0, juce::MidiMessage::noteOn(1, 84, 0.8f)}});
        return rmsDb(r.L, 1.0, 2.0) + rmsDb(r.R, 1.0, 2.0);
    };
    const double raw = level(0, 0), even = level(1, 0), trimmed = level(1, 6);
    CHECK(std::fabs((even - raw) / 2 + 11.4) < 0.5, "evening moved C6 by %.2f dB, want -11.4", (even - raw) / 2);
    CHECK(std::fabs((trimmed - even) / 2 - 6.0) < 0.5, "a +6 dB trim moved C6 by %.2f dB", (trimmed - even) / 2);

    // trims are clamped, saved with the project, and replaced (not merged) by a load
    p.setBowlTrimDb(70, 40.0f);
    CHECK(p.bowlTrimDb(70) == MagnetikaProcessor::kTrimMaxDb, "trim not clamped: %g", p.bowlTrimDb(70));
    p.setBowlTrimDb(70, -3.5f);
    p.setBowlTrimDb(91, 2.25f);
    juce::MemoryBlock mb;
    p.getStateInformation(mb);
    MagnetikaProcessor b;
    b.setBowlTrimDb(60, 5.0f);
    b.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    CHECK(b.bowlTrimDb(70) == -3.5f && b.bowlTrimDb(91) == 2.25f, "trims not restored: %g %g", b.bowlTrimDb(70), b.bowlTrimDb(91));
    CHECK(b.bowlTrimDb(60) == 0.0f, "a load should clear trims the project did not have (C4 kept %g)", b.bowlTrimDb(60));
    CHECK(!b.state().state.hasProperty("bowlTrims"), "trims leaked into the parameter tree (and so into presets)");

    // a project saved before evening existed loads with it off, so it sounds as it did
    auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), static_cast<int>(mb.getSize()));
    CHECK(xml != nullptr, "state did not round-trip as XML");
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.removeChild(tree.getChildWithProperty("id", "levelEven"), nullptr);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), old);
    MagnetikaProcessor c;
    c.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(c.state().getRawParameterValue("levelEven")->load() == 0.0f, "an old state should load with evening 0");
    CHECK(c.bowlGainDb(84) == 0.0f, "with evening off C6 should play as modelled, got %g dB", c.bowlGainDb(84));
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
    if (view.contains("mpe")) setParam(p, "mpe", 1.0f);
    if (view.contains("revb")) setParam(p, "revBSizes", 1.0f);
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

// --render in.mid out.wav [glass|bars] [level]: plays a MIDI file through the plugin as it
// loads (default parameters; `level` sets Glass/Bar level, default 0.6) and writes a
// 48 kHz 24-bit stereo WAV, for comparing builds against each other or against a
// recording. Chords at the default level drive the output limiter; 0.25 keeps clear. Only notes and the sustain pedal
// (CC64) are passed, all on channel 1: a file's program changes would switch the
// instrument, and its volume/expression/pan controllers mean nothing here.
int renderMidi(const juce::String& inPath, const juce::String& outPath, const juce::String& inst, float level) {
    const juce::File inFile = juce::File::getCurrentWorkingDirectory().getChildFile(inPath);
    juce::FileInputStream in(inFile);
    juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) {
        std::printf("cannot read MIDI file %s\n", inPath.toRawUTF8());
        return 1;
    }
    mf.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence seq;
    int notes = 0, pedals = 0, dropped = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (const auto* e : *mf.getTrack(t)) {
            juce::MidiMessage m = e->message;
            if (m.isNoteOnOrOff()) {
                notes += m.isNoteOn() ? 1 : 0;
            } else if (m.isController() && m.getControllerNumber() == 64) {
                ++pedals;
            } else {
                dropped += m.isMetaEvent() ? 0 : 1;
                continue;
            }
            m.setChannel(1);
            seq.addEvent(m);
        }
    seq.sort();
    seq.updateMatchedPairs();
    if (notes == 0) {
        std::printf("no notes in %s\n", inPath.toRawUTF8());
        return 1;
    }

    MagnetikaProcessor p;
    if (inst == "bars") setParam(p, "instrument", 1);
    setParam(p, inst == "bars" ? "b_master" : "g_master", level);
    constexpr int block = 256;
    p.setPlayConfigDetails(0, 2, SR, block);
    p.prepareToPlay(SR, block);

    const double end = seq.getEndTime();
    const int maxTail = static_cast<int>(20 * SR);  // ring-out after the last event, at most 20 s
    juce::AudioBuffer<float> all(2, static_cast<int>(end * SR) + maxTail + block);
    juce::AudioBuffer<float> buf(2, block);
    int next = 0, quiet = 0, written = 0;
    for (int start = 0;; start += block) {
        buf.clear();
        juce::MidiBuffer midi;
        while (next < seq.getNumEvents()) {
            const auto& m = seq.getEventPointer(next)->message;
            const int at = static_cast<int>(std::lround(m.getTimeStamp() * SR));
            if (at >= start + block) break;
            midi.addEvent(m, std::max(0, at - start));
            ++next;
        }
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, start, buf, c, 0, block);
        written = start + block;
        if (next >= seq.getNumEvents()) {
            // stop after half a second below -90 dBFS, or at the tail limit
            quiet = buf.getMagnitude(0, block) < 3.2e-5f ? quiet + block : 0;
            if (quiet >= SR / 2 || written >= static_cast<int>(end * SR) + maxTail) break;
        }
    }

    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(outPath);
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> os(outFile.createOutputStream());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w;
    if (os != nullptr)  // takes the stream on success
        w = wav.createWriterFor(
            os, juce::AudioFormatWriterOptions{}.withSampleRate(SR).withNumChannels(2).withBitsPerSample(24));
    if (w == nullptr) {
        std::printf("cannot write %s\n", outPath.toRawUTF8());
        return 1;
    }
    w->writeFromAudioSampleBuffer(all, 0, written);
    w.reset();  // flushes the header
    const float peak = all.getMagnitude(0, written);
    std::printf("%s -> %s (%s, level %.2f): %d notes, %d pedal events, %d other events dropped; %.1f s, peak %.1f dBFS\n",
                inPath.toRawUTF8(), outPath.toRawUTF8(), inst == "bars" ? "bars" : "glass", level, notes, pedals,
                dropped, written / SR, 20 * std::log10(peak + 1e-12f));
    return 0;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;  // message manager for parameter notifications
    if (argc >= 4 && juce::String(argv[1]) == "--render")
        return renderMidi(argv[2], argv[3], argc >= 5 ? juce::String(argv[4]) : "glass",
                          argc >= 6 ? juce::String(argv[5]).getFloatValue() : 0.6f);
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
    testPresetStoreRoundTrip();
    testFullUiQueueNeverStrandsANote();
    testTwoFingersOnOneNote();
    testMpePressureIsPerNote();
    testMpeBendIsPerNoteAndClamped();
    testMpeSharedNoteAndAllOff();
    testMpeConfigurationMessage();
    testMpeSlide();
    testMpeNoteBookkeeping();
    testMpeZones();
    testMpeBendRangeFromRpn();
    testMpeSlideFollowsTheEffectiveSpin();
    testMpeGlideAcrossBowls();
    testMpePressureLaw();
    testMpeSlideIsRubSpeed();
    testMpeGlideEdges();
    testBuildParameters();
    testOldStateLoadsWithoutVariation();
    testBowlLevels();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all plugin tests passed\n");
    return 0;
}
