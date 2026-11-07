// Tests for the behaviour the JS reference does not have, so the reference
// comparison cannot cover it: "coils always armed" and voice stealing.

#include <cmath>
#include <functional>
#include <cstdio>
#include <vector>

#include "magnetika/synth.hpp"

using namespace magnetika;

namespace {

int failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++failures;                                               \
            std::printf("FAIL %s:%d  %s\n  ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

constexpr double SR = 48000;
constexpr int BLOCK = 128;

// The player's slider defaults (reference/player-defaults.json).
void playerDefaults(Synth& s) {
    const ParamValue glass[] = {
        {ParamId::Q2, 3500}, {ParamId::split, 0.8}, {ParamId::r3, 2.78}, {ParamId::Q3, 2800},
        {ParamId::N, 2.5}, {ParamId::dmu, 0.45}, {ParamId::vs, 0.2}, {ParamId::breath, 0.3},
        {ParamId::fingerSpeed, 3.8}, {ParamId::Fmax, 0.06}, {ParamId::vset, 0.3}, {ParamId::vibRate, 5.5},
        {ParamId::Fvib, 0.3}, {ParamId::spin, 1}, {ParamId::kappa, 0.35}, {ParamId::listen, 0}, {ParamId::master, 0.6},
    };
    s.setParams(Instrument::Glass, glass, static_cast<int>(sizeof glass / sizeof glass[0]));
    const PerfValue perf[] = {{PerfId::sustain, 0}, {PerfId::brake, 0}, {PerfId::Gs, 0.8}, {PerfId::Gb, 1.5}};
    s.setPerf(perf, 4);
}

std::vector<float> renderLeft(Synth& s, double seconds) {
    const int n = static_cast<int>(seconds * SR) / BLOCK * BLOCK;
    std::vector<float> L(static_cast<size_t>(n)), R(static_cast<size_t>(n));
    for (int i = 0; i < n; i += BLOCK) s.render(&L[static_cast<size_t>(i)], &R[static_cast<size_t>(i)], BLOCK);
    return L;
}

// Mean frequency by interpolated upward zero crossings over [a, b) seconds.
double freq(const std::vector<float>& x, double a, double b) {
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = static_cast<size_t>(a * SR) + 1; i < static_cast<size_t>(b * SR) && i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = static_cast<double>(i - 1) + -x[i - 1] / (x[i] - x[i - 1]);
            if (first < 0) first = t;
            last = t;
            ++n;
        }
    return n > 1 ? (n - 1) * SR / (last - first) : 0;
}

double cents(double f, double ref) { return 1200 * std::log2(f / ref); }

std::vector<float> glassNote(bool armed, double bendCents, double seconds, double releaseAt) {
    Synth s(SR);
    playerDefaults(s);
    s.setCoilsArmed(armed);
    const ParamValue bend[] = {{ParamId::bend, bendCents}};
    s.setParams(Instrument::Glass, bend, 1);
    s.noteOn(72, 0.8);
    auto a = renderLeft(s, releaseAt);
    s.noteOff(72);
    auto b = renderLeft(s, seconds - releaseAt);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

void testArmedIsInertWithNothingToDo() {
    // Armed coils with no pedal and no spring/trim demand must not change the sound
    // at all: the drive runs at zero gain. Bit-identical, not merely close.
    const auto off = glassNote(false, 0, 2.0, 1.0);
    const auto on = glassNote(true, 0, 2.0, 1.0);
    size_t diff = 0;
    for (size_t i = 0; i < off.size(); ++i) diff += off[i] != on[i];
    CHECK(diff == 0, "%zu of %zu samples differ", diff, off.size());
}

void testBendNeedsArmedCoilsWithoutSustain() {
    const double ref = freq(glassNote(false, 0, 2.0, 1.5), 0.6, 1.4);
    const double unarmed = freq(glassNote(false, 30, 2.0, 1.5), 0.6, 1.4);
    const double armed = freq(glassNote(true, 30, 2.0, 1.5), 0.6, 1.4);
    // Unarmed: faithful to the hardware model, bend has no coils to act through.
    CHECK(std::fabs(cents(unarmed, ref)) < 0.5, "unarmed bend moved pitch %.2f c", cents(unarmed, ref));
    // Armed: the spring bends upward. Under a rubbing finger the response is not
    // linear in the requested cents: the JS engine with driveOn=1, G=0 measures
    // +7.9 / +21.0 / +36.7 c for 10 / 20 / 30 c asked (stick-slip and spring
    // interact). This port reproduces +36.7 c exactly, so the bounds are loose
    // on purpose: they test "armed bend works", not a calibration.
    CHECK(cents(armed, ref) > 15, "armed bend only %.2f c", cents(armed, ref));
    CHECK(cents(armed, ref) < 60, "armed bend implausible: %.2f c", cents(armed, ref));
}

void testArmedDoesNotSustain() {
    // Armed is not sustain: after release the note must still decay.
    const auto x = glassNote(true, 0, 4.0, 1.0);
    auto rms = [&](double a, double b) {
        double s = 0;
        const size_t i0 = static_cast<size_t>(a * SR), i1 = static_cast<size_t>(b * SR);
        for (size_t i = i0; i < i1; ++i) s += static_cast<double>(x[i]) * x[i];
        return std::sqrt(s / static_cast<double>(i1 - i0));
    };
    const double early = rms(1.5, 2.0), late = rms(3.5, 4.0);
    CHECK(late < early * 0.6, "no decay after release: early %.4g late %.4g", early, late);
}

void testStealPrefersReleasedVoice() {
    Synth s(SR);  // 8 voices
    playerDefaults(s);
    for (int m = 60; m < 67; ++m) s.noteOn(m, 0.8);  // 7 held
    s.noteOn(67, 0.8);
    renderLeft(s, 0.3);
    s.noteOff(67);  // released but still ringing: all 8 voices in use
    renderLeft(s, 0.05);
    CHECK(s.activeVoices() == 8, "expected 8 active, got %d", s.activeVoices());
    s.noteOn(70, 0.8);
    for (int m = 60; m < 67; ++m) CHECK(s.sounding(m), "held note %d was stolen", m);
    CHECK(!s.sounding(67), "released note 67 should have been stolen");
    CHECK(s.sounding(70), "new note 70 not allocated");
}

void testStealWhenAllHeld() {
    // All held: the quietest held voice goes (the JS always took voices[0]).
    Synth s(SR);
    playerDefaults(s);
    for (int m = 60; m < 68; ++m) s.noteOn(m, m == 64 ? 0.05 : 0.9);  // 64 is played softly
    renderLeft(s, 0.2);
    int quietest = -1;
    for (int m = 60; m < 68; ++m)
        if (quietest < 0 || s.envOf(m) < s.envOf(quietest)) quietest = m;
    s.noteOn(72, 0.8);  // must still get a voice
    int held = 0;
    for (int m = 60; m < 68; ++m) held += s.sounding(m);
    CHECK(s.sounding(72), "new note not allocated when all voices held");
    CHECK(held == 7, "expected exactly one held note stolen, %d remain", held);
    CHECK(!s.sounding(quietest), "stole a louder voice; the quietest was %d", quietest);
}

// Every MIDI note, both instruments, two sample rates: the output stays finite and
// released voices free. The JS diverges to NaN for bars >= 119 and glass >= 125 at
// 48 kHz; the substep fix must hold across the whole keyboard.
void testWholeKeyboardStaysFinite() {
    for (double sr : {44100.0, 48000.0}) {
        for (Instrument inst : {Instrument::Glass, Instrument::Bars}) {
            int bad = 0, firstBad = -1, dropped = 0, firstDropped = -1;
            for (int m = 0; m <= 127; ++m) {
                Synth s(sr);
                playerDefaults(s);
                s.setInst(inst);
                const PerfValue sus[] = {{PerfId::sustain, 1}};
                s.setPerf(sus, 1);  // coils on: the hardest case for stability
                s.noteOn(m, 1.0);
                std::vector<float> L(BLOCK), R(BLOCK);
                bool finite = true;
                const int blocks = static_cast<int>(0.5 * sr) / BLOCK;
                for (int b = 0; b < blocks; ++b) {
                    s.render(L.data(), R.data(), BLOCK);
                    for (int i = 0; i < BLOCK; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
                }
                // Still held: if the voice had diverged, the NaN guard would have freed it.
                // So this, not the finite output, is what proves the substep fix works.
                if (!s.sounding(m)) {
                    ++dropped;
                    if (firstDropped < 0) firstDropped = m;
                }
                s.noteOff(m);
                const PerfValue brake[] = {{PerfId::sustain, 0}, {PerfId::brake, 1}};
                s.setPerf(brake, 2);
                for (int b = 0; b < static_cast<int>(1 * sr) / BLOCK; ++b) {
                    s.render(L.data(), R.data(), BLOCK);
                    for (int i = 0; i < BLOCK; ++i) finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]);
                }
                if (!finite) {
                    ++bad;
                    if (firstBad < 0) firstBad = m;
                }
                // Deliberately NOT checked: that every released voice frees within the
                // window. With constant Q the ring time is Q/(pi f), about 17 s at C2,
                // and the brake is force-limited, so low notes legitimately ring on.
                // (Measured 2026-11-03: braked glass frees from about MIDI 64 up.)
            }
            const char* name = inst == Instrument::Glass ? "glass" : "bars";
            CHECK(bad == 0, "%s at %.0f Hz: %d notes non-finite, first MIDI %d", name, sr, bad, firstBad);
            CHECK(dropped == 0, "%s at %.0f Hz: %d held notes lost their voice (diverged?), first MIDI %d",
                  name, sr, dropped, firstDropped);
        }
    }
}

void testVoicesFreeAfterDecay() {
    Synth s(SR);
    playerDefaults(s);
    s.noteOn(72, 0.8);
    renderLeft(s, 0.5);
    s.noteOff(72);
    const ParamValue q[] = {{ParamId::Q2, 200}, {ParamId::Q3, 200}};  // decay fast
    s.setParams(Instrument::Glass, q, 2);
    renderLeft(s, 3.0);
    CHECK(s.activeVoices() == 0, "voice not freed after decay: %d active", s.activeVoices());
}

// ---------------------------------------------------------------- plugin additions

struct Stereo {
    std::vector<float> L, R;
    // energy over [a, b) seconds, both channels (the pattern turns past the ears,
    // so one channel alone swings with the precession)
    double energyDb(double a, double b) const {
        double s = 0;
        const size_t i0 = static_cast<size_t>(a * SR), i1 = static_cast<size_t>(b * SR);
        for (size_t i = i0; i < i1 && i < L.size(); ++i)
            s += static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i];
        return 10 * std::log10(s / static_cast<double>(i1 - i0) + 1e-30);
    }
};

