/*
  ==============================================================================
   S6000 Emulation - colours, fonts and the panel layout (base size 1200 x 640)
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

namespace s6k::ui
{
constexpr int baseWidth  = 1200;
constexpr int baseHeight = 640;

namespace colours
{
    const juce::Colour rack        { 0xffcfc8b6 };
    const juce::Colour chassis     { 0xffdcd5c3 };
    const juce::Colour chassisLite { 0xffe8e2d2 };
    const juce::Colour chassisDark { 0xffaaa290 };
    const juce::Colour panel       { 0xffe2dccb };
    const juce::Colour panelEdge   { 0xffb7af9b };
    const juce::Colour label       { 0xff4b4840 };
    const juce::Colour bezel       { 0xff17191b };
    const juce::Colour bezelEdge   { 0xff34383c };
    const juce::Colour bezelText   { 0xff9aa2a8 };
    const juce::Colour keyFace     { 0xfff1ede0 };
    const juce::Colour keyEdge     { 0xffb4ad99 };
    const juce::Colour keyShadow   { 0xff8f8876 };
    const juce::Colour blueKey     { 0xff2f6cae };
    const juce::Colour blueKeyLite { 0xff6aa0d6 };
    const juce::Colour keypadBg    { 0xffbab6ac };
    const juce::Colour orangeKey   { 0xfff19a36 };
    const juce::Colour modeStrip   { 0xff2a2a2c };
    const juce::Colour modeKey     { 0xffcfcac0 };
    const juce::Colour ledGreen    { 0xff86ff70 };
    const juce::Colour ledRed      { 0xffff3b2f };
    const juce::Colour akaiRed     { 0xffb3262b };
    const juce::Colour jack        { 0xff111111 };

    // OLED
    const juce::Colour oledBg      { 0xff010405 };
    const juce::Colour oledText    { 0xff5cefe2 };
    const juce::Colour oledDim     { 0xff237a74 };
    const juce::Colour oledGrid    { 0xff0d2a29 };
    const juce::Colour oledWarn    { 0xffffc857 };
}

namespace fonts
{
    inline juce::String monoName()
    {
       #if JUCE_MAC
        return "Menlo";
       #elif JUCE_WINDOWS
        return "Consolas";
       #else
        return juce::Font::getDefaultMonospacedFontName();
       #endif
    }

    /** OLED text scale: the big display uses everything 25 % larger (11 -> 13.75 pt) */
    constexpr float oledTextScale = 1.25f;

    inline juce::Font oled (float size = 11.0f, bool bold = false)
    {
        return juce::Font (juce::FontOptions (monoName(), size * oledTextScale, bold ? juce::Font::bold : juce::Font::plain));
    }
    /** big mode title at the bottom of the screen (e.g. "EDIT PROGRAM") */
    inline juce::Font oledTitle() { return juce::Font (juce::FontOptions (monoName(), 22.0f, juce::Font::bold)); }

    inline juce::Font panel (float size = 9.0f, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
namespace layout
{
    // LCD (inside the bezel) - spans from the left soft keys to the right soft keys
    const juce::Rectangle<int> bezel  { 104, 70, 592, 368 };
    const juce::Rectangle<int> lcd    { 111, 96, 578, 330 };

    // soft keys
    constexpr int softKeyW = 36, softKeyH = 21;
    constexpr int leftKeysX = 84, rightKeysX = 716;
    constexpr float firstKeyY = 125.0f, keyPitch = 38.0f;
    inline juce::Point<int> softKeyCentre (int index)   // 0..15 = F1..F16
    {
        const int row = index % 8;
        return { index < 8 ? leftKeysX : rightKeysX, (int) (firstKeyY + keyPitch * (float) row) };
    }

    // data wheel + cursor + keypad
    const juce::Point<int> dataWheel { 852, 112 };
    constexpr int dataWheelRadius = 60;
    const juce::Point<int> cursorLeft { 940, 182 }, cursorRight { 1010, 182 };
    const juce::Rectangle<int> keypadArea { 786, 206, 192, 150 };
    inline juce::Point<int> keypadKey (int row, int col) { return { 818 + col * 63, 230 + row * 36 }; }
    inline juce::Point<int> sideKey (int row) { return { 1010, 230 + row * 36 }; }    // WINDOW UNDO EXIT ENT/PLAY
    inline juce::Point<int> userKey (int i) { return { 800 + i * 53, 392 }; }          // USER1..3 MARK JUMP

    // mode strip
    const juce::Rectangle<int> modeStrip { 136, 488, 530, 66 };
    inline juce::Point<int> modeKey (int i) { return { 194 + i * 59, 534 }; }

    // right column
    const juce::Point<int> mainVolume { 1120, 128 };
    const juce::Point<int> phonesLevel { 1120, 238 };
    const juce::Point<int> phonesJack { 1120, 322 };
    const juce::Point<int> leftInJack { 1120, 414 };
    const juce::Point<int> rightInJack { 1120, 500 };

    // bottom strip
    const juce::Rectangle<int> bottomStrip { 0, 596, 1200, 44 };
    const juce::Rectangle<int> powerButton { 34, 603, 74, 30 };
}
} // namespace s6k::ui
