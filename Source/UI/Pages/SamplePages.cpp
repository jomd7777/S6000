/*
  ==============================================================================
   S6000 Emulation - EDIT SAMPLE (manual p.141-177)
     MAIN, MASTER, TRIM, CHOP, LOOP, JOIN, MIX, FADE UP/DOWN, TIMESTRETCH,
     PITCH SHIFT, BPM MATCH, RE-SAMPLE, EQ, SAMPLE TOOLS, SAMPLE LIST

     Screen layout as on the hardware: soft keys F2..F5 / F10..F13, sample
     info + MEMORY in the centre, the waveform across the whole screen
     (overview + zoomed view), the action on F16.
     Every process runs on a copy ("Processing sample") and ends with the
     PLAY / KEEP ORIGINAL / NEW / BOTH / OVERWRITE and RENAME prompt.
     The waveform can also be edited with the mouse: drag the edit points,
     wheel = zoom, double click = whole sample, click the overview = scroll.
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "Common.h"
#include "../WaveView.h"
#include <thread>

namespace s6k::ui
{
using namespace common;
using juce::int64;

namespace
{
    const juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    /** which sample the audition belongs to (for the play line) */
    const void* gAuditioned = nullptr;

    bool isPlaying (Ctx& c, const void* s) { return s != nullptr && gAuditioned == s && c.engine.isAuditioning(); }

    void playSample (Ctx& c, Sample::Ptr s, bool loop = false, int64 from = -1, int64 to = -1)
    {
        if (s == nullptr) return;
        if (isPlaying (c, s.get())) { c.engine.audition (nullptr); gAuditioned = nullptr; return; }   // PLAY = PLAY / STOP
        gAuditioned = s.get();
        c.engine.audition (s, loop, from, to);
    }

    void stopPlay (Ctx& c) { c.engine.audition (nullptr); gAuditioned = nullptr; }

    juce::String lengthText (int64 frames, double sr)
    {
        const double secs = (double) frames / juce::jmax (1.0, sr);
        if (secs < 60.0) return juce::String (secs, 2) + "s";
        const int m = (int) (secs / 60.0);
        return juce::String (m) + "m" + juce::String (secs - m * 60.0, 2).paddedLeft ('0', 5) + "s";
    }

    juce::String dbText (int v) { return (v > 0 ? "+" : (v < 0 ? "-" : "+")) + juce::String (std::abs (v)).paddedLeft ('0', 2) + " dB"; }
    juce::String hzText (int v) { return juce::String (v) + " Hz"; }

    /** waveform area: full width between the F5/F13 row and the F8/F16 row (as on the hardware) */
    juce::Rectangle<float> waveArea (juce::Rectangle<float> lcd)
    {
        const float top = geo::rowY (lcd, 4) + geo::boxH * 0.5f + 3.0f;
        const float bottom = geo::rowY (lcd, 7) - geo::boxH * 0.5f + 2.0f;
        return { lcd.getX() + geo::margin, top, lcd.getWidth() - 2.0f * geo::margin, bottom - top };
    }

    /** centre column between the two key columns, rows 5..6 (JOIN / MIX) */
    juce::Rectangle<float> centreWaveArea (juce::Rectangle<float> lcd)
    {
        const float x1 = lcd.getX() + geo::margin + geo::boxW + 10.0f, x2 = lcd.getRight() - geo::margin - geo::boxW - 10.0f;
        const float top = geo::rowY (lcd, 5) - geo::boxH * 0.5f + 2.0f, bottom = geo::rowY (lcd, 6) + geo::boxH * 0.5f;
        return { x1, top, x2 - x1, bottom - top };
    }

    void drawSampleInfo (juce::Graphics& g, juce::Rectangle<float>& r, Ctx& c, const Sample* s, const juce::String& suffix = {})
    {
        auto line = [&] (const juce::String& t) { oled::text (g, t, r.removeFromTop (16.0f).reduced (6.0f, 0), juce::Justification::centredLeft, 10.5f); };
        if (s == nullptr) { line ("Length" + suffix + " : ---"); line ("S/Rate" + suffix + " : ---"); line (""); return; }
        line ("Length" + suffix + " : " + lengthText (s->numFrames(), s->sampleRate) + (s->isStereo() ? "  ST" : "  M"));
        line ("S/Rate" + suffix + " : " + juce::String (juce::roundToInt (s->sampleRate)) + " Hz");
        line (juce::String (s->isVirtual ? "VIRTUAL" : "RAM") + "        " + u8 ("\xe2\x99\xa9 ") + c.note (s->rootNote));
    }

    void drawCompactMemory (juce::Graphics& g, juce::Rectangle<float> r, Ctx& c)
    {
        r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 150.0f), r.getHeight());
        oled::box (g, r);
        auto head = r.removeFromTop (16.0f);
        g.drawLine (head.getX(), head.getBottom(), head.getRight(), head.getBottom());
        oled::text (g, "MEMORY", head, juce::Justification::centred, 9.5f, true);
        oled::text (g, c.memoryLine (2), r.removeFromTop (r.getHeight() * 0.5f), juce::Justification::centred, 10.0f);
        oled::text (g, c.memoryLine (3), r, juce::Justification::centred, 10.0f);
    }
}

//==============================================================================
/** "Processing sample" : runs the process on a background thread, then shows the KEEP prompt */
struct ProcessJob
{
    juce::String title;                             // e.g. "RE-SCALE"
    std::vector<Sample::Ptr> originals;             // 1 or 2 (JOIN / MIX)
    juce::String newName;
    std::function<Sample::Ptr()> run;               // background thread, works on copies only
};

class ResultPopup : public Popup
{
public:
    ResultPopup (Ctx& c, ProcessJob j, Sample::Ptr r)
        : Popup (c, j.title), job (std::move (j)), result (std::move (r))
    {
        name = job.newName.substring (0, 20);
        pos = juce::jmin (19, name.length());
    }

    juce::Rectangle<float> popupBounds (juce::Rectangle<float> lcd) const override
    {
        const float y1 = geo::rowY (lcd, 0) - geo::boxH * 0.5f, y2 = geo::rowY (lcd, 4) + geo::boxH * 0.5f;
        return { lcd.getX() + geo::margin, y1, lcd.getWidth() - 2.0f * geo::margin, y2 - y1 };
    }

    void build (Keys& k) override
    {
        const bool two = job.originals.size() > 1;
        if (two)
        {
            k[5] = kb::button ("PLAY ORIG A", [this] { playSample (ctx, job.originals[0]); });
            k[6] = kb::button ("PLAY ORIG B", [this] { playSample (ctx, job.originals[1]); });
            k[7] = kb::button ("KEEP ORIGINALS", [this] { finish (KeepOriginal); });
            k[13] = kb::button ("PLAY NEW", [this] { playSample (ctx, result); });
            k[14] = kb::button ("KEEP ALL", [this] { finish (KeepBoth); });
            k[15] = kb::button ("KEEP NEW", [this] { finish (KeepNew); });
        }
        else
        {
            k[5] = kb::button ("PLAY ORIGINAL", [this] { playSample (ctx, job.originals[0]); });
            k[6] = kb::button ("KEEP BOTH", [this] { finish (KeepBoth); });
            k[7] = kb::button ("KEEP ORIGINAL", [this] { finish (KeepOriginal); });
            k[13] = kb::button ("PLAY NEW", [this] { playSample (ctx, result); });
            k[14] = kb::button ("OVERWRITE", [this] { finish (Overwrite); });
            k[15] = kb::button ("KEEP NEW", [this] { finish (KeepNew); });
        }
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        oled::text (g, "Process completed/new sample created", r.removeFromTop (18.0f), juce::Justification::centred, 10.5f);
        // name field (type, or DATA wheel + CURSOR)
        auto nb = r.removeFromTop (24.0f).withSizeKeepingCentre (juce::jmin (r.getWidth() - 40.0f, 300.0f), 20.0f);
        g.setColour (colours::oledText);
        g.fillRect (nb);
        g.setFont (fonts::oled (12.0f, true));
        const auto shown = name.paddedRight (' ', 20);
        auto cell = nb.reduced (6.0f, 2.0f);
        const float cw = cell.getWidth() / 20.0f;
        for (int i = 0; i < 20; ++i)
        {
            auto ch = juce::Rectangle<float> (cell.getX() + cw * (float) i, cell.getY(), cw, cell.getHeight());
            if (ch.getRight() > cell.getRight()) break;
            if (i == pos) { g.setColour (colours::oledBg); g.fillRect (ch); g.setColour (colours::oledText); }
            else g.setColour (colours::oledBg);
            g.drawText (juce::String::charToString (shown[i]), ch, juce::Justification::centred, false);
        }
        r.removeFromTop (4.0f);
        const bool two = job.originals.size() > 1;
        for (auto t : { two ? "* KEEP ORIGINALS discards new sample" : "* KEEP ORIGINAL discards new sample",
                        two ? "* KEEP NEW discards the original samples" : "* KEEP NEW discards the original sample",
                        two ? "* KEEP ALL keeps all samples (safest)" : "* KEEP BOTH keeps both samples (safest)",
                        two ? "" : "* OVERWRITE replaces original with new" })
            if (juce::String (t).isNotEmpty())
                oled::text (g, t, r.removeFromTop (15.0f).reduced (20.0f, 0), juce::Justification::centredLeft, 9.5f);

        // the new sample, with its play line
        auto wr = r.reduced (20.0f, 3.0f);
        if (wr.getHeight() > 18.0f && result != nullptr)
        {
            wave::Options o;
            o.from = 0; o.to = result->numFrames();
            o.log = ctx.waveLog != 0;
            o.sampleRate = result->sampleRate;
            o.playing = isPlaying (ctx, result.get());
            o.playPos = ctx.engine.getAuditionPosition();
            o.tagLeft = "NEW  " + lengthText (result->numFrames(), result->sampleRate);
            wave::draw (g, wr, result.get(), o, peaks);
        }
    }

    bool wantsAnimation() override { return gAuditioned != nullptr && ctx.engine.isAuditioning(); }