struct NoteSpec {
    Instrument inst = Instrument::Glass;
    int midi = 72;
    double damperT60 = 0;
    bool sustain = false, scaleMass = false, brakeAtRelease = false, guard = true;
    double seconds = 3.0, releaseAt = 1.0;
};

Stereo playNote(const NoteSpec& n) {
    Synth s(SR);
    playerDefaults(s);
    s.setInst(n.inst);
    s.setScaleModalMass(n.scaleMass);
    s.setNyquistGuard(n.guard);
    const ParamValue d[] = {{ParamId::damperT60, n.damperT60}};
    s.setParams(n.inst, d, 1);
    if (n.sustain) {
        const PerfValue sus[] = {{PerfId::sustain, 1}};
        s.setPerf(sus, 1);
    }
    s.noteOn(n.midi, 0.8);
    Stereo out;
    const int total = static_cast<int>(n.seconds * SR) / BLOCK * BLOCK;
    const int release = static_cast<int>(n.releaseAt * SR) / BLOCK * BLOCK;
    out.L.resize(static_cast<size_t>(total));
    out.R.resize(static_cast<size_t>(total));
    for (int i = 0; i < total; i += BLOCK) {
        if (i == release) {
            s.noteOff(n.midi);
            if (n.brakeAtRelease) {
                const PerfValue br[] = {{PerfId::sustain, 0}, {PerfId::brake, 1}};
                s.setPerf(br, 2);
            }
        }
        s.render(&out.L[static_cast<size_t>(i)], &out.R[static_cast<size_t>(i)], BLOCK);
    }
    return out;
}

