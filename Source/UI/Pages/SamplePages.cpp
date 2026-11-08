/*
  ==============================================================================
   S6000 Emulation - EDIT SAMPLE (manual p.141-177)
     MONITOR, MASTER, TRIM, CHOP, LOOP, JOIN, MIX, FADE UP/DOWN, TIMESTRETCH,
     PITCH SHIFT, BPM MATCH, RE-SAMPLE, EQ, COPY SAMPLE, SAMPLE TOOLS, SAMPLE LIST
     All destructive edits support UNDO (one level).
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;
using juce::int64;

//==============================================================================
void common::showSampleTools (Ctx& c)
{
    std::vector<MenuPopup::Item> it;
    it.push_back ({ "GET INFO", [&c]
    {
        auto* s = c.sample();
        if (s == nullptr) { c.info ("GET INFO", { "No sample selected" }); return; }
        c.info ("GET INFO", { "SAMPLE: " + s->name,
                              juce::String (s->isStereo() ? "STEREO" : "MONO") + "  " + juce::String (s->sampleRate, 0) + "Hz",
                              "LENGTH: " + timeString ((double) s->numFrames() / s->sampleRate) + " (" + juce::String (s->numFrames()) + ")",
                              "ROOT: " + c.note (s->rootNote) + "  LOOP: " + (s->loopEnabled ? juce::String ("ON") : juce::String ("OFF")),
                              "SIZE: " + juce::String ((double) s->bytesUsed() / 1048576.0, 2) + " MB" });
    }, 9 });
    it.push_back ({ "SAMPLE LIST", [&c] { c.pages->show (PageId::SampleList); }, 11 });
    it.push_back ({ "QUICKLOAD", [&c] { quickLoad (c); }, 12 });
    it.push_back ({ "QUICKSAVE", [&c] { quickSave (c); }, 13 });
    it.push_back ({ "CREATE PROG", [&c] { createProgramFromSample (c, c.sample()); }, 14 });
    it.push_back ({ "DELETE SAMPLE", [&c]
    {
        auto* s = c.sample();
        if (s == nullptr) return;
        c.confirm ("DELETE SAMPLE", "Delete " + s->name + "?", [&c]
        {
            c.structural ([&c]
            {
                if (! juce::isPositiveAndBelow (c.curSample, c.mem.samples.size())) return;
                c.proc.retire (c.mem.samples.getObjectPointer (c.curSample));
                c.mem.samples.remove (c.curSample);
            });
        });
    }, 15 });
    c.pages->pushPopup (std::make_unique<MenuPopup> (c, "SAMPLE TOOLS", it));
}

//==============================================================================
class SamplePage : public Page
{
public:
    SamplePage (Ctx& c, PageId i, PageId parentPage, juce::String t) : Page (c, i, parentPage, Mode::Sample), pageTitle (std::move (t)) {}
    juce::String title() const override { return pageTitle; }

    Sample* S() const { return ctx.sample(); }

    kb::IntRef sref (int Sample::* m) const { return [this, m]() -> int* { auto* s = S(); return s ? &(s->*m) : nullptr; }; }

    /** frame position parameter (int64 marker) */
    SoftKey marker (const juce::String& t, int64 Sample::* m, std::function<void()> changed = {})
    {
        auto k = valueFn (t, [this, m] { auto* s = S(); return s ? (int) (s->*m) : 0; },
                          [this, m, changed] (int v) { if (auto* s = S()) { s->*m = v; s->sanitiseMarkers(); if (changed) changed(); } },
                          0, S() != nullptr ? S()->numFrames() : 0,
                          [this] (int v) { return markerText (v); });
        return k;
    }

    juce::String markerText (int v) const
    {
        auto* s = S();
        if (s == nullptr) return "0";
        if (ctx.proc.prefs.loopDisplay == 1) return juce::String ((double) v / s->sampleRate, 3) + "s";
        return juce::String (v);
    }

    void commonKeys (Keys& k, bool withMain = true)
    {
        k[0] = sampleHeader (ctx, [this] { resetView(); });
        if (withMain) k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::SampMain); });
        k[8] = button ("SAMPLE TOOLS", [this] { showSampleTools (ctx); });
    }

    void wheel (int delta) override
    {
        if (juce::isPositiveAndBelow (selected, 16))
            if (auto& p = keys[(size_t) selected].param)
                if (p->max > 20000)
                {
                    const int64 span = juce::jmax<int64> (1, viewEnd - viewStart);
                    const int step = (int) juce::jmax<int64> (1, span / 300);
                    p->adjust (delta * step);
                    return;
                }
        Page::wheel (delta);
    }

    void cursor (int dir) override
    {
        // CURSOR = fine (single sample) adjustment of markers
        if (juce::isPositiveAndBelow (selected, 16))
            if (auto& p = keys[(size_t) selected].param) { p->adjust (dir); return; }
    }

    void resetView()
    {
        auto* s = S();
        viewStart = 0;
        viewEnd = s != nullptr ? s->numFrames() : 1;
    }

    void zoom (bool in, int64 centre)
    {
        auto* s = S();
        if (s == nullptr) return;
        const int64 span = juce::jmax<int64> (64, viewEnd - viewStart);
        const int64 ns = in ? juce::jmax<int64> (64, span / 2) : juce::jmin<int64> (s->numFrames(), span * 2);
        viewStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, s->numFrames() - ns), centre - ns / 2);
        viewEnd = viewStart + ns;
    }

    void onShow() override
    {
        auto* s = S();
        if (s != nullptr && (viewEnd <= viewStart || viewEnd > s->numFrames() || lastSample != s)) resetView();
        lastSample = s;
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<float> r, std::initializer_list<std::pair<int64, juce::String>> marks)
    {
        auto* s = S();
        g.setColour (colours::oledText);
        g.drawRect (r, 1.0f);
        if (s == nullptr) { oled::text (g, "NO SAMPLE", r, juce::Justification::centred, 11.0f); return; }
        if (viewEnd <= viewStart || viewEnd > s->numFrames()) resetView();
        oled::waveform (g, r.reduced (1.0f), *s, viewStart, viewEnd);
        const double span = (double) juce::jmax<int64> (1, viewEnd - viewStart);
        for (auto& m : marks)
        {
            if (m.first < viewStart || m.first > viewEnd) continue;
            const float x = r.getX() + r.getWidth() * (float) ((double) (m.first - viewStart) / span);
            g.setColour (colours::oledWarn);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            oled::text (g, m.second, juce::Rectangle<float> (x - 10, r.getY() - 12.0f, 20, 12), juce::Justification::centred, 10.0f, true, colours::oledWarn);
        }
        if (ctx.engine.isAuditioning())
        {
            const int64 pos = ctx.engine.getAuditionPosition();
            if (pos >= viewStart && pos <= viewEnd)
            {
                g.setColour (colours::oledText.withAlpha (0.7f));
                g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) ((double) (pos - viewStart) / span)), r.getY(), r.getBottom());
            }
        }
    }

    bool wantsTicks() const override { return ctx.engine.isAuditioning(); }

    /** destructive edit: run on a copy, then swap in under the lock (with UNDO) */
    void edit (const juce::String& what, const std::function<void (Sample&)>& op)
    {
        auto* s = S();
        if (s == nullptr) { ctx.info (what, { "No sample selected" }); return; }
        ctx.engine.audition (nullptr);
        ctx.snapshot (*s);
        auto work = s->clone (s->name);
        op (*work);
        {
            const juce::SpinLock::ScopedLockType sl (ctx.mem.lock);
            s->buffer = std::move (work->buffer);
            s->sampleRate = work->sampleRate;
            s->start = work->start; s->end = work->end;
            s->loopStart = work->loopStart; s->loopEnd = work->loopEnd; s->loopEnabled = work->loopEnabled;
            s->loopXfade = work->loopXfade;
            s->sanitiseMarkers();
            s->touch();
        }
        resetView();
    }

    void play (int64 from, int64 to, bool loop = false)
    {
        auto* s = S();
        if (s == nullptr) return;
        if (ctx.engine.isAuditioning()) { ctx.engine.audition (nullptr); return; }
        ctx.engine.audition (Sample::Ptr (s), loop, loop ? -1 : from, loop ? -1 : to);
    }

    void enterPlay (bool down) override
    {
        if (auto* s = S()) { if (down) ctx.engine.audition (Sample::Ptr (s), false, s->start, s->end); else ctx.engine.audition (nullptr); }
    }

    juce::String pageTitle;
    int64 viewStart = 0, viewEnd = 1;
    Sample* lastSample = nullptr;
};

