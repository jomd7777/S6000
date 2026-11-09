/*
  ==============================================================================
   S6000 Emulation - reusable pop-up windows
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#pragma once
#include "PageSystem.h"

namespace s6k::ui
{
/** Simple message: lines of text + CLOSE */
class InfoPopup : public Popup
{
public:
    InfoPopup (Ctx& c, const juce::String& t, const juce::StringArray& l) : Popup (c, t), lines (l) { boxWidth = 0.62f; }
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    juce::StringArray lines;
};

/** Question with EXECUTE / CANCEL (+ optional check boxes) */
class PromptPopup : public Popup
{
public:
    struct Check { juce::String label; bool value = false; int key = 0; };

    PromptPopup (Ctx& c, const juce::String& t, const juce::String& q) : Popup (c, t), question (q) {}
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;

    juce::String question;
    std::vector<Check> checks;
    std::function<void (PromptPopup&)> onExecute;
    juce::String executeLabel { "EXECUTE" }, cancelLabel { "CANCEL" };
    bool isChecked (int i) const { return i < (int) checks.size() && checks[(size_t) i].value; }
};

/** Drop down TOOLS menu on the right column (F9 = CLOSE TOOLS) */
class MenuPopup : public Popup
{
public:
    struct Item { juce::String label; std::function<void()> action; int key = -1; bool enabled = true; };
    MenuPopup (Ctx& c, const juce::String& t, std::vector<Item> i) : Popup (c, t), items (std::move (i)) { coversRightColumn = true; }
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    std::vector<Item> items;
};

/** NAMING: DATA wheel + CURSOR or the computer keyboard */
class NamingPopup : public Popup
{
public:
    NamingPopup (Ctx& c, const juce::String& t, const juce::String& initial, int maxLen, std::function<void (const juce::String&)> done);
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    void wheel (int delta) override;
    void cursor (int dir) override;
    void enterPlay (bool down) override;
    bool textInput (juce::juce_wchar c) override;
    juce::String name;
    int pos = 0, maxLength = 20;
    std::function<void (const juce::String&)> onDone;
    static const juce::String charset;
};

/** Pick one item from a list (DATA wheel / SELECT keys) */
class ListPopup : public Popup
{
public:
    ListPopup (Ctx& c, const juce::String& t, juce::StringArray items, int initial, std::function<void (int)> chosen);
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    void wheel (int delta) override;
    void enterPlay (bool down) override;
    juce::StringArray items;
    int index = 0;
    std::function<void (int)> onChosen;
};

/** WINDOW key: edit the selected parameter (+ its partner) in a larger view */
class WindowPopup : public Popup
{
public:
    WindowPopup (Ctx& c, const juce::String& t, ParamPtr a, ParamPtr b, const juce::String& la, const juce::String& lb);
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    ParamPtr pa, pb;
    juce::String labelA, labelB;
};

/** LOADING / MOUNTING progress (F8 = CANCEL) */
class ProgressPopup : public Popup
{
public:
    explicit ProgressPopup (Ctx& c);
    void build (Keys& k) override;
    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override;
    bool wantsTicks() const override { return true; }
    void tick() override;
    int frame = 0;
};

} // namespace s6k::ui
