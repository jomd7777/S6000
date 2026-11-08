/*
  ==============================================================================
   S6000 Emulation - sampler engine
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "SamplerEngine.h"

namespace s6k
{
using fmt::ProgramData;

//==============================================================================
SamplerEngine::SamplerEngine (Memory& m) : mem (m)
{
    for (auto& v : chanVolume) v = 1.0f;

    // EDIT SAMPLE "RAW SAMPLE" monitor: plain playback, root note = sample's original note
    rawProgram = new Program();
    rawProgram->data.name = "RAW SAMPLE";
    rawProgram->data.velSens = 30;
    rawProgram->data.polyphony = 32;
    fmt::KeygroupData kg;
    kg.lowNote = 0; kg.highNote = 127;
    kg.cutoff = 100; kg.resonance = 0;
    kg.amp.attack = 0; kg.amp.decay = 50; kg.amp.sustain = 100; kg.amp.release = 20;
    kg.zones[0].sampleName = "RAW";
    kg.zones[0].playback = fmt::PlayAsSample;
    rawProgram->data.keygroups.push_back (kg);
    rawProgram->resolved.resize (1);
}

void SamplerEngine::prepare (double newSr, int)
{
    sr = newSr;
    crHz = (float) (sr / dsp::controlRate);
    injected.reset (sr);
    reset();
}

void SamplerEngine::reset()
{
    for (auto& v : voices) { v.active = false; v.program = nullptr; v.sample = nullptr; }
    for (int i = 0; i < 32; ++i) { modwheel[i] = aftertouch[i] = pitchbend[i] = extCtrl[i] = 0; sustainPedal[i] = false; chanVolume[i] = 1.0f; }
}

void SamplerEngine::injectNote (int channel, int note, int velocity, bool on)
{
    auto m = on ? juce::MidiMessage::noteOn (juce::jlimit (1, 16, channel + 1), note, (juce::uint8) juce::jlimit (1, 127, velocity))
                : juce::MidiMessage::noteOff (juce::jlimit (1, 16, channel + 1), note);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    injected.addMessageToQueue (m);
}

void SamplerEngine::allNotesOff()
{
    for (int c = 1; c <= 16; ++c)
    {
        auto m = juce::MidiMessage::allNotesOff (c);
        m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        injected.addMessageToQueue (m);
    }
}

void SamplerEngine::audition (Sample::Ptr s, bool loopRegionOnly, juce::int64 from, juce::int64 to)
{
    const juce::SpinLock::ScopedLockType sl (auditionLock);
    pendingAudition = s;
    if (s != nullptr)
    {
        auditionPos = (double) (from >= 0 ? from : (loopRegionOnly ? s->loopStart : s->start));
        auditionEnd = (double) (to >= 0 ? to : (loopRegionOnly ? s->loopEnd : s->end));
        auditionLoopStart = loopRegionOnly ? (double) s->loopStart : -1.0;
    }
    auditionPending = true;
}

bool SamplerEngine::popProgramChange (ProgramChangeRequest& r)
{
    int s1, n1, s2, n2;
    pcFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return false;
    r = pcData[(size_t) (n1 > 0 ? s1 : s2)];
    pcFifo.finishedRead (1);
    return true;
}

//==============================================================================
void SamplerEngine::render (juce::AudioBuffer<float>& outBus, juce::AudioBuffer<float>& fxBus, juce::MidiBuffer& midi, int numSamples)
{
    injectedBuffer.clear();
    injected.removeNextBlockOfMessages (injectedBuffer, numSamples);
    if (! injectedBuffer.isEmpty()) midi.addEvents (injectedBuffer, 0, numSamples, 0);

    // NOTE: the caller (processBlock) holds Memory::lock for the whole block.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        if (t > pos)
        {
            for (auto& v : voices) if (v.active) renderVoice (v, outBus, fxBus, pos, t - pos);
            pos = t;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        for (auto& v : voices) if (v.active) renderVoice (v, outBus, fxBus, pos, numSamples - pos);

    renderAudition (outBus, numSamples);

    int n = 0;
    for (auto& v : voices) if (v.active) ++n;
    activeVoices = n;
    globalTime += numSamples / sr;
}

//==============================================================================
void SamplerEngine::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (0, 15, m.getChannel() - 1);
    midiActivity = true;

    if (m.isNoteOn())
    {
        if (settings.rxNotes) noteOn (ch, m.getNoteNumber(), m.getVelocity());
    }
    else if (m.isNoteOff())
    {
        if (settings.rxNotes) noteOff (ch, m.getNoteNumber(), m.getVelocity());
    }
    else if (m.isPitchWheel())
    {
        if (settings.rxPitchbend) pitchbend[ch] = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    }
    else if (m.isChannelPressure())
    {
        if (settings.rxAftertouch) aftertouch[ch] = (float) m.getChannelPressureValue() / 127.0f;
    }
    else if (m.isAftertouch())
    {
        if (settings.rxAftertouch) aftertouch[ch] = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isController())
    {
        if (! settings.rxControllers) return;
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1) modwheel[ch] = (float) val / 127.0f;
        if (cc == settings.extApmController.load()) extCtrl[ch] = (float) val / 127.0f;
        if (cc == 7) chanVolume[ch] = (float) val / 127.0f;
        if (cc == 64)
        {
            const bool down = val >= 64;
            sustainPedal[ch] = down;
            if (! down)
                for (auto& v : voices)
                    if (v.active && v.sustained && v.channel == ch)
                    {
                        v.sustained = false; v.released = true;
                        if (v.playMode != fmt::PlayOneShot) { v.ampEnv.noteOff(); v.filtEnv.noteOff(); v.auxEnv.noteOff(); }
                    }
        }
        if (cc == 120)
            for (auto& v : voices) if (v.active && v.channel == ch) v.active = false;
        if (cc == 121) { modwheel[ch] = aftertouch[ch] = pitchbend[ch] = 0; chanVolume[ch] = 1; }
        if (cc == 123 || cc == 124 || cc == 125)
            for (auto& v : voices)
                if (v.active && v.channel == ch) { v.released = true; v.ampEnv.noteOff(); v.filtEnv.noteOff(); v.auxEnv.noteOff(); }
    }
    else if (m.isProgramChange())
    {
        if (! settings.rxProgramChange) return;
        const int pc = m.getProgramChangeNumber();
        if (settings.multiSelect && ch == settings.multiSelectChannel.load())
        {
            if (pc < mem.multis.size()) mem.currentMulti = pc;
            return;
        }
        if (settings.programChange)
        {
            int s1, n1, s2, n2;
            pcFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 > 0)
            {
                pcData[(size_t) (n1 > 0 ? s1 : s2)] = { ch, pc };
                pcFifo.finishedWrite (1);
            }
        }
    }
}

void SamplerEngine::noteOn (int ch, int note, int vel)
{
    note = juce::jlimit (0, 127, note + settings.transpose.load());
    const int es = settings.editSample.load();
    if (es >= 0 && es < mem.samples.size())
    {
        rawProgram->resolved[0][0] = mem.samples.getObjectPointer (es);
        fmt::PartData def;
        def.fxChannel = 0;   // dry, like monitoring the raw sample
        triggerProgram (rawProgram.get(), VoicePart::from (def), -1, ch, note, vel);
        return;
    }

    const int ep = settings.editProgram.load();
    if (ep >= 0 && ep < mem.programs.size())
    {
        fmt::PartData def;
        triggerProgram (mem.programs.getObjectPointerUnchecked (ep), VoicePart::from (def), -1, ch, note, vel);
        return;
    }

    auto* multi = mem.getCurrentMulti();
    if (multi == nullptr) return;
    bool anySolo = false;
    for (bool s : multi->solo) anySolo |= s;

    const bool merge = settings.mergeBtoA.load();
    for (int i = 0; i < (int) multi->parts.size() && i < (int) multi->resolved.size(); ++i)
    {
        const auto& part = multi->parts[(size_t) i];
        Program* prog = multi->resolved[(size_t) i].get();
        if (prog == nullptr || part.mute) continue;
        if (anySolo && ! multi->solo[(size_t) i]) continue;
        if (! (part.midiChannel == ch || (merge && part.midiChannel == ch + 16))) continue;
        if (note < part.lowKey || note > part.highKey) continue;
        triggerProgram (prog, VoicePart::from (part), i, ch, note, vel);
    }
}

void SamplerEngine::noteOff (int ch, int note, int vel)
{
    note = juce::jlimit (0, 127, note + settings.transpose.load());
    for (auto& v : voices)
    {
        if (! v.active || v.released || v.channel != ch || v.note != note) continue;
        v.offVelocity = vel;
        if (sustainPedal[ch]) { v.sustained = true; continue; }
        v.released = true;
        if (v.playMode == fmt::PlayOneShot) continue;
        v.ampEnv.noteOff(); v.filtEnv.noteOff(); v.auxEnv.noteOff();
    }
}

struct RangeList
{
    std::array<std::pair<int, int>, 100> r;
    int n = 0;
    void add (int lo, int hi) { if (n < (int) r.size()) r[(size_t) n++] = { lo, hi }; }
};

static float overlapGain (int value, int lo, int hi, const RangeList& others)
{
    float g = 1.0f;
    for (int i = 0; i < others.n; ++i)
    {
        const auto& o = others.r[(size_t) i];
        const int oLo = juce::jmax (lo, o.first), oHi = juce::jmin (hi, o.second);
        if (oLo > oHi || value < oLo || value > oHi) continue;
        const float len = (float) (oHi - oLo + 2);
        const bool weAreLower = lo < o.first || (lo == o.first && hi < o.second);
        const float x = weAreLower ? (float) (oHi - value + 1) / len : (float) (value - oLo + 1) / len;
        g = juce::jmin (g, std::sqrt (juce::jlimit (0.0f, 1.0f, x)));
    }
    return g;
}

void SamplerEngine::triggerProgram (Program* p, const VoicePart& part, int partIndex, int ch, int note, int vel)
{
    auto& d = p->data;
    const int noteM = juce::jlimit (0, 127, note + part.semi);
    if (p->resolved.size() != d.keygroups.size()) return;   // not resolved yet

    for (int k = 0; k < (int) d.keygroups.size(); ++k)
    {
        const auto& kg = d.keygroups[(size_t) k];
        if (noteM < kg.lowNote || noteM > kg.highNote) continue;

        float kgGain = 1.0f;
        if (d.keygroupXfade)
        {
            RangeList others;
            for (int j = 0; j < (int) d.keygroups.size(); ++j)
                if (j != k) others.add (d.keygroups[(size_t) j].lowNote, d.keygroups[(size_t) j].highNote);
            kgGain = overlapGain (noteM, kg.lowNote, kg.highNote, others);
        }

        // mute groups (e.g. open / closed hi-hat)
        if (kg.muteGroup > 0)
            for (auto& v : voices)
                if (v.active && v.program.get() == p && v.partIndex == partIndex && v.kgIndex < (int) d.keygroups.size()
                    && d.keygroups[(size_t) v.kgIndex].muteGroup == kg.muteGroup)
                {
                    v.released = true;
                    v.ampEnv.releaseCoef = std::exp (-4.6f / (0.006f * crHz));
                    v.ampEnv.noteOff();
                }

        for (int z = 0; z < 4; ++z)
        {
            const auto& zone = kg.zones[z];
            Sample* s = p->resolved[(size_t) k][(size_t) z].get();
            if (s == nullptr || s->numFrames() < 2) continue;
            if (vel < zone.lowVel || vel > zone.highVel) continue;

            float zGain = 1.0f;
            if (kg.zoneXfade)
            {
                RangeList others;
                for (int j = 0; j < 4; ++j)
                    if (j != z && p->resolved[(size_t) k][(size_t) j] != nullptr)
                        others.add (kg.zones[j].lowVel, kg.zones[j].highVel);
                zGain = overlapGain (vel, zone.lowVel, zone.highVel, others);
            }

            if (Voice* v = allocateVoice (p, partIndex))
                startVoice (*v, p, k, z, s, part, partIndex, ch, noteM, vel, kgGain, zGain);
        }
    }
}

Voice* SamplerEngine::allocateVoice (Program* p, int partIndex)
{
    // per-program polyphony
    int count = 0;
    Voice* oldestOfProgram = nullptr;
    for (auto& v : voices)
        if (v.active && v.program.get() == p && v.partIndex == partIndex)
        {
            ++count;
            if (oldestOfProgram == nullptr || v.age < oldestOfProgram->age) oldestOfProgram = &v;
        }
    if (count >= juce::jmax (1, p->data.polyphony) && oldestOfProgram != nullptr)
        return oldestOfProgram;

    const int limit = juce::jlimit (1, maxVoices, settings.maxPolyphony.load());
    int used = 0;
    Voice* freeVoice = nullptr;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[(size_t) i].active) ++used;
        else if (freeVoice == nullptr && i < limit) freeVoice = &voices[(size_t) i];
    }
    if (freeVoice != nullptr && used < limit) return freeVoice;

    // steal: quietest released voice, otherwise the oldest
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.active && v.released && (best == nullptr || v.envA < best->envA)) best = &v;
    if (best == nullptr)
        for (auto& v : voices)
            if (v.active && (best == nullptr || v.age < best->age)) best = &v;
    return best;
}

void SamplerEngine::startVoice (Voice& v, Program* p, int k, int z, Sample* s, const VoicePart& part, int partIndex,
                                int ch, int note, int vel, float xfadeGain, float zoneGain)
{
    const auto& d = p->data;
    const auto& kg = d.keygroups[(size_t) k];
    const auto& zone = kg.zones[z];

    v.active = true;
    v.age = ++ageCounter;
    v.program = p;
    v.sample = s;
    v.part = part;
    v.partIndex = partIndex;
    v.kgIndex = k; v.zoneIndex = z;
    v.channel = ch; v.note = note; v.velocity = vel; v.offVelocity = 64;
    v.released = false; v.sustained = false;
    v.playMode = zone.playback == fmt::PlayAsSample ? s->playMode : zone.playback;
    v.dir = 1;
    v.kgXfadeGain = xfadeGain;
    v.zoneGain = zoneGain;

    // start position (velocity -> start)
    double startPos = (double) s->start;
    if (zone.velStart != 0)
    {
        const double ms = (double) zone.velStart * (zone.velStart > 0 ? vel / 127.0 : (1.0 - vel / 127.0));
        startPos += std::abs (ms) * 0.001 * s->sampleRate;
    }
    v.pos = juce::jlimit (0.0, (double) juce::jmax (0, s->numFrames() - 2), startPos);

    // static pitch
    const int keyNote = zone.keyTrack ? note : s->rootNote;
    float semis = (float) (keyNote - s->rootNote)
                + (float) kg.semi + (float) kg.fine * 0.01f
                + (float) zone.semi + (float) zone.fine * 0.01f
                + (float) d.semi + (float) d.fine * 0.01f
                + (float) part.semi + (float) part.fine * 0.01f
                + (float) s->semitone + (float) s->cents * 0.01f
                + (float) settings.semitoneTune.load() + (float) settings.fineTune.load() * 0.01f
                + tuningOffsetCents (d.tuneTemplate, d.key, note % 12, d.userTune) * 0.01f;
    v.staticSemis = semis;
    v.baseRatio = s->sampleRate / sr;
    v.heldBend = 0;

    // velocity sensitivity
    const float vs = (float) d.velSens * 0.01f, vn = (float) vel / 127.0f;
    float velGain = vs >= 0 ? 1.0f - vs * (1.0f - vn) : 1.0f + vs * vn;
    velGain = juce::jlimit (0.0f, 1.0f, velGain);
    v.staticGain = velGain * velGain * juce::Decibels::decibelsToGain ((float) zone.level - 6.0f * (float) kg.attenuation);
    v.staticPan = juce::jlimit (-1.0f, 1.0f, (float) (part.pan + zone.pan) / 50.0f);

    // envelopes
    const float keyOff = (float) (note - 60) / 24.0f;
    auto envSet = [&] (dsp::Adsr& e, const fmt::EnvData& ed)
    {
        float att = dsp::envTimeMs ((float) ed.attack) * std::exp2 (-(float) ed.velAttack * 0.01f * vn * 3.0f);
        const float ks = std::exp2 (-(float) ed.keyscale * 0.01f * keyOff);
        float rel = dsp::envTimeMs ((float) ed.release) * ks;
        e.set (att, dsp::envTimeMs ((float) ed.decay) * ks, (float) ed.sustain * 0.01f, rel, crHz);
        e.noteOn();
    };
    envSet (v.ampEnv, kg.amp);
    envSet (v.filtEnv, kg.filt);
    v.auxEnv.set (kg.aux.rate, kg.aux.level, crHz);
    v.auxEnv.noteOn();

    // LFOs
    v.lfo1 = dsp::Lfo(); v.lfo2 = dsp::Lfo();
    v.lfo1.seed ^= (juce::uint32) (v.age * 2654435761u);
    v.lfo2.seed ^= (juce::uint32) (v.age * 40503u + 7);
    v.lfo1Time = v.lfo2Time = 0;
    v.lfo1Val = v.lfo2Val = 0;

    // LFO1 SYNC : start in phase with an already playing voice of the same program
    if (d.lfo1.syncOrRetrigger)
        for (auto& o : voices)
            if (&o != &v && o.active && o.program.get() == p) { v.lfo1.phase = o.lfo1.phase; break; }

    v.filter[0].reset(); v.filter[1].reset();

    // routing
    const int fxCh = kg.fxOverride > 0 ? kg.fxOverride : part.fxChannel;
    const int send = kg.fxOverride > 0 ? kg.fxSend : part.fxSend;
    v.fxChannel = juce::jlimit (0, 4, fxCh);
    v.sendGain = v.fxChannel > 0 ? (float) send * 0.01f : 0.0f;
    v.sendGain *= v.sendGain;
    int out = zone.output > 0 ? zone.output - 1 : part.output;
    if (out >= 8) { v.monoChannel = juce::jlimit (0, 15, out - 8); v.outPair = v.monoChannel / 2; }
    else { v.monoChannel = -1; v.outPair = juce::jlimit (0, 7, out); }

    v.gL = v.gR = 0;
    v.controlCounter = 0;
    v.fadeOut = -1.0f;
}

float SamplerEngine::modSource (const Voice& v, int src) const
{
    const int ch = v.channel;
    switch (src)
    {
        case 1:  return modwheel[ch];
        case 2:  return juce::jmax (0.0f, pitchbend[ch]);
        case 3:  return juce::jmax (0.0f, -pitchbend[ch]);
        case 4:  return aftertouch[ch];
        case 5:  return (float) v.velocity / 127.0f;
        case 6:  return (float) v.velocity / 63.5f - 1.0f;
        case 7:  return (float) v.offVelocity / 127.0f;
        case 8:  return (float) (v.note - 60) / 64.0f;
        case 9:  return v.lfo1Val;
        case 10: return v.lfo2Val;
        case 11: return v.envA;
        case 12: return v.envF;
        case 13: return v.envX;
        case 14: return extCtrl[ch];
        default: return 0.0f;
    }
}

void SamplerEngine::updateVoiceControl (Voice& v)
{
    const auto& d = v.program->data;
    if (v.kgIndex >= (int) d.keygroups.size()) { v.active = false; return; }
    const auto& kg = d.keygroups[(size_t) v.kgIndex];
    const auto& zone = kg.zones[v.zoneIndex];
    const float dt = (float) dsp::controlRate / (float) sr;
    const int* ms = d.modSource;

    v.envA = v.ampEnv.tick();
    v.envF = v.filtEnv.tick();
    v.envX = v.auxEnv.tick();
    if (! v.ampEnv.isActive()) { v.active = false; v.tgL = v.tgR = 0; return; }

    auto lfoValue = [&] (dsp::Lfo& l, const fmt::LfoData& ld, float& time, bool globalPhase, int rateSrc, int delaySrc, int depthSrc)
    {
        const float rate = (float) ld.rate + (float) ld.rateMod * 0.5f * modSource (v, ms[rateSrc]);
        const float hz = dsp::lfoRateHz (rate);
        float w;
        if (globalPhase)
        {
            const float ph = (float) std::fmod (globalTime * hz, 1.0);
            if (ph < l.phase) l.rnd = l.nextRandom();
            l.phase = ph;
            w = dsp::lfoWave (ld.waveform, ph, l.rnd);
        }
        else
            w = l.advance (ld.waveform, hz, dt);
        time += dt;
        const float delaySec = dsp::envTimeMs ((float) ld.delay + (float) ld.delayMod * 0.5f * modSource (v, ms[delaySrc])) * 0.001f;
        const float fade = delaySec <= 0.0f ? 1.0f : juce::jmin (1.0f, time / delaySec);
        const float depth = juce::jlimit (0.0f, 1.0f, ((float) ld.depth + modwheel[v.channel] * (float) ld.modwheel
                                                        + aftertouch[v.channel] * (float) ld.aftertouch
                                                        + (float) ld.depthMod * 0.5f * modSource (v, ms[depthSrc])) * 0.01f);
        return w * depth * fade;
    };
    v.lfo1Val = lfoValue (v.lfo1, d.lfo1, v.lfo1Time, d.lfo1.syncOrRetrigger != 0, ProgramData::Lfo1Rate, ProgramData::Lfo1Delay, ProgramData::Lfo1Depth);
    v.lfo2Val = lfoValue (v.lfo2, d.lfo2, v.lfo2Time, d.lfo2.syncOrRetrigger == 0, ProgramData::Lfo2Rate, ProgramData::Lfo2Delay, ProgramData::Lfo2Depth);

    // ---- pitch
    const float pb = pitchbend[v.channel];
    float bend = pb >= 0 ? pb * (float) d.bendUp : pb * (float) d.bendDown;
    if (d.bendMode == 1 && v.released) bend = v.heldBend; else v.heldBend = bend;
    const float semis = v.staticSemis + bend + aftertouch[v.channel] * (float) d.aftertouchBend
                      + (float) kg.pitchMod1 * 0.02f * modSource (v, ms[ProgramData::PitchMod1])
                      + (float) kg.pitchMod2 * 0.02f * modSource (v, ms[ProgramData::PitchMod2]);
    v.inc = (float) v.baseRatio * std::exp2 (semis / 12.0f);

    // ---- filter
    float cutoff = (float) kg.cutoff + (float) zone.filter * 0.5f
                 + ((float) kg.filterKeyTrack / 36.0f) * (float) (v.note - 60) * (10.0f / 12.0f)
                 + (float) kg.filt.depth * v.envF
                 + (float) kg.filterMod1 * 0.5f * modSource (v, ms[ProgramData::FiltMod1])
                 + (float) kg.filterMod2 * 0.5f * modSource (v, ms[ProgramData::FiltMod2])
                 + (float) kg.filterMod3 * 0.5f * modSource (v, ms[ProgramData::FiltMod3]);
    cutoff = juce::jlimit (0.0f, 100.0f, cutoff);
    const float reso = (float) kg.resonance / 12.0f;
    v.filter[0].setup (kg.filterMode, cutoff, reso, (float) sr);
    if (v.sample->isStereo())
    {
        auto& f1 = v.filter[1];
        f1.type = v.filter[0].type; f1.bypass = v.filter[0].bypass; f1.gain = v.filter[0].gain; f1.morph = v.filter[0].morph;
        f1.fbAmt = v.filter[0].fbAmt; f1.stages = v.filter[0].stages;
        auto copySvf = [] (dsp::Svf& dst, const dsp::Svf& src) { dst.g = src.g; dst.k = src.k; dst.a1 = src.a1; dst.a2 = src.a2; dst.a3 = src.a3; };
        copySvf (f1.a, v.filter[0].a); copySvf (f1.b, v.filter[0].b);
        f1.hp.a = v.filter[0].hp.a;
        for (int i = 0; i < 12; ++i) f1.ap[i].a = v.filter[0].ap[i].a;
    }

    // ---- amplitude
    float amp = v.envA * v.staticGain * v.kgXfadeGain * v.zoneGain;
    amp *= juce::jmax (0.0f, 1.0f + (float) d.ampMod1 * 0.01f * modSource (v, ms[ProgramData::AmpMod1]));
    amp *= juce::jmax (0.0f, 1.0f + (float) d.ampMod2 * 0.01f * modSource (v, ms[ProgramData::AmpMod2]));
    amp *= juce::jmax (0.0f, 1.0f + (float) kg.ampMod * 0.01f * modSource (v, ms[ProgramData::KgAmpMod]));

    int partLevel = v.part.level, partPan = 0;
    if (v.partIndex >= 0)
        if (auto* m = mem.getCurrentMulti())
            if (v.partIndex < (int) m->parts.size())
            {
                partLevel = m->parts[(size_t) v.partIndex].level;
                partPan = m->parts[(size_t) v.partIndex].pan - v.part.pan;  // live change since note-on
            }
    const float pl = (float) partLevel / 80.0f, ld = (float) d.loudness / 85.0f;
    amp *= pl * pl * ld * ld * chanVolume[v.channel]
         * juce::Decibels::decibelsToGain ((float) settings.masterLevelDb.load());

    float pan = v.staticPan + (float) partPan / 50.0f
              + (float) d.panMod1 * 0.02f * modSource (v, ms[ProgramData::PanMod1])
              + (float) d.panMod2 * 0.02f * modSource (v, ms[ProgramData::PanMod2])
              + (float) d.panMod3 * 0.02f * modSource (v, ms[ProgramData::PanMod3]);
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.tgL = amp * std::cos (angle) * juce::MathConstants<float>::sqrt2;
    v.tgR = amp * std::sin (angle) * juce::MathConstants<float>::sqrt2;
}

//==============================================================================
static inline float hermite (const float* d, int n, int i, float f)
{
    const float xm1 = d[juce::jmax (0, i - 1)];
    const float x0  = d[i];
    const float x1  = d[juce::jmin (n - 1, i + 1)];
    const float x2  = d[juce::jmin (n - 1, i + 2)];
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return ((a * f - b) * f + c) * f + x0;
}

void SamplerEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& outBus, juce::AudioBuffer<float>& fxBus, int start, int num)
{
    Sample* s = v.sample.get();
    const int n = s->numFrames();
    const float* d0 = s->buffer.getReadPointer (0);
    const float* d1 = s->isStereo() ? s->buffer.getReadPointer (1) : nullptr;
    const bool hq = settings.interpolation.load() != 0;

    float* oL = outBus.getWritePointer (v.outPair * 2) + start;
    float* oR = outBus.getWritePointer (v.outPair * 2 + 1) + start;
    float* oMono = v.monoChannel >= 0 ? outBus.getWritePointer (v.monoChannel) + start : nullptr;
    float* fL = v.fxChannel > 0 ? fxBus.getWritePointer ((v.fxChannel - 1) * 2) + start : nullptr;
    float* fR = v.fxChannel > 0 ? fxBus.getWritePointer ((v.fxChannel - 1) * 2 + 1) + start : nullptr;

    const double loopS = (double) s->loopStart, loopE = (double) s->loopEnd;
    const double endPos = (double) juce::jmin<juce::int64> (s->end > 0 ? s->end : n, n) - 1.0;
    const bool hasLoop = s->loopEnabled && loopE - loopS >= 2.0 && loopE <= n;

    for (int i = 0; i < num; ++i)
    {
        if (v.controlCounter <= 0)
        {
            updateVoiceControl (v);
            if (! v.active) return;
            v.controlCounter = dsp::controlRate;
            v.dgL = (v.tgL - v.gL) / (float) dsp::controlRate;
            v.dgR = (v.tgR - v.gR) / (float) dsp::controlRate;
        }
        --v.controlCounter;

        const int ip = (int) v.pos;
        const float fr = (float) (v.pos - ip);
        float l, r;
        if (hq) { l = hermite (d0, n, ip, fr); r = d1 != nullptr ? hermite (d1, n, ip, fr) : l; }
        else
        {
            const int ip1 = juce::jmin (n - 1, ip + 1);
            l = d0[ip] + fr * (d0[ip1] - d0[ip]);
            r = d1 != nullptr ? d1[ip] + fr * (d1[ip1] - d1[ip]) : l;
        }

        // advance + loop
        const bool looping = hasLoop && (v.playMode == fmt::PlayLoopInRelease || (v.playMode == fmt::PlayLoopUntilRelease && ! v.released));
        v.pos += (double) v.inc * v.dir;
        if (looping)
        {
            if (s->loopDirection == 1)
            {
                if (v.dir > 0 && v.pos >= loopE) { v.pos = loopE - (v.pos - loopE); v.dir = -1; }
                else if (v.dir < 0 && v.pos <= loopS) { v.pos = loopS + (loopS - v.pos); v.dir = 1; }
            }
            else if (v.pos >= loopE)
                v.pos -= (loopE - loopS);
        }
        else
        {
            if (v.dir < 0) v.dir = 1;
            if (v.pos >= endPos) { v.active = false; return; }
        }
        v.pos = juce::jlimit (0.0, (double) (n - 1), v.pos);

        // filter
        if (! v.filter[0].bypass)
        {
            l = v.filter[0].process (l);
            r = d1 != nullptr ? v.filter[1].process (r) : l;
        }

        v.gL += v.dgL; v.gR += v.dgR;
        const float outLv = l * v.gL, outRv = r * v.gR;
        if (oMono != nullptr) oMono[i] += 0.5f * (outLv + outRv);
        else { oL[i] += outLv; oR[i] += outRv; }
        if (fL != nullptr) { fL[i] += outLv * v.sendGain; fR[i] += outRv * v.sendGain; }
    }
}

void SamplerEngine::renderAudition (juce::AudioBuffer<float>& outBus, int num)
{
    if (auditionPending.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (auditionLock);
        if (sl.isLocked())
        {
            auditionSample = pendingAudition;
            pendingAudition = nullptr;
            auditionPending = false;
            auditionActive = auditionSample != nullptr;
        }
    }
    if (auditionSample == nullptr) return;

    Sample* s = auditionSample.get();
    const int n = s->numFrames();
    if (n < 2) { auditionSample = nullptr; auditionActive = false; return; }
    const float* d0 = s->buffer.getReadPointer (0);
    const float* d1 = s->isStereo() ? s->buffer.getReadPointer (1) : d0;
    float* oL = outBus.getWritePointer (0);
    float* oR = outBus.getWritePointer (1);
    const double inc = s->sampleRate / sr;
    const float g = auditionGain.load();
    const double end = juce::jlimit (1.0, (double) n - 1.0, auditionEnd);

    for (int i = 0; i < num; ++i)
    {
        const int ip = (int) auditionPos;
        const float fr = (float) (auditionPos - ip);
        const int ip1 = juce::jmin (n - 1, ip + 1);
        oL[i] += g * (d0[ip] + fr * (d0[ip1] - d0[ip]));
        oR[i] += g * (d1[ip] + fr * (d1[ip1] - d1[ip]));
        auditionPos += inc;
        if (auditionPos >= end)
        {
            if (auditionLoopStart >= 0) auditionPos = auditionLoopStart;
            else { auditionSample = nullptr; auditionActive = false; break; }
        }
    }
    auditionPosAtomic = auditionPos;
}

} // namespace s6k
