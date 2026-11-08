/*
  ==============================================================================
   S6000 Emulation - offline sample editing
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "SampleOps.h"
#include "Dsp.h"
#include <complex>

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

std::atomic<float> progress { 0.0f };
std::atomic<bool> abortFlag { false };

/** WSOLA core. N = window, tol = search range, step = search / correlation stride */
static void stretchCore (Sample& s, double ratio, int N, int tol, int step)
{
    ratio = juce::jlimit (0.25, 4.0, ratio);
    const int n = s.numFrames();
    N = juce::jlimit (128, 16384, N & ~1);
    const int Hs = N / 2;
    const double Ha = (double) Hs / ratio;
    tol = juce::jlimit (8, N, tol);
    step = juce::jlimit (1, 16, step);
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
        if (abortFlag.load()) return;
        progress = (float) juce::jlimit (0.0, 1.0, (double) nominal / (double) juce::jmax (1, n));
        int64 best = nominal;
        if (k > 0)
        {
            const int64 target = prev + Hs;
            double bestC = -1.0e30;
            for (int dlt = -tol; dlt <= tol; dlt += step)
            {
                const int64 cand = nominal + dlt;
                if (cand < 0) continue;
                double c = 0;
                for (int i = 0; i < N; i += step * 2) c += (double) at (target + i) * at (cand + i);
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

void timeStretch (Sample& s, double ratio)
{
    const int N = juce::jlimit (512, 4096, juce::nextPowerOfTwo ((int) (s.sampleRate * 0.046)));
    stretchCore (s, ratio, N, N / 4, 2);
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

//==============================================================================
// EDIT SAMPLE redesign helpers
//==============================================================================
int64 findLevel (const Sample& s, float thresholdDb, bool fromStart, int64 lo, int64 hi)
{
    const int64 n = s.numFrames();
    lo = juce::jlimit<int64> (0, n, lo);
    hi = juce::jlimit<int64> (lo, n, hi <= lo ? n : hi);
    const float th = juce::Decibels::decibelsToGain (thresholdDb);
    auto above = [&] (int64 i)
    {
        for (int c = 0; c < s.numChannels(); ++c) if (std::abs (s.buffer.getSample (c, (int) i)) >= th) return true;
        return false;
    };
    if (fromStart) { for (int64 i = lo; i < hi; ++i) if (above (i)) return i; return lo; }
    for (int64 i = hi - 1; i >= lo; --i) if (above (i)) return juce::jmin (hi, i + 1);
    return hi;
}

void erase (Sample& s, int64 a, int64 b)
{
    a = juce::jlimit<int64> (0, s.numFrames(), a);
    b = juce::jlimit<int64> (a, s.numFrames(), b);
    for (int c = 0; c < s.numChannels(); ++c) s.buffer.clear (c, (int) a, (int) (b - a));
    s.touch();
}

void cut (Sample& s, int64 a, int64 b)
{
    const int64 n = s.numFrames();
    a = juce::jlimit<int64> (0, n, a);
    b = juce::jlimit<int64> (a, n, b);
    const int64 len = b - a;
    if (len <= 0 || n - len < 2) return;
    juce::AudioBuffer<float> nb (s.numChannels(), (int) (n - len));
    for (int c = 0; c < s.numChannels(); ++c)
    {
        if (a > 0) nb.copyFrom (c, 0, s.buffer, c, 0, (int) a);
        if (n - b > 0) nb.copyFrom (c, (int) a, s.buffer, c, (int) b, (int) (n - b));
    }
    s.buffer = std::move (nb);
    auto mv = [&] (int64 p) { return p <= a ? p : (p >= b ? p - len : a); };
    s.start = mv (s.start); s.end = mv (s.end); s.loopStart = mv (s.loopStart); s.loopEnd = mv (s.loopEnd);
    s.sanitiseMarkers();
    s.touch();
}

Sample::Ptr extract (const Sample& s, int64 a, int64 b, const juce::String& name)
{
    auto out = s.clone (name);
    discardOutside (*out, a, b);
    out->start = 0; out->end = out->numFrames();
    out->loopStart = 0; out->loopEnd = out->numFrames(); out->loopEnabled = false;
    out->sanitiseMarkers();
    return out;
}

float curveGain (double x, int curve)
{
    x = juce::jlimit (0.0, 1.0, x);
    switch (curve)
    {
        case 1:  return x <= 0.0 ? 0.0f : (float) juce::Decibels::decibelsToGain (-60.0 * (1.0 - x), -60.0);   // LOG (dB linear)
        case 2:  return (float) std::sin (x * juce::MathConstants<double>::halfPi);                              // SINE
        default: return (float) x;                                                                                // LINEAR
    }
}

void fadeUpDown (Sample& s, int64 upEnd, int upCurve, int64 downStart, int downCurve)
{
    const int64 n = s.numFrames();
    upEnd = juce::jlimit<int64> (0, n, upEnd);
    downStart = juce::jlimit<int64> (0, n, downStart);
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* d = s.buffer.getWritePointer (c);
        for (int64 i = 0; i < upEnd; ++i) d[i] *= curveGain ((double) i / (double) upEnd, upCurve);
        const double dl = (double) juce::jmax<int64> (1, n - downStart);
        for (int64 i = downStart; i < n; ++i) d[i] *= curveGain (1.0 - (double) (i - downStart) / dl, downCurve);
    }
    s.touch();
}

static juce::AudioBuffer<float> rangeBuffer (const Range& r, int channels)
{
    const int64 n = r.s->numFrames();
    const int64 a = juce::jlimit<int64> (0, n, r.from), b = juce::jlimit<int64> (a, n, r.to <= a ? n : r.to);
    juce::AudioBuffer<float> out (channels, (int) (b - a));
    const float g = juce::Decibels::decibelsToGain (r.gainDb);
    for (int c = 0; c < channels; ++c)
        if (b > a) out.copyFrom (c, 0, r.s->buffer.getReadPointer (juce::jmin (c, r.s->numChannels() - 1), (int) a), (int) (b - a), g);
    return out;
}

static Sample::Ptr newFrom (const Sample& proto, const juce::String& name, juce::AudioBuffer<float>&& buf)
{
    Sample::Ptr out = new Sample();
    out->name = name;
    out->buffer = std::move (buf);
    out->sampleRate = proto.sampleRate;
    out->rootNote = proto.rootNote; out->semitone = proto.semitone; out->cents = proto.cents;
    out->start = 0; out->end = out->numFrames();
    out->loopStart = 0; out->loopEnd = out->numFrames();
    out->loopEnabled = false;
    out->playMode = fmt::PlayNoLoop;
    out->sanitiseMarkers();
    out->touch();
    return out;
}

Sample::Ptr joinRanges (const Range& first, const Range& second, int64 xfade, int curve, const juce::String& name)
{
    const int ch = juce::jmax (first.s->numChannels(), second.s->numChannels());
    auto a = rangeBuffer (first, ch), b = rangeBuffer (second, ch);
    xfade = juce::jlimit<int64> (0, juce::jmin (a.getNumSamples(), b.getNumSamples()), xfade);
    const int len = (int) (a.getNumSamples() + b.getNumSamples() - xfade);
    juce::AudioBuffer<float> out (ch, juce::jmax (1, len));
    out.clear();
    const int bStart = (int) (a.getNumSamples() - xfade);
    for (int c = 0; c < ch; ++c)
    {
        float* o = out.getWritePointer (c);
        const float* pa = a.getReadPointer (c);
        const float* pb = b.getReadPointer (c);
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            float g = 1.0f;
            if (i >= bStart && xfade > 0) g = curveGain (1.0 - (double) (i - bStart) / (double) xfade, curve);
            o[i] += pa[i] * g;
        }
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float g = 1.0f;
            if (i < xfade) g = curveGain ((double) i / (double) xfade, curve);
            o[bStart + i] += pb[i] * g;
        }
    }
    return newFrom (*first.s, name, std::move (out));
}

Sample::Ptr mixRanges (const Range& ra, const Range& rb, const juce::String& name)
{
    const int ch = juce::jmax (ra.s->numChannels(), rb.s->numChannels());
    auto a = rangeBuffer (ra, ch), b = rangeBuffer (rb, ch);
    juce::AudioBuffer<float> out (ch, juce::jmax (1, juce::jmax (a.getNumSamples(), b.getNumSamples())));
    out.clear();
    for (int c = 0; c < ch; ++c)
    {
        out.addFrom (c, 0, a, c, 0, a.getNumSamples());
        out.addFrom (c, 0, b, c, 0, b.getNumSamples());
    }
    return newFrom (*ra.s, name, std::move (out));
}

Sample::Ptr makeStereo (const Range& left, const Range& right, const juce::String& name)
{
    auto l = rangeBuffer (left, 1), r = rangeBuffer (right, 1);
    juce::AudioBuffer<float> out (2, juce::jmax (1, juce::jmax (l.getNumSamples(), r.getNumSamples())));
    out.clear();
    out.copyFrom (0, 0, l, 0, 0, l.getNumSamples());
    out.copyFrom (1, 0, r, 0, 0, r.getNumSamples());
    return newFrom (*left.s, name, std::move (out));
}

namespace
{
    struct StretchType { const char* name; float windowMs; };
    const StretchType stretchTypes[18] = {
        { "FEM VOX", 22 }, { "MALE VOX", 32 }, { "LOW MALE VOX", 42 }, { "VOCAL", 28 },
        { "HFREQ RHYTHM", 12 }, { "MFREQ RHYTHM", 18 }, { "LFREQ RHYTHM", 28 }, { "PERCUSSION", 10 },
        { "LFREQ PERC.", 20 }, { "STACCATO", 14 }, { "LFREQ SLOW", 70 }, { "MUSIC 1", 40 },
        { "MUSIC 2", 55 }, { "MUSIC 3", 80 }, { "SOFT PERC.", 24 }, { "HFREQ ORCH.", 50 },
        { "LFREQ ORCH.", 90 }, { "SLOW ORCH.", 120 } };
}

juce::String stretchPresetName (int preset)
{
    preset = juce::jlimit (0, numStretchPresets - 1, preset);
    return juce::String (stretchTypes[preset / 3].name) + " " + juce::String::charToString ((juce::juce_wchar) ('A' + preset % 3));
}

void timeStretchPreset (Sample& s, double ratio, int preset)
{
    preset = juce::jlimit (0, numStretchPresets - 1, preset);
    const int v = preset % 3;                                   // A fast, B better, C best
    const int N = (int) (s.sampleRate * stretchTypes[preset / 3].windowMs * 0.001) * 2;   // 2 x window
    const int tol = v == 0 ? N / 4 : v == 1 ? N / 3 : N / 2;
    const int step = v == 0 ? 6 : v == 1 ? 3 : 1;
    stretchCore (s, ratio, N, tol, step);
}

void pitchShiftPreset (Sample& s, float semitones, int preset)
{
    const double ratio = std::exp2 (semitones / 12.0);
    const double origRate = s.sampleRate;
    timeStretchPreset (s, ratio, preset);
    if (abortFlag.load()) return;
    s.sampleRate = origRate * ratio;
    resampleQ (s, origRate, preset % 3 == 0 ? 1 : 2);
}

void resampleQ (Sample& s, double newRate, int quality)
{
    if (newRate <= 1000.0 || std::abs (newRate - s.sampleRate) < 0.5) return;
    if (quality == 1) { resample (s, newRate); return; }
    const double speed = s.sampleRate / newRate;
    const int n = s.numFrames();
    const int outLen = juce::jmax (1, (int) std::floor (n / speed));
    juce::AudioBuffer<float> out (s.numChannels(), outLen);
    if (quality <= 0)
    {
        for (int c = 0; c < s.numChannels(); ++c)
        {
            const float* d = s.buffer.getReadPointer (c);
            float* o = out.getWritePointer (c);
            for (int i = 0; i < outLen; ++i)
            {
                const double p = i * speed;
                const int ip = (int) p;
                const float fr = (float) (p - ip);
                o[i] = d[juce::jmin (n - 1, ip)] + fr * (d[juce::jmin (n - 1, ip + 1)] - d[juce::jmin (n - 1, ip)]);
            }
        }
    }
    else
    {
        // windowed sinc (Blackman, 32 taps), cut-off follows the lower of the two rates
        const int taps = 16;
        const double cutoff = juce::jmin (1.0, 1.0 / speed) * 0.95;
        for (int c = 0; c < s.numChannels(); ++c)
        {
            const float* d = s.buffer.getReadPointer (c);
            float* o = out.getWritePointer (c);
            for (int i = 0; i < outLen; ++i)
            {
                if ((i & 4095) == 0)
                {
                    if (abortFlag.load()) return;
                    progress = (float) ((double) (c * outLen + i) / (double) juce::jmax (1, s.numChannels() * outLen));
                }
                const double p = i * speed;
                const int ip = (int) p;
                double acc = 0, wsum = 0;
                for (int k = -taps + 1; k <= taps; ++k)
                {
                    const int idx = ip + k;
                    const double x = (double) idx - p;
                    const double t = x * cutoff;
                    const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                    const double wpos = (x + taps) / (2.0 * taps);
                    const double win = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * wpos) + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * wpos);
                    const double w = sinc * win;
                    wsum += w;
                    if (idx >= 0 && idx < n) acc += d[idx] * w;
                }
                o[i] = (float) (wsum != 0.0 ? acc / wsum : acc);
            }
        }
    }
    s.buffer = std::move (out);
    s.sampleRate = newRate;
    scaleMarkers (s, 1.0 / speed);
    s.touch();
}

