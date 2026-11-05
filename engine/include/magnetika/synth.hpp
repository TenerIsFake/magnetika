#pragma once
// Polyphonic voice manager: port of Synth in reference/engine.mjs, with these
// deliberate departures, each off or unreachable when replaying the reference:
//
//   1. Voices are preallocated. A new note resets a voice in place instead of
//      allocating, so nothing allocates on the audio thread.
//   2. Stealing takes the quietest released voice; only when every voice is held
//      does it take the quietest held one. (The JS reduce starts from voices[0]
//      and, with all voices held, always steals voices[0] whatever its level.)
//   3. `coilsArmed`: when true, the coils stay engaged at zero sustain gain with
//      no pedal down, so the electronic spring (vibrato, pitch bend) and trim work
//      without sustain. Off by default here so the engine matches the reference;
//      the plugin turns it on. Turning it off restores the JS *parameters*; drive
//      state accumulated while armed (vibrato phase, drive-axis angle) carries over.
//   5. `scaleModalMass` (off by default): modal mass follows the rev B tables
//      instead of the JS's fixed C5-sized mass. Glass doubles per octave down
//      (149 / 75 / 37 g at C4 / C5 / C6), bars by sqrt(2) per octave (511 / 361 /
//      255 g), both anchored at C5 = MIDI 72 to the JS values (20 g, 200 g), so C5
//      is unchanged. Heavier low notes start slower, brake slower, bend less.
//   6. `nyquistGuard` (on by default; it cannot trigger in the reference range): a
//      mode above Nyquist is muted in the output, decided per note at noteOn.
//   7. The `damperT60` parameter (0 by default): a damping tail after release,
//      see BowlPhysics::step.
//   4. A voice whose state goes non-finite is freed and the output sample zeroed,
//      instead of poisoning the mix for good. (With the substep fix in the physics
//      this should not happen; it is a guard, not a mechanism.)
//
// Not thread-safe: call everything from the audio thread (a plugin queues
// parameter and note events and applies them between render calls).

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
    // These two take effect from the next note; sounding notes keep what they had.
    void setScaleModalMass(bool on) { scaleModalMass_ = on; }
    void setNyquistGuard(bool on) { nyquistGuard_ = on; }

    void noteOn(int midi, double vel);
    void noteOff(int midi);
    void allOff();
    // Releases every held note (as noteOff on each), leaving tails to ring.
    void releaseAll();

    // Renders n samples into L/R (overwrites), then the tanh output limiter.
    void render(float* L, float* R, int n);

    int activeVoices() const;
    bool sounding(int midi) const;  // a voice is allocated to this note
    bool held(int midi) const;      // the current instrument holds this note (key or finger down)
    double envOf(int midi) const;   // its envelope follower, or -1 if none

    // Touch interface for an editor (see BowlPhysics::setTouch / setFingerSpeed).
    // Apply to the glass voice playing `midi`, if any; bars ignore them.
    void setNoteTouch(int midi, double touch);
    void setNoteLoad(int midi, double load);  // BowlPhysics::setLoad
    void setNoteFingerSpeed(int midi, double radPerSec);
    // Bars: where this strike lands, 0 = centre .. 1 = end, overriding the
    // strikePos parameter for this voice only (call right after noteOn).
    void setNoteStrikePos(int midi, double pos);
    // Per-note pitch bend in cents for the voice playing `midi` (glass or bars), on
    // top of the shared bend: MPE. The coils' spring sets how far it can go.
    void setNoteBend(int midi, double cents);

    // What an editor draws: one entry per allocated voice. Read on the audio
    // thread after render() and hand across to the UI by the plugin.
    struct VoiceView {
        int midi;
        bool glass;
        bool held;     // key or finger still down
        float level;   // envelope follower, linear (about 0..0.5 at full drive)
        float pattern; // glass: (2,0) pattern orientation, radians; bars: 0
    };
    int snapshot(VoiceView* out, int max) const;

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
    bool scaleModalMass_ = false;
    bool nyquistGuard_ = true;
    std::uint64_t tick_ = 0;
    std::uint32_t notes_ = 0;
};

} // namespace magnetika
