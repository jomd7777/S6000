/*
  ==============================================================================
   S6000 Emulation - MULTI mode (manual p.37-79)
     128 parts, program assignment, PART LEVEL / OUTPUT / EFFECTS SEND /
     FINE TUNE / MIDI CHANNEL with WINDOW functions, EDIT PART, PART & MULTI TOOLS,
     MULTI LIST, PROGRAM LIST
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;

namespace
{
    ParamPtr makeParam (std::function<int()> get, std::function<void (int)> set, int lo, int hi, kb::Fmt f)
    {
        auto p = std::make_shared<Param>();
        p->get = std::move (get); p->set = std::move (set); p->min = lo; p->max = hi; p->format = std::move (f);
        p->bigStep = juce::jmax (1, (hi - lo) / 10);
        return p;
    }
}

//==============================================================================
class MultiPage : public Page
{
public:
    explicit MultiPage (Ctx& c) : Page (c, PageId::Multi, PageId::None, Mode::Multi) {}
    juce::String title() const override { return "MULTI"; }
    bool listLayout() const override { return true; }
    void onShow() override { ctx.editPartMode = false; ctx.engine.settings.editProgram = -1; }

    void build (Keys& k) override
    {
        k[0] = multiHeader (ctx);
        for (int i = 1; i <= 6; ++i)
        {
            const int partIdx = ctx.partScroll + i - 1;
            k[(size_t) i] = row ({}, {}, 0, false, [this, partIdx] { ctx.curPart = partIdx; });
        }
        k[7] = button ("EDIT PART", [this]
        {
            auto* p = ctx.part();
            if (p == nullptr || p->programName.empty()) { ctx.info ("EDIT PART", { "No program assigned", "to this part" }); return; }
            auto prog = ctx.mem.findProgram (juce::String (p->programName));
            if (prog == nullptr) { ctx.info ("EDIT PART", { "Program not in memory" }); return; }
            ctx.curProgram = ctx.mem.indexOfProgram (prog.get());
            ctx.curKeygroup = 0;
            ctx.editPartMode = true;
            ctx.pages->show (PageId::ProgMain);
        });
        k[8] = button (selected == 0 ? "MULTI TOOLS" : "PART TOOLS", [this] { selected == 0 ? multiTools() : partTools(); });

        auto partRef = [this] { return ctx.part(); };
        auto intRef = [partRef] (int fmt::PartData::* m) { return [partRef, m]() -> int* { auto* p = partRef(); return p ? &(p->*m) : nullptr; }; };

        k[9] = value ("Part Level", intRef (&fmt::PartData::level), 0, 100, num());
        k[9].windowA = makeParam ([this] { auto* p = ctx.part(); return p ? p->level : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->level = v; }, 0, 100, num());
        k[9].windowB = makeParam ([this] { auto* p = ctx.part(); return p ? p->pan : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->pan = v; }, -50, 50, pan());
        k[9].windowTitle = "PART LEVEL";

        k[10] = value ("Output", intRef (&fmt::PartData::output), 0, 23, [] (int v) { return names::output (v, false); });

        k[11] = valueFn ("Effects Send", [this] { auto* p = ctx.part(); return p ? p->fxSend : 0; },
                         [this] (int v) { if (auto* p = ctx.part()) p->fxSend = v; }, 0, 100,
                         [this] (int v) { auto* p = ctx.part(); return names::fxChannel (p ? p->fxChannel : 0) + " " + juce::String (v).paddedLeft ('0', 3); });
        k[11].windowA = makeParam ([this] { auto* p = ctx.part(); return p ? p->fxChannel : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->fxChannel = v; },
                                   0, 4, [] (int v) { return names::fxChannel (v); });
        k[11].windowB = makeParam ([this] { auto* p = ctx.part(); return p ? p->fxSend : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->fxSend = v; }, 0, 100, num());
        k[11].windowTitle = "EFFECTS SEND";

        k[12] = value ("Fine Tune", intRef (&fmt::PartData::fine), -50, 50, signed2());
        k[12].windowA = makeParam ([this] { auto* p = ctx.part(); return p ? p->semi : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->semi = v; }, -36, 36, signed2());
        k[12].windowB = k[12].param;
        k[12].windowTitle = "TRANSPOSE";

        k[13] = value ("MIDI Channel", intRef (&fmt::PartData::midiChannel), 0, 31, [] (int v) { return names::midiChannel (v); });
        k[13].windowA = makeParam ([this] { auto* p = ctx.part(); return p ? p->lowKey : 0; }, [this] (int v) { if (auto* p = ctx.part()) p->lowKey = juce::jmin (v, p->highKey); }, 0, 127, note (ctx));
        k[13].windowB = makeParam ([this] { auto* p = ctx.part(); return p ? p->highKey : 127; }, [this] (int v) { if (auto* p = ctx.part()) p->highKey = juce::jmax (v, p->lowKey); }, 0, 127, note (ctx));
        k[13].windowTitle = "KEY RANGE";

        k[14] = button ("SCROLL UP " + upArrow(), [this] { scrollBy (-1); });
        k[15] = button ("SCROLL DOWN " + downArrow(), [this] { scrollBy (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* m = ctx.multi();
        if (m == nullptr) return;
        for (int i = 1; i <= 6; ++i)
        {
            const int idx = ctx.partScroll + i - 1;
            if (idx >= (int) m->parts.size()) break;
            const auto& p = m->parts[(size_t) idx];
            juce::Rectangle<float> box (r.getX(), r.getY() + geo::boxTopForKey (i), r.getWidth(), geo::boxH);
            const bool sel = idx == ctx.curPart;
            const bool focused = sel && juce::isPositiveAndBelow (selected, 7) && selected > 0;
            g.setColour (colours::oledText);
            if (focused) g.fillRect (box); else g.drawRect (box, sel ? 2.0f : 1.0f);
            const auto col = focused ? colours::oledBg : colours::oledText;
            auto left = box.removeFromLeft (76.0f).reduced (4.0f, 1.0f);
            oled::text (g, "Part " + juce::String (idx + 1), left.removeFromTop (13.0f), juce::Justification::centredLeft, 10.0f, false, col);
            oled::text (g, "MIDI " + names::midiChannel (p.midiChannel), left, juce::Justification::centredLeft, 10.0f, false, col);
            g.setColour (col);
            g.drawVerticalLine ((int) box.getX(), box.getY() + 3, box.getBottom() - 3);
            juce::String nm = p.programName.empty() ? juce::String ("--- OFF ---") : juce::String (p.programName);
            if (! p.programName.empty() && m->resolved.size() > (size_t) idx && m->resolved[(size_t) idx] == nullptr) nm += " (?)";
            juce::String flags;
            if (p.mute) flags << "M ";
            if (m->solo[(size_t) idx]) flags << "S ";
            oled::text (g, flags, box.removeFromRight (26.0f), juce::Justification::centred, 10.0f, true, col);
            oled::text (g, nm, box.reduced (6.0f, 0), juce::Justification::centredLeft, 12.0f, true, col);
        }
    }

    void wheel (int delta) override
    {
        if (selected >= 1 && selected <= 6)
        {
            // assign program to the selected part
            auto* m = ctx.multi();
            auto* p = ctx.part();
            if (m == nullptr || p == nullptr) return;
            int idx = 0;
            if (! p->programName.empty())
                if (auto pr = ctx.mem.findProgram (juce::String (p->programName)); pr != nullptr) idx = ctx.mem.indexOfProgram (pr.get()) + 1;
            idx = juce::jlimit (0, ctx.mem.programs.size(), idx + delta);
            ctx.structural ([&]
            {
                p->programName = idx == 0 ? std::string() : ctx.mem.programs.getObjectPointer (idx - 1)->data.name;
            });
            return;
        }
        Page::wheel (delta);
    }

    void enterPlay (bool down) override
    {
        auto* p = ctx.part();
        ctx.engine.injectNote (p != nullptr ? p->midiChannel % 16 : 0, ctx.proc.prefs.playKey, 100, down);
    }

private:
    void scrollBy (int d)
    {
        ctx.partScroll = juce::jlimit (0, maxParts - 6, ctx.partScroll + d * 6);
        ctx.curPart = juce::jlimit (ctx.partScroll, ctx.partScroll + 5, ctx.curPart);
    }

    void multiTools()
    {
        std::vector<MenuPopup::Item> it;
        it.push_back ({ "GET INFO", [this]
        {
            auto* m = ctx.multi();
            if (m == nullptr) return;
            ctx.info ("GET INFO", { "MULTI: " + m->name, juce::String (m->numUsedParts()) + " PARTS USED",
                                    "MULTI " + juce::String (ctx.mem.currentMulti.load() + 1) + " OF " + juce::String (ctx.mem.multis.size()) });
        }, 9 });
        it.push_back ({ "MULTI LIST", [this] { ctx.pages->show (PageId::MultiList); }, 11 });
        it.push_back ({ "QUICKLOAD", [this] { quickLoad (ctx); }, 12 });
        it.push_back ({ "QUICKSAVE", [this] { quickSave (ctx); }, 13 });
        it.push_back ({ "NEW MULTI", [this]
        {
            ctx.structural ([this]
            {
                Multi::Ptr m = new Multi();
                m->name = ctx.mem.uniqueMultiName ("NEW MULTI");
                ctx.mem.multis.add (m);
                ctx.mem.currentMulti = ctx.mem.multis.size() - 1;
            });
        }, 14 });
        ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "MULTI TOOLS", it));
    }

    void partTools()
    {
        std::vector<MenuPopup::Item> it;
        it.push_back ({ "GET INFO", [this]
        {
            auto* p = ctx.part();
            if (p == nullptr) return;
            ctx.info ("PART " + juce::String (ctx.curPart + 1), { "PROGRAM: " + (p->programName.empty() ? juce::String ("OFF") : juce::String (p->programName)),
                                                               "MIDI: " + names::midiChannel (p->midiChannel) + "  LEVEL: " + juce::String (p->level),
                                                               "KEYS: " + ctx.note (p->lowKey) + " - " + ctx.note (p->highKey),
                                                               "FX: " + names::fxChannel (p->fxChannel) + " " + juce::String (p->fxSend) });
        }, 9 });
        it.push_back ({ "MUTE PART", [this] { if (auto* p = ctx.part()) p->mute = ! p->mute; }, 10 });
        it.push_back ({ "PROGRAM LIST", [this] { ctx.pages->show (PageId::ProgramList); }, 11 });
        it.push_back ({ "QUICKLOAD", [this] { quickLoad (ctx); }, 12 });
        it.push_back ({ "QUICKSAVE", [this] { quickSave (ctx); }, 13 });
        it.push_back ({ "SOLO PART", [this] { if (auto* m = ctx.multi()) m->solo[(size_t) ctx.curPart] = ! m->solo[(size_t) ctx.curPart]; }, 14 });
        it.push_back ({ "CLEAR PART", [this] { ctx.structural ([this] { if (auto* p = ctx.part()) p->programName.clear(); }); }, 15 });
        ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "PART TOOLS", it));
    }
};

//==============================================================================
/** Generic list page used by MULTI LIST and PROGRAM LIST */
class ItemListPage : public Page
{
public:
    ItemListPage (Ctx& c, PageId i, PageId parentPage, Mode m) : Page (c, i, parentPage, m) {}
    bool listLayout() const override { return true; }

