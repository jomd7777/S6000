/*
  ==============================================================================
   S6000 Emulation - per-voice resonant filter, 26 types
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "VoiceFilter.h"

namespace s6k
{
//==============================================================================
void VoiceFilter::reset()
{
    a.reset(); b.reset(); hp.reset(); fb = 0;
    for (auto& x : ap) x.reset();
}

void VoiceFilter::setup (int t, float c, float r01, float sr)
{
    type = t;
    bypass = false;
    gain = 1.0f;
    const float hz = dsp::cutoffToHz (c);
    const float q = 0.6f + r01 * 14.0f;

    switch (t)
    {
        case 0: case 1: case 2:
            if (t != 2 && c >= 99.5f && r01 < 0.01f) { bypass = true; return; }
            if (t == 1) { a.set (hz, 0.55f + r01 * 8.0f, sr); b.set (hz, 0.6f, sr); }   // 24 dB: resonance in one stage only
            else { a.set (hz, q, sr); b.set (hz, q, sr); }
            if (t == 2) gain = 1.0f + r01 * 3.0f;
            break;
        case 3: case 4: case 5:
            a.set (hz, q, sr); b.set (hz, q, sr);
            if (t == 5) gain = 1.0f + r01 * 3.0f;
            break;
        case 6: case 8:
            if (c <= 0.5f && r01 < 0.01f && t == 6) { bypass = true; return; }
            hp.set (hz, sr); a.set (hz, q, sr);
            if (t == 8) gain = 1.0f + r01 * 2.0f;
            break;
        case 7:
            a.set (hz, q, sr);
            break;
        case 9: case 10: case 11:
            morph = juce::jlimit (0.0f, 1.0f, c * 0.01f);
            a.set (dsp::cutoffToHz (35.0f + c * 0.4f), q, sr);
            break;
        case 12: case 13: case 14: case 15:
        {
            static const float qs[] = { 1.0f, 2.5f, 6.0f, 0.3f };
            a.set (hz, qs[t - 12] * (1.0f + r01 * 2.0f), sr);
            break;
        }
        case 16:
            a.set (hz, 1.5f + r01 * 3.0f, sr);
            b.set (hz * (1.5f + r01), 1.5f + r01 * 3.0f, sr);
            break;
        case 17: case 18: case 19: case 20:
        {
            static const float qs[] = { 1.5f, 3.0f, 6.0f, 0.5f };
            a.set (hz, qs[t - 17], sr);
            gain = 2.0f + r01 * 6.0f;
            break;
        }
        case 21:
            a.set (hz, 2.0f, sr);
            b.set (hz * (1.6f + r01), 2.0f, sr);
            gain = 2.0f + r01 * 6.0f;
            break;
        case 22: case 23: case 24:
        {
            stages = t == 23 ? 6 : 4;
            fbAmt = r01 * 0.85f;
            for (int i = 0; i < stages; ++i) ap[i].set (hz * (1.0f + 0.35f * (float) i), sr);
            if (t == 24) for (int i = 0; i < 4; ++i) ap[6 + i].set (hz * 2.3f * (1.0f + 0.35f * (float) i), sr);
            break;
        }
        case 25:
        default:
        {
            // VOWELISER : cutoff morphs  A - E - I - O - U
            static const float f1[] = { 730, 530, 270, 570, 300 }, f2[] = { 1090, 1840, 2290, 840, 870 };
            const float pos = juce::jlimit (0.0f, 3.999f, c * 0.04f);
            const int i = (int) pos; const float f = pos - (float) i;
            a.set (f1[i] + f * (f1[i + 1] - f1[i]), 4.0f + r01 * 10.0f, sr);
            b.set (f2[i] + f * (f2[i + 1] - f2[i]), 5.0f + r01 * 10.0f, sr);
            break;
        }
    }
}

float VoiceFilter::process (float x)
{
    float lp, bp, hpo, lp2, bp2, hp2;
    switch (type)
    {
        case 0:  a.tick (x, lp, bp, hpo); return lp;
        case 1:  a.tick (x, lp, bp, hpo); b.tick (lp, lp2, bp2, hp2); return lp2;
        case 2:  a.tick (x, lp, bp, hpo); return dsp::fastTanh (lp * gain) ;
        case 3:  a.tick (x, lp, bp, hpo); return bp * a.k;
        case 4:  a.tick (x, lp, bp, hpo); b.tick (bp * a.k, lp2, bp2, hp2); return bp2 * b.k;
        case 5:  a.tick (x, lp, bp, hpo); return dsp::fastTanh (bp * gain);
        case 6:  return hp.hp (x);
        case 7:  a.tick (x, lp, bp, hpo); return hpo;
        case 8:  { const float h = hp.hp (x); a.tick (h, lp, bp, hpo); return dsp::fastTanh ((h + bp * a.k * (gain - 1.0f)) * 1.0f); }
        case 9:  a.tick (x, lp, bp, hpo); return lp + morph * (hpo - lp);
        case 10: a.tick (x, lp, bp, hpo); return lp + morph * (bp * a.k - lp);
        case 11: a.tick (x, lp, bp, hpo); return bp * a.k + morph * (hpo - bp * a.k);
        case 12: case 13: case 14: case 15:
                 a.tick (x, lp, bp, hpo); return x - bp * a.k;
        case 16: { a.tick (x, lp, bp, hpo); const float y = x - bp * a.k; b.tick (y, lp2, bp2, hp2); return y - bp2 * b.k; }
        case 17: case 18: case 19: case 20:
                 a.tick (x, lp, bp, hpo); return x + bp * a.k * (gain - 1.0f);
        case 21: { a.tick (x, lp, bp, hpo); b.tick (x, lp2, bp2, hp2); return x + (bp * a.k + bp2 * b.k) * (gain - 1.0f) * 0.5f; }
        case 22: case 23:
        {
            float y = x + fb * fbAmt;
            for (int i = 0; i < stages; ++i) y = ap[i].tick (y);
            fb = y;
            return 0.5f * (x + y);
        }
        case 24:
        {
            float y1 = x + fb * fbAmt, y2 = x;
            for (int i = 0; i < 4; ++i) { y1 = ap[i].tick (y1); y2 = ap[6 + i].tick (y2); }
            fb = y1;
            return 0.5f * x + 0.25f * (y1 + y2);
        }
        default:
        {
            a.tick (x, lp, bp, hpo); b.tick (x, lp2, bp2, hp2);
            return (bp * a.k + 0.7f * bp2 * b.k) * 1.6f;
        }
    }
}

} // namespace s6k