bool identical(const Stereo& a, const Stereo& b) { return a.L == b.L && a.R == b.R; }

void testDamperSetsTheTail() {
    // C3 is the long-tail case (Q/(pi f) is several seconds); C5 for contrast.
    for (int midi : {48, 72}) {
        NoteSpec n;
        n.midi = midi;
        n.seconds = 3.0;
        n.releaseAt = 1.0;
        const Stereo free = playNote(n);
        n.damperT60 = 1.5;
        const Stereo damped = playNote(n);
        // Over the 1.5 s after the damper has engaged (it ramps in over ~30 ms),
        // a 1.5 s T60 should take about 60 dB off. Wide bounds: the envelope swings
        // as the pattern precesses past the ears.
        const double drop = damped.energyDb(1.05, 1.25) - damped.energyDb(2.55, 2.75);
        const double freeDrop = free.energyDb(1.05, 1.25) - free.energyDb(2.55, 2.75);
        CHECK(drop > 45 && drop < 80, "MIDI %d: damper T60 1.5 s dropped %.1f dB in 1.5 s (want ~60)", midi, drop);
        CHECK(drop > freeDrop + 20, "MIDI %d: damper barely beats free decay (%.1f vs %.1f dB)", midi, drop, freeDrop);
        // Before release the damper must not touch anything.
        bool sameBefore = true;
        for (size_t i = 0; i < static_cast<size_t>(1.0 * SR) / BLOCK * BLOCK; ++i)
            sameBefore = sameBefore && free.L[i] == damped.L[i] && free.R[i] == damped.R[i];
        CHECK(sameBefore, "MIDI %d: damper changed the sound while the note was held", midi);
    }
}

void testDamperYieldsToSustain() {
    // With the sustain coils holding the released note, the damper stays off:
    // bit-identical to no damper at all.
    NoteSpec n;
    n.sustain = true;
    const Stereo a = playNote(n);
    n.damperT60 = 0.5;
    CHECK(identical(a, playNote(n)), "damper acted while sustain held the note");
}

// Frequency of the strongest component within +-8 % of `guess` over [a, b)
// seconds: a Hann-windowed DFT, scanned in 1 Hz steps, then 0.01 Hz around the
// best. Robust where zero crossings are not (friction noise, or the swirl
// nulling one ear).
double peakFreq(const std::vector<float>& x, double a, double b, double guess) {
    const size_t i0 = static_cast<size_t>(a * SR), i1 = std::min(x.size(), static_cast<size_t>(b * SR));
    const double n = static_cast<double>(i1 - i0), twoPi = 2 * 3.14159265358979;
    std::vector<double> w(i1 - i0);
    for (size_t i = 0; i < w.size(); ++i) w[i] = (0.5 - 0.5 * std::cos(twoPi * static_cast<double>(i) / n)) * x[i0 + i];
    auto power = [&](double f) {
        const double c = std::cos(twoPi * f / SR), sn = std::sin(twoPi * f / SR);
        double pr = 1, pi = 0, re = 0, im = 0;  // phasor e^{-jwk} by rotation
        for (double v : w) {
            re += v * pr;
            im += v * pi;
            const double nr = pr * c + pi * sn;
            pi = pi * c - pr * sn;
            pr = nr;
        }
        return re * re + im * im;
    };
    auto scan = [&](double lo, double hi, double step) {
        double best = -1, bestF = lo;
        for (double f = lo; f <= hi; f += step)
            if (const double pw = power(f); pw > best) { best = pw; bestF = f; }
        return bestF;
    };
    const double coarse = scan(guess * 0.92, guess * 1.08, 1.0);
    return scan(coarse - 1.5, coarse + 1.5, 0.01);
}

void testNoteBend() {
    // MPE per-note bend: through the same spring as the shared bend, so a per-note
    // 30 c bend on an armed glass note moves its pitch as the shared 30 c does.
    constexpr double C5 = 523.251;
    auto run = [](double shared, double perNote, int bentMidi) {
        Synth s(SR);
        playerDefaults(s);
        s.setCoilsArmed(true);
        const ParamValue bend[] = {{ParamId::bend, shared}};
        s.setParams(Instrument::Glass, bend, 1);
        s.noteOn(72, 0.8);
        s.setNoteBend(bentMidi, perNote);
        return renderLeft(s, 1.5);
    };
    const auto plain = run(0, 0, 72), viaShared = run(30, 0, 72), viaNote = run(0, 30, 72), other = run(0, 30, 76);
    const double ref = peakFreq(plain, 0.6, 1.4, C5);
    const double byNote = cents(peakFreq(viaNote, 0.6, 1.4, C5), ref), byShared = cents(peakFreq(viaShared, 0.6, 1.4, C5), ref);
    CHECK(std::fabs(byNote - byShared) < 0.5, "per-note bend %.2f c differs from the shared bend %.2f c", byNote, byShared);
    CHECK(byNote > 15 && byNote < 60, "per-note bend moved pitch %.2f c", byNote);
    CHECK(other == plain, "bending another note (not sounding) changed this one");
    // a new note on the bowl starts unbent: a retrigger after a bent note sounds
    // like a retrigger after an unbent one (same sequence, bend 0)
    auto retrigger = [](double firstBend) {
        Synth s(SR);
        playerDefaults(s);
        s.setCoilsArmed(true);
        s.noteOn(72, 0.8);
        s.setNoteBend(72, firstBend);
        renderLeft(s, 0.5);
        s.noteOff(72);
        renderLeft(s, 0.1);
        s.noteOn(72, 0.8);
        return renderLeft(s, 1.5);
    };
    const double carried = cents(peakFreq(retrigger(30), 0.6, 1.4, C5), peakFreq(retrigger(0), 0.6, 1.4, C5));
    CHECK(std::fabs(carried) < 3, "per-note bend carried into the next note: %.2f c", carried);
    // and bars bend too
    auto bar = [](double perNote) {
        Synth s(SR);
        playerDefaults(s);
        s.setInst(Instrument::Bars);
        s.setCoilsArmed(true);
        s.noteOn(72, 0.8);
        s.setNoteBend(72, perNote);
        return renderLeft(s, 1.0);
    };
    const double barShift = cents(peakFreq(bar(30), 0.3, 0.9, C5), peakFreq(bar(0), 0.3, 0.9, C5));
    CHECK(barShift > 15 && barShift < 60, "bars per-note bend moved pitch %.2f c", barShift);
}

