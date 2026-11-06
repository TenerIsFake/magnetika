#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "PluginEditor.h"

namespace magnetika::plugin {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr int kVoices = Synth::kMaxVoices;  // 16
}

MagnetikaProcessor::MagnetikaProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "MAGNETIKA", createLayout()) {
    auto get = [this](const char* pid) {
        auto* r = apvts_.getRawParameterValue(pid);
        jassert(r != nullptr);
        return r;
    };
    p_ = {get(id::instrument), get(id::sustain), get(id::brake), get(id::vibDepth), get(id::rotation),
          get(id::trimOn), get(id::trimAmount), get(id::pressure), get(id::bendRange), get(id::n3ctl),
          get(id::coilsArmed), get(id::massScaling), get(glassSustainGain), get(glassBrakeGain),
          get(barsSustainGain), get(barsBrakeGain)};
    for (const auto& m : engineMappings()) mappedRaw_.push_back(get(m.paramId));
    mappedLast_.assign(mappedRaw_.size(), 0.0f);
    spindleOnRaw_ = get(id::spindleOn);
    mpeRaw_ = get(id::mpe);
    mpeBendRangeRaw_ = get(id::mpeBendRange);
    fingerSpeedRaw_ = get("g_fingerSpeed");
    spinRaw_ = get("g_spin");
    mpeGlideRaw_ = get(id::mpeGlide);
    revBRaw_ = get(id::revBSizes);
    serialRaw_ = get(id::serialNo);
    variationRaw_ = get(id::variation);
    rpnMsb_.fill(127);
    rpnLsb_.fill(127);
    startTimerHz(20);
}

void MagnetikaProcessor::syncMpe() {
    auto write = [this](const char* pid, float value) {
        if (auto* prm = apvts_.getParameter(pid)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(value));
            prm->endChangeGesture();
        }
    };
    if (const int on = pendingMpe_.exchange(-1); on >= 0) write(id::mpe, static_cast<float>(on));
    if (const float st = pendingMpeBendRange_.exchange(-1.0f); st >= 0) write(id::mpeBendRange, st);
}

void MagnetikaProcessor::syncProgramChange() {
    const int prog = pendingProgram_.exchange(-1);
    if (prog < 0) return;
    if (auto* prm = apvts_.getParameter(id::instrument)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(static_cast<float>(prog)));
        prm->endChangeGesture();
    }
}

bool MagnetikaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled() &&
           (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void MagnetikaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    synth_ = std::make_unique<Synth>(sampleRate, kVoices);
    scratchL_.assign(static_cast<size_t>(std::max(samplesPerBlock, 512)), 0.0f);
    scratchR_.assign(scratchL_.size(), 0.0f);
    keyboard_.reset();
    perf_ = Perf{};
    bendWheel_ = 0;
    mpeCh_ = {};
    mpeHeld_ = {};
    rpnMsb_.fill(127);
    rpnLsb_.fill(127);
    lastMpeParam_ = -1;
    applyParameters(true);
}