//==============================================================================
class SampMainPage : public SamplePage
{
public:
    explicit SampMainPage (Ctx& c) : SamplePage (c, PageId::SampMain, PageId::None, "EDIT\nSAMPLE") {}
    void build (Keys& k) override
    {
        commonKeys (k, false);
        const bool ok = S() != nullptr;
        k[1] = valueFn ("Monitor", [this] { return ctx.sampleMonitor; }, [this] (int v) { ctx.sampleMonitor = v; }, 0, 1, list ({ "RAW SAMPLE", "PROGRAM" }));
        k[2] = button ("MASTER", [this] { ctx.pages->show (PageId::SampMaster); }, ok);
        k[3] = button ("TRIM", [this] { ctx.pages->show (PageId::SampTrim); }, ok);
        k[4] = button ("CHOP", [this] { ctx.pages->show (PageId::SampChop); }, ok);
        k[5] = button ("LOOP", [this] { ctx.pages->show (PageId::SampLoop); }, ok);
        k[6] = button ("JOIN", [this] { ctx.pages->show (PageId::SampJoin); }, ok);
        k[7] = button ("MIX", [this] { ctx.pages->show (PageId::SampMix); }, ok);
        k[9] = button ("FADE UP/DOWN", [this] { ctx.pages->show (PageId::SampFade); }, ok);
        k[10] = button ("TIME STRETCH", [this] { ctx.pages->show (PageId::SampTimestretch); }, ok);
        k[11] = button ("PITCH SHIFT", [this] { ctx.pages->show (PageId::SampPitchShift); }, ok);
        k[12] = button ("BPM MATCH", [this] { ctx.pages->show (PageId::SampBpm); }, ok);
        k[13] = button ("RE-SAMPLE", [this] { ctx.pages->show (PageId::SampResample); }, ok);
        k[14] = button ("EQ", [this] { ctx.pages->show (PageId::SampEq); }, ok);
        k[15] = button ("COPY SAMPLE", [this]
        {
            auto* s = S();
            if (s == nullptr) return;
            ctx.structural ([this, s]
            {
                ctx.mem.samples.add (s->clone (ctx.mem.uniqueSampleName (s->name)));
                ctx.curSample = ctx.mem.samples.size() - 1;
            });
        }, ok);
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (auto* s = S())
        {
            oled::text (g, "Length : " + timeString ((double) s->numFrames() / s->sampleRate), r.removeFromTop (16.0f), juce::Justification::centredLeft, 10.5f);
            oled::text (g, "S-Rate : " + juce::String (s->sampleRate, 0) + "Hz", r.removeFromTop (16.0f), juce::Justification::centredLeft, 10.5f);
            oled::text (g, juce::String (s->isVirtual ? "VIRTUAL" : "RAM") + "      " + juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa ")) + ctx.note (s->rootNote),
                        r.removeFromTop (16.0f), juce::Justification::centredLeft, 10.5f);
        }
        else
            r.removeFromTop (42.0f);
        r.removeFromTop (4.0f);
        oled::memoryBox (g, r.removeFromTop (96.0f), ctx);
    }

