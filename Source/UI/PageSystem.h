/*
  ==============================================================================
   S6000 Emulation - "Touch and Tweak" page system
     Every screen is a Page that fills the 16 soft keys (F1..F16).
     Touch a key = select / execute, turn the DATA wheel = tweak.
     Pop-ups (tools menus, prompts, naming, windows) stack on top.
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include "Theme.h"
#include "../Core/Model.h"

class S6000AudioProcessor;

namespace s6k { class DiskManager; class SamplerEngine; }

namespace s6k::ui
{
class PageManager;

//==============================================================================
enum class PageId
{
    None, Boot,
    Load, DiskList,
    Multi, MultiList, ProgramList, SampleList,
    Fx, FxEdit1, FxEdit2, FxRv3, FxRv4,
    ProgMain, ProgOutput, ProgMidiTune, ProgEditUser, ProgPitchBend, ProgLfo1, ProgLfo2,
    ProgKeygroup, ProgZones, ProgKeyspan, ProgKgPitchAmp, ProgFilter, ProgAmpEnv, ProgFiltEnv, ProgAuxEnv,
    SampMain, SampMaster, SampTrim, SampChop, SampLoop, SampJoin, SampMix, SampFade,
    SampTimestretch, SampPitchShift, SampBpm, SampResample, SampEq,
    Record, Save,
    Util, UtilSystem, UtilClock, UtilPrefs, UtilMidi, UtilMidiFilter, UtilFxInOuts,
    UtilDisk, UtilMemTest, UtilHardware, UtilPlayKey, UtilTestTone, UtilSaveOs
};

enum class Mode { Multi = 0, Fx, Sample, Program, Record, Utilities, Save, Load, None };

//==============================================================================
struct Param
{
    std::function<int()> get;
    std::function<void (int)> set;
    int min = 0, max = 127;
    std::function<juce::String (int)> format;
    int bigStep = 10;               // keypad +/- with SHIFT, wheel acceleration

    juce::String text() const { const int v = get ? get() : 0; return format ? format (v) : juce::String (v); }
    void adjust (int delta) { if (get && set) set (juce::jlimit (min, max, get() + delta)); }
    void assign (int v) { if (set) set (juce::jlimit (min, max, v)); }
};
using ParamPtr = std::shared_ptr<Param>;

struct SoftKey
{
    enum class Kind { None, Button, Value, Check, Header, Row, Label };

    Kind kind = Kind::None;
    juce::String title;           // first line / button text
    juce::String subtitle;        // second line for rows / labels
    ParamPtr param;               // for Value / Header
    std::function<void()> onPress;
    std::function<bool()> checked;
    bool highlight = false;       // e.g. selected list row
    bool arrows = false;          // shows "<>" (two-function key, e.g. APM inputs)
    bool enabled = true;
    int icon = 0;                 // rows: 0 none 1 folder 2 multi 3 program 4 sample 5 disk 6 image
    ParamPtr windowA, windowB;    // optional WINDOW parameters
    juce::String windowTitle, windowLabelA, windowLabelB;
};
using Keys = std::array<SoftKey, 16>;

//==============================================================================
/** Shared state for all pages. */
struct Ctx
{
    Ctx (S6000AudioProcessor& p);

    S6000AudioProcessor& proc;
    Memory& mem;
    DiskManager& disk;
    SamplerEngine& engine;
    PageManager* pages = nullptr;

    // current selections (as on the hardware these follow you between modes)
    int curProgram = 0, curKeygroup = 0, curZone = 0, curSample = 0, curPart = 0, partScroll = 0;
    bool editPartMode = false;
    int sampleMonitor = 0;      // EDIT SAMPLE > MONITOR : 0 RAW SAMPLE, 1 PROGRAM
    int waveLog = 1;            // EDIT SAMPLE > SAMPLE TOOLS > W/FORM : 1 LOG, 0 LIN

    Multi* multi() const;
    fmt::PartData* part() const;
    Program* program() const;
    fmt::KeygroupData* keygroup() const;
    Sample* sample() const;
    void clampSelections();

    juce::String note (int n) const;
    juce::String memoryLine (int i) const;
    void structural (const std::function<void()>& f);   // runs f under the memory lock + re-resolves

    // UNDO for destructive sample edits (one level, as on the hardware)
    Sample::Ptr undoCopy;
    juce::String undoName;
    void snapshot (Sample& s);
    bool undo();

    // feedback
    void info (const juce::String& title, const juce::StringArray& lines);
    void confirm (const juce::String& title, const juce::String& question, std::function<void()> yes);
    void error (const juce::String& text);
};

