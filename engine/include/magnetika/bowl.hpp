#pragma once
// One glass bowl: the two degenerate (2,0) modes and the (3,0) pair, a wet finger,
// virtual coils (sustain / brake / electronic spring / trim) and Bryan coupling.
//
// Port of BowlPhysics in reference/engine.mjs. Expressions keep the JS operand
// order so that results match the reference to the last bit where the maths
// library allows; do not "simplify" them without re-running the reference check.

#include "magnetika/jsmath.hpp"
#include "magnetika/params.hpp"

namespace magnetika {

struct BowlParams {
    double f0 = 660, Q2 = 2500, split = 0.4, r3 = 2.78, Q3 = 1800, N = 2.5, dmu = 0.45, vs = 0.2,
           breath = 0.3, fingerSpeed = 3.8;
    double driveOn = 0, G = 0.8, Fmax = 0.06, vset = 0.3, rot = 0, vibDepth = 0, vibRate = 5.5,
           Fvib = 0.3, trim = 0, n3ctl = 0;
    double spin = 1, kappa = 0.35, listen = 0, master = 0.6, bend = 0, pressure = 1;
    double damperT60 = 0;  // not in the JS; 0 = off
    double sustainT60 = 0, coilStart = 0;  // not in the JS; 0 = off (see ParamId)

    // Applies the ids a bowl has; others (bar-only) are ignored, as Object.assign
    // stores them unused in the JS.
    void set(ParamId id, double v);
    double get(ParamId id) const;  // 0 for ids a bowl does not have
};

class BowlPhysics {
public:
    explicit BowlPhysics(double sampleRate = 48000.0);

    BowlParams p;

    void set(ParamId id, double v) { p.set(id, v); }
    void seed(std::uint32_t s) { rng_.seed(s); }
    // Modal mass in kg. The JS fixes 0.02 (a C5-sized bowl); set before noteOn.
    void setModalMass(double kg) { m_ = kg; }
    double modalMass() const { return m_; }
    // Mute, in the output only, any mode pair whose frequency would sit above
    // Nyquist (it would alias). Decided at noteOn, so it never switches mid-note.
    void setNyquistGuard(bool on) { nyquistGuard_ = on; }
    bool partialAudible(int pair) const { return pair == 2 ? keep2_ : keep3_; }
    void noteOn(double vel);
    void noteOff();
    void kick(double vel);
    void silence();

    // Touch interface (not in the JS; the defaults leave it bit-identical):
    // touch scales this note's finger load (1 = as played); fingerSpeed sets how
    // fast the finger travels along the rim, rad/s. On a stopped spindle a still
    // finger (0) does not excite the bowl, so a touch screen can make the player rub.
    void setTouch(double t) { touch_ = t; }
    // Rim radius in metres: the rim speed under the finger is (finger - spindle) x R.
    void setRimRadius(double m) { R_ = m; }
    double rimRadius() const { return R_; }
    // Where the (2,0) principal axes start relative to the finger, radians: a
    // property of each real bowl (its wall-thickness variation).
    void setAxisAngle(double rad) { alpha_ = rad; }
    // The finger load a strike of velocity `vel` gives (the JS velScale).
    static double velocityLoad(double vel) { return 0.4 + 0.8 * vel; }
    // Sets this note's finger load outright, in the same units as velocityLoad,
    // so it no longer depends on the strike: MPE pressure, which takes over the
    // load from velocity once the player presses.
    void setLoad(double load) { touch_ = load / (velScale_ != 0 ? velScale_ : 1); }
    // Per-note pitch bend in cents (MPE), added to the shared `bend` parameter and
    // applied by the same electronic spring, so it needs the coils engaged and is
    // limited by Fvib like any bend. Reset to 0 by noteOn.
    void setNoteBend(double cents) { noteBend_ = cents; }
    void setFingerSpeed(double radPerSec) {
        omF_ = radPerSec;
        moving_ = on_ && radPerSec != 0;
    }

    // Renders n samples; adds into L/R when `add`, as float32 like the JS Float32Array.
    void render(float* L, float* R, int n, bool add);

    double env() const { return env_; }
    bool on() const { return on_; }
    // Orientation of the (2,0) standing-wave pattern over the last render call,
    // radians in [-pi/4, pi/4) (the pattern repeats every quarter turn). Read-only
    // display state: it never feeds back into the sound.
    double patternAngle() const { return pattern_; }

private:
    void step();
    void updateSubsteps();

    double sr_;
    int sub_ = 2;
    double dt_;
    double q_[4] = {}, v_[4] = {}, a_[4] = {};
    double thF_, thT_, omF_ = 0, N_ = 0, NT_ = 0;
    bool on_ = false, moving_ = false;
    double psi_, env_ = 0, noiseLP_ = 0, alpha_ = 0, venv_ = 0, t_ = 0;
    double velScale_ = 0;  // JS: undefined until the first noteOn; `velScale||1`
    static constexpr double mu_k_ = 0.35;
    double R_ = 0.045;  // rim radius, m (the JS fixes 45 mm for every bowl)
    double m_ = 0.02;
    double Ft_ = 0, vrel_ = 0;
    double damp_ = 0;  // damper engagement 0..1, ramps in after release
    double relT_ = 0;  // seconds since the finger lifted (sustain fade)
    double startT_ = -1, startF_ = 0;  // coil start: time into the burst (-1 = none), its force
    double touch_ = 1.0;
    double noteBend_ = 0;  // cents
    double patS1_ = 0, patS2_ = 0, pattern_ = 0;
    bool nyquistGuard_ = true, keep2_ = true, keep3_ = true;
    js::XorShift32 rng_;
};

} // namespace magnetika
