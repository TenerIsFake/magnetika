#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Parameters.h"
#include "magnetika/synth.hpp"

namespace magnetika::plugin {

class MagnetikaProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MagnetikaProcessor();
    ~MagnetikaProcessor() override { stopTimer(); }

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
    // A MIDI program change moves the `instrument` parameter, so the parameter is the one
    // source of truth for the editor and the host. The audio thread only records it; this
    // (message thread, on a 20 Hz timer) writes it.
    void syncProgramChange();
    juce::MidiKeyboardState& keyboardState() { return keyboard_; }

    // ---- editor -> audio: touch gestures (message thread only; single producer)
    struct UiEvent {
        enum Type : unsigned char { NoteOn, NoteOff, Touch, FingerSpeed, StrikePos, AllOff } type;
        int midi;
        float value;  // velocity, touch, rad/s, or 0..1 strike position
    };
    bool pushUiEvent(const UiEvent& e);

    // ---- audio -> editor: what to draw (published after every block)
    struct Display {
        std::array<Synth::VoiceView, Synth::kMaxVoices> voices{};
        int count = 0;
        float peakL = 0, peakR = 0;  // output peak over the last block
        bool limiting = false;       // the tanh output stage is audibly compressing
        std::uint32_t midiCount = 0; // increments with every MIDI message received
        bool sustain = false, brake = false;
        float rotation = 0, vibDepth = 0;
        int instrument = 0;
    };
    Display readDisplay() const;

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
    void drainUiEvents();
    void publishDisplay(const juce::AudioBuffer<float>& buffer);
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

    std::atomic<float>* spindleOnRaw_ = nullptr;
    float lastSpindleOn_ = -1;

    juce::AbstractFifo uiFifo_{1024};
    std::array<UiEvent, 1024> uiBuf_{};
    // A release must never be lost: if the queue is full when a NoteOff / AllOff
    // arrives (a host that has stopped calling processBlock), these flags make
    // the next block release every note / silence everything instead.
    std::atomic<bool> releaseAllPending_{false}, allOffPending_{false};
    std::atomic<int> pendingProgram_{-1};  // instrument from a MIDI program change, not yet in the parameter
    void timerCallback() override { syncProgramChange(); }

    // seqlock: odd while the audio thread writes, even when the copy is consistent
    std::atomic<std::uint32_t> dispSeq_{0};
    Display disp_;
    mutable Display lastGood_;  // message thread only
    std::uint32_t midiCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaProcessor)
};

} // namespace magnetika::plugin
