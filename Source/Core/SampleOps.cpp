/*
  ==============================================================================
   S6000 Emulation - offline sample editing
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "SampleOps.h"
#include "Dsp.h"

namespace s6k::ops
{
using juce::int64;

static void scaleMarkers (Sample& s, double factor)
{
    s.start = (int64) std::round ((double) s.start * factor);
    s.end = (int64) std::round ((double) s.end * factor);
    s.loopStart = (int64) std::round ((double) s.loopStart * factor);
    s.loopEnd = (int64) std::round ((double) s.loopEnd * factor);
    s.loopXfade = (int) std::round ((double) s.loopXfade * factor);
    s.sanitiseMarkers();
}

float peakLevel (const Sample& s)
{
    float p = 0;
    for (int c = 0; c < s.numChannels(); ++c) p = juce::jmax (p, s.buffer.getMagnitude (c, 0, s.numFrames()));
    return p;
}

void normalise (Sample& s, float targetDb)
{
    const float p = peakLevel (s);
    if (p > 1.0e-6f) s.buffer.applyGain (juce::Decibels::decibelsToGain (targetDb) / p);
    s.touch();
}

void reverse (Sample& s, int64 from, int64 to)
{
    from = juce::jlimit<int64> (0, s.numFrames(), from);
    to = juce::jlimit<int64> (from, s.numFrames(), to <= from ? s.numFrames() : to);
    if (to - from > 1) s.buffer.reverse ((int) from, (int) (to - from));
    s.touch();
}

void makeMono (Sample& s)
{
    if (! s.isStereo()) return;
    juce::AudioBuffer<float> m (1, s.numFrames());
    m.clear();
    m.addFrom (0, 0, s.buffer, 0, 0, s.numFrames(), 0.5f);
    m.addFrom (0, 0, s.buffer, 1, 0, s.numFrames(), 0.5f);
    s.buffer = std::move (m);
    s.touch();
}

void rescale (Sample& s, float gainDb)
{
    s.buffer.applyGain (juce::Decibels::decibelsToGain (gainDb));
    s.touch();
}

void discardOutside (Sample& s, int64 a, int64 b)
{
    a = juce::jlimit<int64> (0, s.numFrames(), a);
    b = juce::jlimit<int64> (a + 1, s.numFrames(), b);
    const int len = (int) (b - a);
    juce::AudioBuffer<float> nb (s.numChannels(), len);
    for (int c = 0; c < s.numChannels(); ++c) nb.copyFrom (c, 0, s.buffer, c, (int) a, len);
    s.buffer = std::move (nb);
    s.start -= a; s.end -= a; s.loopStart -= a; s.loopEnd -= a;
    s.start = juce::jmax<int64> (0, s.start); s.end = juce::jmin<int64> (len, s.end <= 0 ? len : s.end);
    s.sanitiseMarkers();
    s.touch();
}

void fade (Sample& s, int64 from, int64 to, bool fadeUp, int curve)
{
    from = juce::jlimit<int64> (0, s.numFrames(), from);
    to = juce::jlimit<int64> (from, s.numFrames(), to);
    const double len = (double) juce::jmax<int64> (1, to - from);
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* d = s.buffer.getWritePointer (c);
        for (int64 i = from; i < to; ++i)
        {
            double x = (double) (i - from) / len;
            if (! fadeUp) x = 1.0 - x;
            const double g = curve == 1 ? x * x : curve == 2 ? std::sqrt (x) : x;
            d[i] *= (float) g;
        }
    }
    s.touch();
}

void crossfadeLoop (Sample& s, int xf, int profile)
{
    const int64 ls = s.loopStart, le = s.loopEnd;
    xf = (int) juce::jlimit<int64> (0, juce::jmin<int64> (ls, le - ls), (int64) xf);
    if (xf < 2) return;
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* d = s.buffer.getWritePointer (c);
        for (int i = 0; i < xf; ++i)
        {
            const float x = (float) i / (float) xf;   // 0 -> 1 across the fade before loop end
            float gIn = x, gOut = 1.0f - x;
            if (profile == 1) { gIn = std::sin (x * juce::MathConstants<float>::halfPi); gOut = std::cos (x * juce::MathConstants<float>::halfPi); }
            const int64 endIdx = le - xf + i, preIdx = ls - xf + i;
            d[endIdx] = d[endIdx] * gOut + d[preIdx] * gIn;
        }
    }
    s.loopXfade = xf;
    s.touch();
}

int64 findZeroCrossing (const Sample& s, int64 pos, int direction)
{
    const float* d = s.buffer.getReadPointer (0);
    const int64 n = s.numFrames();
    for (int64 i = juce::jlimit<int64> (1, n - 1, pos); i > 0 && i < n; i += (direction >= 0 ? 1 : -1))
        if ((d[i - 1] <= 0.0f && d[i] > 0.0f)) return i;
    return pos;
}

int64 autoFindLoopEnd (const Sample& s, int64 ls, int64 approxEnd, int search)
{
    const float* d = s.buffer.getReadPointer (0);
    const int64 n = s.numFrames();
    const int win = 256;
    int64 best = approxEnd;
    double bestErr = 1.0e30;
    for (int64 e = juce::jmax<int64> (ls + win + 2, approxEnd - search); e < juce::jmin<int64> (n - 1, approxEnd + search); ++e)
    {
        // compare the samples leading into loop end with those leading into loop start
        double err = 0;
        for (int i = 1; i <= win && ls - i >= 0; i += 4)
        {
            const double diff = d[e - i] - d[ls - i];
            err += diff * diff;
        }
        err += 8.0 * std::pow ((double) d[e] - d[ls], 2.0);
        if (err < bestErr) { bestErr = err; best = e; }
    }
    return best;
}

Sample::Ptr join (const Sample& a, const Sample& b, const juce::String& name)
{
    Sample::Ptr bb = b.clone (b.name);
    if (std::abs (bb->sampleRate - a.sampleRate) > 0.5) resample (*bb, a.sampleRate);
    const int ch = juce::jmax (a.numChannels(), bb->numChannels());
    Sample::Ptr out = a.clone (name);
    out->buffer.setSize (ch, a.numFrames() + bb->numFrames());
    for (int c = 0; c < ch; ++c)
    {
        out->buffer.copyFrom (c, 0, a.buffer, juce::jmin (c, a.numChannels() - 1), 0, a.numFrames());
        out->buffer.copyFrom (c, a.numFrames(), bb->buffer, juce::jmin (c, bb->numChannels() - 1), 0, bb->numFrames());
    }
    out->start = 0;
    out->end = out->numFrames();
    out->loopEnabled = false;
    out->loopStart = 0; out->loopEnd = out->numFrames();
    out->touch();
    return out;
}

void mix (Sample& a, const Sample& b, float levelA, float levelB, int64 offset)
{
    Sample::Ptr bb = b.clone (b.name);
    if (std::abs (bb->sampleRate - a.sampleRate) > 0.5) resample (*bb, a.sampleRate);
    const int ch = juce::jmax (a.numChannels(), bb->numChannels());
    const int len = (int) juce::jmax<int64> (a.numFrames(), offset + bb->numFrames());
    juce::AudioBuffer<float> out (ch, len);
    out.clear();
    for (int c = 0; c < ch; ++c)
    {
        out.addFrom (c, 0, a.buffer, juce::jmin (c, a.numChannels() - 1), 0, a.numFrames(), levelA);
        out.addFrom (c, (int) offset, bb->buffer, juce::jmin (c, bb->numChannels() - 1), 0, bb->numFrames(), levelB);
    }
    a.buffer = std::move (out);
    a.end = a.numFrames();
    a.sanitiseMarkers();
    a.touch();
}

void timeStretch (Sample& s, double ratio)
{
    ratio = juce::jlimit (0.25, 4.0, ratio);
    const int n = s.numFrames();
    const int N = juce::jlimit (512, 4096, juce::nextPowerOfTwo ((int) (s.sampleRate * 0.046)));
    const int Hs = N / 2;
    const double Ha = (double) Hs / ratio;
    const int tol = N / 4;
    const int outLen = (int) std::ceil (n * ratio) + N;

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (dsp::twoPi * (float) i / (float) N);

    // mono analysis signal
    std::vector<float> mono ((size_t) n + (size_t) N * 2 + (size_t) tol * 2, 0.0f);
    for (int c = 0; c < s.numChannels(); ++c)
    {
        const float* d = s.buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i];
    }
    auto at = [&] (int64 i) { return (i >= 0 && i < n) ? mono[(size_t) i] : 0.0f; };

    juce::AudioBuffer<float> out (s.numChannels(), outLen);
    out.clear();
    std::vector<float> norm ((size_t) outLen, 0.0f);

    int64 prev = 0;
    for (int k = 0; ; ++k)
    {
        const int64 outPos = (int64) k * Hs;
        if (outPos + N > outLen) break;
        const int64 nominal = (int64) std::round (k * Ha);
        if (nominal > n) break;
        int64 best = nominal;
        if (k > 0)
        {
            const int64 target = prev + Hs;
            double bestC = -1.0e30;
            for (int dlt = -tol; dlt <= tol; dlt += 2)
            {
                const int64 cand = nominal + dlt;
                if (cand < 0) continue;
                double c = 0;
                for (int i = 0; i < N; i += 8) c += (double) at (target + i) * at (cand + i);
                if (c > bestC) { bestC = c; best = cand; }
            }
        }
        for (int c = 0; c < s.numChannels(); ++c)
        {
            const float* d = s.buffer.getReadPointer (c);
            float* o = out.getWritePointer (c);
            for (int i = 0; i < N; ++i)
            {
                const int64 src = best + i;
                if (src >= 0 && src < n) o[outPos + i] += d[src] * win[(size_t) i];
            }
        }
        for (int i = 0; i < N; ++i) norm[(size_t) (outPos + i)] += win[(size_t) i];
        prev = best;
    }
    const int finalLen = juce::jmax (1, (int) std::round (n * ratio));
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* o = out.getWritePointer (c);
        for (int i = 0; i < finalLen; ++i) if (norm[(size_t) i] > 0.1f) o[i] /= norm[(size_t) i];
    }
    out.setSize (s.numChannels(), finalLen, true);
    s.buffer = std::move (out);
    scaleMarkers (s, ratio);
    s.touch();
}

void resample (Sample& s, double newRate)
{
    if (newRate <= 1000.0 || std::abs (newRate - s.sampleRate) < 0.5) return;
    const double speed = s.sampleRate / newRate;   // input samples per output sample
    const int outLen = juce::jmax (1, (int) std::floor (s.numFrames() / speed));
    juce::AudioBuffer<float> out (s.numChannels(), outLen);
    for (int c = 0; c < s.numChannels(); ++c)
    {
        juce::LagrangeInterpolator li;
        // pad the input so the interpolator never reads past the end
        juce::AudioBuffer<float> in (1, s.numFrames() + 8);
        in.clear();
        in.copyFrom (0, 0, s.buffer, c, 0, s.numFrames());
        li.process (speed, in.getReadPointer (0), out.getWritePointer (c), outLen);
    }
    s.buffer = std::move (out);
    s.sampleRate = newRate;
    scaleMarkers (s, 1.0 / speed);
    s.touch();
}

void pitchShift (Sample& s, float semitones)
{
    const double ratio = std::exp2 (semitones / 12.0);
    const double origRate = s.sampleRate;
    timeStretch (s, ratio);                       // longer by ratio ...
    s.sampleRate = origRate * ratio;              // ... then play it faster
    resample (s, origRate);
}

void eq (Sample& s, float lowDb, float midFreq, float midDb, float highDb)
{
    for (int c = 0; c < s.numChannels(); ++c)
    {
        dsp::Biquad lo, mi, hi;
        lo.setLowShelf ((float) s.sampleRate, 200.0f, lowDb);
        mi.setPeak ((float) s.sampleRate, midFreq, 0.9f, midDb);
        hi.setHighShelf ((float) s.sampleRate, 5000.0f, highDb);
        float* d = s.buffer.getWritePointer (c);
        for (int i = 0; i < s.numFrames(); ++i) d[i] = hi.tick (mi.tick (lo.tick (d[i])));
    }
    s.touch();
}

std::vector<int64> equalSlices (const Sample& s, int num)
{
    std::vector<int64> pts;
    num = juce::jlimit (1, 128, num);
    const int64 a = s.start, b = s.end > s.start ? s.end : s.numFrames();
    for (int i = 0; i <= num; ++i) pts.push_back (a + (b - a) * i / num);
    return pts;
}

std::vector<int64> detectTransients (const Sample& s, int sensitivity, int maxSlices)
{
    std::vector<int64> pts { s.start };
    const int hop = 256;
    const float* d = s.buffer.getReadPointer (0);
    const int64 end = s.end > s.start ? s.end : s.numFrames();
    float prev = 0, avg = 0;
    const float thresh = 1.5f + (100.0f - (float) sensitivity) * 0.08f;
    int64 last = s.start;
    for (int64 i = s.start; i + hop < end; i += hop)
    {
        float e = 0;
        for (int k = 0; k < hop; ++k) e += d[i + k] * d[i + k];
        e = std::sqrt (e / hop);
        avg = 0.9f * avg + 0.1f * e;
        if (e > prev * thresh && e > avg * 1.2f && e > 0.01f && i - last > (int64) (s.sampleRate * 0.06))
        {
            pts.push_back (findZeroCrossing (s, i, -1));
            last = i;
            if ((int) pts.size() > maxSlices) break;
        }
        prev = e;
    }
    pts.push_back (end);
    return pts;
}

std::vector<Sample::Ptr> chop (const Sample& s, const std::vector<int64>& pts, const juce::String& prefix)
{
    std::vector<Sample::Ptr> out;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const int64 a = pts[i], b = pts[i + 1];
        if (b - a < 16) continue;
        Sample::Ptr p = new Sample();
        p->name = (prefix.substring (0, 16) + "-" + juce::String ((int) out.size() + 1).paddedLeft ('0', 2));
        p->buffer.setSize (s.numChannels(), (int) (b - a));
        for (int c = 0; c < s.numChannels(); ++c) p->buffer.copyFrom (c, 0, s.buffer, c, (int) a, (int) (b - a));
        p->sampleRate = s.sampleRate;
        p->rootNote = s.rootNote;
        p->start = 0; p->end = p->numFrames();
        p->loopStart = 0; p->loopEnd = p->numFrames();
        p->loopEnabled = false;
        p->playMode = fmt::PlayOneShot;
        out.push_back (p);
    }
    return out;
}

double estimateBpm (const Sample& s, int beats)
{
    const double secs = (double) juce::jmax<int64> (1, (s.end > s.start ? s.end : s.numFrames()) - s.start) / s.sampleRate;
    return 60.0 * juce::jmax (1, beats) / secs;
}

} // namespace s6k::ops
