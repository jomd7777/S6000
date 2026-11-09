/*
  ==============================================================================
   S6000 Emulation - RECORD mode (manual p.178-185)
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;

//==============================================================================
class RecordDonePopup : public Popup
{
public:
    RecordDonePopup (Ctx& c, std::function<void()> keep, std::function<void()> discard, std::function<void()> playIt)
        : Popup (c, "RECORDING FINISHED"), onKeep (std::move (keep)), onDiscard (std::move (discard)), onPlay (std::move (playIt)) { boxHeight = 0.42f; }

    void build (Keys& k) override
    {
        k[7] = kb::button ("DISCARD", [this] { auto f = onDiscard; close(); if (f) f(); });
        k[8] = kb::button ("PLAY", [this] { if (onPlay) onPlay(); });
        k[15] = kb::button ("KEEP", [this] { auto f = onKeep; close(); if (f) f(); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (8.0f);
        oled::text (g, "Audition the recording", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
        oled::text (g, "KEEP it or DISCARD it", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
    }
    std::function<void()> onKeep, onDiscard, onPlay;
};

//==============================================================================
class RecordPage : public Page
{
public:
    explicit RecordPage (Ctx& c) : Page (c, PageId::Record, PageId::None, Mode::Record) {}
    juce::String title() const override { return "RECORD"; }
    bool wantsTicks() const override { return true; }

    void onShow() override
    {
        if (recName.isEmpty()) recName = ctx.mem.uniqueSampleName ("NEW SAMPLE 1");
    }

    void build (Keys& k) override
    {
        auto& rec = ctx.proc.recorder;
        auto hp = std::make_shared<Param>();
        hp->get = [] { return 0; };
        hp->set = [] (int) {};
        hp->format = [this] (int) { return recName; };
        k[0] = kb::header ({}, hp);
        k[0].onPress = [this] { rename(); };

        k[1] = kb::value ("Record Level", [&rec]() -> int* { return &rec.recordLevelDb; }, -60, 12,
                          [] (int v) { return v <= -60 ? juce::String ("-INF") : names::signedValue (v, 2) + "dB"; });
        k[1].arrows = true;
        k[2] = kb::value ("Record Trigger", [&rec]() -> int* { return &rec.trigger; }, 0, 4,
                          kb::list ({ "ONE KEY", "THRESHOLD L+R", "THRESHOLD L", "THRESHOLD R", "MIDI NOTE ON" }));
        k[3] = kb::value ("Threshold", [&rec]() -> int* { return &rec.thresholdDb; }, -60, 0, [] (int v) { return juce::String (v) + "dB"; });
        k[4] = kb::value ("MIDI Trig Chan", [&rec]() -> int* { return &rec.midiTrigChannel; }, 0, 31, [] (int v) { return names::midiChannel (v); });
        k[5] = kb::valueFn ("Record To", [] { return 0; }, [this] (int v) { if (v) ctx.info ("RECORD TO", { "Direct-to-disk is not", "used in the plug-in:", "recordings go to RAM" }); }, 0, 1,
                            kb::list ({ "RAM", "DISK" }));

        const int st = rec.state.load();
        juce::String armLabel = st == Recorder::Idle || st == Recorder::Done ? "ARM" : st == Recorder::Armed ? (rec.trigger == Recorder::OneKey ? "RECORD" : "ARMED (STOP)") : "STOP";
        k[7] = kb::button (armLabel, [this] { armPressed(); });
        k[7].highlight = st == Recorder::Armed || st == Recorder::Recording;

        k[8] = kb::button ("RESET MARGIN", [this] { marginDb = 96.0f; });
        k[9] = kb::value ("Record Source", [&rec]() -> int* { return &rec.source; }, 0, 2, kb::list ({ "ANALOGUE", "DIGITAL", "MAIN OUTS" }));
        k[10] = kb::value ("Length min.sec", [&rec]() -> int* { return &rec.lengthSeconds; }, 1, 1800,
                           [] (int v) { return juce::String (v / 60) + "m" + juce::String (v % 60).paddedLeft ('0', 2) + "s"; });
        k[11] = kb::value ("Original Note", [&rec]() -> int* { return &rec.originalNote; }, 0, 127, kb::note (ctx));
        k[12] = kb::value ("Record Mode", [&rec]() -> int* { return &rec.stereo; }, 0, 1, kb::list ({ "MONO", "STEREO" }));
        k[13] = kb::button ("SETUP D-D", [this] { ctx.info ("SETUP D-D", { "Direct-to-disk set-up", "is not needed here:", "RAM holds 30 minutes" }); });
        k[15] = kb::button ("RENAME/NEW", [this] { rename(); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto& rec = ctx.proc.recorder;
        // INPUT LEVEL
        auto box = r.removeFromTop (62.0f);
        oled::box (g, box);
        auto head = box.removeFromTop (16.0f);
        g.drawLine (head.getX(), head.getBottom(), head.getRight(), head.getBottom());
        oled::text (g, "INPUT LEVEL", head, juce::Justification::centred, 10.0f, true);
        oled::text (g, "Margin " + juce::String ((int) marginDb) + " dB", box.removeFromTop (16.0f), juce::Justification::centred, 10.0f);
        auto ml = box.removeFromTop (16.0f).reduced (4.0f, 2.0f);
        oled::text (g, "L", ml.removeFromLeft (10.0f), juce::Justification::centred, 10.0f);
        oled::meter (g, ml, rec.peakL.load());
        auto mr = box.removeFromTop (16.0f).reduced (4.0f, 2.0f);
        oled::text (g, "R", mr.removeFromLeft (10.0f), juce::Justification::centred, 10.0f);
        oled::meter (g, mr, rec.peakR.load());

        r.removeFromTop (4.0f);
        auto mem = r.removeFromTop (50.0f);
        oled::box (g, mem);
        auto mh = mem.removeFromTop (16.0f);
        g.drawLine (mh.getX(), mh.getBottom(), mh.getRight(), mh.getBottom());
        oled::text (g, "MEMORY", mh, juce::Justification::centred, 10.0f, true);
        oled::text (g, juce::String (ctx.mem.samples.size()) + " Samples", mem.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
        const double bytesFree = (double) ctx.mem.capacityBytes - (double) ctx.mem.bytesUsed();
        const double secs = juce::jmax (0.0, bytesFree / (2.0 * (rec.stereo ? 2.0 : 1.0) * juce::jmax (1.0, ctx.proc.getSampleRate())));
        oled::text (g, juce::String (ctx.mem.percentFree()) + "% Free + " + juce::String ((int) secs / 60) + "m" + juce::String ((int) secs % 60) + "s",
                    mem.removeFromTop (16.0f), juce::Justification::centred, 10.5f);

        r.removeFromTop (4.0f);
        oled::text (g, "S/Rate : " + juce::String (ctx.proc.getSampleRate() / 1000.0, 1) + "kHz", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        juce::String status = "New Recording";
        switch (rec.state.load())
        {
            case Recorder::Armed:     status = rec.trigger == Recorder::OneKey ? "ARMED - press RECORD" : "ARMED - waiting..."; break;
            case Recorder::Recording: status = "RECORDING " + juce::String ((double) rec.recorded.load() / juce::jmax (1.0, rec.getSampleRate()), 1) + "s"; break;
            case Recorder::Done:      status = "FINISHED"; break;
            default: break;
        }
        oled::text (g, status, r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f, rec.state.load() == Recorder::Recording);
        if (ctx.proc.getBusCount (true) == 0 && rec.source != Recorder::MainOuts)
            oled::text (g, "(no input bus: use MAIN OUTS)", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledWarn);
    }

    void tick() override
    {
        auto& rec = ctx.proc.recorder;
        const float pk = juce::jmax (rec.peakL.load(), rec.peakR.load());
        marginDb = juce::jmin (marginDb, -juce::Decibels::gainToDecibels (pk, -96.0f));
        if (rec.state.load() == Recorder::Done && ! donePopup)
        {
            donePopup = true;
            ctx.pages->pushPopup (std::make_unique<RecordDonePopup> (ctx, [this] { keep(); }, [this] { discard(); }, [this]
            {
                if (pending == nullptr) pending = takeRecording();
                if (pending != nullptr) ctx.engine.audition (pending);
            }));
        }
    }

    void enterPlay (bool down) override
    {
        if (down) armPressed();
    }

private:
    juce::String recName;
    float marginDb = 96.0f;
    bool donePopup = false;
    Sample::Ptr pending;

    void rename()
    {
        ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "NAME RECORDING", recName, 20, [this] (const juce::String& n) { recName = n; }));
    }

    void armPressed()
    {
        auto& rec = ctx.proc.recorder;
        const int st = rec.state.load();
        if (st == Recorder::Idle || st == Recorder::Done)
        {
            const double bytesFree = (double) ctx.mem.capacityBytes - (double) ctx.mem.bytesUsed();
            const double need = 2.0 * (rec.stereo ? 2 : 1) * rec.lengthSeconds * ctx.proc.getSampleRate();
            if (need > bytesFree) { ctx.info ("RECORD", { "NOT ENOUGH MEMORY", "Shorten the length", "or delete samples" }); return; }
            donePopup = false;
            pending = nullptr;
            rec.arm ((double) rec.lengthSeconds);
        }
        else if (st == Recorder::Armed && rec.trigger == Recorder::OneKey)
            rec.startNow();
        else
            rec.stop();
    }

    Sample::Ptr takeRecording()
    {
        auto& rec = ctx.proc.recorder;
        const auto n = (int) rec.recorded.load();
        if (n <= 0) return nullptr;
        Sample::Ptr s = new Sample();
        s->name = ctx.mem.uniqueSampleName (recName);
        s->buffer.setSize (rec.buffer.getNumChannels(), n);
        for (int c = 0; c < rec.buffer.getNumChannels(); ++c) s->buffer.copyFrom (c, 0, rec.buffer, c, 0, n);
        s->sampleRate = rec.getSampleRate();
        s->rootNote = rec.originalNote;
        s->start = 0; s->end = n; s->loopStart = 0; s->loopEnd = n;
        s->playMode = fmt::PlayNoLoop;
        return s;
    }

    void keep()
    {
        if (pending == nullptr) pending = takeRecording();
        if (pending != nullptr)
        {
            auto s = pending;
            ctx.structural ([this, s] { ctx.mem.samples.add (s); ctx.curSample = ctx.mem.samples.size() - 1; });
        }
        pending = nullptr;
        ctx.proc.recorder.reset();
        ctx.proc.recorder.buffer.setSize (1, 0);
        recName = ctx.mem.uniqueSampleName (recName);
        donePopup = false;
    }

    void discard()
    {
        ctx.engine.audition (nullptr);
        pending = nullptr;
        ctx.proc.recorder.reset();
        donePopup = false;
    }
};

void registerRecordPage (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<RecordPage> (c));
}

} // namespace s6k::ui