    void wheel (int delta) override
    {
        juce::String s = name.paddedRight (' ', pos + 1);
        int ci = NamingPopup::charset.indexOfChar (s[pos]);
        if (ci < 0) ci = 0;
        ci = (ci + delta) % NamingPopup::charset.length();
        if (ci < 0) ci += NamingPopup::charset.length();
        name = (s.substring (0, pos) + juce::String::charToString (NamingPopup::charset[ci]) + s.substring (pos + 1)).trimEnd();
    }
    void cursor (int dir) override { pos = juce::jlimit (0, 19, pos + dir); }
    bool textInput (juce::juce_wchar c) override
    {
        if (c == 8 || c == 127) { if (pos > 0) { name = name.substring (0, pos - 1) + name.substring (pos); --pos; } return true; }
        if (c < 32 || c > 126) return false;
        juce::String s = name.paddedRight (' ', pos);
        name = (s.substring (0, pos) + juce::String::charToString (c) + s.substring (pos + 1)).substring (0, 20);
        pos = juce::jmin (19, pos + 1);
        return true;
    }
    void enterPlay (bool down) override { if (down) playSample (ctx, result); }

private:
    enum Choice { KeepOriginal, KeepNew, KeepBoth, Overwrite };
    ProcessJob job;
    Sample::Ptr result;
    juce::String name;
    int pos = 0;
    wave::Peaks peaks;

    static void copyInto (Ctx& c, Sample& dst, const Sample& src)
    {
        const juce::SpinLock::ScopedLockType sl (c.mem.lock);
        dst.buffer.makeCopyOf (src.buffer);
        dst.sampleRate = src.sampleRate;
        dst.rootNote = src.rootNote; dst.semitone = src.semitone; dst.cents = src.cents;
        dst.start = src.start; dst.end = src.end;
        dst.loopEnabled = src.loopEnabled; dst.loopStart = src.loopStart; dst.loopEnd = src.loopEnd;
        dst.loopXfade = src.loopXfade; dst.loopDirection = src.loopDirection; dst.playMode = src.playMode;
        dst.sanitiseMarkers();
        dst.touch();
    }

    void renameWithRefs (Sample& s, const juce::String& wanted)
    {
        if (wanted.trim().isEmpty() || wanted.trim().equalsIgnoreCase (s.name)) return;
        const auto oldName = s.name, newName = ctx.mem.uniqueSampleName (wanted.trim());
        ctx.structural ([&]
        {
            s.name = newName;
            for (auto* pr : ctx.mem.programs)
                for (auto& kg : pr->data.keygroups)
                    for (auto& z : kg.zones)
                        if (juce::String (z.sampleName).equalsIgnoreCase (oldName)) z.sampleName = newName.toStdString();
        });
    }

    void finish (Choice choice)
    {
        stopPlay (ctx);
        auto& c = ctx;
        auto j = job;
        auto res = result;
        const auto nm = name.trim().isNotEmpty() ? name.trim() : j.newName;
        close();    // (kept alive until the call stack unwinds)

        auto origAlive = [&c] (const Sample::Ptr& s) { return s != nullptr && c.mem.samples.contains (s.get()); };

        switch (choice)
        {
            case KeepOriginal: break;
            case KeepBoth:
            {
                res->name = c.mem.uniqueSampleName (nm);
                c.structural ([&c, res] { c.mem.samples.add (res); c.curSample = c.mem.samples.size() - 1; });
                break;
            }
            case Overwrite:
            case KeepNew:
            {
                if (j.originals.size() > 1)
                {
                    // JOIN / MIX: new sample in, both originals out
                    res->name = c.mem.uniqueSampleName (nm);
                    c.structural ([&c, res, j]
                    {
                        for (auto& o : j.originals)
                        {
                            const int idx = c.mem.samples.indexOf (o.get());
                            if (idx >= 0) { c.proc.retire (o.get()); c.mem.samples.remove (idx); }
                        }
                        c.mem.samples.add (res);
                        c.curSample = c.mem.samples.size() - 1;
                    });
                    break;
                }
                auto orig = j.originals[0];
                if (! origAlive (orig))
                {
                    res->name = c.mem.uniqueSampleName (nm);
                    c.structural ([&c, res] { c.mem.samples.add (res); c.curSample = c.mem.samples.size() - 1; });
                    break;
                }
                c.snapshot (*orig);                 // UNDO restores the original
                copyInto (c, *orig, *res);
                if (choice == KeepNew) renameWithRefs (*orig, nm);
                c.curSample = juce::jmax (0, c.mem.samples.indexOf (orig.get()));
                break;
            }
        }
        if (c.pages != nullptr) c.pages->refresh();
    }
};

class ProcessingPopup : public Popup
{
public:
    ProcessingPopup (Ctx& c, ProcessJob j) : Popup (c, j.title), job (std::move (j))
    {
        boxWidth = 0.62f; boxHeight = 0.42f;
        ops::abortFlag = false;
        ops::progress = 0.0f;
        auto fn = job.run;
        worker = std::thread ([this, fn]
        {
            Sample::Ptr r;
            try { r = fn ? fn() : nullptr; } catch (...) { r = nullptr; }
            if (ops::abortFlag.load()) r = nullptr;
            result = r;
            done = true;
        });
    }

    ~ProcessingPopup() override
    {
        if (worker.joinable()) { ops::abortFlag = true; worker.join(); }
    }

    void build (Keys& k) override { k[7] = kb::button ("ABORT", [] { ops::abortFlag = true; }); }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (8.0f);
        oled::text (g, "Processing sample", r.removeFromTop (18.0f), juce::Justification::centred, 11.0f, true);
        auto bar = r.removeFromTop (20.0f).reduced (24.0f, 3.0f);
        g.setColour (colours::oledText);
        g.drawRect (bar, 1.0f);
        const float p = ops::progress.load();
        if (p > 0.001f)
        {
            g.fillRect (bar.reduced (2.0f).withWidth ((bar.getWidth() - 4.0f) * juce::jlimit (0.0f, 1.0f, p)));
            oled::text (g, juce::String ((int) (p * 100.0f)) + "%", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
        }
        else
        {
            const float w = bar.getWidth() * 0.2f;
            const float x = bar.getX() + 2.0f + std::fmod ((float) frame * 6.0f, bar.getWidth() - w - 4.0f);
            g.fillRect (juce::Rectangle<float> (x, bar.getY() + 2.0f, w, bar.getHeight() - 4.0f));
        }
    }

    bool wantsTicks() const override { return true; }

    void tick() override
    {
        ++frame;
        if (! done.load()) return;
        if (worker.joinable()) worker.join();
        auto& c = ctx;
        auto j = job;
        auto r = result;
        const bool aborted = ops::abortFlag.load();
        close();
        if (r != nullptr) c.pages->pushPopup (std::make_unique<ResultPopup> (c, j, r));
        else if (! aborted) c.info (j.title, { "Cannot process this sample" });
    }

private:
    ProcessJob job;
    std::thread worker;
    std::atomic<bool> done { false };
    Sample::Ptr result;
    int frame = 0;
};

static void startProcess (Ctx& c, ProcessJob j)
{
    stopPlay (c);
    c.pages->pushPopup (std::make_unique<ProcessingPopup> (c, std::move (j)));
}

/** the usual case: process a copy of one sample */
static void processCopy (Ctx& c, const juce::String& title, Sample* s, std::function<void (Sample&)> op)
{
    if (s == nullptr) { c.info (title, { "No sample selected" }); return; }
    ProcessJob j;
    j.title = title;
    Sample::Ptr orig (s);
    j.originals = { orig };
    j.newName = c.mem.uniqueSampleName (s->name);
    const auto nn = j.newName;
    j.run = [orig, op, nn]() -> Sample::Ptr { auto r = orig->clone (nn); op (*r); return r; };
    startProcess (c, std::move (j));
}

