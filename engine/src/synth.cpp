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
        if (idx >= kNumParams) continue;  // never index past the store
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
        for (int i = 0; i < count; ++i) v.set(values[i].id, vary(v.midi, values[i].id, values[i].value));
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

double Synth::revBDiameterMm(int midi) {
    // rev B bowl table, C4..C6 (instrument_schematic_revB, section 2)
    static constexpr double kRevB[25] = {147, 143, 139, 135, 131, 127, 123, 120, 117, 113, 110, 107, 104,
                                         101, 98,  95,  93,  90,  87,  85,  82,  80,  78,  76,  73};
    if (midi >= 60 && midi <= 84) return kRevB[midi - 60];
    return 104.0 * std::pow(2.0, (72 - midi) / 24.0);  // the table's sqrt 2 per octave, anchored at C5
}

namespace {
// A fixed pseudo-random number in [-1, 1] for (serial, note, slot): splitmix64.
double unitHash(std::uint32_t serial, int midi, int slot) {
    std::uint64_t z = (static_cast<std::uint64_t>(serial) << 32) ^ (static_cast<std::uint64_t>(midi & 0xff) << 8) ^
                      static_cast<std::uint64_t>(slot & 0xff);
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) / static_cast<double>(1ull << 52) - 1.0;
}
} // namespace

// Each varied quantity has its own fixed hash slot (not the ParamId's ordinal), so
// an instrument number keeps its sound when parameters are added in later versions.
double Synth::vary(int midi, ParamId id, double value) const {
    if (variation_ <= 0 || midi < 0) return value;
    auto u = [&](int slot) { return unitHash(serial_, midi, slot) * variation_; };
    switch (id) {
        case ParamId::f0: return value * std::pow(2.0, 3.0 * u(1) / 1200.0);  // +-3 c: the tuner's tolerance
        case ParamId::Q1: return value * (1 + 0.2 * u(2));                     // surface finish
        case ParamId::Q2: return value * (1 + 0.2 * u(3));
        case ParamId::Q3: return value * (1 + 0.2 * u(4));
        case ParamId::split: return value * (1 + 0.4 * u(5));                  // wall-thickness asymmetry
        case ParamId::kappa: return value * (1 + 0.15 * u(6));                 // Bryan factor
        case ParamId::r3: return value * (1 + 0.01 * u(7));                    // shell shape
        case ParamId::ratio: return value * (1 + 0.01 * u(8));                 // bar arching
        default: return value;
    }
}

double Synth::axisAngle(int midi) const {
    if (variation_ <= 0 || midi < 0) return 0.0;
    return unitHash(serial_, midi, 9) * variation_ * 3.14159265358979 / 4;  // +-45 deg
}

void Synth::make(Voice& v, int midi) {
    const Store& store = inst_ == Instrument::Glass ? glassP_ : barP_;
    v.kind = kindFor(inst_);
    if (v.kind == Voice::Kind::Bowl) v.bowl = BowlPhysics(sr_);
    else v.bar = BarPhysics(sr_);
    for (std::size_t i = 0; i < kNumParams; ++i)
        if (store.has.test(i)) v.set(static_cast<ParamId>(i), vary(midi, static_cast<ParamId>(i), store.value[i]));
    const double spread = (midi - 72) / 24.0 * 0.5;
    const auto listenIdx = static_cast<std::size_t>(ParamId::listen);
    const double baseListen = store.has.test(listenIdx) ? store.value[listenIdx] : 0.0;
    v.set(ParamId::f0, vary(midi, ParamId::f0, 440 * std::pow(2.0, (midi - 69) / 12.0)));
    v.set(ParamId::listen, baseListen + spread);
    if (v.kind == Voice::Kind::Bowl) {
        v.bowl.setNyquistGuard(nyquistGuard_);
        if (scaleModalMass_) v.bowl.setModalMass(0.02 * std::pow(2.0, (72 - midi) / 12.0));
        if (revBSizes_) v.bowl.setRimRadius(revBDiameterMm(midi) / 2000.0);
        if (variation_ > 0) v.bowl.setAxisAngle(axisAngle(midi));
    } else {
        v.bar.setNyquistGuard(nyquistGuard_);
        if (scaleModalMass_) v.bar.setModalMass(0.2 * std::pow(2.0, (72 - midi) / 24.0));
    }
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
        const bool diverged = !std::isfinite(v.env());
        if (diverged || (!v.held && v.env() < 1e-5 && !v.on())) {
            v.kind = Voice::Kind::None;
            v.midi = -1;
            v.held = false;
        }
    }
    for (int i = 0; i < n; ++i) {
        const double l = std::tanh(static_cast<double>(L[i])), r = std::tanh(static_cast<double>(R[i]));
        L[i] = std::isfinite(l) ? static_cast<float>(l) : 0.0f;
        R[i] = std::isfinite(r) ? static_cast<float>(r) : 0.0f;
    }
}

int Synth::activeVoices() const {
    int n = 0;
    for (int i = 0; i < numVoices_; ++i) n += voices_[i].kind != Voice::Kind::None;
    return n;
}

void Synth::releaseAll() {
    for (int i = 0; i < numVoices_; ++i) {
        Voice& v = voices_[i];
        if (v.kind == Voice::Kind::None || !v.held) continue;
        v.held = false;
        if (v.kind == Voice::Kind::Bowl) v.bowl.noteOff();
        else v.bar.noteOff();
    }
}

void Synth::setNoteTouch(int midi, double touch) {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind == Voice::Kind::Bowl && voices_[i].midi == midi) voices_[i].bowl.setTouch(touch);
}

void Synth::setNoteLoad(int midi, double load) {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind == Voice::Kind::Bowl && voices_[i].midi == midi) voices_[i].bowl.setLoad(load);
}

void Synth::setNoteFingerSpeed(int midi, double radPerSec) {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind == Voice::Kind::Bowl && voices_[i].midi == midi) voices_[i].bowl.setFingerSpeed(radPerSec);
}

void Synth::setNoteStrikePos(int midi, double pos) {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind == Voice::Kind::Bar && voices_[i].midi == midi) voices_[i].bar.p.strikePos = pos;
}

void Synth::setNoteBend(int midi, double cents) {
    for (int i = 0; i < numVoices_; ++i) {
        Voice& v = voices_[i];
        if (v.midi != midi) continue;
        if (v.kind == Voice::Kind::Bowl) v.bowl.setNoteBend(cents);
        else if (v.kind == Voice::Kind::Bar) v.bar.setNoteBend(cents);
    }
}

int Synth::snapshot(VoiceView* out, int max) const {
    int n = 0;
    for (int i = 0; i < numVoices_ && n < max; ++i) {
        const Voice& v = voices_[i];
        if (v.kind == Voice::Kind::None) continue;
        const bool glass = v.kind == Voice::Kind::Bowl;
        out[n++] = {v.midi, glass, v.held, static_cast<float>(v.env()),
                    glass ? static_cast<float>(v.bowl.patternAngle()) : 0.0f};
    }
    return n;
}

bool Synth::sounding(int midi) const { return envOf(midi) >= 0; }

bool Synth::held(int midi) const {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind == kindFor(inst_) && voices_[i].midi == midi && voices_[i].held) return true;
    return false;
}

double Synth::envOf(int midi) const {
    for (int i = 0; i < numVoices_; ++i)
        if (voices_[i].kind != Voice::Kind::None && voices_[i].midi == midi) return voices_[i].env();
    return -1;
}

} // namespace magnetika
