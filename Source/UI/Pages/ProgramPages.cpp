/*
  ==============================================================================
   S6000 Emulation - EDIT PROGRAM / EDIT PART (manual p.80-140)
     MAIN, OUTPUT, MIDI/TUNE, EDIT USER, PITCHBEND, LFO 1, LFO 2, KEYGROUP,
     ZONES (4 velocity layers), KEYSPAN, KG PITCH/AMP, FILTER (26 types),
     AMP ENVELOPE, FILTER ENVELOPE, AUX ENVELOPE, PROG TOOLS, CREATE PROG
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;
using fmt::ProgramData;
using fmt::KeygroupData;
using fmt::EnvData;
using fmt::LfoData;
using fmt::ZoneData;

//==============================================================================
void common::createProgramFromSample (Ctx& c, Sample* s)
{
    auto p = std::make_unique<PromptPopup> (c, "CREATE PROGRAM", s != nullptr ? "New program using\n" + s->name : juce::String ("New empty program"));
    p->checks = { { "ALL SAMPLES", false, 5 }, { "BY ROOT NOTE", true, 6 } };
    p->onExecute = [&c] (PromptPopup& pp)
    {
        const bool all = pp.isChecked (0), byRoot = pp.isChecked (1);
        c.structural ([&]
        {
            Program::Ptr prg = new Program();
            auto* cur = c.sample();
            prg->setName (c.mem.uniqueProgramName (cur != nullptr && ! all ? cur->name : juce::String ("NEW PROGRAM")));
            if (! all)
            {
                KeygroupData kg;
                if (cur != nullptr) kg.zones[0].sampleName = cur->name.toStdString();
                prg->data.keygroups.push_back (kg);
            }
            else
            {
                std::vector<Sample*> list;
                for (auto* s : c.mem.samples) list.push_back (s);
                if (byRoot) std::stable_sort (list.begin(), list.end(), [] (Sample* a, Sample* b) { return a->rootNote < b->rootNote; });
                int nextNote = 36;
                for (size_t i = 0; i < list.size() && i < 99; ++i)
                {
                    KeygroupData kg;
                    kg.zones[0].sampleName = list[i]->name.toStdString();
                    if (byRoot)
                    {
                        kg.lowNote = i == 0 ? 21 : (list[i - 1]->rootNote + list[i]->rootNote) / 2 + 1;
                        kg.highNote = i + 1 == list.size() ? 127 : (list[i]->rootNote + list[i + 1]->rootNote) / 2;
                        if (kg.highNote < kg.lowNote) kg.highNote = kg.lowNote;
                    }
                    else
                    {
                        kg.lowNote = kg.highNote = juce::jmin (127, nextNote++);
                        kg.zones[0].keyTrack = false;
                        kg.zones[0].playback = fmt::PlayOneShot;
                    }
                    prg->data.keygroups.push_back (kg);
                }
                if (prg->data.keygroups.empty()) prg->data.keygroups.push_back (KeygroupData());
            }
            c.mem.programs.add (prg);
            c.curProgram = c.mem.programs.size() - 1;
            c.curKeygroup = 0;
        });
        c.pages->show (PageId::ProgMain);
    };
    c.pages->pushPopup (std::move (p));
}

void common::showProgramTools (Ctx& c)
{
    std::vector<MenuPopup::Item> it;
    it.push_back ({ "GET INFO", [&c]
    {
        auto* p = c.program();
        if (p == nullptr) { c.info ("GET INFO", { "No program selected" }); return; }
        int zones = 0, missing = 0;
        for (size_t k = 0; k < p->data.keygroups.size(); ++k)
            for (int z = 0; z < 4; ++z)
                if (! p->data.keygroups[k].zones[z].sampleName.empty())
                {
                    ++zones;
                    if (k >= p->resolved.size() || p->resolved[k][(size_t) z] == nullptr) ++missing;
                }
        c.info ("GET INFO", { "PROGRAM: " + p->getName(), juce::String (p->numKeygroups()) + " KEYGROUPS, " + juce::String (zones) + " ZONES",
                              missing > 0 ? juce::String (missing) + " SAMPLES MISSING" : juce::String ("ALL SAMPLES IN MEMORY"),
                              "MIDI PROGRAM: " + (p->data.midiProgram > 0 ? juce::String (p->data.midiProgram) : juce::String ("OFF")) });
    }, 9 });
    it.push_back ({ "NEW KEYGROUP", [&c]
    {
        auto* p = c.program();
        if (p == nullptr || p->numKeygroups() >= 99) return;
        c.structural ([&c, p]
        {
            KeygroupData kg = c.keygroup() != nullptr ? *c.keygroup() : KeygroupData();
            p->data.keygroups.insert (p->data.keygroups.begin() + c.curKeygroup + 1, kg);
            ++c.curKeygroup;
        });
    }, 10 });
    it.push_back ({ "PROGRAM LIST", [&c] { c.pages->show (PageId::ProgramList); }, 11 });
    it.push_back ({ "QUICKLOAD", [&c] { quickLoad (c); }, 12 });
    it.push_back ({ "QUICKSAVE", [&c] { quickSave (c); }, 13 });
    it.push_back ({ "DELETE KEYGROUP", [&c]
    {
        auto* p = c.program();
        if (p == nullptr || p->numKeygroups() <= 1) { c.info ("DELETE KEYGROUP", { "A program needs at", "least one keygroup" }); return; }
        c.structural ([&c, p] { p->data.keygroups.erase (p->data.keygroups.begin() + c.curKeygroup); });
    }, 14 });
    c.pages->pushPopup (std::make_unique<MenuPopup> (c, "PROG TOOLS", it));
}

//==============================================================================
class ProgramPage : public Page
{
public:
    ProgramPage (Ctx& c, PageId i, PageId parentPage, juce::String t) : Page (c, i, parentPage, Mode::Program), pageTitle (std::move (t)) {}
    juce::String title() const override { return pageTitle; }

    ProgramData* P() const { auto* p = ctx.program(); return p != nullptr ? &p->data : nullptr; }
    KeygroupData* KG() const { return ctx.keygroup(); }

    kb::IntRef pr (int ProgramData::* m) const { return [this, m]() -> int* { auto* p = P(); return p ? &(p->*m) : nullptr; }; }
    kb::IntRef kg (int KeygroupData::* m) const { return [this, m]() -> int* { auto* k = KG(); return k ? &(k->*m) : nullptr; }; }
    kb::IntRef env (EnvData KeygroupData::* e, int EnvData::* m) const { return [this, e, m]() -> int* { auto* k = KG(); return k ? &((k->*e).*m) : nullptr; }; }
    kb::IntRef lfo (LfoData ProgramData::* l, int LfoData::* m) const { return [this, l, m]() -> int* { auto* p = P(); return p ? &((p->*l).*m) : nullptr; }; }
    kb::IntRef src (int index) const { return [this, index]() -> int* { auto* p = P(); return p ? &p->modSource[index] : nullptr; }; }
    kb::IntRef aux (int which, bool level) const
    {
        return [this, which, level]() -> int* { auto* k = KG(); return k ? (level ? &k->aux.level[which] : &k->aux.rate[which]) : nullptr; };
    }

    void commonKeys (Keys& k, bool withMain = true, bool keygroupSelector = false)
    {
        k[0] = programHeader (ctx);
        if (withMain) k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::ProgMain); });
        if (keygroupSelector) k[8] = keygroupSelect (ctx);
        else k[8] = button ("PROG TOOLS", [this] { showProgramTools (ctx); });
    }

    bool noProgram (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        if (P() != nullptr) return false;
        oled::text (g, "NO PROGRAMS", r.withHeight (16).translated (0, r.getHeight() * 0.3f), juce::Justification::centred, 11.0f, true);
        oled::text (g, "IN MEMORY", r.withHeight (16).translated (0, r.getHeight() * 0.3f + 16), juce::Justification::centred, 11.0f, true);
        return true;
    }

    void zoneList (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        auto* k = KG();
        if (k == nullptr) return;
        auto box = r.removeFromTop (96.0f);
        oled::box (g, box);
        auto head = box.removeFromTop (18.0f);
        oled::text (g, "Zone sample", head, juce::Justification::centred, 10.0f, true);
        for (int z = 0; z < 4; ++z)
            oled::text (g, juce::String (z + 1) + "." + juce::String (k->zones[z].sampleName), box.removeFromTop (geo::lineH).reduced (4, 0),
                        juce::Justification::centredLeft, 10.5f);
    }

    juce::String pageTitle;
};

//==============================================================================
class ProgMainPage : public ProgramPage
{
public:
    explicit ProgMainPage (Ctx& c) : ProgramPage (c, PageId::ProgMain, PageId::None, "EDIT\nPROGRAM") {}
    juce::String title() const override { return ctx.editPartMode ? "EDIT\nPART" : "EDIT\nPROGRAM"; }
    bool exitPressed() override { if (ctx.editPartMode) { ctx.pages->show (PageId::Multi); return true; } return false; }

    void build (Keys& k) override
    {
        commonKeys (k, false);
        const bool ok = P() != nullptr;
        k[1] = button ("OUTPUT", [this] { ctx.pages->show (PageId::ProgOutput); }, ok);
        k[2] = button ("MIDI/TUNE", [this] { ctx.pages->show (PageId::ProgMidiTune); }, ok);
        k[3] = button ("PITCH BEND", [this] { ctx.pages->show (PageId::ProgPitchBend); }, ok);
        k[4] = button ("LFO's", [this] { ctx.pages->show (PageId::ProgLfo1); }, ok);
        k[5] = button ("KEYGROUP", [this] { ctx.pages->show (PageId::ProgKeygroup); }, ok);
        k[6] = button ("KEYSPAN", [this] { ctx.pages->show (PageId::ProgKeyspan); }, ok);
        if (ctx.editPartMode)
        {
            k[7] = button ("MULTI", [this] { ctx.pages->show (PageId::Multi); });
            k[8] = button ("SOLO PART", [this] { if (auto* m = ctx.multi()) m->solo[(size_t) ctx.curPart] = ! m->solo[(size_t) ctx.curPart]; });
            if (auto* m = ctx.multi()) k[8].highlight = m->solo[(size_t) ctx.curPart];
        }
        k[9] = button ("KG PITCH/AMP", [this] { ctx.pages->show (PageId::ProgKgPitchAmp); }, ok);
        k[10] = button ("FILTER", [this] { ctx.pages->show (PageId::ProgFilter); }, ok);
        k[11] = button ("AMP ENVELOPE", [this] { ctx.pages->show (PageId::ProgAmpEnv); }, ok);
        k[12] = button ("FILT ENVELOPE", [this] { ctx.pages->show (PageId::ProgFiltEnv); }, ok);
        k[13] = button ("AUX ENVELOPE", [this] { ctx.pages->show (PageId::ProgAuxEnv); }, ok);
        k[15] = button ("CREATE PROG", [this] { createProgramFromSample (ctx, ctx.sample()); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        oled::memoryBox (g, r.withTrimmedBottom (40.0f), ctx);
    }
};

//==============================================================================
class ProgOutputPage : public ProgramPage
{
public:
    explicit ProgOutputPage (Ctx& c) : ProgramPage (c, PageId::ProgOutput, PageId::ProgMain, "OUTPUT") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = value ("Loudness", pr (&ProgramData::loudness), 0, 100, num());
        k[2] = value ("Velocity Sens", pr (&ProgramData::velSens), -100, 100, signed3());
        k[3] = value ("Polyphony", pr (&ProgramData::polyphony), 1, 128, suffix (" VOICES"));
        k[9]  = modInput (*this, 9,  "Amp Mod 1", pr (&ProgramData::ampMod1), src (ProgramData::AmpMod1), 0, 100);
        k[10] = modInput (*this, 10, "Amp Mod 2", pr (&ProgramData::ampMod2), src (ProgramData::AmpMod2), 0, 100);
        k[11] = modInput (*this, 11, "Pan Mod 1", pr (&ProgramData::panMod1), src (ProgramData::PanMod1), -50, 50);
        k[12] = modInput (*this, 12, "Pan Mod 2", pr (&ProgramData::panMod2), src (ProgramData::PanMod2), -50, 50);
        k[13] = modInput (*this, 13, "Pan Mod 3", pr (&ProgramData::panMod3), src (ProgramData::PanMod3), -50, 50);
        k[15] = button ("GOTO SOURCE", [this] { gotoSource (ctx, *this); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (! noProgram (g, r)) oled::memoryBox (g, r.withTrimmedBottom (40.0f), ctx);
    }
};

//==============================================================================
class ProgMidiTunePage : public ProgramPage
{
public:
    explicit ProgMidiTunePage (Ctx& c) : ProgramPage (c, PageId::ProgMidiTune, PageId::ProgMain, "MIDI/TUNE") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = value ("Program Number", pr (&ProgramData::midiProgram), 0, 128, [] (int v) { return v == 0 ? juce::String ("OFF") : juce::String (v).paddedLeft ('0', 3); });
        k[2] = value ("Semitone Tune", pr (&ProgramData::semi), -36, 36, signed2());
        k[3] = value ("Fine Tune", pr (&ProgramData::fine), -50, 50, signed2());
        k[4] = value ("Tune Template", pr (&ProgramData::tuneTemplate), 0, names::numTuneTemplates() - 1, [] (int v) { return names::tuneTemplate (v); });
        k[5] = value ("Key", pr (&ProgramData::key), 0, 11, list ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }));
        k[6] = button ("COPY TO USER", [this]
        {
            auto* p = P();
            if (p == nullptr) return;
            for (int i = 0; i < 12; ++i) p->userTune[i] = (int) std::round (tuningOffsetCents (p->tuneTemplate, p->key, i, p->userTune));
            ctx.info ("COPY TO USER", { names::tuneTemplate (p->tuneTemplate), "copied to USER template" });
        });
        k[15] = button ("EDIT USER", [this] { ctx.pages->show (PageId::ProgEditUser); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* p = P();
        auto kbR = r.removeFromTop (60.0f).reduced (4.0f, 6.0f);
        oled::keyboard (g, kbR, 60, 71);
        // tuning offsets
        auto bars = r.removeFromTop (70.0f).reduced (4.0f, 4.0f);
        g.setColour (colours::oledGrid);
        g.drawHorizontalLine ((int) bars.getCentreY(), bars.getX(), bars.getRight());
        const float w = bars.getWidth() / 12.0f;
        g.setColour (colours::oledText);
        for (int i = 0; i < 12; ++i)
        {
            const float c = tuningOffsetCents (p->tuneTemplate, p->key, i, p->userTune);
            const float h = c / 50.0f * bars.getHeight() * 0.5f;
            g.fillRect (juce::Rectangle<float> (bars.getX() + w * (float) i + 2.0f, bars.getCentreY() - juce::jmax (0.0f, h), w - 4.0f, std::abs (h) + 1.0f));
        }
        oled::text (g, names::tuneTemplate (p->tuneTemplate), r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
    }
};

class ProgEditUserPage : public ProgramPage
{
public:
    explicit ProgEditUserPage (Ctx& c) : ProgramPage (c, PageId::ProgEditUser, PageId::ProgMidiTune, "EDIT USER") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const int keyIdx[] = { 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14 };
        for (int i = 0; i < 12; ++i)
            k[(size_t) keyIdx[i]] = value (juce::String (nn[i]) + " cents", [this, i]() -> int* { auto* p = P(); return p ? &p->userTune[i] : nullptr; }, -50, 50, signed2());
        k[15] = button ("MIDI/TUNE", [this] { ctx.pages->show (PageId::ProgMidiTune); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        oled::keyboard (g, r.removeFromTop (60.0f).reduced (4.0f, 6.0f), 60, 71);
        oled::text (g, "USER TEMPLATE", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
        oled::text (g, "Select USER as Tune", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
        oled::text (g, "Template to use it", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
    }
};

//==============================================================================
class ProgPitchBendPage : public ProgramPage
{
public:
    explicit ProgPitchBendPage (Ctx& c) : ProgramPage (c, PageId::ProgPitchBend, PageId::ProgMain, "PITCHBEND") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = value ("Pitchbend Up", pr (&ProgramData::bendUp), 0, 24, suffix (" SEMI"));
        k[2] = value ("Pitchbend Down", pr (&ProgramData::bendDown), 0, 24, suffix (" SEMI"));
        k[3] = value ("Bend Mode", pr (&ProgramData::bendMode), 0, 1, list ({ "NORMAL", "HELD" }));
        k[4] = value ("Aftertouch", pr (&ProgramData::aftertouchBend), -12, 12, signed2());
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* p = P();
        auto gr = r.removeFromTop (120.0f).reduced (10.0f);
        g.setColour (colours::oledGrid);
        g.drawRect (gr, 1.0f);
        g.drawHorizontalLine ((int) gr.getCentreY(), gr.getX(), gr.getRight());
        g.setColour (colours::oledText);
        const float up = (float) p->bendUp / 24.0f, dn = (float) p->bendDown / 24.0f;
        g.drawLine (gr.getCentreX(), gr.getCentreY(), gr.getRight(), gr.getCentreY() - up * gr.getHeight() * 0.5f, 1.5f);
        g.drawLine (gr.getCentreX(), gr.getCentreY(), gr.getX(), gr.getCentreY() + dn * gr.getHeight() * 0.5f, 1.5f);
        oled::text (g, "+" + juce::String (p->bendUp) + " / -" + juce::String (p->bendDown), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
    }
};

//==============================================================================
class ProgLfoPage : public ProgramPage
{
public:
    ProgLfoPage (Ctx& c, bool second) : ProgramPage (c, second ? PageId::ProgLfo2 : PageId::ProgLfo1, PageId::ProgMain, second ? "LFO 2" : "LFO 1"), lfo2 (second) {}
    void build (Keys& k) override
    {
        commonKeys (k);
        auto l = lfo2 ? &ProgramData::lfo2 : &ProgramData::lfo1;
        k[1] = value ("Rate", lfo (l, &LfoData::rate), 0, 100, num());
        k[2] = value ("Delay", lfo (l, &LfoData::delay), 0, 100, num());
        k[3] = value ("Depth", lfo (l, &LfoData::depth), 0, 100, num());
        k[4] = value ("Waveform", lfo (l, &LfoData::waveform), 0, 8, [] (int v) { return names::lfoWave (v); });
        k[5] = value (lfo2 ? "LFO Re-trigger" : "LFO Sync", lfo (l, &LfoData::syncOrRetrigger), 0, 1, onOff());
        k[6] = button (lfo2 ? "LFO 1" : "LFO 2", [this] { ctx.pages->show (lfo2 ? PageId::ProgLfo1 : PageId::ProgLfo2); });
        const int base = lfo2 ? ProgramData::Lfo2Rate : ProgramData::Lfo1Rate;
        k[9]  = modInput (*this, 9,  "Rate Mod",  lfo (l, &LfoData::rateMod),  src (base));
        k[10] = modInput (*this, 10, "Delay Mod", lfo (l, &LfoData::delayMod), src (base + 1));
        k[11] = modInput (*this, 11, "Depth Mod", lfo (l, &LfoData::depthMod), src (base + 2));
        k[12] = value ("Modwheel", lfo (l, &LfoData::modwheel), 0, 100, num());
        k[13] = value ("Aftertouch", lfo (l, &LfoData::aftertouch), 0, 100, num());
        k[14] = button ("GOTO DEST", [this] { gotoDest (ctx, lfo2 ? 10 : 9); });
        k[15] = button ("GOTO SOURCE", [this] { gotoSource (ctx, *this); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* p = P();
        const auto& L = lfo2 ? p->lfo2 : p->lfo1;
        auto gr = r.removeFromTop (110.0f).reduced (10.0f, 14.0f);
        g.setColour (colours::oledGrid);
        g.drawHorizontalLine ((int) gr.getCentreY(), gr.getX(), gr.getRight());
        juce::Path path;
        float rnd = 0.3f;
        for (int x = 0; x <= (int) gr.getWidth(); ++x)
        {
            const float ph = std::fmod ((float) x / gr.getWidth() * 2.0f, 1.0f);
            if (x > 0 && ph < 0.01f) rnd = -rnd * 0.7f + 0.2f;
            const float v = dsp::lfoWave (L.waveform, ph, rnd);
            const float y = gr.getCentreY() - v * gr.getHeight() * 0.5f;
            if (x == 0) path.startNewSubPath (gr.getX(), y); else path.lineTo (gr.getX() + (float) x, y);
        }
        g.setColour (colours::oledText);
        g.strokePath (path, juce::PathStrokeType (1.4f));
        oled::text (g, names::lfoWave (L.waveform) + "  " + juce::String (dsp::lfoRateHz ((float) L.rate), 2) + "Hz", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
    }
private:
    bool lfo2;
};

//==============================================================================
class ProgKeygroupPage : public ProgramPage
{
public:
    explicit ProgKeygroupPage (Ctx& c) : ProgramPage (c, PageId::ProgKeygroup, PageId::ProgMain, "KEYGROUP") {}
    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        k[1] = value ("Low Note", kg (&KeygroupData::lowNote), 0, 127, note (ctx), [this] { if (auto* x = KG()) x->highNote = juce::jmax (x->highNote, x->lowNote); });
        k[2] = value ("High Note", kg (&KeygroupData::highNote), 0, 127, note (ctx), [this] { if (auto* x = KG()) x->lowNote = juce::jmin (x->highNote, x->lowNote); });
        k[3] = value ("Mute Group", kg (&KeygroupData::muteGroup), 0, 32, [] (int v) { return v == 0 ? juce::String ("OFF") : juce::String (v); });
        k[4] = value ("Overide FX", kg (&KeygroupData::fxOverride), 0, 4, [] (int v) { return v == 0 ? juce::String ("OFF") : names::fxChannel (v); });
        k[5] = value ("FX Send Level", kg (&KeygroupData::fxSend), 0, 100, num());
        k[6] = button ("ZONES", [this] { ctx.pages->show (PageId::ProgZones); });
        k[9] = value ("Semitone Tune", kg (&KeygroupData::semi), -36, 36, signed2());
        k[10] = value ("Fine Tune", kg (&KeygroupData::fine), -50, 50, signed2());
        k[11] = button ("KEYSPAN", [this] { ctx.pages->show (PageId::ProgKeyspan); });
        k[12] = button ("NEW KEYGROUP", [this]
        {
            auto* p = ctx.program();
            if (p == nullptr || p->numKeygroups() >= 99) return;
            ctx.structural ([this, p]
            {
                KeygroupData copy = KG() != nullptr ? *KG() : KeygroupData();
                p->data.keygroups.insert (p->data.keygroups.begin() + ctx.curKeygroup + 1, copy);
                ++ctx.curKeygroup;
            });
        });
        k[13] = button ("DELETE KEYGROUP", [this]
        {
            auto* p = ctx.program();
            if (p == nullptr || p->numKeygroups() <= 1) return;
            ctx.confirm ("DELETE KEYGROUP", "Delete keygroup " + juce::String (ctx.curKeygroup + 1) + "?", [this]
            {
                if (auto* pp = ctx.program()) ctx.structural ([this, pp] { pp->data.keygroups.erase (pp->data.keygroups.begin() + ctx.curKeygroup); });
            });
        });
        k[14] = button ("PROG TOOLS", [this] { showProgramTools (ctx); });
        k[15] = button ("KG PITCH/AMP", [this] { ctx.pages->show (PageId::ProgKgPitchAmp); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        zoneList (g, r);
        r.removeFromTop (102.0f);
        if (auto* k = KG()) oled::text (g, ctx.note (k->lowNote) + " - " + ctx.note (k->highNote), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true);
        oled::text (g, juce::String (ctx.program()->numKeygroups()) + " Keygroups in prog", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
    }
};

//==============================================================================
class ProgZonesPage : public ProgramPage
{
public:
    explicit ProgZonesPage (Ctx& c) : ProgramPage (c, PageId::ProgZones, PageId::ProgKeygroup, "ZONES") {}

    ZoneData* Z() const { auto* k = KG(); return k ? &k->zones[juce::jlimit (0, 3, ctx.curZone)] : nullptr; }
    kb::IntRef zr (int ZoneData::* m) const { return [this, m]() -> int* { auto* z = Z(); return z ? &(z->*m) : nullptr; }; }

    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        for (int z = 0; z < 4; ++z)
        {
            auto key = valueFn ("Zone " + juce::String (z + 1),
                                [this, z]
                                {
                                    auto* x = KG();
                                    if (x == nullptr || x->zones[z].sampleName.empty()) return 0;
                                    auto s = ctx.mem.findSample (juce::String (x->zones[z].sampleName));
                                    return s != nullptr ? ctx.mem.indexOfSample (s.get()) + 1 : 0;
                                },
                                [this, z] (int v)
                                {
                                    auto* x = KG();
                                    if (x == nullptr) return;
                                    ctx.structural ([&] { x->zones[z].sampleName = v == 0 ? std::string() : ctx.mem.samples.getObjectPointer (v - 1)->name.toStdString(); });
                                    if (v > 0) ctx.curSample = v - 1;
                                },
                                0, ctx.mem.samples.size(),
                                [this, z] (int) { auto* x = KG(); return x == nullptr || x->zones[z].sampleName.empty() ? juce::String ("--- OFF ---") : juce::String (x->zones[z].sampleName); });
            k[(size_t) (1 + z)] = key;
        }
        if (selected >= 1 && selected <= 4) ctx.curZone = selected - 1;
        k[5] = button ("KEYGROUP", [this] { ctx.pages->show (PageId::ProgKeygroup); });
        k[6] = button ("KEYSPAN", [this] { ctx.pages->show (PageId::ProgKeyspan); });

        k[9] = value ("Zone Level", zr (&ZoneData::level), -36, 12, db());
        k[10] = value ("Pan/Balance", zr (&ZoneData::pan), -50, 50, pan());
        k[11] = value ("Fine Tune", zr (&ZoneData::fine), -50, 50, signed2());
        k[11].windowA = value ("Semitone", zr (&ZoneData::semi), -36, 36, signed2()).param;
        k[11].windowB = k[11].param;
        k[11].windowTitle = "ZONE TUNE";
        k[12] = value ("Playback", zr (&ZoneData::playback), 0, 4, [] (int v) { return names::playback (v, true); });
        k[12].windowA = value ("Output", zr (&ZoneData::output), 0, 24, [] (int v) { return names::output (v, true); }).param;
        k[12].windowB = value ("Key Track", []() -> int* { return nullptr; }, 0, 1, onOff()).param;
        k[12].windowB->get = [this] { auto* z = Z(); return z && z->keyTrack ? 1 : 0; };
        k[12].windowB->set = [this] (int v) { if (auto* z = Z()) z->keyTrack = v != 0; };
        k[12].windowTitle = "PLAYBACK / OUTPUT";
        k[13] = value ("High Velocity", zr (&ZoneData::highVel), 0, 127, num(), [this] { if (auto* z = Z()) z->lowVel = juce::jmin (z->lowVel, z->highVel); });
        k[14] = value ("Low Velocity", zr (&ZoneData::lowVel), 0, 127, num(), [this] { if (auto* z = Z()) z->highVel = juce::jmax (z->lowVel, z->highVel); });
        SoftKey xf;
        xf.kind = SoftKey::Kind::Check;
        xf.title = "ZONE XFADE";
        xf.checked = [this] { auto* x = KG(); return x != nullptr && x->zoneXfade; };
        xf.onPress = [this] { if (auto* x = KG()) x->zoneXfade = ! x->zoneXfade; };
        k[15] = xf;
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* k = KG();
        if (k == nullptr) return;
        auto head = r.removeFromTop (16.0f);
        oled::text (g, "Zone Velocity", head, juce::Justification::centred, 10.0f, true);
        auto gr = r.removeFromTop (130.0f).reduced (14.0f, 4.0f);
        g.setColour (colours::oledText);
        g.drawRect (gr, 1.0f);
        const float bw = gr.getWidth() / 4.0f;
        for (int z = 0; z < 4; ++z)
        {
            const auto& zd = k->zones[z];
            if (zd.sampleName.empty()) continue;
            const float y1 = gr.getBottom() - gr.getHeight() * (float) zd.highVel / 127.0f;
            const float y2 = gr.getBottom() - gr.getHeight() * (float) zd.lowVel / 127.0f;
            auto bar = juce::Rectangle<float> (gr.getX() + bw * (float) z + 4.0f, y1, bw - 8.0f, juce::jmax (1.0f, y2 - y1));
            if (z == ctx.curZone) g.fillRect (bar); else g.drawRect (bar, 1.0f);
        }
        for (int z = 0; z < 4; ++z)
            oled::text (g, juce::String (z + 1), juce::Rectangle<float> (gr.getX() + bw * (float) z, gr.getBottom() + 1.0f, bw, 13.0f), juce::Justification::centred, 10.0f);
        oled::text (g, "127", juce::Rectangle<float> (gr.getX() - 14.0f, gr.getY() - 2.0f, 30.0f, 12.0f), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        r.removeFromTop (16.0f);
        oled::text (g, juce::String (ctx.program()->numKeygroups()) + " Keygroups in prog", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
    }
};

//==============================================================================
class ProgKeyspanPage : public ProgramPage
{
public:
    explicit ProgKeyspanPage (Ctx& c) : ProgramPage (c, PageId::ProgKeyspan, PageId::ProgMain, "KEYSPAN") {}
    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        k[1] = value ("Low Note", kg (&KeygroupData::lowNote), 0, 127, note (ctx), [this] { if (auto* x = KG()) x->highNote = juce::jmax (x->highNote, x->lowNote); });
        k[2] = value ("High Note", kg (&KeygroupData::highNote), 0, 127, note (ctx), [this] { if (auto* x = KG()) x->lowNote = juce::jmin (x->highNote, x->lowNote); });
        SoftKey xf;
        xf.kind = SoftKey::Kind::Check;
        xf.title = "KG XFADE";
        xf.checked = [this] { auto* p = P(); return p != nullptr && p->keygroupXfade; };
        xf.onPress = [this] { if (auto* p = P()) p->keygroupXfade = ! p->keygroupXfade; };
        k[3] = xf;
        k[5] = button ("KEYGROUP", [this] { ctx.pages->show (PageId::ProgKeygroup); });
        k[6] = button ("ZONES", [this] { ctx.pages->show (PageId::ProgZones); });
        k[9] = button ("SPREAD KGs", [this]
        {
            auto* p = P();
            if (p == nullptr) return;
            // spread all keygroups evenly across C0..G8 by their order
            const int n = (int) p->keygroups.size();
            for (int i = 0; i < n; ++i)
            {
                p->keygroups[(size_t) i].lowNote = 24 + (103 * i) / n;
                p->keygroups[(size_t) i].highNote = juce::jmax (p->keygroups[(size_t) i].lowNote, 24 + (103 * (i + 1)) / n - 1);
            }
            p->keygroups.back().highNote = 127;
            p->keygroups.front().lowNote = 0;
        });
        k[10] = button ("FULL RANGE", [this] { if (auto* x = KG()) { x->lowNote = 0; x->highNote = 127; } });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* p = P();
        auto* k = KG();
        auto kbr = r.removeFromTop (44.0f).reduced (2.0f, 4.0f);
        oled::keyboard (g, kbr, 24, 107, k ? k->lowNote : -1, k ? k->highNote : -1);
        auto lines = r.removeFromTop (110.0f).reduced (2.0f, 4.0f);
        const float lo = 24.0f, hi = 108.0f;
        const int n = (int) p->keygroups.size();
        const float lh = juce::jmax (2.0f, juce::jmin (8.0f, lines.getHeight() / (float) juce::jmax (1, n)));
        for (int i = 0; i < n; ++i)
        {
            const auto& kgd = p->keygroups[(size_t) i];
            const float x1 = lines.getX() + lines.getWidth() * (juce::jlimit (lo, hi, (float) kgd.lowNote) - lo) / (hi - lo);
            const float x2 = lines.getX() + lines.getWidth() * (juce::jlimit (lo, hi, (float) kgd.highNote + 1.0f) - lo) / (hi - lo);
            g.setColour (i == ctx.curKeygroup ? colours::oledText : colours::oledDim);
            g.fillRect (juce::Rectangle<float> (x1, lines.getY() + lh * (float) i, juce::jmax (1.5f, x2 - x1), lh - 1.0f));
        }
        if (k != nullptr)
            oled::text (g, "KG " + juce::String (ctx.curKeygroup + 1) + ": " + ctx.note (k->lowNote) + " - " + ctx.note (k->highNote), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true);
    }
};

//==============================================================================
class ProgKgPitchAmpPage : public ProgramPage
{
public:
    explicit ProgKgPitchAmpPage (Ctx& c) : ProgramPage (c, PageId::ProgKgPitchAmp, PageId::ProgMain, "KG PITCH/AMP") {}
    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        k[1] = value ("Semitone Tune", kg (&KeygroupData::semi), -36, 36, signed2());
        k[2] = value ("Fine Tune", kg (&KeygroupData::fine), -50, 50, signed2());
        k[3] = value ("Attenuation", kg (&KeygroupData::attenuation), 0, 5, [] (int v) { return juce::String (-6 * v) + "dB"; });
        k[9]  = modInput (*this, 9,  "Pitch Mod 1", kg (&KeygroupData::pitchMod1), src (ProgramData::PitchMod1));
        k[10] = modInput (*this, 10, "Pitch Mod 2", kg (&KeygroupData::pitchMod2), src (ProgramData::PitchMod2));
        k[11] = modInput (*this, 11, "Amp Mod", kg (&KeygroupData::ampMod), src (ProgramData::KgAmpMod));
        k[15] = button ("GOTO SOURCE", [this] { gotoSource (ctx, *this); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (! noProgram (g, r)) zoneList (g, r);
    }
};

//==============================================================================
class ProgFilterPage : public ProgramPage
{
public:
    explicit ProgFilterPage (Ctx& c) : ProgramPage (c, PageId::ProgFilter, PageId::ProgMain, "FILTER") {}
    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        k[1] = value ("Filter Mode", kg (&KeygroupData::filterMode), 0, names::numFilterTypes() - 1, [] (int v) { return names::filterType (v); });
        k[2] = value ("Cutoff Freq", kg (&KeygroupData::cutoff), 0, 100, num());
        k[3] = value ("Resonance", kg (&KeygroupData::resonance), 0, 12, num());
        k[4] = value ("Keyboard Track", kg (&KeygroupData::filterKeyTrack), -36, 36, signed2());
        k[5] = button ("KEYGROUP", [this] { ctx.pages->show (PageId::ProgKeygroup); });
        k[6] = button ("KEYSPAN", [this] { ctx.pages->show (PageId::ProgKeyspan); });
        k[9]  = modInput (*this, 9,  "Mod Input 1", kg (&KeygroupData::filterMod1), src (ProgramData::FiltMod1));
        k[10] = modInput (*this, 10, "Mod Input 2", kg (&KeygroupData::filterMod2), src (ProgramData::FiltMod2));
        k[11] = modInput (*this, 11, "Mod Input 3", kg (&KeygroupData::filterMod3), src (ProgramData::FiltMod3));
        k[12] = button ("FILT ENVELOPE", [this] { ctx.pages->show (PageId::ProgFiltEnv); });
        k[13] = value ("Filt Env Depth", env (&KeygroupData::filt, &EnvData::depth), -100, 100, signed3());
        k[14] = value ("Attenuation", kg (&KeygroupData::attenuation), 0, 5, [] (int v) { return juce::String (-6 * v) + "dB"; });
        k[15] = button ("GOTO SOURCE", [this] { gotoSource (ctx, *this); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        zoneList (g, r);
        r.removeFromTop (102.0f);
        auto* k = KG();
        if (k == nullptr) return;
        // response curve (computed from the same SVF the engine uses)
        auto gr = r.removeFromTop (r.getHeight() - 30.0f).reduced (6.0f, 4.0f);
        g.setColour (colours::oledGrid);
        g.drawRect (gr, 1.0f);
        const float fc = dsp::cutoffToHz ((float) k->cutoff);
        const float q = 0.6f + (float) k->resonance / 12.0f * 14.0f;
        juce::Path path;
        for (int x = 0; x <= (int) gr.getWidth(); x += 2)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) x / gr.getWidth());
            const float w = f / fc;
            float mag = 1.0f;
            const int t = k->filterMode;
            const float denom = std::sqrt ((1 - w * w) * (1 - w * w) + (w / q) * (w / q));
            if (t <= 2) mag = 1.0f / denom * (t == 1 ? 1.0f / denom : 1.0f);
            else if (t <= 5) mag = (w / q) / denom;
            else if (t <= 8) mag = (w * w) / denom;
            else if (t >= 12 && t <= 16) mag = std::abs (1 - w * w) / denom;
            else if (t >= 17 && t <= 21) mag = 1.0f + 2.0f * (w / q) / denom;
            else mag = 1.0f;
            const float db = juce::jlimit (-36.0f, 18.0f, juce::Decibels::gainToDecibels (mag));
            const float y = gr.getY() + gr.getHeight() * (1.0f - (db + 36.0f) / 54.0f);
            if (x == 0) path.startNewSubPath (gr.getX(), y); else path.lineTo (gr.getX() + (float) x, y);
        }
        g.setColour (colours::oledText);
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }
};

//==============================================================================
class ProgEnvPage : public ProgramPage
{
public:
    ProgEnvPage (Ctx& c, bool filterEnv)
        : ProgramPage (c, filterEnv ? PageId::ProgFiltEnv : PageId::ProgAmpEnv, PageId::ProgMain, filterEnv ? "FILTER ENV" : "AMP ENVELOPE"), isFilter (filterEnv) {}

    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        auto e = isFilter ? &KeygroupData::filt : &KeygroupData::amp;
        k[1] = value ("Attack", env (e, &EnvData::attack), 0, 100, num());
        k[2] = value ("Decay", env (e, &EnvData::decay), 0, 100, num());
        k[3] = value ("Sustain", env (e, &EnvData::sustain), 0, 100, num());
        k[4] = value ("Release", env (e, &EnvData::release), 0, 100, num());
        k[5] = value ("Keyscale", env (e, &EnvData::keyscale), -100, 100, signed3());
        k[6] = valueFn ("Env Template", [this] { return tmpl; }, [this] (int v) { tmpl = v; applyTemplate(); }, 0, 6,
                        list ({ "-", "ORGAN", "PIANO", "STRINGS", "PAD", "PLUCK", "PERC" }));
        k[9] = value ("Velo>Attack", env (e, &EnvData::velAttack), -100, 100, signed3());
        k[10] = value ("On Vel>Rel", env (e, &EnvData::onVelRelease), -100, 100, signed3());
        k[11] = value ("Off Vel>Rel", env (e, &EnvData::offVelRelease), -100, 100, signed3());
        if (isFilter)
        {
            k[12] = value ("Env Depth", env (e, &EnvData::depth), -100, 100, signed3());
            k[13] = button ("FILTER", [this] { ctx.pages->show (PageId::ProgFilter); });
        }
        k[15] = button ("GOTO DEST", [this] { gotoDest (ctx, isFilter ? 12 : 11); });
        k[1].windowA = k[1].param; k[1].windowB = k[4].param; k[1].windowTitle = "ATTACK / RELEASE";
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* k = KG();
        if (k == nullptr) return;
        const auto& e = isFilter ? k->filt : k->amp;
        oled::envelopeAdsr (g, r.removeFromTop (120.0f).reduced (8.0f, 10.0f), e.attack, e.decay, e.sustain, e.release);
        oled::text (g, "A " + juce::String (dsp::envTimeMs ((float) e.attack), 0) + "ms  R " + juce::String (dsp::envTimeMs ((float) e.release), 0) + "ms",
                    r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
    }

private:
    bool isFilter;
    int tmpl = 0;
    void applyTemplate()
    {
        auto* k = KG();
        if (k == nullptr || tmpl == 0) return;
        auto& e = isFilter ? k->filt : k->amp;
        static const int t[7][4] = { { 0, 50, 100, 15 }, { 0, 0, 100, 8 }, { 0, 70, 0, 40 }, { 45, 50, 90, 55 }, { 65, 60, 85, 70 }, { 0, 45, 0, 30 }, { 0, 25, 0, 20 } };
        e.attack = t[tmpl][0]; e.decay = t[tmpl][1]; e.sustain = t[tmpl][2]; e.release = t[tmpl][3];
    }
};

//==============================================================================
class ProgAuxEnvPage : public ProgramPage
{
public:
    explicit ProgAuxEnvPage (Ctx& c) : ProgramPage (c, PageId::ProgAuxEnv, PageId::ProgMain, "AUX ENVELOPE") {}
    void build (Keys& k) override
    {
        commonKeys (k, true, true);
        for (int i = 0; i < 4; ++i)
        {
            k[(size_t) (1 + i)] = value ("Rate " + juce::String (i + 1), aux (i, false), 0, 100, num());
            k[(size_t) (9 + i)] = value ("Level " + juce::String (i + 1), aux (i, true), 0, 100, num());
        }
        k[5] = value ("Vel>Rate 1", [this]() -> int* { auto* x = KG(); return x ? &x->aux.velRate1 : nullptr; }, -100, 100, signed3());
        k[6] = value ("Key>Rate 2+4", [this]() -> int* { auto* x = KG(); return x ? &x->aux.keyRate2 : nullptr; }, -100, 100, signed3(),
                      [this] { if (auto* x = KG()) x->aux.keyRate4 = x->aux.keyRate2; });
        k[13] = value ("Vel>Level", [this]() -> int* { auto* x = KG(); return x ? &x->aux.velLevel : nullptr; }, -100, 100, signed3());
        k[15] = button ("GOTO DEST", [this] { gotoDest (ctx, 13); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (noProgram (g, r)) return;
        auto* k = KG();
        if (k == nullptr) return;
        auto gr = r.removeFromTop (120.0f).reduced (8.0f, 10.0f);
        g.setColour (colours::oledGrid);
        g.drawRect (gr, 0.8f);
        juce::Path p;
        float x = gr.getX();
        p.startNewSubPath (x, gr.getBottom());
        const float seg = gr.getWidth() / 5.0f;
        for (int i = 0; i < 4; ++i)
        {
            x += seg * (0.15f + 0.85f * (float) k->aux.rate[i] / 100.0f);
            if (i == 3) x = juce::jmax (x, gr.getX() + seg * 4.0f);
            p.lineTo (x, gr.getBottom() - gr.getHeight() * (float) k->aux.level[i] / 100.0f);
            if (i == 2) { x += seg * 0.6f; p.lineTo (x, gr.getBottom() - gr.getHeight() * (float) k->aux.level[i] / 100.0f); }
        }
        g.setColour (colours::oledText);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        oled::text (g, "R1-R4 / L1-L4  (L3 = SUSTAIN)", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
    }
};

//==============================================================================
void registerProgramPages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<ProgMainPage> (c));
    pm.add (std::make_unique<ProgOutputPage> (c));
    pm.add (std::make_unique<ProgMidiTunePage> (c));
    pm.add (std::make_unique<ProgEditUserPage> (c));
    pm.add (std::make_unique<ProgPitchBendPage> (c));
    pm.add (std::make_unique<ProgLfoPage> (c, false));
    pm.add (std::make_unique<ProgLfoPage> (c, true));
    pm.add (std::make_unique<ProgKeygroupPage> (c));
    pm.add (std::make_unique<ProgZonesPage> (c));
    pm.add (std::make_unique<ProgKeyspanPage> (c));
    pm.add (std::make_unique<ProgKgPitchAmpPage> (c));
    pm.add (std::make_unique<ProgFilterPage> (c));
    pm.add (std::make_unique<ProgEnvPage> (c, false));
    pm.add (std::make_unique<ProgEnvPage> (c, true));
    pm.add (std::make_unique<ProgAuxEnvPage> (c));
}

} // namespace s6k::ui