//==============================================================================
/** small window with parameters + an action (FIND START/END, CROSSFADE, PLAY MODE ...) */
class ToolWindow : public Popup
{
public:
    ToolWindow (Ctx& c, const juce::String& t, std::vector<std::pair<int, SoftKey>> k, juce::StringArray text)
        : Popup (c, t), items (std::move (k)), lines (std::move (text))
    {
        for (auto& i : items) if (i.second.kind == SoftKey::Kind::Value) { selected = i.first; break; }
    }
    void build (Keys& k) override
    {
        for (auto& i : items) k[(size_t) i.first] = i.second;
        k[7] = kb::button ("CLOSE WINDOW", [this] { close(); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (6.0f);
        for (auto& l : lines) oled::text (g, l, r.removeFromTop (16.0f).reduced (6.0f, 0), juce::Justification::centredLeft, 10.0f, false, colours::oledDim.brighter (0.5f));
        r.removeFromTop (6.0f);
        for (auto& i : items)
            if (i.second.kind == SoftKey::Kind::Value && i.second.param != nullptr)
            {
                auto line = r.removeFromTop (30.0f).reduced (6.0f, 2.0f);
                const bool sel = selected == i.first;
                g.setColour (colours::oledText);
                if (sel) g.drawRect (line, 1.0f);
                oled::text (g, i.second.title, line.reduced (6.0f, 0), juce::Justification::centredLeft, 10.5f);
                oled::text (g, sel && entry.isNotEmpty() ? entry + "_" : i.second.param->text(), line.reduced (6.0f, 0), juce::Justification::centredRight, 12.0f, true);
            }
    }
    std::vector<std::pair<int, SoftKey>> items;
    juce::StringArray lines;
};

//==============================================================================
void common::showSampleTools (Ctx& c)
{
    std::vector<MenuPopup::Item> it;
    it.push_back ({ "GET INFO", [&c]
    {
        auto* s = c.sample();
        if (s == nullptr) { c.info ("GET INFO", { "No sample selected" }); return; }
        c.info ("GET INFO", { "Name    : " + s->name,
                              "Length  : " + lengthText (s->numFrames(), s->sampleRate) + "  (" + juce::String (s->numFrames()) + ")",
                              "S/Rate  : " + juce::String (juce::roundToInt (s->sampleRate)) + " Hz  " + (s->isStereo() ? "STEREO" : "MONO"),
                              "Loop    : " + names::playback (s->playMode, false),
                              "Type    : " + juce::String (s->isVirtual ? "VIRTUAL" : "RAM") + "   " + juce::String ((double) s->bytesUsed() / 1048576.0, 2) + " MB" });
    }, 9 });
    it.push_back ({ "CREATE PROG", [&c] { createProgramFromSample (c, c.sample()); }, 10 });
    it.push_back ({ "SAMPLE LIST", [&c] { c.pages->show (PageId::SampleList); }, 11 });
    it.push_back ({ "QUICKLOAD", [&c] { quickLoad (c); }, 12 });
    it.push_back ({ "QUICKSAVE", [&c] { quickSave (c); }, 13 });
    it.push_back ({ "DELETE SAMPLE", [&c]
    {
        auto* s = c.sample();
        if (s == nullptr) return;
        c.confirm ("DELETE SAMPLE", "Delete " + s->name + "?", [&c]
        {
            stopPlay (c);
            c.structural ([&c]
            {
                if (! juce::isPositiveAndBelow (c.curSample, c.mem.samples.size())) return;
                c.proc.retire (c.mem.samples.getObjectPointer (c.curSample));
                c.mem.samples.remove (c.curSample);
            });
        });
    }, 14 });
    it.push_back ({ juce::String ("W/FORM : ") + (c.waveLog ? "LOG" : "LIN"), [&c] { c.waveLog = c.waveLog ? 0 : 1; }, 15 });
    c.pages->pushPopup (std::make_unique<MenuPopup> (c, "SAMPLE TOOLS", it));
}

//==============================================================================
class SamplePage : public Page
{
public:
    SamplePage (Ctx& c, PageId i, PageId parentPage, juce::String t) : Page (c, i, parentPage, Mode::Sample), pageTitle (std::move (t)) {}
    juce::String title() const override { return pageTitle; }

    Sample* S() const { return ctx.sample(); }

    //--------------------------------------------------------------------------
    // edit points shown on the waveform (and dragged with the mouse)
    struct Handle
    {
        int64 pos = 0;
        juce::String label;
        int key = -1;                           // soft key that edits it
        int side = 1;
        int view = 0;                           // waveform it belongs to
        std::function<void (int64)> set;
    };
    virtual std::vector<Handle> handles() { return {}; }

    bool isHandleKey (int k)
    {
        if (k < 0) return false;
        for (auto& h : handles()) if (h.key == k) return true;
        return false;
    }

    /** the edit point the PLAY TO / FROM, ZOOM keys work on */
    int64 focusPos()
    {
        auto hs = handles();
        for (auto& h : hs) if (h.key == selected) return h.pos;
        for (auto& h : hs) if (h.key == focusKey) return h.pos;
        return hs.empty() ? (viewStart + viewEnd) / 2 : hs.front().pos;
    }

    //--------------------------------------------------------------------------
    // view (zoom)
    void resetView()
    {
        auto* s = S();
        viewStart = 0;
        viewEnd = s != nullptr ? juce::jmax (1, s->numFrames()) : 1;
    }

    void syncView()
    {
        auto* s = S();
        const int n = s != nullptr ? s->numFrames() : 0;
        if (s != lastSample || n != lastFrames || viewEnd <= viewStart || viewEnd > juce::jmax (1, n))
        {
            if (s != lastSample || n != lastFrames) sampleChanged();
            lastSample = s; lastFrames = n;
            resetView();
        }
    }
    virtual void sampleChanged() {}

    void zoom (bool in, int64 centre)
    {
        auto* s = S();
        if (s == nullptr) return;
        const int64 n = s->numFrames();
        const int64 span = juce::jmax<int64> (16, viewEnd - viewStart);
        const int64 ns = in ? juce::jmax<int64> (16, span / 2) : juce::jmin<int64> (n, span * 2);
        viewStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, n - ns), centre - ns / 2);
        viewEnd = juce::jmin<int64> (n, viewStart + ns);
    }

    void ensureVisible (int64 p)
    {
        const int64 span = viewEnd - viewStart;
        auto* s = S();
        if (s == nullptr || span >= s->numFrames()) return;
        if (p < viewStart) { viewStart = juce::jmax<int64> (0, p - span / 8); viewEnd = viewStart + span; }
        else if (p > viewEnd) { viewEnd = juce::jmin<int64> (s->numFrames(), p + span / 8); viewStart = viewEnd - span; }
    }

    //--------------------------------------------------------------------------
    // keys
    void commonKeys (Keys& k, bool withMain = true)
    {
        syncView();
        k[0] = sampleHeader (ctx, [this] { syncView(); });
        if (withMain) k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::SampMain); });
        k[8] = button ("SAMPLE TOOLS", [this] { showSampleTools (ctx); });
        if (isHandleKey (selected)) focusKey = selected;
    }

    juce::String frames (int v) const { return juce::String (v); }

    SoftKey markerKey (const juce::String& t, std::function<int64()> get, std::function<void (int64)> set, int maxFrames = -1)
    {
        auto* s = S();
        const int hi = maxFrames >= 0 ? maxFrames : (s != nullptr ? s->numFrames() : 0);
        return valueFn (t, [get] { return (int) get(); },
                        [this, set] (int v) { set (v); ensureVisible (v); },
                        0, hi, [this] (int v) { return frames (v); });
    }

    SoftKey zoomIn()  { return button ("ZOOM IN",  [this] { zoom (true, focusPos()); }); }
    SoftKey zoomOut() { return button ("ZOOM OUT", [this] { zoom (false, focusPos()); }); }

    SoftKey playTo()
    {
        return button ("PLAY TO", [this]
        {
            auto* s = S();
            if (s == nullptr) return;
            const int64 p = focusPos();
            playSample (ctx, Sample::Ptr (s), false, juce::jmax<int64> (0, p - (int64) (s->sampleRate * 2.0)), juce::jmax<int64> (1, p));
        });
    }
    SoftKey playFrom()
    {
        return button ("PLAY FROM", [this]
        {
            auto* s = S();
            if (s == nullptr) return;
            const int64 p = focusPos();
            playSample (ctx, Sample::Ptr (s), false, p, p < s->end ? s->end : s->numFrames());
        });
    }

    void wheel (int delta) override
    {
        if (isHandleKey (selected))
            if (auto& p = keys[(size_t) selected].param)
            {
                const int64 span = juce::jmax<int64> (1, spanForKey (selected));
                const float w = juce::jmax (100.0f, slots[0].rect.getWidth());
                int64 step = juce::jmax<int64> (1, (int64) std::llround ((double) span / w * 2.0));
                if (std::abs (delta) > 1) step *= 3;
                p->adjust ((int) juce::jlimit<int64> (-0x3fffffff, 0x3fffffff, step * delta));
                return;
            }
        Page::wheel (delta);
    }

    void cursor (int dir) override
    {
        // CURSOR = single sample adjustment of the edit points
        if (isHandleKey (selected)) { if (auto& p = keys[(size_t) selected].param) p->adjust (dir); return; }
        Page::cursor (dir);
    }

    void enterPlay (bool down) override
    {
        auto* s = S();
        if (s == nullptr) return;
        if (down) { gAuditioned = s; ctx.engine.audition (Sample::Ptr (s), false, s->start, s->end); }
        else stopPlay (ctx);
    }

    void onHide() override { ctx.engine.setWatchedSample (nullptr); }

    //--------------------------------------------------------------------------
    // drawing
    struct Slot { juce::Rectangle<float> rect; int64 from = 0, to = 1; bool overview = false, zoomable = false; const Sample* sample = nullptr; };
    std::array<Slot, 3> slots;
    int numSlots = 0;

    int64 spanForKey (int key)
    {
        for (auto& h : handles())
            if (h.key == key && h.view < numSlots) return slots[(size_t) h.view].to - slots[(size_t) h.view].from;
        return viewEnd - viewStart;
    }

    wave::Options options (const Sample* s, int64 from, int64 to, int view)
    {
        wave::Options o;
        o.from = from; o.to = to;
        o.log = ctx.waveLog != 0;
        if (s != nullptr)
        {
            o.sampleRate = s->sampleRate;
            o.playing = isPlaying (ctx, s);
            o.playPos = ctx.engine.getAuditionPosition();
            if (s == S())
            {
                juce::int64 vp[8];
                const int nv = ctx.engine.getWatchedPositions (vp, 8);
                for (int i = 0; i < nv; ++i) o.voices.push_back (vp[i]);
            }
        }
        for (auto& h : handles())
            if (h.view == view)
            {
                wave::Marker m;
                m.pos = h.pos; m.label = h.label; m.side = h.side;
                m.selected = h.key == selected;
                o.markers.push_back (m);
            }
        return o;
    }

    /** standard layout: overview strip + zoomed main view (slot 0 = main view, slot 1 = overview) */
    void paintStandard (juce::Graphics& g, juce::Rectangle<float> area, const std::function<void (wave::Options&, bool overview)>& decorate, bool withOverview = true)
    {
        auto* s = S();
        syncView();
        const int64 n = s != nullptr ? juce::jmax (1, s->numFrames()) : 1;
        juce::Rectangle<float> ov;
        if (withOverview)
        {
            ov = area.removeFromTop (juce::jmin (16.0f, area.getHeight() * 0.25f));
            area.removeFromTop (3.0f);
            auto o = options (s, 0, n, -1);
            o.compact = true;
            o.windowFrom = viewStart; o.windowTo = viewEnd;
            if (decorate) decorate (o, true);
            wave::draw (g, ov, s, o, overPeaks);
        }
        auto o = options (s, viewStart, viewEnd, 0);
        o.tagLeft = wave::zoomTag (ctx.waveLog != 0, n, viewEnd - viewStart);
        if (decorate) decorate (o, false);
        wave::draw (g, area, s, o, mainPeaks);

        slots[0] = { area, viewStart, viewEnd, false, true, s };
        numSlots = 1;
        if (withOverview) slots[(size_t) numSlots++] = { ov, 0, n, true, false, s };
    }

    void drawCentreInfo (juce::Graphics& g, juce::Rectangle<float> r)
    {
        drawSampleInfo (g, r, ctx, S());
        r.removeFromTop (4.0f);
        oled::memoryBox (g, r.removeFromTop (96.0f), ctx);
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override { drawCentreInfo (g, r); }

    bool wantsAnimation() override
    {
        auto* s = S();
        ctx.engine.setWatchedSample (s);
        juce::int64 vp[1];
        return isPlaying (ctx, s) || ctx.engine.getWatchedPositions (vp, 1) > 0 || (gAuditioned != nullptr && ctx.engine.isAuditioning());
    }

    //--------------------------------------------------------------------------
    // mouse
    bool mouseDown (juce::Point<float> p, int clicks, bool) override
    {
        dragKey = -1; dragSlot = -1;
        for (int i = 0; i < numSlots; ++i)
        {
            auto& sl = slots[(size_t) i];
            if (! sl.rect.expanded (2.0f).contains (p)) continue;
            dragSlot = i;
            if (sl.overview) { scrollTo (wave::posAt (sl.rect, sl.from, sl.to, p.x)); return true; }
            if (clicks >= 2 && sl.zoomable) { resetView(); return true; }
            // nearest edit point on this view
            float best = 12.0f;
            for (auto& h : handles())
            {
                if (h.view != i || h.pos < sl.from || h.pos > sl.to) continue;
                const float d = std::abs (wave::xOf (sl.rect, sl.from, sl.to, h.pos) - p.x);
                if (d < best) { best = d; dragKey = h.key; }
            }
            if (dragKey < 0)
            {
                // click elsewhere moves the selected edit point of this view
                for (auto& h : handles()) if (h.view == i && (h.key == selected || (dragKey < 0 && h.key == focusKey))) dragKey = h.key;
            }
            if (dragKey >= 0) { selected = focusKey = dragKey; entry.clear(); mouseDrag (p); }
            return true;
        }
        return false;
    }

    void mouseDrag (juce::Point<float> p) override
    {
        if (! juce::isPositiveAndBelow (dragSlot, numSlots)) return;
        auto& sl = slots[(size_t) dragSlot];
        const int64 pos = wave::posAt (sl.rect, sl.from, sl.to, juce::jlimit (sl.rect.getX(), sl.rect.getRight(), p.x));
        if (sl.overview) { scrollTo (pos); return; }
        for (auto& h : handles()) if (h.key == dragKey && h.set) { h.set (pos); break; }
    }

    void mouseUp (juce::Point<float>) override { dragKey = -1; dragSlot = -1; }

    bool mouseWheel (juce::Point<float> p, int delta) override
    {
        for (int i = 0; i < numSlots; ++i)
        {
            auto& sl = slots[(size_t) i];
            if (! sl.rect.contains (p)) continue;
            if (! sl.zoomable) return false;
            const int64 at = wave::posAt (sl.rect, sl.from, sl.to, p.x);
            auto* s = S();
            if (s == nullptr) return true;
            const int64 n = s->numFrames(), span = viewEnd - viewStart;
            const int64 ns = juce::jlimit<int64> (16, n, delta > 0 ? span * 2 / 3 : span * 3 / 2);
            const double rel = (double) (at - viewStart) / (double) juce::jmax<int64> (1, span);
            viewStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, n - ns), at - (int64) (rel * (double) ns));
            viewEnd = juce::jmin<int64> (n, viewStart + ns);
            return true;
        }
        return false;
    }

    void scrollTo (int64 centre)
    {
        auto* s = S();
        if (s == nullptr) return;
        const int64 span = viewEnd - viewStart;
        viewStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, s->numFrames() - span), centre - span / 2);
        viewEnd = viewStart + span;
    }

    juce::String pageTitle;
    int64 viewStart = 0, viewEnd = 1;
    Sample* lastSample = nullptr;
    int lastFrames = -1;
    int focusKey = -1, dragKey = -1, dragSlot = -1;
    wave::Peaks mainPeaks, overPeaks, extraPeaks, extraPeaks2;
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

    void enterPlay (bool down) override
    {
        if (ctx.sampleMonitor == 0) { SamplePage::enterPlay (down); return; }
        Page::enterPlay (down);
    }

    bool wantsAnimation() override { return false; }     // no waveform on this page
};

