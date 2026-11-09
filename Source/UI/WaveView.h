/*
  ==============================================================================
   S6000 Emulation - EDIT SAMPLE waveform display
     Overview strip + zoomable main view, LOG / LIN amplitude display,
     START..END highlight, loop region, edit point flags, slice lines,
     animated play line (audition + MIDI voices) and PLAY / STOP status.
     Peaks are cached per sample version / range / width (CPU friendly).
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#pragma once
#include "PageSystem.h"

namespace s6k::ui::wave
{
using juce::int64;

/** per-column peak cache */
struct Peaks
{
    const Sample* sample = nullptr;
    int version = -1, cols = 0;
    int64 from = 0, to = 0;
    bool sampleLevel = false;               // zoomed in past 1 frame per pixel
    std::vector<float> mn, mx, rms;

    void update (const Sample& s, int64 from, int64 to, int cols);
};

struct Marker
{
    int64 pos = 0;
    juce::String label;
    int side = 1;                           // flag to the right (+1) or left (-1) of the line
    bool selected = false;
    juce::Colour colour = colours::oledWarn;
};

enum class LoopStyle { None, Invert, Band };

struct Options
{
    int64 from = 0, to = 0;                 // visible range (frames)
    bool log = true;                        // LOG (unipolar dB) or LIN (bipolar)
    bool compact = false;                   // overview strip: no flags / tags / ruler

    int64 hlFrom = -1, hlTo = -1;           // highlighted region (START..END etc.)
    LoopStyle loopStyle = LoopStyle::None;
    int64 loopFrom = 0, loopTo = 0;

    std::vector<Marker> markers;
    std::vector<int64> slices;              // thin dotted lines (CHOP slices)
    std::vector<std::pair<int64, int64>> hatched;   // regions that will disappear (CUT / ERASE preview)

    int64 windowFrom = -1, windowTo = -1;   // overview: the zoomed part shown below

    bool showStatus = true;                 // PLAY / STOP tag
    bool playing = false;
    int64 playPos = -1;
    std::vector<int64> voices;              // notes played from the MIDI keyboard
    double sampleRate = 44100.0;

    juce::String tagLeft;                   // e.g. "LOG x4"
    std::function<void (juce::Graphics&, juce::Rectangle<float>, const std::function<float (int64)>&)> overlay;
};

float xOf (juce::Rectangle<float> r, int64 from, int64 to, int64 pos);
int64 posAt (juce::Rectangle<float> r, int64 from, int64 to, float x);

void draw (juce::Graphics& g, juce::Rectangle<float> r, const Sample* s, const Options& o, Peaks& cache);

/** small helper: 0..1 level -> display height fraction for LOG view (48 dB range) */
float logHeight (float v);

juce::String zoomTag (bool log, int64 total, int64 visible);
}