void testDamperOnBars() {
    NoteSpec n;
    n.inst = Instrument::Bars;
    n.midi = 60;
    const Stereo free = playNote(n);
    n.damperT60 = 1.0;
    const Stereo damped = playNote(n);
    const double drop = damped.energyDb(1.05, 1.25) - damped.energyDb(2.05, 2.25);
    CHECK(drop > 40 && drop < 80, "bars: damper T60 1.0 s dropped %.1f dB in 1 s (want ~60)", drop);
    CHECK(free.energyDb(2.05, 2.25) > damped.energyDb(2.05, 2.25) + 20, "bars: damper had no effect");
}

void testModalMassScaling() {
    // Anchored at C5: scaling must leave MIDI 72 bit-identical, glass and bars.
    for (Instrument inst : {Instrument::Glass, Instrument::Bars}) {
        NoteSpec n;
        n.inst = inst;
        n.midi = 72;
        const Stereo a = playNote(n);
        n.scaleMass = true;
        CHECK(identical(a, playNote(n)), "%s: mass scaling changed C5",
              inst == Instrument::Glass ? "glass" : "bars");
    }
    // A heavier low bowl under the same finger and coils builds up more slowly.
    // Measured 2026-11-03: at C3 with scaling (80 g) the note is still at the
    // friction-noise level (-35 dB) after 1 s, where the 20 g bowl reaches -20 dB.
    // That is why scaling is off by default: the finger and coil forces do not
    // scale with the bowl, and how hard a player presses a big bowl is not modelled.
    // (A brake comparison is confounded by this: the heavy note never got going.)
    NoteSpec n;
    n.midi = 48;
    n.sustain = true;
    n.seconds = 1.0;
    n.releaseAt = 1.0;
    const double lightLevel = playNote(n).energyDb(0.75, 1.0);
    n.scaleMass = true;
    const double heavyLevel = playNote(n).energyDb(0.75, 1.0);
    CHECK(heavyLevel < lightLevel - 6, "C3: scaled (heavier) bowl built up as fast as the 20 g one (%.1f vs %.1f dB)",
          heavyLevel, lightLevel);
}

// Energy between f1 and f2 Hz, by direct DFT over n samples starting at t0.
double bandDb(const std::vector<float>& x, double t0, int n, double f1, double f2) {
    const size_t i0 = static_cast<size_t>(t0 * SR);
    double e = 0;
    for (int k = static_cast<int>(f1 * n / SR); k <= static_cast<int>(f2 * n / SR); ++k) {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / n);  // Hann
            const double ph = 2 * 3.141592653589793 * k * i / n;
            re += w * x[i0 + static_cast<size_t>(i)] * std::cos(ph);
            im -= w * x[i0 + static_cast<size_t>(i)] * std::sin(ph);
        }
        e += re * re + im * im;
    }
    return 10 * std::log10(e + 1e-30);
}

void testNyquistGuard() {
    // Bars MIDI 120: f0 8.37 kHz, the 4x partial at 33.5 kHz aliases into the
    // 12-20 kHz band at 48 kHz. Guarded, that band should be empty.
    NoteSpec n;
    n.inst = Instrument::Bars;
    n.midi = 120;
    n.seconds = 0.5;
    n.releaseAt = 0.5;
    n.guard = false;
    const double aliased = bandDb(playNote(n).L, 0.05, 4096, 12000, 20000);
    n.guard = true;
    const double guarded = bandDb(playNote(n).L, 0.05, 4096, 12000, 20000);
    CHECK(aliased - guarded > 30, "bars MIDI 120: guard removed only %.1f dB of alias", aliased - guarded);

    // Per-mode decisions, straight from the physics.
    BowlPhysics hi(SR);
    hi.p.f0 = 10548;  // MIDI 124: (3,0) at 29.3 kHz
    hi.noteOn(1.0);
    CHECK(hi.partialAudible(2) && !hi.partialAudible(3), "MIDI 124: expected (2,0) kept, (3,0) muted");
    BowlPhysics c6(SR);
    c6.p.f0 = 1046.5;  // top of the real instrument
    c6.noteOn(1.0);
    CHECK(c6.partialAudible(2) && c6.partialAudible(3), "C6 must keep both pairs");
    // The limit is 24 kHz / 1.05 = 22.86 kHz (room for +40 c bend and vibrato).
    BarPhysics b113(SR);
    b113.p.f0 = 5587.65;  // MIDI 113: partial 22.35 kHz, just under the limit -> kept
    b113.noteOn(1.0);
    CHECK(b113.partialAudible(), "MIDI 113 bar partial at 22.35 kHz is under the limit and must be kept");
    BarPhysics b114(SR);
    b114.p.f0 = 5919.91;  // MIDI 114: partial 23.68 kHz -> muted
    b114.noteOn(1.0);
    CHECK(!b114.partialAudible(), "MIDI 114 bar partial at 23.68 kHz should be muted");
}

void testDamperIndependentOfMass() {
    // The damper acts on acceleration directly, so it sets the same tail whatever
    // the modal mass: it is the tool for long low tails, not mass scaling.
    NoteSpec n;
    n.midi = 60;
    n.sustain = false;
    n.damperT60 = 1.0;
    n.seconds = 2.5;
    n.releaseAt = 1.0;
    const Stereo a = playNote(n);
    n.scaleMass = true;
    const Stereo b = playNote(n);
    const double dropA = a.energyDb(1.05, 1.2) - a.energyDb(2.05, 2.2);
    const double dropB = b.energyDb(1.05, 1.2) - b.energyDb(2.05, 2.2);
    CHECK(dropA > 40 && dropB > 40, "damper T60 1 s: drops %.1f (20 g) and %.1f dB (scaled), want ~60 both", dropA, dropB);
}