//==============================================================================
class Page
{
public:
    Page (Ctx& c, PageId i, PageId parentPage, Mode m) : ctx (c), id (i), parent (parentPage), mode (m) {}
    virtual ~Page() = default;

    virtual juce::String title() const = 0;                    // big text at the bottom
    virtual void build (Keys& k) = 0;
    virtual void paintCentre (juce::Graphics&, juce::Rectangle<float>) {}
    virtual bool listLayout() const { return false; }           // centre also covers the left column
    virtual void onShow() {}
    virtual void onHide() {}
    virtual bool wantsTicks() const { return false; }
    virtual void tick() {}
    virtual void wheel (int delta);
    virtual void cursor (int dir);
    virtual void enterPlay (bool down);
    virtual bool exitPressed() { return false; }                // true = handled
    virtual bool textInput (juce::juce_wchar) { return false; }
    virtual bool isPopup() const { return false; }
    virtual juce::Rectangle<float> popupBounds (juce::Rectangle<float> lcd) const { return lcd; }

    /** drawing outside the centre area (e.g. full width waveforms), before the soft keys */
    virtual void paintFull (juce::Graphics&, juce::Rectangle<float> /*lcd*/) {}
    /** true while something moves on screen (play line): the display repaints at ~30 fps */
    virtual bool wantsAnimation() { return false; }
    /** touch / mouse on the screen (LCD coordinates). Return true when handled. */
    virtual bool mouseDown (juce::Point<float>, int /*clicks*/, bool /*shift*/) { return false; }
    virtual void mouseDrag (juce::Point<float>) {}
    virtual void mouseUp (juce::Point<float>) {}
    virtual bool mouseWheel (juce::Point<float>, int /*delta*/) { return false; }

    Ctx& ctx;
    const PageId id;
    const PageId parent;
    const Mode mode;
    int selected = -1;          // selected soft key (0..15)
    Keys keys;                  // last built keys
    juce::String entry;         // numeric keypad entry buffer
    std::array<bool, 16> altMode {};   // two-function keys (APM: amount <-> source)
};

//==============================================================================
/** Pop-up base: draws a framed box and owns all 16 keys. */
class Popup : public Page
{
public:
    Popup (Ctx& c, const juce::String& t) : Page (c, PageId::None, PageId::None, Mode::None), heading (t) {}
    bool isPopup() const override { return true; }
    juce::String title() const override { return {}; }
    juce::Rectangle<float> popupBounds (juce::Rectangle<float> lcd) const override;
    void close();
    juce::String heading;
    float boxWidth = 0.56f, boxHeight = 0.62f;
    bool coversRightColumn = false;
};

//==============================================================================
class PageManager
{
public:
    explicit PageManager (Ctx& c);

    void add (std::unique_ptr<Page> p);
    Page* get (PageId id) const;
    Page* current() const { return currentPage; }
    Page* top() const { return popups.empty() ? currentPage : popups.back().get(); }

    void show (PageId id);
    void showMode (Mode m);
    void pushPopup (std::unique_ptr<Page> p);
    void closePopup (Page* p = nullptr);
    void closeAllPopups();
    bool hasPopup() const { return ! popups.empty(); }

    // panel input
    void press (int key);
    void wheel (int delta);
    void cursor (int dir);
    void keypad (juce::juce_wchar c);
    void enterPlay (bool down);
    void exit();
    void window();
    void undo();
    void mark();
    void jump();
    void userKey (int index, bool store);
    bool textInput (juce::juce_wchar c);

    // touch screen / mouse (LCD coordinates)
    bool mouseDown (juce::Point<float> p, int clicks, bool shift);
    void mouseDrag (juce::Point<float> p);
    void mouseUp (juce::Point<float> p);
    bool mouseWheel (juce::Point<float> p, int delta);
    bool animating();

    void refresh();
    bool tick();                     // ~20 Hz from the editor
    void paint (juce::Graphics& g, juce::Rectangle<float> lcd);
    void setPowered (bool on) { powered = on; refresh(); }
    void setBooting (bool b) { booting = b; }
    Mode activeMode() const;

    std::function<void()> onRepaint;
    std::function<void (PageId)> onPageChanged;

    PageId markedPage = PageId::None;
    std::array<PageId, 3> userKeys { PageId::Multi, PageId::ProgMain, PageId::SampMain };
    std::array<PageId, 8> lastPageOfMode {};

private:
    Ctx& ctx;
    std::vector<std::unique_ptr<Page>> pages;
    std::vector<std::unique_ptr<Page>> popups;
    Page* currentPage = nullptr;
    Page* mousePage = nullptr;
    bool powered = true, booting = false;

