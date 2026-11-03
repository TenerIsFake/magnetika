#include "magnetika/bar.hpp"

#include <cmath>

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
    double D1 = 0, D2 = 0, K1 = 0;
    if (P.driveOn != 0) {
        const double av = std::fabs(v[0]);
        venv_ = av > venv_ ? av : venv_ * (1 - dt * 30);
        const double geff = P.G > 0 ? std::fmax(-P.G, P.G * (1 - venv_ / P.vset)) : P.G;
        D1 = P.Fmax * std::tanh(geff * v[0] / P.Fmax);
        if (P.coil2 != 0) D2 = 0.5 * P.Fmax * std::tanh(geff * v[1] / (0.5 * P.Fmax));
        t_ += dt;
        const double cents = P.vibDepth * std::sin(2 * PI * P.vibRate * t_) + P.bend;
        if (cents != 0) {
            const double dk = -2 * m * w1 * w1 * (std::pow(2.0, cents / 1200) - 1);
            const double f = dk * q[0];
            K1 = P.Fvib * std::tanh(f / P.Fvib);
        }
    }
    a[0] = (F1 + D1 + K1) / m - w1 * w1 * q[0] - (w1 / P.Q1) * v[0];
    a[1] = (F2 + D2) / m - w2 * w2 * q[1] - (w2 / P.Q2) * v[1];
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
        const double y = g * (a_[0] + 0.45 * a_[1]);
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