    virtual int count() const = 0;
    virtual juce::String rowText (int i) const = 0;
    virtual juce::String rowInfo (int) const { return {}; }
    virtual int current() const = 0;
    virtual void setCurrent (int i) = 0;
    virtual int icon() const = 0;

    void addRowKeys (Keys& k)
    {
        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = scroll + geo::rowForKey (i);
            k[(size_t) i] = kb::row ({}, {}, 0, false, [this, rowIdx] { if (rowIdx < count()) setCurrent (rowIdx); });
        }
        k[14] = kb::button ("SELECT " + upArrow(), [this] { wheel (-1); });
        k[15] = kb::button ("SELECT " + downArrow(), [this] { wheel (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        oled::text (g, title() + "  (" + juce::String (count()) + ")", r.removeFromTop (geo::lineH), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        const int visible = geo::listRows, cur = current();
        if (cur < scroll) scroll = cur;
        if (cur >= scroll + visible) scroll = cur - visible + 1;
        scroll = juce::jlimit (0, juce::jmax (0, count() - visible), scroll);
        for (int i = scroll; i < juce::jmin (count(), scroll + visible); ++i)
        {
            auto line = r.removeFromTop (geo::lineH);
            const bool s = i == cur;
            if (s) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = s ? colours::oledBg : colours::oledText;
            oled::text (g, juce::String (i + 1).paddedLeft ('0', 3), line.removeFromLeft (28.0f), juce::Justification::centredLeft, 10.0f, false, col);
            oled::icon (g, icon(), line.removeFromLeft (18.0f), col);
            oled::text (g, rowInfo (i), line.removeFromRight (90.0f), juce::Justification::centredRight, 10.0f, false, col);
            oled::text (g, rowText (i), line, juce::Justification::centredLeft, 11.0f, false, col);
        }
        if (count() == 0) oled::text (g, "(NONE IN MEMORY)", r, juce::Justification::centred, 11.0f);
    }

    void wheel (int delta) override { if (count() > 0) setCurrent (juce::jlimit (0, count() - 1, current() + delta)); }

protected:
    int scroll = 0;
};

//==============================================================================
class MultiListPage : public ItemListPage
{
public:
    explicit MultiListPage (Ctx& c) : ItemListPage (c, PageId::MultiList, PageId::Multi, Mode::Multi) {}
    juce::String title() const override { return "MULTI LIST"; }
    int count() const override { return ctx.mem.multis.size(); }
    juce::String rowText (int i) const override { return ctx.mem.multis.getObjectPointer (i)->name; }
    juce::String rowInfo (int i) const override { return juce::String (ctx.mem.multis.getObjectPointer (i)->numUsedParts()) + " PARTS"; }
    int current() const override { return ctx.mem.currentMulti.load(); }
    void setCurrent (int i) override { ctx.mem.currentMulti = i; }
    int icon() const override { return 2; }

    void build (Keys& k) override
    {
        k[0] = kb::header ("MULTI LIST");
        addRowKeys (k);
        k[7] = kb::button ("MULTI", [this] { ctx.pages->show (PageId::Multi); });
        k[9] = kb::button ("NEW MULTI", [this]
        {
            ctx.structural ([this]
            {
                Multi::Ptr m = new Multi();
                m->name = ctx.mem.uniqueMultiName ("NEW MULTI");
                ctx.mem.multis.add (m);
                ctx.mem.currentMulti = ctx.mem.multis.size() - 1;
            });
        });
        k[10] = kb::button ("COPY", [this]
        {
            auto* src = ctx.multi();
            if (src == nullptr) return;
            ctx.structural ([this, src]
            {
                Multi::Ptr m = new Multi();
                m->name = ctx.mem.uniqueMultiName (src->name);
                m->parts = src->parts; m->fx = src->fx; m->solo = src->solo;
                ctx.mem.multis.add (m);
                ctx.mem.currentMulti = ctx.mem.multis.size() - 1;
            });
        });
        k[11] = kb::button ("DELETE", [this]
        {
            ctx.confirm ("DELETE MULTI", "Delete " + (ctx.multi() ? ctx.multi()->name : juce::String()) + "?", [this]
            {
                ctx.structural ([this]
                {
                    const int i = ctx.mem.currentMulti.load();
                    if (! juce::isPositiveAndBelow (i, ctx.mem.multis.size())) return;
                    ctx.proc.retire (ctx.mem.multis.getObjectPointer (i));
                    ctx.mem.multis.remove (i);
                    if (ctx.mem.multis.isEmpty()) { Multi::Ptr m = new Multi(); m->name = "NEW MULTI 1"; ctx.mem.multis.add (m); }
                    ctx.mem.currentMulti = juce::jlimit (0, ctx.mem.multis.size() - 1, i);
                });
            });
        });
        k[12] = kb::button ("RENAME", [this]
        {
            if (auto* m = ctx.multi())
                ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "RENAME MULTI", m->name, 20, [this] (const juce::String& n)
                {
                    if (auto* x = ctx.multi()) x->name = ctx.mem.uniqueMultiName (n);
                }));
        });
        k[13] = kb::button ("SELECT", [this] { ctx.pages->show (PageId::Multi); });
    }
};

//==============================================================================
class ProgramListPage : public ItemListPage
{
public:
    explicit ProgramListPage (Ctx& c) : ItemListPage (c, PageId::ProgramList, PageId::Multi, Mode::Multi) {}
    juce::String title() const override { return "PROGRAM LIST"; }
    int count() const override { return ctx.mem.programs.size(); }
    juce::String rowText (int i) const override { return ctx.mem.programs.getObjectPointer (i)->getName(); }
    juce::String rowInfo (int i) const override
    {
        auto* p = ctx.mem.programs.getObjectPointer (i);
        return juce::String (p->numKeygroups()) + " KG  #" + (p->data.midiProgram > 0 ? juce::String (p->data.midiProgram).paddedLeft ('0', 3) : juce::String ("OFF"));
    }
    int current() const override { return ctx.curProgram; }
    void setCurrent (int i) override { ctx.curProgram = i; ctx.curKeygroup = 0; }
    int icon() const override { return 3; }
    bool exitPressed() override { ctx.pages->show (ctx.pages->activeMode() == Mode::Program ? PageId::ProgMain : PageId::Multi); return true; }

