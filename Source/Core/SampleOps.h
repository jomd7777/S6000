/*
  ==============================================================================
   S6000 Emulation - offline sample editing (EDIT SAMPLE functions)
   MASTER, TRIM, CHOP, LOOP, JOIN, MIX, FADE UP/DOWN, TIMESTRETCH,
   PITCH SHIFT, BPM MATCH, RE-SAMPLE, EQ
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include "Model.h"

namespace s6k::ops
{
float peakLevel (const Sample& s);
void normalise (Sample& s, float targetDb = 0.0f);
void reverse (Sample& s, juce::int64 from, juce::int64 to);
void makeMono (Sample& s);
void rescale (Sample& s, float gainDb);
void discardOutside (Sample& s, juce::int64 keepStart, juce::int64 keepEnd);
void fade (Sample& s, juce::int64 from, juce::int64 to, bool fadeUp, int curve);     // curve 0 LINEAR 1 EXP 2 LOG
void crossfadeLoop (Sample& s, int xfadeFrames, int profile);                         // profile 0 LINEAR 1 EQUAL POWER
juce::int64 findZeroCrossing (const Sample& s, juce::int64 pos, int direction);
juce::int64 autoFindLoopEnd (const Sample& s, juce::int64 loopStart, juce::int64 approxEnd, int searchFrames);
Sample::Ptr join (const Sample& a, const Sample& b, const juce::String& name);
void mix (Sample& a, const Sample& b, float levelA, float levelB, juce::int64 offset);
void timeStretch (Sample& s, double ratio);                                           // ratio > 1 = longer
void pitchShift (Sample& s, float semitones);
void resample (Sample& s, double newRate);
void eq (Sample& s, float lowDb, float midFreq, float midDb, float highDb);
std::vector<juce::int64> equalSlices (const Sample& s, int numSlices);
std::vector<juce::int64> detectTransients (const Sample& s, int sensitivity /*0..100*/, int maxSlices);
std::vector<Sample::Ptr> chop (const Sample& s, const std::vector<juce::int64>& points, const juce::String& prefix);
double estimateBpm (const Sample& s, int beats);

//==============================================================================
// EDIT SAMPLE (manual p.141-177) - process helpers used by the pages.
// Long processes report progress / can be aborted through these two flags
// (only one process runs at a time, on a background thread).
extern std::atomic<float> progress;      // 0..1
extern std::atomic<bool> abortFlag;

/** FIND START / FIND END: first / last frame whose level exceeds the threshold */
juce::int64 findLevel (const Sample& s, float thresholdDb, bool fromStart, juce::int64 lo, juce::int64 hi);
void erase (Sample& s, juce::int64 a, juce::int64 b);          // CHOP > ERASE : silence, keep the gap
void cut (Sample& s, juce::int64 a, juce::int64 b);            // CHOP > CUT   : remove and butt the rest together
Sample::Ptr extract (const Sample& s, juce::int64 a, juce::int64 b, const juce::String& name);   // CHOP > EXTRACT

/** Fade / X-Fade curves: 0 LINEAR 1 LOG 2 SINE  (x 0..1 rising) */
float curveGain (double x, int curve);
void fadeUpDown (Sample& s, juce::int64 upEnd, int upCurve, juce::int64 downStart, int downCurve);

struct Range { const Sample* s = nullptr; juce::int64 from = 0, to = 0; float gainDb = 0.0f; };
Sample::Ptr joinRanges (const Range& first, const Range& second, juce::int64 xfade, int curve, const juce::String& name);
Sample::Ptr mixRanges (const Range& a, const Range& b, const juce::String& name);
Sample::Ptr makeStereo (const Range& left, const Range& right, const juce::String& name);

/** TIMESTRETCH / PITCH SHIFT presets: 18 types x A/B/C = 54 */
juce::String stretchPresetName (int preset);
constexpr int numStretchPresets = 54;
void timeStretchPreset (Sample& s, double ratio, int preset);
void pitchShiftPreset (Sample& s, float semitones, int preset);

/** RE-SAMPLE: quality 0 LOW (linear) 1 MED (Lagrange) 2 HIGH (windowed sinc) */
void resampleQ (Sample& s, double newRate, int quality);
void requantise (Sample& s, int bits);

/** 3 band parametric EQ (LF / HF shelves 6 or 12 dB/oct, MF bell with Q) */
struct EqSettings
{
    float lf = 100.0f,   lfGain = 0.0f;  int lfSlope = 1;      // 0 = 6dB/oct 1 = 12dB/oct
    float mf = 1000.0f,  mfGain = 0.0f;  float mfQ = 1.0f;     // 0.5 .. 20
    float hf = 10000.0f, hfGain = 0.0f;  int hfSlope = 1;
    float outDb = 0.0f;
};
void eq3 (Sample& s, const EqSettings& e);
float eqResponseDb (const EqSettings& e, double sampleRate, double freq);
}
