/*
  ==============================================================================
   S6000 Emulation - UTILITIES (manual p.219-241)
     SYSTEM SETUP, SET CLOCK, PREFERENCES, MIDI SETUP, MIDI FILTER, FX IN/OUTS,
     DISK UTILS, MEMORY TEST, SHOW HARDWARE, SET PLAY KEY, TEST TONE, SAVE O/S
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;

//==============================================================================
class UtilPage : public Page
{
public:
    UtilPage (Ctx& c, PageId i, PageId parentPage, juce::String t) : Page (c, i, parentPage, Mode::Utilities), pageTitle (std::move (t)) {}
    juce::String title() const override { return pageTitle; }

    S6000Prefs& prefs() const { return ctx.proc.prefs; }
    kb::IntRef pref (int S6000Prefs::* m) const { return [this, m]() -> int* { return &(prefs().*m); }; }
    std::function<void()> apply() const { return [this] { ctx.proc.applyPrefsToEngine(); ctx.proc.savePrefs(); }; }

    SoftKey clockHeader() const
    {
        return header (juce::Time::getCurrentTime().formatted ("%a %d/%m/%y   %H:%M:%S"));
    }

    void logo (juce::Graphics& g, juce::Rectangle<float>& r) const
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (fonts::oledTitle().withHeight (26.0f), "S6000", 0, 0);
        juce::Path p; ga.createPath (p);
        auto area = r.removeFromTop (34.0f);
        const auto b = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (area.getCentreX() - b.getCentreX(), area.getCentreY() - b.getCentreY()));
        g.setColour (colours::oledText);
        g.strokePath (p, juce::PathStrokeType (1.2f));
        oled::text (g, "Operating System", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
        oled::text (g, "VERSION " + juce::String (osVersion), r.removeFromTop (16.0f), juce::Justification::centred, 10.5f, true);
    }

    void systemCentre (juce::Graphics& g, juce::Rectangle<float> r)
    {
        logo (g, r);
        r.removeFromTop (6.0f);
        oled::memoryBox (g, r.removeFromTop (96.0f), ctx);
    }

    bool wantsTicks() const override { return true; }   // clock
    void tick() override {}

    void mainKey (Keys& k) { k[7] = button ("MAIN", [this] { ctx.pages->show (PageId::Util); }); }

    juce::String pageTitle;
};

//==============================================================================
class UtilMainPage : public UtilPage
{
public:
    explicit UtilMainPage (Ctx& c) : UtilPage (c, PageId::Util, PageId::None, "UTILITIES") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = button ("SYSTEM SETUP", [this] { ctx.pages->show (PageId::UtilSystem); });
        k[2] = button ("PREFERENCES", [this] { ctx.pages->show (PageId::UtilPrefs); });
        k[3] = button ("MIDI SETUP", [this] { ctx.pages->show (PageId::UtilMidi); });
        k[4] = button ("FX IN/OUTS", [this] { ctx.pages->show (PageId::UtilFxInOuts); });
        k[7] = button ("TEST TONE", [this] { ctx.proc.testTone = ! ctx.proc.testTone.load(); });
        k[7].highlight = ctx.proc.testTone.load();
        k[9] = button ("DISK UTILS", [this] { ctx.pages->show (PageId::UtilDisk); });
        k[10] = button ("MEMORY TEST", [this] { ctx.pages->show (PageId::UtilMemTest); });
        k[11] = button ("SHOW HARDWARE", [this] { ctx.pages->show (PageId::UtilHardware); });
        k[12] = button ("SET PLAY KEY", [this] { ctx.pages->show (PageId::UtilPlayKey); });
        k[15] = button ("SAVE O/S", [this] { ctx.pages->show (PageId::UtilSaveOs); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override { systemCentre (g, r); }
};

//==============================================================================
class UtilSystemPage : public UtilPage
{
public:
    explicit UtilSystemPage (Ctx& c) : UtilPage (c, PageId::UtilSystem, PageId::Util, "SYSTEM\nSETUP") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = value ("Transpose", pref (&S6000Prefs::sysTranspose), -24, 24, signed2(), apply());
        k[2] = value ("Semitone Tune", pref (&S6000Prefs::sysSemitone), -24, 24, signed2(), apply());
        k[3] = value ("Fine Tune", pref (&S6000Prefs::sysFine), -50, 50, signed2(), apply());
        k[4] = value ("Master Level", pref (&S6000Prefs::sysMasterLevel), -48, 12, [] (int v) { return names::signedValue (v, 2) + "dB"; }, apply());
        k[5] = button ("SET CLOCK", [this] { ctx.pages->show (PageId::UtilClock); });
        k[6] = valueFn ("Memory Size", [this] { return (int) (ctx.mem.capacityBytes / (1024 * 1024)); },
                        [this] (int v) { ctx.mem.capacityBytes = (size_t) juce::jlimit (32, 1024, v) * 1024 * 1024; ctx.proc.settings().setValue ("memoryMB", v); },
                        32, 1024, suffix ("MB"));
        mainKey (k);
        k[9] = label ("Sync Source", "INT " + juce::String (ctx.proc.getSampleRate() / 1000.0, 1) + "kHz");
        k[10] = valueFn ("Digital I/O", [this] { return digital; }, [this] (int v) { digital = v; }, 0, 1, list ({ "PROFESSIONAL", "CONSUMER" }));
        k[11] = value ("Polyphony", pref (&S6000Prefs::polyphony), 8, 128, suffix (" VOICES"), apply());
        k[12] = value ("Interpolation", pref (&S6000Prefs::interpolation), 0, 1, list ({ "LINEAR (LOW CPU)", "HERMITE (HQ)" }), apply());
        k[15] = button ("SAVE O/S", [this] { ctx.pages->show (PageId::UtilSaveOs); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        systemCentre (g, r);
        oled::text (g, "S/Rate : " + juce::String (ctx.proc.getSampleRate() / 1000.0, 1) + "kHz", r.withTrimmedTop (r.getHeight() - 30.0f).withHeight (14.0f),
                    juce::Justification::centred, 10.5f);
    }
private:
    int digital = 0;
};

//==============================================================================
class UtilClockPage : public UtilPage
{
public:
    explicit UtilClockPage (Ctx& c) : UtilPage (c, PageId::UtilClock, PageId::UtilSystem, "SET CLOCK") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[7] = button ("SYSTEM SETUP", [this] { ctx.pages->show (PageId::UtilSystem); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const auto t = juce::Time::getCurrentTime();
        r.removeFromTop (20.0f);
        oled::text (g, t.formatted ("%H:%M:%S"), r.removeFromTop (20.0f), juce::Justification::centred, 12.0f, true);
        oled::text (g, t.formatted ("%A"), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
        oled::text (g, t.formatted ("%d %B %Y"), r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
        r.removeFromTop (12.0f);
        oled::text (g, "The clock follows", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
        oled::text (g, "your computer", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
    }
};

//==============================================================================
class UtilPrefsPage : public UtilPage
{
public:
    explicit UtilPrefsPage (Ctx& c) : UtilPage (c, PageId::UtilPrefs, PageId::Util, "PREFERENCES") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = value ("Note Display", pref (&S6000Prefs::noteDisplay), 0, 1, list ({ "C3=60 (AKAI)", "C4=60" }), apply());
        k[2] = value ("Loop Display", pref (&S6000Prefs::loopDisplay), 0, 1, list ({ "SAMPLES", "SECONDS" }), apply());
        k[3] = value ("Screen Display", pref (&S6000Prefs::screenInvert), 0, 1, list ({ "NORMAL", "INVERTED" }), apply());
        k[4] = value ("Screen Saver", pref (&S6000Prefs::screenSaver), 0, 60, [] (int v) { return v == 0 ? juce::String ("OFF") : juce::String (v) + " MIN"; }, apply());
        k[5] = value ("Key Repeat Dly", pref (&S6000Prefs::keyRepeatDelay), 1, 10, num(), apply());
        k[6] = value ("Fan Speed", pref (&S6000Prefs::fanSpeed), 0, 1, list ({ "NORMAL", "QUIET" }), apply());
        mainKey (k);
        k[9] = value ("Save In Project", pref (&S6000Prefs::saveSession), 0, 1, onOff(), apply());
        k[10] = value ("Autoload", pref (&S6000Prefs::autoload), 0, 1, onOff(), apply());
        k[11] = value ("Boot Screen", pref (&S6000Prefs::showSplash), 0, 1, onOff(), apply());
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        systemCentre (g, r);
    }
};

//==============================================================================
class UtilMidiPage : public UtilPage
{
public:
    explicit UtilMidiPage (Ctx& c) : UtilPage (c, PageId::UtilMidi, PageId::Util, "MIDI SETUP") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = value ("Program Change", pref (&S6000Prefs::midiProgramChange), 0, 1, onOff(), apply());
        k[2] = value ("Multi Select", pref (&S6000Prefs::midiMultiSelect), 0, 1, onOff(), apply());
        k[3] = value ("Multi Slct Ch", pref (&S6000Prefs::midiMultiSelectCh), 0, 15, [] (int v) { return names::midiChannel (v); }, apply());
        k[4] = value ("Ext APM Control", pref (&S6000Prefs::extApmCtrl), 0, 127, [] (int v) { return "CC " + juce::String (v); }, apply());
        k[5] = value ("Merge B > A", pref (&S6000Prefs::mergeBtoA), 0, 1, onOff(), apply());
        k[6] = button ("MIDI FILTER", [this] { ctx.pages->show (PageId::UtilMidiFilter); });
        mainKey (k);
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (10.0f);
        oled::text (g, "PORT A = channels 1-16", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "PORT B : parts 1B-16B", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, prefs().mergeBtoA ? "also follow 1-16" : "are silent (no port B)", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        r.removeFromTop (6.0f);
        oled::memoryBox (g, r.removeFromTop (96.0f), ctx);
    }
};

class UtilMidiFilterPage : public UtilPage
{
public:
    explicit UtilMidiFilterPage (Ctx& c) : UtilPage (c, PageId::UtilMidiFilter, PageId::UtilMidi, "MIDI FILTER") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = check ("NOTES", pref (&S6000Prefs::rxNotes), apply());
        k[2] = check ("PITCHBEND", pref (&S6000Prefs::rxBend), apply());
        k[3] = check ("CONTROLLERS", pref (&S6000Prefs::rxCtrl), apply());
        k[4] = check ("AFTERTOUCH", pref (&S6000Prefs::rxAftertouch), apply());
        k[5] = check ("PROGRAM CHANGE", pref (&S6000Prefs::rxProgram), apply());
        mainKey (k);
        k[15] = button ("MIDI SETUP", [this] { ctx.pages->show (PageId::UtilMidi); });
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (10.0f);
        oled::text (g, "Checked = RECEIVE", r.removeFromTop (16.0f), juce::Justification::centred, 10.5f);
    }
};

//==============================================================================
class UtilFxInOutsPage : public UtilPage
{
public:
    explicit UtilFxInOutsPage (Ctx& c) : UtilPage (c, PageId::UtilFxInOuts, PageId::Util, "FX IN/OUTS") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = value ("Input Level", [this]() -> int* { return &ctx.proc.recorder.recordLevelDb; }, -60, 12, [] (int v) { return names::signedValue (v, 2) + "dB"; });
        k[2] = value ("L/R Direct Lvl", pref (&S6000Prefs::directMonitor), 0, 100, num(), apply());
        k[3] = value ("FX Output", pref (&S6000Prefs::fxOutPair), 0, 7, [] (int v) { return names::output (v, false); }, apply());
        mainKey (k);
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (10.0f);
        oled::text (g, "EB20 returns go to", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "output " + names::output (prefs().fxOutPair, false), r.removeFromTop (geo::lineH), juce::Justification::centred, 11.0f, true);
        r.removeFromTop (8.0f);
        oled::text (g, "Direct level monitors", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.0f, false, colours::oledDim);
        oled::text (g, "the record input", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.0f, false, colours::oledDim);
    }
};

//==============================================================================
class UtilDiskPage : public UtilPage
{
public:
    explicit UtilDiskPage (Ctx& c) : UtilPage (c, PageId::UtilDisk, PageId::Util, "DISK UTILS") {}
    bool listLayout() const override { return true; }
    bool wantsTicks() const override { return false; }
    juce::Array<juce::File> files() const
    {
        auto d = folder.isDirectory() ? folder : ctx.disk.getHostRootFolder();
        auto f = d.findChildFiles (juce::File::findFilesAndDirectories, false);
        f.sort();
        return f;
    }
    void onShow() override { if (! folder.isDirectory()) folder = ctx.disk.getHostRootFolder(); }

    void build (Keys& k) override
    {
        k[0] = header (folder.getFileName());
        k[0].subtitle = "S6000 HD";
        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = scroll + geo::rowForKey (i);
            k[(size_t) i] = row ({}, {}, 0, false, [this, rowIdx]
            {
                auto f = files();
                if (rowIdx >= f.size()) return;
                if (rowIdx == sel && f[rowIdx].isDirectory()) { folder = f[rowIdx]; sel = 0; scroll = 0; }
                else sel = rowIdx;
            });
        }
        mainKey (k);
        k[9] = button ("RENAME", [this]
        {
            auto f = files();
            if (! juce::isPositiveAndBelow (sel, f.size())) return;
            const auto file = f[sel];
            ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "RENAME ITEM", file.getFileNameWithoutExtension(), 40, [this, file] (const juce::String& n)
            {
                if (! file.moveFileTo (file.getSiblingFile (DiskManager::safeFileName (n) + file.getFileExtension()))) ctx.error ("RENAME FAILED");
            }));
        });
        k[10] = button ("DELETE", [this]
        {
            auto f = files();
            if (! juce::isPositiveAndBelow (sel, f.size())) return;
            const auto file = f[sel];
            ctx.confirm ("DELETE ITEM", "Move " + file.getFileName() + "\nto the trash?", [this, file]
            {
                if (! file.moveToTrash()) ctx.error ("DELETE FAILED");
            });
        });
        k[11] = button ("NEW FOLDER", [this]
        {
            ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "NEW FOLDER", "NEW FOLDER", 30, [this] (const juce::String& n)
            {
                if (! folder.getChildFile (DiskManager::safeFileName (n)).createDirectory()) ctx.error ("CANNOT CREATE FOLDER");
            }));
        });
        k[12] = button ("CLOSE FOLDER", [this] { if (folder != ctx.disk.getHostRootFolder()) { folder = folder.getParentDirectory(); sel = 0; scroll = 0; } });
        k[13] = button ("FORMAT", [this] { ctx.info ("FORMAT", { "Formatting is disabled:", "the S6000 HD is a folder", "on your computer" }); });
        k[14] = button ("SHOW IN OS", [this] { folder.revealToUser(); });
        k[15] = button ("UNLOCK DISK", [this] { ctx.info ("UNLOCK DISK", { "Disk is not locked" }); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        oled::text (g, folder.getFullPathName().getLastCharacters (40), r.removeFromTop (geo::lineH), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        auto f = files();
        const int visible = geo::listRows;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        for (int i = scroll; i < juce::jmin (f.size(), scroll + visible); ++i)
        {
            auto line = r.removeFromTop (geo::lineH);
            const bool s = i == sel;
            if (s) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = s ? colours::oledBg : colours::oledText;
            disk::VNode tmp; tmp.name = f[i].getFileName().toStdString(); disk::classifyByName (tmp);
            const int ic = f[i].isDirectory() ? 1 : tmp.type == disk::VNode::Type::Multi ? 2 : tmp.type == disk::VNode::Type::Program ? 3
                         : tmp.type == disk::VNode::Type::Sample ? 4 : tmp.type == disk::VNode::Type::Disk ? 6 : 0;
            oled::icon (g, ic, line.removeFromLeft (24.0f), col);
            oled::text (g, f[i].getFileName(), line, juce::Justification::centredLeft, 11.0f, false, col);
        }
        if (f.isEmpty()) oled::text (g, "(EMPTY)", r.removeFromTop (30.0f), juce::Justification::centred, 11.0f);
    }

    void wheel (int delta) override { sel = juce::jlimit (0, juce::jmax (0, files().size() - 1), sel + delta); }

private:
    juce::File folder;
    int sel = 0, scroll = 0;
};

//==============================================================================
class UtilMemTestPage : public UtilPage
{
public:
    explicit UtilMemTestPage (Ctx& c) : UtilPage (c, PageId::UtilMemTest, PageId::Util, "MEMORY TEST") {}
    void onShow() override { progress = 0; }
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        mainKey (k);
        k[15] = button ("TEST AGAIN", [this] { progress = 0; });
    }
    void tick() override { if (progress < 100) progress += 2; }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const int mb = (int) (ctx.mem.capacityBytes / (1024 * 1024));
        r.removeFromTop (10.0f);
        oled::text (g, "TESTING " + juce::String (mb) + "MB", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true);
        for (int s = 0; s < 4; ++s)
        {
            auto line = r.removeFromTop (16.0f).reduced (10.0f, 2.0f);
            oled::text (g, "SIMM " + juce::String (s + 1), line.removeFromLeft (52.0f), juce::Justification::centredLeft, 10.5f);
            const float pr = juce::jlimit (0.0f, 1.0f, ((float) progress - 25.0f * (float) s) / 25.0f);
            g.setColour (colours::oledText);
            g.drawRect (line, 1.0f);
            g.fillRect (line.reduced (2.0f).withWidth ((line.getWidth() - 4.0f) * pr));
        }
        r.removeFromTop (8.0f);
        oled::text (g, progress >= 100 ? "MEMORY OK" : "PLEASE WAIT...", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f, true);
    }
private:
    int progress = 0;
};

//==============================================================================
class UtilHardwarePage : public UtilPage
{
public:
    explicit UtilHardwarePage (Ctx& c) : UtilPage (c, PageId::UtilHardware, PageId::Util, "SHOW\nHARDWARE") {}
    void build (Keys& k) override { k[0] = clockHeader(); mainKey (k); }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        juce::String fmt = juce::AudioProcessor::getWrapperTypeDescription (ctx.proc.wrapperType);
        const juce::StringArray lines {
            "S6000  OS " + juce::String (osVersion),
            "RAM " + juce::String ((int) (ctx.mem.capacityBytes / 1048576)) + "MB  " + juce::String (ctx.mem.percentFree()) + "% FREE",
            "VOICES 128  (" + juce::String (ctx.engine.getActiveVoiceCount()) + " ACTIVE)",
            "EB20 FX     : INSTALLED",
            "OUTPUTS     : 16 (8 PAIRS)",
            "SAMPLE RATE : " + juce::String (ctx.proc.getSampleRate(), 0),
            "FORMAT      : " + fmt.toUpperCase(),
            "(c) dominikHop"
        };
        for (auto& l : lines) oled::text (g, l, r.removeFromTop (geo::lineH), juce::Justification::centredLeft, 10.5f);
    }
};

//==============================================================================
class UtilPlayKeyPage : public UtilPage
{
public:
    explicit UtilPlayKeyPage (Ctx& c) : UtilPage (c, PageId::UtilPlayKey, PageId::Util, "SET PLAY KEY") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = value ("Play Key Note", pref (&S6000Prefs::playKey), 0, 127, note (ctx), apply());
        mainKey (k);
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        r.removeFromTop (10.0f);
        oled::text (g, "ENT/PLAY plays", r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
        oled::text (g, ctx.note (prefs().playKey), r.removeFromTop (18.0f), juce::Justification::centred, 12.0f, true);
        oled::keyboard (g, r.removeFromTop (40.0f).reduced (4.0f), prefs().playKey - 12, prefs().playKey + 11, prefs().playKey, prefs().playKey);
    }
};

//==============================================================================
class UtilSaveOsPage : public UtilPage
{
public:
    explicit UtilSaveOsPage (Ctx& c) : UtilPage (c, PageId::UtilSaveOs, PageId::Util, "SAVE O/S") {}
    void build (Keys& k) override
    {
        k[0] = clockHeader();
        k[1] = button ("SAVE SETTINGS", [this] { ctx.proc.savePrefs(); ctx.proc.settings().saveIfNeeded(); ctx.info ("SAVE O/S", { "System settings saved" }); });
        k[2] = button ("FACTORY RESET", [this]
        {
            ctx.confirm ("FACTORY RESET", "Reset all UTILITIES\nsettings?", [this]
            {
                ctx.proc.prefs = S6000Prefs();
                ctx.proc.applyPrefsToEngine();
                ctx.proc.savePrefs();
            });
        });
        mainKey (k);
    }
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        logo (g, r);
        r.removeFromTop (8.0f);
        oled::text (g, "The O/S is built into", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "this plug-in. Settings", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "are kept like the", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
        oled::text (g, "battery backed RAM.", r.removeFromTop (geo::lineH), juce::Justification::centred, 10.5f);
    }
};

void registerUtilityPages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<UtilMainPage> (c));
    pm.add (std::make_unique<UtilSystemPage> (c));
    pm.add (std::make_unique<UtilClockPage> (c));
    pm.add (std::make_unique<UtilPrefsPage> (c));
    pm.add (std::make_unique<UtilMidiPage> (c));
    pm.add (std::make_unique<UtilMidiFilterPage> (c));
    pm.add (std::make_unique<UtilFxInOutsPage> (c));
    pm.add (std::make_unique<UtilDiskPage> (c));
    pm.add (std::make_unique<UtilMemTestPage> (c));
    pm.add (std::make_unique<UtilHardwarePage> (c));
    pm.add (std::make_unique<UtilPlayKeyPage> (c));
    pm.add (std::make_unique<UtilSaveOsPage> (c));
}

} // namespace s6k::ui