    void build (Keys& k) override
    {
        k[0] = kb::header ("PROGRAM LIST");
        addRowKeys (k);
        k[7] = kb::button ("ASSIGN TO PART", [this]
        {
            auto* p = ctx.program();
            if (p == nullptr) return;
            ctx.structural ([this, p] { if (auto* part = ctx.part()) part->programName = p->data.name; });
            ctx.pages->show (PageId::Multi);
        });
        k[8] = kb::button ("EDIT PROGRAM", [this] { ctx.editPartMode = false; ctx.pages->show (PageId::ProgMain); });
        k[9] = kb::button ("NEW PROGRAM", [this]
        {
            ctx.structural ([this]
            {
                Program::Ptr p = new Program();
                p->setName (ctx.mem.uniqueProgramName ("NEW PROGRAM"));
                p->data.keygroups.push_back (fmt::KeygroupData());
                ctx.mem.programs.add (p);
                ctx.curProgram = ctx.mem.programs.size() - 1;
            });
        });
        k[10] = kb::button ("COPY", [this]
        {
            auto* src = ctx.program();
            if (src == nullptr) return;
            ctx.structural ([this, src]
            {
                Program::Ptr p = new Program();
                p->data = src->data;
                p->setName (ctx.mem.uniqueProgramName (src->getName()));
                ctx.mem.programs.add (p);
                ctx.curProgram = ctx.mem.programs.size() - 1;
            });
        });
        k[11] = kb::button ("DELETE", [this]
        {
            auto* p = ctx.program();
            if (p == nullptr) return;
            ctx.confirm ("DELETE PROGRAM", "Delete " + p->getName() + "?", [this]
            {
                ctx.structural ([this]
                {
                    const int i = ctx.curProgram;
                    if (! juce::isPositiveAndBelow (i, ctx.mem.programs.size())) return;
                    const auto name = ctx.mem.programs.getObjectPointer (i)->data.name;
                    for (auto* m : ctx.mem.multis)
                        for (auto& part : m->parts)
                            if (part.programName == name) part.programName.clear();
                    ctx.proc.retire (ctx.mem.programs.getObjectPointer (i));
                    ctx.mem.programs.remove (i);
                });
            });
        });
        k[12] = kb::button ("RENAME", [this]
        {
            if (auto* p = ctx.program())
                ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "RENAME PROGRAM", p->getName(), 20, [this] (const juce::String& n)
                {
                    auto* x = ctx.program();
                    if (x == nullptr) return;
                    const auto oldName = x->data.name;
                    const auto newName = ctx.mem.uniqueProgramName (n).toStdString();
                    ctx.structural ([&]
                    {
                        x->data.name = newName;
                        for (auto* m : ctx.mem.multis)
                            for (auto& part : m->parts)
                                if (part.programName == oldName) part.programName = newName;
                    });
                }));
        });
        k[13] = kb::button ("RENUMBER", [this]
        {
            auto* p = ctx.program();
            if (p == nullptr) return;
            juce::StringArray nums { "OFF" };
            for (int i = 1; i <= 128; ++i) nums.add (juce::String (i).paddedLeft ('0', 3));
            ctx.pages->pushPopup (std::make_unique<ListPopup> (ctx, "MIDI PROGRAM NUMBER", nums, p->data.midiProgram,
                                                               [this] (int i) { if (auto* x = ctx.program()) x->data.midiProgram = i; }));
        });
    }
};

void registerMultiPages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<MultiPage> (c));
    pm.add (std::make_unique<MultiListPage> (c));
    pm.add (std::make_unique<ProgramListPage> (c));
}

} // namespace s6k::ui
