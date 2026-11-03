#pragma once
// Parameter identifiers shared by both instruments.
//
// The JS engine stores parameters in plain objects and `Object.assign`s whatever
// it is given, so a key meant for one instrument is harmlessly stored by the
// other (the player sends `rot`, `trim` and `n3ctl` to the bars too). Here every
// key has one id; each instrument applies the ids it has and ignores the rest,
// which is the same behaviour. Unknown *names* are an error at lookup time, so a
// typo in a trace or preset cannot be silently dropped.

#include <cstddef>
#include <optional>
#include <string_view>

namespace magnetika {

enum class ParamId : unsigned char {
    // shared
    f0, driveOn, G, Fmax, vset, vibDepth, vibRate, Fvib, listen, master, bend, pressure,
    // glass
    Q2, split, r3, Q3, N, dmu, vs, breath, fingerSpeed, rot, trim, n3ctl, spin, kappa,
    // bars
    Q1, ratio, strikeF, contact, strikePos, coil2,
    // plugin additions (not in the JS reference; 0 = off = reference behaviour)
    damperT60,  // seconds: dry-finger / damper-bar tail after release, 0 = off
    Count
};

constexpr std::size_t kNumParams = static_cast<std::size_t>(ParamId::Count);

struct ParamValue {
    ParamId id;
    double value;
};

std::optional<ParamId> paramFromName(std::string_view name);
std::string_view paramName(ParamId id);

// Performance state (Synth::setPerf in the JS): pedals and the coil gains.
enum class PerfId : unsigned char { sustain, brake, Gs, Gb, Count };

struct PerfValue {
    PerfId id;
    double value;
};

std::optional<PerfId> perfFromName(std::string_view name);

} // namespace magnetika
