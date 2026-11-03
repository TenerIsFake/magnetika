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

    // Renders n samples; adds into L/R when `add`, as float32 like the JS Float32Array.
    void render(float* L, float* R, int n, bool add);

    double env() const { return env_; }
    bool on() const { return on_; }

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
    static constexpr double R_ = 0.045, mu_k_ = 0.35;
    double m_ = 0.02;
    double Ft_ = 0, vrel_ = 0;
    double damp_ = 0;  // damper engagement 0..1, ramps in after release
    bool nyquistGuard_ = true, keep2_ = true, keep3_ = true;
    js::XorShift32 rng_;
};

} // namespace magnetika