//==============================================================================
class SampMasterPage : public SamplePage
{
public:
    explicit SampMasterPage (Ctx& c) : SamplePage (c, PageId::SampMaster, PageId::SampMain, "MASTER") {}
    void build (Keys& k) override
    {
        commonKeys (k);
        k[1] = value ("Original Note", [this]() -> int* { auto* s = S(); return s ? &s->rootNote : nullptr; }, 0, 127, note (ctx));
        k[2] = value ("Semitone Tune", [this]() -> int* { auto* s = S(); return s ? &s->semitone : nullptr; }, -36, 36, signed2());
        k[3] = value ("Fine Tune", [this]() -> int* { auto* s = S(); return s ? &s->cents : nullptr; }, -50, 50, signed2());
        k[4] = button ("MAKE MONO", [this]
        {
            auto* s = S();
            if (s != nullptr && ! s->isStereo()) { ctx.info ("MAKE MONO", { "Cannot process - the selected", "sample is already mono" }); return; }
            processCopy (ctx, "MAKE MONO", s, [] (Sample& x) { ops::makeMono (x); });
        });
        k[9] = valueFn ("Re-Scale Level", [this] { return rescaleDb; }, [this] (int v) { rescaleDb = v; }, -48, 24, dbText);
        k[10] = button ("RE-SCALE", [this] { const float d = (float) rescaleDb; processCopy (ctx, "RE-SCALE", S(), [d] (Sample& x) { ops::rescale (x, d); }); });
        k[11] = button (isPlaying (ctx, S()) ? u8 ("\xe2\x96\xa0 STOP") : u8 ("\xe2\x96\xb6 PLAY"), [this] { if (auto* s = S()) playSample (ctx, Sample::Ptr (s), false, s->start, s->end); });
        k[12] = button ("NORMALISE", [this] { processCopy (ctx, "NORMALISE", S(), [] (Sample& x) { ops::normalise (x, 0.0f); }); });
        k[15] = button ("REVERSE", [this] { processCopy (ctx, "REVERSE", S(), [] (Sample& x) { ops::reverse (x, 0, x.numFrames()); }); });
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        paintStandard (g, waveArea (lcd), [s] (wave::Options& o, bool)
        {
            if (s == nullptr) return;
            o.hlFrom = s->start; o.hlTo = s->end;
            if (s->loopEnabled) { o.loopStyle = wave::LoopStyle::Invert; o.loopFrom = s->loopStart; o.loopTo = s->loopEnd; }
            o.tagLeft = "PEAK " + juce::String (juce::Decibels::gainToDecibels (ops::peakLevel (*s)), 1) + " dB";
        }, false);
        slots[0].zoomable = false;
    }

    bool wantsAnimation() override
    {
        const bool p = isPlaying (ctx, S());
        if (p != wasPlaying) { wasPlaying = p; ctx.pages->refresh(); }    // PLAY <-> STOP key text
        return SamplePage::wantsAnimation();
    }

private:
    int rescaleDb = 0;
    bool wasPlaying = false;
};

//==============================================================================
class SampTrimPage : public SamplePage
{
public:
    explicit SampTrimPage (Ctx& c) : SamplePage (c, PageId::SampTrim, PageId::SampMain, "TRIM") {}

    std::vector<Handle> handles() override
    {
        auto* s = S();
        if (s == nullptr) return {};
        return { { s->start, "S", 1, 1, 0, [this] (int64 v) { setStart (v); } },
                 { s->end,   "E", 9, -1, 0, [this] (int64 v) { setEnd (v); } } };
    }

    void setStart (int64 v) { if (auto* s = S()) s->start = juce::jlimit<int64> (0, juce::jmax<int64> (0, s->end - 1), v); }
    void setEnd (int64 v)   { if (auto* s = S()) s->end = juce::jlimit<int64> (juce::jmin<int64> (s->numFrames(), s->start + 1), s->numFrames(), v); }

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        if (selected < 0) selected = 1;
        k[1] = markerKey ("Start", [this] { auto* x = S(); return x ? x->start : 0; }, [this] (int64 v) { setStart (v); });
        k[9] = markerKey ("End", [this] { auto* x = S(); return x ? x->end : 0; }, [this] (int64 v) { setEnd (v); });
        k[2] = button ("FIND START", [this] { findWindow (true); }, s != nullptr);
        k[10] = button ("FIND END", [this] { findWindow (false); }, s != nullptr);
        k[3] = playTo();
        k[4] = playFrom();
        k[11] = zoomIn();
        k[12] = zoomOut();
        k[15] = button ("TRIM SAMPLE", [this]
        {
            auto* x = S();
            if (x == nullptr) return;
            const int64 a = x->start, b = x->end, ls = x->loopStart, le = x->loopEnd;
            std::vector<MenuPopup::Item> it;
            it.push_back ({ "DISCARD", [this, a, b] { processCopy (ctx, "TRIM", S(), [a, b] (Sample& y) { ops::discardOutside (y, a, b); }); }, 9 });
            it.push_back ({ u8 ("LOOP START\xe2\x86\x92" "END"), [this, ls, b] { processCopy (ctx, "TRIM", S(), [ls, b] (Sample& y) { ops::discardOutside (y, ls, b); }); }, 10, x->loopEnabled });
            it.push_back ({ u8 ("START\xe2\x86\x92LOOP END"), [this, a, le] { processCopy (ctx, "TRIM", S(), [a, le] (Sample& y) { ops::discardOutside (y, a, le); }); }, 11, x->loopEnabled });
            ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "TRIM MENU", it));
        }, s != nullptr);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        paintStandard (g, waveArea (lcd), [s] (wave::Options& o, bool)
        {
            if (s == nullptr) return;
            o.hlFrom = s->start; o.hlTo = s->end;           // everything happens between START and END
            if (s->loopEnabled) { o.loopStyle = wave::LoopStyle::Band; o.loopFrom = s->loopStart; o.loopTo = s->loopEnd; }
        });
    }

private:
    int thresholdDb = -60;

    void findWindow (bool start)
    {
        auto th = valueFn ("Threshold", [this] { return thresholdDb; }, [this] (int v) { thresholdDb = v; }, -90, 0, [] (int v) { return juce::String (v) + " dB"; });
        auto go = button (start ? "FIND START" : "FIND END", [this, start]
        {
            auto* s = S();
            if (s == nullptr) return;
            if (start) setStart (ops::findLevel (*s, (float) thresholdDb, true, 0, s->end));
            else       setEnd (ops::findLevel (*s, (float) thresholdDb, false, s->start, s->numFrames()));
            selected = focusKey = start ? 1 : 9;
            ensureVisible (start ? s->start : s->end);
        });
        ctx.pages->pushPopup (std::make_unique<ToolWindow> (ctx, start ? "FIND START" : "FIND END",
            std::vector<std::pair<int, SoftKey>> { { 1, th }, { 3, go } },
            juce::StringArray { "Set the level at which", start ? "the sound starts." : "the sound ends." }));
    }
};