    void paintKeys (juce::Graphics& g, juce::Rectangle<float> lcd, Page& p, bool dim, const Keys* mask = nullptr);
};

//==============================================================================
/** Geometry inside the LCD (coordinates relative to the LCD rectangle). */
namespace geo
{
    constexpr float boxW = 152.0f, boxH = 33.0f, margin = 4.0f;
    constexpr float lineH = 18.0f;        // list rows
    constexpr int listRows = 12;
    /** y of the soft key row, so the boxes line up with the physical keys */
    inline float rowY (juce::Rectangle<float> lcd, int row)
    {
        return lcd.getY() + (layout::firstKeyY - (float) layout::lcd.getY()) + layout::keyPitch * (float) row;
    }
    inline juce::Rectangle<float> keyBox (juce::Rectangle<float> lcd, int i)
    {
        const float x = i < 8 ? lcd.getX() + margin : lcd.getRight() - margin - boxW;
        return { x, rowY (lcd, i % 8) - boxH * 0.5f, boxW, boxH };
    }
    inline juce::Rectangle<float> headerBox (juce::Rectangle<float> lcd)
    {
        return { lcd.getX() + margin, rowY (lcd, 0) - 14.0f, lcd.getWidth() - 2 * margin - boxW - 8.0f, 28.0f };
    }
    inline juce::Rectangle<float> centre (juce::Rectangle<float> lcd)
    {
        return { lcd.getX() + margin + boxW + 6.0f, rowY (lcd, 0) + 18.0f, lcd.getWidth() - 2 * (margin + boxW + 6.0f), rowY (lcd, 7) - rowY (lcd, 0) - 38.0f };
    }
    inline juce::Rectangle<float> listArea (juce::Rectangle<float> lcd)
    {
        return { lcd.getX() + margin, rowY (lcd, 0) + 16.0f, lcd.getWidth() - 2 * margin - boxW - 8.0f, rowY (lcd, 7) - rowY (lcd, 0) - 34.0f };
    }
    /** list row index next to soft key i (F2..F7) on list pages (first line = path header) */
    inline int rowForKey (int i)
    {
        const float keyY = layout::keyPitch * (float) i;                // relative to row 0
        return juce::jmax (0, (int) ((keyY - 16.0f - lineH) / lineH));
    }
    /** top of a part box aligned with key i, relative to listArea().getY() */
    inline float boxTopForKey (int i) { return layout::keyPitch * (float) i - 16.0f - boxH * 0.5f; }
}

//==============================================================================
// OLED drawing helpers
namespace oled
{
    void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
               float size = 11.0f, bool bold = false, juce::Colour c = colours::oledText);
    void box (juce::Graphics& g, juce::Rectangle<float> r, bool filled = false, float thickness = 1.0f);
    void memoryBox (juce::Graphics& g, juce::Rectangle<float> r, Ctx& ctx);
    void keyboard (juce::Graphics& g, juce::Rectangle<float> r, int lo, int hi, int markLo = -1, int markHi = -1);
    void waveform (juce::Graphics& g, juce::Rectangle<float> r, const Sample& s, juce::int64 from, juce::int64 to);
    void envelopeAdsr (juce::Graphics& g, juce::Rectangle<float> r, int a, int d, int s, int rel);
    void icon (juce::Graphics& g, int type, juce::Rectangle<float> r, juce::Colour c);
    void meter (juce::Graphics& g, juce::Rectangle<float> r, float level);
}

//==============================================================================
// key builders
namespace kb
{
    using Fmt = std::function<juce::String (int)>;
    using IntRef = std::function<int*()>;

    SoftKey button (const juce::String& t, std::function<void()> f, bool enabled = true);
    SoftKey value (const juce::String& t, IntRef ref, int lo, int hi, Fmt fmt = {}, std::function<void()> changed = {});
    SoftKey valueFn (const juce::String& t, std::function<int()> get, std::function<void (int)> set, int lo, int hi, Fmt fmt = {});
    SoftKey check (const juce::String& t, IntRef ref, std::function<void()> changed = {});
    SoftKey label (const juce::String& t, const juce::String& sub = {});
    SoftKey header (const juce::String& t, ParamPtr p = {});
    SoftKey row (const juce::String& t, const juce::String& sub, int icon, bool highlight, std::function<void()> f);

    Fmt num();
    Fmt signed3();
    Fmt signed2();
    Fmt list (const juce::StringArray& items);
    Fmt onOff();
    Fmt pan();
    Fmt db();
    Fmt note (Ctx& c);
    Fmt suffix (const juce::String& s);
}

} // namespace s6k::ui
