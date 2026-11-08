/*
  ==============================================================================
   S6000 Emulation - lightweight DSP building blocks (CPU friendly)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include <cmath>

namespace s6k::dsp
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;
constexpr int controlRate = 32;     // modulation is computed once every 32 samples

inline float fastTanh (float x)
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float fastExp2 (float x) { return std::exp2 (x); }

/** 0..100 value -> filter cutoff frequency 20 Hz .. 20 kHz */
inline float cutoffToHz (float c) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 110.0f, c) * 0.01f); }

/** 0..100 envelope rate -> milliseconds (0 = instant, 100 = ~20 s) */
inline float envTimeMs (float v)
{
    if (v <= 0.0f) return 0.0f;
    return std::pow (20000.0f, juce::jlimit (0.0f, 100.0f, v) * 0.01f);
}

/** 0..100 LFO rate -> Hz ( 0 = ~60 s per cycle, 45 = ~5.5 Hz vibrato, 100 = 100 Hz ) */
inline float lfoRateHz (float r)
{
    r = juce::jlimit (0.0f, 100.0f, r) * 0.01f;
    return 0.0166f + 100.0f * std::pow (r, 3.63f);
}

//==============================================================================
/** Topology-preserving-transform state variable filter (Zavalishin). */
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;

    void set (float hz, float q, float sr)
    {
        hz = juce::jlimit (10.0f, sr * 0.45f, hz);
        g = std::tan (juce::MathConstants<float>::pi * hz / sr);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }

    void reset() { ic1 = ic2 = 0; }
};

struct OnePole
{
    float z = 0, a = 0;
    void set (float hz, float sr) { a = std::exp (-twoPi * juce::jlimit (5.0f, sr * 0.45f, hz) / sr); }
    inline float lp (float x) { z = x + a * (z - x); return z; }
    inline float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

struct Allpass1
{
    float z = 0, a = 0;
    void set (float hz, float sr)
    {
        const float t = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, sr * 0.45f, hz) / sr);
        a = (t - 1.0f) / (t + 1.0f);
    }
    inline float tick (float x) { const float y = a * x + z; z = x - a * y; return y; }
    void reset() { z = 0; }
};