//==============================================================================
class SampChopPage : public SamplePage
{
public:
    explicit SampChopPage (Ctx& c) : SamplePage (c, PageId::SampChop, PageId::SampMain, "CHOP") {}

    void sampleChanged() override
    {
        auto* s = S();
        a = s ? s->start : 0;
        b = s ? s->end : 0;
    }

    std::vector<Handle> handles() override
    {
        if (S() == nullptr) return {};
        return { { a, "S", 1, 1, 0, [this] (int64 v) { setA (v); } },
                 { b, "E", 9, -1, 0, [this] (int64 v) { setB (v); } } };
    }
    void setA (int64 v) { if (S() != nullptr) a = juce::jlimit<int64> (0, juce::jmax<int64> (0, b - 1), v); }
    void setB (int64 v) { if (auto* s = S()) b = juce::jlimit<int64> (juce::jmin<int64> (s->numFrames(), a + 1), s->numFrames(), v); }

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        if (selected < 0) selected = 1;
        k[1] = markerKey ("Start", [this] { return a; }, [this] (int64 v) { setA (v); });
        k[9] = markerKey ("End", [this] { return b; }, [this] (int64 v) { setB (v); });
        k[2] = button ("PLAY REGION", [this] { if (auto* x = S()) playSample (ctx, Sample::Ptr (x), false, a, b); }, s != nullptr);
        k[3] = playTo();
        k[4] = playFrom();
        k[10] = valueFn ("Slices", [this] { return slices; }, [this] (int v) { slices = v == 1 ? (slices == 0 ? 2 : 0) : v; }, 0, 64,
                         [] (int v) { return v < 2 ? juce::String ("OFF") : juce::String (v); });
        k[11] = zoomIn();
        k[12] = zoomOut();
        k[15] = button ("CHOP SAMPLE", [this]
        {
            auto* x = S();
            if (x == nullptr) return;
            const int64 ca = a, cb = b;
            std::vector<MenuPopup::Item> it;
            it.push_back ({ "EXTRACT", [this, ca, cb]
            {
                auto* y = S();
                if (y == nullptr) return;
                ProcessJob j;
                j.title = "EXTRACT";
                Sample::Ptr orig (y);
                j.originals = { orig };
                j.newName = ctx.mem.uniqueSampleName (y->name);
                const auto nn = j.newName;
                j.run = [orig, ca, cb, nn] { return ops::extract (*orig, ca, cb, nn); };
                startProcess (ctx, std::move (j));
            }, 9 });
            it.push_back ({ "ERASE", [this, ca, cb] { processCopy (ctx, "ERASE", S(), [ca, cb] (Sample& y) { ops::erase (y, ca, cb); }); }, 11 });
            it.push_back ({ "CUT", [this, ca, cb] { processCopy (ctx, "CUT", S(), [ca, cb] (Sample& y) { ops::cut (y, ca, cb); }); }, 13 });
            if (slices >= 2)
                it.push_back ({ "SLICE > PROGRAM", [this] { doSlices(); }, 10 });
            ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "CUT/ERASE MENU", it));
        }, s != nullptr);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        auto pts = slicePoints();
        paintStandard (g, waveArea (lcd), [this, s, pts] (wave::Options& o, bool)
        {
            if (s == nullptr) return;
            o.hlFrom = a; o.hlTo = b;
            if (s->loopEnabled) { o.loopStyle = wave::LoopStyle::Band; o.loopFrom = s->loopStart; o.loopTo = s->loopEnd; }
            if (pts.size() > 2) o.slices.assign (pts.begin() + 1, pts.end() - 1);
        });
    }

private:
    int64 a = 0, b = 0;
    int slices = 0;

    std::vector<int64> slicePoints() const
    {
        std::vector<int64> p;
        if (slices < 2 || b <= a) return p;
        for (int i = 0; i <= slices; ++i) p.push_back (a + (b - a) * i / slices);
        return p;
    }

    void doSlices()
    {
        auto* s = S();
        auto pts = slicePoints();
        if (s == nullptr || pts.size() < 3) return;
        auto parts = ops::chop (*s, pts, s->name);
        ctx.structural ([&]
        {
            juce::StringArray nm;
            for (auto& p : parts) { p->name = ctx.mem.uniqueSampleName (p->name); nm.add (p->name); ctx.mem.samples.add (p); }
            Program::Ptr prg = new Program();
            prg->setName (ctx.mem.uniqueProgramName (s->name.substring (0, 14) + " CHOP"));
            for (int i = 0; i < nm.size() && i < 99; ++i)
            {
                fmt::KeygroupData kgd;
                kgd.lowNote = kgd.highNote = juce::jmin (127, 36 + i);       // from C1 upwards (drum pads)
                kgd.zones[0].sampleName = nm[i].toStdString();
                kgd.zones[0].keyTrack = false;
                kgd.zones[0].playback = fmt::PlayOneShot;
                prg->data.keygroups.push_back (kgd);
            }
            ctx.mem.programs.add (prg);
            ctx.curProgram = ctx.mem.programs.size() - 1;
        });
        ctx.info ("CHOP", { juce::String ((int) parts.size()) + " samples created", "+ program (C1 upwards)" });
    }
};

//==============================================================================
class SampLoopPage : public SamplePage
{
public:
    explicit SampLoopPage (Ctx& c) : SamplePage (c, PageId::SampLoop, PageId::SampMain, "LOOP") {}

    std::vector<Handle> handles() override
    {
        auto* s = S();
        if (s == nullptr) return {};
        return { { s->loopStart, "LS", 1, 1, 0, [this] (int64 v) { setLoopStart (v); } },
                 { s->loopEnd,   "LE", 9, -1, 0, [this] (int64 v) { setLoopEnd (v); } } };
    }

    void setLoopStart (int64 v)
    {
        auto* s = S();
        if (s == nullptr) return;
        const int64 len = s->loopEnd - s->loopStart;
        if (s->loopLock)
        {
            s->loopStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, s->numFrames() - len), v);
            s->loopEnd = s->loopStart + len;
        }
        else s->loopStart = juce::jlimit<int64> (0, juce::jmax<int64> (0, s->loopEnd - 2), v);
        s->sanitiseMarkers();
    }
    void setLoopEnd (int64 v)
    {
        auto* s = S();
        if (s == nullptr) return;
        const int64 len = s->loopEnd - s->loopStart;
        if (s->loopLock)
        {
            s->loopEnd = juce::jlimit<int64> (len, s->numFrames(), v);
            s->loopStart = s->loopEnd - len;
        }
        else s->loopEnd = juce::jlimit<int64> (juce::jmin<int64> (s->numFrames(), s->loopStart + 2), s->numFrames(), v);
        s->sanitiseMarkers();
    }

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        if (selected < 0) selected = 1;
        k[1] = markerKey ("Loop Start", [this] { auto* x = S(); return x ? x->loopStart : 0; }, [this] (int64 v) { setLoopStart (v); });
        k[9] = markerKey ("Loop End", [this] { auto* x = S(); return x ? x->loopEnd : 0; }, [this] (int64 v) { setLoopEnd (v); });

        auto xf = valueFn ("X-Fade Len", [this] { auto* x = S(); return x ? x->loopXfade : 0; },
                           [this] (int v) { if (auto* x = S()) { x->loopXfade = v; x->sanitiseMarkers(); } },
                           0, s ? (int) juce::jmin<int64> (s->loopEnd - s->loopStart, s->loopStart) : 0,
                           [this] (int v) { auto* x = S(); return juce::String (v) + (x ? "  " + juce::String (juce::roundToInt (1000.0 * v / x->sampleRate)) + "ms" : juce::String()); });
        k[2] = xf;

        auto profile = std::make_shared<Param>();
        profile->get = [this] { return fadeProfile; };
        profile->set = [this] (int v) { fadeProfile = v; };
        profile->min = 0; profile->max = 1;
        profile->format = [] (int v) { return v ? juce::String ("EQUAL POWER") : juce::String ("LINEAR"); };
        k[3] = button ("CROSSFADE", [this]
        {
            auto* x = S();
            if (x == nullptr) return;
            if (x->loopXfade < 2) { ctx.info ("CROSSFADE", { "Set an X-Fade Length first" }); return; }
            const int len = x->loopXfade, pr = fadeProfile;
            processCopy (ctx, "CROSSFADE", x, [len, pr] (Sample& y) { ops::crossfadeLoop (y, len, pr); });
        }, s != nullptr);
        k[3].windowA = profile; k[3].windowB = xf.param;
        k[3].windowTitle = "CROSSFADE"; k[3].windowLabelA = "Fade Profile"; k[3].windowLabelB = "Xfade Length";

        k[4] = button ("PLAY LOOP", [this] { if (auto* x = S()) playSample (ctx, Sample::Ptr (x), true); }, s != nullptr);

        // PLAY MODE (WINDOW: LOOP LOCK, LOOP DIRECTION)
        auto pm = valueFn ("Play Mode", [this] { auto* x = S(); return x ? x->playMode : 0; },
                           [this] (int v)
                           {
                               if (auto* x = S())
                               {
                                   x->playMode = v;
                                   x->loopEnabled = (v == fmt::PlayLoopInRelease || v == fmt::PlayLoopUntilRelease);
                                   x->sanitiseMarkers();
                               }
                           }, 0, 3, [] (int v) { return names::playback (v, false); });
        auto lock = std::make_shared<Param>();
        lock->get = [this] { auto* x = S(); return x && x->loopLock ? 1 : 0; };
        lock->set = [this] (int v) { if (auto* x = S()) x->loopLock = v != 0; };
        lock->min = 0; lock->max = 1;
        lock->format = [] (int v) { return v ? juce::String ("ON") : juce::String ("OFF"); };
        auto dir = std::make_shared<Param>();
        dir->get = [this] { auto* x = S(); return x ? x->loopDirection : 0; };
        dir->set = [this] (int v) { if (auto* x = S()) x->loopDirection = v; };
        dir->min = 0; dir->max = 1;
        dir->format = [] (int v) { return v ? juce::String ("ALTERNATING") : juce::String ("FORWARDS"); };
        pm.windowA = lock; pm.windowB = dir;
        pm.windowTitle = "PLAY MODE"; pm.windowLabelA = "Loop Lock"; pm.windowLabelB = "Loop Direction";
        k[10] = pm;

        k[11] = zoomIn();
        k[12] = zoomOut();
        k[15] = button ("AUTO FIND", [this]
        {
            auto* x = S();
            if (x == nullptr) return;
            setLoopEnd (ops::autoFindLoopEnd (*x, x->loopStart, x->loopEnd, (int) (x->sampleRate * 0.02)));
            selected = focusKey = 9;
            ensureVisible (x->loopEnd);
        }, s != nullptr);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        auto area = waveArea (lcd);
        auto labelRow = area.removeFromTop (16.0f);
        area.removeFromTop (2.0f);
        const float splitX = lcd.getRight() - geo::margin - geo::boxW;
        auto right = area.withLeft (splitX);
        auto left = area.withRight (splitX - 6.0f);

        // "Loop length : 1.49s"  |  "End -> | <- Start"
        oled::text (g, "Loop length :  " + (s ? lengthText (s->loopEnd - s->loopStart, s->sampleRate) : juce::String ("---")),
                    labelRow.withRight (splitX - 6.0f), juce::Justification::centred, 10.5f);
        auto lab = labelRow.withLeft (splitX);
        g.setColour (colours::oledText);
        g.drawRect (lab, 1.0f);
        g.drawVerticalLine ((int) lab.getCentreX(), lab.getY(), lab.getBottom());
        oled::text (g, u8 ("End \xe2\x86\x92"), lab.withRight (lab.getCentreX()), juce::Justification::centred, 9.5f);
        oled::text (g, u8 ("\xe2\x86\x90 Start"), lab.withLeft (lab.getCentreX()), juce::Justification::centred, 9.5f);

        paintStandard (g, left, [s] (wave::Options& o, bool)
        {
            if (s == nullptr) return;
            o.hlFrom = s->loopStart; o.hlTo = s->loopEnd;
            o.showStatus = true;
        });

        // splice view: the end of the loop butted against its start (what you hear at the join)
        g.setColour (colours::oledDim);
        g.drawRect (right, 1.0f);
        if (s == nullptr) return;
        const int64 half = juce::jlimit<int64> (24, 16384, (viewEnd - viewStart) / 16);
        auto lr = right.withWidth (right.getWidth() * 0.5f), rr = right.withLeft (right.getCentreX());
        wave::Options o1, o2;
        o1.log = o2.log = false;
        o1.showStatus = o2.showStatus = false;
        o1.sampleRate = o2.sampleRate = s->sampleRate;
        o1.from = juce::jmax<int64> (0, s->loopEnd - half); o1.to = juce::jmax<int64> (o1.from + 1, s->loopEnd);
        o2.from = s->loopStart; o2.to = juce::jmin<int64> (s->numFrames(), s->loopStart + half);
        if (isPlaying (ctx, s)) { o1.playing = o2.playing = true; o1.playPos = o2.playPos = ctx.engine.getAuditionPosition(); }
        wave::draw (g, lr, s, o1, extraPeaks);
        wave::draw (g, rr, s, o2, extraPeaks2);
        g.setColour (colours::oledWarn);
        g.fillRect (juce::Rectangle<float> (right.getCentreX() - 0.5f, right.getY(), 1.0f, right.getHeight()));
    }