// ---------------------------------------------------------------- editor touch interface

// Renders `seconds`, calling `before(blockIndex)` ahead of each 128-sample block.
template <typename F>
Stereo runBlocks(Synth& s, double seconds, F before) {
    Stereo out;
    const int total = static_cast<int>(seconds * SR) / BLOCK * BLOCK;
    out.L.resize(static_cast<size_t>(total));
    out.R.resize(static_cast<size_t>(total));
    for (int i = 0, b = 0; i < total; i += BLOCK, ++b) {
        before(b);
        s.render(&out.L[static_cast<size_t>(i)], &out.R[static_cast<size_t>(i)], BLOCK);
    }
    return out;
}
constexpr int blockAt(double seconds) { return static_cast<int>(seconds * 48000.0) / 128; }

void testTouchScalesTheFinger() {
    // A light touch (0.3) on a held glass note sounds quieter than a full one.
    auto level = [](double touch) {
        Synth s(SR);
        playerDefaults(s);
        s.noteOn(72, 0.8);
        s.setNoteTouch(72, touch);
        return runBlocks(s, 1.5, [](int) {}).energyDb(1.0, 1.5);
    };
    const double full = level(1.0), light = level(0.3);
    CHECK(light < full - 3, "touch 0.3 not quieter than 1.0: %.1f vs %.1f dB", light, full);
    // a new strike starts at full touch: a light touch must not carry over to the
    // next note on the same bowl (a MIDI note after a touch-screen note)
    Synth s(SR);
    playerDefaults(s);
    s.noteOn(72, 0.8);
    s.setNoteTouch(72, 0.3);
    renderLeft(s, 0.3);
    s.noteOff(72);
    renderLeft(s, 0.2);
    s.noteOn(72, 0.8);
    const double again = runBlocks(s, 1.5, [](int) {}).energyDb(1.0, 1.5);
    CHECK(again > (full + light) / 2, "retriggered note kept the light touch: %.1f dB (full %.1f, light %.1f)", again, full, light);
}

void testNoteLoad() {
    // setLoad in velocityLoad units: the strike's own load is a no-op (bit-identical),
    // and a set load no longer depends on how hard the note was struck
    auto run = [](double vel, double load) {
        Synth s(SR);
        playerDefaults(s);
        s.noteOn(72, vel);
        if (load > 0) s.setNoteLoad(72, load);
        return runBlocks(s, 2.0, [](int) {});
    };
    const Stereo plain = run(0.8, 0), same = run(0.8, BowlPhysics::velocityLoad(0.8));
    CHECK(same.L == plain.L && same.R == plain.R, "setLoad(velocityLoad(vel)) changed the note");
    const double soft = run(0.2, 1.6).energyDb(1.5, 2.0), hard = run(1.0, 1.6).energyDb(1.5, 2.0);
    CHECK(std::fabs(soft - hard) < 1.5, "a set load still depends on velocity: %.1f vs %.1f dB", soft, hard);
    const double softFree = run(0.2, 0).energyDb(1.5, 2.0);
    CHECK(soft > softFree + 6, "load 1.6 on a soft strike should make it speak (%.1f vs %.1f dB)", soft, softFree);
}

void testRevBSizes() {
    CHECK(Synth::revBDiameterMm(60) == 147 && Synth::revBDiameterMm(72) == 104 && Synth::revBDiameterMm(84) == 73,
          "rev B table not read: C4 %.1f C5 %.1f C6 %.1f", Synth::revBDiameterMm(60), Synth::revBDiameterMm(72),
          Synth::revBDiameterMm(84));
    CHECK(std::fabs(Synth::revBDiameterMm(48) - 104 * std::sqrt(2.0) * std::sqrt(2.0)) < 0.01,
          "below the table the size should grow by sqrt 2 per octave: C3 %.2f", Synth::revBDiameterMm(48));
    // a big bowl's rim runs faster: at 1 rev/s the C4 bowl (73.5 mm radius, 0.46 m/s)
    // is past where this friction model's stick-slip holds; at 45 mm (0.28 m/s) it speaks
    auto c4 = [](bool revB) {
        Synth s(SR);
        playerDefaults(s);
        s.setCoilsArmed(true);
        s.setRevBSizes(revB);
        s.noteOn(60, 0.8);
        return runBlocks(s, 2.5, [](int) {}).energyDb(1.5, 2.5);
    };
    const double small = c4(false), revB = c4(true);
    CHECK(small > revB + 12, "rev B size did not change how C4 speaks at 1 rev/s: %.1f vs %.1f dB", small, revB);
}

void testBuildVariation() {
    Synth off(SR);
    CHECK(off.vary(72, ParamId::Q2, 3500) == 3500, "variation must be off by default");
    Synth s(SR);
    s.setVariation(7, 1.0);
    double minC = 1e9, maxC = -1e9;
    bool qOk = true, splitOk = true, angleOk = true;
    for (int m = 36; m <= 96; ++m) {
        const double f = 440 * std::pow(2.0, (m - 69) / 12.0);
        const double c = 1200 * std::log2(s.vary(m, ParamId::f0, f) / f);
        minC = std::min(minC, c);
        maxC = std::max(maxC, c);
        qOk = qOk && std::fabs(s.vary(m, ParamId::Q2, 1000) / 1000 - 1) <= 0.2 + 1e-12;
        splitOk = splitOk && std::fabs(s.vary(m, ParamId::split, 1) - 1) <= 0.4 + 1e-12;
        angleOk = angleOk && std::fabs(s.axisAngle(m)) <= 3.14159265358979 / 4 + 1e-12;
        CHECK(s.vary(m, ParamId::N, 2.5) == 2.5, "finger load is the player's, not the bowl's: varied at %d", m);
    }
    CHECK(minC >= -3 && maxC <= 3 && maxC - minC > 2, "tuning spread should stay within +-3 c and use it: %.2f..%.2f", minC, maxC);
    CHECK(qOk && splitOk && angleOk, "a varied parameter left its range");
    Synth t(SR);
    t.setVariation(8, 1.0);
    CHECK(t.vary(72, ParamId::Q2, 1000) != s.vary(72, ParamId::Q2, 1000), "two instrument numbers gave the same bowl");
    // one instrument always sounds the same
    auto render = [](std::uint32_t serial) {
        Synth s(SR);
        playerDefaults(s);
        s.setVariation(serial, 1.0);
        s.noteOn(72, 0.8);
        return runBlocks(s, 0.5, [](int) {});
    };
    const Stereo a = render(7), b = render(7), c = render(8);
    CHECK(a.L == b.L, "the same instrument number rendered differently");
    CHECK(a.L != c.L, "a different instrument number rendered the same");
}