/** RBJ biquad (EQ / offline processing). */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void setPeak (float sr, float f, float q, float gainDb)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), w = twoPi * juce::jlimit (10.0f, sr * 0.45f, f) / sr;
        const float al = std::sin (w) / (2.0f * q), c = std::cos (w);
        norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (float sr, float f, float gainDb)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), w = twoPi * juce::jlimit (10.0f, sr * 0.45f, f) / sr;
        const float c = std::cos (w), s = std::sin (w), al = s / 2.0f * std::sqrt (2.0f), sa = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
              (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void setHighShelf (float sr, float f, float gainDb)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f), w = twoPi * juce::jlimit (10.0f, sr * 0.45f, f) / sr;
        const float c = std::cos (w), s = std::sin (w), al = s / 2.0f * std::sqrt (2.0f), sa = 2 * std::sqrt (A) * al;
        norm (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
              (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    void setLowPass (float sr, float f, float q)
    {
        const float w = twoPi * juce::jlimit (10.0f, sr * 0.45f, f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighPass (float sr, float f, float q)
    {
        const float w = twoPi * juce::jlimit (10.0f, sr * 0.45f, f) / sr, al = std::sin (w) / (2 * q), c = std::cos (w);
        norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

private:
    void norm (float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
};

//==============================================================================
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    inline float readFrac (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = read (i), b = read (i + 1);
        return a + f * (b - a);
    }
};

//==============================================================================
/** LFO waveform, phase 0..1 -> -1..+1 */
inline float lfoWave (int wave, float phase, float randomValue)
{
    switch (wave)
    {
        case 0: return std::sin (twoPi * phase);
        case 1: return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        case 2: return phase < 0.5f ? 1.0f : -1.0f;
        case 3: return phase < 0.5f ? 1.0f : 0.0f;          // SQUARE+  (unipolar positive)
        case 4: return phase < 0.5f ? 0.0f : -1.0f;         // SQUARE-  (unipolar negative)
        case 5: return 2.0f * phase - 1.0f;                 // SAW BI
        case 6: return phase;                               // SAW UP (unipolar)
        case 7: return 1.0f - phase;                        // SAW DOWN (unipolar)
        default: return randomValue;                        // RANDOM (sample & hold)
    }
}

struct Lfo
{
    float phase = 0, rnd = 0, delayLevel = 0;
    uint32_t seed = 0x1234567;

    float nextRandom() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }

    /** advance by dt seconds at the given Hz, returns waveform value */
    float advance (int wave, float hz, float dt)
    {
        phase += hz * dt;
        if (phase >= 1.0f) { phase -= std::floor (phase); rnd = nextRandom(); }
        return lfoWave (wave, phase, rnd);
    }
};

//==============================================================================
/** ADSR running at control rate; linear attack, exponential decay/release. */
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0, sustain = 1;
    float attackInc = 1, decayCoef = 0, releaseCoef = 0;

    void set (float attackMs, float decayMs, float sustainLevel, float releaseMs, float controlRateHz)
    {
        attackInc = attackMs <= 0.5f ? 1.0f : 1000.0f / (attackMs * controlRateHz);
        decayCoef = decayMs <= 0.5f ? 0.0f : std::exp (-4.6f * 1000.0f / (decayMs * controlRateHz));
        releaseCoef = releaseMs <= 0.5f ? 0.0f : std::exp (-4.6f * 1000.0f / (releaseMs * controlRateHz));
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
    }
    void noteOn() { stage = Attack; if (attackInc >= 1.0f) { level = 1.0f; stage = Decay; } }
    void noteOff() { if (stage != Idle) stage = Release; }
    void kill() { stage = Idle; level = 0; }
    bool isActive() const { return stage != Idle; }

    float tick()
    {
        switch (stage)
        {
            case Attack:  level += attackInc; if (level >= 1.0f) { level = 1.0f; stage = Decay; } break;
            case Decay:   level = sustain + (level - sustain) * decayCoef; if (std::abs (level - sustain) < 1.0e-4f) { level = sustain; stage = Sustain; } break;
            case Sustain: level = sustain; break;
            case Release: level *= releaseCoef; if (level < 1.0e-4f) { level = 0; stage = Idle; } break;
            case Idle: default: level = 0; break;
        }
        return level;
    }
};

/** S5000 style 4-rate / 4-level auxiliary envelope (stage 3 = sustain). */
struct AuxEnv
{
    int stage = -1;    // -1 idle, 0..2 running, 3 sustain, 4 release
    float level = 0, target = 0, inc = 0;
    float rates[4] {}, levels[4] {};
    float crHz = 1378.0f;

    void set (const int* r, const int* l, float controlRateHz)
    {
        crHz = controlRateHz;
        for (int i = 0; i < 4; ++i) { rates[i] = envTimeMs ((float) r[i]); levels[i] = (float) l[i] * 0.01f; }
    }
    void startStage (int s)
    {
        stage = s;
        target = levels[juce::jlimit (0, 3, s == 4 ? 3 : s)];
        const float steps = juce::jmax (1.0f, rates[juce::jlimit (0, 3, s == 4 ? 3 : s)] * crHz / 1000.0f);
        inc = (target - level) / steps;
    }
    void noteOn()  { level = 0; startStage (0); }
    void noteOff() { if (stage >= 0) startStage (4); }
    float tick()
    {
        if (stage < 0 || stage == 3) return level;
        level += inc;
        if ((inc >= 0 && level >= target) || (inc < 0 && level <= target))
        {
            level = target;
            if (stage < 2) startStage (stage + 1);
            else if (stage == 2) stage = 3;
            else stage = -2;  // finished release (holds level)
        }
        return level;
    }
};

} // namespace s6k::dsp