private:
    int fadeProfile = 0;
};

//==============================================================================
class SampTwoSamplePage : public SamplePage
{
public:
    SampTwoSamplePage (Ctx& c, PageId i, juce::String t, bool mixMode) : SamplePage (c, i, PageId::SampMain, std::move (t)), mix (mixMode) {}

    Sample* B() const { return juce::isPositiveAndBelow (bIndex, ctx.mem.samples.size()) ? ctx.mem.samples.getObjectPointer (bIndex) : nullptr; }

    void syncRanges()
    {
        auto* a = S(); auto* b = B();
        if (a != lastA || (a != nullptr && a->numFrames() != lastAN)) { lastA = a; lastAN = a ? a->numFrames() : 0; aFrom = a ? a->start : 0; aTo = a ? a->end : 0; }
        if (b != lastB || (b != nullptr && b->numFrames() != lastBN)) { lastB = b; lastBN = b ? b->numFrames() : 0; bFrom = b ? b->start : 0; bTo = b ? b->end : 0; }
    }

    std::vector<Handle> handles() override
    {
        std::vector<Handle> h;
        if (S() != nullptr)
        {
            h.push_back ({ aFrom, "F", 1, 1, 0, [this] (int64 v) { setAF (v); } });
            h.push_back ({ aTo, "T", 2, -1, 0, [this] (int64 v) { setAT (v); } });
        }
        if (B() != nullptr)
        {
            h.push_back ({ bFrom, "F", 9, 1, 1, [this] (int64 v) { setBF (v); } });
            h.push_back ({ bTo, "T", 10, -1, 1, [this] (int64 v) { setBT (v); } });
        }
        return h;
    }
    void setAF (int64 v) { aFrom = juce::jlimit<int64> (0, juce::jmax<int64> (0, aTo - 1), v); }
    void setAT (int64 v) { if (auto* a = S()) aTo = juce::jlimit<int64> (juce::jmin<int64> (a->numFrames(), aFrom + 1), a->numFrames(), v); }
    void setBF (int64 v) { bFrom = juce::jlimit<int64> (0, juce::jmax<int64> (0, bTo - 1), v); }
    void setBT (int64 v) { if (auto* b = B()) bTo = juce::jlimit<int64> (juce::jmin<int64> (b->numFrames(), bFrom + 1), b->numFrames(), v); }

    void build (Keys& k) override
    {
        bIndex = juce::jlimit (0, juce::jmax (0, ctx.mem.samples.size() - 1), bIndex);
        syncRanges();
        k[0] = sampleHeader (ctx);
        k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::SampMain); });
        if (isHandleKey (selected)) focusKey = selected;

        // F9 : sample B (DATA wheel)
        auto pb = std::make_shared<Param>();
        pb->get = [this] { return bIndex; };
        pb->set = [this] (int v) { bIndex = v; syncRanges(); };
        pb->min = 0; pb->max = juce::jmax (0, ctx.mem.samples.size() - 1);
        pb->format = [this] (int) { auto* b = B(); return b ? b->name : juce::String ("(NO SAMPLES)"); };
        k[8] = header ({}, pb);

        auto* a = S(); auto* b = B();
        if (selected < 0) selected = 1;
        k[1] = markerKey ("Sample A From", [this] { return aFrom; }, [this] (int64 v) { setAF (v); }, a ? a->numFrames() : 0);
        k[2] = markerKey ("Sample A To", [this] { return aTo; }, [this] (int64 v) { setAT (v); }, a ? a->numFrames() : 0);
        k[3] = valueFn ("Sample A Level", [this] { return levelA; }, [this] (int v) { levelA = v; }, -48, 12, dbText);
        k[9] = markerKey ("Sample B From", [this] { return bFrom; }, [this] (int64 v) { setBF (v); }, b ? b->numFrames() : 0);
        k[10] = markerKey ("Sample B To", [this] { return bTo; }, [this] (int64 v) { setBT (v); }, b ? b->numFrames() : 0);
        k[11] = valueFn ("Sample B Level", [this] { return levelB; }, [this] (int v) { levelB = v; }, -48, 12, dbText);
        k[5] = button ("PLAY SAMPLE A", [this] { if (auto* x = S()) playSample (ctx, Sample::Ptr (x), false, aFrom, aTo); }, a != nullptr);
        k[6] = button ("PLAY SAMPLE B", [this] { if (auto* x = B()) playSample (ctx, Sample::Ptr (x), false, bFrom, bTo); }, b != nullptr);

        if (mix)
        {
            k[13] = button ("MAKE STEREO", [this] { execute (true); }, a != nullptr && b != nullptr);
            k[15] = button ("MIX SAMPLES", [this] { execute (false); }, a != nullptr && b != nullptr);
        }
        else
        {
            SoftKey ab; ab.kind = SoftKey::Kind::Check; ab.title = u8 ("JOIN A\xe2\x86\x92" "B");
            ab.checked = [this] { return order == 0; }; ab.onPress = [this] { order = 0; };
            SoftKey ba; ba.kind = SoftKey::Kind::Check; ba.title = u8 ("JOIN B\xe2\x86\x92" "A");
            ba.checked = [this] { return order == 1; }; ba.onPress = [this] { order = 1; };
            k[4] = ab; k[12] = ba;
            k[13] = valueFn ("X-Fade Length", [this] { return xfadeMs; }, [this] (int v) { xfadeMs = v; }, 0, 10000, [] (int v) { return juce::String (v) + " ms"; });
            k[14] = valueFn ("X-Fade Curve", [this] { return xfadeCurve; }, [this] (int v) { xfadeCurve = v; }, 0, 2, list ({ "LINEAR", "LOG", "SINE" }));
            k[15] = button ("JOIN SAMPLES", [this] { execute (false); }, a != nullptr && b != nullptr);
        }
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r = r.withTop (r.getY() - 6.0f);
        drawSampleInfo (g, r, ctx, S(), " A");
        drawSampleInfo (g, r, ctx, B(), " B");
        r.removeFromTop (2.0f);
        drawCompactMemory (g, r.removeFromTop (48.0f), ctx);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        syncRanges();
        auto area = centreWaveArea (lcd);
        auto top = area.removeFromTop (area.getHeight() * 0.5f - 1.0f);
        area.removeFromTop (2.0f);
        auto* a = S(); auto* b = B();
        numSlots = 0;
        auto drawOne = [&] (Sample* s, juce::Rectangle<float> r, int64 f, int64 t, int view, wave::Peaks& pk)
        {
            const int64 n = s != nullptr ? s->numFrames() : 1;
            auto o = options (s, 0, n, view);
            o.hlFrom = f; o.hlTo = t;
            o.showStatus = false;
            wave::draw (g, r, s, o, pk);
            slots[(size_t) numSlots++] = { r, 0, n, false, false, s };
        };
        drawOne (a, top, aFrom, aTo, 0, extraPeaks);
        drawOne (b, area, bFrom, bTo, 1, extraPeaks2);
        // arrows from PLAY SAMPLE A / B and the display style, as on the hardware
        oled::text (g, u8 ("\xe2\x86\x92"), top.withX (top.getX() - 12.0f).withWidth (10.0f), juce::Justification::centred, 10.0f);
        oled::text (g, u8 ("\xe2\x86\x92"), area.withX (area.getX() - 12.0f).withWidth (10.0f), juce::Justification::centred, 10.0f);
        oled::text (g, ctx.waveLog ? "LOG" : "LIN", juce::Rectangle<float> (top.getX() - 40.0f, top.getBottom() - 6.0f, 36.0f, 12.0f), juce::Justification::centredRight, 8.5f, false, colours::oledDim.brighter (0.5f));
    }

