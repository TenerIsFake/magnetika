#include "magnetika/bar.hpp"

#include <cmath>

#include "magnetika/jsmath.hpp"

namespace magnetika {

namespace {
constexpr double PI = 3.141592653589793;  // Math.PI
}

void BarParams::set(ParamId id, double v) {
    switch (id) {
        case ParamId::f0: f0 = v; break;
        case ParamId::Q1: Q1 = v; break;
        case ParamId::Q2: Q2 = v; break;
        case ParamId::ratio: ratio = v; break;
        case ParamId::strikeF: strikeF = v; break;
        case ParamId::contact: contact = v; break;
        case ParamId::strikePos: strikePos = v; break;
        case ParamId::driveOn: driveOn = v; break;
        case ParamId::G: G = v; break;
        case ParamId::Fmax: Fmax = v; break;
        case ParamId::vset: vset = v; break;
        case ParamId::vibDepth: vibDepth = v; break;
        case ParamId::vibRate: vibRate = v; break;
        case ParamId::Fvib: Fvib = v; break;
        case ParamId::coil2: coil2 = v; break;
        case ParamId::listen: listen = v; break;
        case ParamId::master: master = v; break;
        case ParamId::bend: bend = v; break;
        case ParamId::pressure: pressure = v; break;
        case ParamId::damperT60: damperT60 = v; break;
        case ParamId::sustainT60: sustainT60 = v; break;
        default: break;  // glass-only
    }
}

double BarParams::get(ParamId id) const {
    switch (id) {
        case ParamId::f0: return f0;
        case ParamId::listen: return listen;
        default: return 0.0;
    }
}

BarPhysics::BarPhysics(double sampleRate) : sr_(sampleRate), dt_(1.0 / (sampleRate * 2)) {}

void BarPhysics::noteOn(double vel) {
    st_ = 0;
    sv_ = vel;
    on_ = true;
    relT_ = 0;
    relCap_ = -1;
    noteBend_ = 0;
    // Nyquist guard for the 4x partial; see BowlPhysics::noteOn.
    keep2_ = !nyquistGuard_ || p.f0 * p.ratio < 0.5 * sr_ / 1.05;
}

void BarPhysics::silence() {
    q_[0] = q_[1] = 0;
    v_[0] = v_[1] = 0;
    env_ = 0;
    venv_ = 0;
}

void BarPhysics::step() {
    const BarParams& P = p;
    const double dt = dt_, m = m_;
    double* q = q_;
    double* v = v_;
    double* a = a_;
    const double w1 = 2 * PI * P.f0, w2 = w1 * P.ratio;
    double F1 = 0, F2 = 0;
    if (st_ >= 0) {
        const double tau = P.contact * 1e-3, F0 = P.strikeF * (0.3 + 0.7 * sv_) * P.pressure;
        if (st_ < tau) {
            const double f = F0 * std::sin(PI * st_ / tau);
            F1 = f;
            F2 = f * (0.15 + 0.85 * P.strikePos);
            st_ += dt;
        } else {
            st_ = -1;
        }
    }
    // Sustain fade, as BowlPhysics: the decaying setpoint plus a level ceiling,
    // here on the fundamental (the mode the main coil drives). The ceiling is taken
    // once the mallet has left the bar, so a tap released within the strike keeps it.
    const bool fade = P.sustainT60 > 0 && !on_;
    double relAmp = 0;
    if (fade) {
        if (P.driveOn != 0 && P.G > 0) relT_ += dt;
        relAmp = std::sqrt(v[0] * v[0] + w1 * w1 * q[0] * q[0]);
        if (relCap_ < 0) {
            if (st_ < 0) relCap_ = relAmp;
        }
        else if (P.driveOn != 0 && P.G > 0) relCap_ *= std::exp(-6.907755278982137 * dt / P.sustainT60);
        else relCap_ = std::fmin(relCap_, relAmp);
    }
    double D1 = 0, D2 = 0, K1 = 0;
    if (P.driveOn != 0) {
        const double av = std::fabs(v[0]);
        venv_ = av > venv_ ? av : venv_ * (1 - dt * 30);
        double vset = P.vset;
        if (fade && P.G > 0) vset *= std::exp(-6.907755278982137 * relT_ / P.sustainT60);
        double geff = P.G > 0 ? std::fmax(-P.G, P.G * (1 - venv_ / vset)) : P.G;
        if (fade && P.G > 0 && relCap_ >= 0)
            geff = std::fmin(geff, P.G * std::fmax(0.0, std::fmin(1.0, 10 * (1 - relAmp / (relCap_ + 1e-12)))));
        D1 = P.Fmax * std::tanh(geff * v[0] / P.Fmax);
        if (P.coil2 != 0) D2 = 0.5 * P.Fmax * std::tanh(geff * v[1] / (0.5 * P.Fmax));
        t_ += dt;
        const double cents = P.vibDepth * std::sin(2 * PI * P.vibRate * t_) + P.bend + noteBend_;  // + 0: exact JS
        if (cents != 0) {
            const double dk = -2 * m * w1 * w1 * (std::pow(2.0, cents / 1200) - 1);
            const double f = dk * q[0];
            K1 = P.Fvib * std::tanh(f / P.Fvib);
        }
    }
    a[0] = (F1 + D1 + K1) / m - w1 * w1 * q[0] - (w1 / P.Q1) * v[0];
    a[1] = (F2 + D2) / m - w2 * w2 * q[1] - (w2 / P.Q2) * v[1];

    // Damper (not in the JS): the vibraphone damper bar. Released bars are damped
    // to a damperT60 tail unless the sustain coils hold them. See BowlPhysics.
    const bool sustainHeld = P.driveOn != 0 && P.G > 0;
    const double dampTarget = (P.damperT60 > 0 && !on_ && !sustainHeld) ? 1.0 : 0.0;
    if (dampTarget > 0 || damp_ > 0) {
        damp_ += (dampTarget - damp_) * js::min1(dt / 0.03);
        if (damp_ < 1e-9) damp_ = 0;
    }
    if (damp_ > 0) {
        const double sigma = 6.907755278982137 / (P.damperT60 > 0 ? P.damperT60 : 1e9);
        a[0] -= std::fmax(0.0, 2 * sigma - w1 / P.Q1) * damp_ * v[0];
        a[1] -= std::fmax(0.0, 2 * sigma - w2 / P.Q2) * damp_ * v[1];
    }

    for (int i = 0; i < 2; ++i) {
        v[i] += a[i] * dt;
        q[i] += v[i] * dt;
    }
}

// See BowlPhysics::updateSubsteps. Bars diverge from MIDI 119 at 48 kHz, where
// the 4x partial passes omega*dt = 2 with the JS's fixed sub = 2.
void BarPhysics::updateSubsteps() {
    const double wmax = 2 * PI * p.f0 * (p.ratio > 1 ? p.ratio : 1);
    int sub = 2;
    while (wmax / (sr_ * sub) > 1.0 && sub < 64) sub *= 2;
    if (sub != sub_) {
        sub_ = sub;
        dt_ = 1.0 / (sr_ * sub);
    }
}

void BarPhysics::render(float* L, float* R, int n, bool add) {
    updateSubsteps();
    const double g = 1.2e-3 * p.master, pan = 0.5 + 0.5 * std::sin(p.listen);
    const double gl = std::sqrt(1 - pan) * 1.2, gr = std::sqrt(pan) * 1.2;
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < sub_; ++s) step();
        const double y = g * (a_[0] + (keep2_ ? 0.45 * a_[1] : 0.0));
        if (add) {
            L[i] = static_cast<float>(static_cast<double>(L[i]) + y * gl);
            R[i] = static_cast<float>(static_cast<double>(R[i]) + y * gr);
        } else {
            L[i] = static_cast<float>(y * gl);
            R[i] = static_cast<float>(y * gr);
        }
        const double e = std::fabs(y);
        env_ = e > env_ ? e : env_ * 0.9995;
    }
}

} // namespace magnetika
