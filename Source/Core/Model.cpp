/*
  ==============================================================================
   S6000 Emulation - data model
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "Model.h"
#include <unordered_map>

namespace s6k
{
//==============================================================================
void Sample::sanitiseMarkers()
{
    const juce::int64 n = numFrames();
    start = juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, n - 1), start);
    if (end <= start || end > n) end = n;
    loopEnd = juce::jlimit<juce::int64> (0, n, loopEnd);
    loopStart = juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, loopEnd - 1), loopStart);
    if (loopEnd - loopStart < 2) loopEnabled = false;
    loopXfade = (int) juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, loopEnd - loopStart), (juce::int64) loopXfade);
}

Sample::Ptr Sample::clone (const juce::String& newName) const
{
    Sample::Ptr s = new Sample();
    s->name = newName;
    s->buffer.makeCopyOf (buffer);
    s->sampleRate = sampleRate; s->rootNote = rootNote; s->semitone = semitone; s->cents = cents;
    s->start = start; s->end = end; s->loopEnabled = loopEnabled; s->loopStart = loopStart; s->loopEnd = loopEnd;
    s->loopXfade = loopXfade; s->loopDirection = loopDirection; s->loopLock = loopLock; s->playMode = playMode;
    return s;
}

//==============================================================================
namespace
{
    template <typename F> void visitFx (FxMultiChannel& c, F&& f)
    {
        for (int* v : { &c.distType, &c.distAmount, &c.distLevel, &c.rmodFreq, &c.rmodDepth, &c.eqOn,
                        &c.lowFreq, &c.lfGain, &c.lmidFreq, &c.lmidGain, &c.hmidFreq, &c.hmidGain, &c.highFreq, &c.hfGain,
                        &c.modType, &c.modRate, &c.modDepth, &c.modFeedback, &c.rotSpeed1, &c.rotSpeed2, &c.rotAccel, &c.rotInitSpeed,
                        &c.pitchLSemi, &c.pitchLFine, &c.pitchRSemi, &c.pitchRFine, &c.amodRate, &c.amodDepth,
                        &c.delayType, &c.delayL, &c.delayR, &c.fbL, &c.fbR, &c.delayHfDamp, &c.pingPong,
                        &c.revOn, &c.revType, &c.preDelay, &c.revTime, &c.diffusion, &c.revHfDamp, &c.revLfDamp, &c.revLevel, &c.revPan,
                        &c.distEqLevel, &c.modDelLevel, &c.modDelPan, &c.modDelWidth, &c.directSignal, &c.path })
            f (*v);
    }
    template <typename F> void visitRv (FxReverbChannel& c, F&& f)
    {
        for (int* v : { &c.on, &c.input, &c.inputLevel, &c.type, &c.preDelay, &c.time, &c.diffusion, &c.hfDamp, &c.lfDamp, &c.level, &c.pan })
            f (*v);
    }
}

fmt::Bytes FxSettings::serialise() const
{
    fmt::Bytes b { 'E', 'B', '2', '0', 1 };
    auto put = [&b] (int& v) { b.push_back ((uint8_t) (v & 0xFF)); b.push_back ((uint8_t) ((v >> 8) & 0xFF)); };
    auto& self = const_cast<FxSettings&> (*this);
    for (auto& c : self.fx) visitFx (c, put);
    for (auto& c : self.rv) visitRv (c, put);
    put (self.bypass);
    return b;
}

void FxSettings::deserialise (const fmt::Bytes& b)
{
    if (b.size() < 5 || b[0] != 'E' || b[1] != 'B' || b[2] != '2' || b[3] != '0') return;
    size_t pos = 5;
    auto get = [&b, &pos] (int& v)
    {
        if (pos + 1 < b.size()) v = (int) (int16_t) (b[pos] | (b[pos + 1] << 8));
        pos += 2;
    };
    for (auto& c : fx) visitFx (c, get);
    for (auto& c : rv) visitRv (c, get);
    get (bypass);
}

//==============================================================================
Multi::Multi()
{
    parts.resize (maxParts);
    resolved.resize (maxParts);
    for (int i = 0; i < maxParts; ++i)
    {
        parts[(size_t) i].midiChannel = i % 32;
        parts[(size_t) i].fxChannel = 1;
    }
    fx.fx[1].modType = 2;
    fx.fx[1].revType = 5;
    fx.rv[1].type = 1;
}

int Multi::numUsedParts() const
{
    int n = 0;
    for (auto& p : parts) if (! p.programName.empty()) ++n;
    return n;
}

//==============================================================================
Memory::Memory()
{
    Multi::Ptr m = new Multi();
    m->name = "NEW MULTI 1";
    multis.add (m);
}

Multi* Memory::getCurrentMulti() const
{
    const int i = currentMulti.load();
    return juce::isPositiveAndBelow (i, multis.size()) ? multis.getObjectPointerUnchecked (i) : nullptr;
}

Sample::Ptr Memory::findSample (const juce::String& n) const
{
    if (n.isEmpty()) return nullptr;
    for (auto* s : samples) if (s->name.equalsIgnoreCase (n)) return s;
    // Akai S1000 names are upper-case, 12 chars: try a looser match
    const auto t = n.trim();
    for (auto* s : samples) if (s->name.trim().equalsIgnoreCase (t)) return s;
    return nullptr;
}

Program::Ptr Memory::findProgram (const juce::String& n) const
{
    if (n.isEmpty()) return nullptr;
    for (auto* p : programs) if (p->getName().equalsIgnoreCase (n)) return p;
    const auto t = n.trim();
    for (auto* p : programs) if (p->getName().trim().equalsIgnoreCase (t)) return p;
    return nullptr;
}

int Memory::indexOfSample (const Sample* s) const   { return samples.indexOf (s); }
int Memory::indexOfProgram (const Program* p) const { return programs.indexOf (p); }

void Memory::resolveProgram (Program& p) const
{
    p.resolved.resize (p.data.keygroups.size());
    for (size_t k = 0; k < p.data.keygroups.size(); ++k)
        for (int z = 0; z < 4; ++z)
            p.resolved[k][(size_t) z] = findSample (juce::String (p.data.keygroups[k].zones[z].sampleName));
}

void Memory::resolveMulti (Multi& m) const
{
    m.resolved.resize (m.parts.size());
    for (size_t i = 0; i < m.parts.size(); ++i)
        m.resolved[i] = findProgram (juce::String (m.parts[i].programName));
}

void Memory::resolveAll()
{
    // hash lookups: this runs under the audio lock, so it must stay fast even
    // with thousands of samples and 99-keygroup programs
    std::unordered_map<std::string, Sample*> sampleMap;
    sampleMap.reserve ((size_t) samples.size() * 2);
    for (auto* s : samples) sampleMap.emplace (s->name.trim().toLowerCase().toStdString(), s);
    std::unordered_map<std::string, Program*> programMap;
    programMap.reserve ((size_t) programs.size() * 2);
    for (auto* p : programs) programMap.emplace (p->getName().trim().toLowerCase().toStdString(), p);

    auto key = [] (const std::string& n) { return juce::String (n).trim().toLowerCase().toStdString(); };

    for (auto* p : programs)
    {
        p->resolved.resize (p->data.keygroups.size());
        for (size_t k = 0; k < p->data.keygroups.size(); ++k)
            for (int z = 0; z < 4; ++z)
            {
                const auto& nm = p->data.keygroups[k].zones[z].sampleName;
                Sample* s = nullptr;
                if (! nm.empty()) { auto it = sampleMap.find (key (nm)); if (it != sampleMap.end()) s = it->second; }
                p->resolved[k][(size_t) z] = s;
            }
    }
    for (auto* m : multis)
    {
        m->resolved.resize (m->parts.size());
        for (size_t i = 0; i < m->parts.size(); ++i)
        {
            const auto& nm = m->parts[i].programName;
            Program* p = nullptr;
            if (! nm.empty()) { auto it = programMap.find (key (nm)); if (it != programMap.end()) p = it->second; }
            m->resolved[i] = p;
        }
    }
}

size_t Memory::bytesUsed() const
{
    size_t b = 0;
    for (auto* s : samples) b += s->bytesUsed();
    return b;
}

int Memory::percentFree() const
{
    const double used = (double) bytesUsed() / (double) capacityBytes;
    return juce::jlimit (0, 100, (int) std::floor ((1.0 - used) * 100.0));
}

static juce::String makeUnique (const juce::String& base, const std::function<bool (const juce::String&)>& exists)
{
    juce::String b = base.trim();
    if (b.isEmpty()) b = "NEW";
    if (! exists (b)) return b;
    // strip a trailing number
    auto stem = b.trimCharactersAtEnd ("0123456789").trimEnd();
    if (stem.isEmpty()) stem = b;
    for (int i = 1; i < 10000; ++i)
    {
        auto c = (stem + " " + juce::String (i)).substring (0, 20);
        if (! exists (c)) return c;
    }
    return b;
}

juce::String Memory::uniqueSampleName (const juce::String& base) const
{
    return makeUnique (base, [this] (const juce::String& n) { return findSample (n) != nullptr; });
}

juce::String Memory::uniqueProgramName (const juce::String& base) const
{
    return makeUnique (base, [this] (const juce::String& n) { return findProgram (n) != nullptr; });
}

juce::String Memory::uniqueMultiName (const juce::String& base) const
{
    return makeUnique (base, [this] (const juce::String& n)
    {
        for (auto* m : multis) if (m->name.equalsIgnoreCase (n)) return true;
        return false;
    });
}

void Memory::clear()
{
    samples.clear();
    programs.clear();
    multis.clear();
    Multi::Ptr m = new Multi();
    m->name = "NEW MULTI 1";
    multis.add (m);
    currentMulti = 0;
}

//==============================================================================
namespace names
{
    static const char* filterNames[] = { "2-POLE LP", "4-POLE LP", "2-POLE LP+", "2-POLE BP", "4-POLE BP", "2-POLE BP+",
                                         "1-POLE HP", "2-POLE HP", "1-POLE HP+", "LO<>HI", "LO<>BAND", "BAND<>HI",
                                         "NOTCH 1", "NOTCH 2", "NOTCH 3", "WIDE NOTCH", "BI-NOTCH",
                                         "PEAK 1", "PEAK 2", "PEAK 3", "WIDE PEAK", "BI-PEAK",
                                         "PHASER 1", "PHASER 2", "BI-PHASE", "VOWELISER" };
    static const char* waveNames[] = { "SINE", "TRIANGLE", "SQUARE", "SQUARE+", "SQUARE-", "SAW BI", "SAW UP", "SAW DOWN", "RANDOM" };
    static const char* srcNames[] = { "NO SOURCE", "MODWHEEL", "BEND UP", "BEND DOWN", "AFT'TOUCH", "VELOCITY", "BIPOLAR VEL",
                                      "OFF VEL", "KEYBOARD", "LFO1", "LFO2", "AMP ENV", "FILT ENV", "AUX ENV", "EXT CTRL" };
    static const char* tuneNames[] = { "EVEN-TEMPERED", "ORCHESTRAL", "WERKMEISTER", "1/5 MEANTONE", "1/4 MEANTONE", "JUST", "ARABIAN", "USER" };
    static const char* revNames[] = { "ROOM", "STUDIO", "SMALL HALL", "LARGE HALL", "CHURCH", "PLATE", "GATED" };
    static const char* modNames[] = { "OFF", "CHORUS", "FLANGE", "PHASE", "ROTARY", "FREQ/AMP MOD", "PITCH SHIFT", "PITCH+FDBACK" };
    static const char* delNames[] = { "OFF", "MONO LEFT", "MONO L/R", "XOVER L+R", "STEREO DELAY" };

    juce::String note (int n, bool c3is60)
    {
        static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        n = juce::jlimit (0, 127, n);
        const int oct = n / 12 - (c3is60 ? 2 : 1);
        return juce::String (nn[n % 12]) + juce::String (oct);
    }

    juce::String midiChannel (int ch)
    {
        ch = juce::jlimit (0, 31, ch);
        return juce::String (ch % 16 + 1) + (ch < 16 ? "A" : "B");
    }

    juce::String output (int out, bool allowMulti)
    {
        if (allowMulti) { if (out == 0) return "MULTI"; --out; }
        if (out <= 0) return "L-R";
        if (out < 8) return juce::String (out * 2 + 1) + "/" + juce::String (out * 2 + 2);
        return juce::String (out - 7);
    }

    juce::String fxChannel (int ch)
    {
        static const char* f[] = { "OFF", "FX1", "FX2", "RV3", "RV4" };
        return f[juce::jlimit (0, 4, ch)];
    }

    juce::String playback (int m, bool allowAsSample)
    {
        static const char* p[] = { "NO LOOPING", "ONE SHOT", "LOOP IN REL", "LOOP UNTIL REL", "AS SAMPLE" };
        m = juce::jlimit (0, allowAsSample ? 4 : 3, m);
        return p[m];
    }

    juce::String filterType (int t)   { return filterNames[juce::jlimit (0, 25, t)]; }
    juce::String lfoWave (int w)      { return waveNames[juce::jlimit (0, 8, w)]; }
    juce::String modSource (int s)    { return srcNames[juce::jlimit (0, 14, s)]; }
    juce::String tuneTemplate (int t) { return tuneNames[juce::jlimit (0, 7, t)]; }
    juce::String reverbType (int t)   { return revNames[juce::jlimit (0, 6, t)]; }
    juce::String modFxType (int t)    { return modNames[juce::jlimit (0, 7, t)]; }
    juce::String delayType (int t)    { return delNames[juce::jlimit (0, 4, t)]; }
    int numFilterTypes()   { return 26; }
    int numModSources()    { return 15; }
    int numTuneTemplates() { return 8; }

    juce::String signedValue (int v, int digits)
    {
        return (v < 0 ? "-" : "+") + juce::String (std::abs (v)).paddedLeft ('0', digits);
    }

    juce::String panValue (int v)
    {
        if (v == 0) return "MID";
        return v < 0 ? "L" + juce::String (-v) : "R" + juce::String (v);
    }

    juce::String levelDb (int db)
    {
        return signedValue (db, 2) + "dB";
    }
}

float tuningOffsetCents (int templ, int key, int pitchClass, const int* user)
{
    static const float tables[7][12] = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0, -9.8f, 3.9f, -5.9f, 7.8f, -2.0f, -11.7f, 2.0f, -7.8f, 5.9f, -3.9f, 9.8f },        // orchestral (pythagorean)
        { 0, -9.8f, -7.8f, -5.9f, -9.8f, -2.0f, -11.7f, -3.9f, -7.8f, -11.7f, -3.9f, -7.8f },  // werckmeister III
        { 0, -16.4f, -4.7f, 7.0f, -9.4f, 2.3f, -14.1f, -2.3f, -18.8f, -7.0f, 4.7f, -11.7f },  // 1/5 comma meantone
        { 0, -24.0f, -6.8f, 10.3f, -13.7f, 3.4f, -20.5f, -3.4f, -27.4f, -10.3f, 6.8f, -17.1f }, // 1/4 comma meantone
        { 0, 11.7f, 3.9f, 15.6f, -13.7f, -2.0f, -9.8f, 2.0f, 13.7f, -15.6f, 17.6f, -11.7f },  // just
        { 0, 0, 0, 0, -50.0f, 0, 0, 0, 0, 0, 0, -50.0f }                                       // arabian
    };
    const int pc = ((pitchClass - key) % 12 + 12) % 12;
    if (templ == 7) return user != nullptr ? (float) user[pitchClass % 12] : 0.0f;
    return tables[juce::jlimit (0, 6, templ)][pc];
}

} // namespace s6k
