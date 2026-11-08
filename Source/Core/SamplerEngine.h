/*
  ==============================================================================
   S6000 Emulation - 128 voice sampler engine
     * 4 velocity zones per keygroup, up to 99 keygroups per program
     * 26 resonant filter types, AMP / FILTER ADSR, 4-stage AUX envelope
     * 2 LFOs per voice, Assignable Program Modulation (APM)
     * 128 part multi, 1A..16B channel mapping, mute groups, FX sends
   CPU strategy: modulation is computed at control rate (every 32 samples),
   idle voices cost nothing, filters bypass when fully open.
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include "Model.h"
#include "Dsp.h"
#include "VoiceFilter.h"

namespace s6k
{


//==============================================================================
struct EngineSettings
{
    std::atomic<int> editProgram { -1 };       // >= 0 : play this program on every channel (EDIT PROGRAM)
    std::atomic<int> editSample { -1 };        // >= 0 : EDIT SAMPLE, MONITOR = RAW SAMPLE -> play this sample chromatically
    std::atomic<bool> mergeBtoA { true };
    std::atomic<int> transpose { 0 }, semitoneTune { 0 }, fineTune { 0 }, masterLevelDb { 0 };
    std::atomic<int> extApmController { 16 };
    std::atomic<bool> programChange { true };
    std::atomic<bool> multiSelect { false };
    std::atomic<int> multiSelectChannel { 0 };
    std::atomic<int> interpolation { 1 };      // 0 LINEAR (lowest CPU), 1 HERMITE
    std::atomic<int> playKey { 60 };
    std::atomic<bool> rxNotes { true }, rxPitchbend { true }, rxControllers { true }, rxAftertouch { true }, rxProgramChange { true };
    std::atomic<int> maxPolyphony { maxVoices };
};

//==============================================================================
/** Part parameters copied into a voice (no strings -> no allocation on the audio thread). */
struct VoicePart
{
    int level = 80, pan = 0, semi = 0, fine = 0, output = 0, fxChannel = 1, fxSend = 25;
    static VoicePart from (const fmt::PartData& p) { return { p.level, p.pan, p.semi, p.fine, p.output, p.fxChannel, p.fxSend }; }
};

//==============================================================================
struct Voice
{
    bool active = false;
    int note = 60, channel = 0, velocity = 100, offVelocity = 64;
    int partIndex = -1, kgIndex = 0, zoneIndex = 0;
    juce::uint32 age = 0;
    Program::Ptr program;
    Sample::Ptr sample;
    VoicePart part;

    double pos = 0, baseRatio = 1;
    int dir = 1;
    int playMode = 0;
    bool released = false, sustained = false;
    float staticSemis = 0, heldBend = 0;
    float kgXfadeGain = 1, zoneGain = 1, staticGain = 1, staticPan = 0;

    dsp::Adsr ampEnv, filtEnv;
    dsp::AuxEnv auxEnv;
    dsp::Lfo lfo1, lfo2;
    float lfo1Time = 0, lfo2Time = 0;
    VoiceFilter filter[2];

    float lfo1Val = 0, lfo2Val = 0, envA = 0, envF = 0, envX = 0;
    float inc = 1;
    float dgL = 0, dgR = 0;
    float gL = 0, gR = 0, tgL = 0, tgR = 0;     // current / target gains (ramped)
    float sendGain = 0;
    int fxChannel = 0, outPair = 0, monoChannel = -1;
    int controlCounter = 0;
    float fadeOut = -1.0f;                       // >= 0 when being stolen / choked
};

//==============================================================================
class SamplerEngine
{
public:
    explicit SamplerEngine (Memory& m);

    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** Renders one block. outBus = 16 channels (8 stereo pairs),
        fxBus = 8 channels (stereo sends for FX1, FX2, RV3, RV4). */
    void render (juce::AudioBuffer<float>& outBus, juce::AudioBuffer<float>& fxBus, juce::MidiBuffer& midi, int numSamples);

    /** UI injected notes (ENT/PLAY, play key) – thread safe. */
    void injectNote (int channel, int note, int velocity, bool on);
    void allNotesOff();

    /** Audition a sample (LOAD / EDIT SAMPLE). Pass nullptr to stop. */
    void audition (Sample::Ptr s, bool loopRegionOnly = false, juce::int64 from = -1, juce::int64 to = -1);
    bool isAuditioning() const { return auditionActive.load(); }
    juce::int64 getAuditionPosition() const { return (juce::int64) auditionPosAtomic.load(); }

    /** EDIT SAMPLE: the waveform shows a play line for every voice playing this sample (MIDI keyboard) */
    void setWatchedSample (const Sample* s) { watchedSample = s; }
    int getWatchedPositions (juce::int64* out, int maxCount) const
    {
        const int n = juce::jmin (maxCount, watchCount.load());
        for (int i = 0; i < n; ++i) out[i] = (juce::int64) watchPos[(size_t) i].load();
        return n;
    }

    int getActiveVoiceCount() const { return activeVoices.load(); }
    bool getAndClearMidiActivity() { return midiActivity.exchange (false); }

    struct ProgramChangeRequest { int channel = 0, program = 0; };
    bool popProgramChange (ProgramChangeRequest& r);

    EngineSettings settings;
    std::atomic<float> auditionGain { 1.0f };

private:
    Memory& mem;
    Program::Ptr rawProgram;     // one full-range keygroup used to play a raw sample (EDIT SAMPLE)
    double sr = 44100.0;
    float crHz = 1378.0f;
    std::array<Voice, maxVoices> voices;
    juce::uint32 ageCounter = 0;
    double globalTime = 0;

    float modwheel[32] {}, aftertouch[32] {}, pitchbend[32] {}, extCtrl[32] {}, chanVolume[32];
    bool sustainPedal[32] {};
    std::atomic<int> activeVoices { 0 };
    std::atomic<bool> midiActivity { false };

    juce::MidiMessageCollector injected;
    juce::MidiBuffer injectedBuffer;

    // audition
    Sample::Ptr auditionSample, pendingAudition;
    Sample::Ptr auditionHold, auditionHoldPrev;                          // message thread keeps the last reference (no frees on the audio thread)
    std::atomic<const Sample*> watchedSample { nullptr };
    std::array<std::atomic<float>, 8> watchPos {};
    std::atomic<int> watchCount { 0 };
    std::atomic<bool> auditionPending { false }, auditionActive { false };
    juce::SpinLock auditionLock;
    double auditionPos = 0, auditionEnd = 0, auditionLoopStart = -1;
    std::atomic<double> auditionPosAtomic { 0 };

    // program change requests (audio -> message thread)
    juce::AbstractFifo pcFifo { 64 };
    std::array<ProgramChangeRequest, 64> pcData;

    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int ch, int note, int vel);
    void noteOff (int ch, int note, int vel);
    void triggerProgram (Program* p, const VoicePart& part, int partIndex, int ch, int note, int vel);
    void startVoice (Voice& v, Program* p, int kg, int zone, Sample* s, const VoicePart& part, int partIndex,
                     int ch, int note, int vel, float xfadeGain, float zoneGain);
    Voice* allocateVoice (Program* p, int partIndex);
    void updateVoiceControl (Voice& v);
    void renderVoice (Voice& v, juce::AudioBuffer<float>& outBus, juce::AudioBuffer<float>& fxBus, int start, int num);
    void renderAudition (juce::AudioBuffer<float>& outBus, int num);
    float modSource (const Voice& v, int src) const;
};

} // namespace s6k
