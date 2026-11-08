/*
  ==============================================================================
   S6000 Emulation - FX mode, EB20 4-channel effects (manual p.186-215)
     MULTIFX 1/2 : RING MOD/DISTORTION, EQ, MODULATION, DELAY, REVERB, OUTPUT MIX
     RV3 / RV4   : reverb channels (input SEND / FX1 / FX2)
     EFFECTS TOOLS (templates), COPY FX
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;

namespace
{
    void drawChain (juce::Graphics& g, juce::Rectangle<float> r, const FxMultiChannel& c, int highlightSection, float level, const juce::String& label)
    {
        static const char* blocks[] = { "DIST", "EQ", "MOD", "DEL", "REV" };
        const bool on[] = { c.distType > 0, c.eqOn != 0, c.modType > 0, c.delayType > 0, c.revOn != 0 };
        oled::text (g, label, r.removeFromLeft (30.0f), juce::Justification::centredLeft, 10.0f, true);
        const float w = r.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            auto b = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.0f, 1.0f);
            g.setColour (colours::oledText);
            if (on[i]) g.fillRect (b); else g.drawRect (b, 1.0f);
            if (i == highlightSection) { g.setColour (colours::oledWarn); g.drawRect (b.expanded (1.5f), 1.5f); }
            oled::text (g, blocks[i], b, juce::Justification::centred, 10.0f, true, on[i] ? colours::oledBg : colours::oledText);
            if (i < 4) { g.setColour (colours::oledText); g.drawLine (b.getRight(), b.getCentreY(), b.getRight() + 4.0f, b.getCentreY(), 1.0f); }
        }
        if (level > 0.0005f)
        {
            g.setColour (colours::oledWarn);
            g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre ({ r.getRight() + 3.0f, r.getCentreY() }));
        }
    }
}

//==============================================================================
class FxMainPage : public Page
{
public:
    explicit FxMainPage (Ctx& c) : Page (c, PageId::Fx, PageId::None, Mode::Fx) {}
    juce::String title() const override { return "EFFECTS"; }
    bool wantsTicks() const override { return true; }

    void build (Keys& k) override
    {
        k[0] = multiHeader (ctx);
        k[1] = button ("MULTIFX 1", [this] { ctx.pages->show (PageId::FxEdit1); });
        k[2] = button ("MULTIFX 2", [this] { ctx.pages->show (PageId::FxEdit2); });
        k[3] = button ("RV3", [this] { ctx.pages->show (PageId::FxRv3); });
        k[4] = button ("RV4", [this] { ctx.pages->show (PageId::FxRv4); });
        SoftKey bp;
        bp.kind = SoftKey::Kind::Check;
        bp.title = "FX BYPASS";
        bp.checked = [this] { auto* m = ctx.multi(); return m != nullptr && m->fx.bypass != 0; };
        bp.onPress = [this] { if (auto* m = ctx.multi()) m->fx.bypass = m->fx.bypass ? 0 : 1; };
        k[6] = bp;
        k[8] = button ("EFFECTS TOOLS", [this] { tools(); });
        k[9] = button ("REVERB 1", [this] { openReverb (0); });
        k[10] = button ("REVERB 2", [this] { openReverb (1); });
        k[11] = button ("REVERB 3", [this] { ctx.pages->show (PageId::FxRv3); });
        k[12] = button ("REVERB 4", [this] { ctx.pages->show (PageId::FxRv4); });
        k[15] = button ("COPY FX", [this] { copyFx(); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* m = ctx.multi();
        if (m == nullptr) return;
        r.removeFromTop (6.0f);
        drawChain (g, r.removeFromTop (22.0f), m->fx.fx[0], -1, ctx.proc.effects.getChannelLevel (0), "FX1");
        r.removeFromTop (8.0f);
        drawChain (g, r.removeFromTop (22.0f), m->fx.fx[1], -1, ctx.proc.effects.getChannelLevel (1), "FX2");
        r.removeFromTop (10.0f);
        for (int i = 0; i < 2; ++i)
        {
            const auto& rv = m->fx.rv[i];
            auto line = r.removeFromTop (20.0f);
            oled::text (g, "RV" + juce::String (3 + i), line.removeFromLeft (30.0f), juce::Justification::centredLeft, 10.0f, true);
            oled::text (g, (rv.input == 0 ? juce::String ("SEND") : "FX" + juce::String (rv.input)) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + names::reverbType (rv.type),
                        line, juce::Justification::centredLeft, 10.5f, false, rv.on ? colours::oledText : colours::oledDim);
            r.removeFromTop (4.0f);
        }
        if (m->fx.bypass) oled::text (g, "*** BYPASSED ***", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true, colours::oledWarn);
    }

private:
    void openReverb (int ch)
    {
        ctx.pages->show (ch == 0 ? PageId::FxEdit1 : PageId::FxEdit2);
        if (auto* p = ctx.pages->current()) p->altMode[0] = true;   // request REVERB section
        ctx.pages->refresh();
    }

    void copyFx()
    {
        juce::StringArray names;
        for (auto* m : ctx.mem.multis) names.add (m->name);
        ctx.pages->pushPopup (std::make_unique<ListPopup> (ctx, "COPY FX FROM MULTI", names, 0, [this] (int i)
        {
            auto* dst = ctx.multi();
            if (dst != nullptr && juce::isPositiveAndBelow (i, ctx.mem.multis.size())) dst->fx = ctx.mem.multis.getObjectPointer (i)->fx;
        }));
    }

    void tools()
    {
        std::vector<MenuPopup::Item> it;
        it.push_back ({ "INIT ALL FX", [this] { if (auto* m = ctx.multi()) { Multi fresh; m->fx = fresh.fx; } }, 9 });
        it.push_back ({ "COPY FX", [this] { copyFx(); }, 10 });
        it.push_back ({ "ALL REVERBS OFF", [this] { if (auto* m = ctx.multi()) { m->fx.fx[0].revOn = m->fx.fx[1].revOn = 0; m->fx.rv[0].on = m->fx.rv[1].on = 0; } }, 11 });
        it.push_back ({ "ALL REVERBS ON", [this] { if (auto* m = ctx.multi()) { m->fx.fx[0].revOn = m->fx.fx[1].revOn = 1; m->fx.rv[0].on = m->fx.rv[1].on = 1; } }, 12 });
        ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "EFFECTS TOOLS", it));
    }
};

//==============================================================================
class FxEditPage : public Page
{
public:
    FxEditPage (Ctx& c, int channel) : Page (c, channel == 0 ? PageId::FxEdit1 : PageId::FxEdit2, PageId::Fx, Mode::Fx), ch (channel) {}
    juce::String title() const override { static const char* s[] = { "RING/DIST", "EQ", "MODULATION", "DELAY", "REVERB", "OUTPUT MIX" }; return s[section]; }
    bool wantsTicks() const override { return true; }

    FxMultiChannel* C() const { auto* m = ctx.multi(); return m ? &m->fx.fx[ch] : nullptr; }
    kb::IntRef f (int FxMultiChannel::* m) const { return [this, m]() -> int* { auto* c = C(); return c ? &(c->*m) : nullptr; }; }

    void build (Keys& k) override
    {
        if (altMode[0]) { section = 4; altMode[0] = false; pageIdx = 0; }
        auto hp = std::make_shared<Param>();
        hp->get = [this] { return ch; };
        hp->set = [] (int) {};
        hp->format = [this] (int) { auto* m = ctx.multi(); return "MULTIFX " + juce::String (ch + 1) + (m ? "  " + m->name : juce::String()); };
        k[0] = header ({}, hp);

        static const char* secNames[] = { "RING/DIST", "EQ", "MODULATION", "DELAY", "REVERB", "OUTPUT MIX" };
        for (int s = 0; s < 6; ++s)
        {
            k[(size_t) (1 + s)] = button (secNames[s], [this, s] { section = s; pageIdx = 0; selected = -1; });
            k[(size_t) (1 + s)].highlight = s == section;
        }
        k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::Fx); });
        k[8] = button ("EFFECTS TOOLS", [this] { templates(); });

        std::vector<SoftKey> params = sectionParams();
        const int perPage = 7;
        const int pages = juce::jmax (1, ((int) params.size() + perPage - 1) / perPage);
        pageIdx = juce::jlimit (0, pages - 1, pageIdx);
        const bool more = params.size() > (size_t) perPage;
        const int slots = more ? 6 : 7;
        for (int i = 0; i < slots; ++i)
        {
            const size_t idx = (size_t) (pageIdx * slots + i);
            if (idx < params.size()) k[(size_t) (9 + i)] = params[idx];
        }
        if (more) k[15] = button ("MORE " + juce::String (pageIdx + 1) + "/" + juce::String (((int) params.size() + slots - 1) / slots), [this, slots, n = (int) params.size()]
        {
            pageIdx = (pageIdx + 1) % juce::jmax (1, (n + slots - 1) / slots);
            selected = -1;
        });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* c = C();
        if (c == nullptr) return;
        r.removeFromTop (4.0f);
        drawChain (g, r.removeFromTop (22.0f), *c, section == 5 ? -1 : section, ctx.proc.effects.getChannelLevel (ch), "FX" + juce::String (ch + 1));
        r.removeFromTop (8.0f);
        juce::StringArray lines;
        switch (section)
        {
            case 0: lines = { "DIST/RING: " + juce::String (c->distType == 0 ? "OFF" : c->distType == 1 ? "DISTORTION" : "RING MOD") }; break;
            case 1: lines = { juce::String ("EQ: ") + (c->eqOn ? "ON" : "OFF"), "LOW " + juce::String (c->lfGain) + "dB  HIGH " + juce::String (c->hfGain) + "dB" }; break;
            case 2: lines = { "MOD: " + names::modFxType (c->modType) }; break;
            case 3: lines = { "DELAY: " + names::delayType (c->delayType), juce::String (c->delayL) + "ms / " + juce::String (c->delayR) + "ms" }; break;
            case 4: lines = { "REVERB: " + names::reverbType (c->revType) + (c->revOn ? "" : " (OFF)") }; break;
            default:
            {
                static const char* paths[] = { "MOD/DEL > REVERB", "MOD/DEL || REVERB", "REVERB > MOD/DEL" };
                lines = { paths[juce::jlimit (0, 2, c->path)], juce::String ("DIRECT ") + (c->directSignal ? "ON" : "OFF") };
                break;
            }
        }
        for (auto& l : lines) oled::text (g, l, r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        r.removeFromTop (6.0f);
        oled::meter (g, r.removeFromTop (12.0f).reduced (10.0f, 0), ctx.proc.effects.getChannelLevel (ch));
    }

private:
    int ch, section = 0, pageIdx = 0;

    std::vector<SoftKey> sectionParams()
    {
        std::vector<SoftKey> p;
        auto* c = C();
        switch (section)
        {
            case 0:
                p.push_back (value ("Type", f (&FxMultiChannel::distType), 0, 2, list ({ "OFF", "DISTORTION", "RING MOD" })));
                p.push_back (value ("Distortion", f (&FxMultiChannel::distAmount), 0, 100, num()));
                p.push_back (value ("Output Level", f (&FxMultiChannel::distLevel), 0, 100, num()));
                p.push_back (value ("RMod Frequency", f (&FxMultiChannel::rmodFreq), 0, 100, num()));
                p.push_back (value ("RMod Depth", f (&FxMultiChannel::rmodDepth), 0, 100, num()));
                break;
            case 1:
                p.push_back (check ("EQ ON", f (&FxMultiChannel::eqOn)));
                p.push_back (value ("Low Freq", f (&FxMultiChannel::lowFreq), 20, 1000, suffix ("Hz")));
                p.push_back (value ("LF Gain", f (&FxMultiChannel::lfGain), -18, 18, db()));
                p.push_back (value ("LMid Freq", f (&FxMultiChannel::lmidFreq), 100, 5000, suffix ("Hz")));
                p.push_back (value ("LMid Gain", f (&FxMultiChannel::lmidGain), -18, 18, db()));
                p.push_back (value ("HMid Freq", f (&FxMultiChannel::hmidFreq), 500, 16000, suffix ("Hz")));
                p.push_back (value ("HMid Gain", f (&FxMultiChannel::hmidGain), -18, 18, db()));
                p.push_back (value ("High Freq", f (&FxMultiChannel::highFreq), 1000, 20000, suffix ("Hz")));
                p.push_back (value ("HF Gain", f (&FxMultiChannel::hfGain), -18, 18, db()));
                break;
            case 2:
            {
                p.push_back (value ("Mod Type", f (&FxMultiChannel::modType), 0, 7, [] (int v) { return names::modFxType (v); }));
                const int t = c ? c->modType : 0;
                if (t >= 1 && t <= 3)
                {
                    p.push_back (value ("Rate", f (&FxMultiChannel::modRate), 0, 100, num()));
                    p.push_back (value ("Depth", f (&FxMultiChannel::modDepth), 0, 100, num()));
                    p.push_back (value ("Feedback", f (&FxMultiChannel::modFeedback), -100, 100, signed3()));
                }
                else if (t == 4)
                {
                    p.push_back (value ("Speed 1", f (&FxMultiChannel::rotSpeed1), 0, 100, num()));
                    p.push_back (value ("Speed 2", f (&FxMultiChannel::rotSpeed2), 0, 100, num()));
                    p.push_back (value ("Acceleration", f (&FxMultiChannel::rotAccel), 0, 100, num()));
                    p.push_back (value ("Depth/Width", f (&FxMultiChannel::modDepth), 0, 100, num()));
                    p.push_back (value ("Init Speed", f (&FxMultiChannel::rotInitSpeed), 0, 1, list ({ "SPEED 1", "SPEED 2" })));
                }
                else if (t == 5)
                {
                    p.push_back (value ("FMod Rate", f (&FxMultiChannel::modRate), 0, 100, num()));
                    p.push_back (value ("FMod Depth", f (&FxMultiChannel::modDepth), 0, 100, num()));
                    p.push_back (value ("FMod Feedback", f (&FxMultiChannel::modFeedback), -100, 100, signed3()));
                    p.push_back (value ("AMod Rate", f (&FxMultiChannel::amodRate), 0, 100, num()));
                    p.push_back (value ("AMod Depth", f (&FxMultiChannel::amodDepth), 0, 100, num()));
                }
                else if (t >= 6)
                {
                    p.push_back (value ("Left Semitone", f (&FxMultiChannel::pitchLSemi), -24, 24, signed2()));
                    p.push_back (value ("Left Fine", f (&FxMultiChannel::pitchLFine), -50, 50, signed2()));
                    p.push_back (value ("Right Semitone", f (&FxMultiChannel::pitchRSemi), -24, 24, signed2()));
                    p.push_back (value ("Right Fine", f (&FxMultiChannel::pitchRFine), -50, 50, signed2()));
                    if (t == 7) p.push_back (value ("Feedback", f (&FxMultiChannel::modFeedback), 0, 100, num()));
                }
                break;
            }
            case 3:
                p.push_back (value ("Delay Type", f (&FxMultiChannel::delayType), 0, 4, [] (int v) { return names::delayType (v); }));
                p.push_back (value ("Left Delay", f (&FxMultiChannel::delayL), 1, 1950, suffix ("ms")));
                p.push_back (value ("Left Feedback", f (&FxMultiChannel::fbL), 0, 100, num()));
                p.push_back (value ("Right Delay", f (&FxMultiChannel::delayR), 1, 1950, suffix ("ms")));
                p.push_back (value ("Right Feedback", f (&FxMultiChannel::fbR), 0, 100, num()));
                p.push_back (value ("HF Damping", f (&FxMultiChannel::delayHfDamp), 0, 100, num()));
                p.push_back (check ("PING PONG", f (&FxMultiChannel::pingPong)));
                break;
            case 4:
                p.push_back (check ("REVERB ACTIVE", f (&FxMultiChannel::revOn)));
                p.push_back (value ("Reverb Type", f (&FxMultiChannel::revType), 0, 6, [] (int v) { return names::reverbType (v); }));
                p.push_back (value ("Pre-Delay", f (&FxMultiChannel::preDelay), 0, 300, suffix ("ms")));
                p.push_back (value ("Time", f (&FxMultiChannel::revTime), 0, 100, num()));
                p.push_back (value ("Diffusion", f (&FxMultiChannel::diffusion), 0, 100, num()));
                p.push_back (value ("HF Damping", f (&FxMultiChannel::revHfDamp), 0, 100, num()));
                p.push_back (value ("LF Damping", f (&FxMultiChannel::revLfDamp), 0, 100, num()));
                p.push_back (value ("Reverb Level", f (&FxMultiChannel::revLevel), 0, 100, num()));
                p.push_back (value ("Reverb Pan", f (&FxMultiChannel::revPan), -50, 50, pan()));
                break;
            default:
                p.push_back (value ("Dist/EQ Level", f (&FxMultiChannel::distEqLevel), 0, 100, num()));
                p.push_back (value ("Mod/Del Level", f (&FxMultiChannel::modDelLevel), 0, 100, num()));
                p.push_back (value ("Mod/Del Pan", f (&FxMultiChannel::modDelPan), -50, 50, pan()));
                p.push_back (value ("Mod/Del Width", f (&FxMultiChannel::modDelWidth), 0, 100, num()));
                p.push_back (check ("DIRECT SIGNAL", f (&FxMultiChannel::directSignal)));
                p.push_back (value ("Path Control", f (&FxMultiChannel::path), 0, 2, list ({ "MOD>REV", "MOD||REV", "REV>MOD" })));
                break;
        }
        return p;
    }

    void templates()
    {
        auto* c = C();
        if (c == nullptr) return;
        std::vector<MenuPopup::Item> it;
        auto add = [&it] (const juce::String& n, std::function<void()> f) { it.push_back ({ n, std::move (f), -1 }); };
        switch (section)
        {
            case 1:
                add ("FLAT", [c] { c->eqOn = 1; c->lfGain = c->lmidGain = c->hmidGain = c->hfGain = 0; });
                add ("BASS BOOST", [c] { c->eqOn = 1; c->lowFreq = 120; c->lfGain = 8; c->lmidGain = 0; c->hmidGain = 0; c->hfGain = 0; });
                add ("TREBLE BOOST", [c] { c->eqOn = 1; c->highFreq = 6000; c->hfGain = 8; c->lfGain = 0; });
                add ("SMILE", [c] { c->eqOn = 1; c->lfGain = 6; c->lmidGain = -3; c->hmidGain = -2; c->hfGain = 6; });
                add ("TELEPHONE", [c] { c->eqOn = 1; c->lfGain = -18; c->lmidFreq = 1200; c->lmidGain = 10; c->hfGain = -18; });
                add ("MID CUT", [c] { c->eqOn = 1; c->lmidFreq = 600; c->lmidGain = -9; c->hmidGain = 0; });
                break;
            case 2:
                add ("CHORUS SLOW", [c] { c->modType = 1; c->modRate = 15; c->modDepth = 40; c->modFeedback = 0; });
                add ("CHORUS FAST", [c] { c->modType = 1; c->modRate = 45; c->modDepth = 30; c->modFeedback = 10; });
                add ("JET FLANGE", [c] { c->modType = 2; c->modRate = 12; c->modDepth = 80; c->modFeedback = 70; });
                add ("PHASER", [c] { c->modType = 3; c->modRate = 20; c->modDepth = 70; c->modFeedback = 40; });
                add ("ROTARY", [c] { c->modType = 4; c->rotSpeed1 = 15; c->rotSpeed2 = 70; c->modDepth = 60; });
                add ("OCTAVES", [c] { c->modType = 6; c->pitchLSemi = -12; c->pitchRSemi = 12; c->pitchLFine = c->pitchRFine = 0; });
                add ("DETUNE", [c] { c->modType = 6; c->pitchLSemi = 0; c->pitchRSemi = 0; c->pitchLFine = -8; c->pitchRFine = 8; });
                break;
            case 3:
                add ("SLAP", [c] { c->delayType = 4; c->delayL = 90; c->delayR = 110; c->fbL = c->fbR = 5; });
                add ("1/4 @120BPM", [c] { c->delayType = 2; c->delayL = c->delayR = 500; c->fbL = c->fbR = 35; });
                add ("1/8. @120BPM", [c] { c->delayType = 2; c->delayL = 375; c->delayR = 375; c->fbL = c->fbR = 40; });
                add ("PING PONG", [c] { c->delayType = 3; c->delayL = 250; c->delayR = 500; c->fbL = c->fbR = 45; });
                add ("LONG ECHO", [c] { c->delayType = 1; c->delayL = 800; c->fbL = 60; c->delayHfDamp = 60; });
                break;
            case 4:
                for (int t = 0; t < 7; ++t) add (names::reverbType (t), [c, t] { c->revOn = 1; c->revType = t; });
                break;
            default:
                add ("INIT CHANNEL", [this] { if (auto* x = C()) *x = FxMultiChannel(); });
                break;
        }
        ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "EFFECTS TOOLS", it));
    }
};

//==============================================================================
class FxReverbPage : public Page
{
public:
    FxReverbPage (Ctx& c, int index) : Page (c, index == 0 ? PageId::FxRv3 : PageId::FxRv4, PageId::Fx, Mode::Fx), idx (index) {}
    juce::String title() const override { return idx == 0 ? "REVERB 3" : "REVERB 4"; }
    bool wantsTicks() const override { return true; }
    FxReverbChannel* R() const { auto* m = ctx.multi(); return m ? &m->fx.rv[idx] : nullptr; }
    kb::IntRef f (int FxReverbChannel::* m) const { return [this, m]() -> int* { auto* r = R(); return r ? &(r->*m) : nullptr; }; }

    void build (Keys& k) override
    {
        k[0] = multiHeader (ctx);
        k[1] = value (idx == 0 ? "RV3 Input" : "RV4 Input", f (&FxReverbChannel::input), 0, 2, list ({ "SEND", "FX1 OUT", "FX2 OUT" }));
        k[2] = value ("Input Level", f (&FxReverbChannel::inputLevel), 0, 100, num());
        k[3] = check ("REVERB ACTIVE", f (&FxReverbChannel::on));
        k[4] = value ("Reverb Level", f (&FxReverbChannel::level), 0, 100, num());
        k[5] = value ("Reverb Pan", f (&FxReverbChannel::pan), -50, 50, pan());
        k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::Fx); });
        k[9] = value ("Reverb Type", f (&FxReverbChannel::type), 0, 6, [] (int v) { return names::reverbType (v); });
        k[10] = value ("Pre-Delay", f (&FxReverbChannel::preDelay), 0, 300, suffix ("ms"));
        k[11] = value ("Time", f (&FxReverbChannel::time), 0, 100, num());
        k[12] = value ("Diffusion", f (&FxReverbChannel::diffusion), 0, 100, num());
        k[13] = value ("HF Damping", f (&FxReverbChannel::hfDamp), 0, 100, num());
        k[14] = value ("LF Damping", f (&FxReverbChannel::lfDamp), 0, 100, num());
        k[15] = button (idx == 0 ? "RV4" : "RV3", [this] { ctx.pages->show (idx == 0 ? PageId::FxRv4 : PageId::FxRv3); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* rv = R();
        if (rv == nullptr) return;
        r.removeFromTop (8.0f);
        oled::text (g, (rv->input == 0 ? juce::String ("SEND") : "FX" + juce::String (rv->input) + " OUT") + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 RV")) + juce::String (3 + idx),
                    r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true);
        auto gr = r.removeFromTop (70.0f).reduced (8.0f, 6.0f);
        juce::Path p;
        p.startNewSubPath (gr.getX(), gr.getBottom());
        const float pre = gr.getWidth() * 0.15f * (float) rv->preDelay / 300.0f;
        p.lineTo (gr.getX() + pre, gr.getBottom());
        p.lineTo (gr.getX() + pre, gr.getY());
        p.quadraticTo (gr.getX() + pre + gr.getWidth() * 0.1f, gr.getBottom() - 4.0f, gr.getX() + pre + gr.getWidth() * (0.2f + 0.65f * (float) rv->time / 100.0f), gr.getBottom());
        g.setColour (colours::oledText);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        oled::text (g, names::reverbType (rv->type), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
        oled::meter (g, r.removeFromTop (12.0f).reduced (10.0f, 0), ctx.proc.effects.getChannelLevel (2 + idx));
    }
private:
    int idx;
};

void registerFxPages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<FxMainPage> (c));
    pm.add (std::make_unique<FxEditPage> (c, 0));
    pm.add (std::make_unique<FxEditPage> (c, 1));
    pm.add (std::make_unique<FxReverbPage> (c, 0));
    pm.add (std::make_unique<FxReverbPage> (c, 1));
}

} // namespace s6k::ui
