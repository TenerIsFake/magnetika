// Tests for the behaviour the JS reference does not have, so the reference
// comparison cannot cover it: "coils always armed" and voice stealing.

#include <cmath>
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
    Synth s(SR);
    playerDefaults(s);
    for (int m = 60; m < 68; ++m) s.noteOn(m, 0.8);
    renderLeft(s, 0.2);
    s.noteOn(72, 0.8);  // must still get a voice
    int held = 0;
    for (int m = 60; m < 68; ++m) held += s.sounding(m);
    CHECK(s.sounding(72), "new note not allocated when all voices held");
    CHECK(held == 7, "expected exactly one held note stolen, %d remain", held);
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

} // namespace

int main() {
    testArmedIsInertWithNothingToDo();
    testBendNeedsArmedCoilsWithoutSustain();
    testArmedDoesNotSustain();
    testStealPrefersReleasedVoice();
    testStealWhenAllHeld();
    testVoicesFreeAfterDecay();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all engine tests passed\n");
    return 0;
}
