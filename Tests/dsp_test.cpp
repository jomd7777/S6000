// Numerical sanity test of the DSP blocks (filters / envelopes / LFOs).
//   g++ -std=c++17 -O2 -I<shim> Tests/dsp_test.cpp Source/Core/VoiceFilter.cpp
#include "../Source/Core/VoiceFilter.h"
#include <cstdio>
#include <random>
using namespace s6k;

int main()
{
    int fails = 0;
    const float sr = 44100.0f;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);

    // 1) every filter type stays bounded for any cutoff / resonance, with control-rate updates
    for (int t = 0; t < 26; ++t)
    {
        float peak = 0; bool nan = false;
        for (float c : { 0.0f, 10.0f, 35.0f, 60.0f, 85.0f, 100.0f })
            for (float r : { 0.0f, 0.5f, 1.0f })
            {
                VoiceFilter f; f.reset();
                for (int i = 0; i < 44100; ++i)
                {
                    if (i % 32 == 0) f.setup (t, c + 10.0f * std::sin (i * 0.0003f), r, sr);   // swept, as with an envelope
                    float y = f.bypass ? 0.0f : f.process (noise (rng) * 0.5f);
                    if (! std::isfinite (y)) nan = true;
                    peak = std::max (peak, std::abs (y));
                }
            }
        const bool ok = ! nan && peak < 40.0f;
        if (! ok) ++fails;
        std::printf ("filter %2d  peak %7.3f %s\n", t, peak, ok ? "ok" : "FAIL");
    }

    // 2) 2-POLE LP attenuates highs: 10 kHz sine through cutoff 30 (~160 Hz)
    {
        VoiceFilter f; f.reset(); f.setup (0, 30.0f, 0.0f, sr);
        float pk = 0;
        for (int i = 0; i < 20000; ++i) { float y = f.process (std::sin (2 * 3.14159f * 10000 * i / sr)); if (i > 2000) pk = std::max (pk, std::abs (y)); }
        std::printf ("LP 10k through 160Hz: %.5f %s\n", pk, pk < 0.01f ? "ok" : "FAIL"); if (pk >= 0.01f) ++fails;
    }
    // 3) LP fully open + no resonance is bypassed (CPU saving)
    { VoiceFilter f; f.setup (0, 100.0f, 0.0f, sr); std::printf ("bypass when open: %s\n", f.bypass ? "ok" : "FAIL"); if (! f.bypass) ++fails; }

    // 4) ADSR reaches sustain and ends after release
    {
        dsp::Adsr e; const float cr = sr / 32.0f;
        e.set (dsp::envTimeMs (20), dsp::envTimeMs (50), 0.6f, dsp::envTimeMs (40), cr);
        e.noteOn();
        for (int i = 0; i < (int) cr * 3; ++i) e.tick();
        const float sus = e.level;
        e.noteOff();
        int n = 0; while (e.isActive() && n < cr * 30) { e.tick(); ++n; }
        const bool ok = std::abs (sus - 0.6f) < 0.01f && ! e.isActive();
        std::printf ("ADSR sustain %.3f, release %.3fs %s\n", sus, n / cr, ok ? "ok" : "FAIL"); if (! ok) ++fails;
    }
    // 5) AUX envelope passes through its 4 levels
    {
        dsp::AuxEnv a; int r[4] = { 20, 20, 20, 20 }, l[4] = { 100, 30, 70, 0 };
        a.set (r, l, sr / 32.0f); a.noteOn();
        for (int i = 0; i < 4000; ++i) a.tick();
        const float s = a.level; a.noteOff();
        for (int i = 0; i < 4000; ++i) a.tick();
        const bool ok = std::abs (s - 0.7f) < 0.01f && std::abs (a.level) < 0.01f;
        std::printf ("AUX env sustain %.2f end %.2f %s\n", s, a.level, ok ? "ok" : "FAIL"); if (! ok) ++fails;
    }
    // 6) LFO rate curve: 0 = ~60 s, 45 ~ 5 Hz, 100 = 100 Hz
    {
        const float a = dsp::lfoRateHz (0), b = dsp::lfoRateHz (45), c = dsp::lfoRateHz (100);
        const bool ok = a < 0.02f && b > 3.0f && b < 8.0f && std::abs (c - 100.0f) < 1.0f;
        std::printf ("LFO rates %.4f / %.2f / %.1f Hz %s\n", a, b, c, ok ? "ok" : "FAIL"); if (! ok) ++fails;
        for (int w = 0; w < 9; ++w) for (float p = 0; p < 1; p += 0.01f) { float v = dsp::lfoWave (w, p, 0.3f); if (v < -1.001f || v > 1.001f) { ++fails; std::printf ("wave %d out of range\n", w); break; } }
    }
    std::printf (fails ? "FAILURES %d\n" : "ALL OK\n", fails);
    return fails;
}