void testCoilStart() {
    // from rest the stick-slip takes about a second to build at 1 rev/s; a coil burst
    // at note-on has the bowl audible within ~0.1 s, and leaves the steady tone alone
    auto run = [](double coilStart) {
        Synth s(SR);
        playerDefaults(s);
        s.setCoilsArmed(true);
        const ParamValue cs[] = {{ParamId::coilStart, coilStart}};
        s.setParams(Instrument::Glass, cs, 1);
        s.noteOn(72, 0.8);
        return runBlocks(s, 3.0, [](int) {});
    };
    const Stereo off = run(0), on = run(0.1);
    const double steadyOff = off.energyDb(2.0, 3.0), steadyOn = on.energyDb(2.0, 3.0);
    auto audibleAt = [](const Stereo& r, double steady) {
        for (double t = 0; t < 2.5; t += 0.005)
            if (r.energyDb(t, t + 0.02) > steady - 20) return t + 0.02;
        return 9.0;
    };
    const double tOff = audibleAt(off, steadyOff), tOn = audibleAt(on, steadyOn);
    CHECK(tOn < 0.2, "coil start: C5 not audible until %.3f s", tOn);
    CHECK(tOff > 0.6, "without coil start C5 should take its time (%.3f s): the test lost its baseline", tOff);
    CHECK(std::fabs(steadyOn - steadyOff) < 2, "coil start changed the steady tone: %.1f vs %.1f dB", steadyOn, steadyOff);
    // and it needs the coils: unarmed, no pedal, there is nothing to drive them
    Synth u(SR);
    playerDefaults(u);
    const ParamValue cs[] = {{ParamId::coilStart, 0.1}};
    u.setParams(Instrument::Glass, cs, 1);
    u.noteOn(72, 0.8);
    const Stereo unarmed = runBlocks(u, 0.5, [](int) {});
    Synth r(SR);
    playerDefaults(r);
    r.noteOn(72, 0.8);
    const Stereo ref = runBlocks(r, 0.5, [](int) {});
    CHECK(unarmed.L == ref.L, "coil start drove a bowl with the coils off");
}

void testSustainFade() {
    // sustain pedal down, note released at 1 s: held forever with fade 0 (the JS),
    // gone 60 dB in sustainT60 seconds otherwise
    auto run = [](Instrument inst, double t60) {
        Synth s(SR);
        playerDefaults(s);
        s.setInst(inst);
        s.setCoilsArmed(true);
        const PerfValue sus[] = {{PerfId::sustain, 1}};
        s.setPerf(sus, 1);
        const ParamValue f[] = {{ParamId::sustainT60, t60}};
        s.setParams(inst, f, 1);
        s.noteOn(72, 0.8);
        return runBlocks(s, 5.0, [&](int b) { if (b == blockAt(1.0)) s.noteOff(72); });
    };
    for (Instrument inst : {Instrument::Glass, Instrument::Bars}) {
        const char* name = inst == Instrument::Glass ? "glass" : "bars";
        const Stereo hold = run(inst, 0), fade = run(inst, 3.0);
        const double holdDrop = hold.energyDb(1.5, 2.0) - hold.energyDb(4.5, 5.0);
        const double fadeDrop = fade.energyDb(1.5, 2.0) - fade.energyDb(4.5, 5.0);
        CHECK(std::fabs(holdDrop) < 3, "%s: with fade 0 the pedal should hold the note (dropped %.1f dB)", name, holdDrop);
        CHECK(fadeDrop > 40, "%s: a 3 s sustain fade dropped only %.1f dB in 3 s", name, fadeDrop);
    }
}

void testSustainNeverRevivesAReleasedNote() {
    // Tener: the pedal brought back notes that were dying away. A note released
    // with the pedal up, caught by the pedal later, must keep falling; one released
    // under the pedal must never get louder than it was. Fade and damper as the plugin's.
    auto run = [](Instrument inst, bool pedalFirst, double vel, double offAt) {
        Synth s(SR);
        playerDefaults(s);
        s.setInst(inst);
        s.setCoilsArmed(true);
        const ParamValue f[] = {{ParamId::sustainT60, 6.0}, {ParamId::damperT60, inst == Instrument::Glass ? 1.5 : 1.0}};
        s.setParams(inst, f, 2);
        PerfValue sus[] = {{PerfId::sustain, pedalFirst ? 1.0 : 0.0}};
        s.setPerf(sus, 1);
        s.noteOn(72, vel);
        return runBlocks(s, 5.0, [&](int b) {
            if (b == blockAt(offAt)) s.noteOff(72);
            if (b == blockAt(offAt + 0.5) && !pedalFirst) {
                sus[0].value = 1;
                s.setPerf(sus, 1);
            }
        });
    };
    for (Instrument inst : {Instrument::Glass, Instrument::Bars}) {
        const char* name = inst == Instrument::Glass ? "glass" : "bars";
        // half-second windows, 2 dB slack: a glass note's level swings 1-2 dB as its
        // pattern turns past the ears; the old revival was +19 dB (bars)
        const Stereo caught = run(inst, false, 0.8, 1.0);
        const double atCatch = caught.energyDb(1.3, 1.5);
        double lowest = atCatch;
        for (double t = 1.5; t < 4.99; t += 0.5) {
            const double now = caught.energyDb(t, t + 0.5);
            CHECK(now < lowest + 2, "%s: the pedal brought a dying note back up at %.1f s (%.1f -> %.1f dB)", name, t, lowest,
                  now);
            lowest = std::fmin(lowest, now);
        }
        const double caughtDrop = atCatch - caught.energyDb(4.5, 5.0);
        CHECK(caughtDrop > 15, "%s: a note the pedal caught dropped only %.1f dB in 3.3 s", name, caughtDrop);
        // released once settled; compared with its loudest 0.25 s in the half second
        // before (a glass note's level swings about a dB as the pattern turns)
        for (double vel : {0.2, 0.9}) {
            const Stereo held = run(inst, true, vel, 2.0);
            const double atRelease = std::fmax(held.energyDb(1.5, 1.75), held.energyDb(1.75, 2.0));
            for (double t = 2.0; t < 4.99; t += 0.25) {
                const double now = held.energyDb(t, t + 0.25);
                CHECK(now < atRelease + 1, "%s vel %.1f: released under the pedal, louder at %.2f s (%.1f vs %.1f dB)", name,
                      vel, t, now, atRelease);
            }
        }
    }
}