// Pushes every changed parameter into the engine (all of them when `force`).
// Runs at the start of each block, on the audio thread.
void MagnetikaProcessor::applyParameters(bool force) {
    const auto& maps = engineMappings();
    // The spindle switch gates the spin speed: stopped means fixed bowls (spin 0).
    const float spindle = spindleOnRaw_->load();
    const bool spindleChanged = spindle != lastSpindleOn_;
    lastSpindleOn_ = spindle;
    for (size_t i = 0; i < maps.size(); ++i) {
        const float v = mappedRaw_[i]->load();
        const bool isSpin = maps[i].engineId == ParamId::spin;
        if (!force && v == mappedLast_[i] && !(isSpin && spindleChanged)) continue;
        mappedLast_[i] = v;
        double value = maps[i].degreesToRadians ? v * kPi / 180.0 : static_cast<double>(v);
        if (isSpin && spindle < 0.5f) value = 0.0;
        const ParamValue pv{maps[i].engineId, value};
        synth_->setParams(maps[i].instrument, &pv, 1);
    }

    const float armed = p_.coilsArmed->load(), mass = p_.massScaling->load();
    if (force || armed != lastArmed_) synth_->setCoilsArmed(armed > 0.5f);
    if (force || mass != lastMassScaling_) synth_->setScaleModalMass(mass > 0.5f);
    lastArmed_ = armed;
    // the instrument as built: its bowl sizes and how its bowls and bars differ (next note on)
    const float build[3] = {revBRaw_->load(), serialRaw_->load(), variationRaw_->load()};
    if (force || !std::equal(build, build + 3, lastBuild_)) {
        std::copy(build, build + 3, lastBuild_);
        synth_->setRevBSizes(build[0] > 0.5f);
        synth_->setVariation(static_cast<std::uint32_t>(build[1]), build[2]);
    }
    lastMassScaling_ = mass;

    // Performance parameters: apply only the ones the user moved, so a value set
    // by MIDI (CC, pedal, wheel) is not overwritten every block.
    bool dirty = force;
    auto moved = [force](float now, float& last) {
        const bool m = force || now != last;
        last = now;
        return m;
    };
    auto& L = perfLast_;
    if (moved(p_.instrument->load(), L.instrument)) { perf_.instrument = static_cast<int>(L.instrument); dirty = true; }
    if (moved(p_.sustain->load(), L.sustain)) { perf_.sustain = L.sustain > 0.5f; dirty = true; }
    if (moved(p_.brake->load(), L.brake)) { perf_.brake = L.brake > 0.5f; dirty = true; }
    if (moved(p_.vibDepth->load(), L.vibDepth)) { perf_.vibDepth = L.vibDepth; dirty = true; }
    if (moved(p_.rotation->load(), L.rotation)) { perf_.rotation = L.rotation; dirty = true; }
    if (moved(p_.trimOn->load(), L.trimOn)) { perf_.trimOn = L.trimOn > 0.5f; dirty = true; }
    if (moved(p_.trimAmount->load(), L.trimAmount)) { perf_.trimAmount = L.trimAmount; dirty = true; }
    if (moved(p_.pressure->load(), L.pressure)) { perf_.pressure = L.pressure; dirty = true; }
    if (moved(p_.n3ctl->load(), L.n3ctl)) { perf_.n3ctl = L.n3ctl > 0.5f; dirty = true; }

    const float gains[4] = {p_.gG->load(), p_.gGb->load(), p_.bG->load(), p_.bGb->load()};
    for (int i = 0; i < 4; ++i) {
        if (gains[i] != lastGains_[i]) dirty = true;
        lastGains_[i] = gains[i];
    }
    if (const int z = pendingZones_.exchange(-1); z >= 0) setZones(z / 16, z % 16);  // from a saved project
    const float mpeParam = mpeRaw_->load();
    if (force || mpeParam != lastMpeParam_) {
        const bool on = mpeParam > 0.5f;
        if (on != mpeActive_) resetMpeNotes();
        if (on && lowerMembers_ == 0 && upperMembers_ == 0) setZones(15, 0);  // the usual lower zone
        mpeActive_ = on;
    }
    lastMpeParam_ = mpeParam;
    const float memberRange = mpeBendRangeRaw_->load();
    if (force || memberRange != lastMpeBendRangeParam_) memberBendRange_ = memberRange;
    lastMpeBendRangeParam_ = memberRange;
    // slide maps onto the rub speed through these: a change re-applies every sounding slide
    const float slideInputs[3] = {spindleOnRaw_->load(), spinRaw_->load(), fingerSpeedRaw_->load()};
    if (!std::equal(slideInputs, slideInputs + 3, lastSlideInputs_)) {
        std::copy(slideInputs, slideInputs + 3, lastSlideInputs_);
        if (mpeActive_ && !force)
            for (const auto& c : mpeCh_)
                if (c.note >= 0 && c.slide >= 0) applySlide(c.note, c.slide, false);
    }

    // the bend range scales the wheel, so a range change moves the bend
    const float bend = bendWheel_ * p_.bendRange->load();
    if (bend != perf_.bendCents) { perf_.bendCents = bend; dirty = true; }

    if (dirty) pushPerf();
}

