/*
  ==============================================================================
   S6000 Emulation - physical panel controls (painted, no bitmaps)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include "Theme.h"

namespace s6k::ui
{
//==============================================================================
class PanelButton : public juce::Button
{
public:
    enum class Style { Soft, Blue, Orange, Mode, Small };

    PanelButton (const juce::String& name, Style s) : juce::Button (name), style (s)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void setLed (bool on) { if (led != on) { led = on; repaint(); } }
    bool isLedOn() const { return led; }

    std::function<void (bool down)> onPressChange;   // ENT/PLAY needs press + release
    std::function<void()> onRightClick;

    void paintButton (juce::Graphics& g, bool over, bool down) override;
    void buttonStateChanged() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    Style style;
    bool led = false, lastDown = false;
};

//==============================================================================
/** The big DATA wheel: drag (rotary or vertical) or use the mouse wheel. */
class DataWheel : public juce::Component
{
public:
    DataWheel() { setRepaintsOnMouseActivity (false); }
    std::function<void (int delta)> onTurn;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

private:
    float angle = 0.0f, accum = 0.0f, lastAngle = 0.0f;
    juce::Point<float> lastPos;
    double lastTime = 0;
    void emit (float steps);
};

//==============================================================================
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

//==============================================================================
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("POWER") { setClickingTogglesState (true); setToggleState (true, juce::dontSendNotification); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;
};

//==============================================================================
/** All static artwork: chassis, rack ears, bezel, labels, logos, jacks. */
class ChassisBackground : public juce::Component
{
public:
    ChassisBackground() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); setOpaque (true); }
    void paint (juce::Graphics& g) override;
};

void drawJack (juce::Graphics& g, juce::Point<int> c, float r);
void drawPanelLabel (juce::Graphics& g, const juce::String& t, juce::Point<int> centre, float size = 9.0f, juce::Colour c = colours::label);

} // namespace s6k::ui
