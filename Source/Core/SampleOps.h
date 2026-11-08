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
}
