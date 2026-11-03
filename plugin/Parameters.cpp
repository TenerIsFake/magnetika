#include "Parameters.h"

namespace magnetika::plugin {

namespace {

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

Range linear(float lo, float hi, float step = 0.0f) { return Range(lo, hi, step); }

// For quality factors spanning 100 .. 100 000: equal slider travel per decade,
// snapped to whole numbers (the exp/log round trip otherwise turns a default of
// 3500 into 3499.9995).
Range logRange(float lo, float hi) {
    return Range(lo, hi,
                 [](float a, float b, float t) { return a * std::pow(b / a, t); },
                 [](float a, float b, float v) { return std::log(v / a) / std::log(b / a); },
                 [](float a, float b, float v) { return juce::jlimit(a, b, std::round(v)); });
}

// Shows as many decimals as the parameter's step implies (none for whole numbers).
std::unique_ptr<APF> fl(const char* pid, const char* name, Range r, float def, const char* unit = "") {
    const float step = r.interval;
    const int decimals = step <= 0 ? (r.end >= 100 ? 0 : 2) : step >= 1 ? 0 : step >= 0.1f ? 1 : step >= 0.01f ? 2 : 3;
    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel(unit)
                     .withStringFromValueFunction([decimals](float v, int) { return juce::String(v, decimals); });
    return std::make_unique<APF>(juce::ParameterID{pid, 1}, name, r, def, attrs);
}

} // namespace

const std::vector<Material>& materials() {
    // name, Q2, Q3, split (Hz): the single-bowl simulation's MATERIALS list
    static const std::vector<Material> m = {
        {"Soda-lime glass", 1500, 1200, 1.5},
        {"Soda-lime + enamel band", 1300, 1000, 1.5},
        {"Lead crystal + enamel band", 4000, 3000, 1.0},
        {"Borosilicate", 3500, 2800, 0.8},
        {"Borosilicate + bonded magnets", 1800, 1500, 1.2},
        {"Borosilicate + ferrite frit ring", 3000, 2400, 0.8},
        {"Fused silica + ferrite frit ring", 40000, 28000, 0.1},
        {"Fused silica", 60000, 40000, 0.1},
        {"Magnetite glass-ceramic", 400, 300, 0.8},
        {"Fe-based metallic glass", 150, 120, 0.5},
    };
    return m;
}

const std::vector<EngineMapping>& engineMappings() {
    using I = Instrument;
    using P = ParamId;
    static const std::vector<EngineMapping> m = {
        // glass
        {"g_Q2", I::Glass, P::Q2, false},
        {"g_split", I::Glass, P::split, false},
        {"g_r3", I::Glass, P::r3, false},
        {"g_Q3", I::Glass, P::Q3, false},
        {"g_N", I::Glass, P::N, false},
        {"g_dmu", I::Glass, P::dmu, false},
        {"g_vs", I::Glass, P::vs, false},
        {"g_breath", I::Glass, P::breath, false},
        {"g_fingerSpeed", I::Glass, P::fingerSpeed, false},
        {"g_Fmax", I::Glass, P::Fmax, false},
        {"g_vset", I::Glass, P::vset, false},
        {"g_vibRate", I::Glass, P::vibRate, false},
        {"g_Fvib", I::Glass, P::Fvib, false},
        {"g_spin", I::Glass, P::spin, false},
        {"g_kappa", I::Glass, P::kappa, false},
        {"g_listen", I::Glass, P::listen, true},
        {"g_master", I::Glass, P::master, false},
        {"g_damper", I::Glass, P::damperT60, false},
        // bars
        {"b_Q1", I::Bars, P::Q1, false},
        {"b_Q2", I::Bars, P::Q2, false},
        {"b_ratio", I::Bars, P::ratio, false},
        {"b_strikeF", I::Bars, P::strikeF, false},
        {"b_contact", I::Bars, P::contact, false},
        {"b_strikePos", I::Bars, P::strikePos, false},
        {"b_Fmax", I::Bars, P::Fmax, false},
        {"b_vset", I::Bars, P::vset, false},
        {"b_vibRate", I::Bars, P::vibRate, false},
        {"b_Fvib", I::Bars, P::Fvib, false},
        {"b_listen", I::Bars, P::listen, true},
        {"b_master", I::Bars, P::master, false},
        {"b_damper", I::Bars, P::damperT60, false},
    };
    return m;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- performance
    layout.add(std::make_unique<APC>(juce::ParameterID{id::instrument, 1}, "Instrument",
                                     juce::StringArray{"Glass bowls", "Bars"}, 0));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::sustain, 1}, "Sustain (coils)", false));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::brake, 1}, "Brake", false));
    layout.add(fl(id::vibDepth, "Vibrato depth", linear(0, 30, 0.5f), 0, "c"));
    layout.add(fl(id::rotation, "Drive-axis rotation", linear(-1, 1, 0.01f), 0, "rev/s"));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::trimOn, 1}, "Electronic trim", false));
    layout.add(fl(id::trimAmount, "Trim amount", linear(0, 1, 0.01f), 1, "x split"));
    layout.add(fl(id::pressure, "Finger load scale", linear(0.2f, 2, 0.01f), 1, "x"));
    layout.add(fl(id::bendRange, "Pitch-bend range", linear(0, 40, 1), 40, "c"));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::n3ctl, 1}, "Coils on (3,0) too", false));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::coilsArmed, 1}, "Coils always armed", true));
    layout.add(std::make_unique<APB>(juce::ParameterID{id::massScaling, 1}, "Modal-mass scaling", false));

    // ---- glass (player GLASS spec; damper is a plugin addition)
    layout.add(fl("g_Q2", "Glass (2,0) Q", logRange(100, 100000), 3500));
    layout.add(fl("g_split", "Glass A-B split", linear(0, 5, 0.05f), 0.8f, "Hz"));
    layout.add(fl("g_r3", "Glass (3,0)/(2,0) ratio", linear(2.3f, 3.2f, 0.01f), 2.78f, "x"));
    layout.add(fl("g_Q3", "Glass (3,0) Q", logRange(100, 100000), 2800));
    layout.add(fl("g_N", "Finger load", linear(0.2f, 6, 0.05f), 2.5f, "N"));
    layout.add(fl("g_dmu", "Finger wetness", linear(0, 0.8f, 0.01f), 0.45f));
    layout.add(fl("g_vs", "Weakening velocity", linear(0.02f, 0.5f, 0.005f), 0.2f, "m/s"));
    layout.add(fl("g_breath", "Friction noise", linear(0, 1, 0.05f), 0.3f));
    layout.add(fl("g_fingerSpeed", "Finger speed (fixed bowl)", linear(1, 8, 0.1f), 3.8f, "rad/s"));
    layout.add(fl(glassSustainGain, "Glass sustain gain", linear(0.05f, 2, 0.01f), 0.8f, "N.s/m"));
    layout.add(fl(glassBrakeGain, "Glass brake gain", linear(0.1f, 3, 0.05f), 1.5f, "N.s/m"));
    layout.add(fl("g_Fmax", "Glass coil force limit", linear(0.005f, 0.5f, 0.005f), 0.06f, "N"));
    layout.add(fl("g_vset", "Glass sustain setpoint", linear(0.02f, 1, 0.01f), 0.3f, "m/s"));
    layout.add(fl("g_vibRate", "Glass vibrato rate", linear(1, 8, 0.1f), 5.5f, "Hz"));
    layout.add(fl("g_Fvib", "Glass spring force limit", linear(0.02f, 1, 0.01f), 0.3f, "N"));
    layout.add(fl("g_spin", "Bowl rotation (0 = fixed)", linear(0, 2, 0.05f), 1, "rev/s"));
    layout.add(fl("g_kappa", "Bryan factor", linear(0, 0.7f, 0.01f), 0.35f));
    layout.add(fl("g_listen", "Glass ear angle", linear(-180, 180, 1), 0, "deg"));
    layout.add(fl("g_master", "Glass level", linear(0, 1, 0.01f), 0.6f));
    layout.add(fl("g_damper", "Glass damper (tail, 0 = off)", linear(0, 10, 0.05f), 1.5f, "s"));

    // ---- bars (player BARS spec)
    layout.add(fl("b_Q1", "Bar fundamental Q", logRange(200, 20000), 2500));
    layout.add(fl("b_Q2", "Bar partial Q", logRange(200, 20000), 1500));
    layout.add(fl("b_ratio", "Bar partial ratio", linear(2.5f, 4.5f, 0.01f), 4, "x"));
    layout.add(fl("b_strikeF", "Mallet strike force", linear(5, 80, 1), 25, "N"));
    layout.add(fl("b_contact", "Mallet contact time", linear(0.5f, 6, 0.1f), 2, "ms"));
    layout.add(fl("b_strikePos", "Strike position", linear(0, 1, 0.01f), 0.3f));
    layout.add(fl(barsSustainGain, "Bar sustain gain", linear(0.05f, 3, 0.01f), 0.8f, "N.s/m"));
    layout.add(fl(barsBrakeGain, "Bar brake gain", linear(0.1f, 4, 0.05f), 1.5f, "N.s/m"));
    layout.add(fl("b_Fmax", "Bar coil force limit", linear(0.02f, 2, 0.01f), 0.5f, "N"));
    layout.add(fl("b_vset", "Bar sustain setpoint", linear(0.02f, 1, 0.01f), 0.2f, "m/s"));
    layout.add(fl("b_vibRate", "Bar vibrato rate", linear(1, 8, 0.1f), 5.5f, "Hz"));
    layout.add(fl("b_Fvib", "Bar spring force limit", linear(0.02f, 2, 0.01f), 0.5f, "N"));
    layout.add(fl("b_listen", "Bar pan", linear(-90, 90, 1), 0, "deg"));
    layout.add(fl("b_master", "Bar level", linear(0, 1, 0.01f), 0.6f));
    layout.add(fl("b_damper", "Bar damper (tail, 0 = off)", linear(0, 10, 0.05f), 2.5f, "s"));

    return layout;
}

} // namespace magnetika::plugin