private:
    bool mix;
    int bIndex = 0, levelA = 0, levelB = 0, order = 0, xfadeMs = 0, xfadeCurve = 0;
    int64 aFrom = 0, aTo = 0, bFrom = 0, bTo = 0;
    Sample* lastA = nullptr; Sample* lastB = nullptr;
    int lastAN = -1, lastBN = -1;

    void execute (bool stereo)
    {
        auto* a = S(); auto* b = B();
        if (a == nullptr || b == nullptr) return;
        if (std::abs (a->sampleRate - b->sampleRate) > 0.5)
        {
            ctx.info ("CANNOT PROCESS", { "The two selected samples have", "different sample rates" });
            return;
        }
        if (stereo && (a->isStereo() || b->isStereo()))
        {
            ctx.info ("CANNOT PROCESS", { "the selected sample is", "already stereo" });
            return;
        }
        Sample::Ptr pa (a), pb (b);
        ops::Range ra { pa.get(), aFrom, aTo, (float) levelA }, rb { pb.get(), bFrom, bTo, (float) levelB };
        ProcessJob j;
        j.originals = { pa, pb };
        if (mix)
        {
            j.title = stereo ? "MAKE STEREO" : "MIX";
            j.newName = ctx.mem.uniqueSampleName (stereo ? "STEREO MIX" : "MIX");
            const auto nn = j.newName;
            if (stereo) j.run = [pa, pb, ra, rb, nn] { return ops::makeStereo (ra, rb, nn); };
            else        j.run = [pa, pb, ra, rb, nn] { return ops::mixRanges (ra, rb, nn); };
        }
        else
        {
            j.title = "JOIN";
            j.newName = ctx.mem.uniqueSampleName ("JOIN");
            const auto nn = j.newName;
            const int64 xf = (int64) (xfadeMs * 0.001 * a->sampleRate);
            const int curve = xfadeCurve;
            const bool bFirst = order == 1;
            j.run = [pa, pb, ra, rb, nn, xf, curve, bFirst] { return bFirst ? ops::joinRanges (rb, ra, xf, curve, nn) : ops::joinRanges (ra, rb, xf, curve, nn); };
        }
        startProcess (ctx, std::move (j));
    }
};

//==============================================================================
class SampFadePage : public SamplePage
{
public:
    explicit SampFadePage (Ctx& c) : SamplePage (c, PageId::SampFade, PageId::SampMain, "FADE") {}

    void sampleChanged() override
    {
        auto* s = S();
        up = 0;
        down = s ? s->numFrames() : 0;
    }

    std::vector<Handle> handles() override
    {
        if (S() == nullptr) return {};
        return { { up, "UP", 1, 1, 0, [this] (int64 v) { setUp (v); } },
                 { down, "DN", 9, -1, 0, [this] (int64 v) { setDown (v); } } };
    }
    void setUp (int64 v)   { if (auto* s = S()) up = juce::jlimit<int64> (0, juce::jmin<int64> (down, s->numFrames()), v); }
    void setDown (int64 v) { if (auto* s = S()) down = juce::jlimit<int64> (up, s->numFrames(), v); }

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        if (selected < 0) selected = 1;
        k[1] = markerKey ("Fade Up", [this] { return up; }, [this] (int64 v) { setUp (v); });
        k[9] = markerKey ("Fade Down", [this] { return down; }, [this] (int64 v) { setDown (v); });
        k[2] = valueFn ("Fade Curve", [this] { return upCurve; }, [this] (int v) { upCurve = v; }, 0, 2, list ({ "LINEAR", "LOG", "SINE" }));
        k[10] = valueFn ("Fade Curve", [this] { return downCurve; }, [this] (int v) { downCurve = v; }, 0, 2, list ({ "LINEAR", "LOG", "SINE" }));
        k[3] = button (isPlaying (ctx, s) ? u8 ("\xe2\x96\xa0 STOP") : u8 ("\xe2\x96\xb6 PLAY"), [this] { if (auto* x = S()) playSample (ctx, Sample::Ptr (x), false, 0, x->numFrames()); }, s != nullptr);
        k[11] = zoomIn();
        k[12] = zoomOut();
        k[15] = button ("FADE SAMPLE", [this]
        {
            const int64 u = up, d = down;
            const int uc = upCurve, dc = downCurve;
            processCopy (ctx, "FADE", S(), [u, d, uc, dc] (Sample& x) { ops::fadeUpDown (x, u, uc, d, dc); });
        }, s != nullptr);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        const int64 u = up, d = down;
        const int uc = upCurve, dc = downCurve;
        paintStandard (g, waveArea (lcd), [s, u, d, uc, dc] (wave::Options& o, bool overview)
        {
            if (s == nullptr) return;
            const int64 n = s->numFrames();
            o.overlay = [n, u, d, uc, dc, overview, from = o.from, to = o.to] (juce::Graphics& gg, juce::Rectangle<float> r, const std::function<float (int64)>& xOf)
            {
                // gain envelope + darkening of the faded part
                auto gainAt = [&] (int64 p)
                {
                    float gn = 1.0f;
                    if (p < u) gn *= ops::curveGain ((double) p / (double) juce::jmax<int64> (1, u), uc);
                    if (p > d) gn *= ops::curveGain (1.0 - (double) (p - d) / (double) juce::jmax<int64> (1, n - d), dc);
                    return gn;
                };
                juce::Path env, shade;
                const int cols = juce::jmax (2, (int) r.getWidth());
                for (int x = 0; x <= cols; ++x)
                {
                    const int64 p = from + (to - from) * x / cols;
                    const float y = r.getBottom() - r.getHeight() * gainAt (p);
                    const float px = r.getX() + (float) x * r.getWidth() / (float) cols;
                    if (x == 0) { env.startNewSubPath (px, y); shade.startNewSubPath (px, r.getY()); }
                    else env.lineTo (px, y);
                    shade.lineTo (px, y);
                }
                shade.lineTo (r.getRight(), r.getY());
                shade.closeSubPath();
                gg.setColour (colours::oledBg.withAlpha (0.55f));
                gg.fillPath (shade);
                gg.setColour (colours::oledWarn.withAlpha (overview ? 0.6f : 0.9f));
                gg.strokePath (env, juce::PathStrokeType (overview ? 1.0f : 1.5f));
                juce::ignoreUnused (xOf);
            };
        });
    }

    bool wantsAnimation() override
    {
        const bool p = isPlaying (ctx, S());
        if (p != wasPlaying) { wasPlaying = p; ctx.pages->refresh(); }
        return SamplePage::wantsAnimation();
    }

private:
    int64 up = 0, down = 0;
    int upCurve = 0, downCurve = 0;
    bool wasPlaying = false;
};

//==============================================================================
class SampProcessPage : public SamplePage
{
public:
    enum Kind { Stretch, Pitch, Bpm, Resample, Eq };
    SampProcessPage (Ctx& c, PageId i, juce::String t, Kind kd) : SamplePage (c, i, PageId::SampMain, std::move (t)), kind (kd) {}

    void sampleChanged() override
    {
        if (auto* s = S())
        {
            newRate = (int) s->sampleRate;
            double bpm = ops::estimateBpm (*s, 4);
            while (bpm < 60.0) bpm *= 2.0;
            while (bpm > 200.0) bpm *= 0.5;
            sourceBpm = (int) std::round (bpm * 100.0);
            if (newBpm <= 0) newBpm = sourceBpm;
        }
    }

