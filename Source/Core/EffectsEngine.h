/*
  ==============================================================================
   S6000 Emulation - EB20 style 4 channel effects processor
     FX1 / FX2 : DISTORTION | RING MOD -> EQ -> MOD (chorus, flange, phase,
                 rotary, freq/amp mod, pitch shift, pitch+fdback) -> DELAY -> REVERB
                 + OUTPUT MIX (path / levels / width)
     RV3 / RV4 : reverb only, input from its send or from FX1 / FX2
   CPU: a channel only runs while it receives signal or its tail is ringing.
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#pragma once
#include "Model.h"
#include "Dsp.h"

namespace s6k
{
class ReverbUnit
{
public:
    void prepare (double sr);
    void reset();
    void setParams (int type, int preDelayMs, int time, int diffusion, int hfDamp, int lfDamp, double sr);
    void process (float* l, float* r, int n);   // in-place, 100% wet

private:
    juce::Reverb verb;
    dsp::DelayLine preL, preR;
    dsp::OnePole lfL, lfR;
    int preSamples = 0, type = 0;
    float lfCut = 0;
    float gateEnv = 0, gateHold = 0, gateThresh = 0.02f;
    double rate = 44100;
};

class FxChannelUnit
{
public:
    void prepare (double sr, int maxBlock);
    void reset();
    void process (const FxMultiChannel& p, float* l, float* r, int n);

private:
    double sr = 44100;
    dsp::Biquad eq[2][4];
    int eqHash = 0;
    float ringPhase = 0;
    dsp::DelayLine modL, modR;
    float modPhase = 0, rotSpeed = 0, rotPhaseH = 0, rotPhaseL = 0, amPhase = 0;
    dsp::Allpass1 phL[6], phR[6];
    float phFbL = 0, phFbR = 0;
    float psPhase = 0;
    dsp::DelayLine delL, delR;
    dsp::OnePole delDampL, delDampR;
    ReverbUnit reverb;
    int revHash = 0;
    std::vector<float> tmpL, tmpR, revL, revR, modOutL, modOutR;

    void processMod (const FxMultiChannel& p, float* l, float* r, int n);
    void processDelay (const FxMultiChannel& p, float* l, float* r, int n);
};

class EffectsEngine
{
public:
    void prepare (double sr, int maxBlock);
    void reset();

    /** fxBus : 8 channels (FX1 L/R, FX2 L/R, RV3 L/R, RV4 L/R sends).
        Wet returns are added to mainL / mainR. */
    void process (const FxSettings& s, juce::AudioBuffer<float>& fxBus, float* mainL, float* mainR, int n);

    float getChannelLevel (int ch) const { return levels[(size_t) juce::jlimit (0, 3, ch)].load(); }

private:
    FxChannelUnit fx[2];
    ReverbUnit rv[2];
    int rvHash[2] {};
    int tail[4] {};
    int tailBlocks = 200;
    double rate = 44100.0;
    std::array<std::atomic<float>, 4> levels {};
    std::vector<float> fxOutL[2], fxOutR[2];
};

} // namespace s6k
