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
    velScale_ = 0.4 + 0.8 * vel;
    psi_ = thF_;
}

void BowlPhysics::noteOff() {
    on_ = false;
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
    NT_ = on_ ? P.N * (velScale_ != 0 ? velScale_ : 1) * P.pressure : 0;
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

    double FA = 0, FB = 0, F3A = 0, F3B = 0, FkA = 0, FkB = 0;
    if (P.driveOn != 0) {
        psi_ += P.rot * 2 * PI * dt;
        const double cp = std::cos(2 * psi_), sp = std::sin(2 * psi_);
        const double vpsi = v[0] * cp + v[1] * sp;
        const double av = std::fabs(vpsi);
        venv_ = av > venv_ ? av : venv_ * (1 - dt * 30);
        const double geff = P.G > 0 ? std::fmax(-P.G, P.G * (1 - venv_ / P.vset)) : P.G;
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
        // electronic spring: vibrato + pitch bend
        t_ += dt;
        const double cents = P.vibDepth * std::sin(2 * PI * P.vibRate * t_) + P.bend;
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
    for (int i = 0; i < 4; ++i) {
        v[i] += a[i] * dt;
        q[i] += v[i] * dt;
    }
    Ft_ = Ft;
    vrel_ = vrel;
}

void BowlPhysics::render(float* L, float* R, int n, bool add) {
    const double dth = 0.26, thL = p.listen - dth, thR = p.listen + dth;
    const double c2L = std::cos(2 * thL), s2L = std::sin(2 * thL), c3L = std::cos(3 * thL), s3L = std::sin(3 * thL);
    const double c2R = std::cos(2 * thR), s2R = std::sin(2 * thR), c3R = std::cos(3 * thR), s3R = std::sin(3 * thR);
    const double g2 = 3e-4 * p.master, g3 = 1.8e-4 * p.master;
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
    }
}

} // namespace magnetika
