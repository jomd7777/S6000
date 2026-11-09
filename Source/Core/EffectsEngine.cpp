/*
  ==============================================================================
   S6000 Emulation - EB20 effects
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "EffectsEngine.h"

namespace s6k
{
//==============================================================================
void ReverbUnit::prepare (double sr)
{
    rate = sr;
    verb.setSampleRate (sr);
    preL.prepare ((int) (sr * 0.35)); preR.prepare ((int) (sr * 0.35));
    reset();
}

void ReverbUnit::reset()
{
    verb.reset(); preL.clear(); preR.clear(); lfL.reset(); lfR.reset(); gateEnv = 0; gateHold = 0;
}

void ReverbUnit::setParams (int t, int preDelayMs, int time, int diffusion, int hfDamp, int lfDamp, double sr)
{
    static const float baseSize[] = { 0.35f, 0.5f, 0.62f, 0.76f, 0.86f, 0.68f, 0.8f };
    type = juce::jlimit (0, 6, t);
    juce::Reverb::Parameters p;
    p.roomSize = juce::jlimit (0.05f, 0.985f, baseSize[type] + ((float) time * 0.01f - 0.5f) * 0.3f);
    p.damping = juce::jlimit (0.0f, 1.0f, (float) hfDamp * 0.01f * (type == 5 ? 0.5f : 1.0f));
    p.wetLevel = 0.8f;
    p.dryLevel = 0.0f;
    p.width = juce::jlimit (0.0f, 1.0f, 0.4f + (float) diffusion * 0.006f);
    p.freezeMode = 0.0f;
    verb.setParameters (p);
    preSamples = juce::jlimit (0, (int) (sr * 0.3), (int) ((float) preDelayMs * 0.001f * (float) sr));
    lfCut = (float) lfDamp;
    if (lfDamp > 0) { lfL.set (20.0f + (float) lfDamp * 4.0f, (float) sr); lfR.set (20.0f + (float) lfDamp * 4.0f, (float) sr); }
}

void ReverbUnit::process (float* l, float* r, int n)
{
    float inPeak = 0;
    for (int i = 0; i < n; ++i)
    {
        inPeak = juce::jmax (inPeak, std::abs (l[i]) + std::abs (r[i]));
        preL.push (l[i]); preR.push (r[i]);
        l[i] = preL.read (preSamples);
        r[i] = preR.read (preSamples);
    }
    verb.processStereo (l, r, n);

    if (lfCut > 0)
        for (int i = 0; i < n; ++i) { l[i] = lfL.hp (l[i]); r[i] = lfR.hp (r[i]); }

    if (type == 6)   // GATED : close quickly once the input stops
    {
        const float target = inPeak > gateThresh ? 1.0f : 0.0f;
        if (target > 0) gateHold = (float) rate * 0.25f; else gateHold = juce::jmax (0.0f, gateHold - (float) n);
        const float goal = (target > 0 || gateHold > 0) ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            gateEnv += (goal - gateEnv) * (goal > gateEnv ? 0.01f : 0.004f);
            l[i] *= gateEnv; r[i] *= gateEnv;
        }
    }
}

//==============================================================================
void FxChannelUnit::prepare (double newSr, int maxBlock)
{
    sr = newSr;
    modL.prepare ((int) (sr * 0.1)); modR.prepare ((int) (sr * 0.1));
    delL.prepare ((int) (sr * 2.0)); delR.prepare ((int) (sr * 2.0));
    reverb.prepare (sr);
    for (auto* v : { &tmpL, &tmpR, &revL, &revR, &modOutL, &modOutR }) v->assign ((size_t) maxBlock, 0.0f);
    eqHash = revHash = -1;
    reset();
}

void FxChannelUnit::reset()
{
    modL.clear(); modR.clear(); delL.clear(); delR.clear();
    for (auto& b : eq) for (auto& q : b) q.reset();
    for (auto& a : phL) a.reset();
    for (auto& a : phR) a.reset();
    phFbL = phFbR = 0;
    reverb.reset();
}

void FxChannelUnit::processMod (const FxMultiChannel& p, float* l, float* r, int n)
{
    const float fs = (float) sr;
    const float rateHz = 0.05f + std::pow ((float) p.modRate * 0.01f, 2.0f) * 10.0f;
    const float depth = (float) p.modDepth * 0.01f;
    const float fb = juce::jlimit (-0.95f, 0.95f, (float) p.modFeedback * 0.0095f);

    switch (p.modType)
    {
        case 1: case 2:   // CHORUS / FLANGE
        {
            const bool flange = p.modType == 2;
            const float base = (flange ? 0.0012f : 0.012f) * fs;
            const float sweep = (flange ? 0.004f : 0.007f) * fs * depth;
            for (int i = 0; i < n; ++i)
            {
                modPhase += rateHz / fs;
                if (modPhase >= 1.0f) modPhase -= 1.0f;
                const float sl = std::sin (dsp::twoPi * modPhase), sr2 = std::cos (dsp::twoPi * modPhase);
                const float dl = base + sweep * (0.5f + 0.5f * sl), dr = base + sweep * (0.5f + 0.5f * sr2);
                const float wl = modL.readFrac (dl), wr = modR.readFrac (dr);
                modL.push (l[i] + wl * fb); modR.push (r[i] + wr * fb);
                l[i] = wl; r[i] = wr;
            }
            break;
        }
        case 3:   // PHASE
        {
            for (int i = 0; i < n; i += dsp::controlRate)
            {
                const int m = juce::jmin (dsp::controlRate, n - i);
                modPhase += rateHz * (float) m / fs;
                if (modPhase >= 1.0f) modPhase -= 1.0f;
                const float sweepL = 200.0f * std::pow (20.0f, depth * (0.5f + 0.5f * std::sin (dsp::twoPi * modPhase)));
                const float sweepR = 200.0f * std::pow (20.0f, depth * (0.5f + 0.5f * std::cos (dsp::twoPi * modPhase)));
                for (int k = 0; k < 6; ++k) { phL[k].set (sweepL * (1.0f + 0.4f * (float) k), fs); phR[k].set (sweepR * (1.0f + 0.4f * (float) k), fs); }
                for (int j = i; j < i + m; ++j)
                {
                    float yl = l[j] + phFbL * fb, yr = r[j] + phFbR * fb;
                    for (int k = 0; k < 6; ++k) { yl = phL[k].tick (yl); yr = phR[k].tick (yr); }
                    phFbL = yl; phFbR = yr;
                    l[j] = yl; r[j] = yr;
                }
            }
            break;
        }
        case 4:   // ROTARY SPEAKER
        {
            const float slow = 0.5f + (float) p.rotSpeed1 * 0.03f, fast = 3.0f + (float) p.rotSpeed2 * 0.06f;
            const float target = p.rotInitSpeed ? fast : slow;
            const float accel = 0.00002f + (float) p.rotAccel * 0.000004f;
            if (rotSpeed <= 0.0f) rotSpeed = target;
            for (int i = 0; i < n; ++i)
            {
                rotSpeed += (target - rotSpeed) * accel;
                rotPhaseH += rotSpeed / fs;          if (rotPhaseH >= 1.0f) rotPhaseH -= 1.0f;
                rotPhaseL += rotSpeed * 0.85f / fs;  if (rotPhaseL >= 1.0f) rotPhaseL -= 1.0f;
                const float m = 0.5f * (l[i] + r[i]);
                const float sh = std::sin (dsp::twoPi * rotPhaseH), sl = std::sin (dsp::twoPi * rotPhaseL);
                modL.push (m);
                const float doppler = modL.readFrac (0.002f * fs + 0.0012f * fs * depth * sh);
                const float hornL = doppler * (1.0f + 0.5f * depth * sh), hornR = doppler * (1.0f - 0.5f * depth * sh);
                const float drumL = m * (1.0f + 0.3f * depth * sl), drumR = m * (1.0f - 0.3f * depth * sl);
                l[i] = 0.6f * hornL + 0.4f * drumL;
                r[i] = 0.6f * hornR + 0.4f * drumR;
            }
            break;
        }
        case 5:   // FREQ / AMP MOD (vibrato + autopan)
        {
            const float amHz = 0.05f + std::pow ((float) p.amodRate * 0.01f, 2.0f) * 10.0f;
            const float amDepth = (float) p.amodDepth * 0.01f;
            for (int i = 0; i < n; ++i)
            {
                modPhase += rateHz / fs; if (modPhase >= 1.0f) modPhase -= 1.0f;
                amPhase += amHz / fs;    if (amPhase >= 1.0f) amPhase -= 1.0f;
                const float d = 0.003f * fs + 0.0025f * fs * depth * std::sin (dsp::twoPi * modPhase);
                const float wl = modL.readFrac (d), wr = modR.readFrac (d);
                modL.push (l[i] + wl * fb); modR.push (r[i] + wr * fb);
                const float pan = amDepth * std::sin (dsp::twoPi * amPhase);
                l[i] = wl * (1.0f - pan); r[i] = wr * (1.0f + pan);
            }
            break;
        }
        case 6: case 7:   // PITCH SHIFT / PITCH + FEEDBACK  (two-tap granular)
        {
            const float ratioL = std::exp2 (((float) p.pitchLSemi + (float) p.pitchLFine * 0.01f) / 12.0f);
            const float ratioR = std::exp2 (((float) p.pitchRSemi + (float) p.pitchRFine * 0.01f) / 12.0f);
            const float win = 0.05f * fs;
            const float pfb = p.modType == 7 ? juce::jlimit (0.0f, 0.9f, (float) p.modFeedback * 0.009f) : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                psPhase += 1.0f / win;
                if (psPhase >= 1.0f) psPhase -= 1.0f;
                auto shift = [&] (dsp::DelayLine& dl, float ratio, float in)
                {
                    const float ph1 = psPhase, ph2 = std::fmod (psPhase + 0.5f, 1.0f);
                    const float d1 = (1.0f - ratio) * ph1 * win, d2 = (1.0f - ratio) * ph2 * win;
                    const float a = dl.readFrac (std::abs (d1) + 1.0f), b = dl.readFrac (std::abs (d2) + 1.0f);
                    const float g1 = std::sin (juce::MathConstants<float>::pi * ph1), g2 = std::sin (juce::MathConstants<float>::pi * ph2);
                    const float out = a * g1 + b * g2;
                    dl.push (in + out * pfb);
                    return out;
                };
                const float ol = shift (modL, ratioL, l[i]);
                const float orr = shift (modR, ratioR, r[i]);
                l[i] = ol; r[i] = orr;
            }
            break;
        }
        default:
            for (int i = 0; i < n; ++i) { l[i] = 0; r[i] = 0; }
            break;
    }
}

void FxChannelUnit::processDelay (const FxMultiChannel& p, float* l, float* r, int n)
{
    const float fs = (float) sr;
    const int dL = juce::jlimit (1, (int) (fs * 1.95f), (int) ((float) p.delayL * 0.001f * fs));
    const int dR = juce::jlimit (1, (int) (fs * 1.95f), (int) ((float) p.delayR * 0.001f * fs));
    const float fL = juce::jlimit (0.0f, 0.97f, (float) p.fbL * 0.0097f), fR = juce::jlimit (0.0f, 0.97f, (float) p.fbR * 0.0097f);
    const float dampHz = 20000.0f * std::pow (0.05f, (float) p.delayHfDamp * 0.01f);
    delDampL.set (dampHz, fs); delDampR.set (dampHz, fs);

    for (int i = 0; i < n; ++i)
    {
        const float mono = 0.5f * (l[i] + r[i]);
        const float tl = delL.read (dL), tr = delR.read (dR);
        float ol, orr;
        switch (p.delayType)
        {
            case 1:  // MONO LEFT : single delay, mono out
                delL.push (mono + delDampL.lp (tl) * fL);
                ol = orr = tl;
                delR.push (0);
                break;
            case 2:  // MONO L/R : mono in, independent times
                delL.push (mono + delDampL.lp (tl) * fL);
                delR.push (mono + delDampR.lp (tr) * fR);
                ol = tl; orr = tr;
                break;
            case 3:  // XOVER L+R : cross feedback (ping pong)
                delL.push (l[i] + delDampL.lp (tr) * fR);
                delR.push (r[i] + delDampR.lp (tl) * fL);
                ol = tl; orr = tr;
                break;
            default: // STEREO
                delL.push ((p.pingPong ? mono : l[i]) + delDampL.lp (p.pingPong ? tr : tl) * fL);
                delR.push ((p.pingPong ? 0.0f : r[i]) + delDampR.lp (p.pingPong ? tl : tr) * fR);
                ol = tl; orr = tr;
                break;
        }
        l[i] = ol; r[i] = orr;
    }
}

void FxChannelUnit::process (const FxMultiChannel& p, float* l, float* r, int n)
{
    const float fs = (float) sr;
    n = juce::jmin (n, (int) tmpL.size());

    // --- DISTORTION / RING MOD
    if (p.distType == 1)
    {
        const float drive = 1.0f + (float) p.distAmount * 0.3f, lvl = (float) p.distLevel * 0.01f;
        for (int i = 0; i < n; ++i) { l[i] = dsp::fastTanh (l[i] * drive) * lvl; r[i] = dsp::fastTanh (r[i] * drive) * lvl; }
    }
    else if (p.distType == 2)
    {
        const float hz = 20.0f * std::pow (250.0f, (float) p.rmodFreq * 0.01f), depth = (float) p.rmodDepth * 0.01f;
        for (int i = 0; i < n; ++i)
        {
            ringPhase += hz / fs; if (ringPhase >= 1.0f) ringPhase -= 1.0f;
            const float m = 1.0f - depth + depth * std::sin (dsp::twoPi * ringPhase);
            l[i] *= m; r[i] *= m;
        }
    }

    // --- EQ
    if (p.eqOn)
    {
        const int h = p.lowFreq * 7 + p.lfGain * 131 + p.lmidFreq * 17 + p.lmidGain * 1031 + p.hmidFreq * 29 + p.hmidGain * 7919 + p.highFreq * 3 + p.hfGain * 104729;
        if (h != eqHash)
        {
            eqHash = h;
            for (auto& b : eq)
            {
                b[0].setLowShelf (fs, (float) p.lowFreq, (float) p.lfGain);
                b[1].setPeak (fs, (float) p.lmidFreq, 1.0f, (float) p.lmidGain);
                b[2].setPeak (fs, (float) p.hmidFreq, 1.0f, (float) p.hmidGain);
                b[3].setHighShelf (fs, (float) p.highFreq, (float) p.hfGain);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            for (int k = 0; k < 4; ++k) { a = eq[0][(size_t) k].tick (a); b = eq[1][(size_t) k].tick (b); }
            l[i] = a; r[i] = b;
        }
    }

    // keep the DIST/EQ signal (direct)
    std::copy (l, l + n, tmpL.begin());
    std::copy (r, r + n, tmpR.begin());

    const bool modOn = p.modType > 0, delOn = p.delayType > 0, revOn = p.revOn != 0;

    auto runModDelay = [&] (const float* inL, const float* inR)
    {
        std::copy (inL, inL + n, modOutL.begin());
        std::copy (inR, inR + n, modOutR.begin());
        if (modOn) processMod (p, modOutL.data(), modOutR.data(), n);
        if (delOn)
        {
            if (modOn)
            {
                // delay is fed from the mod output and summed with it
                std::copy (modOutL.begin(), modOutL.begin() + n, revL.begin());
                std::copy (modOutR.begin(), modOutR.begin() + n, revR.begin());
                processDelay (p, revL.data(), revR.data(), n);
                for (int i = 0; i < n; ++i) { modOutL[(size_t) i] += revL[(size_t) i]; modOutR[(size_t) i] += revR[(size_t) i]; }
            }
            else
                processDelay (p, modOutL.data(), modOutR.data(), n);
        }
        if (! modOn && ! delOn)
        {
            std::fill (modOutL.begin(), modOutL.begin() + n, 0.0f);
            std::fill (modOutR.begin(), modOutR.begin() + n, 0.0f);
        }

        // MOD/DEL pan + width + level
        const float lvl = (float) p.modDelLevel * 0.01f, w = (float) p.modDelWidth * 0.01f;
        const float pan = (float) p.modDelPan / 50.0f;
        const float pl = juce::jmin (1.0f, 1.0f - pan), pr = juce::jmin (1.0f, 1.0f + pan);
        for (int i = 0; i < n; ++i)
        {
            const float a = modOutL[(size_t) i], b = modOutR[(size_t) i];
            const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * w;
            modOutL[(size_t) i] = (mid + side) * lvl * pl;
            modOutR[(size_t) i] = (mid - side) * lvl * pr;
        }
    };

    auto runReverb = [&] (const float* inL, const float* inR)
    {
        const int h = p.revType * 7 + p.preDelay * 131 + p.revTime * 1031 + p.diffusion * 7919 + p.revHfDamp * 104729 + p.revLfDamp * 3;
        if (h != revHash) { revHash = h; reverb.setParams (p.revType, p.preDelay, p.revTime, p.diffusion, p.revHfDamp, p.revLfDamp, sr); }
        std::copy (inL, inL + n, revL.begin());
        std::copy (inR, inR + n, revR.begin());
        reverb.process (revL.data(), revR.data(), n);
        const float lvl = (float) p.revLevel * 0.01f, pan = (float) p.revPan / 50.0f;
        const float pl = juce::jmin (1.0f, 1.0f - pan) * lvl, pr = juce::jmin (1.0f, 1.0f + pan) * lvl;
        for (int i = 0; i < n; ++i) { revL[(size_t) i] *= pl; revR[(size_t) i] *= pr; }
    };

    const float direct = p.directSignal ? (float) p.distEqLevel * 0.01f : 0.0f;

    if (p.path == 2 && revOn)          // REV > MOD
    {
        runReverb (tmpL.data(), tmpR.data());
        std::vector<float>& rl = revL; std::vector<float>& rr = revR;
        for (int i = 0; i < n; ++i) { l[i] = tmpL[(size_t) i] + rl[(size_t) i]; r[i] = tmpR[(size_t) i] + rr[(size_t) i]; }
        runModDelay (l, r);
        for (int i = 0; i < n; ++i)
        {
            l[i] = direct * tmpL[(size_t) i] + modOutL[(size_t) i] + (modOn || delOn ? 0.0f : rl[(size_t) i]);
            r[i] = direct * tmpR[(size_t) i] + modOutR[(size_t) i] + (modOn || delOn ? 0.0f : rr[(size_t) i]);
        }
        return;
    }

    runModDelay (tmpL.data(), tmpR.data());

    if (revOn)
    {
        if (p.path == 0)   // MOD > REV (series)
        {
            for (int i = 0; i < n; ++i) { l[i] = tmpL[(size_t) i] + modOutL[(size_t) i]; r[i] = tmpR[(size_t) i] + modOutR[(size_t) i]; }
            runReverb (l, r);
        }
        else               // parallel
            runReverb (tmpL.data(), tmpR.data());
    }

    for (int i = 0; i < n; ++i)
    {
        l[i] = direct * tmpL[(size_t) i] + modOutL[(size_t) i] + (revOn ? revL[(size_t) i] : 0.0f);
        r[i] = direct * tmpR[(size_t) i] + modOutR[(size_t) i] + (revOn ? revR[(size_t) i] : 0.0f);
    }
}

//==============================================================================
void EffectsEngine::prepare (double sr, int maxBlock)
{
    rate = sr;
    for (auto& f : fx) f.prepare (sr, maxBlock);
    for (auto& r : rv) r.prepare (sr);
    for (int i = 0; i < 2; ++i) { fxOutL[i].assign ((size_t) maxBlock, 0.0f); fxOutR[i].assign ((size_t) maxBlock, 0.0f); }
    tailBlocks = juce::jmax (1, (int) (sr * 12.0 / juce::jmax (16, maxBlock)));
    for (auto& t : tail) t = 0;
    for (auto& h : rvHash) h = -1;
}

void EffectsEngine::reset()
{
    for (auto& f : fx) f.reset();
    for (auto& r : rv) r.reset();
}

void EffectsEngine::process (const FxSettings& s, juce::AudioBuffer<float>& fxBus, float* mainL, float* mainR, int n)
{
    n = juce::jmin (n, (int) fxOutL[0].size());
    if (s.bypass) { for (auto& l : levels) l = 0; return; }

    bool running[4] {};
    for (int c = 0; c < 4; ++c)
    {
        const float peak = juce::jmax (fxBus.getMagnitude (c * 2, 0, n), fxBus.getMagnitude (c * 2 + 1, 0, n));
        if (peak > 1.0e-6f) tail[c] = tailBlocks;
        else if (tail[c] > 0) --tail[c];
        running[c] = tail[c] > 0;
    }
    // RV3/RV4 fed from FX1/FX2 keep those running
    for (int r = 0; r < 2; ++r)
        if (s.rv[r].input > 0 && running[s.rv[r].input - 1]) { tail[2 + r] = juce::jmax (tail[2 + r], 1); running[2 + r] = true; }

    for (int c = 0; c < 2; ++c)
    {
        float* l = fxOutL[c].data();
        float* r = fxOutR[c].data();
        if (! running[c]) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); levels[(size_t) c] = 0; continue; }
        std::copy (fxBus.getReadPointer (c * 2), fxBus.getReadPointer (c * 2) + n, l);
        std::copy (fxBus.getReadPointer (c * 2 + 1), fxBus.getReadPointer (c * 2 + 1) + n, r);
        fx[c].process (s.fx[c], l, r, n);
        float pk = 0;
        for (int i = 0; i < n; ++i) { mainL[i] += l[i]; mainR[i] += r[i]; pk = juce::jmax (pk, std::abs (l[i])); }
        levels[(size_t) c] = pk;
    }

    for (int k = 0; k < 2; ++k)
    {
        const auto& p = s.rv[k];
        const int c = 2 + k;
        if (! running[c] || ! p.on) { levels[(size_t) c] = 0; continue; }
        float* l = fxBus.getWritePointer (c * 2);
        float* r = fxBus.getWritePointer (c * 2 + 1);
        if (p.input > 0)
        {
            std::copy (fxOutL[p.input - 1].begin(), fxOutL[p.input - 1].begin() + n, l);
            std::copy (fxOutR[p.input - 1].begin(), fxOutR[p.input - 1].begin() + n, r);
        }
        const float in = (float) p.inputLevel * 0.01f;
        for (int i = 0; i < n; ++i) { l[i] *= in; r[i] *= in; }
        const int h = p.type * 7 + p.preDelay * 131 + p.time * 1031 + p.diffusion * 7919 + p.hfDamp * 104729 + p.lfDamp * 3;
        if (h != rvHash[k]) { rvHash[k] = h; rv[k].setParams (p.type, p.preDelay, p.time, p.diffusion, p.hfDamp, p.lfDamp, rate); }
        rv[k].process (l, r, n);
        const float lvl = (float) p.level * 0.01f, pan = (float) p.pan / 50.0f;
        const float pl = juce::jmin (1.0f, 1.0f - pan) * lvl, pr = juce::jmin (1.0f, 1.0f + pan) * lvl;
        float pk = 0;
        for (int i = 0; i < n; ++i) { mainL[i] += l[i] * pl; mainR[i] += r[i] * pr; pk = juce::jmax (pk, std::abs (l[i] * pl)); }
        levels[(size_t) c] = pk;
    }
}

} // namespace s6k
