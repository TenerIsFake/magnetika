#include "magnetika/bowl.hpp"

#include <cmath>

namespace magnetika {

namespace {
constexpr double PI = 3.141592653589793;  // Math.PI
}

void BowlParams::set(ParamId id, double v) {
    switch (id) {
        case ParamId::f0: f0 = v; break;
        case ParamId::Q2: Q2 = v; break;
        case ParamId::split: split = v; break;
        case ParamId::r3: r3 = v; break;
        case ParamId::Q3: Q3 = v; break;
        case ParamId::N: N = v; break;
        case ParamId::dmu: dmu = v; break;
        case ParamId::vs: vs = v; break;
        case ParamId::breath: breath = v; break;
        case ParamId::fingerSpeed: fingerSpeed = v; break;
        case ParamId::driveOn: driveOn = v; break;
        case ParamId::G: G = v; break;
        case ParamId::Fmax: Fmax = v; break;
        case ParamId::vset: vset = v; break;
        case ParamId::rot: rot = v; break;
        case ParamId::vibDepth: vibDepth = v; break;
        case ParamId::vibRate: vibRate = v; break;
        case ParamId::Fvib: Fvib = v; break;
        case ParamId::trim: trim = v; break;
        case ParamId::n3ctl: n3ctl = v; break;
        case ParamId::spin: spin = v; break;
        case ParamId::kappa: kappa = v; break;
        case ParamId::listen: listen = v; break;
        case ParamId::master: master = v; break;
        case ParamId::bend: bend = v; break;
        case ParamId::pressure: pressure = v; break;
        case ParamId::damperT60: damperT60 = v; break;
        case ParamId::sustainT60: sustainT60 = v; break;
        case ParamId::coilStart: coilStart = v; break;
        default: break;  // bar-only
    }
}

double BowlParams::get(ParamId id) const {
    switch (id) {
        case ParamId::f0: return f0;
        case ParamId::listen: return listen;
        default: return 0.0;  // only f0 and listen are read back by the synth
    }
}

BowlPhysics::BowlPhysics(double sampleRate)
    : sr_(sampleRate), dt_(1.0 / (sampleRate * 2)), thF_(PI / 2), thT_(PI / 2), psi_(PI / 2) {}

void BowlPhysics::noteOn(double vel) {
    on_ = true;
    if (p.spin > 0.05) {
        moving_ = false;
        thT_ = PI / 2;
        omF_ = 0;
    } else {
        moving_ = true;
        omF_ = p.fingerSpeed;
    }
    velScale_ = velocityLoad(vel);
    touch_ = 1.0;  // a new strike starts at full finger load; a touch screen sets it after noteOn
    noteBend_ = 0;  // likewise, no per-note bend carries into the next note
    relT_ = 0;
    relCap_ = -1;
    // coil start: a velocity-scaled burst through the coils (see step); 0 = off, as the JS
    startT_ = p.coilStart > 0 ? 0.0 : -1.0;
    startF_ = p.coilStart * vel;
    psi_ = thF_;
    // Nyquist guard, decided once per note so a mode never pops in or out. The
    // 5 % margin covers pitch bend (+40 c) and vibrato on top of the note.
    const double limit = 0.5 * sr_ / 1.05;
    keep2_ = !nyquistGuard_ || p.f0 < limit;
    keep3_ = !nyquistGuard_ || p.f0 * p.r3 < limit;
}

void BowlPhysics::noteOff() {
    if (on_) {  // a repeated note-off (a stray MPE one) must not restart the fade
        relT_ = 0;
        relCap_ = -1;  // taken at the next step, from the level the note has then
    }
    on_ = false;
    startT_ = -1;
    moving_ = false;
    omF_ = 0;
}

void BowlPhysics::kick(double vel) {
    v_[0] += 0.06 * vel;
    v_[2] += 0.02 * vel;
}

void BowlPhysics::silence() {
    for (int i = 0; i < 4; ++i) q_[i] = v_[i] = 0;
    env_ = 0;
    venv_ = 0;
    on_ = false;
}

void BowlPhysics::step() {
    const BowlParams& P = p;
    const double dt = dt_, m = m_;
    double* q = q_;
    double* v = v_;
    double* a = a_;

    const double w0 = 2 * PI * P.f0, w3 = w0 * P.r3;
    const double dw2 = 2 * PI * P.split, dw3 = dw2 * 1.5;
    const double Om = P.spin * 2 * PI;
    alpha_ += Om * dt;
    if (alpha_ > PI) alpha_ -= 2 * PI;
    const double c4 = std::cos(4 * alpha_), s4 = std::sin(4 * alpha_);
    const double c6 = std::cos(6 * alpha_), s6 = std::sin(6 * alpha_);

    // finger: angle follows its target (5 ms), load ramps (15 ms)
    if (moving_) thT_ += omF_ * dt;
    double d = thT_ - thF_;
    d = std::atan2(std::sin(d), std::cos(d));
    thF_ += d * js::min1(dt / 0.005);
    NT_ = on_ ? P.N * (velScale_ != 0 ? velScale_ : 1) * P.pressure * touch_ : 0;  // touch_ = 1: exact JS
    N_ += (NT_ - N_) * js::min1(dt / 0.015);

    const double th = thF_, s2 = std::sin(2 * th), c2 = std::cos(2 * th);
    const double s3t = std::sin(3 * th), c3t = std::cos(3 * th);
    // tangential rim velocity at the finger (inextensional ring), and the rim passing under it
    const double vg = -v[0] * s2 / 2 + v[1] * c2 / 2 - v[2] * s3t / 3 + v[3] * c3t / 3;
    const double vf = (omF_ - Om) * R_;
    const double vrel = vf - vg;
    double Ft = 0;
    if (N_ > 1e-4) {
        const double mu = std::tanh(vrel / 0.002) * (mu_k_ + P.dmu * std::exp(-std::fabs(vrel) / P.vs));
        Ft = N_ * mu;
    }
    const double Q2A = Ft * (-s2 / 2), Q2B = Ft * (c2 / 2), Q3A = Ft * (-s3t / 3), Q3B = Ft * (c3t / 3);

    // Sustain fade (not in the JS), two parts, both only for a released note:
    // 1. under the pedal the regulator's setpoint decays to -60 dB over sustainT60,
    //    so the coils let the note fade instead of holding it forever;
    // 2. a level ceiling, taken from the note's amplitude when the finger lifts.
    //    With the pedal up it follows the note down (the damper); under the pedal it
    //    decays like the setpoint. The coils may never drive the note above it, so
    //    the pedal never brings back a note that was dying away, and releasing a
    //    note under the pedal never makes it louder. A note the pedal catches late
    //    rings on at its own rate until the fading setpoint reaches it; it is not
    //    braked at once (the fade clock runs only under the pedal). A held note keeps
    //    full sustain.
    const bool fade = P.sustainT60 > 0 && !on_;
    double relAmp = 0;
    if (fade) {
        if (P.driveOn != 0 && P.G > 0) relT_ += dt;
        relAmp = std::sqrt(v[0] * v[0] + v[1] * v[1] + w0 * w0 * (q[0] * q[0] + q[1] * q[1]) + v[2] * v[2] +
                           v[3] * v[3] + w3 * w3 * (q[2] * q[2] + q[3] * q[3]));
        if (relCap_ < 0) relCap_ = relAmp;
        else if (P.driveOn != 0 && P.G > 0) relCap_ *= std::exp(-6.907755278982137 * dt / P.sustainT60);
        else relCap_ = std::fmin(relCap_, relAmp);
    }

    double FA = 0, FB = 0, F3A = 0, F3B = 0, FkA = 0, FkB = 0;
    if (P.driveOn != 0) {
        psi_ += P.rot * 2 * PI * dt;
        const double cp = std::cos(2 * psi_), sp = std::sin(2 * psi_);
        const double vpsi = v[0] * cp + v[1] * sp;
        const double av = std::fabs(vpsi);
        venv_ = av > venv_ ? av : venv_ * (1 - dt * 30);
        double vset = P.vset;
        if (fade && P.G > 0) vset *= std::exp(-6.907755278982137 * relT_ / P.sustainT60);
        double geff = P.G > 0 ? std::fmax(-P.G, P.G * (1 - venv_ / vset)) : P.G;
        // The ceiling only withholds drive (full 10 % under it, none at or over it);
        // braking stays the setpoint's job. Braking toward the ceiling too was tried
        // and slowed the fade: the coils brake only the part of the motion on their axis.
        if (fade && P.G > 0 && relCap_ >= 0)
            geff = std::fmin(geff, P.G * std::fmax(0.0, std::fmin(1.0, 10 * (1 - relAmp / (relCap_ + 1e-12)))));
        if (P.G > 0) {
            const double Fd = P.Fmax * std::tanh(geff * vpsi / P.Fmax);
            FA = Fd * cp;
            FB = Fd * sp;
            if (P.n3ctl != 0) {
                const double c3p = std::cos(3 * psi_), s3p = std::sin(3 * psi_);
                const double v3 = v[2] * c3p + v[3] * s3p;
                const double F3 = 0.7 * P.Fmax * std::tanh(geff * v3 / (0.7 * P.Fmax));
                F3A = F3 * c3p;
                F3B = F3 * s3p;
            }
        } else {
            // brake: oppose the full velocity vector, magnitude limited
            const double vm = js::hypot(v[0], v[1]) + 1e-12;
            const double Fb = P.Fmax * std::tanh(-P.G * vm / P.Fmax);
            FA = -Fb * v[0] / vm;
            FB = -Fb * v[1] / vm;
            if (P.n3ctl != 0) {
                const double v3m = js::hypot(v[2], v[3]) + 1e-12;
                const double F3 = 0.7 * P.Fmax * std::tanh(-P.G * v3m / (0.7 * P.Fmax));
                F3A = -F3 * v[2] / v3m;
                F3B = -F3 * v[3] / v3m;
            }
        }
        // Coil start (not in the JS): rev B's coil-started notes. For up to 120 ms
        // after note-on the coils drive the (2,0) mode at its frequency along the
        // finger's axis, so the bowl is already moving when the rub takes over
        // (from rest the stick-slip needs 0.7-1.4 s to build). Velocity-scaled,
        // smooth (sin^2) so it does not click, and stopped once the mode is at half
        // the sustain setpoint.
        if (startT_ >= 0) {
            constexpr double kBurst = 0.12;
            if (startT_ < kBurst && venv_ < 0.5 * P.vset) {
                const double e = std::sin(PI * startT_ / kBurst);
                const double Fs = startF_ * e * e * std::cos(w0 * startT_);
                FA += Fs * cp;
                FB += Fs * sp;
                startT_ += dt;
            } else {
                startT_ = -1;
            }
        }
        // electronic spring: vibrato + pitch bend
        t_ += dt;
        const double cents = P.vibDepth * std::sin(2 * PI * P.vibRate * t_) + P.bend + noteBend_;  // + 0: exact JS
        if (cents != 0) {
            const double dk = -2 * m * w0 * w0 * (std::pow(2.0, cents / 1200) - 1);
            const double fa = dk * q[0], fb = dk * q[1];
            const double fm = js::hypot(fa, fb) + 1e-12, fl = P.Fvib * std::tanh(fm / P.Fvib);
            FkA += fa * fl / fm;
            FkB += fb * fl / fm;
        }
        if (P.trim > 0) {
            const double kt = m * w0 * dw2 * P.trim;
            const double ta = kt * (c4 * q[0] + s4 * q[1]), tb = kt * (s4 * q[0] - c4 * q[1]);
            const double tm = js::hypot(ta, tb) + 1e-12, tl = P.Fvib * std::tanh(tm / P.Fvib);
            FkA += ta * tl / tm;
            FkB += tb * tl / tm;
        }
    }

    // Bryan (Coriolis) coupling
    const double g2 = 2 * (2 * P.kappa * Om), g3 = 2 * (3 * P.kappa * 0.55 * Om);
    a[0] = (Q2A + FA + FkA) / m - w0 * w0 * q[0] - w0 * dw2 * (c4 * q[0] + s4 * q[1]) - (w0 / P.Q2) * v[0] - g2 * v[1];
    a[1] = (Q2B + FB + FkB) / m - w0 * w0 * q[1] - w0 * dw2 * (s4 * q[0] - c4 * q[1]) - (w0 / P.Q2) * v[1] + g2 * v[0];
    a[2] = (Q3A + F3A) / m - w3 * w3 * q[2] - w3 * dw3 * (c6 * q[2] + s6 * q[3]) - (w3 / P.Q3) * v[2] - g3 * v[3];
    a[3] = (Q3B + F3B) / m - w3 * w3 * q[3] - w3 * dw3 * (s6 * q[2] - c6 * q[3]) - (w3 / P.Q3) * v[3] + g3 * v[2];

    // Damper (not in the JS): a dry finger laid on the released bowl, the
    // traditional way to stop an armonica note. Engages over 30 ms after the
    // wet finger lifts, and only while the sustain coils are not holding the note,
    // so it never fights sustain. Adds just enough damping to bring each mode's
    // tail to damperT60 seconds (60 dB); a mode already decaying faster is left
    // alone. damperT60 = 0 keeps damp_ at exactly 0, so the JS path is untouched.
    const bool sustainHeld = P.driveOn != 0 && P.G > 0;
    const double dampTarget = (P.damperT60 > 0 && !on_ && !sustainHeld) ? 1.0 : 0.0;
    if (dampTarget > 0 || damp_ > 0) {
        damp_ += (dampTarget - damp_) * js::min1(dt / 0.03);
        if (damp_ < 1e-9) damp_ = 0;
    }
    if (damp_ > 0) {
        const double sigma = 6.907755278982137 / (P.damperT60 > 0 ? P.damperT60 : 1e9);  // ln(1000)/T60
        const double c2x = std::fmax(0.0, 2 * sigma - w0 / P.Q2) * damp_;
        const double c3x = std::fmax(0.0, 2 * sigma - w3 / P.Q3) * damp_;
        a[0] -= c2x * v[0];
        a[1] -= c2x * v[1];
        a[2] -= c3x * v[2];
        a[3] -= c3x * v[3];
    }

    for (int i = 0; i < 4; ++i) {
        v[i] += a[i] * dt;
        q[i] += v[i] * dt;
    }
    Ft_ = Ft;
    vrel_ = vrel;
}

// Departure from the JS (which fixes sub = 2): the explicit integrator is stable
// only while omega*dt < 2, and the (3,0) pair at r3 = 2.78 crosses that near the top
// of the MIDI range (glass >= 125 at 48 kHz, lower at 44.1 kHz). A diverging voice
// turns to NaN and, since NaN never decays below the free threshold, silences the
// mix for good. Substeps double until omega_max*dt <= 1. In the reference range
// omega_max*dt stays below 0.4 with sub = 2, so dt is bit-for-bit the JS value.
void BowlPhysics::updateSubsteps() {
    const double wmax = 2 * PI * p.f0 * (p.r3 > 1 ? p.r3 : 1);
    int sub = 2;
    while (wmax / (sr_ * sub) > 1.0 && sub < 64) sub *= 2;
    if (sub != sub_) {
        sub_ = sub;
        dt_ = 1.0 / (sr_ * sub);
    }
}

void BowlPhysics::render(float* L, float* R, int n, bool add) {
    updateSubsteps();
    const double dth = 0.26, thL = p.listen - dth, thR = p.listen + dth;
    const double c2L = std::cos(2 * thL), s2L = std::sin(2 * thL), c3L = std::cos(3 * thL), s3L = std::sin(3 * thL);
    const double c2R = std::cos(2 * thR), s2R = std::sin(2 * thR), c3R = std::cos(3 * thR), s3R = std::sin(3 * thR);
    // Nyquist guard: a muted pair still runs (it couples through the finger), it is
    // just not heard. Multiplying by an exact 0 or leaving the gain alone keeps the
    // audible path bit-identical to the JS.
    const double g2 = keep2_ ? 3e-4 * p.master : 0.0, g3 = keep3_ ? 1.8e-4 * p.master : 0.0;
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < sub_; ++s) step();
        double nz = 0;
        if (N_ > 1e-4) {
            const double slip = js::min1(std::fabs(vrel_) / 0.15);
            const double wn = rng_.next();
            noiseLP_ += (wn - noiseLP_) * 0.25;
            nz = noiseLP_ * N_ * slip * p.breath * 0.12 * p.master;
        }
        const double l = g2 * (a_[0] * c2L + a_[1] * s2L) + g3 * (a_[2] * c3L + a_[3] * s3L) + nz;
        const double r = g2 * (a_[0] * c2R + a_[1] * s2R) + g3 * (a_[2] * c3R + a_[3] * s3R) + nz;
        if (add) {
            L[i] = static_cast<float>(static_cast<double>(L[i]) + l);
            R[i] = static_cast<float>(static_cast<double>(R[i]) + r);
        } else {
            L[i] = static_cast<float>(l);
            R[i] = static_cast<float>(r);
        }
        const double e = std::fabs(l) + std::fabs(r);
        env_ = e > env_ ? e : env_ * 0.9995;
        // display only: second moments of the (2,0) pair give the pattern orientation
        patS1_ += q_[0] * q_[0] - q_[1] * q_[1];
        patS2_ += 2 * q_[0] * q_[1];
    }
    if (patS1_ != 0 || patS2_ != 0) pattern_ = 0.25 * std::atan2(patS2_, patS1_);
    patS1_ = patS2_ = 0;
}

} // namespace magnetika
