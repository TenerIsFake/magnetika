#pragma once
// Plugin parameters. Ranges and defaults are the browser player's sliders
// (reference/player-defaults.json and the GLASS / BARS / PERF specs in the
// original player), plus the plugin additions: armed coils, damper, mass scaling.

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "magnetika/params.hpp"
#include "magnetika/synth.hpp"

namespace magnetika::plugin {

namespace id {
// performance (shared by both instruments, like the player's PERF sliders)
inline constexpr const char* instrument = "instrument";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* brake = "brake";
inline constexpr const char* vibDepth = "vibDepth";
inline constexpr const char* rotation = "rotation";
inline constexpr const char* trimOn = "trimOn";
inline constexpr const char* trimAmount = "trimAmount";
inline constexpr const char* pressure = "pressure";
inline constexpr const char* bendRange = "bendRange";
inline constexpr const char* n3ctl = "n3ctl";
// plugin additions
inline constexpr const char* coilsArmed = "coilsArmed";
inline constexpr const char* massScaling = "massScaling";
} // namespace id

// One engine parameter of one instrument, driven by one plugin parameter.
struct EngineMapping {
    const char* paramId;
    Instrument instrument;
    ParamId engineId;
    bool degreesToRadians;
};

// Plugin parameters that map straight onto engine parameters.
const std::vector<EngineMapping>& engineMappings();

// The sustain and brake gains are performance state in the engine (Synth::setPerf),
// taken from whichever instrument is current, as in the player.
inline constexpr const char* glassSustainGain = "g_G";
inline constexpr const char* glassBrakeGain = "g_Gb";
inline constexpr const char* barsSustainGain = "b_G";
inline constexpr const char* barsBrakeGain = "b_Gb";

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Glass materials from the original single-bowl simulation. Selecting one sets
// Q2, Q3 and the A-B split, exactly as the player's material chips do.
struct Material {
    const char* name;
    double Q2, Q3, split;
};
const std::vector<Material>& materials();

} // namespace magnetika::plugin
