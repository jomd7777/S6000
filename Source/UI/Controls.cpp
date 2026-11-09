/*
  ==============================================================================
   S6000 Emulation - painted panel controls
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "Controls.h"

namespace s6k::ui
{
//==============================================================================
void drawPanelLabel (juce::Graphics& g, const juce::String& t, juce::Point<int> c, float size, juce::Colour col)
{
    g.setColour (col);
    g.setFont (fonts::panel (size, true));
    g.drawFittedText (t, c.x - 60, c.y - 8, 120, 16, juce::Justification::centred, 2);
}

void drawJack (juce::Graphics& g, juce::Point<int> c, float r)
{
    const auto cf = c.toFloat();
    g.setColour (juce::Colour (0xff9a9488));
    g.fillEllipse (juce::Rectangle<float> (r * 2.5f, r * 2.5f).withCentre (cf.translated (0, 1.5f)));
    juce::ColourGradient ring (juce::Colour (0xff3a3a3a), cf.x, cf.y - r * 1.2f, juce::Colour (0xff050505), cf.x, cf.y + r * 1.2f, false);
    g.setGradientFill (ring);
    g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (cf));
    g.setColour (juce::Colour (0xffc8a24a));                 // gold ring as on the photo
    g.drawEllipse (juce::Rectangle<float> (r * 1.55f, r * 1.55f).withCentre (cf), 2.0f);
    g.setColour (colours::jack);
    g.fillEllipse (juce::Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (cf));
}

//==============================================================================
void PanelButton::buttonStateChanged()
{
    const bool d = isDown();
    if (d != lastDown)
    {
        lastDown = d;
        if (onPressChange) onPressChange (d);
    }
}

void PanelButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
    juce::Button::mouseDown (e);
}

void PanelButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const float corner = style == Style::Mode ? 2.5f : 3.0f;

    juce::Colour face = colours::keyFace;
    if (style == Style::Blue) face = colours::blueKey;
    else if (style == Style::Orange) face = colours::orangeKey;
    else if (style == Style::Mode) face = led ? colours::ledGreen : colours::modeKey;

    // shadow / skirt
    g.setColour (style == Style::Mode ? juce::Colours::black.withAlpha (0.6f) : colours::keyShadow);
    g.fillRoundedRectangle (r.translated (0, 2.0f), corner);

    auto faceR = down ? r.translated (0, 1.2f) : r;
    juce::ColourGradient grad (face.brighter (style == Style::Blue ? 0.35f : 0.18f), faceR.getX(), faceR.getY(),
                               face.darker (down ? 0.25f : 0.08f), faceR.getX(), faceR.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (faceR, corner);

    if (style == Style::Mode && led)
    {
        g.setColour (colours::ledGreen.withAlpha (0.35f));
        g.drawRoundedRectangle (faceR.expanded (1.5f), corner + 1.0f, 2.0f);
    }

    g.setColour (style == Style::Blue ? juce::Colour (0xff1d4a7c) : colours::keyEdge);
    g.drawRoundedRectangle (faceR, corner, 1.0f);

    // top highlight
    g.setColour (juce::Colours::white.withAlpha (style == Style::Blue ? 0.30f : 0.55f));
    g.drawLine (faceR.getX() + corner, faceR.getY() + 1.2f, faceR.getRight() - corner, faceR.getY() + 1.2f, 1.0f);

    if (over && ! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (faceR, corner);
    }
}

//==============================================================================
void DataWheel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (4.0f);
    const auto c = b.getCentre();
    const float r = b.getWidth() * 0.5f;

    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (b.translated (0, 4.0f));

    // knurled skirt
    juce::ColourGradient skirt (juce::Colour (0xff3a3a3a), c.x, c.y - r, juce::Colour (0xff0b0b0b), c.x, c.y + r, false);
    g.setGradientFill (skirt);
    g.fillEllipse (b);
    g.setColour (juce::Colour (0xff050505));
    for (int i = 0; i < 72; ++i)
    {
        const float a = angle + juce::MathConstants<float>::twoPi * (float) i / 72.0f;
        g.drawLine (c.x + std::cos (a) * r * 0.86f, c.y + std::sin (a) * r * 0.86f,
                    c.x + std::cos (a) * r * 0.99f, c.y + std::sin (a) * r * 0.99f, 1.2f);
    }

    // cap
    const float cr = r * 0.80f;
    juce::ColourGradient cap (juce::Colour (0xff4a4a4a), c.x - cr * 0.4f, c.y - cr * 0.7f, juce::Colour (0xff141414), c.x + cr * 0.4f, c.y + cr, true);
    g.setGradientFill (cap);
    g.fillEllipse (juce::Rectangle<float> (cr * 2, cr * 2).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (juce::Rectangle<float> (cr * 2, cr * 2).withCentre (c), 1.0f);

    // finger dimple
    const float da = angle - juce::MathConstants<float>::halfPi;
    const juce::Point<float> dp (c.x + std::cos (da) * cr * 0.58f, c.y + std::sin (da) * cr * 0.58f);
    juce::ColourGradient dim (juce::Colour (0xff0a0a0a), dp.x, dp.y - 6, juce::Colour (0xff3c3c3c), dp.x, dp.y + 8, false);
    g.setGradientFill (dim);
    g.fillEllipse (juce::Rectangle<float> (cr * 0.42f, cr * 0.42f).withCentre (dp));
}

void DataWheel::emit (float steps)
{
    accum += steps;
    const int whole = (int) accum;
    if (whole != 0)
    {
        accum -= (float) whole;
        const double now = juce::Time::getMillisecondCounterHiRes();
        const int accel = (now - lastTime < 25.0 && std::abs (whole) >= 1) ? 2 : 1;
        lastTime = now;
        if (onTurn) onTurn (whole * accel);
    }
    repaint();
}

void DataWheel::mouseDown (const juce::MouseEvent& e)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    lastPos = e.position;
    lastAngle = std::atan2 (e.position.y - c.y, e.position.x - c.x);
}

void DataWheel::mouseDrag (const juce::MouseEvent& e)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float dist = e.position.getDistanceFrom (c);
    if (dist < getWidth() * 0.18f || e.mods.isShiftDown())
    {
        // vertical drag near the centre
        const float dy = lastPos.y - e.position.y;
        angle += dy * 0.02f;
        emit (dy / 7.0f);
    }
    else
    {
        const float a = std::atan2 (e.position.y - c.y, e.position.x - c.x);
        float d = a - lastAngle;
        if (d > juce::MathConstants<float>::pi) d -= juce::MathConstants<float>::twoPi;
        if (d < -juce::MathConstants<float>::pi) d += juce::MathConstants<float>::twoPi;
        lastAngle = a;
        angle += d;
        emit (d / (juce::MathConstants<float>::twoPi / 30.0f));
    }
    lastPos = e.position;
}

void DataWheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    float dy = w.deltaY != 0 ? w.deltaY : w.deltaX;
    if (w.isReversed) dy = -dy;
    float steps = dy * 8.0f;
    if (std::abs (steps) < 1.0f && ! w.isSmooth) steps = dy > 0 ? 1.0f : -1.0f;
    angle += steps * 0.2f;
    emit (steps);
}

//==============================================================================
void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (size, size);
    const auto c = b.getCentre();
    const float r = size * 0.5f;

    // scale dots
    g.setColour (colours::label);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f - juce::MathConstants<float>::halfPi;
        g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre ({ c.x + std::cos (a) * (r + 3.5f), c.y + std::sin (a) * (r + 3.5f) }));
    }

    const auto kb = b.reduced (r * 0.12f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (kb.translated (0, 3));
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillEllipse (kb);
    const auto face = kb.reduced (kb.getWidth() * 0.08f);
    juce::ColourGradient grad (juce::Colour (0xfffbfaf6), face.getX(), face.getY(), juce::Colour (0xffcfcbc0), face.getRight(), face.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (face);
    g.setColour (juce::Colour (0xffa9a49a));
    g.drawEllipse (face, 1.0f);

    const float a = start + pos * (end - start) - juce::MathConstants<float>::halfPi;
    const auto fc = face.getCentre();
    const float fr = face.getWidth() * 0.5f;
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawLine (fc.x + std::cos (a) * fr * 0.15f, fc.y + std::sin (a) * fr * 0.15f,
                fc.x + std::cos (a) * fr * 0.88f, fc.y + std::sin (a) * fr * 0.88f, juce::jmax (2.0f, fr * 0.12f));
}

//==============================================================================
void PowerButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillRoundedRectangle (r, 5.0f);
    auto rocker = r.reduced (3.0f);
    auto lit = on ? rocker.removeFromLeft (rocker.getWidth() * 0.5f) : rocker.removeFromRight (rocker.getWidth() * 0.5f);
    juce::ColourGradient grad (juce::Colour (0xff4a4a4a), lit.getX(), lit.getY(), juce::Colour (0xff222222), lit.getX(), lit.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (lit, 3.0f);
    if (over || down) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (r, 5.0f); }

    // LED + legend
    const auto ledC = juce::Point<float> (r.getRight() + 10.0f, r.getCentreY());
    juce::ignoreUnused (ledC);
    g.setColour (on ? colours::ledRed : juce::Colour (0xff4a1612));
    g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ r.getX() + 10.0f, r.getCentreY() }));
    if (on)
    {
        g.setColour (colours::ledRed.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre ({ r.getX() + 10.0f, r.getCentreY() }));
    }
    g.setColour (juce::Colour (0xffd8d2c2));
    g.setFont (fonts::panel (9.0f, true));
    g.drawText (on ? "I  ON" : "O  OFF", r.withTrimmedLeft (18.0f), juce::Justification::centred);
}

//==============================================================================
void ChassisBackground::paint (juce::Graphics& g)
{
    using namespace layout;
    const auto full = getLocalBounds().toFloat();

    // rack + ears
    g.fillAll (colours::rack);
    for (float ex : { 0.0f, (float) baseWidth - 26.0f })
    {
        juce::Rectangle<float> ear (ex, 0, 26.0f, (float) bottomStrip.getY());
        g.setColour (colours::chassisDark.brighter (0.15f));
        g.fillRect (ear);
        g.setColour (colours::chassisDark.darker (0.2f));
        g.drawRect (ear, 1.0f);
        for (float hy : { 70.0f, 520.0f })
        {
            juce::Rectangle<float> slot (ex + 7.0f, hy, 12.0f, 26.0f);
            g.setColour (juce::Colour (0xff3a3833));
            g.fillRoundedRectangle (slot, 6.0f);
        }
    }

    // main body
    juce::Rectangle<float> body (26.0f, 8.0f, (float) baseWidth - 52.0f, (float) bottomStrip.getY() - 12.0f);
    juce::ColourGradient bodyGrad (colours::chassisLite, 0, body.getY(), colours::chassis.darker (0.05f), 0, body.getBottom(), false);
    g.setGradientFill (bodyGrad);
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (colours::chassisDark);
    g.drawRoundedRectangle (body, 6.0f, 1.2f);

    // removable panel
    juce::Rectangle<float> panel (42.0f, 28.0f, 1026.0f, 552.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (panel.translated (0, 3.0f), 28.0f);
    juce::ColourGradient pg (colours::panel.brighter (0.06f), 0, panel.getY(), colours::panel.darker (0.06f), 0, panel.getBottom(), false);
    g.setGradientFill (pg);
    g.fillRoundedRectangle (panel, 28.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (panel, 28.0f, 1.5f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (panel.reduced (2.0f), 26.0f, 1.0f);
    // side lugs
    for (float lx : { panel.getX() - 6.0f, panel.getRight() - 6.0f })
        for (float ly : { 60.0f, 340.0f })
        {
            g.setColour (colours::panel.darker (0.08f));
            g.fillRoundedRectangle (lx, ly, 12.0f, 46.0f, 5.0f);
            g.setColour (colours::panelEdge);
            g.drawRoundedRectangle (lx, ly, 12.0f, 46.0f, 5.0f, 1.0f);
        }

    // bezel
    const auto bz = bezel.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bz.translated (0, 4.0f), 14.0f);
    juce::ColourGradient bg (colours::bezelEdge, 0, bz.getY(), colours::bezel, 0, bz.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (bz, 14.0f);
    g.setColour (juce::Colour (0xff0a0b0c));
    g.drawRoundedRectangle (bz.reduced (1.0f), 14.0f, 2.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (lcd.toFloat().expanded (6.0f), 6.0f);

    // F1..F16 legends on the chassis, outside the soft keys
    g.setColour (colours::label);
    g.setFont (fonts::panel (8.5f, true));
    for (int i = 0; i < 16; ++i)
    {
        const auto kc = softKeyCentre (i);
        const int lx = i < 8 ? kc.x - softKeyW / 2 - 24 : kc.x + softKeyW / 2 + 2;
        g.drawText ("F" + juce::String (i + 1), lx, kc.y - 7, 22, 14, juce::Justification::centred);
    }

    // model name in the centre of the chassis, above the display
    {
        const float cx = (float) baseWidth * 0.5f, cy = 50.0f;
        g.setColour (colours::label);
        g.setFont (fonts::panel (11.0f, true));
        g.drawText ("MIDI STEREO DIGITAL SAMPLER", (int) cx - 250, (int) cy - 9, 230, 18, juce::Justification::centredRight);
        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)), "S6000", 0.0f, 0.0f);
        juce::Path p;
        ga.createPath (p);
        const auto pb = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (cx - pb.getX(), cy - pb.getCentreY()));
        g.setColour (juce::Colour (0xff3a3833));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    // DATA / CONTRAST / CURSOR
    drawPanelLabel (g, "DATA", { dataWheel.x - 74, dataWheel.y - 44 }, 9.0f, colours::akaiRed.darker (0.2f));
    drawPanelLabel (g, "CONTRAST", { 985, 74 }, 8.0f);
    g.setColour (juce::Colour (0xff2b2a27));
    g.fillRoundedRectangle (978.0f, 40.0f, 14.0f, 8.0f, 3.0f);
    drawPanelLabel (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x80  CURSOR  \xe2\x96\xb6")), { (cursorLeft.x + cursorRight.x) / 2, cursorLeft.y - 20 }, 9.0f);

    // keypad
    g.setColour (colours::keypadBg);
    g.fillRoundedRectangle (keypadArea.toFloat(), 4.0f);
    g.setColour (colours::keypadBg.darker (0.15f));
    g.drawRoundedRectangle (keypadArea.toFloat(), 4.0f, 1.0f);
    static const char* keyLabels[4][3] = { { "7", "8", "9" }, { "4", "5", "6" }, { "1", "2", "3" }, { "-", "0", "+" } };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
        {
            const auto p = keypadKey (r, c);
            drawPanelLabel (g, keyLabels[r][c], { p.x, p.y - 15 }, 8.5f);
        }
    static const char* sideLabels[] = { "WINDOW", "UNDO", "EXIT", "ENT/PLAY" };
    for (int r = 0; r < 4; ++r)
    {
        const auto p = sideKey (r);
        drawPanelLabel (g, sideLabels[r], { p.x, p.y - 15 }, 8.0f);
    }

    // user keys
    g.setColour (juce::Colour (0xff6e6b64));
    g.fillRect (786.0f, 362.0f, 140.0f, 9.0f);
    g.setColour (juce::Colour (0xffe8e2d2));
    g.setFont (fonts::panel (7.5f, true));
    g.drawText ("USER KEYS", 786, 362, 140, 9, juce::Justification::centred);
    static const char* uk[] = { "1", "2", "3", "MARK", "JUMP" };
    for (int i = 0; i < 5; ++i)
    {
        const auto p = userKey (i);
        drawPanelLabel (g, uk[i], { p.x, p.y - 15 }, 8.0f);
    }

    // mode strip
    const auto ms = modeStrip.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (ms.translated (0, 2), 8.0f);
    g.setColour (colours::modeStrip);
    g.fillRoundedRectangle (ms, 8.0f);
    static const char* modes[] = { "MULTI", "FX", "SAMPLE", "PROGRAM", "RECORD", "UTILITIES", "SAVE", "LOAD" };
    for (int i = 0; i < 8; ++i)
    {
        const auto p = modeKey (i);
        drawPanelLabel (g, modes[i], { p.x, p.y - 21 }, 8.0f, juce::Colour (0xffe9e4d8));
    }
    {
        const int x1 = modeKey (2).x - 22, x2 = modeKey (3).x + 22, y = modeStrip.getY() + 11;
        g.setColour (juce::Colour (0xffe9e4d8));
        g.drawLine ((float) x1, (float) y, (float) x1 + 24.0f, (float) y, 1.0f);
        g.drawLine ((float) x2 - 24.0f, (float) y, (float) x2, (float) y, 1.0f);
        g.setFont (fonts::panel (8.0f, true));
        g.drawText ("EDIT", x1, y - 6, x2 - x1, 12, juce::Justification::centred);
    }

    // ASCII KEYBOARD
    drawPanelLabel (g, "ASCII\nKEYBOARD", { 92, 498 }, 8.0f);

    // AKAI professional
    g.setColour (colours::akaiRed);
    g.setFont (juce::Font (juce::FontOptions ("Times New Roman", 38.0f, juce::Font::bold)));
    g.drawText ("AKAI", 786, 500, 120, 44, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions ("Times New Roman", 19.0f, juce::Font::italic)));
    g.drawText ("professional", 896, 512, 140, 30, juce::Justification::centredLeft);

    // right column
    drawPanelLabel (g, "MAIN VOLUME", { mainVolume.x, mainVolume.y - 56 }, 9.0f);
    drawPanelLabel (g, "MIN", { mainVolume.x - 30, mainVolume.y + 44 }, 7.0f);
    drawPanelLabel (g, "MAX", { mainVolume.x + 30, mainVolume.y + 44 }, 7.0f);
    drawPanelLabel (g, "PHONES LEVEL", { phonesLevel.x, phonesLevel.y - 40 }, 9.0f);
    drawPanelLabel (g, "MIN", { phonesLevel.x - 24, phonesLevel.y + 30 }, 7.0f);
    drawPanelLabel (g, "MAX", { phonesLevel.x + 24, phonesLevel.y + 30 }, 7.0f);
    drawPanelLabel (g, "PHONES", { phonesJack.x, phonesJack.y - 30 }, 9.0f);
    drawPanelLabel (g, "L/MONO IN", { leftInJack.x, leftInJack.y - 30 }, 9.0f);
    drawPanelLabel (g, "RIGHT IN", { rightInJack.x, rightInJack.y - 30 }, 9.0f);
    drawJack (g, phonesJack, 12.0f);
    drawJack (g, leftInJack, 12.0f);
    drawJack (g, rightInJack, 12.0f);

    // bottom strip
    const auto bs = bottomStrip.toFloat();
    juce::ColourGradient sg (colours::chassisDark.brighter (0.25f), 0, bs.getY(), colours::chassisDark, 0, bs.getBottom(), false);
    g.setGradientFill (sg);
    g.fillRect (bs);
    g.setColour (colours::chassisDark.darker (0.3f));
    g.drawLine (0, bs.getY(), full.getRight(), bs.getY(), 1.0f);
    g.setColour (colours::label);
    g.setFont (fonts::panel (9.0f, true));
    g.drawText ("POWER", powerButton.getRight() + 8, powerButton.getY(), 60, powerButton.getHeight(), juce::Justification::centredLeft);
}

} // namespace s6k::ui
