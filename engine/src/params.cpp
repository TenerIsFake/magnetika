#include "magnetika/params.hpp"

#include <array>

namespace magnetika {

namespace {
constexpr std::array<std::string_view, kNumParams> kNames = {
    "f0", "driveOn", "G", "Fmax", "vset", "vibDepth", "vibRate", "Fvib", "listen", "master", "bend", "pressure",
    "Q2", "split", "r3", "Q3", "N", "dmu", "vs", "breath", "fingerSpeed", "rot", "trim", "n3ctl", "spin", "kappa",
    "Q1", "ratio", "strikeF", "contact", "strikePos", "coil2",
    "damperT60",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(PerfId::Count)> kPerfNames = {
    "sustain", "brake", "Gs", "Gb",
};
} // namespace

std::optional<ParamId> paramFromName(std::string_view name) {
    for (std::size_t i = 0; i < kNames.size(); ++i)
        if (kNames[i] == name) return static_cast<ParamId>(i);
    return std::nullopt;
}

std::string_view paramName(ParamId id) { return kNames[static_cast<std::size_t>(id)]; }

std::optional<PerfId> perfFromName(std::string_view name) {
    for (std::size_t i = 0; i < kPerfNames.size(); ++i)
        if (kPerfNames[i] == name) return static_cast<PerfId>(i);
    return std::nullopt;
}

} // namespace magnetika
