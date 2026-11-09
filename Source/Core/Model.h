/*
  ==============================================================================
   S6000 Emulation - In-memory data model (MULTIs, PROGRAMs, SAMPLEs, FX)
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "../Formats/AkaiFormats.h"

namespace s6k
{
constexpr int maxVoices        = 128;   // S6000: 128 voice polyphony
constexpr int maxParts         = 128;   // parts per multi
constexpr int maxMultis        = 128;
constexpr int numOutputPairs   = 8;     // 16 outs as 8 stereo pairs
constexpr int numFxChannels    = 4;     // FX1, FX2, RV3, RV4
constexpr const char* osVersion = "2.14";

//==============================================================================
struct Sample : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Sample>;

    juce::String name;
    juce::AudioBuffer<float> buffer;      // 1 (mono) or 2 (stereo) channels
    double sampleRate = 44100.0;
    int rootNote = 60, semitone = 0, cents = 0;
    juce::int64 start = 0, end = 0;
    bool loopEnabled = false;
    juce::int64 loopStart = 0, loopEnd = 0;
    int loopXfade = 0;                    // frames
    int loopDirection = 0;                // 0 FORWARDS, 1 ALTERNATING
    bool loopLock = false;
    int playMode = fmt::PlayLoopInRelease;
    bool isVirtual = false;
    std::atomic<int> version { 0 };       // bumps on every edit (waveform caches)
    juce::MemoryBlock flacCache;          // session storage cache
    int flacCacheVersion = -1;

    int numFrames() const   { return buffer.getNumSamples(); }
    int numChannels() const { return buffer.getNumChannels(); }
    bool isStereo() const   { return buffer.getNumChannels() > 1; }
    size_t bytesUsed() const { return (size_t) numFrames() * (size_t) numChannels() * 2; } // as 16 bit on HW

    void touch() { ++version; }
    void sanitiseMarkers();
    Sample::Ptr clone (const juce::String& newName) const;
};

//==============================================================================
struct Program : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Program>;

    fmt::ProgramData data;
    std::vector<std::array<Sample::Ptr, 4>> resolved;  // zone samples, parallel to data.keygroups

    juce::String getName() const { return juce::String (data.name); }
    void setName (const juce::String& n) { data.name = n.substring (0, 20).toStdString(); }
    int numKeygroups() const { return (int) data.keygroups.size(); }
};

//==============================================================================
struct FxMultiChannel
{
    int distType = 0;           // 0 OFF 1 DISTORTION 2 RING MOD
    int distAmount = 30, distLevel = 70, rmodFreq = 40, rmodDepth = 50;
    int eqOn = 0;
    int lowFreq = 100, lfGain = 0, lmidFreq = 400, lmidGain = 0, hmidFreq = 2500, hmidGain = 0, highFreq = 8000, hfGain = 0;
    int modType = 1;            // 0 OFF 1 CHORUS 2 FLANGE 3 PHASE 4 ROTARY 5 FREQ/AMP MOD 6 PITCH SHIFT 7 PITCH+FEEDBACK
    int modRate = 20, modDepth = 40, modFeedback = 0;
    int rotSpeed1 = 15, rotSpeed2 = 70, rotAccel = 50, rotInitSpeed = 0;
    int pitchLSemi = -12, pitchLFine = 0, pitchRSemi = 12, pitchRFine = 0;
    int amodRate = 30, amodDepth = 0;
    int delayType = 0;          // 0 OFF 1 MONO LEFT 2 MONO L/R 3 XOVER L+R 4 STEREO DELAY
    int delayL = 250, delayR = 375, fbL = 30, fbR = 30, delayHfDamp = 30, pingPong = 0;
    int revOn = 1, revType = 2, preDelay = 20, revTime = 50, diffusion = 60, revHfDamp = 40, revLfDamp = 0;
    int revLevel = 50, revPan = 0;
    int distEqLevel = 100, modDelLevel = 100, modDelPan = 0, modDelWidth = 100, directSignal = 1;
    int path = 0;               // 0 MOD>REV  1 MOD||REV  2 REV>MOD
};

struct FxReverbChannel
{
    int on = 1, input = 0;      // input 0 SEND, 1 FX1, 2 FX2
    int inputLevel = 100;
    int type = 3, preDelay = 30, time = 60, diffusion = 70, hfDamp = 40, lfDamp = 0, level = 60, pan = 0;
};

struct FxSettings
{
    FxMultiChannel fx[2];
    FxReverbChannel rv[2];
    int bypass = 0;
    fmt::Bytes serialise() const;
    void deserialise (const fmt::Bytes& b);
};

//==============================================================================
struct Multi : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Multi>;

    juce::String name { "NEW MULTI" };
    std::vector<fmt::PartData> parts;              // 128
    std::vector<Program::Ptr> resolved;            // parallel to parts
    std::array<bool, maxParts> solo {};
    FxSettings fx;

    Multi();
    int numUsedParts() const;
};

//==============================================================================
/** Everything held in sampler memory. Structural changes must be made
    while holding Memory::lock (the audio thread try-locks it once per block). */
struct Memory
{
    juce::SpinLock lock;

    juce::ReferenceCountedArray<Sample>  samples;
    juce::ReferenceCountedArray<Program> programs;
    juce::ReferenceCountedArray<Multi>   multis;

    std::atomic<int> currentMulti { 0 };

    size_t capacityBytes = (size_t) 256 * 1024 * 1024;   // S6000 fully expanded: 256 MB

    Memory();

    Multi* getCurrentMulti() const;
    Sample::Ptr findSample (const juce::String& name) const;
    Program::Ptr findProgram (const juce::String& name) const;
    int indexOfSample (const Sample* s) const;
    int indexOfProgram (const Program* p) const;

    void resolveProgram (Program& p) const;     // name -> sample pointers
    void resolveMulti (Multi& m) const;         // name -> program pointers
    void resolveAll();

    size_t bytesUsed() const;
    int percentFree() const;
    juce::String uniqueSampleName (const juce::String& base) const;
    juce::String uniqueProgramName (const juce::String& base) const;
    juce::String uniqueMultiName (const juce::String& base) const;

    void clear();
};

//==============================================================================
namespace names
{
    juce::String note (int midiNote, bool c3is60 = true);
    juce::String midiChannel (int ch);              // 0..31 -> 1A..16B
    juce::String output (int out, bool allowMulti);
    juce::String fxChannel (int ch);
    juce::String playback (int mode, bool allowAsSample);
    juce::String filterType (int t);
    juce::String lfoWave (int w);
    juce::String modSource (int s);
    juce::String tuneTemplate (int t);
    juce::String reverbType (int t);
    juce::String modFxType (int t);
    juce::String delayType (int t);
    juce::String signedValue (int v, int digits = 3);
    juce::String panValue (int v);
    juce::String levelDb (int tenths);
    int numFilterTypes();
    int numModSources();
    int numTuneTemplates();
}

/** Tuning templates (cents offset per pitch class, relative to KEY). */
float tuningOffsetCents (int templ, int key, int pitchClass, const int* user);

} // namespace s6k