    void enterPlay (bool down) override
    {
        if (ctx.sampleMonitor == 0) { SamplePage::enterPlay (down); return; }
        Page::enterPlay (down);
    }

};

//==============================================================================
class SampMasterPage : public SamplePage
{
public:
    explicit SampMasterPage (Ctx& c) : SamplePage (c, PageId::SampMaster, PageId::SampMain, "MASTER") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = value ("Semitone Tune", sref (&Sample::semitone), -36, 36, signed2());
        k[2] = value ("Fine Tune", sref (&Sample::cents), -50, 50, signed2());
        k[3] = value ("Original Note", sref (&Sample::rootNote), 0, 127, note (ctx));
        k[4] = value ("Play Mode", sref (&Sample::playMode), 0, 3, [] (int v) { return names::playback (v, false); });
        k[5] = button ("REVERSE", [this] { edit ("REVERSE", [] (Sample& s) { ops::reverse (s, 0, s.numFrames()); }); });
        k[6] = button ("MAKE MONO", [this] { edit ("MAKE MONO", [] (Sample& s) { ops::makeMono (s); }); });
        k[9] = button ("NORMALISE", [this] { edit ("NORMALISE", [] (Sample& s) { ops::normalise (s, 0.0f); }); });
        k[10] = valueFn ("Re-Scale Level", [this] { return rescaleDb; }, [this] (int v) { rescaleDb = v; }, -24, 24, db());
        k[11] = button ("RE-SCALE", [this] { const float d = (float) rescaleDb; edit ("RE-SCALE", [d] (Sample& s) { ops::rescale (s, d); }); });
        k[12] = button ("PLAY", [this] { if (auto* s = S()) play (s->start, s->end); });
        SoftKey lp;
        lp.kind = SoftKey::Kind::Check;
        lp.title = "LOOP ON";
        lp.checked = [this] { auto* s = S(); return s != nullptr && s->loopEnabled; };
        lp.onPress = [this] { if (auto* s = S()) { s->loopEnabled = ! s->loopEnabled; s->sanitiseMarkers(); } };
        k[13] = lp;
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* s = S();
        drawWave (g, r.removeFromTop (110.0f).reduced (0, 12.0f), { { s ? s->start : 0, "S" }, { s ? s->end : 0, "E" } });
        if (s != nullptr)
            oled::text (g, "Peak " + juce::String (juce::Decibels::gainToDecibels (ops::peakLevel (*s)), 1) + "dB", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
    }
private:
    int rescaleDb = 0;
};

