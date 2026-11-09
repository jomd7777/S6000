/*
  ==============================================================================
   S6000 Emulation - RECORD mode sampler (audio thread side is lock free)
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

namespace s6k
{
class Recorder
{
public:
    enum State { Idle = 0, Armed, Recording, Done };
    enum Trigger { OneKey = 0, ThresholdLR, ThresholdL, ThresholdR, MidiNote };
    enum Source { Analogue = 0, Digital, MainOuts };

    // settings (edited on the RECORD page)
    int recordLevelDb = 0;       // -inf..+12  (software gain)
    int trigger = ThresholdLR;
    int thresholdDb = -40;
    int midiTrigChannel = 0;     // 0..31
    int lengthSeconds = 10;      // min.sec * 100 (we store seconds * 100) -> see page
    int originalNote = 60;
    int stereo = 0;              // 0 MONO 1 STEREO
    int source = Analogue;

    std::atomic<int> state { Idle };
    std::atomic<float> peakL { 0 }, peakR { 0 };
    std::atomic<juce::int64> recorded { 0 };

    void prepare (double sr) { sampleRate = sr; }
    double getSampleRate() const { return sampleRate; }

    /** message thread: allocate the buffer and arm */
    void arm (double seconds)
    {
        state = Idle;
        const int frames = (int) juce::jlimit (1024.0, sampleRate * 60.0 * 30.0, seconds * sampleRate);
        buffer.setSize (stereo ? 2 : 1, frames, false, true, false);
        buffer.clear();
        recorded = 0;
        state = Armed;
    }
    void startNow()   { if (state.load() == Armed) state = Recording; }
    void stop()       { if (state.load() == Recording || state.load() == Armed) state = recorded.load() > 0 ? Done : Idle; }
    void reset()      { state = Idle; recorded = 0; }

    /** audio thread */
    void process (const float* inL, const float* inR, int n, const juce::MidiBuffer& midi)
    {
        const float g = juce::Decibels::decibelsToGain ((float) recordLevelDb, -96.0f);
        float pl = 0, pr = 0;
        for (int i = 0; i < n; ++i) { pl = juce::jmax (pl, std::abs (inL[i] * g)); pr = juce::jmax (pr, std::abs (inR[i] * g)); }
        peakL = juce::jmax (pl, peakL.load() * 0.85f);
        peakR = juce::jmax (pr, peakR.load() * 0.85f);

        int st = state.load();
        if (st == Armed)
        {
            const float th = juce::Decibels::decibelsToGain ((float) thresholdDb);
            if ((trigger == ThresholdLR && juce::jmax (pl, pr) >= th) || (trigger == ThresholdL && pl >= th) || (trigger == ThresholdR && pr >= th))
                st = Recording;
            if (trigger == MidiNote)
                for (const auto m : midi)
                {
                    const auto msg = m.getMessage();
                    if (msg.isNoteOn() && (msg.getChannel() - 1) == midiTrigChannel % 16) { st = Recording; break; }
                }
            if (st == Recording) state = Recording;
        }
        if (st != Recording) return;

        juce::int64 pos = recorded.load();
        const int room = (int) juce::jmin<juce::int64> (n, buffer.getNumSamples() - pos);
        if (room > 0)
        {
            if (buffer.getNumChannels() == 1)
            {
                float* d = buffer.getWritePointer (0) + pos;
                for (int i = 0; i < room; ++i) d[i] = 0.5f * (inL[i] + inR[i]) * g;
            }
            else
            {
                float* a = buffer.getWritePointer (0) + pos;
                float* b = buffer.getWritePointer (1) + pos;
                for (int i = 0; i < room; ++i) { a[i] = inL[i] * g; b[i] = inR[i] * g; }
            }
            pos += room;
            recorded = pos;
        }
        if (pos >= buffer.getNumSamples()) state = Done;
    }

    juce::AudioBuffer<float> buffer;

private:
    double sampleRate = 44100.0;
};

} // namespace s6k