void requantise (Sample& s, int bits)
{
    if (bits >= 16 || bits < 1) return;
    const float steps = (float) (1 << (bits - 1));
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* d = s.buffer.getWritePointer (c);
        for (int i = 0; i < s.numFrames(); ++i) d[i] = std::round (juce::jlimit (-1.0f, 1.0f, d[i]) * steps) / steps;
    }
    s.touch();
}

namespace
{
    struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    Coefs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
    Coefs shelf (double sr, double f, double gainDb, bool high, int slope)
    {
        f = juce::jlimit (10.0, sr * 0.45, f);
        if (slope == 0)
        {
            // first order shelf (6 dB/oct), bilinear transform
            const double G = std::pow (10.0, gainDb / 20.0), K = std::tan (juce::MathConstants<double>::pi * f / sr);
            const double sG = std::sqrt (G);
            if (! high) return norm (G * K / sG + 1.0 * 1.0, G * K / sG - 1.0, 0.0, K / sG + 1.0, K / sG - 1.0, 0.0);
            return norm (sG * K + G, sG * K - G, 0.0, sG * K + 1.0, sG * K - 1.0, 0.0);
        }
        const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2.0 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * al;
        if (! high)
            return norm (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
                         (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
        return norm (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                     (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    Coefs bell (double sr, double f, double q, double gainDb)
    {
        f = juce::jlimit (10.0, sr * 0.45, f);
        const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
        const double al = std::sin (w) / (2.0 * juce::jlimit (0.1, 40.0, q)), c = std::cos (w);
        return norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    double magDb (const Coefs& k, double sr, double f)
    {
        const std::complex<double> z = std::polar (1.0, -juce::MathConstants<double>::twoPi * f / sr);
        const auto num = k.b0 + k.b1 * z + k.b2 * z * z;
        const auto den = 1.0 + k.a1 * z + k.a2 * z * z;
        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (num / den)));
    }
}

void eq3 (Sample& s, const EqSettings& e)
{
    const double sr = s.sampleRate;
    const Coefs k[3] = { shelf (sr, e.lf, e.lfGain, false, e.lfSlope), bell (sr, e.mf, e.mfQ, e.mfGain), shelf (sr, e.hf, e.hfGain, true, e.hfSlope) };
    const float out = juce::Decibels::decibelsToGain (e.outDb);
    for (int c = 0; c < s.numChannels(); ++c)
    {
        float* d = s.buffer.getWritePointer (c);
        double z[3][2] = {};
        for (int i = 0; i < s.numFrames(); ++i)
        {
            double x = d[i];
            for (int b = 0; b < 3; ++b)
            {
                const double y = k[b].b0 * x + z[b][0];
                z[b][0] = k[b].b1 * x - k[b].a1 * y + z[b][1];
                z[b][1] = k[b].b2 * x - k[b].a2 * y;
                x = y;
            }
            d[i] = (float) x * out;
        }
    }
    s.touch();
}

float eqResponseDb (const EqSettings& e, double sr, double f)
{
    return (float) (magDb (shelf (sr, e.lf, e.lfGain, false, e.lfSlope), sr, f)
                  + magDb (bell (sr, e.mf, e.mfQ, e.mfGain), sr, f)
                  + magDb (shelf (sr, e.hf, e.hfGain, true, e.hfSlope), sr, f)) + e.outDb;
}

} // namespace s6k::ops
