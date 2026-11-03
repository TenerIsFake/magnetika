#include "magnetika/synth.hpp"

#include <cmath>

namespace magnetika {

Synth::Synth(double sampleRate, int voices)
    : sr_(sampleRate), numVoices_(voices < 1 ? 1 : (voices > kMaxVoices ? kMaxVoices : voices)) {
    for (auto& v : voices_) {
        v.bowl = BowlPhysics(sr_);
        v.bar = BarPhysics(sr_);
    }
}

void Synth::setParams(Instrument which, const ParamValue* values, int count) {
    Store& store = which == Instrument::Glass ? glassP_ : barP_;
    bool hasF0 = false, hasListen = false;
    double listen = 0;
    for (int i = 0; i < count; ++i) {
        const auto idx = static_cast<std::size_t>(values[i].id);
        store.value[idx] = values[i].value;
        store.has.set(idx);
        if (values[i].id == ParamId::f0) hasF0 = true;
        if (values[i].id == ParamId::listen) { hasListen = true; listen = values[i].value; }
    }
    // Live voices of that instrument take the change, but keep their own pitch and
    // their per-note listening angle (the JS restores f0 and re-applies the spread).
    const auto kind = kindFor(which);
    for (int vi = 0; vi < numVoices_; ++vi) {
        Voice& v = voices_[vi];
        if (v.kind != kind) continue;
        const double f0 = kind == Voice::Kind::Bowl ? v.bowl.p.f0 : v.bar.p.f0;
        for (int i = 0; i < count; ++i) v.set(values[i].id, values[i].value);
        if (hasF0) v.set(ParamId::f0, f0);
        if (hasListen) v.set(ParamId::listen, listen + (v.midi - 72) / 24.0 * 0.5);
    }
}

void Synth::setPerf(const PerfValue* values, int count) {
    for (int i = 0; i < count; ++i) {
        switch (values[i].id) {
            case PerfId::sustain: sustain_ = values[i].value; break;
            case PerfId::brake: brake_ = values[i].value; break;
            case PerfId::Gs: Gs_ = values[i].value; break;
            case PerfId::Gb: Gb_ = values[i].value; break;
            default: break;
        }
    }
    for (int vi = 0; vi < numVoices_; ++vi)
        if (voices_[vi].kind != Voice::Kind::None) applyPerf(voices_[vi]);
}

void Synth::setCoilsArmed(bool armed) {
    coilsArmed_ = armed;
    for (int vi = 0; vi < numVoices_; ++vi)
        if (voices_[vi].kind != Voice::Kind::None) applyPerf(voices_[vi]);
}

void Synth::applyPerf(Voice& v) {
    const bool pedal = sustain_ != 0 || brake_ != 0;
    const double driveOn = (pedal || coilsArmed_) ? 1 : 0;
    // With no pedal but armed coils, sustain gain is zero: the drive does nothing,
    // but the electronic spring and trim (which live inside the drive) are active.
    // Unarmed, G is Gs exactly as in the JS (unused while driveOn is 0).
    const double G = brake_ != 0 ? -Gb_ : ((sustain_ != 0 || !coilsArmed_) ? Gs_ : 0.0);
    v.set(ParamId::driveOn, driveOn);
    v.set(ParamId::G, G);
}

void Synth::make(Voice& v, int midi) {
    const Store& store = inst_ == Instrument::Glass ? glassP_ : barP_;
    v.kind = kindFor(inst_);
    if (v.kind == Voice::Kind::Bowl) v.bowl = BowlPhysics(sr_);
    else v.bar = BarPhysics(sr_);
    for (std::size_t i = 0; i < kNumParams; ++i)
        if (store.has.test(i)) v.set(static_cast<ParamId>(i), store.value[i]);
    const double spread = (midi - 72) / 24.0 * 0.5;
    const auto listenIdx = static_cast<std::size_t>(ParamId::listen);
    const double baseListen = store.has.test(listenIdx) ? store.value[listenIdx] : 0.0;
    v.set(ParamId::f0, 440 * std::pow(2.0, (midi - 69) / 12.0));
    v.set(ParamId::listen, baseListen + spread);
}

int Synth::stealIndex() const {
    int best = -1;
    bool bestReleased = false;
    for (int i = 0; i < numVoices_; ++i) {
        const Voice& v = voices_[i];
        const bool released = !v.held;
        if (best < 0 || (released && !bestReleased) ||
            (released == bestReleased && v.env() < voices_[best].env())) {
            best = i;
            bestReleased = released;
        }
    }
    return best;
}

void Synth::noteOn(int midi, double vel) {
    const auto want = kindFor(inst_);
    Voice* v = nullptr;
    for (int i = 0; i < numVoices_ && !v; ++i)
        if (voices_[i].midi == midi && voices_[i].kind == want) v = &voices_[i];
    if (!v) {
        for (int i = 0; i < numVoices_ && !v; ++i)
            if (voices_[i].kind == Voice::Kind::None) v = &voices_[i];
        if (!v) v = &voices_[stealIndex()];
        make(*v, midi);
        v->midi = midi;
        ++notes_;
        // JS: seed(Math.imul(midi+1, 2654435761) ^ notes)
        if (v->kind == Voice::Kind::Bowl)
            v->bowl.seed(static_cast<std::uint32_t>(midi + 1) * 2654435761u ^ notes_);
    }
    v->age = tick_;
    v->held = true;
    applyPerf(*v);
    if (v->kind == Voice::Kind::Bowl) v->bowl.noteOn(vel);
    else v->bar.noteOn(vel);
}

void Synth::noteOff(int midi) {
    for (int i = 0; i < numVoices_; ++i) {
        Voice& v = voices_[i];
        if (v.midi != midi || v.kind == Voice::Kind::None) continue;
        v.held = false;
        if (v.kind == Voice::Kind::Bowl) v.bowl.noteOff();
        else v.bar.noteOff();
    }
}

void Synth::allOff() {
    for (int i = 0; i < numVoices_; ++i) {
        Voice& v = voices_[i];
        if (v.kind == Voice::Kind::None) continue;
        v.held = false;
        if (v.kind == Voice::Kind::Bowl) v.bowl.silence();
        else v.bar.silence();
        v.kind = Voice::Kind::None;
        v.midi = -1;
    }
}

void Synth::render(float* L, float* R, int n) {
    for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
    ++tick_;
    for (int vi = 0; vi < numVoices_; ++vi) {
        Voice& v = voices_[vi];
        if (v.kind == Voice::Kind::None) continue;
        if (v.kind == Voice::Kind::Bowl) v.bowl.render(L, R, n, true);
        else v.bar.render(L, R, n, true);
        if (!v.held && v.env() < 1e-5 && !v.on()) {
            v.kind = Voice::Kind::None;
            v.midi = -1;
        }
    }
    for (int i = 0; i < n; ++i) {
        L[i] = static_cast<float>(std::tanh(static_cast<double>(L[i])));
        R[i] = static_cast<float>(std::tanh(static_cast<double>(R[i])));
    }
}

int Synth::activeVoices() const {
    int n = 0;
    for (int i = 0; i < numVoices_; ++i) n += voices_[i].kind != Voice::Kind::None;
    return n;
}

bool Synth::sounding(int midi) const {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind != Voice::Kind::None && voices_[i].midi == midi) return true;
    return false;
}

} // namespace magnetika
