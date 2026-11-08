/*
  ==============================================================================
   S6000 Emulation - Akai file formats (pure C++17)
     * S1000 / S3000 samples and programs (from Akai disks / CD-ROMs)
     * S5000 / S6000 .AKP programs and .AKM multis (RIFF based)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s6k::fmt
{
using Bytes = std::vector<uint8_t>;

//==============================================================================
// Akai S900/S1000/S3000 character set:  0-9, space, A-Z, #, +, -, .
std::string akaiToAscii (const uint8_t* p, size_t n);
void asciiToAkai (const std::string& s, uint8_t* out, size_t n);
bool isValidAkaiName (const uint8_t* p, size_t n);

/** Header sanity check used both by the file system and the raw "deep scan". */
bool isS1000SampleHeader (const uint8_t* p, size_t n);
bool isS1000ProgramHeader (const uint8_t* p, size_t n);

//==============================================================================
enum PlaybackMode   // S5000/S6000 zone / sample playback modes
{
    PlayNoLoop = 0, PlayOneShot = 1, PlayLoopInRelease = 2, PlayLoopUntilRelease = 3, PlayAsSample = 4
};

struct SampleData
{
    std::string name;
    double sampleRate = 44100.0;
    int rootNote = 60;          // C3 = 60 (Akai convention)
    int semitone = 0, cents = 0;
    int numChannels = 1;
    std::vector<float> ch[2];   // de-interleaved PCM, -1..+1
    int64_t start = 0, end = 0; // play markers
    bool loopEnabled = false;
    int64_t loopStart = 0, loopEnd = 0;
    int playMode = PlayLoopInRelease;
};

bool parseS1000Sample (const Bytes& file, bool isS3000, SampleData& out, std::string& error);

//==============================================================================
struct ZoneData
{
    std::string sampleName;
    int lowVel = 0, highVel = 127;
    int fine = 0, semi = 0;
    int filter = 0;
    int pan = 0;              // -50..+50
    int playback = PlayAsSample;
    int output = 0;           // 0 = MULTI, 1.. = 1/2,3/4 ... L R pairs etc.
    int level = 0;            // dB  -100..+100 (tenths internally on HW, we use 0.1dB units / 10)
    bool keyTrack = true;
    int velStart = 0;
};

struct EnvData
{
    int attack = 0, decay = 50, sustain = 100, release = 15;  // 0..100
    int velAttack = 0, keyscale = 0, onVelRelease = 0, offVelRelease = 0;
    int depth = 0;           // filter env depth -100..+100
};

struct AuxEnvData
{
    int rate[4]  = { 0, 50, 50, 15 };
    int level[4] = { 100, 100, 100, 0 };
    int velRate1 = 0, keyRate2 = 0, keyRate4 = 0, velLevel = 0;
};

struct KeygroupData
{
    int lowNote = 21, highNote = 127;
    int semi = 0, fine = 0;
    int fxOverride = 0, fxSend = 0;
    int pitchMod1 = 0, pitchMod2 = 0, ampMod = 0;
    bool zoneXfade = false;
    int muteGroup = 0;
    int filterMode = 0, cutoff = 100, resonance = 0, filterKeyTrack = 0;
    int filterMod1 = 0, filterMod2 = 0, filterMod3 = 0, attenuation = 0;
    EnvData amp, filt;
    AuxEnvData aux;
    ZoneData zones[4];
};

struct LfoData
{
    int waveform = 1;    // 0 SINE 1 TRIANGLE 2 SQUARE 3 SQUARE+ 4 SQUARE- 5 SAW BI 6 SAW UP 7 SAW DOWN 8 RANDOM
    int rate = 43, delay = 0, depth = 0;
    int syncOrRetrigger = 0;
    int modwheel = 0, aftertouch = 0;
    int rateMod = 0, delayMod = 0, depthMod = 0;
};

struct ProgramData
{
    std::string name;
    int midiProgram = 0;
    int loudness = 85, velSens = 25;
    int ampMod1 = 0, ampMod2 = 0, panMod1 = 0, panMod2 = 0, panMod3 = 0;
    int semi = 0, fine = 0;
    int tuneTemplate = 0, key = 0;
    int userTune[12] = {};
    int bendUp = 2, bendDown = 2, bendMode = 0, aftertouchBend = 0;
    int polyphony = 32;
    bool keygroupXfade = false;
    LfoData lfo1, lfo2;

    // Assignable Program Modulation: source for each modulation input
    enum ModInput { AmpMod1, AmpMod2, PanMod1, PanMod2, PanMod3, PitchMod1, PitchMod2, KgAmpMod,
                    FiltMod1, FiltMod2, FiltMod3, Lfo1Rate, Lfo1Delay, Lfo1Depth, Lfo2Rate, Lfo2Delay, Lfo2Depth,
                    NumModInputs };
    int modSource[NumModInputs] = { 8, 4, 10, 8, 1, 9, 0, 5, 5, 10, 11, 8, 8, 8, 0, 0, 0 };
    std::vector<KeygroupData> keygroups;
};

/** isS3000: -1 = detect, 0 = S1000 (150 byte records), 1 = S3000 (192 byte records) */
bool parseS1000Program (const Bytes& file, ProgramData& out, std::string& error, int isS3000 = -1);
bool parseAkp (const Bytes& file, ProgramData& out, std::string& error);
Bytes writeAkp (const ProgramData& p);

//==============================================================================
struct PartData
{
    std::string programName;
    int midiChannel = 0;     // 0..31 (1A..16A, 1B..16B)
    int level = 80;          // 0..100
    int pan = 0;             // -50..+50
    int output = 0;
    int fxChannel = 0;       // 0 OFF 1 FX1 2 FX2 3 RV3 4 RV4
    int fxSend = 25;
    int semi = 0, fine = 0;
    int lowKey = 21, highKey = 127;
    bool mute = false;
};

struct MultiData
{
    std::string name;
    std::vector<PartData> parts;
    Bytes fxBlob;            // serialized EB20 effects settings ("mfx " chunk)
};

bool parseAkm (const Bytes& file, MultiData& out, std::string& error);
Bytes writeAkm (const MultiData& m);

} // namespace s6k::fmt
