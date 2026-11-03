#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

#include "Parameters.h"
#include "magnetika/synth.hpp"

namespace magnetika::plugin {

class MagnetikaProcessor : public juce::AudioProcessor {
public:
    MagnetikaProcessor();
    ~MagnetikaProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Magnetika"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts_; }
    juce::MidiKeyboardState& keyboardState() { return keyboard_; }

private:
    // Performance state: what the engine is currently told. Both the plugin
    // parameters and incoming MIDI write it; whichever moved last wins, as when
    // a hardware pedal and a panel switch share one function.
    struct Perf {
        int instrument = 0;  // 0 glass, 1 bars
        bool sustain = false, brake = false, trimOn = false, n3ctl = false;
        float vibDepth = 0, rotation = 0, trimAmount = 1, pressure = 1, bendCents = 0;
    };

    void applyParameters(bool force);
    void pushPerf();
    void handleMidi(const juce::MidiMessage& m);
    void renderRange(juce::AudioBuffer<float>& buffer, int start, int end);

    juce::AudioProcessorValueTreeState apvts_;

    // Parameter value pointers, looked up once (no string lookups on the audio thread).
    struct Raw {
        std::atomic<float>*instrument, *sustain, *brake, *vibDepth, *rotation, *trimOn, *trimAmount, *pressure,
            *bendRange, *n3ctl, *coilsArmed, *massScaling, *gG, *gGb, *bG, *bGb;
    } p_{};
    juce::MidiKeyboardState keyboard_;
    std::unique_ptr<Synth> synth_;

    std::vector<std::atomic<float>*> mappedRaw_;  // parallel to engineMappings()
    std::vector<float> mappedLast_;

    // last seen value of each performance parameter, to detect a user move
    struct PerfParamLast {
        float instrument, sustain, brake, vibDepth, rotation, trimOn, trimAmount, pressure, n3ctl;
    } perfLast_{};
    float lastArmed_ = -1, lastMassScaling_ = -1;
    float lastGains_[4] = {-1, -1, -1, -1};

    Perf perf_;
    float bendWheel_ = 0;  // -1 .. 1, from the pitch wheel

    std::vector<float> scratchL_, scratchR_;  // only for a mono output bus

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaProcessor)
};

} // namespace magnetika::plugin