//==============================================================================
class SampTrimPage : public SamplePage
{
public:
    explicit SampTrimPage (Ctx& c) : SamplePage (c, PageId::SampTrim, PageId::SampMain, "TRIM") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = marker ("Start", &Sample::start);
        k[2] = marker ("End", &Sample::end);
        k[3] = value ("Play Mode", sref (&Sample::playMode), 0, 3, [] (int v) { return names::playback (v, false); });
        k[4] = button ("FIND START/END", [this]
        {
            auto* s = S();
            if (s == nullptr) return;
            s->start = ops::findZeroCrossing (*s, s->start, 1);
            s->end = ops::findZeroCrossing (*s, s->end, -1);
            s->sanitiseMarkers();
        });
        k[5] = button ("ZOOM IN", [this] { zoom (true, focusPos()); });
        k[6] = button ("ZOOM OUT", [this] { zoom (false, focusPos()); });
        k[9] = button ("PLAY FROM", [this] { if (auto* s = S()) play (focusPos(), s->end); });
        k[10] = button ("PLAY TO", [this] { if (auto* s = S()) play (juce::jmax<int64> (0, focusPos() - (int64) (s->sampleRate * 1.5)), focusPos()); });
        k[11] = button ("PLAY REGION", [this] { if (auto* s = S()) play (s->start, s->end); });
        k[12] = label ("Length", S() ? markerText ((int) (S()->end - S()->start)) : juce::String());
        k[15] = button ("TRIM SAMPLE", [this]
        {
            std::vector<MenuPopup::Item> it;
            it.push_back ({ "DISCARD S>E", [this] { edit ("TRIM", [] (Sample& s) { ops::discardOutside (s, s.start, s.end); }); }, 9 });
            it.push_back ({ "START>LOOP END", [this] { edit ("TRIM", [] (Sample& s) { ops::discardOutside (s, s.start, s.loopEnd); }); }, 10 });
            it.push_back ({ "LOOP START>END", [this] { edit ("TRIM", [] (Sample& s) { ops::discardOutside (s, s.loopStart, s.end); }); }, 11 });
            ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "TRIM SAMPLE", it));
        });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* s = S();
        drawWave (g, r.removeFromTop (150.0f).reduced (0, 12.0f), { { s ? s->start : 0, "S" }, { s ? s->end : 0, "E" } });
        if (s != nullptr)
            oled::text (g, "View " + juce::String ((double) (viewEnd - viewStart) / s->sampleRate, 3) + "s", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
    }
private:
    int64 focusPos() const { auto* s = S(); if (s == nullptr) return 0; return selected == 2 ? s->end : s->start; }
};

//==============================================================================
class SampChopPage : public SamplePage
{
public:
    explicit SampChopPage (Ctx& c) : SamplePage (c, PageId::SampChop, PageId::SampMain, "CHOP") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = marker ("Start", &Sample::start, [this] { recalc(); });
        k[2] = marker ("End", &Sample::end, [this] { recalc(); });
        k[3] = valueFn ("Slices", [this] { return slices; }, [this] (int v) { slices = v; recalc(); }, 2, 64, num());
        k[4] = valueFn ("Chop Mode", [this] { return mode; }, [this] (int v) { mode = v; recalc(); }, 0, 1, list ({ "EQUAL", "TRANSIENT" }));
        k[5] = valueFn ("Sensitivity", [this] { return sens; }, [this] (int v) { sens = v; recalc(); }, 0, 100, num());
        SoftKey mp;
        mp.kind = SoftKey::Kind::Check;
        mp.title = "MAKE PROGRAM";
        mp.checked = [this] { return makeProgram; };
        mp.onPress = [this] { makeProgram = ! makeProgram; };
        k[6] = mp;
        k[9] = valueFn ("Slice", [this] { return slice + 1; }, [this] (int v) { slice = v - 1; }, 1, juce::jmax (1, (int) points.size() - 1), num());
        k[10] = button ("PLAY SLICE", [this]
        {
            if (slice + 1 < (int) points.size()) play (points[(size_t) slice], points[(size_t) slice + 1]);
        });
        k[11] = button ("ZOOM IN", [this] { if (auto* s = S()) zoom (true, s->start); });
        k[12] = button ("ZOOM OUT", [this] { if (auto* s = S()) zoom (false, s->start); });
        k[15] = button ("CHOP", [this] { doChop(); });
    }
    void onShow() override { SamplePage::onShow(); recalc(); }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto wr = r.removeFromTop (150.0f).reduced (0, 12.0f);
        drawWave (g, wr, {});
        const double span = (double) juce::jmax<int64> (1, viewEnd - viewStart);
        for (size_t i = 0; i < points.size(); ++i)
        {
            if (points[i] < viewStart || points[i] > viewEnd) continue;
            const float x = wr.getX() + wr.getWidth() * (float) ((double) (points[i] - viewStart) / span);
            g.setColour ((int) i == slice || (int) i == slice + 1 ? colours::oledWarn : colours::oledDim);
            g.drawVerticalLine ((int) x, wr.getY(), wr.getBottom());
        }
        oled::text (g, juce::String ((int) points.size() - 1) + " slices", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
    }
private:
    int slices = 8, mode = 0, sens = 50, slice = 0;
    bool makeProgram = true;
    std::vector<int64> points;
    void recalc()
    {
        auto* s = S();
        points.clear();
        if (s == nullptr) return;
        points = mode == 0 ? ops::equalSlices (*s, slices) : ops::detectTransients (*s, sens, 64);
        slice = juce::jlimit (0, juce::jmax (0, (int) points.size() - 2), slice);
    }
    void doChop()
    {
        auto* s = S();
        if (s == nullptr || points.size() < 2) return;
        auto parts = ops::chop (*s, points, s->name);
        const bool mk = makeProgram;
        ctx.structural ([&]
        {
            juce::StringArray names;
            for (auto& p : parts)
            {
                p->name = ctx.mem.uniqueSampleName (p->name);
                names.add (p->name);
                ctx.mem.samples.add (p);
            }
            if (mk)
            {
                Program::Ptr prg = new Program();
                prg->setName (ctx.mem.uniqueProgramName (s->name.substring (0, 14) + " CHOP"));
                for (int i = 0; i < names.size() && i < 99; ++i)
                {
                    fmt::KeygroupData kgd;
                    kgd.lowNote = kgd.highNote = juce::jmin (127, 36 + i);       // from C1 upwards (drum pads)
                    kgd.zones[0].sampleName = names[i].toStdString();
                    kgd.zones[0].keyTrack = false;
                    kgd.zones[0].playback = fmt::PlayOneShot;
                    prg->data.keygroups.push_back (kgd);
                }
                ctx.mem.programs.add (prg);
                ctx.curProgram = ctx.mem.programs.size() - 1;
            }
        });
        ctx.info ("CHOP", { juce::String ((int) parts.size()) + " samples created", mk ? juce::String ("+ program (C1 upwards)") : juce::String() });
    }
};

