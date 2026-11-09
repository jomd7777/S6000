/*
  ==============================================================================
   AKAI S6000 style sampler  -  OS Ver. 2.14 emulation
   AU / VST3 / AAX / Standalone   -   JUCE 8
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>
#include "Core/Model.h"
#include "Core/SamplerEngine.h"
#include "Core/EffectsEngine.h"
#include "Core/DiskManager.h"
#include "Core/Recorder.h"

/** Utilities > PREFERENCES / SYSTEM SETUP / MIDI SETUP values (global, like the
    battery backed RAM of the hardware – stored in the user settings file). */
struct S6000Prefs
{
    int noteDisplay = 0;        // 0 C3=60 (AKAI)  1 C4=60
    int loopDisplay = 0;        // 0 START/END  1 START/LENGTH
    int screenInvert = 0;
    int screenSaver = 0;        // 0 OFF, minutes
    int keyRepeatDelay = 4;
    int saveSession = 1;        // store memory inside the DAW project
    int autoload = 1;
    int showSplash = 1;
    int sysTranspose = 0, sysSemitone = 0, sysFine = 0, sysMasterLevel = 0;
    int midiProgramChange = 1, midiMultiSelect = 0, midiMultiSelectCh = 0, extApmCtrl = 16, mergeBtoA = 1;
    int rxNotes = 1, rxBend = 1, rxCtrl = 1, rxAftertouch = 1, rxProgram = 1;
    int interpolation = 1;
    int polyphony = 128;
    int playKey = 60;
    int fanSpeed = 0;
    int fxOutPair = 0;          // FX IN/OUTS: where the EB20 returns go (0 = L-R)
    int directMonitor = 0;      // FX IN/OUTS: L/R DIRECT LEVEL (input monitoring 0..100)
};

class S6000AudioProcessor : public juce::AudioProcessor,
                            private juce::Timer
{
public:
    S6000AudioProcessor();
    ~S6000AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "S6000"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    s6k::Memory memory;
    s6k::SamplerEngine engine { memory };
    s6k::EffectsEngine effects;
    s6k::DiskManager disk { memory };
    s6k::Recorder recorder;
    juce::AudioProcessorValueTreeState params;

    S6000Prefs prefs;
    void applyPrefsToEngine();
    void savePrefs();

    // global (all instances) UI settings, e.g. the last ZOOM
    juce::PropertiesFile& settings();
    float getZoom();
    void setZoom (float z);

    std::atomic<bool> powerOn { true };
    std::atomic<bool> testTone { false };
    std::atomic<float> outPeakL { 0 }, outPeakR { 0 };
    double getSampleRate() const { return currentSampleRate; }

    /** Keep removed objects alive until the audio thread no longer uses them. */
    void retire (juce::ReferenceCountedObject* o);

    juce::ValueTree uiState { "UI" };       // stored in the DAW project
    std::function<void()> onSessionRestored;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    juce::ApplicationProperties appProps;
    juce::AudioBuffer<float> outBus, fxBus, recIn;
    juce::SmoothedValue<float> mainGain, phonesGain;
    double currentSampleRate = 44100.0;
    double testTonePhase = 0.0;
    juce::ReferenceCountedArray<juce::ReferenceCountedObject> graveyard;
    juce::CriticalSection graveyardLock;
    bool autoloadDone = false;
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0);   // guards async callbacks

    void timerCallback() override;
    void loadPrefs();
    juce::ValueTree saveSession();
    void restoreSession (const juce::ValueTree& t);

    static BusesProperties makeBuses();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (S6000AudioProcessor)
};