// Mirrors the player's pushPerf: pedal state and the current instrument's coil
// gains go to setPerf; the performance controls go to both instruments.
void MagnetikaProcessor::pushPerf() {
    const auto inst = perf_.instrument == 0 ? Instrument::Glass : Instrument::Bars;
    synth_->setInst(inst);
    const float Gs = inst == Instrument::Glass ? lastGains_[0] : lastGains_[2];
    const float Gb = inst == Instrument::Glass ? lastGains_[1] : lastGains_[3];
    const PerfValue pv[] = {{PerfId::sustain, perf_.sustain ? 1.0 : 0.0},
                            {PerfId::brake, perf_.brake ? 1.0 : 0.0},
                            {PerfId::Gs, Gs},
                            {PerfId::Gb, Gb}};
    synth_->setPerf(pv, 4);
    const ParamValue shared[] = {{ParamId::vibDepth, perf_.vibDepth},
                                 {ParamId::rot, perf_.rotation},
                                 {ParamId::trim, perf_.trimOn ? perf_.trimAmount : 0.0f},
                                 {ParamId::bend, perf_.bendCents},
                                 {ParamId::pressure, perf_.pressure},
                                 {ParamId::n3ctl, perf_.n3ctl ? 1.0 : 0.0}};
    synth_->setParams(Instrument::Glass, shared, 6);
    synth_->setParams(Instrument::Bars, shared, 6);
}

