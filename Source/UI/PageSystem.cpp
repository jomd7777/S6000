/*
  ==============================================================================
   S6000 Emulation - page system implementation
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "PageSystem.h"
#include "Popups.h"
#include "../PluginProcessor.h"

namespace s6k::ui
{
//==============================================================================
Ctx::Ctx (S6000AudioProcessor& p) : proc (p), mem (p.memory), disk (p.disk), engine (p.engine) {}

Multi* Ctx::multi() const { return mem.getCurrentMulti(); }

fmt::PartData* Ctx::part() const
{
    auto* m = multi();
    return (m != nullptr && juce::isPositiveAndBelow (curPart, (int) m->parts.size())) ? &m->parts[(size_t) curPart] : nullptr;
}

Program* Ctx::program() const
{
    return juce::isPositiveAndBelow (curProgram, mem.programs.size()) ? mem.programs.getObjectPointerUnchecked (curProgram) : nullptr;
}

fmt::KeygroupData* Ctx::keygroup() const
{
    auto* p = program();
    if (p == nullptr || ! juce::isPositiveAndBelow (curKeygroup, (int) p->data.keygroups.size())) return nullptr;
    return &p->data.keygroups[(size_t) curKeygroup];
}

Sample* Ctx::sample() const
{
    return juce::isPositiveAndBelow (curSample, mem.samples.size()) ? mem.samples.getObjectPointerUnchecked (curSample) : nullptr;
}

void Ctx::clampSelections()
{
    curProgram = juce::jlimit (0, juce::jmax (0, mem.programs.size() - 1), curProgram);
    curSample = juce::jlimit (0, juce::jmax (0, mem.samples.size() - 1), curSample);
    curPart = juce::jlimit (0, maxParts - 1, curPart);
    curZone = juce::jlimit (0, 3, curZone);
    if (auto* p = program()) curKeygroup = juce::jlimit (0, juce::jmax (0, p->numKeygroups() - 1), curKeygroup);
    else curKeygroup = 0;
    mem.currentMulti = juce::jlimit (0, juce::jmax (0, mem.multis.size() - 1), mem.currentMulti.load());
}

juce::String Ctx::note (int n) const { return names::note (n, proc.prefs.noteDisplay == 0); }

juce::String Ctx::memoryLine (int i) const
{
    switch (i)
    {
        case 0: return juce::String (mem.multis.size()) + (mem.multis.size() == 1 ? " Multi" : " Multis");
        case 1: return juce::String (mem.programs.size()) + (mem.programs.size() == 1 ? " Program" : " Programs");
        case 2: return juce::String (mem.samples.size()) + (mem.samples.size() == 1 ? " Sample" : " Samples");
        default: return juce::String (mem.percentFree()) + "% Free";
    }
}

void Ctx::structural (const std::function<void()>& f)
{
    {
        const juce::SpinLock::ScopedLockType sl (mem.lock);
        f();
        mem.resolveAll();
    }
    clampSelections();
}

void Ctx::snapshot (Sample& s)
{
    undoCopy = s.clone (s.name);
    undoName = s.name;
}

bool Ctx::undo()
{
    if (undoCopy == nullptr) return false;
    auto target = mem.findSample (undoName);
    if (target == nullptr) return false;
    auto redo = target->clone (target->name);
    {
        const juce::SpinLock::ScopedLockType sl (mem.lock);
        target->buffer.makeCopyOf (undoCopy->buffer);
        target->sampleRate = undoCopy->sampleRate;
        target->start = undoCopy->start; target->end = undoCopy->end;
        target->loopStart = undoCopy->loopStart; target->loopEnd = undoCopy->loopEnd;
        target->loopEnabled = undoCopy->loopEnabled; target->loopXfade = undoCopy->loopXfade;
        target->touch();
    }
    undoCopy = redo;   // UNDO again = REDO
    return true;
}

void Ctx::info (const juce::String& title, const juce::StringArray& lines)
{
    if (pages != nullptr) pages->pushPopup (std::make_unique<InfoPopup> (*this, title, lines));
}

void Ctx::confirm (const juce::String& title, const juce::String& question, std::function<void()> yes)
{
    if (pages == nullptr) return;
    auto p = std::make_unique<PromptPopup> (*this, title, question);
    p->onExecute = [yes] (PromptPopup&) { if (yes) yes(); };
    pages->pushPopup (std::move (p));
}

void Ctx::error (const juce::String& text)
{
    info ("ERROR", juce::StringArray::fromLines (text));
}

//==============================================================================
void Page::wheel (int delta)
{
    if (! juce::isPositiveAndBelow (selected, 16)) return;
    auto& k = keys[(size_t) selected];
    if (k.param != nullptr)
    {
        const int range = k.param->max - k.param->min;
        const int step = (std::abs (delta) > 1 && range > 200) ? delta * juce::jmax (1, range / 400) : delta;
        k.param->adjust (step);
    }
}

void Page::cursor (int dir)
{
    if (! juce::isPositiveAndBelow (selected, 16)) return;
    auto& k = keys[(size_t) selected];
    if (k.param != nullptr) k.param->adjust (dir * k.param->bigStep);
}

void Page::enterPlay (bool down)
{
    const int key = ctx.proc.prefs.playKey;
    ctx.engine.injectNote (0, key, 100, down);
}

//==============================================================================
juce::Rectangle<float> Popup::popupBounds (juce::Rectangle<float> lcd) const
{
    // the window sits in the centre, never on top of the soft-key boxes it uses
    bool usesLeft = false, usesRight = false;
    for (int i = 0; i < 16; ++i)
        if (keys[(size_t) i].kind != SoftKey::Kind::None) (i < 8 ? usesLeft : usesRight) = true;
    const float gap = geo::margin + geo::boxW + 8.0f;
    const float x1 = usesLeft ? lcd.getX() + gap : lcd.getX() + geo::margin;
    const float x2 = usesRight ? lcd.getRight() - gap : lcd.getRight() - geo::margin;
    const float y1 = geo::rowY (lcd, 0) - geo::boxH * 0.5f;
    const float y2 = geo::rowY (lcd, 7) + geo::boxH * 0.5f;
    return { x1, y1, x2 - x1, y2 - y1 };
}

void Popup::close()
{
    if (ctx.pages != nullptr) ctx.pages->closePopup (this);
}

//==============================================================================
PageManager::PageManager (Ctx& c) : ctx (c)
{
    ctx.pages = this;
    lastPageOfMode.fill (PageId::None);
}

void PageManager::add (std::unique_ptr<Page> p) { pages.push_back (std::move (p)); }

Page* PageManager::get (PageId id) const
{
    for (auto& p : pages) if (p->id == id) return p.get();
    return nullptr;
}

Mode PageManager::activeMode() const
{
    return currentPage != nullptr ? currentPage->mode : Mode::None;
}

void PageManager::show (PageId id)
{
    auto* p = get (id);
    if (p == nullptr) return;
    mousePage = nullptr;
    popups.clear();
    if (currentPage != nullptr && currentPage != p) currentPage->onHide();
    currentPage = p;
    if (p->mode != Mode::None) lastPageOfMode[(size_t) p->mode] = id;
    p->entry.clear();
    ctx.clampSelections();
    p->onShow();

    refresh();
    if (onPageChanged) onPageChanged (id);
}

void PageManager::showMode (Mode m)
{
    static const PageId mainPages[] = { PageId::Multi, PageId::Fx, PageId::SampMain, PageId::ProgMain,
                                        PageId::Record, PageId::Util, PageId::Save, PageId::Load };
    if (m == Mode::None) return;
    const PageId main = mainPages[(int) m];
    if (m != Mode::Program) ctx.editPartMode = (m == Mode::Multi) ? false : ctx.editPartMode;
    if (m == Mode::Program && activeMode() != Mode::Program) ctx.editPartMode = false;

    if (activeMode() == m && currentPage != nullptr && currentPage->id != main && ! hasPopup())
        show (main);
    else
    {
        const PageId last = lastPageOfMode[(size_t) m];
        show (last != PageId::None ? last : main);
    }
}

void PageManager::pushPopup (std::unique_ptr<Page> p)
{
    popups.push_back (std::move (p));
    refresh();
}

void PageManager::closePopup (Page* p)
{
    if (popups.empty()) return;
    if (p == nullptr) { popups.pop_back(); refresh(); return; }
    for (auto it = popups.begin(); it != popups.end(); ++it)
        if (it->get() == p)
        {
            // keep the object alive until the current call stack unwinds
            auto keep = std::shared_ptr<Page> (std::move (*it));
            popups.erase (it);
            juce::MessageManager::callAsync ([keep] {});
            break;
        }
    refresh();
}

void PageManager::closeAllPopups()
{
    popups.clear();
    refresh();
}

void PageManager::press (int key)
{
    if (! powered || booting || ! juce::isPositiveAndBelow (key, 16)) return;
    Page* p = top();
    if (p == nullptr) return;
    auto k = p->keys[(size_t) key];   // copy: actions may rebuild the keys
    if (! k.enabled) return;

    switch (k.kind)
    {
        case SoftKey::Kind::None:
        case SoftKey::Kind::Label:
            return;
        case SoftKey::Kind::Button:
        case SoftKey::Kind::Row:
            p->selected = key;
            if (k.onPress) k.onPress();
            break;
        case SoftKey::Kind::Check:
            if (k.onPress) k.onPress();
            else if (k.param != nullptr) k.param->assign (k.param->get() ? 0 : 1);
            break;
        case SoftKey::Kind::Value:
        case SoftKey::Kind::Header:
            if (p->selected == key && k.onPress) k.onPress();
            p->selected = key;
            p->entry.clear();
            break;
    }
    refresh();
}

void PageManager::wheel (int delta)
{
    if (! powered || booting) return;
    if (auto* p = top()) { p->wheel (delta); refresh(); }
}

void PageManager::cursor (int dir)
{
    if (! powered || booting) return;
    if (auto* p = top()) { p->cursor (dir); refresh(); }
}

void PageManager::keypad (juce::juce_wchar c)
{
    if (! powered || booting) return;
    Page* p = top();
    if (p == nullptr) return;
    if (c >= '0' && c <= '9')
    {
        if (p->entry.length() < 8) p->entry += juce::String::charToString (c);
    }
    else if (c == '-')
    {
        if (p->entry.isEmpty()) p->wheel (-1);
        else p->entry = p->entry.startsWith ("-") ? p->entry.substring (1) : "-" + p->entry;
    }
    else if (c == '+')
    {
        if (p->entry.isEmpty()) p->wheel (1);
    }
    refresh();
}

void PageManager::enterPlay (bool down)
{
    if (! powered || booting) return;
    Page* p = top();
    if (p == nullptr) return;
    if (down && p->entry.isNotEmpty())
    {
        if (juce::isPositiveAndBelow (p->selected, 16))
            if (auto& prm = p->keys[(size_t) p->selected].param) prm->assign (p->entry.getIntValue());
        p->entry.clear();
        refresh();
        return;
    }
    p->enterPlay (down);
    refresh();
}

void PageManager::exit()
{
    if (! powered || booting) return;
    if (! popups.empty()) { popups.pop_back(); refresh(); return; }
    if (currentPage == nullptr) return;
    if (currentPage->entry.isNotEmpty()) { currentPage->entry.clear(); refresh(); return; }
    if (currentPage->exitPressed()) { refresh(); return; }
    if (currentPage->parent != PageId::None) show (currentPage->parent);
}

void PageManager::window()
{
    if (! powered || booting) return;
    Page* p = top();
    if (p == nullptr) return;
    if (juce::isPositiveAndBelow (p->selected, 16))
    {
        const auto& k = p->keys[(size_t) p->selected];
        if (k.windowA != nullptr)
        {
            pushPopup (std::make_unique<WindowPopup> (ctx, k.windowTitle.isNotEmpty() ? k.windowTitle : k.title,
                                                      k.windowA, k.windowB, k.windowLabelA, k.windowLabelB));
            return;
        }
        if (k.param != nullptr)
        {
            pushPopup (std::make_unique<WindowPopup> (ctx, k.title, k.param, nullptr, k.title, ""));
            return;
        }
    }
    ctx.info ("WINDOW", { "No window for", "this parameter" });
}

void PageManager::undo()
{
    if (! powered || booting) return;
    if (ctx.undo()) ctx.info ("UNDO", { "Last sample edit", "has been undone.", "Press UNDO again to redo." });
    else ctx.info ("UNDO", { "Nothing to undo" });
}

void PageManager::mark()
{
    if (currentPage != nullptr) markedPage = currentPage->id;
    ctx.info ("MARK", { "Page marked.", "Press JUMP to return here." });
}

void PageManager::jump()
{
    if (markedPage != PageId::None) show (markedPage);
}

void PageManager::userKey (int i, bool store)
{
    if (! juce::isPositiveAndBelow (i, 3) || ! powered) return;
    if (store)
    {
        if (currentPage != nullptr) userKeys[(size_t) i] = currentPage->id;
        ctx.info ("USER KEY " + juce::String (i + 1), { "Page stored.", "(right-click / shift-click", " on a USER KEY stores)" });
    }
    else
        show (userKeys[(size_t) i]);
}

bool PageManager::textInput (juce::juce_wchar c)
{
    if (! powered || booting) return false;
    if (auto* p = top())
        if (p->textInput (c)) { refresh(); return true; }
    return false;
}

bool PageManager::mouseDown (juce::Point<float> p, int clicks, bool shift)
{
    mousePage = nullptr;
    if (! powered || booting) return false;
    if (auto* t = top())
        if (t->mouseDown (p, clicks, shift)) { mousePage = t; refresh(); return true; }
    return false;
}

void PageManager::mouseDrag (juce::Point<float> p)
{
    if (mousePage != nullptr && mousePage == top()) { mousePage->mouseDrag (p); refresh(); }
}

void PageManager::mouseUp (juce::Point<float> p)
{
    if (mousePage != nullptr && mousePage == top()) { mousePage->mouseUp (p); refresh(); }
    mousePage = nullptr;
}

bool PageManager::mouseWheel (juce::Point<float> p, int delta)
{
    if (! powered || booting) return false;
    if (auto* t = top())
        if (t->mouseWheel (p, delta)) { refresh(); return true; }
    return false;
}

bool PageManager::animating()
{
    if (! powered || booting) return false;
    bool any = currentPage != nullptr && currentPage->wantsAnimation();
    for (auto& p : popups) any = p->wantsAnimation() || any;
    return any;
}

bool PageManager::tick()
{
    bool any = false;
    std::vector<Page*> ps;
    if (currentPage != nullptr) ps.push_back (currentPage);
    for (auto& p : popups) ps.push_back (p.get());
    for (auto* p : ps)
        if (p->wantsTicks()) { p->tick(); any = true; }
    if (any) refresh();
    return any;
}

void PageManager::refresh()
{
    // what the MIDI keyboard plays depends on the mode (as on the hardware):
    //  EDIT PROGRAM -> the edited program on every channel (EDIT PART keeps the multi)
    //  EDIT SAMPLE  -> MONITOR RAW SAMPLE: the selected sample / PROGRAM: the current program
    //  other modes  -> the multi
    {
        const Mode m = currentPage != nullptr ? currentPage->mode : Mode::None;
        int ep = -1, es = -1;
        if (m == Mode::Program && ! ctx.editPartMode && ctx.program() != nullptr) ep = ctx.curProgram;
        if (m == Mode::Sample)
        {
            if (ctx.sampleMonitor == 0 && ctx.sample() != nullptr) es = ctx.curSample;
            else if (ctx.sampleMonitor == 1 && ctx.program() != nullptr) ep = ctx.curProgram;
        }
        ctx.engine.settings.editProgram = ep;
        ctx.engine.settings.editSample = es;
    }

    if (currentPage != nullptr)
    {
        Keys k;
        currentPage->build (k);
        currentPage->keys = k;
    }
    for (auto& p : popups)
    {
        Keys k;
        p->build (k);
        p->keys = k;
    }
    if (onRepaint) onRepaint();
}

//==============================================================================
void PageManager::paintKeys (juce::Graphics& g, juce::Rectangle<float> lcd, Page& p, bool dim, const Keys* mask)
{
    using namespace oled;
    for (int i = 0; i < 16; ++i)
    {
        const auto& k = p.keys[(size_t) i];
        if (k.kind == SoftKey::Kind::None) continue;
        if (mask != nullptr && (*mask)[(size_t) i].kind != SoftKey::Kind::None) continue;   // covered by a pop-up key
        const bool sel = (p.selected == i) && (k.kind == SoftKey::Kind::Value || k.kind == SoftKey::Kind::Header);
        const auto col = dim ? colours::oledDim : colours::oledText;

        if (k.kind == SoftKey::Kind::Header)
        {
            // two headers (JOIN / MIX: sample A on F1, sample B on F9) split the top line
            auto r = geo::headerBox (lcd);
            if (p.keys[8].kind == SoftKey::Kind::Header)
            {
                const float mid = lcd.getCentreX();
                r = i == 8 ? juce::Rectangle<float> (mid + 4.0f, r.getY(), lcd.getRight() - geo::margin - mid - 4.0f, r.getHeight())
                           : r.withRight (mid - 4.0f);
            }
            g.setColour (col);
            g.fillRect (r);
            const auto txt = k.param != nullptr ? k.param->text() : k.title;
            text (g, (sel ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")) : juce::String()) + txt, r.reduced (6, 0), juce::Justification::centredLeft, 12.0f, true, colours::oledBg);
            if (k.subtitle.isNotEmpty()) text (g, k.subtitle, r.reduced (5, 0), juce::Justification::centredRight, 10.0f, false, colours::oledBg);
            continue;
        }

        const auto r = geo::keyBox (lcd, i);
        const auto textCol = sel ? colours::oledBg : (k.enabled ? col : colours::oledDim);

        switch (k.kind)
        {
            case SoftKey::Kind::Label:
                text (g, k.title, r.withHeight (16.0f).translated (0, 1.0f), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
                text (g, k.subtitle, r.withTrimmedTop (16.0f), juce::Justification::centredLeft, 11.0f, false, col);
                break;

            case SoftKey::Kind::Button:
            case SoftKey::Kind::Row:
            {
                if (k.kind == SoftKey::Kind::Row && k.title.isEmpty()) break;   // invisible list selector
                g.setColour (k.highlight ? col : col.withAlpha (k.enabled ? 1.0f : 0.5f));
                if (k.highlight) g.fillRoundedRectangle (r.reduced (1.0f, 4.0f), 2.0f);
                else g.drawRoundedRectangle (r.reduced (1.0f, 4.0f), 2.0f, 1.0f);
                text (g, k.title, r.reduced (5.0f, 4.0f), k.kind == SoftKey::Kind::Row ? juce::Justification::centredLeft : juce::Justification::centred,
                      10.5f, false, k.highlight ? colours::oledBg : (k.enabled ? col : colours::oledDim));
                break;
            }

            case SoftKey::Kind::Check:
            {
                g.setColour (col);
                g.drawRoundedRectangle (r.reduced (1.0f, 4.0f), 2.0f, 1.0f);
                const bool on = k.checked ? k.checked() : (k.param != nullptr && k.param->get() != 0);
                auto cb = juce::Rectangle<float> (13, 13).withCentre ({ r.getRight() - 14.0f, r.getCentreY() });
                g.drawRect (cb, 1.0f);
                if (on) g.fillRect (cb.reduced (2.5f));
                text (g, k.title, r.reduced (6.0f, 4.0f).withTrimmedRight (16.0f), juce::Justification::centredLeft, 10.5f, false, col);
                break;
            }

            case SoftKey::Kind::Value:
            {
                g.setColour (col);
                if (sel) g.fillRect (r);
                else g.drawRect (r, 1.0f);
                auto inner = r.reduced (5.0f, 2.0f);
                auto top = inner.removeFromTop (15.0f);
                juce::String t = k.title;
                if (k.arrows) t = juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80\xe2\x96\xb6 ")) + t;
                text (g, t, top, juce::Justification::centredLeft, 10.0f, false, textCol);
                juce::String v = k.param != nullptr ? k.param->text() : k.subtitle;
                if (sel && p.entry.isNotEmpty()) v = p.entry + "_";
                text (g, v, inner, juce::Justification::centredRight, 11.0f, true, textCol);
                break;
            }
            default: break;
        }
    }
}

void PageManager::paint (juce::Graphics& g, juce::Rectangle<float> lcd)
{
    g.setColour (colours::oledBg);
    g.fillRect (lcd);
    if (! powered) return;

    if (booting || currentPage == nullptr)
    {
        oled::text (g, "AKAI", lcd.withHeight (60).translated (0, 60), juce::Justification::centred, 12.0f, true);
        juce::GlyphArrangement ga;
        ga.addLineOfText (fonts::oledTitle().withHeight (40.0f), "S6000", 0, 0);
        juce::Path pth; ga.createPath (pth);
        const auto pb = pth.getBounds();
        pth.applyTransform (juce::AffineTransform::translation (lcd.getCentreX() - pb.getCentreX(), lcd.getY() + 135.0f - pb.getCentreY()));
        g.setColour (colours::oledText);
        g.strokePath (pth, juce::PathStrokeType (1.4f));
        oled::text (g, "MIDI STEREO DIGITAL SAMPLER", lcd.withY (lcd.getY() + 165).withHeight (16), juce::Justification::centred, 10.0f);
        oled::text (g, "OPERATING SYSTEM VERSION " + juce::String (osVersion), lcd.withY (lcd.getY() + 190).withHeight (16), juce::Justification::centred, 11.0f, true);
        oled::text (g, "BOOTING...", lcd.withY (lcd.getY() + 225).withHeight (16), juce::Justification::centred, 10.0f, false, colours::oledDim);
        oled::text (g, juce::String (juce::CharPointer_UTF8 ("\xc2\xa9")) + " dominkHop  -  github.com/jomd7777/S6000", lcd.withY (lcd.getBottom() - 26).withHeight (16), juce::Justification::centred, 10.0f, false, colours::oledDim);
        return;
    }

    Page& p = *currentPage;
    const bool dimBase = ! popups.empty();
    {
        juce::Graphics::ScopedSaveState ss (g);
        if (p.listLayout())
            p.paintCentre (g, geo::listArea (lcd));
        else
        {
            const auto c = geo::centre (lcd);
            p.paintCentre (g, c);
        }
        p.paintFull (g, lcd);
    }
    paintKeys (g, lcd, p, dimBase, popups.empty() ? nullptr : &popups.front()->keys);

    // mode title
    const auto t = p.title();
    if (t.isNotEmpty())
    {
        auto tr = juce::Rectangle<float> (lcd.getX() + geo::boxW + 10.0f, geo::rowY (lcd, 7) - 20.0f, lcd.getWidth() - 2.0f * geo::boxW - 20.0f, 40.0f);
        g.setColour (dimBase ? colours::oledDim : colours::oledText);
        g.setFont (t.containsChar ('\n') ? fonts::oledTitle().withHeight (17.0f) : fonts::oledTitle());
        g.drawFittedText (t, tr.toNearestInt(), juce::Justification::centred, 2);
    }

    for (auto& pp : popups)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (lcd);
        const auto b = pp->popupBounds (lcd);
        g.setColour (colours::oledBg);
        g.fillRect (b);
        g.setColour (colours::oledText);
        g.drawRect (b, 1.5f);
        g.drawRect (b.reduced (3.0f), 1.0f);
        auto inner = b.reduced (6.0f);
        if (auto* pop = dynamic_cast<Popup*> (pp.get()))
        {
            auto head = inner.removeFromTop (22.0f);
            g.fillRect (head);
            oled::text (g, pop->heading, head.reduced (4, 0), juce::Justification::centred, 11.0f, true, colours::oledBg);
            inner.removeFromTop (4.0f);
        }
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (inner.toNearestInt());
            pp->paintCentre (g, inner);
        }
        paintKeys (g, lcd, *pp, false);
    }
}

//==============================================================================
namespace oled
{
    void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, float size, bool bold, juce::Colour c)
    {
        g.setFont (fonts::oled (size, bold));
        if (c == colours::oledText)
        {
            // soft OLED glow
            g.setColour (c.withAlpha (0.18f));
            g.drawText (s, r.translated (0.6f, 0.0f), j, true);
            g.drawText (s, r.translated (-0.6f, 0.0f), j, true);
        }
        g.setColour (c);
        g.drawText (s, r, j, true);
    }

    void box (juce::Graphics& g, juce::Rectangle<float> r, bool filled, float t)
    {
        g.setColour (colours::oledText);
        if (filled) g.fillRect (r); else g.drawRect (r, t);
    }

    void memoryBox (juce::Graphics& g, juce::Rectangle<float> r, Ctx& ctx)
    {
        r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 170.0f), 96.0f);
        box (g, r);
        auto head = r.removeFromTop (18.0f);
        g.setColour (colours::oledText);
        g.drawLine (head.getX(), head.getBottom(), head.getRight(), head.getBottom());
        text (g, "MEMORY", head, juce::Justification::centred, 10.0f, true);
        for (int i = 0; i < 4; ++i)
            text (g, ctx.memoryLine (i), r.removeFromTop (19.0f).reduced (8.0f, 0), juce::Justification::centredLeft, 10.5f);
    }

    void keyboard (juce::Graphics& g, juce::Rectangle<float> r, int lo, int hi, int markLo, int markHi)
    {
        const int first = (lo / 12) * 12, last = juce::jmin (127, ((hi / 12) + 1) * 12 - 1);
        int whites = 0;
        for (int n = first; n <= last; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
        const float w = r.getWidth() / (float) juce::jmax (1, whites);
        float x = r.getX();
        std::vector<std::pair<float, int>> blacks;
        for (int n = first; n <= last; ++n)
        {
            if (juce::MidiMessage::isMidiNoteBlack (n)) { blacks.push_back ({ x, n }); continue; }
            juce::Rectangle<float> k (x, r.getY(), w, r.getHeight());
            const bool marked = n >= markLo && n <= markHi;
            g.setColour (marked ? colours::oledText : colours::oledBg);
            g.fillRect (k.reduced (0.5f));
            g.setColour (colours::oledText);
            g.drawRect (k, 0.8f);
            x += w;
        }
        for (auto& b : blacks)
        {
            juce::Rectangle<float> k (b.first - w * 0.32f, r.getY(), w * 0.64f, r.getHeight() * 0.6f);
            const bool marked = b.second >= markLo && b.second <= markHi;
            g.setColour (marked ? colours::oledDim.brighter (0.4f) : colours::oledText);
            g.fillRect (k);
        }
    }

    void waveform (juce::Graphics& g, juce::Rectangle<float> r, const Sample& s, juce::int64 from, juce::int64 to)
    {
        g.setColour (colours::oledGrid);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        const int n = s.numFrames();
        if (n < 2) return;
        from = juce::jlimit<juce::int64> (0, n - 1, from);
        to = juce::jlimit<juce::int64> (from + 1, n, to);
        const int cols = juce::jmax (1, (int) r.getWidth());
        const double per = (double) (to - from) / cols;
        g.setColour (colours::oledText);
        const int chans = s.numChannels();
        for (int x = 0; x < cols; ++x)
        {
            const juce::int64 a = from + (juce::int64) (x * per);
            const juce::int64 b = juce::jmin<juce::int64> (to, a + juce::jmax<juce::int64> (1, (juce::int64) per));
            float mn = 1.0f, mx = -1.0f;
            const juce::int64 stride = juce::jmax<juce::int64> (1, (b - a) / 64);   // decimate long sections (CPU)
            for (int c = 0; c < chans; ++c)
            {
                const float* d = s.buffer.getReadPointer (c);
                for (juce::int64 i = a; i < b; i += stride) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
            }
            const float y1 = r.getCentreY() - mx * r.getHeight() * 0.5f, y2 = r.getCentreY() - mn * r.getHeight() * 0.5f;
            g.drawVerticalLine ((int) (r.getX() + (float) x), juce::jmin (y1, y2), juce::jmax (y1, y2) + 1.0f);
        }
    }

    void envelopeAdsr (juce::Graphics& g, juce::Rectangle<float> r, int a, int d, int s, int rel)
    {
        const float w = r.getWidth() / 4.0f;
        const float x0 = r.getX(), yb = r.getBottom(), yt = r.getY();
        const float xa = x0 + w * (float) a / 100.0f;
        const float xd = xa + w * (float) d / 100.0f;
        const float ys = yb - r.getHeight() * (float) s / 100.0f;
        const float xs = x0 + w * 3.0f;
        const float xr = xs + w * (float) rel / 100.0f;
        juce::Path p;
        p.startNewSubPath (x0, yb);
        p.lineTo (xa, yt);
        p.quadraticTo (xa + (xd - xa) * 0.25f, ys, xd, ys);
        p.lineTo (xs, ys);
        p.quadraticTo (xs + (xr - xs) * 0.25f, yb, xr, yb);
        g.setColour (colours::oledText);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        g.setColour (colours::oledGrid);
        g.drawRect (r, 0.8f);
    }

    void icon (juce::Graphics& g, int type, juce::Rectangle<float> r, juce::Colour c)
    {
        g.setColour (c);
        r = r.withSizeKeepingCentre (17.0f, 13.0f);
        switch (type)
        {
            case 1: // folder
            {
                juce::Path p;
                p.startNewSubPath (r.getX(), r.getBottom());
                p.lineTo (r.getX(), r.getY() + 1);
                p.lineTo (r.getX() + 5, r.getY() + 1);
                p.lineTo (r.getX() + 6.5f, r.getY() + 2.5f);
                p.lineTo (r.getRight(), r.getY() + 2.5f);
                p.lineTo (r.getRight(), r.getBottom());
                p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (1.0f));
                break;
            }
            case 2: case 3: case 4:
            {
                g.drawRect (r, 1.0f);
                g.setFont (fonts::oled (10.0f, true));
                g.drawText (type == 2 ? "M" : type == 3 ? "P" : "S", r, juce::Justification::centred);
                break;
            }
            case 5: case 6:
            {
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
                g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre ({ r.getRight() - 3, r.getBottom() - 3 }));
                if (type == 6) g.drawEllipse (juce::Rectangle<float> (6, 6).withCentre (r.getCentre()), 1.0f);
                break;
            }
            default: break;
        }
    }

    void meter (juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour (colours::oledText);
        g.drawRect (r, 1.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.fillRect (r.reduced (2.0f).withWidth ((r.getWidth() - 4.0f) * frac));
    }
}

//==============================================================================
namespace kb
{
    SoftKey button (const juce::String& t, std::function<void()> f, bool enabled)
    {
        SoftKey k; k.kind = SoftKey::Kind::Button; k.title = t; k.onPress = std::move (f); k.enabled = enabled;
        return k;
    }

    SoftKey value (const juce::String& t, IntRef ref, int lo, int hi, Fmt fmt, std::function<void()> changed)
    {
        SoftKey k; k.kind = SoftKey::Kind::Value; k.title = t;
        auto p = std::make_shared<Param>();
        p->get = [ref] { if (auto* v = ref()) return *v; return 0; };
        p->set = [ref, changed] (int v) { if (auto* x = ref()) { *x = v; if (changed) changed(); } };
        p->min = lo; p->max = hi; p->format = std::move (fmt);
        p->bigStep = juce::jmax (1, (hi - lo) / 10);
        k.param = p;
        return k;
    }

    SoftKey valueFn (const juce::String& t, std::function<int()> get, std::function<void (int)> set, int lo, int hi, Fmt fmt)
    {
        SoftKey k; k.kind = SoftKey::Kind::Value; k.title = t;
        auto p = std::make_shared<Param>();
        p->get = std::move (get); p->set = std::move (set); p->min = lo; p->max = hi; p->format = std::move (fmt);
        p->bigStep = juce::jmax (1, (hi - lo) / 10);
        k.param = p;
        return k;
    }

    SoftKey check (const juce::String& t, IntRef ref, std::function<void()> changed)
    {
        SoftKey k = value (t, ref, 0, 1, onOff(), changed);
        k.kind = SoftKey::Kind::Check;
        return k;
    }

    SoftKey label (const juce::String& t, const juce::String& sub)
    {
        SoftKey k; k.kind = SoftKey::Kind::Label; k.title = t; k.subtitle = sub;
        return k;
    }

    SoftKey header (const juce::String& t, ParamPtr p)
    {
        SoftKey k; k.kind = SoftKey::Kind::Header; k.title = t; k.param = std::move (p);
        return k;
    }

    SoftKey row (const juce::String& t, const juce::String& sub, int icon, bool highlight, std::function<void()> f)
    {
        SoftKey k; k.kind = SoftKey::Kind::Row; k.title = t; k.subtitle = sub; k.icon = icon; k.highlight = highlight; k.onPress = std::move (f);
        return k;
    }

    Fmt num()     { return [] (int v) { return juce::String (v); }; }
    Fmt signed3() { return [] (int v) { return names::signedValue (v, 3); }; }
    Fmt signed2() { return [] (int v) { return names::signedValue (v, 2); }; }
    Fmt list (const juce::StringArray& items)
    {
        return [items] (int v) { return juce::isPositiveAndBelow (v, items.size()) ? items[v] : juce::String (v); };
    }
    Fmt onOff()   { return [] (int v) { return v ? juce::String ("ON") : juce::String ("OFF"); }; }
    Fmt pan()     { return [] (int v) { return names::panValue (v); }; }
    Fmt db()      { return [] (int v) { return names::levelDb (v); }; }
    Fmt note (Ctx& c) { return [&c] (int v) { return c.note (v); }; }
    Fmt suffix (const juce::String& s) { return [s] (int v) { return juce::String (v) + s; }; }
}

} // namespace s6k::ui
