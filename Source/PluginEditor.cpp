/*
  ==============================================================================
   AKAI S6000 style sampler  -  editor (front panel)
     * ZOOM 50 % .. 200 %, the last zoom is remembered for the next time
     * F1..F16 soft keys, DATA wheel, keypad, WINDOW / UNDO / EXIT / ENT-PLAY,
       USER KEYS, MARK / JUMP, mode keys with LEDs, MAIN VOLUME, PHONES LEVEL
     * POWER switch (lower left) with boot screen
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "PluginEditor.h"
#include "UI/Pages/Common.h"

using namespace s6k;
using namespace s6k::ui;

static const float zoomSteps[] = { 0.5f, 0.6f, 0.7f, 0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.4f, 1.5f, 1.75f, 2.0f };

//==============================================================================
void OledDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    if (sleeping)
    {
        g.fillAll (colours::oledBg);
        return;
    }
    if (proc.prefs.screenInvert)
    {
        // render, then invert (only when the INVERTED preference is used)
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) (r.getWidth() * scale)), juce::jmax (1, (int) (r.getHeight() * scale)), true);
        {
            juce::Graphics ig (img);
            ig.addTransform (juce::AffineTransform::scale (scale));
            pages.paint (ig, r);
        }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                auto c = bd.getPixelColour (x, y);
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (1.0f - c.getFloatRed() * 0.9f, 1.0f - c.getFloatGreen() * 0.9f, 1.0f - c.getFloatBlue() * 0.9f, 1.0f));
            }
        g.drawImage (img, r);
        return;
    }
    pages.paint (g, r);
}

void OledDisplay::mouseDown (const juce::MouseEvent& e)
{
    // touching a soft-key box on the screen works like pressing the key next to it
    const auto r = getLocalBounds().toFloat();
    const float x = e.position.x, y = e.position.y;
    const int row = juce::jlimit (0, 7, (int) std::round ((y - geo::rowY (r, 0)) / layout::keyPitch));
    if (x < geo::boxW + 8.0f) pages.press (row);
    else if (x > r.getWidth() - geo::boxW - 8.0f) pages.press (8 + row);
}

void OledDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAccum += (w.isReversed ? -w.deltaY : w.deltaY) * 8.0f;
    const int steps = (int) wheelAccum;
    if (steps != 0) { wheelAccum -= (float) steps; pages.wheel (steps); }
}

//==============================================================================
S6000Panel::S6000Panel (S6000AudioProcessor& p, PageManager& pm)
    : display (pm, p), proc (p)
{
    addAndMakeVisible (background);
    addAndMakeVisible (display);

    for (int i = 0; i < 16; ++i)
    {
        softKeys[(size_t) i] = std::make_unique<PanelButton> ("F" + juce::String (i + 1), PanelButton::Style::Soft);
        addAndMakeVisible (*softKeys[(size_t) i]);
    }
    for (int i = 0; i < 8; ++i)
    {
        modeKeys[(size_t) i] = std::make_unique<PanelButton> ("MODE" + juce::String (i), PanelButton::Style::Mode);
        addAndMakeVisible (*modeKeys[(size_t) i]);
    }
    for (int i = 0; i < 12; ++i)
    {
        keypad[(size_t) i] = std::make_unique<PanelButton> ("KEY" + juce::String (i), PanelButton::Style::Blue);
        addAndMakeVisible (*keypad[(size_t) i]);
    }
    auto mk = [this] (std::unique_ptr<PanelButton>& b, const char* n, PanelButton::Style s) { b = std::make_unique<PanelButton> (n, s); addAndMakeVisible (*b); };
    mk (cursorL, "CURSOR L", PanelButton::Style::Small);
    mk (cursorR, "CURSOR R", PanelButton::Style::Small);
    mk (windowKey, "WINDOW", PanelButton::Style::Orange);
    mk (undoKey, "UNDO", PanelButton::Style::Small);
    mk (exitKey, "EXIT", PanelButton::Style::Small);
    mk (enterKey, "ENT/PLAY", PanelButton::Style::Small);
    mk (markKey, "MARK", PanelButton::Style::Small);
    mk (jumpKey, "JUMP", PanelButton::Style::Small);
    for (int i = 0; i < 3; ++i) mk (userKeys[(size_t) i], "USER", PanelButton::Style::Small);

    addAndMakeVisible (dataWheel);

    for (auto* s : { &mainVolume, &phonesLevel })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setLookAndFeel (&knobLnf);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setPopupDisplayEnabled (true, true, this);
        s->setTextValueSuffix (" dB");
        addAndMakeVisible (*s);
    }
    volAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "mainVolume", mainVolume);
    phonesAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "phonesLevel", phonesLevel);
    mainVolume.setTooltip ("MAIN VOLUME (outputs 1/2)");
    phonesLevel.setTooltip ("PHONES LEVEL (audition / preview level)");

    addAndMakeVisible (power);
    power.setTooltip ("POWER");

    credit.setText (juce::String (juce::CharPointer_UTF8 ("S6000  \xe2\x80\xa2  OS VER. 2.14  \xe2\x80\xa2  creador \xc2\xa9 dominikHop")), juce::dontSendNotification);
    credit.setFont (fonts::panel (11.0f, true));
    credit.setColour (juce::Label::textColourId, colours::label);
    credit.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (credit);
    instagram.setFont (fonts::panel (11.0f, true), false, juce::Justification::centredLeft);
    instagram.setColour (juce::HyperlinkButton::textColourId, colours::akaiRed);
    addAndMakeVisible (instagram);

    for (auto* b : { &zoomOut, &zoomIn, &zoomLabel })
    {
        b->setColour (juce::TextButton::buttonColourId, colours::chassisDark.darker (0.1f));
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    zoomLabel.setTooltip ("ZOOM (the last zoom is remembered)");

    setSize (baseWidth, baseHeight);   // last: resized() needs every control to exist
}

S6000Panel::~S6000Panel()
{
    mainVolume.setLookAndFeel (nullptr);
    phonesLevel.setLookAndFeel (nullptr);
}

void S6000Panel::resized()
{
    using namespace layout;
    if (softKeys[15] == nullptr || jumpKey == nullptr) return;   // still being constructed
    background.setBounds (getLocalBounds());
    display.setBounds (lcd);

    auto place = [] (juce::Component& c, juce::Point<int> centre, int w, int h) { c.setBounds (juce::Rectangle<int> (w, h).withCentre (centre)); };
    for (int i = 0; i < 16; ++i) place (*softKeys[(size_t) i], softKeyCentre (i), softKeyW, softKeyH);
    for (int i = 0; i < 8; ++i) place (*modeKeys[(size_t) i], modeKey (i), 36, 20);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) place (*keypad[(size_t) (r * 3 + c)], keypadKey (r, c), 38, 17);
    place (*cursorL, cursorLeft, 34, 18);
    place (*cursorR, cursorRight, 34, 18);
    place (*windowKey, sideKey (0), 34, 17);
    place (*undoKey, sideKey (1), 34, 17);
    place (*exitKey, sideKey (2), 34, 17);
    place (*enterKey, sideKey (3), 34, 17);
    for (int i = 0; i < 3; ++i) place (*userKeys[(size_t) i], userKey (i), 34, 18);
    place (*markKey, userKey (3), 34, 18);
    place (*jumpKey, userKey (4), 34, 18);
    place (dataWheel, layout::dataWheel, dataWheelRadius * 2 + 10, dataWheelRadius * 2 + 10);
    place (mainVolume, layout::mainVolume, 84, 84);
    place (phonesLevel, layout::phonesLevel, 56, 56);
    power.setBounds (powerButton);
    credit.setBounds (180, 604, 470, 28);
    instagram.setBounds (654, 604, 260, 28);
    zoomOut.setBounds (1040, 607, 26, 22);
    zoomLabel.setBounds (1068, 607, 58, 22);
    zoomIn.setBounds (1128, 607, 26, 22);
}

//==============================================================================
S6000AudioProcessorEditor::S6000AudioProcessorEditor (S6000AudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ctx (p), pages (ctx), panel (p, pages)
{
    registerAllPages (pages, ctx);
    addAndMakeVisible (panel);
    wireControls();

    pages.onRepaint = [this] { panel.display.repaint(); };
    pages.onPageChanged = [this] (PageId id) { proc.uiState.setProperty ("page", (int) id, nullptr); updateLeds(); };

    proc.disk.onJobFinished = [this] { ctx.clampSelections(); pages.refresh(); };
    proc.disk.onError = [this] (const juce::String& e) { ctx.error (e); };
    proc.onSessionRestored = [this] { ctx.clampSelections(); pages.refresh(); };

    // ZOOM: restore the last zoom (global setting shared by every instance)
    zoom = proc.getZoom();
    constrainer.setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    constrainer.setSizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize ((int) std::round (baseWidth * zoom), (int) std::round (baseHeight * zoom));

    setWantsKeyboardFocus (true);

    if (proc.powerOn.load()) startBoot();
    else pages.setPowered (false);

    startTimerHz (20);
}

S6000AudioProcessorEditor::~S6000AudioProcessorEditor()
{
    stopTimer();
    proc.disk.onJobFinished = nullptr;
    proc.disk.onError = nullptr;
    proc.onSessionRestored = nullptr;
    proc.setZoom (zoom);
    proc.settings().saveIfNeeded();
    proc.engine.settings.editProgram = -1;
    proc.engine.settings.editSample = -1;
}

void S6000AudioProcessorEditor::wireControls()
{
    for (int i = 0; i < 16; ++i)
        panel.softKeys[(size_t) i]->onClick = [this, i] { userActivity(); pages.press (i); };

    for (int i = 0; i < 8; ++i)
        panel.modeKeys[(size_t) i]->onClick = [this, i] { userActivity(); if (proc.powerOn.load() && bootTicks <= 0) pages.showMode ((Mode) i); };

    static const juce::juce_wchar keyChars[12] = { '7', '8', '9', '4', '5', '6', '1', '2', '3', '-', '0', '+' };
    for (int i = 0; i < 12; ++i)
        panel.keypad[(size_t) i]->onClick = [this, i] { userActivity(); pages.keypad (keyChars[i]); };

    panel.cursorL->onClick = [this] { userActivity(); pages.cursor (-1); };
    panel.cursorR->onClick = [this] { userActivity(); pages.cursor (1); };
    panel.windowKey->onClick = [this] { userActivity(); pages.window(); };
    panel.undoKey->onClick = [this] { userActivity(); pages.undo(); };
    panel.exitKey->onClick = [this] { userActivity(); pages.exit(); };
    panel.enterKey->onPressChange = [this] (bool down) { userActivity(); pages.enterPlay (down); };
    panel.markKey->onClick = [this] { userActivity(); pages.mark(); };
    panel.jumpKey->onClick = [this] { userActivity(); pages.jump(); };
    for (int i = 0; i < 3; ++i)
    {
        panel.userKeys[(size_t) i]->onClick = [this, i]
        {
            userActivity();
            pages.userKey (i, juce::ModifierKeys::currentModifiers.isShiftDown());
        };
        panel.userKeys[(size_t) i]->onRightClick = [this, i] { pages.userKey (i, true); };
    }
    panel.dataWheel.onTurn = [this] (int d) { userActivity(); pages.wheel (d); };

    panel.power.onClick = [this]
    {
        const bool on = panel.power.getToggleState();
        proc.powerOn = on;
        if (on) startBoot();
        else
        {
            proc.engine.allNotesOff();
            proc.engine.audition (nullptr);
            pages.setPowered (false);
            updateLeds();
        }
    };
    panel.power.setToggleState (proc.powerOn.load(), juce::dontSendNotification);

    panel.zoomOut.onClick = [this]
    {
        for (int i = (int) std::size (zoomSteps); --i >= 0;)
            if (zoomSteps[i] < zoom - 0.01f) { setZoom (zoomSteps[i]); return; }
    };
    panel.zoomIn.onClick = [this]
    {
        for (float z : zoomSteps)
            if (z > zoom + 0.01f) { setZoom (z); return; }
    };
    panel.zoomLabel.onClick = [this]
    {
        juce::PopupMenu m;
        for (int i = 0; i < (int) std::size (zoomSteps); ++i)
            m.addItem (i + 1, juce::String ((int) std::round (zoomSteps[i] * 100.0f)) + "%", true, std::abs (zoomSteps[i] - zoom) < 0.01f);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&panel.zoomLabel),
                         [safe = juce::Component::SafePointer<S6000AudioProcessorEditor> (this)] (int r)
                         {
                             if (safe != nullptr && r > 0) safe->setZoom (zoomSteps[r - 1]);
                         });
    };
}

void S6000AudioProcessorEditor::setZoom (float z)
{
    z = juce::jlimit (0.5f, 2.0f, z);
    setSize ((int) std::round (baseWidth * z), (int) std::round (baseHeight * z));
}

void S6000AudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::rack);
}

void S6000AudioProcessorEditor::resized()
{
    zoom = juce::jlimit (0.5f, 2.0f, (float) getWidth() / (float) baseWidth);
    panel.setTransform (juce::AffineTransform::scale (zoom));
    panel.setBounds (0, 0, baseWidth, baseHeight);
    panel.zoomLabel.setButtonText (juce::String ((int) std::round (zoom * 100.0f)) + "%");
    proc.setZoom (zoom);     // remembered for the next time the window opens
}

//==============================================================================
void S6000AudioProcessorEditor::startBoot()
{
    pages.setPowered (true);
    if (proc.prefs.showSplash)
    {
        bootTicks = 30;      // 1.5 s at 20 Hz
        pages.setBooting (true);
        pages.refresh();
    }
    else
        finishBoot();
}

void S6000AudioProcessorEditor::finishBoot()
{
    bootTicks = 0;
    pages.setBooting (false);
    const int saved = (int) proc.uiState.getProperty ("page", (int) PageId::Multi);
    const auto id = (PageId) saved;
    if (pages.get (id) != nullptr) pages.show (id);
    else pages.show (PageId::Multi);
    updateLeds();
}

void S6000AudioProcessorEditor::userActivity()
{
    idleTicks = 0;
    if (panel.display.sleeping) { panel.display.sleeping = false; panel.display.repaint(); }
}

void S6000AudioProcessorEditor::updateLeds()
{
    const bool on = proc.powerOn.load() && bootTicks <= 0;
    const auto m = pages.activeMode();
    for (int i = 0; i < 8; ++i)
    {
        bool lit = on && (int) m == i;
        if (lit && ledBlink > 0) lit = false;    // flashes on MIDI input
        panel.modeKeys[(size_t) i]->setLed (lit);
    }
}

void S6000AudioProcessorEditor::timerCallback()
{
    if (bootTicks > 0 && --bootTicks == 0) finishBoot();

    if (proc.engine.getAndClearMidiActivity()) ledBlink = 2;
    else if (ledBlink > 0) --ledBlink;
    updateLeds();

    // screen saver
    if (proc.prefs.screenSaver > 0 && ! panel.display.sleeping)
        if (++idleTicks > proc.prefs.screenSaver * 60 * 20) { panel.display.sleeping = true; panel.display.repaint(); }

    if (proc.powerOn.load() && bootTicks <= 0)
    {
        // LOAD / MOUNT progress window
        if (proc.disk.isBusy() && ! pages.hasPopup())
            pages.pushPopup (std::make_unique<ProgressPopup> (ctx));
        pages.tick();
    }
}

//==============================================================================
bool S6000AudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    userActivity();
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const auto ch = key.getTextCharacter();

    // ZOOM shortcuts
    if (mods.isCommandDown())
    {
        if (ch == '+' || ch == '=') { panel.zoomIn.triggerClick(); return true; }
        if (ch == '-') { panel.zoomOut.triggerClick(); return true; }
        if (ch == '0') { setZoom (1.0f); return true; }
        if (code == 'Z') { pages.undo(); return true; }
    }

    // function keys F1..F16  (SHIFT + F1..F4 = F13..F16)
    for (int i = 0; i < 12; ++i)
        if (code == juce::KeyPress::F1Key + i)
        {
            pages.press (mods.isShiftDown() && i < 4 ? 12 + i : i);
            return true;
        }
    if (code == juce::KeyPress::F13Key) { pages.press (12); return true; }
    if (code == juce::KeyPress::F14Key) { pages.press (13); return true; }
    if (code == juce::KeyPress::F15Key) { pages.press (14); return true; }
    if (code == juce::KeyPress::F16Key) { pages.press (15); return true; }

    if (code == juce::KeyPress::escapeKey) { pages.exit(); return true; }
    if (code == juce::KeyPress::returnKey)
    {
        if (! enterHeld) { enterHeld = true; pages.enterPlay (true); }
        return true;
    }
    if (code == juce::KeyPress::upKey)   { pages.wheel (mods.isShiftDown() ? 10 : 1); return true; }
    if (code == juce::KeyPress::downKey) { pages.wheel (mods.isShiftDown() ? -10 : -1); return true; }
    if (code == juce::KeyPress::pageUpKey)   { pages.wheel (10); return true; }
    if (code == juce::KeyPress::pageDownKey) { pages.wheel (-10); return true; }
    if (code == juce::KeyPress::leftKey)  { pages.cursor (-1); return true; }
    if (code == juce::KeyPress::rightKey) { pages.cursor (1); return true; }
    if (code == juce::KeyPress::tabKey)   { pages.window(); return true; }

    // ASCII KEYBOARD: naming first, then the numeric keypad
    if (code == juce::KeyPress::backspaceKey) { if (! pages.textInput (8)) pages.exit(); return true; }
    if (ch >= 32 && ch < 127)
    {
        if (pages.textInput (ch)) return true;
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+') { pages.keypad (ch); return true; }
    }
    return false;
}

bool S6000AudioProcessorEditor::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && enterHeld && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey))
    {
        enterHeld = false;
        pages.enterPlay (false);
        return true;
    }
    return false;
}

//==============================================================================
bool S6000AudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.isDirectory()) return true;
        disk::VNode n;
        n.name = file.getFileName().toStdString();
        disk::classifyByName (n);
        if (n.type != disk::VNode::Type::Other) return true;
    }
    return false;
}

void S6000AudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (! proc.powerOn.load()) return;
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.isDirectory()) proc.disk.loadHostFolder (file);
        else proc.disk.loadHostFile (file);
    }
    pages.show (PageId::Load);
    pages.pushPopup (std::make_unique<ProgressPopup> (ctx));
}
