/*
  ==============================================================================
   S6000 Emulation - helpers shared by all pages
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#pragma once
#include "../PageSystem.h"
#include "../Popups.h"
#include "../../PluginProcessor.h"
#include "../../Core/SampleOps.h"
#include "../../Core/Dsp.h"
#include "../../Core/DiskManager.h"
#include "../../Core/SamplerEngine.h"

namespace s6k::ui
{
void registerLoadPages (PageManager&, Ctx&);
void registerMultiPages (PageManager&, Ctx&);
void registerProgramPages (PageManager&, Ctx&);
void registerSamplePages (PageManager&, Ctx&);
void registerRecordPage (PageManager&, Ctx&);
void registerFxPages (PageManager&, Ctx&);
void registerSavePage (PageManager&, Ctx&);
void registerUtilityPages (PageManager&, Ctx&);

inline void registerAllPages (PageManager& pm, Ctx& c)
{
    registerLoadPages (pm, c);
    registerMultiPages (pm, c);
    registerProgramPages (pm, c);
    registerSamplePages (pm, c);
    registerRecordPage (pm, c);
    registerFxPages (pm, c);
    registerSavePage (pm, c);
    registerUtilityPages (pm, c);
}

namespace common
{
    using namespace kb;

    inline const juce::String upArrow()   { return juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")); }
    inline const juce::String downArrow() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")); }

    /** Header (F1) that selects the program being edited with the DATA wheel. */
    inline SoftKey programHeader (Ctx& c)
    {
        auto p = std::make_shared<Param>();
        p->get = [&c] { return c.curProgram; };
        p->set = [&c] (int v)
        {
            c.curProgram = v;
            c.curKeygroup = 0;
            c.engine.settings.editProgram = c.editPartMode ? -1 : v;
        };
        p->min = 0; p->max = juce::jmax (0, c.mem.programs.size() - 1);
        p->format = [&c] (int) { auto* pr = c.program(); return pr != nullptr ? pr->getName() : juce::String ("(NO PROGRAMS)"); };
        auto k = header ({}, p);
        k.onPress = [&c]
        {
            if (auto* pr = c.program())
                c.pages->pushPopup (std::make_unique<NamingPopup> (c, "RENAME PROGRAM", pr->getName(), 20, [&c] (const juce::String& n)
                {
                    if (auto* x = c.program()) x->setName (c.mem.uniqueProgramName (n));
                    c.mem.resolveAll();
                }));
        };
        return k;
    }

    inline SoftKey sampleHeader (Ctx& c, std::function<void()> changed = {})
    {
        auto p = std::make_shared<Param>();
        p->get = [&c] { return c.curSample; };
        p->set = [&c, changed] (int v) { c.curSample = v; if (changed) changed(); };
        p->min = 0; p->max = juce::jmax (0, c.mem.samples.size() - 1);
        p->format = [&c] (int)
        {
            auto* s = c.sample();
            if (s == nullptr) return juce::String ("(NO SAMPLES)");
            return s->name + (s->loopEnabled ? "  ~" : "") + (s->isStereo() ? "  [ST]" : "  [M]");
        };
        auto k = header ({}, p);
        k.onPress = [&c]
        {
            if (auto* s = c.sample())
                c.pages->pushPopup (std::make_unique<NamingPopup> (c, "RENAME SAMPLE", s->name, 20, [&c] (const juce::String& n)
                {
                    if (auto* x = c.sample())
                    {
                        const auto oldName = x->name;
                        const auto newName = c.mem.uniqueSampleName (n);
                        c.structural ([&]
                        {
                            x->name = newName;
                            // keep programs pointing at the renamed sample
                            for (auto* pr : c.mem.programs)
                                for (auto& kg : pr->data.keygroups)
                                    for (auto& z : kg.zones)
                                        if (juce::String (z.sampleName).equalsIgnoreCase (oldName)) z.sampleName = newName.toStdString();
                        });
                    }
                }));
        };
        return k;
    }

    inline SoftKey multiHeader (Ctx& c)
    {
        auto p = std::make_shared<Param>();
        p->get = [&c] { return c.mem.currentMulti.load(); };
        p->set = [&c] (int v) { c.mem.currentMulti = v; };
        p->min = 0; p->max = juce::jmax (0, c.mem.multis.size() - 1);
        p->format = [&c] (int) { auto* m = c.multi(); return m != nullptr ? m->name : juce::String ("(NO MULTI)"); };
        auto k = header ({}, p);
        k.onPress = [&c]
        {
            if (auto* m = c.multi())
                c.pages->pushPopup (std::make_unique<NamingPopup> (c, "RENAME MULTI", m->name, 20, [&c] (const juce::String& n)
                {
                    if (auto* x = c.multi()) x->name = c.mem.uniqueMultiName (n);
                }));
        };
        return k;
    }

    /** "KEYGRP n" selector (F9 on the keygroup related pages) */
    inline SoftKey keygroupSelect (Ctx& c)
    {
        return valueFn ("KEYGRP", [&c] { return c.curKeygroup + 1; },
                        [&c] (int v) { c.curKeygroup = v - 1; },
                        1, juce::jmax (1, c.program() != nullptr ? c.program()->numKeygroups() : 1),
                        [&c] (int v) { auto* p = c.program(); return juce::String (v) + "/" + juce::String (p != nullptr ? p->numKeygroups() : 0); });
    }

    /** APM modulation input: first touch = amount, second touch = source (WINDOW shows both). */
    inline SoftKey modInput (Page& pg, int keyIndex, const juce::String& title, IntRef amount, IntRef source, int lo = -100, int hi = 100)
    {
        bool* alt = &pg.altMode[(size_t) keyIndex];
        SoftKey k;
        k.kind = SoftKey::Kind::Value;
        k.title = title + (*alt ? "  SRC" : "");
        k.arrows = true;
        auto p = std::make_shared<Param>();
        p->get = [alt, amount, source] { if (*alt) { auto* s = source(); return s ? *s : 0; } auto* a = amount(); return a ? *a : 0; };
        p->set = [alt, amount, source] (int v) { if (*alt) { if (auto* s = source()) *s = v; } else if (auto* a = amount()) *a = v; };
        p->min = *alt ? 0 : lo;
        p->max = *alt ? names::numModSources() - 1 : hi;
        p->format = [amount, source] (int)
        {
            auto* s = source(); auto* a = amount();
            return names::modSource (s ? *s : 0).substring (0, 10) + " " + names::signedValue (a ? *a : 0, 3);
        };
        k.param = p;
        k.onPress = [alt] { *alt = ! *alt; };

        auto ps = std::make_shared<Param>();
        ps->get = [source] { auto* s = source(); return s ? *s : 0; };
        ps->set = [source] (int v) { if (auto* s = source()) *s = v; };
        ps->min = 0; ps->max = names::numModSources() - 1;
        ps->format = [] (int v) { return names::modSource (v); };
        auto pa = std::make_shared<Param>();
        pa->get = [amount] { auto* a = amount(); return a ? *a : 0; };
        pa->set = [amount] (int v) { if (auto* a = amount()) *a = v; };
        pa->min = lo; pa->max = hi;
        pa->format = [] (int v) { return names::signedValue (v, 3); };
        k.windowA = ps; k.windowB = pa; k.windowTitle = title;
        return k;
    }

    inline void gotoSource (Ctx& c, Page& pg)
    {
        if (! juce::isPositiveAndBelow (pg.selected, 16) || pg.keys[(size_t) pg.selected].windowA == nullptr)
        {
            c.info ("GOTO SOURCE", { "Select a modulation", "input first" });
            return;
        }
        const int src = pg.keys[(size_t) pg.selected].windowA->get();
        switch (src)
        {
            case 9:  c.pages->show (PageId::ProgLfo1); break;
            case 10: c.pages->show (PageId::ProgLfo2); break;
            case 11: c.pages->show (PageId::ProgAmpEnv); break;
            case 12: c.pages->show (PageId::ProgFiltEnv); break;
            case 13: c.pages->show (PageId::ProgAuxEnv); break;
            case 14: c.pages->show (PageId::UtilMidi); break;
            default: c.info ("GOTO SOURCE", { names::modSource (src), "is a MIDI / note source" }); break;
        }
    }

    inline void gotoDest (Ctx& c, int source)
    {
        auto* p = c.program();
        if (p == nullptr) return;
        static const char* inputNames[] = { "AMP MOD 1", "AMP MOD 2", "PAN MOD 1", "PAN MOD 2", "PAN MOD 3", "PITCH MOD 1", "PITCH MOD 2",
                                            "KG AMP MOD", "FILTER MOD 1", "FILTER MOD 2", "FILTER MOD 3", "LFO1 RATE", "LFO1 DELAY",
                                            "LFO1 DEPTH", "LFO2 RATE", "LFO2 DELAY", "LFO2 DEPTH" };
        static const PageId pagesFor[] = { PageId::ProgOutput, PageId::ProgOutput, PageId::ProgOutput, PageId::ProgOutput, PageId::ProgOutput,
                                           PageId::ProgKgPitchAmp, PageId::ProgKgPitchAmp, PageId::ProgKgPitchAmp,
                                           PageId::ProgFilter, PageId::ProgFilter, PageId::ProgFilter,
                                           PageId::ProgLfo1, PageId::ProgLfo1, PageId::ProgLfo1, PageId::ProgLfo2, PageId::ProgLfo2, PageId::ProgLfo2 };
        juce::StringArray items;
        std::vector<PageId> targets;
        for (int i = 0; i < fmt::ProgramData::NumModInputs; ++i)
            if (p->data.modSource[i] == source) { items.add (inputNames[i]); targets.push_back (pagesFor[i]); }
        if (items.isEmpty()) { c.info ("GOTO DEST", { names::modSource (source), "is not assigned", "to any destination" }); return; }
        c.pages->pushPopup (std::make_unique<ListPopup> (c, "GOTO DEST", items, 0, [&c, targets] (int i) { c.pages->show (targets[(size_t) i]); }));
    }

    inline void quickLoad (Ctx& c) { c.pages->show (PageId::Load); }
    inline void quickSave (Ctx& c) { c.pages->show (PageId::Save); }

    inline juce::String timeString (double seconds)
    {
        const int m = (int) (seconds / 60.0);
        const double s = seconds - m * 60.0;
        return juce::String (m) + "m" + juce::String (s, 2) + "s";
    }

    inline void clearMemory (Ctx& c)
    {
        c.engine.audition (nullptr);
        c.structural ([&c]
        {
            for (auto* s : c.mem.samples) c.proc.retire (s);
            for (auto* p : c.mem.programs) c.proc.retire (p);
            for (auto* m : c.mem.multis) c.proc.retire (m);
            c.mem.clear();
        });
        c.undoCopy = nullptr;
        c.curProgram = c.curSample = c.curKeygroup = c.curPart = 0;
    }

    void showProgramTools (Ctx& c);
    void showSampleTools (Ctx& c);
    void createProgramFromSample (Ctx& c, Sample* s);
}
} // namespace s6k::ui