void testSustainStillSustainsReleasedNotes() {
    // The ceiling must not stop the pedal doing its job. Bars, pedal down, no damper
    // (so coils on and coils off differ only by the coils' drive), a long fade:
    // a tap released within the mallet's strike and a note released at 1 s both
    // stay well above their natural decay. And a repeated note-off (a stray MPE
    // one) must not restart the fade.
    auto run = [](double offAt, double Gs, int extraOffs) {
        Synth s(SR);
        playerDefaults(s);
        s.setInst(Instrument::Bars);
        s.setCoilsArmed(true);
        const ParamValue f[] = {{ParamId::sustainT60, 30.0}, {ParamId::damperT60, 0}};
        s.setParams(Instrument::Bars, f, 2);
        const PerfValue sus[] = {{PerfId::sustain, 1}, {PerfId::Gs, Gs}};
        s.setPerf(sus, 2);
        s.noteOn(72, 0.8);
        if (offAt == 0) s.noteOff(72);  // before the first sample: inside the strike
        return runBlocks(s, 4.0, [&](int b) {
            if (offAt > 0 && b == blockAt(offAt)) s.noteOff(72);
            for (int k = 1; k <= extraOffs; ++k)
                if (b == blockAt(offAt + k)) s.noteOff(72);
        });
    };
    for (double offAt : {0.0, 1.0}) {
        const double coils = run(offAt, 0.8, 0).energyDb(3.0, 3.5), natural = run(offAt, 0, 0).energyDb(3.0, 3.5);
        CHECK(coils > natural + 6, "released at %.1f s: the pedal's coils held the bar only %.1f dB over its natural decay",
              offAt, coils - natural);
    }
    // glass and the default fade: a note released under the pedal at 1 s, then
    // stray note-offs at 2 and 3 s, must fade as with one note-off
    auto glass = [](int extraOffs) {
        Synth s(SR);
        playerDefaults(s);
        s.setCoilsArmed(true);
        const ParamValue f[] = {{ParamId::sustainT60, 6.0}};
        s.setParams(Instrument::Glass, f, 1);
        const PerfValue sus[] = {{PerfId::sustain, 1}};
        s.setPerf(sus, 1);
        s.noteOn(72, 0.8);
        return runBlocks(s, 5.0, [&](int b) {
            if (b == blockAt(1.0)) s.noteOff(72);
            for (int k = 1; k <= extraOffs; ++k)
                if (b == blockAt(1.0 + k)) s.noteOff(72);
        });
    };
    const double once = glass(0).energyDb(4.0, 4.5), repeated = glass(2).energyDb(4.0, 4.5);
    CHECK(std::fabs(repeated - once) < 2, "repeated note-offs changed a fading note: %.1f vs %.1f dB", repeated, once);
}

void testStillFingerOnStoppedSpindleStopsExciting() {
    // Spindle stopped: the finger must travel along the rim to excite the bowl.
    // One finger keeps rubbing; the other stops at 0.5 s and the note must fade.
    auto run = [](bool keepRubbing) {
        Synth s(SR);
        playerDefaults(s);
        const ParamValue still[] = {{ParamId::spin, 0}};
        s.setParams(Instrument::Glass, still, 1);
        s.noteOn(72, 0.8);
        return runBlocks(s, 2.5, [&](int b) {
            if (b == blockAt(0.5) && !keepRubbing) s.setNoteFingerSpeed(72, 0);
        });
    };
    const Stereo rub = run(true), still = run(false);
    const double rubLate = rub.energyDb(2.0, 2.5), stillLate = still.energyDb(2.0, 2.5);
    CHECK(stillLate < rubLate - 10, "a still finger kept exciting the bowl: %.1f vs rubbing %.1f dB", stillLate, rubLate);
    // and the default (no call) is the JS behaviour: the finger keeps its own speed
    Synth s(SR);
    playerDefaults(s);
    const ParamValue still0[] = {{ParamId::spin, 0}};
    s.setParams(Instrument::Glass, still0, 1);
    s.noteOn(72, 0.8);
    const Stereo dflt = runBlocks(s, 2.5, [](int) {});
    CHECK(dflt.L == rub.L && dflt.R == rub.R, "untouched note must be bit-identical to the JS path");
}

void testPatternAngleFollowsTheDrive() {
    // With the coils driving and the drive axis rotating, the reported pattern
    // angle turns; with rotation 0 on a fixed bowl it stays put.
    auto travel = [](double rot) {
        Synth s(SR);
        playerDefaults(s);
        const ParamValue p[] = {{ParamId::spin, 0}, {ParamId::split, 0}, {ParamId::rot, rot}};
        s.setParams(Instrument::Glass, p, 3);
        const PerfValue sus[] = {{PerfId::sustain, 1}};
        s.setPerf(sus, 1);
        s.noteOn(72, 0.8);
        double total = 0, prev = 0;
        bool first = true;
        Synth::VoiceView v[16];
        runBlocks(s, 3.0, [&](int b) {
            if (b == blockAt(0.4)) s.noteOff(72);  // the coils carry it from here
            if (b < blockAt(1.0) || s.snapshot(v, 16) == 0) return;
            const double a = v[0].pattern;
            if (!first) {
                double d = a - prev;  // unwrap on the quarter-turn period
                while (d > 0.7854) d -= 1.5708;
                while (d < -0.7854) d += 1.5708;
                total += d;
            }
            prev = a;
            first = false;
        });
        return std::fabs(total);
    };
    const double turning = travel(0.5), still = travel(0.0);
    CHECK(turning > 1.5, "pattern did not follow a 0.5 rev/s drive: turned %.2f rad in 2 s", turning);
    CHECK(still < 0.5, "pattern drifted %.2f rad with no rotation", still);
}

