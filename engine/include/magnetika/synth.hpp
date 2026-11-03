#pragma once
// Polyphonic voice manager: port of Synth in reference/engine.mjs, with three
// deliberate departures, each off or unreachable when replaying the reference:
//
//   1. Voices are preallocated. A new note resets a voice in place instead of
//      allocating, so nothing allocates on the audio thread.
//   2. Stealing never takes a held voice while a released one exists. (The JS
//      reduce starts from voices[0] and can steal it even when it is held.)
//   3. `coilsArmed`: when true, the coils stay engaged at zero sustain gain with
//      no pedal down, so the electronic spring (vibrato, pitch bend) and trim work
//      without sustain. Off by default here so the engine matches the reference;
//      the plugin turns it on.

#include <array>
#include <bitset>
#include <cstdint>

#include "magnetika/bar.hpp"
#include "magnetika/bowl.hpp"
#include "magnetika/params.hpp"

namespace magnetika {

enum class Instrument : unsigned char { Glass, Bars };

class Synth {
public:
    static constexpr int kMaxVoices = 16;

    explicit Synth(double sampleRate = 48000.0, int voices = 8);

    void setInst(Instrument i) { inst_ = i; }
    Instrument inst() const { return inst_; }

    void setParams(Instrument which, const ParamValue* values, int count);
    void setPerf(const PerfValue* values, int count);
    void setCoilsArmed(bool armed);

    void noteOn(int midi, double vel);
    void noteOff(int midi);
    void allOff();

    // Renders n samples into L/R (overwrites), then the tanh output limiter.
    void render(float* L, float* R, int n);

    int activeVoices() const;
    bool sounding(int midi) const;  // a voice is allocated to this note

private:
    struct Store {  // the JS glassP / barP objects: only keys that were set
        std::array<double, kNumParams> value{};
        std::bitset<kNumParams> has;
    };
    struct Voice {
        enum class Kind : unsigned char { None, Bowl, Bar } kind = Kind::None;
        BowlPhysics bowl;
        BarPhysics bar;
        int midi = -1;
        std::uint64_t age = 0;
        bool held = false;

        double env() const { return kind == Kind::Bowl ? bowl.env() : bar.env(); }
        bool on() const { return kind == Kind::Bowl ? bowl.on() : bar.on(); }
        void set(ParamId id, double v) {
            if (kind == Kind::Bowl) bowl.set(id, v); else bar.set(id, v);
        }
    };

    static Voice::Kind kindFor(Instrument i) {
        return i == Instrument::Glass ? Voice::Kind::Bowl : Voice::Kind::Bar;
    }
    void make(Voice& v, int midi);
    void applyPerf(Voice& v);
    int stealIndex() const;

    double sr_;
    int numVoices_;
    std::array<Voice, kMaxVoices> voices_;
    Instrument inst_ = Instrument::Glass;
    Store glassP_, barP_;
    double sustain_ = 0, brake_ = 0, Gs_ = 0.8, Gb_ = 1.5;
    bool coilsArmed_ = false;
    std::uint64_t tick_ = 0;
    std::uint32_t notes_ = 0;
};

} // namespace magnetika
