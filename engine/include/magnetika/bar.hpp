#pragma once
// One bar: a fundamental and an arched partial, a mallet force pulse, and the
// same coil laws as the glass. Port of BarPhysics in reference/engine.mjs.

#include "magnetika/params.hpp"

namespace magnetika {

struct BarParams {
    double f0 = 440, Q1 = 2500, Q2 = 1500, ratio = 4.0, strikeF = 25, contact = 2.0, strikePos = 0.3;
    double driveOn = 0, G = 0.8, Fmax = 0.5, vset = 0.2, vibDepth = 0, vibRate = 5.5, Fvib = 0.5, coil2 = 0;
    double listen = 0, master = 0.6, bend = 0, pressure = 1;

    void set(ParamId id, double v);  // glass-only ids are ignored
    double get(ParamId id) const;
};

class BarPhysics {
public:
    explicit BarPhysics(double sampleRate = 48000.0);

    BarParams p;

    void set(ParamId id, double v) { p.set(id, v); }
    void noteOn(double vel);
    void noteOff() { on_ = false; }
    void kick(double vel) { noteOn(vel); }
    void silence();

    void render(float* L, float* R, int n, bool add);

    double env() const { return env_; }
    bool on() const { return on_; }

private:
    void step();

    double sr_;
    int sub_ = 2;
    double dt_;
    double q_[2] = {}, v_[2] = {}, a_[2] = {};
    static constexpr double m_ = 0.2;
    double env_ = 0, venv_ = 0, t_ = 0, st_ = -1, sv_ = 0;
    bool on_ = false;
};

} // namespace magnetika
