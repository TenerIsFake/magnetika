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
inline constexpr const char* spindleOn = "spindleOn";  // off = fixed bowls (engine spin 0)
inline constexpr const char* mpe = "mpe";                // MIDI Polyphonic Expression on member channels
inline constexpr const char* mpeBendRange = "mpeBendRange";  // semitones of a full member-channel bend
inline constexpr const char* revBSizes = "revBSizes";    // bowls at their rev B rim sizes (wants ~0.5 rev/s)
inline constexpr const char* serialNo = "serialNo";      // which simulated instrument (build variation pattern)
inline constexpr const char* variation = "variation";    // how much its bowls and bars differ, 0..1
inline constexpr const char* mpeGlide = "mpeGlide";  // glass: a long MPE bend slides the finger to the next bowl
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

// A state or preset saved before a parameter existed would load it at its default.
// For build variation that would change how old material sounds (it played as 0),
// so a state without it gets 0. Call on a tree before replaceState.
void upgradeState(juce::ValueTree& state);

// Glass materials from the original single-bowl simulation. Selecting one sets
// Q2, Q3 and the A-B split, exactly as the player's material chips do.
struct Material {
    const char* name;
    double Q2, Q3, split;
};
const std::vector<Material>& materials();

} // namespace magnetika::plugin