//==============================================================================
class SampLoopPage : public SamplePage
{
public:
    explicit SampLoopPage (Ctx& c) : SamplePage (c, PageId::SampLoop, PageId::SampMain, "LOOP") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = marker ("Loop Start", &Sample::loopStart, [this] { lockLength (true); });
        k[2] = marker ("Loop End", &Sample::loopEnd, [this] { lockLength (false); });
        k[3] = valueFn ("Loop Length", [this] { auto* s = S(); return s ? (int) (s->loopEnd - s->loopStart) : 0; },
                        [this] (int v) { if (auto* s = S()) { s->loopStart = s->loopEnd - v; s->sanitiseMarkers(); } },
                        2, S() ? S()->numFrames() : 2, [this] (int v) { return markerText (v); });
        k[4] = value ("Loop Direction", sref (&Sample::loopDirection), 0, 1, list ({ "FORWARDS", "ALTERNATING" }));
        k[5] = value ("Play Mode", sref (&Sample::playMode), 0, 3, [] (int v) { return names::playback (v, false); });
        k[6] = value ("Xfade Length", sref (&Sample::loopXfade), 0, S() ? S()->numFrames() : 0, [this] (int v)
        {
            auto* s = S();
            return s ? juce::String ((int) (1000.0 * v / s->sampleRate)) + "ms" : juce::String();
        });
        k[9] = button ("PLAY LOOP", [this] { play (0, 0, true); });
        k[10] = button ("AUTO FIND", [this]
        {
            auto* s = S();
            if (s == nullptr) return;
            s->loopEnd = ops::autoFindLoopEnd (*s, s->loopStart, s->loopEnd, (int) (s->sampleRate * 0.02));
            s->sanitiseMarkers();
        });
        k[11] = valueFn ("Fade Profile", [this] { return profile; }, [this] (int v) { profile = v; }, 0, 1, list ({ "LINEAR", "EQUAL POWER" }));
        k[12] = button ("CROSSFADE", [this]
        {
            auto* s = S();
            if (s == nullptr || s->loopXfade < 2) { ctx.info ("CROSSFADE", { "Set an Xfade Length first" }); return; }
            const int xf = s->loopXfade, pr = profile;
            edit ("CROSSFADE", [xf, pr] (Sample& x) { ops::crossfadeLoop (x, xf, pr); });
        });
        SoftKey lk;
        lk.kind = SoftKey::Kind::Check;
        lk.title = "LOOP LOCK";
        lk.checked = [this] { auto* s = S(); return s != nullptr && s->loopLock; };
        lk.onPress = [this] { if (auto* s = S()) s->loopLock = ! s->loopLock; };
        k[13] = lk;
        k[14] = button (juce::String ("ZOOM ") + (spliceZoom > 64 ? "IN" : "OUT"), [this] { spliceZoom = spliceZoom > 64 ? spliceZoom / 4 : 4096; });
        SoftKey lp;
        lp.kind = SoftKey::Kind::Check;
        lp.title = "LOOP ON";
        lp.checked = [this] { auto* s = S(); return s != nullptr && s->loopEnabled; };
        lp.onPress = [this] { if (auto* s = S()) { s->loopEnabled = ! s->loopEnabled; if (s->loopEnabled && s->playMode == fmt::PlayNoLoop) s->playMode = fmt::PlayLoopInRelease; s->sanitiseMarkers(); } };
        k[15] = lp;
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* s = S();
        auto wr = r.removeFromTop (130.0f).reduced (0, 6.0f);
        g.setColour (colours::oledText);
        g.drawRect (wr, 1.0f);
        if (s == nullptr) return;
        // loop splice view: before LOOP END | after LOOP START
        auto left = wr.withWidth (wr.getWidth() * 0.5f), right = wr.withTrimmedLeft (wr.getWidth() * 0.5f);
        const int64 half = spliceZoom;
        oled::waveform (g, left.reduced (1.0f), *s, juce::jmax<int64> (0, s->loopEnd - half), s->loopEnd);
        oled::waveform (g, right.reduced (1.0f), *s, s->loopStart, juce::jmin<int64> (s->numFrames(), s->loopStart + half));
        g.setColour (colours::oledWarn);
        g.drawVerticalLine ((int) wr.getCentreX(), wr.getY(), wr.getBottom());
        oled::text (g, "END | START", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledWarn);
        oled::text (g, juce::String ((double) (s->loopEnd - s->loopStart) / s->sampleRate, 3) + "s loop", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
    }
private:
    int profile = 1;
    int64 spliceZoom = 1024;
    int64 lockedLen = -1;
    void lockLength (bool startMoved)
    {
        auto* s = S();
        if (s == nullptr) return;
        if (! s->loopLock) { lockedLen = -1; return; }
        if (lockedLen < 0) lockedLen = s->loopEnd - s->loopStart;
        if (startMoved) s->loopEnd = juce::jmin<int64> (s->numFrames(), s->loopStart + lockedLen);
        else s->loopStart = juce::jmax<int64> (0, s->loopEnd - lockedLen);
        s->sanitiseMarkers();
    }
};

