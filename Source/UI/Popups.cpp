/*
  ==============================================================================
   S6000 Emulation - pop-up windows
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "Popups.h"
#include "../Core/DiskManager.h"

namespace s6k::ui
{
//==============================================================================
void InfoPopup::build (Keys& k)
{
    k[15] = kb::button ("CLOSE", [this] { close(); });
}

void InfoPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromTop (4.0f);
    for (auto& l : lines)
        oled::text (g, l, r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
}

//==============================================================================
void PromptPopup::build (Keys& k)
{
    k[7] = kb::button (cancelLabel, [this] { close(); });
    k[15] = kb::button (executeLabel, [this]
    {
        auto cb = onExecute;
        PromptPopup* self = this;
        close();
        if (cb) cb (*self);
    });
    for (size_t i = 0; i < checks.size(); ++i)
    {
        SoftKey c;
        c.kind = SoftKey::Kind::Check;
        c.title = checks[i].label;
        c.checked = [this, i] { return checks[i].value; };
        c.onPress = [this, i] { checks[i].value = ! checks[i].value; };
        k[(size_t) juce::jlimit (0, 15, checks[i].key)] = c;
    }
}

void PromptPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromTop (6.0f);
    for (auto& l : juce::StringArray::fromLines (question))
        oled::text (g, l, r.removeFromTop (16.0f), juce::Justification::centred, 11.0f);
}

//==============================================================================
void MenuPopup::build (Keys& k)
{
    k[8] = kb::button ("CLOSE TOOLS", [this] { close(); });
    int next = 9;
    for (size_t i = 0; i < items.size(); ++i)
    {
        int key = items[i].key;
        if (key < 0) { key = next; next = next >= 15 ? 1 : next + 1; }
        auto act = items[i].action;
        k[(size_t) juce::jlimit (0, 15, key)] = kb::row (items[i].label, {}, 0, false, [this, act]
        {
            close();
            if (act) act();
        });
        k[(size_t) juce::jlimit (0, 15, key)].enabled = items[i].enabled;
    }
}

void MenuPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    oled::text (g, "Select a function", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
    oled::text (g, "F9 = CLOSE TOOLS", r.removeFromBottom (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
}

//==============================================================================
const juce::String NamingPopup::charset = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz#+-._!&()";

NamingPopup::NamingPopup (Ctx& c, const juce::String& t, const juce::String& initial, int maxLen, std::function<void (const juce::String&)> done)
    : Popup (c, t), name (initial.substring (0, maxLen)), maxLength (maxLen), onDone (std::move (done))
{
    boxWidth = 0.6f; boxHeight = 0.5f;
    pos = juce::jmin (name.length(), maxLength - 1);
}

void NamingPopup::build (Keys& k)
{
    k[7] = kb::button ("CANCEL", [this] { close(); });
    k[12] = kb::button ("CLEAR", [this] { name.clear(); pos = 0; });
    k[13] = kb::button ("INSERT", [this] { if (name.length() < maxLength) name = name.substring (0, pos) + " " + name.substring (pos); });
    k[14] = kb::button ("DELETE", [this] { name = name.substring (0, pos) + name.substring (pos + 1); });
    k[15] = kb::button ("ENTER", [this]
    {
        auto cb = onDone; auto n = name.trim();
        close();
        if (cb && n.isNotEmpty()) cb (n);
    });
}

void NamingPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromTop (10.0f);
    auto line = r.removeFromTop (22.0f).reduced (8.0f, 0);
    const auto f = fonts::oled (12.0f, true);
    const float cw = juce::GlyphArrangement::getStringWidth (f, "M");
    const juce::String shown = name.paddedRight (' ', juce::jmax (pos + 1, name.length()));
    for (int i = 0; i < shown.length(); ++i)
    {
        auto cr = juce::Rectangle<float> (line.getX() + cw * (float) i, line.getY(), cw, line.getHeight());
        const bool cur = i == pos;
        if (cur) { g.setColour (colours::oledText); g.fillRect (cr); }
        g.setFont (f);
        g.setColour (cur ? colours::oledBg : colours::oledText);
        g.drawText (shown.substring (i, i + 1), cr, juce::Justification::centred);
    }
    r.removeFromTop (8.0f);
    oled::text (g, "DATA = character   CURSOR = move", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
    oled::text (g, "or type on the computer keyboard", r.removeFromTop (16.0f), juce::Justification::centred, 10.0f, false, colours::oledDim);
}

void NamingPopup::wheel (int delta)
{
    juce::String s = name.paddedRight (' ', pos + 1);
    int ci = charset.indexOfChar (s[pos]);
    if (ci < 0) ci = 0;
    ci = (ci + delta) % charset.length();
    if (ci < 0) ci += charset.length();
    name = s.substring (0, pos) + juce::String::charToString (charset[ci]) + s.substring (pos + 1);
}

void NamingPopup::cursor (int dir)
{
    pos = juce::jlimit (0, maxLength - 1, pos + dir);
}

void NamingPopup::enterPlay (bool down)
{
    if (! down) return;
    auto cb = onDone; auto n = name.trim();
    close();
    if (cb && n.isNotEmpty()) cb (n);
}

bool NamingPopup::textInput (juce::juce_wchar c)
{
    if (c == 8 || c == 127)
    {
        if (pos > 0) { name = name.substring (0, pos - 1) + name.substring (pos); --pos; }
        return true;
    }
    if (c < 32 || c > 126) return false;
    juce::String s = name.paddedRight (' ', pos);
    s = s.substring (0, pos) + juce::String::charToString (c) + s.substring (pos + 1);
    name = s.substring (0, maxLength);
    pos = juce::jmin (maxLength - 1, pos + 1);
    return true;
}

//==============================================================================
ListPopup::ListPopup (Ctx& c, const juce::String& t, juce::StringArray i, int initial, std::function<void (int)> chosen)
    : Popup (c, t), items (std::move (i)), index (juce::jlimit (0, juce::jmax (0, items.size() - 1), initial)), onChosen (std::move (chosen))
{
    boxWidth = 0.6f; boxHeight = 0.74f;
}

void ListPopup::build (Keys& k)
{
    k[7] = kb::button ("CANCEL", [this] { close(); });
    k[13] = kb::button ("SELECT", [this]
    {
        auto cb = onChosen; const int i = index;
        close();
        if (cb && items.size() > 0) cb (i);
    });
    k[14] = kb::button (juce::String (juce::CharPointer_UTF8 ("SCROLL \xe2\x96\xb2")), [this] { wheel (-1); });
    k[15] = kb::button (juce::String (juce::CharPointer_UTF8 ("SCROLL \xe2\x96\xbc")), [this] { wheel (1); });
}

void ListPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    const int visible = juce::jmax (1, (int) (r.getHeight() / 15.0f));
    const int first = juce::jlimit (0, juce::jmax (0, items.size() - visible), index - visible / 2);
    for (int i = first; i < juce::jmin (items.size(), first + visible); ++i)
    {
        auto line = r.removeFromTop (geo::lineH);
        if (i == index) { g.setColour (colours::oledText); g.fillRect (line); }
        oled::text (g, items[i], line.reduced (6.0f, 0), juce::Justification::centredLeft, 11.0f, false,
                    i == index ? colours::oledBg : colours::oledText);
    }
    if (items.isEmpty()) oled::text (g, "(EMPTY)", r, juce::Justification::centred, 11.0f);
}

void ListPopup::wheel (int delta)
{
    index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index + delta);
}

void ListPopup::enterPlay (bool down)
{
    if (! down) return;
    auto cb = onChosen; const int i = index;
    close();
    if (cb && items.size() > 0) cb (i);
}

//==============================================================================
WindowPopup::WindowPopup (Ctx& c, const juce::String& t, ParamPtr a, ParamPtr b, const juce::String& la, const juce::String& lb)
    : Popup (c, t + " WINDOW"), pa (std::move (a)), pb (std::move (b)), labelA (la), labelB (lb)
{
    boxWidth = 0.58f; boxHeight = 0.5f;
    selected = 1;
}

void WindowPopup::build (Keys& k)
{
    if (pa != nullptr) { k[1].kind = SoftKey::Kind::Value; k[1].title = labelA.isNotEmpty() ? labelA : "Value"; k[1].param = pa; }
    if (pb != nullptr) { k[2].kind = SoftKey::Kind::Value; k[2].title = labelB.isNotEmpty() ? labelB : "Value 2"; k[2].param = pb; }
    k[15] = kb::button ("CLOSE", [this] { close(); });
}

void WindowPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromTop (6.0f);
    auto drawParam = [&] (const ParamPtr& p, const juce::String& lab, bool sel)
    {
        if (p == nullptr) return;
        auto line = r.removeFromTop (34.0f).reduced (8.0f, 2.0f);
        if (sel) { g.setColour (colours::oledText); g.drawRect (line, 1.0f); }
        oled::text (g, lab, line.removeFromLeft (line.getWidth() * 0.45f).reduced (4, 0), juce::Justification::centredLeft, 11.0f);
        oled::text (g, p->text(), line.reduced (4, 0), juce::Justification::centredRight, 12.0f, true);
    };
    drawParam (pa, labelA.isNotEmpty() ? labelA : "VALUE", selected == 1);
    drawParam (pb, labelB.isNotEmpty() ? labelB : "VALUE 2", selected == 2);
}

//==============================================================================
ProgressPopup::ProgressPopup (Ctx& c) : Popup (c, "DISK")
{
    boxWidth = 0.62f; boxHeight = 0.42f;
}

void ProgressPopup::build (Keys& k)
{
    k[7] = kb::button ("CANCEL", [this] { ctx.disk.cancel(); });
}

void ProgressPopup::paintCentre (juce::Graphics& g, juce::Rectangle<float> r)
{
    r.removeFromTop (6.0f);
    auto st = ctx.disk.getStatus();
    if (st.isEmpty()) st = "PLEASE WAIT";
    oled::text (g, st, r.removeFromTop (18.0f).reduced (6, 0), juce::Justification::centred, 11.0f, true);
    auto bar = r.removeFromTop (16.0f).reduced (16.0f, 3.0f);
    g.setColour (colours::oledText);
    g.drawRect (bar, 1.0f);
    const int pct = ctx.disk.getProgressPercent();
    if (pct > 0) g.fillRect (bar.reduced (2.0f).withWidth ((bar.getWidth() - 4.0f) * (float) pct / 100.0f));
    else
    {
        const float w = bar.getWidth() * 0.2f;
        const float x = bar.getX() + 2.0f + std::fmod ((float) frame * 6.0f, bar.getWidth() - w - 4.0f);
        g.fillRect (juce::Rectangle<float> (x, bar.getY() + 2.0f, w, bar.getHeight() - 4.0f));
    }
}

void ProgressPopup::tick()
{
    ++frame;
    if (! ctx.disk.isBusy()) close();
}

} // namespace s6k::ui
