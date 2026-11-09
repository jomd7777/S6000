/*
  ==============================================================================
   S6000 Emulation - per-voice resonant filter, 26 types (FILTER page)
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#pragma once
#include "Dsp.h"

namespace s6k
{
struct VoiceFilter
{
    int type = 0;
    bool bypass = true;
    float gain = 1.0f, morph = 0.0f;
    dsp::Svf a, b;
    dsp::OnePole hp;
    dsp::Allpass1 ap[12];
    float fb = 0.0f, fbAmt = 0.0f;
    int stages = 4;

    void reset();
    void setup (int filterType, float cutoffValue, float resonance01, float sr);
    float process (float x);
};

} // namespace s6k
