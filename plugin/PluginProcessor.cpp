#include "PluginProcessor.h"

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
    startTimerHz(20);
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
            case UiEvent::AllOff: synth_->allOff(); break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i) apply(uiBuf_[static_cast<size_t>(scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) apply(uiBuf_[static_cast<size_t>(scope.startIndex2 + i)]);
    if (allOff) synth_->allOff();
    else if (releaseAll) synth_->releaseAll();
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
    if (auto xml = apvts_.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void MagnetikaProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts_.state.getType())) apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MagnetikaProcessor::createEditor() { return new MagnetikaEditor(*this); }

} // namespace magnetika::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new magnetika::plugin::MagnetikaProcessor(); }
