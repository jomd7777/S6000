/*
  ==============================================================================
   AKAI S6000 style sampler  -  editor (front panel)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Controls.h"
#include "UI/PageSystem.h"

//==============================================================================
/** The 416 x 312 OLED screen. */
class OledDisplay : public juce::Component
{
public:
    OledDisplay (s6k::ui::PageManager& pm, S6000AudioProcessor& p) : pages (pm), proc (p) { setOpaque (true); }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool sleeping = false;

private:
    s6k::ui::PageManager& pages;
    S6000AudioProcessor& proc;
    float wheelAccum = 0;
    bool pageHasMouse = false;
    double lastFrameMs = 0;
    bool wasAnimating = false;
    // smooth play line: repaint at ~30 fps only while a page animates (CPU friendly)
    juce::VBlankAttachment vblank { this, [this]
    {
        if (sleeping || ! isShowing()) return;
        const bool anim = pages.animating();
        if (! anim && ! wasAnimating) return;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (anim && now - lastFrameMs < 32.0) return;
        lastFrameMs = now;
        wasAnimating = anim;
        repaint();
    } };
};

//==============================================================================
/** Everything on the front panel, laid out at the base size (1200 x 640). */
class S6000Panel : public juce::Component
{
public:
    S6000Panel (S6000AudioProcessor& p, s6k::ui::PageManager& pm);
    ~S6000Panel() override;
    void resized() override;

    s6k::ui::ChassisBackground background;
    OledDisplay display;
    std::array<std::unique_ptr<s6k::ui::PanelButton>, 16> softKeys;
    std::array<std::unique_ptr<s6k::ui::PanelButton>, 8> modeKeys;
    std::array<std::unique_ptr<s6k::ui::PanelButton>, 12> keypad;
    std::unique_ptr<s6k::ui::PanelButton> cursorL, cursorR, windowKey, undoKey, exitKey, enterKey, markKey, jumpKey;
    std::array<std::unique_ptr<s6k::ui::PanelButton>, 3> userKeys;
    s6k::ui::DataWheel dataWheel;
    juce::Slider mainVolume, phonesLevel;
    s6k::ui::PowerButton power;
    juce::Label credit;
    juce::HyperlinkButton instagram { "www.instagram.com/dominikhop", juce::URL ("https://www.instagram.com/dominikhop") };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, zoomLabel { "100%" };

private:
    S6000AudioProcessor& proc;
    s6k::ui::KnobLookAndFeel knobLnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttach, phonesAttach;
};

//==============================================================================
class S6000AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer
{
public:
    explicit S6000AudioProcessorEditor (S6000AudioProcessor&);
    ~S6000AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setZoom (float z);
    float getZoom() const { return zoom; }

private:
    S6000AudioProcessor& proc;
    s6k::ui::Ctx ctx;
    s6k::ui::PageManager pages;
    S6000Panel panel;
    juce::ComponentBoundsConstrainer constrainer;
    float zoom = 1.0f;
    int bootTicks = 0, ledBlink = 0, idleTicks = 0;
    bool enterHeld = false;

    void timerCallback() override;
    void startBoot();
    void finishBoot();
    void updateLeds();
    void wireControls();
    void userActivity();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S6000AudioProcessorEditor)
};