//==============================================================================
class SampTwoSamplePage : public SamplePage
{
public:
    SampTwoSamplePage (Ctx& c, PageId i, juce::String t, bool mixMode) : SamplePage (c, i, PageId::SampMain, std::move (t)), mix (mixMode) {}
    Sample* B() const { return juce::isPositiveAndBelow (bIndex, ctx.mem.samples.size()) ? ctx.mem.samples.getObjectPointer (bIndex) : nullptr; }

    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = valueFn ("Sample B", [this] { return bIndex; }, [this] (int v) { bIndex = v; }, 0, juce::jmax (0, ctx.mem.samples.size() - 1),
                        [this] (int) { auto* b = B(); return b ? b->name : juce::String ("---"); });
        if (mix)
        {
            k[2] = valueFn ("Level A", [this] { return levelA; }, [this] (int v) { levelA = v; }, 0, 100, num());
            k[3] = valueFn ("Level B", [this] { return levelB; }, [this] (int v) { levelB = v; }, 0, 100, num());
            k[4] = valueFn ("Offset", [this] { return offsetMs; }, [this] (int v) { offsetMs = v; }, 0, 60000, suffix ("ms"));
        }
        k[9] = button ("PLAY B", [this] { if (auto* b = B()) ctx.engine.audition (Sample::Ptr (b)); });
        k[15] = button (mix ? "MIX" : "JOIN", [this] { execute(); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* a = S(); auto* b = B();
        oled::text (g, "A: " + (a ? a->name : juce::String ("---")), r.removeFromTop (16.0f), juce::Justification::centredLeft, 11.0f, true);
        if (a) oled::waveform (g, r.removeFromTop (44.0f).reduced (2.0f), *a, 0, a->numFrames());
        oled::text (g, (mix ? "+ B: " : "& B: ") + (b ? b->name : juce::String ("---")), r.removeFromTop (16.0f), juce::Justification::centredLeft, 11.0f, true);
        if (b) oled::waveform (g, r.removeFromTop (44.0f).reduced (2.0f), *b, 0, b->numFrames());
    }

private:
    bool mix;
    int bIndex = 0, levelA = 100, levelB = 100, offsetMs = 0;
    void execute()
    {
        auto* a = S(); auto* b = B();
        if (a == nullptr || b == nullptr) return;
        Sample::Ptr bb (b);
        if (mix)
        {
            const float la = (float) levelA / 100.0f, lb = (float) levelB / 100.0f;
            const int64 off = (int64) (offsetMs * 0.001 * a->sampleRate);
            edit ("MIX", [bb, la, lb, off] (Sample& s) { ops::mix (s, *bb, la, lb, off); });
        }
        else
        {
            auto joined = ops::join (*a, *b, ctx.mem.uniqueSampleName (a->name.substring (0, 10) + "+" + b->name.substring (0, 8)));
            ctx.structural ([&] { ctx.mem.samples.add (joined); ctx.curSample = ctx.mem.samples.size() - 1; });
            resetView();
        }
    }
};

//==============================================================================
class SampFadePage : public SamplePage
{
public:
    explicit SampFadePage (Ctx& c) : SamplePage (c, PageId::SampFade, PageId::SampMain, "FADE UP/DOWN") {}
    void onShow() override
    {
        SamplePage::onShow();
        if (auto* s = S()) { from = 0; to = juce::jmin<int64> (s->numFrames(), (int64) (s->sampleRate * 0.5)); }
    }
    void build (Keys& k) override
    {
        commonKeys (k);
        const int n = S() ? S()->numFrames() : 0;
        k[1] = valueFn ("Fade Start", [this] { return (int) from; }, [this] (int v) { from = v; to = juce::jmax (to, from); }, 0, n, [this] (int v) { return markerText (v); });
        k[2] = valueFn ("Fade End", [this] { return (int) to; }, [this] (int v) { to = v; from = juce::jmin (to, from); }, 0, n, [this] (int v) { return markerText (v); });
        k[3] = valueFn ("Fade Type", [this] { return up; }, [this] (int v) { up = v; }, 0, 1, list ({ "FADE DOWN", "FADE UP" }));
        k[4] = valueFn ("Curve", [this] { return curve; }, [this] (int v) { curve = v; }, 0, 2, list ({ "LINEAR", "EXPONENTIAL", "LOGARITHMIC" }));
        k[5] = button ("START>END", [this] { if (auto* s = S()) { from = s->start; to = s->end; } });
        k[9] = button ("PLAY", [this] { play (from, to); });
        k[15] = button ("FADE", [this] { const auto f = from, t = to; const bool u = up != 0; const int c = curve; edit ("FADE", [f, t, u, c] (Sample& s) { ops::fade (s, f, t, u, c); }); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        drawWave (g, r.removeFromTop (150.0f).reduced (0, 12.0f), { { from, "<" }, { to, ">" } });
    }
private:
    int64 from = 0, to = 0;
    int up = 0, curve = 0;
};

//==============================================================================
class SampProcessPage : public SamplePage
{
public:
    enum Kind { Stretch, Pitch, Bpm, Resample, Eq };
    SampProcessPage (Ctx& c, PageId i, juce::String t, Kind kd) : SamplePage (c, i, PageId::SampMain, std::move (t)), kind (kd) {}

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        switch (kind)
        {
            case Stretch:
                k[1] = valueFn ("Stretch %", [this] { return ratio; }, [this] (int v) { ratio = v; }, 25, 400, suffix ("%"));
                k[2] = label ("New Length", s ? timeString (s->numFrames() / s->sampleRate * ratio / 100.0) : juce::String());
                k[15] = button ("STRETCH", [this] { const double r = ratio / 100.0; edit ("TIME STRETCH", [r] (Sample& x) { ops::timeStretch (x, r); }); });
                break;
            case Pitch:
                k[1] = valueFn ("Semitones", [this] { return semis; }, [this] (int v) { semis = v; }, -24, 24, signed2());
                k[2] = valueFn ("Cents", [this] { return cents; }, [this] (int v) { cents = v; }, -50, 50, signed2());
                k[15] = button ("SHIFT", [this] { const float st = (float) semis + (float) cents * 0.01f; edit ("PITCH SHIFT", [st] (Sample& x) { ops::pitchShift (x, st); }); });
                break;
            case Bpm:
                k[1] = valueFn ("Beats", [this] { return beats; }, [this] (int v) { beats = v; }, 1, 128, num());
                k[2] = label ("Original BPM", s ? juce::String (ops::estimateBpm (*s, beats), 2) : juce::String());
                k[3] = valueFn ("New BPM", [this] { return newBpm; }, [this] (int v) { newBpm = v; }, 400, 3000, [] (int v) { return juce::String (v / 10) + "." + juce::String (v % 10); });
                k[15] = button ("MATCH", [this]
                {
                    auto* x = S();
                    if (x == nullptr) return;
                    const double r = ops::estimateBpm (*x, beats) / (newBpm / 10.0);
                    edit ("BPM MATCH", [r] (Sample& y) { ops::timeStretch (y, r); });
                });
                break;
            case Resample:
            {
                static const int rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000 };
                k[1] = valueFn ("New Rate", [this] { return rateIdx; }, [this] (int v) { rateIdx = v; }, 0, 8, [] (int v) { return juce::String (rates[v]) + "Hz"; });
                k[2] = label ("Current Rate", s ? juce::String (s->sampleRate, 0) + "Hz" : juce::String());
                k[15] = button ("RE-SAMPLE", [this] { const double nr = rates[rateIdx]; edit ("RE-SAMPLE", [nr] (Sample& x) { ops::resample (x, nr); }); });
                break;
            }
            case Eq:
                k[1] = valueFn ("Low Gain", [this] { return low; }, [this] (int v) { low = v; }, -18, 18, db());
                k[2] = valueFn ("Mid Freq", [this] { return midF; }, [this] (int v) { midF = v; }, 100, 10000, suffix ("Hz"));
                k[3] = valueFn ("Mid Gain", [this] { return mid; }, [this] (int v) { mid = v; }, -18, 18, db());
                k[4] = valueFn ("High Gain", [this] { return high; }, [this] (int v) { high = v; }, -18, 18, db());
                k[15] = button ("APPLY EQ", [this]
                {
                    const float a = (float) low, b = (float) midF, c = (float) mid, d = (float) high;
                    edit ("EQ", [a, b, c, d] (Sample& x) { ops::eq (x, a, b, c, d); });
                });
                break;
        }
        k[9] = button ("PLAY", [this] { if (auto* x = S()) play (x->start, x->end); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto* s = S();
        drawWave (g, r.removeFromTop (110.0f).reduced (0, 10.0f), {});
        if (s == nullptr) return;
        oled::text (g, "Length " + timeString (s->numFrames() / s->sampleRate), r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, juce::String (s->sampleRate, 0) + "Hz " + (s->isStereo() ? "STEREO" : "MONO"), r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "UNDO restores the sample", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.0f, false, colours::oledDim);
    }

private:
    Kind kind;
    int ratio = 100, semis = 0, cents = 0, beats = 4, newBpm = 1200, rateIdx = 5, low = 0, midF = 1000, mid = 0, high = 0;
};

//==============================================================================
class SampleListPage : public Page
{
public:
    explicit SampleListPage (Ctx& c) : Page (c, PageId::SampleList, PageId::SampMain, Mode::Sample) {}
    juce::String title() const override { return "SAMPLE LIST"; }
    bool listLayout() const override { return true; }

    void build (Keys& k) override
    {
        k[0] = header ("SAMPLE LIST");
        k[0].subtitle = juce::String (ctx.mem.samples.size()) + " SAMPLES";
        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = scroll + geo::rowForKey (i);
            k[(size_t) i] = row ({}, {}, 0, false, [this, rowIdx] { if (rowIdx < ctx.mem.samples.size()) ctx.curSample = rowIdx; });
        }
        k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::SampMain); });
        k[9] = button ("PLAY", [this] { if (auto* s = ctx.sample()) ctx.engine.audition (Sample::Ptr (s)); });
        k[10] = button ("COPY", [this]
        {
            if (auto* s = ctx.sample())
                ctx.structural ([this, s] { ctx.mem.samples.add (s->clone (ctx.mem.uniqueSampleName (s->name))); });
        });
        k[11] = button ("DELETE", [this]
        {
            auto* s = ctx.sample();
            if (s == nullptr) return;
            ctx.confirm ("DELETE SAMPLE", "Delete " + s->name + "?", [this]
            {
                ctx.structural ([this]
                {
                    if (! juce::isPositiveAndBelow (ctx.curSample, ctx.mem.samples.size())) return;
                    ctx.proc.retire (ctx.mem.samples.getObjectPointer (ctx.curSample));
                    ctx.mem.samples.remove (ctx.curSample);
                });
            });
        });
        k[12] = button ("RENAME", [this] { auto h = sampleHeader (ctx); if (h.onPress) h.onPress(); });
        k[13] = button ("DELETE UNUSED", [this]
        {
            ctx.confirm ("DELETE UNUSED", "Delete samples not\nused by any program?", [this]
            {
                ctx.structural ([this]
                {
                    for (int i = ctx.mem.samples.size(); --i >= 0;)
                    {
                        bool used = false;
                        for (auto* p : ctx.mem.programs)
                            for (auto& kgd : p->data.keygroups)
                                for (auto& z : kgd.zones)
                                    if (juce::String (z.sampleName).equalsIgnoreCase (ctx.mem.samples.getObjectPointer (i)->name)) used = true;
                        if (! used) { ctx.proc.retire (ctx.mem.samples.getObjectPointer (i)); ctx.mem.samples.remove (i); }
                    }
                });
            });
        });
        k[14] = button ("SELECT " + upArrow(), [this] { wheel (-1); });
        k[15] = button ("SELECT " + downArrow(), [this] { wheel (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        oled::text (g, "NAME                  LENGTH  RATE", r.removeFromTop (geo::lineH), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        const int visible = geo::listRows, n = ctx.mem.samples.size(), cur = ctx.curSample;
        if (cur < scroll) scroll = cur;
        if (cur >= scroll + visible) scroll = cur - visible + 1;
        scroll = juce::jlimit (0, juce::jmax (0, n - visible), scroll);
        for (int i = scroll; i < juce::jmin (n, scroll + visible); ++i)
        {
            auto* s = ctx.mem.samples.getObjectPointer (i);
            auto line = r.removeFromTop (geo::lineH);
            const bool sel = i == cur;
            if (sel) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = sel ? colours::oledBg : colours::oledText;
            oled::icon (g, 4, line.removeFromLeft (18.0f), col);
            oled::text (g, juce::String (s->sampleRate / 1000.0, 1) + "k", line.removeFromRight (40.0f), juce::Justification::centredRight, 10.0f, false, col);
            oled::text (g, juce::String ((double) s->numFrames() / s->sampleRate, 2) + "s", line.removeFromRight (56.0f), juce::Justification::centredRight, 10.0f, false, col);
            oled::text (g, s->name, line, juce::Justification::centredLeft, 11.0f, false, col);
        }
        if (n == 0) oled::text (g, "(NO SAMPLES)", r, juce::Justification::centred, 11.0f);
    }

    void wheel (int delta) override { ctx.curSample = juce::jlimit (0, juce::jmax (0, ctx.mem.samples.size() - 1), ctx.curSample + delta); }
private:
    int scroll = 0;
};

//==============================================================================
void registerSamplePages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<SampMainPage> (c));
    pm.add (std::make_unique<SampMasterPage> (c));
    pm.add (std::make_unique<SampTrimPage> (c));
    pm.add (std::make_unique<SampChopPage> (c));
    pm.add (std::make_unique<SampLoopPage> (c));
    pm.add (std::make_unique<SampTwoSamplePage> (c, PageId::SampJoin, "JOIN", false));
    pm.add (std::make_unique<SampTwoSamplePage> (c, PageId::SampMix, "MIX", true));
    pm.add (std::make_unique<SampFadePage> (c));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampTimestretch, "TIMESTRETCH", SampProcessPage::Stretch));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampPitchShift, "PITCH SHIFT", SampProcessPage::Pitch));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampBpm, "BPM MATCH", SampProcessPage::Bpm));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampResample, "RE-SAMPLE", SampProcessPage::Resample));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampEq, "EQ", SampProcessPage::Eq));
    pm.add (std::make_unique<SampleListPage> (c));
}

} // namespace s6k::ui