void testSnapshotReportsVoices() {
    Synth s(SR);
    playerDefaults(s);
    s.noteOn(60, 0.8);
    s.noteOn(67, 0.8);
    runBlocks(s, 0.3, [](int) {});
    s.noteOff(67);
    Synth::VoiceView v[16];
    const int n = s.snapshot(v, 16);
    bool saw60 = false, saw67 = false;
    for (int i = 0; i < n; ++i) {
        if (v[i].midi == 60) saw60 = v[i].held && v[i].glass && v[i].level > 0;
        if (v[i].midi == 67) saw67 = !v[i].held;
    }
    CHECK(n == 2, "snapshot listed %d voices, want 2", n);
    CHECK(saw60, "held glass note 60 missing or wrong in the snapshot");
    CHECK(saw67, "released note 67 should be listed as not held");
}

void testNoteGain() {
    // all 1 is the old path bit for bit; 0.5 is -6 dB on that note only; the other
    // instrument's table does not touch it; a live change ramps over one block
    const int total = static_cast<int>(2.0 * SR) / 1000 * 1000;
    auto run = [&](int block, const std::function<void(Synth&)>& setup, const std::function<void(Synth&, int)>& each,
                   bool chord) {
        Synth s(SR);
        playerDefaults(s);
        setup(s);
        s.noteOn(72, 0.8);
        if (chord) s.noteOn(60, 0.8);
        Stereo out;
        out.L.resize(static_cast<size_t>(total));
        out.R.resize(static_cast<size_t>(total));
        for (int i = 0, b = 0; i < total; i += block, ++b) {
            each(s, b);
            s.render(&out.L[static_cast<size_t>(i)], &out.R[static_cast<size_t>(i)], std::min(block, total - i));
        }
        return out;
    };
    const auto none = [](Synth&) {};
    const auto still = [](Synth&, int) {};
    const Stereo plain = run(BLOCK, none, still, true);
    const Stereo ones = run(BLOCK, [](Synth& s) { s.setNoteGain(Instrument::Glass, 72, 1.0); }, still, true);
    CHECK(ones.L == plain.L && ones.R == plain.R, "gain 1 changed the render");
    const Stereo bars = run(BLOCK, [](Synth& s) { s.setNoteGain(Instrument::Bars, 72, 0.0); }, still, true);
    CHECK(bars.L == plain.L && bars.R == plain.R, "the bars table changed a glass note");

    // one quiet note alone, so the limiter stays linear
    const auto quiet = [](Synth& s) {
        const ParamValue m[] = {{ParamId::master, 0.05}};
        s.setParams(Instrument::Glass, m, 1);
    };
    const auto half = [&](Synth& s) {
        quiet(s);
        s.setNoteGain(Instrument::Glass, 72, 0.5);
    };
    const double d = run(BLOCK, half, still, false).energyDb(1.0, 2.0) - run(BLOCK, quiet, still, false).energyDb(1.0, 2.0);
    CHECK(std::fabs(d + 6.02) < 0.3, "gain 0.5 moved the note by %.2f dB, want -6.02", d);
    const Stereo small = run(BLOCK, half, still, false), big = run(1000, half, still, false);  // 1000 > the chunk
    CHECK(small.L == big.L && small.R == big.R, "a scaled note depends on the block size");

    // live: 1 -> 0 at block 200 is silent from the next block on, with no step
    const int at = 200;
    const Stereo cut = run(BLOCK, none, [&](Synth& s, int b) {
        if (b == at) {
            s.setNoteGain(Instrument::Glass, 72, 0.0);
            s.setNoteGain(Instrument::Glass, 60, 0.0);
        }
    }, true);
    double after = 0, step = 0, before = 0;
    for (size_t i = static_cast<size_t>((at + 1) * BLOCK); i < cut.L.size(); ++i)
        after = std::max(after, static_cast<double>(std::fabs(cut.L[i])));
    for (size_t i = static_cast<size_t>(at * BLOCK); i < static_cast<size_t>((at + 1) * BLOCK); ++i)
        step = std::max(step, static_cast<double>(std::fabs(cut.L[i] - cut.L[i - 1])));
    for (size_t i = static_cast<size_t>((at - 1) * BLOCK); i < static_cast<size_t>(at * BLOCK); ++i)
        before = std::max(before, static_cast<double>(std::fabs(plain.L[i] - plain.L[i - 1])));
    CHECK(after == 0.0, "gain 0 left %.3g after the ramp", after);
    CHECK(step <= before * 1.5 + 1e-6, "the ramp jumped: step %.3g vs %.3g", step, before);
}

} // namespace

int main() {
    testArmedIsInertWithNothingToDo();
    testBendNeedsArmedCoilsWithoutSustain();
    testArmedDoesNotSustain();
    testStealPrefersReleasedVoice();
    testStealWhenAllHeld();
    testVoicesFreeAfterDecay();
    testWholeKeyboardStaysFinite();
    testDamperSetsTheTail();
    testDamperYieldsToSustain();
    testDamperOnBars();
    testModalMassScaling();
    testNyquistGuard();
    testDamperIndependentOfMass();
    testTouchScalesTheFinger();
    testNoteBend();
    testNoteLoad();
    testRevBSizes();
    testBuildVariation();
    testCoilStart();
    testSustainFade();
    testSustainNeverRevivesAReleasedNote();
    testSustainStillSustainsReleasedNotes();
    testStillFingerOnStoppedSpindleStopsExciting();
    testPatternAngleFollowsTheDrive();
    testSnapshotReportsVoices();
    testNoteGain();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all engine tests passed\n");
    return 0;
}