    void build (Keys& k) override
    {
        commonKeys (k);
        auto* s = S();
        if (selected < 0) selected = 1;
        auto presetKey = [this]
        {
            return valueFn ("Preset", [this] { return preset; }, [this] (int v) { preset = v; }, 0, ops::numStretchPresets - 1,
                            [] (int v) { return ops::stretchPresetName (v); });
        };
        switch (kind)
        {
            case Stretch:
            {
                k[1] = valueFn ("Stretch Amount", [this] { return stretch; }, [this] (int v) { stretch = v; }, 5000, 20000,
                                [] (int v) { return juce::String (v / 100.0, 2) + "%"; });
                k[2] = label ("New Length", s ? lengthText ((int64) (s->numFrames() * stretch / 10000.0), s->sampleRate) : juce::String());
                k[9] = presetKey();
                k[15] = button ("STRETCH SAMPLE", [this]
                {
                    const double r = stretch / 10000.0; const int p = preset;
                    processCopy (ctx, "TIMESTRETCH", S(), [r, p] (Sample& x) { ops::timeStretchPreset (x, r, p); });
                }, s != nullptr);
                break;
            }
            case Pitch:
            {
                k[1] = valueFn ("Semitone Tune", [this] { return semis; }, [this] (int v) { semis = v; }, -24, 24, signed2());
                k[2] = valueFn ("Fine Tune", [this] { return fine; }, [this] (int v) { fine = v; }, -50, 50, signed2());
                k[9] = presetKey();
                k[10] = valueFn ("Auto Tune", [this] { return autoTune; }, [this] (int v) { autoTune = v; }, 0, 1, onOff());
                k[15] = button ("RETUNE SAMPLE", [this]
                {
                    const float st = (float) semis + (float) fine * 0.01f; const int p = preset; const bool at = autoTune != 0; const int sm = semis;
                    processCopy (ctx, "PITCH SHIFT", S(), [st, p, at, sm] (Sample& x)
                    {
                        ops::pitchShiftPreset (x, st, p);
                        if (! at) x.rootNote = juce::jlimit (0, 127, x.rootNote + sm);   // AUTO TUNE OFF: same key plays the old pitch
                    });
                }, s != nullptr);
                break;
            }
            case Bpm:
            {
                auto bpmFmt = [] (int v) { return juce::String (v / 100.0, 2) + " B.P.M."; };
                k[1] = valueFn ("New Tempo", [this] { return newBpm; }, [this] (int v) { newBpm = v; }, 2000, 99999, bpmFmt);
                k[2] = valueFn ("Source Tempo", [this] { return sourceBpm; }, [this] (int v) { sourceBpm = v; }, 2000, 99999, bpmFmt);
                k[9] = presetKey();
                k[3] = label ("Stretch", juce::String (100.0 * sourceBpm / juce::jmax (1, newBpm), 2) + "%");
                k[15] = button ("MATCH NEW BPM", [this]
                {
                    const double r = (double) sourceBpm / (double) juce::jmax (1, newBpm); const int p = preset;
                    if (r < 0.25 || r > 4.0) { ctx.info ("BPM MATCH", { "Tempo change out of range", "(25% .. 400%)" }); return; }
                    processCopy (ctx, "BPM MATCH", S(), [r, p] (Sample& x) { ops::timeStretchPreset (x, r, p); });
                }, s != nullptr);
                break;
            }
            case Resample:
            {
                k[1] = valueFn ("Re-Sample Freq", [this] { return newRate; }, [this] (int v) { newRate = v; }, 4000, 96000, hzText);
                k[1].param->bigStep = 1000;
                k[2] = valueFn ("Resolution", [this] { return bitsIdx; }, [this] (int v) { bitsIdx = v; }, 0, 5, list ({ "16-BIT", "12-BIT", "10-BIT", "8-BIT", "6-BIT", "4-BIT" }));
                k[9] = valueFn ("Quality", [this] { return quality; }, [this] (int v) { quality = v; }, 0, 2, list ({ "LOW", "MED", "HIGH" }));
                k[10] = valueFn ("Preset", [this] { return ratePreset; }, [this] (int v)
                {
                    ratePreset = v;
                    static const double f[] = { 1.0, 0.5, 0.75, 1.0 / 3.0, 2.0 / 3.0, 0.25 };
                    static const int abs[] = { 0, 0, 0, 0, 0, 0, 48000, 44100, 32000, 22050, 11025 };
                    if (auto* x = S()) newRate = v < 6 ? (int) std::round (x->sampleRate * f[v]) : abs[v];
                }, 0, 10, list ({ "SAME FREQ", "1/2", "3/4", "1/3", "2/3", "1/4", "48000", "44100", "32000", "22050", "11025" }));
                k[15] = button ("SET NEW FREQ", [this]
                {
                    static const int bits[] = { 16, 12, 10, 8, 6, 4 };
                    const double nr = newRate; const int q = quality, b = bits[bitsIdx];
                    processCopy (ctx, "RE-SAMPLE", S(), [nr, q, b] (Sample& x) { ops::resampleQ (x, nr, q); ops::requantise (x, b); });
                }, s != nullptr);
                break;
            }
            case Eq:
            {
                k[1] = valueFn ("High Frequency", [this] { return (int) eq.hf; }, [this] (int v) { eq.hf = (float) v; }, 2000, 20000, hzText);
                k[2] = valueFn ("Mid Frequency", [this] { return (int) eq.mf; }, [this] (int v) { eq.mf = (float) v; }, 500, 10000, hzText);
                k[3] = valueFn ("Low Frequency", [this] { return (int) eq.lf; }, [this] (int v) { eq.lf = (float) v; }, 10, 2000, hzText);
                auto gainKey = [] (const juce::String& t, float* gain)
                {
                    return valueFn (t, [gain] { return (int) *gain; }, [gain] (int v) { *gain = (float) v; }, -18, 18, dbText);
                };
                auto slopeParam = [] (int* slope)
                {
                    auto p = std::make_shared<Param>();
                    p->get = [slope] { return *slope; }; p->set = [slope] (int v) { *slope = v; };
                    p->min = 0; p->max = 1; p->format = [] (int v) { return v ? juce::String ("12dB/OCT") : juce::String ("6dB/OCT"); };
                    return p;
                };
                k[9] = gainKey ("HF Gain", &eq.hfGain);
                k[9].windowA = slopeParam (&eq.hfSlope); k[9].windowTitle = "HF GAIN"; k[9].windowLabelA = "Shelf";
                k[10] = gainKey ("MF Gain", &eq.mfGain);
                {
                    auto q = std::make_shared<Param>();
                    q->get = [this] { return (int) std::round (eq.mfQ * 10.0f); };
                    q->set = [this] (int v) { eq.mfQ = (float) v / 10.0f; };
                    q->min = 5; q->max = 200; q->format = [] (int v) { return juce::String (v / 10.0, 1); };
                    k[10].windowA = q; k[10].windowTitle = "MF GAIN"; k[10].windowLabelA = "Q (width)";
                }
                k[11] = gainKey ("LF Gain", &eq.lfGain);
                k[11].windowA = slopeParam (&eq.lfSlope); k[11].windowTitle = "LF GAIN"; k[11].windowLabelA = "Shelf";
                k[12] = valueFn ("Output Level", [this] { return (int) eq.outDb; }, [this] (int v) { eq.outDb = (float) v; }, -18, 18, dbText);
                k[15] = button ("EQ SAMPLE", [this]
                {
                    const auto e = eq;
                    processCopy (ctx, "EQ", S(), [e] (Sample& x) { ops::eq3 (x, e); });
                }, s != nullptr);
                break;
            }
        }
        // PLAY / STOP on a free key (F12, or F5 on the EQ page)
        k[kind == Eq ? 4 : 11] = button (isPlaying (ctx, s) ? u8 ("\xe2\x96\xa0 STOP") : u8 ("\xe2\x96\xb6 PLAY"), [this] { if (auto* x = S()) playSample (ctx, Sample::Ptr (x), false, x->start, x->end); }, s != nullptr);
    }

    void wheel (int delta) override
    {
        // frequencies move in 1/24 octave steps
        if (kind == Eq && (selected == 1 || selected == 2 || selected == 3))
        {
            if (auto& p = keys[(size_t) selected].param)
            {
                const double f = (double) p->get() * std::pow (2.0, delta / 24.0);
                const int v = (int) std::round (f);
                p->assign (v == p->get() ? v + (delta > 0 ? 1 : -1) : v);
            }
            return;
        }
        if (kind == Resample && selected == 1)
            if (auto& p = keys[1].param) { p->adjust (delta * (std::abs (delta) > 1 ? 1000 : 100)); return; }
        if ((kind == Bpm && (selected == 1 || selected == 2)) || (kind == Stretch && selected == 1))
            if (auto& p = keys[(size_t) selected].param) { p->adjust (delta * (std::abs (delta) > 1 ? 100 : 1)); return; }
        SamplePage::wheel (delta);
    }

    void paintFull (juce::Graphics& g, juce::Rectangle<float> lcd) override
    {
        auto* s = S();
        auto e = eq;
        const bool isEq = kind == Eq;
        paintStandard (g, waveArea (lcd), [s, e, isEq] (wave::Options& o, bool)
        {
            if (s == nullptr) return;
            o.hlFrom = s->start; o.hlTo = s->end;
            if (s->loopEnabled) { o.loopStyle = wave::LoopStyle::Invert; o.loopFrom = s->loopStart; o.loopTo = s->loopEnd; }
            if (! isEq) return;
            const double sr = s->sampleRate;
            o.overlay = [e, sr] (juce::Graphics& gg, juce::Rectangle<float> r, const std::function<float (int64)>&)
            {
                // frequency response (20 Hz .. 20 kHz, +/-18 dB) drawn over the waveform
                gg.setColour (colours::oledBg.withAlpha (0.55f));
                gg.fillRect (r);
                const float midY = r.getCentreY();
                gg.setColour (colours::oledGrid.brighter (0.4f));
                gg.fillRect (juce::Rectangle<float> (r.getX(), midY, r.getWidth(), 1.0f));
                for (double f : { 100.0, 1000.0, 10000.0 })
                {
                    const float x = r.getX() + r.getWidth() * (float) (std::log10 (f / 20.0) / 3.0);
                    gg.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
                    oled::text (gg, f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 2.0f, r.getBottom() - 12.0f, 30.0f, 11.0f),
                                juce::Justification::centredLeft, 7.0f, false, colours::oledDim.brighter (0.4f));
                }
                juce::Path p;
                const int cols = juce::jmax (2, (int) r.getWidth());
                for (int x = 0; x <= cols; ++x)
                {
                    const double f = 20.0 * std::pow (1000.0, (double) x / cols);
                    const float db = juce::jlimit (-20.0f, 20.0f, ops::eqResponseDb (e, sr, juce::jmin (f, sr * 0.49)));
                    const float y = midY - db / 20.0f * r.getHeight() * 0.5f;
                    const float px = r.getX() + (float) x * r.getWidth() / (float) cols;
                    if (x == 0) p.startNewSubPath (px, y); else p.lineTo (px, y);
                }
                gg.setColour (colours::oledWarn);
                gg.strokePath (p, juce::PathStrokeType (1.6f));
            };
            o.tagLeft = "EQ  +/-18dB";
        }, false);
        slots[0].zoomable = false;
    }

    bool wantsAnimation() override
    {
        const bool p = isPlaying (ctx, S());
        if (p != wasPlaying) { wasPlaying = p; ctx.pages->refresh(); }
        return SamplePage::wantsAnimation();
    }

private:
    Kind kind;
    int stretch = 10000, preset = 0, semis = 0, fine = 0, autoTune = 1;
    int newBpm = 0, sourceBpm = 12000;
    int newRate = 44100, bitsIdx = 0, quality = 1, ratePreset = 0;
    ops::EqSettings eq;
    bool wasPlaying = false;
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
        k[9] = button ("PLAY", [this] { if (auto* s = ctx.sample()) playSample (ctx, Sample::Ptr (s)); });
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
                stopPlay (ctx);
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
                stopPlay (ctx);
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
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampTimestretch, "T-STRETCH", SampProcessPage::Stretch));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampPitchShift, "P-SHIFT", SampProcessPage::Pitch));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampBpm, "BPM MATCH", SampProcessPage::Bpm));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampResample, "RE-SAMPLE", SampProcessPage::Resample));
    pm.add (std::make_unique<SampProcessPage> (c, PageId::SampEq, "EQ", SampProcessPage::Eq));
    pm.add (std::make_unique<SampleListPage> (c));
}

} // namespace s6k::ui