bool MagnetikaProcessor::pushUiEvent(const UiEvent& e) {
    {
        const auto scope = uiFifo_.write(1);
        if (scope.blockSize1 + scope.blockSize2 >= 1) {
            uiBuf_[static_cast<size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
            return true;
        }
    }
    // full: drop it, but never drop a release
    if (e.type == UiEvent::NoteOff) releaseAllPending_.store(true);
    if (e.type == UiEvent::AllOff) allOffPending_.store(true);
    return false;
}

// Touch gestures from the editor, applied at the start of the block.
void MagnetikaProcessor::drainUiEvents() {
    // a release refused by a full queue is newer than everything queued: apply it last
    const bool allOff = allOffPending_.exchange(false), releaseAll = releaseAllPending_.exchange(false);
    const auto scope = uiFifo_.read(uiFifo_.getNumReady());
    auto apply = [this](const UiEvent& e) {
        switch (e.type) {
            case UiEvent::NoteOn: synth_->noteOn(e.midi, e.value); break;
            case UiEvent::NoteOff: synth_->noteOff(e.midi); break;
            case UiEvent::Touch: synth_->setNoteTouch(e.midi, e.value); break;
            case UiEvent::FingerSpeed: synth_->setNoteFingerSpeed(e.midi, e.value); break;
            case UiEvent::StrikePos: synth_->setNoteStrikePos(e.midi, e.value); break;
            case UiEvent::AllOff:
                synth_->allOff();
                resetMpeNotes();
                break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i) apply(uiBuf_[static_cast<size_t>(scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) apply(uiBuf_[static_cast<size_t>(scope.startIndex2 + i)]);
    if (allOff) synth_->allOff();
    else if (releaseAll) synth_->releaseAll();
    if (allOff || releaseAll) resetMpeNotes();
}

void MagnetikaProcessor::publishDisplay(const juce::AudioBuffer<float>& buffer) {
    const int n = buffer.getNumSamples();
    dispSeq_.fetch_add(1, std::memory_order_relaxed);  // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    disp_.count = synth_->snapshot(disp_.voices.data(), static_cast<int>(disp_.voices.size()));
    disp_.peakL = n > 0 ? buffer.getMagnitude(0, 0, n) : 0.0f;
    disp_.peakR = n > 0 && buffer.getNumChannels() > 1 ? buffer.getMagnitude(1, 0, n) : disp_.peakL;
    disp_.limiting = std::max(disp_.peakL, disp_.peakR) > 0.46f;  // tanh(0.5): visibly compressing
    disp_.midiCount = midiCount_;
    disp_.sustain = perf_.sustain;
    disp_.brake = perf_.brake;
    disp_.rotation = perf_.rotation;
    disp_.vibDepth = perf_.vibDepth;
    disp_.instrument = perf_.instrument;
    disp_.mpe = mpeActive_;
    dispSeq_.fetch_add(1, std::memory_order_release);  // even: consistent
}

MagnetikaProcessor::Display MagnetikaProcessor::readDisplay() const {
    Display copy;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto s1 = dispSeq_.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        copy = disp_;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (dispSeq_.load(std::memory_order_relaxed) == s1) {
            lastGood_ = copy;
            return copy;
        }
    }
    return lastGood_;  // the writer kept winning: show the previous frame, not an empty one
}

// The browser player's MIDI map (see README), so the two are interchangeable.
void MagnetikaProcessor::handleMidi(const juce::MidiMessage& m) {
    ++midiCount_;
    const int ch = m.getChannel();
    if (m.isController() && handleRpn(ch, m.getControllerNumber(), m.getControllerValue())) return;
    if (isMpeMember(ch) && handleMpeMember(ch, m)) return;
    if (m.isNoteOn()) {
        synth_->noteOn(m.getNoteNumber(), m.getFloatVelocity());
        return;
    }
    if (m.isNoteOff()) {
        synth_->noteOff(m.getNoteNumber());
        return;
    }
    if (m.isAllNotesOff() || m.isAllSoundOff()) {
        synth_->allOff();
        resetMpeNotes();
        return;
    }
    if (m.isController()) {
        const int cc = m.getControllerNumber();
        const float v = static_cast<float>(m.getControllerValue());
        switch (cc) {
            case 64: perf_.sustain = v >= 64; break;
            case 66:
            case 67: perf_.brake = v >= 64; break;
            case 1: perf_.vibDepth = v / 127.0f * 30.0f; break;
            case 74: perf_.rotation = v / 127.0f * 2.0f - 1.0f; break;
            case 71:
                perf_.trimAmount = v / 127.0f;
                perf_.trimOn = v > 3;
                break;
            case 11: perf_.pressure = 0.2f + 1.8f * v / 127.0f; break;
            default: return;
        }
        pushPerf();
        return;
    }
    if (m.isChannelPressure()) {
        perf_.pressure = 0.2f + 1.8f * static_cast<float>(m.getChannelPressureValue()) / 127.0f;
        pushPerf();
        return;
    }
    if (m.isPitchWheel()) {
        bendWheel_ = static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f;
        perf_.bendCents = bendWheel_ * p_.bendRange->load();
        pushPerf();
        return;
    }
    if (m.isProgramChange()) {
        perf_.instrument = m.getProgramChangeNumber() % 2;
        pushPerf();
        pendingProgram_.store(perf_.instrument);  // the parameter follows on the message thread
    }
}

// ---------------------------------------------------------------- MPE

bool MagnetikaProcessor::isMpeMember(int ch) const {
    if (!mpeActive_) return false;
    return (lowerMembers_ > 0 && ch >= 2 && ch <= 1 + lowerMembers_) ||
           (upperMembers_ > 0 && ch <= 15 && ch >= 16 - upperMembers_);
}

// Registered parameters, only as far as MPE needs: RPN 6 on a zone's master
// channel is the MPE Configuration Message (data = member count, 0 = zone off);
// RPN 0 on a member channel sets the member pitch-bend range.
bool MagnetikaProcessor::handleRpn(int ch, int cc, int value) {
    if (ch < 1 || ch > 16) return false;
    const auto c = static_cast<size_t>(ch);
    if (cc == 101) { rpnMsb_[c] = value; return true; }
    if (cc == 100) { rpnLsb_[c] = value; return true; }
    if (cc == 99 || cc == 98) {  // an NRPN deselects any RPN, so its data cannot be misread as one
        rpnMsb_[c] = rpnLsb_[c] = 127;
        return true;
    }
    if (cc == 38) return true;  // data entry LSB: nothing here needs it
    if (cc != 6) return false;
    if (rpnMsb_[c] == 0 && rpnLsb_[c] == 6 && (ch == 1 || ch == 16)) {
        const int n = juce::jlimit(0, 15, value);
        int lower = lowerMembers_, upper = upperMembers_;
        // zones never overlap: a zone that takes channels shrinks the other one
        // (a zone switched off takes none, so "lower 15, upper 0" keeps all 15)
        if (ch == 1) {
            lower = n;
            if (n > 0) upper = std::min(upper, 14 - n);
        } else {
            upper = n;
            if (n > 0) lower = std::min(lower, 14 - n);
        }
        setZones(std::max(0, lower), std::max(0, upper));
        const bool on = lowerMembers_ > 0 || upperMembers_ > 0;
        if (on != mpeActive_) resetMpeNotes();
        mpeActive_ = on;
        pendingMpe_.store(on ? 1 : 0);  // the MPE switch follows on the message thread
    } else if (rpnMsb_[c] == 0 && rpnLsb_[c] == 0 && isMpeMember(ch)) {
        memberBendRange_ = static_cast<float>(juce::jlimit(1, 96, value));
        pendingMpeBendRange_.store(memberBendRange_);  // the parameter follows
    }
    return true;  // other RPNs (master bend range, tuning) are not used
}

void MagnetikaProcessor::setZones(int lower, int upper) {
    if (lower != lowerMembers_ || upper != upperMembers_) resetMpeNotes();  // channels changed meaning
    lowerMembers_ = lower;
    upperMembers_ = upper;
    zonesShared_.store(lower * 16 + upper);
}

// Per-note expression on a member channel. A note shared by two channels is one
// bowl (two fingers on one bowl): it sounds at the first and is released at the
// last; expression comes from whichever channel spoke last. On the bars every
// strike sounds, and the damper returns when the last channel lets go.
// MPE pressure is how firmly the finger presses, like bow force on a string. On
// rubbed glass the load decides whether the glass speaks and how (clean to
// pressed, choked), not how loud: measured on C4-C6 at 1 rev/s, a bowl catches at
// a load of about 0.94-1.04 and is within +-3 dB from there to 2.0; below, only
// friction hiss. So the curve puts a resting finger at a whisper (0.3), every
// note speaking by 30 % of the travel (1.1, past all measured catch points), and
// the rest of the travel for tone up to 2.0. Loudness belongs to the rub speed
// (slide). Units are BowlPhysics::velocityLoad's.
double MagnetikaProcessor::pressureToLoad(float pressure) {
    const double p = juce::jlimit(0.0, 1.0, static_cast<double>(pressure));
    return p < 0.3 ? 0.3 + (1.1 - 0.3) * p / 0.3 : 1.1 + (2.0 - 1.1) * (p - 0.3) / 0.7;
}

// The strike's velocity sets the first contact; pressure takes over once it
// reaches that load (so a hard strike does not collapse to a whisper because the
// controller's pressure starts at 0), and from then on rules, down to a whisper.
void MagnetikaProcessor::applyPressure(int ch) {
    auto& c = mpeCh_[static_cast<size_t>(ch)];
    if (c.note < 0 || c.pressure < 0 || perf_.instrument != 0) return;
    const double load = pressureToLoad(c.pressure);
    if (!c.pressing && load >= BowlPhysics::velocityLoad(c.velocity)) c.pressing = true;
    if (c.pressing) synth_->setNoteLoad(c.note, load);
}

// A bend past the coils' reach. A real armonica cannot glide: a glissando is the
// finger sliding across rims. So on the glass a long MPE bend hands the finger to
// the bowl nearest the requested pitch once it is more than 55 c (half a semitone
// plus 5 c, so it does not flicker at the boundary) from the current one; that
// bowl's coils supply the rest, up to the pitch-bend range, and the bowl left
// behind rings on. The bars cannot be glided onto without a strike: they clamp.
int MagnetikaProcessor::bowlFor(int ch) const {
    const auto& c = mpeCh_[static_cast<size_t>(ch)];
    if (c.base < 0) return c.note;
    if (perf_.instrument != 0 || mpeGlideRaw_->load() < 0.5f) return c.base;
    const float want = c.bend * memberBendRange_ * 100.0f;  // cents from the note given
    const int target = juce::jlimit(0, 127, c.base + static_cast<int>(std::lround(want / 100.0f)));
    if (c.note < 0) return target;  // a new note starts on the nearest bowl
    return std::abs(want - 100.0f * static_cast<float>(c.note - c.base)) > 55.0f ? target : c.note;
}

float MagnetikaProcessor::springCents(int ch) const {
    const auto& c = mpeCh_[static_cast<size_t>(ch)];
    const float range = p_.bendRange->load();  // what the coils' spring may do, cents
    const float want = c.bend * memberBendRange_ * 100.0f - 100.0f * static_cast<float>(c.note - c.base);
    return juce::jlimit(-range, range, want);
}

// After a bend: move the finger to another bowl if the glide asks for it, then
// let the coils bend the bowl it is on.
void MagnetikaProcessor::followGlide(int ch, bool mayHandOff) {
    auto& c = mpeCh_[static_cast<size_t>(ch)];
    if (c.note < 0) return;
    const int bowl = bowlFor(ch);
    // (after a switch to bars mid-glide: no strike, just bend)
    if (mayHandOff && bowl != c.note && perf_.instrument == 0) {
        auto& left = mpeHeld_[static_cast<size_t>(c.note)];
        if (left > 0) --left;
        if (left == 0) synth_->noteOff(c.note);  // it rings on, as a bowl does when the finger slides off
        c.note = bowl;
        auto& held = mpeHeld_[static_cast<size_t>(bowl)];
        if (held < 255) ++held;
        if (synth_->held(bowl)) return;  // joining a bowl another finger holds: its bend is left alone
        synth_->noteOn(bowl, c.velocity);
        applyPressure(ch);  // a finger already pressing keeps its load on the new bowl
        if (c.slide >= 0) applySlide(bowl, c.slide, true);
    }
    synth_->setNoteBend(c.note, springCents(ch));
}

// Per-note expression on a member channel. A note shared by two channels is one
// bowl (two fingers on one bowl): it sounds at the first and is released at the
// last; expression comes from whichever channel spoke last. On the bars every
// strike sounds, and the damper returns when the last channel lets go.
bool MagnetikaProcessor::handleMpeMember(int ch, const juce::MidiMessage& m) {
    auto& c = mpeCh_[static_cast<size_t>(ch)];
    const bool glass = perf_.instrument == 0;
    if (m.isNoteOn()) {
        const int midi = m.getNoteNumber();
        const bool again = c.base == midi && c.note >= 0;  // a repeated note-on is a re-strike, not another holder
        if (c.note >= 0 && !again) releaseMemberNote(ch, c.note);  // one note per channel
        c.base = midi;
        if (!again) {
            c.note = -1;
            c.note = bowlFor(ch);  // the controller may have bent the channel before the note-on
            auto& held = mpeHeld_[static_cast<size_t>(c.note)];
            if (held < 255) ++held;
        }
        const int bowl = c.note;
        const bool strike = !glass || !synth_->held(bowl);
        if (strike || !again) {  // a new contact: the strike sets the load until pressure overtakes it
            c.velocity = m.getFloatVelocity();
            c.pressing = false;
        }
        if (strike) {
            synth_->noteOn(bowl, c.velocity);
            // the controller sends a note's initial expression before its note-on
            synth_->setNoteBend(bowl, springCents(ch));
            applyPressure(ch);
            if (c.slide >= 0) applySlide(bowl, c.slide, true);
        }
        // joining a bowl another channel holds: its live expression is left alone
        return true;
    }
    if (m.isNoteOff()) {
        const int midi = m.getNoteNumber();
        releaseMemberNote(ch, c.base == midi && c.note >= 0 ? c.note : midi);  // the bowl a glide moved to
        return true;
    }
    if (m.isPitchWheel()) {
        c.bend = static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f;
        followGlide(ch);
        return true;
    }
    if (m.isChannelPressure() || m.isAftertouch()) {
        const int v = m.isChannelPressure() ? m.getChannelPressureValue() : m.getAfterTouchValue();
        c.pressure = static_cast<float>(v) / 127.0f;
        applyPressure(ch);
        return true;
    }
    if (m.isController()) {
        switch (m.getControllerNumber()) {
            case 74:
                c.slide = static_cast<float>(m.getControllerValue()) / 127.0f;
                if (c.note >= 0) applySlide(c.note, c.slide, false);
                return true;
            case 120:  // all sound off / all notes off on a member channel: this channel only
            case 123:
                if (c.note >= 0) releaseMemberNote(ch, c.note);
                return true;
            case 121:  // reset controllers: this channel's expression back to neutral
                c.bend = 0;
                c.pressure = c.slide = -1;
                c.pressing = false;
                if (c.note >= 0) {
                    followGlide(ch, false);  // the spring pulls toward the note given; no new attack
                    if (glass) {
                        synth_->setNoteTouch(c.note, 1);
                        applySlide(c.note, 0.5f, false);  // centre: rubs as without MPE
                    }
                }
                return true;
            default: return false;  // a pedal or other controller sent on a member channel: global
        }
    }
    return m.isProgramChange();  // a member channel cannot change the instrument
}

// A member channel lets go of `midi`. A note no channel counted (begun before MPE
// switched on or the zones changed) is released outright, so nothing can stick;
// a counted note another channel owns is left alone (a stray note-off).
void MagnetikaProcessor::releaseMemberNote(int ch, int midi) {
    auto& c = mpeCh_[static_cast<size_t>(ch)];
    auto& held = mpeHeld_[static_cast<size_t>(midi)];
    if (c.note == midi) {
        c.note = c.base = -1;
        if (held > 0) --held;
        if (held == 0) synth_->noteOff(midi);
    } else if (held == 0) {
        synth_->noteOff(midi);
    }
}

// Slide (CC74) is the rub speed, like bow speed: on rubbed glass it, not the
// finger load, sets the loudness. Measured on C4-C6: the level rises with the
// finger's speed relative to the rim up to about 4-6 rad/s, then the glass stops
// speaking (by 6-7 rad/s; C6 earliest). Centre plays as without MPE. On the bars,
// slide is where the next strike lands.
void MagnetikaProcessor::applySlide(int midi, float slide, bool atNoteOn) {
    if (perf_.instrument != 0) {
        if (atNoteOn) synth_->setNoteStrikePos(midi, slide);
        return;
    }
    const double s = juce::jlimit(0.0, 1.0, static_cast<double>(slide));
    const bool centre = std::abs(s - 0.5) < 0.01;
    // the limits were measured on the 45 mm bowl; what the glass tolerates is a rim
    // speed, so a bigger bowl reaches it at a lower angular speed
    const double k = revBRaw_->load() > 0.5f ? 45.0 / (Synth::revBDiameterMm(midi) / 2.0) : 1.0;
    // the same test the engine makes: a turning spindle, or a fixed bowl (spin <= 0.05)
    const double spin = spinRaw_->load();
    if (spindleOnRaw_->load() > 0.5f && spin > 0.05) {
        // spindle running: the finger moves with the glass (slower rub, quieter) or
        // against it (faster, louder), never past 5 rad/s relative unless the spindle
        // alone already is; centre is a still finger
        const double Om = 2 * kPi * spin, top = std::max(Om, 5.0 * k);
        const double rel = centre ? Om : s < 0.5 ? Om * (0.08 + 0.92 * s / 0.5) : Om + (top - Om) * (s - 0.5) / 0.5;
        synth_->setNoteFingerSpeed(midi, Om - rel);  // the engine rubs at |finger - rim|
    } else {
        // fixed bowl: bottom = a still finger (silent), centre = the finger-speed
        // setting, top = 4.5 rad/s, the loudest speed before any note stops speaking
        const double fs = fingerSpeedRaw_->load(), top = std::max(fs, 4.5 * k);
        synth_->setNoteFingerSpeed(midi, centre ? fs : s < 0.5 ? fs * s / 0.5 : fs + (top - fs) * (s - 0.5) / 0.5);
    }
}

void MagnetikaProcessor::resetMpeNotes() {
    // A finger that glided away from its note would never get its note-off: the
    // controller's note-off names the note it gave. Release those bowls now.
    if (synth_)
        for (const auto& c : mpeCh_)
            if (c.note >= 0 && c.base >= 0 && c.note != c.base) synth_->noteOff(c.note);
    mpeHeld_ = {};
    for (auto& c : mpeCh_) c.note = c.base = -1;
}

void MagnetikaProcessor::renderRange(juce::AudioBuffer<float>& buffer, int start, int end) {
    if (end <= start) return;
    if (buffer.getNumChannels() >= 2) {
        synth_->render(buffer.getWritePointer(0) + start, buffer.getWritePointer(1) + start, end - start);
        return;
    }
    // mono bus: render stereo into scratch in chunks, write the mid signal
    float* out = buffer.getWritePointer(0);
    for (int pos = start; pos < end;) {
        const int n = std::min(end - pos, static_cast<int>(scratchL_.size()));
        synth_->render(scratchL_.data(), scratchR_.data(), n);
        for (int i = 0; i < n; ++i) out[pos + i] = 0.5f * (scratchL_[static_cast<size_t>(i)] + scratchR_[static_cast<size_t>(i)]);
        pos += n;
    }
}

void MagnetikaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;  // braked tails can go denormal
    const int n = buffer.getNumSamples();
    if (!synth_) {  // no prepareToPlay yet
        buffer.clear();
        return;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    keyboard_.processNextMidiBuffer(midi, 0, n, true);  // on-screen keyboard notes
    applyParameters(false);
    drainUiEvents();

    // Sample-accurate: render up to each MIDI event, then apply it.
    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        renderRange(buffer, pos, at);
        pos = at;
        handleMidi(meta.getMessage());
    }
    renderRange(buffer, pos, n);
    publishDisplay(buffer);
}

void MagnetikaProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    if (const int z = zonesShared_.load(); z >= 0) state.setProperty("mpeZones", z, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MagnetikaProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts_.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xml);
            upgradeState(state);  // older projects keep their sound
            if (state.hasProperty("mpeZones")) pendingZones_.store(juce::jlimit(0, 255, static_cast<int>(state["mpeZones"])));
            apvts_.replaceState(state);
        }
}

juce::AudioProcessorEditor* MagnetikaProcessor::createEditor() { return new MagnetikaEditor(*this); }

} // namespace magnetika::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new magnetika::plugin::MagnetikaProcessor(); }
