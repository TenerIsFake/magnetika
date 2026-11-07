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
    bool supportsMPE() const override { return true; }

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
    // Likewise an MPE Configuration Message moves the `MPE` switch (message thread).
    void syncMpe();
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
        bool mpe = false;  // member channels are read as per-note expression
    };
    Display readDisplay() const;

    // ---- per-bowl level (glass): the evening curve (a parameter) plus the player's own
    // trim for each bowl. Trims are set from the editor (message thread), saved with the
    // project and not with presets: they belong to the instrument, like its tuning.
    static constexpr float kTrimMinDb = -24.0f, kTrimMaxDb = 12.0f;
    void setBowlTrimDb(int midi, float db);
    float bowlTrimDb(int midi) const;
    void resetBowlTrims();
    float bowlGainDb(int midi) const;  // what the bowl plays at now: evening + trim

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
    // ---- MPE (audio thread)
    bool isMpeMember(int channel) const;
    bool handleRpn(int channel, int cc, int value);  // true if the CC was RPN plumbing
    bool handleMpeMember(int channel, const juce::MidiMessage& m);  // false: not per-note, use the global map
    void releaseMemberNote(int channel, int midi);
    int bowlFor(int channel) const;    // which bowl a glass glide on this channel should be on
    float springCents(int channel) const;  // the bend left for that bowl's coils, clamped
    void followGlide(int channel, bool mayHandOff = true);
    static double pressureToLoad(float pressure);
    void applyPressure(int channel);  // the channel's pressure to its bowl, once it has taken over
    void setZones(int lower, int upper);
    void applySlide(int midi, float slide, bool atNoteOn);
    void resetMpeNotes();
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
    std::atomic<int> pendingMpe_{-1};      // MPE on/off from a configuration message, not yet in the parameter
    std::atomic<float> pendingMpeBendRange_{-1};  // from RPN 0 on a member channel, likewise
    // The zone layout is not a parameter; it is saved with the state so a project
    // reopens with the zones the controller configured (controllers do not resend it).
    std::atomic<int> zonesShared_{-1};     // lower * 16 + upper, published by the audio thread
    std::atomic<int> pendingZones_{-1};    // restored from a saved state, applied on the audio thread
    void timerCallback() override {
        syncProgramChange();
        syncMpe();
    }

    // MPE state. Zones as the MPE spec: a lower zone has master channel 1 and members
    // 2.., an upper zone master 16 and members ..15. With no configuration message,
    // switching MPE on gives the usual lower zone of 15 members.
    struct MpeChannel {
        int note = -1;        // the bowl or bar this member channel is playing
        int base = -1;        // the note it was given (differs from `note` after a glide)
        float velocity = 0.8f;
        bool pressing = false;  // pressure has overtaken the strike's load since the note-on
        float bend = 0;       // -1 .. 1
        float pressure = -1;  // 0 .. 1, or -1 until the controller sends one
        float slide = -1;     // CC74 0 .. 1, or -1 until sent
    };
    std::array<MpeChannel, 17> mpeCh_{};          // by MIDI channel 1..16
    std::array<unsigned char, 128> mpeHeld_{};    // member channels holding each note
    std::array<int, 17> rpnMsb_{}, rpnLsb_{};     // RPN selection per channel (127 = none)
    int lowerMembers_ = 0, upperMembers_ = 0;  // switching MPE on with no zones gives a lower zone of 15
    float memberBendRange_ = 48;  // semitones of a full member bend (the parameter, or RPN 0)
    float lastMpeBendRangeParam_ = -1;
    float lastBuild_[3] = {-1, -1, -1};  // rev B sizes, instrument No., variation
    float lastSlideInputs_[3] = {-1, -1, -1};  // spindle switch, spin, finger speed: slide depends on them
    bool mpeActive_ = false;
    float lastMpeParam_ = -1;
    std::atomic<float>*mpeRaw_ = nullptr, *mpeBendRangeRaw_ = nullptr, *fingerSpeedRaw_ = nullptr, *spinRaw_ = nullptr,
                       *mpeGlideRaw_ = nullptr, *revBRaw_ = nullptr, *serialRaw_ = nullptr, *variationRaw_ = nullptr,
                       *levelEvenRaw_ = nullptr;
    std::array<std::atomic<float>, 128> trimDb_{};
    std::atomic<std::uint32_t> trimsVersion_{0};  // bumped by every trim change
    std::uint32_t appliedTrims_ = ~0u;            // audio thread: the version the engine has
    float lastLevelEven_ = -1;
    void applyBowlLevels();

    // seqlock: odd while the audio thread writes, even when the copy is consistent
    std::atomic<std::uint32_t> dispSeq_{0};
    Display disp_;
    mutable Display lastGood_;  // message thread only
    std::uint32_t midiCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetikaProcessor)
};

} // namespace magnetika::plugin
