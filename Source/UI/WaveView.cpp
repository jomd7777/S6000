/*
  ==============================================================================
   S6000 Emulation - EDIT SAMPLE waveform display
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "WaveView.h"

namespace s6k::ui::wave
{
namespace
{
    const juce::Colour playWhite { 0xfff4fffd };

    juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
}

//==============================================================================
float logHeight (float v)
{
    if (v <= 0.0f) return 0.0f;
    const float db = 20.0f * std::log10 (v);
    return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

float xOf (juce::Rectangle<float> r, int64 from, int64 to, int64 pos)
{
    const double span = (double) juce::jmax<int64> (1, to - from);
    return r.getX() + r.getWidth() * (float) ((double) (pos - from) / span);
}

int64 posAt (juce::Rectangle<float> r, int64 from, int64 to, float x)
{
    const double span = (double) juce::jmax<int64> (1, to - from);
    return from + (int64) std::llround ((double) (x - r.getX()) / (double) juce::jmax (1.0f, r.getWidth()) * span);
}

juce::String zoomTag (bool log, int64 total, int64 visible)
{
    const double z = (double) juce::jmax<int64> (1, total) / (double) juce::jmax<int64> (1, visible);
    juce::String zs = z < 1.05 ? juce::String (1) : (z < 9.95 ? juce::String (z, 1) : juce::String ((int) std::round (z)));
    return juce::String (log ? "LOG" : "LIN") + "  " + u8 ("\xc3\x97") + zs;
}

//==============================================================================
void Peaks::update (const Sample& s, int64 a, int64 b, int c)
{
    const int ver = s.version.load();
    if (sample == &s && version == ver && from == a && to == b && cols == c) return;
    sample = &s; version = ver; from = a; to = b; cols = c;
    mn.assign ((size_t) c, 0.0f); mx.assign ((size_t) c, 0.0f); rms.assign ((size_t) c, 0.0f);

    const int64 n = s.numFrames();
    const int chans = s.numChannels();
    if (n < 2 || c < 1 || b <= a) return;
    const double per = (double) (b - a) / (double) c;
    sampleLevel = per < 1.0;

    for (int x = 0; x < c; ++x)
    {
        if (sampleLevel)
        {
            // interpolated value at this pixel (shows the real wave shape when zoomed right in)
            const double p = (double) a + (x + 0.5) * per;
            const int64 i0 = juce::jlimit<int64> (0, n - 1, (int64) std::floor (p));
            const int64 i1 = juce::jmin<int64> (n - 1, i0 + 1);
            const float fr = (float) (p - (double) i0);
            float v = 0;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float* d = s.buffer.getReadPointer (ch);
                v += d[i0] + fr * (d[i1] - d[i0]);
            }
            v /= (float) chans;
            mn[(size_t) x] = mx[(size_t) x] = v;
            rms[(size_t) x] = std::abs (v);
            continue;
        }
        const int64 i0 = juce::jlimit<int64> (0, n, a + (int64) (x * per));
        const int64 i1 = juce::jlimit<int64> (i0 + 1, n, a + (int64) ((x + 1) * per));
        const int64 stride = juce::jmax<int64> (1, (i1 - i0) / 1024);      // decimate very long columns
        float lo = 1.0e9f, hi = -1.0e9f;
        double sq = 0; int64 cnt = 0;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = s.buffer.getReadPointer (ch);
            for (int64 i = i0; i < i1; i += stride)
            {
                const float v = d[i];
                lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
                sq += (double) v * v; ++cnt;
            }
        }
        if (cnt == 0) { lo = hi = 0; }
        mn[(size_t) x] = lo; mx[(size_t) x] = hi;
        rms[(size_t) x] = (float) std::sqrt (sq / (double) juce::jmax<int64> (1, cnt));
    }
}

//==============================================================================
static void drawFlag (juce::Graphics& g, juce::Rectangle<float> r, float x, const Marker& m, bool compact)
{
    const auto col = m.colour.withAlpha (m.selected ? 1.0f : 0.8f);
    g.setColour (col);
    if (m.selected)
    {
        g.setColour (col.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x - 2.5f, r.getY(), 5.0f, r.getHeight()));
        g.setColour (col);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, r.getY(), 2.0f, r.getHeight()));
    }
    else
        g.fillRect (juce::Rectangle<float> (x - 0.5f, r.getY(), 1.0f, r.getHeight()));

    if (compact || m.label.isEmpty()) return;
    const float w = 7.0f + 7.0f * (float) m.label.length(), h = 12.0f;
    auto flag = juce::Rectangle<float> (m.side > 0 ? x : x - w, r.getY(), w, h);
    flag = flag.withX (juce::jlimit (r.getX(), r.getRight() - w, flag.getX()));
    g.setColour (col);
    g.fillRect (flag);
    g.setColour (colours::oledBg);
    g.setFont (fonts::oled (7.5f, true));
    g.drawText (m.label, flag, juce::Justification::centred, false);
}

static void drawOffscreen (juce::Graphics& g, juce::Rectangle<float> r, const Marker& m, bool left)
{
    // the edit point is outside the zoomed view: show an arrow at the edge
    const auto col = m.colour.withAlpha (m.selected ? 1.0f : 0.7f);
    const float y = r.getY() + 2.0f;
    juce::Path p;
    if (left) { p.startNewSubPath (r.getX() + 2.0f, y + 5.0f); p.lineTo (r.getX() + 8.0f, y); p.lineTo (r.getX() + 8.0f, y + 10.0f); }
    else      { p.startNewSubPath (r.getRight() - 2.0f, y + 5.0f); p.lineTo (r.getRight() - 8.0f, y); p.lineTo (r.getRight() - 8.0f, y + 10.0f); }
    p.closeSubPath();
    g.setColour (col);
    g.fillPath (p);
    if (m.label.isNotEmpty())
    {
        g.setFont (fonts::oled (7.5f, true));
        g.drawText (m.label, juce::Rectangle<float> (left ? r.getX() + 9.0f : r.getRight() - 31.0f, y - 1.0f, 22.0f, 12.0f),
                    left ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }
}

static void drawRuler (juce::Graphics& g, juce::Rectangle<float> r, int64 from, int64 to, double sr)
{
    const double secs = (double) (to - from) / juce::jmax (1.0, sr);
    if (secs <= 0) return;
    static const double steps[] = { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 30, 60, 120 };
    double step = steps[0];
    for (double s : steps) { step = s; if (secs / s <= 10.0) break; }
    const double t0 = std::ceil ((double) from / sr / step) * step;
    g.setColour (colours::oledDim.withAlpha (0.9f));
    for (double t = t0; t <= (double) to / sr; t += step)
    {
        const float x = xOf (r, from, to, (int64) std::llround (t * sr));
        const bool major = std::fmod (std::round (t / step), 5.0) == 0.0;
        g.fillRect (juce::Rectangle<float> (x, r.getBottom() - (major ? 5.0f : 3.0f), 1.0f, major ? 5.0f : 3.0f));
    }
}

//==============================================================================
void draw (juce::Graphics& g, juce::Rectangle<float> r, const Sample* s, const Options& o, Peaks& cache)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt().expanded (1));

    // frame
    g.setColour (colours::oledBg);
    g.fillRect (r);
    g.setColour (colours::oledDim);
    g.drawRect (r, 1.0f);
    auto in = r.reduced (1.0f);

    if (s == nullptr || s->numFrames() < 2)
    {
        oled::text (g, "NO SAMPLE", in, juce::Justification::centred, o.compact ? 8.0f : 10.0f, false, colours::oledDim);
        return;
    }

    const int64 from = juce::jlimit<int64> (0, s->numFrames() - 1, o.from);
    const int64 to = juce::jlimit<int64> (from + 1, s->numFrames(), o.to);
    const int cols = juce::jmax (1, (int) in.getWidth());
    cache.update (*s, from, to, cols);

    auto colX = [&] (int64 p) { return xOf (in, from, to, p); };
    const bool hasHl = o.hlFrom >= 0 && o.hlTo > o.hlFrom;
    const float hx1 = hasHl ? colX (o.hlFrom) : in.getX(), hx2 = hasHl ? colX (o.hlTo) : in.getRight();
    const bool loopOn = o.loopStyle != LoopStyle::None && o.loopTo > o.loopFrom;
    const float lx1 = loopOn ? colX (o.loopFrom) : 0.0f, lx2 = loopOn ? colX (o.loopTo) : 0.0f;

    // grid
    g.setColour (colours::oledGrid);
    if (o.log)
    {
        for (float db : { -6.0f, -18.0f, -30.0f })
            g.fillRect (juce::Rectangle<float> (in.getX(), in.getBottom() - in.getHeight() * (db + 48.0f) / 48.0f, in.getWidth(), 1.0f));
    }
    else
    {
        g.fillRect (juce::Rectangle<float> (in.getX(), in.getCentreY(), in.getWidth(), 1.0f));
        g.fillRect (juce::Rectangle<float> (in.getX(), in.getY() + in.getHeight() * 0.25f, in.getWidth(), 1.0f).withHeight (0.6f));
        g.fillRect (juce::Rectangle<float> (in.getX(), in.getY() + in.getHeight() * 0.75f, in.getWidth(), 1.0f).withHeight (0.6f));
    }

    // highlight band (everything happens between START and END)
    if (hasHl)
    {
        const auto band = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (in.getX(), hx1), in.getY(), juce::jmin (in.getRight(), hx2), in.getBottom());
        if (band.getWidth() > 0)
        {
            g.setColour (colours::oledText.withAlpha (0.10f));
            g.fillRect (band);
            g.setColour (colours::oledText.withAlpha (0.35f));
            g.fillRect (band.withHeight (1.0f));
            g.fillRect (band.withY (band.getBottom() - 1.0f).withHeight (1.0f));
        }
    }
    // loop region
    if (loopOn && o.loopStyle == LoopStyle::Invert)
    {
        g.setColour (colours::oledText.withAlpha (o.compact ? 0.75f : 0.85f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (in.getX(), lx1), in.getY(), juce::jmin (in.getRight(), lx2), in.getBottom()));
    }

    // waveform columns
    const float base = in.getBottom(), mid = in.getCentreY(), H = in.getHeight();
    for (int x = 0; x < cols; ++x)
    {
        const float px = in.getX() + (float) x;
        const bool inHl = ! hasHl || (px + 0.5f >= hx1 && px + 0.5f <= hx2);
        const bool inLoop = loopOn && o.loopStyle == LoopStyle::Invert && px + 0.5f >= lx1 && px + 0.5f <= lx2;
        juce::Colour cPeak, cRms;
        if (inLoop)      { cPeak = colours::oledBg.withAlpha (0.55f); cRms = colours::oledBg; }
        else if (inHl)   { cPeak = colours::oledText.withAlpha (0.55f); cRms = colours::oledText; }
        else             { cPeak = colours::oledDim.withAlpha (0.45f); cRms = colours::oledDim.withAlpha (0.85f); }

        const float lo = cache.mn[(size_t) x], hi = cache.mx[(size_t) x], rm = cache.rms[(size_t) x];
        if (o.log)
        {
            const float hp = logHeight (juce::jmax (std::abs (lo), std::abs (hi))) * H;
            const float hr = logHeight (rm) * H;
            g.setColour (cPeak); g.fillRect (juce::Rectangle<float> (px, base - hp, 1.0f, hp));
            if (! cache.sampleLevel) { g.setColour (cRms); g.fillRect (juce::Rectangle<float> (px, base - hr, 1.0f, hr)); }
            else { g.setColour (cRms); g.fillRect (juce::Rectangle<float> (px, base - hp, 1.0f, juce::jmin (2.0f, hp))); }
        }
        else if (cache.sampleLevel)
        {
            // continuous line through the interpolated samples
            const float y = mid - juce::jlimit (-1.0f, 1.0f, hi) * H * 0.5f;
            const float yp = x > 0 ? mid - juce::jlimit (-1.0f, 1.0f, cache.mx[(size_t) x - 1]) * H * 0.5f : y;
            g.setColour (cPeak.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (px, juce::jmin (y, mid), 1.0f, std::abs (y - mid)));
            g.setColour (cRms);
            g.fillRect (juce::Rectangle<float> (px, juce::jmin (y, yp), 1.0f, juce::jmax (1.2f, std::abs (y - yp))));
        }
        else
        {
            const float y1 = mid - juce::jlimit (-1.0f, 1.0f, hi) * H * 0.5f, y2 = mid - juce::jlimit (-1.0f, 1.0f, lo) * H * 0.5f;
            g.setColour (cPeak); g.fillRect (juce::Rectangle<float> (px, y1, 1.0f, juce::jmax (1.0f, y2 - y1)));
            const float rh = juce::jmin (rm, 1.0f) * H * 0.5f;
            g.setColour (cRms); g.fillRect (juce::Rectangle<float> (px, mid - rh, 1.0f, juce::jmax (1.0f, rh * 2.0f)));
        }
    }

    // sample dots when zoomed right in
    if (cache.sampleLevel && ! o.log && ! o.compact)
    {
        const double pxPerFrame = in.getWidth() / (double) (to - from);
        if (pxPerFrame >= 5.0)
        {
            g.setColour (colours::oledText);
            for (int64 i = from; i < to; ++i)
            {
                float v = 0;
                for (int ch = 0; ch < s->numChannels(); ++ch) v += s->buffer.getReadPointer (ch)[i];
                v /= (float) s->numChannels();
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ colX (i), mid - juce::jlimit (-1.0f, 1.0f, v) * H * 0.5f }));
            }
        }
    }

    // loop band (top bar) style
    if (loopOn && o.loopStyle == LoopStyle::Band)
    {
        const auto bar = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (in.getX(), lx1), in.getY(), juce::jmin (in.getRight(), lx2), in.getY() + 3.0f);
        g.setColour (colours::oledText.withAlpha (0.7f));
        g.fillRect (bar);
    }

    // regions that will be removed (CUT / ERASE preview)
    for (auto& h : o.hatched)
    {
        const float a = juce::jmax (in.getX(), colX (h.first)), b = juce::jmin (in.getRight(), colX (h.second));
        if (b <= a) continue;
        juce::Graphics::ScopedSaveState s2 (g);
        g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (a, in.getY(), b, in.getBottom()).toNearestInt());
        g.setColour (colours::oledBg.withAlpha (0.55f));
        g.fillRect (in);
        g.setColour (colours::oledWarn.withAlpha (0.45f));
        for (float d = a - in.getHeight(); d < b; d += 6.0f)
            g.drawLine (d, in.getBottom(), d + in.getHeight(), in.getY(), 1.0f);
    }

    // slices
    for (auto p : o.slices)
    {
        if (p < from || p > to) continue;
        const float x = colX (p);
        g.setColour (colours::oledText.withAlpha (0.6f));
        for (float y = in.getY(); y < in.getBottom(); y += 4.0f) g.fillRect (juce::Rectangle<float> (x, y, 1.0f, 2.0f));
    }

    if (o.overlay) o.overlay (g, in, colX);

    if (! o.compact) drawRuler (g, in, from, to, o.sampleRate);

    // overview: the part shown in the zoomed view below
    if (o.windowFrom >= 0 && o.windowTo > o.windowFrom && (o.windowFrom > from || o.windowTo < to))
    {
        const float a = colX (o.windowFrom), b = juce::jmax (a + 2.0f, colX (o.windowTo));
        g.setColour (colours::oledBg.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (in.getX(), in.getY(), a, in.getBottom()));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (b, in.getY(), in.getRight(), in.getBottom()));
        g.setColour (playWhite.withAlpha (0.9f));
        g.drawRect (juce::Rectangle<float>::leftTopRightBottom (a, r.getY(), b, r.getBottom()), 1.0f);
    }

    // edit points
    for (auto& m : o.markers)
    {
        if (m.pos < from) { if (! o.compact) drawOffscreen (g, in, m, true); continue; }
        if (m.pos > to)   { if (! o.compact) drawOffscreen (g, in, m, false); continue; }
        drawFlag (g, in, colX (m.pos), m, o.compact);
    }

    // play lines: MIDI voices (thin) and the audition (white, with glow)
    for (auto v : o.voices)
    {
        if (v < from || v > to) continue;
        const float x = colX (v);
        g.setColour (playWhite.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x, in.getY(), 1.0f, in.getHeight()));
    }
    if (o.playing && o.playPos >= from && o.playPos <= to)
    {
        const float x = colX (o.playPos);
        g.setColour (playWhite.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x - 3.0f, in.getY(), 6.0f, in.getHeight()));
        g.setColour (playWhite);
        g.fillRect (juce::Rectangle<float> (x - 0.75f, in.getY(), 1.5f, in.getHeight()));
        if (! o.compact)
        {
            juce::Path tri;
            tri.startNewSubPath (x - 5.0f, in.getY());
            tri.lineTo (x + 5.0f, in.getY());
            tri.lineTo (x, in.getY() + 6.0f);
            tri.closeSubPath();
            g.fillPath (tri);
        }
    }

    if (o.compact) return;

    // tags
    if (o.tagLeft.isNotEmpty())
    {
        auto t = juce::Rectangle<float> (in.getX() + 3.0f, in.getY() + 14.0f, 70.0f, 13.0f);
        g.setColour (colours::oledBg.withAlpha (0.7f));
        g.fillRect (t.withWidth (8.0f + 6.2f * (float) o.tagLeft.length()));
        oled::text (g, o.tagLeft, t.translated (2.0f, 0), juce::Justification::centredLeft, 7.5f, false, colours::oledDim.brighter (0.6f));
    }
    if (o.showStatus)
    {
        juce::String st;
        juce::Colour col;
        if (o.playing)
        {
            st = u8 ("\xe2\x96\xb6 PLAY");
            if (o.playPos >= 0) st << " " << juce::String ((double) o.playPos / juce::jmax (1.0, o.sampleRate), 2) << "s";
            col = playWhite;
        }
        else if (! o.voices.empty()) { st = u8 ("\xe2\x99\xaa MIDI"); col = playWhite.withAlpha (0.8f); }
        else { st = u8 ("\xe2\x96\xa0 STOP"); col = colours::oledDim.brighter (0.4f); }
        const float w = 8.0f + 6.4f * (float) st.length();
        auto t = juce::Rectangle<float> (in.getRight() - w - 3.0f, in.getY() + 14.0f, w, 13.0f);
        g.setColour (colours::oledBg.withAlpha (0.75f));
        g.fillRect (t);
        g.setColour (col.withAlpha (0.6f));
        g.drawRect (t, 1.0f);
        oled::text (g, st, t, juce::Justification::centred, 7.5f, true, col);
    }
}

} // namespace s6k::ui::wave
