/*
  ==============================================================================
   AKAI S6000 style sampler  -  processor
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace s6k;

//==============================================================================
juce::AudioProcessor::BusesProperties S6000AudioProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main L-R", juce::AudioChannelSet::stereo(), true);
    for (int i = 1; i < numOutputPairs; ++i)
        b = b.withOutput ("Out " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2), juce::AudioChannelSet::stereo(), false);

    // Record input. AU instruments are kept output-only for maximum host compatibility
    // (record from MAIN OUTS / re-sample there).
    if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_AudioUnit)
        b = b.withInput ("Rec In", juce::AudioChannelSet::stereo(),
                         juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone);
    return b;
}

juce::AudioProcessorValueTreeState::ParameterLayout S6000AudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mainVolume", 1 }, "Main Volume",
                                                        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "phonesLevel", 1 }, "Phones / Audition Level",
                                                        juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), -6.0f,
                                                        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "fxBypass", 1 }, "FX Bypass", false));
    return l;
}

S6000AudioProcessor::S6000AudioProcessor()
    : AudioProcessor (makeBuses()),
      params (*this, nullptr, "PARAMS", createLayout())
{
    juce::PropertiesFile::Options o;
    o.applicationName = "S6000";
    o.filenameSuffix = ".settings";
    o.folderName = juce::String ("dominikHop") + juce::File::getSeparatorString() + "S6000";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
    appProps.setStorageParameters (o);

    loadPrefs();
    applyPrefsToEngine();
    startTimerHz (15);
}

S6000AudioProcessor::~S6000AudioProcessor()
{
    stopTimer();
    savePrefs();
    if (auto* s = appProps.getUserSettings()) s->saveIfNeeded();
}

juce::PropertiesFile& S6000AudioProcessor::settings()
{
    return *appProps.getUserSettings();
}

float S6000AudioProcessor::getZoom()
{
    return juce::jlimit (0.5f, 2.0f, (float) settings().getDoubleValue ("zoom", 1.0));
}

void S6000AudioProcessor::setZoom (float z)
{
    // saved automatically by the PropertiesFile timer (and on close)
    settings().setValue ("zoom", juce::jlimit (0.5f, 2.0f, z));
}

//==============================================================================
void S6000AudioProcessor::loadPrefs()
{
    auto& s = settings();
    auto get = [&s] (const char* k, int& v) { v = s.getIntValue (k, v); };
    get ("noteDisplay", prefs.noteDisplay); get ("loopDisplay", prefs.loopDisplay); get ("screenInvert", prefs.screenInvert);
    get ("screenSaver", prefs.screenSaver); get ("keyRepeatDelay", prefs.keyRepeatDelay); get ("saveSession", prefs.saveSession);
    get ("autoload", prefs.autoload); get ("showSplash", prefs.showSplash);
    get ("sysTranspose", prefs.sysTranspose); get ("sysSemitone", prefs.sysSemitone); get ("sysFine", prefs.sysFine);
    get ("sysMasterLevel", prefs.sysMasterLevel); get ("midiProgramChange", prefs.midiProgramChange);
    get ("midiMultiSelect", prefs.midiMultiSelect); get ("midiMultiSelectCh", prefs.midiMultiSelectCh);
    get ("extApmCtrl", prefs.extApmCtrl); get ("mergeBtoA", prefs.mergeBtoA);
    get ("rxNotes", prefs.rxNotes); get ("rxBend", prefs.rxBend); get ("rxCtrl", prefs.rxCtrl);
    get ("rxAftertouch", prefs.rxAftertouch); get ("rxProgram", prefs.rxProgram);
    get ("interpolation", prefs.interpolation); get ("polyphony", prefs.polyphony); get ("playKey", prefs.playKey);
    get ("fanSpeed", prefs.fanSpeed); get ("fxOutPair", prefs.fxOutPair); get ("directMonitor", prefs.directMonitor);

    memory.capacityBytes = (size_t) juce::jlimit (32, 1024, settings().getIntValue ("memoryMB", 256)) * 1024 * 1024;
    const auto root = settings().getValue ("hostRoot");
    if (root.isNotEmpty() && juce::File (root).isDirectory()) disk.setHostRootFolder (juce::File (root));
}

void S6000AudioProcessor::savePrefs()
{
    auto& s = settings();
    auto put = [&s] (const char* k, int v) { s.setValue (k, v); };
    put ("noteDisplay", prefs.noteDisplay); put ("loopDisplay", prefs.loopDisplay); put ("screenInvert", prefs.screenInvert);
    put ("screenSaver", prefs.screenSaver); put ("keyRepeatDelay", prefs.keyRepeatDelay); put ("saveSession", prefs.saveSession);
    put ("autoload", prefs.autoload); put ("showSplash", prefs.showSplash);
    put ("sysTranspose", prefs.sysTranspose); put ("sysSemitone", prefs.sysSemitone); put ("sysFine", prefs.sysFine);
    put ("sysMasterLevel", prefs.sysMasterLevel); put ("midiProgramChange", prefs.midiProgramChange);
    put ("midiMultiSelect", prefs.midiMultiSelect); put ("midiMultiSelectCh", prefs.midiMultiSelectCh);
    put ("extApmCtrl", prefs.extApmCtrl); put ("mergeBtoA", prefs.mergeBtoA);
    put ("rxNotes", prefs.rxNotes); put ("rxBend", prefs.rxBend); put ("rxCtrl", prefs.rxCtrl);
    put ("rxAftertouch", prefs.rxAftertouch); put ("rxProgram", prefs.rxProgram);
    put ("interpolation", prefs.interpolation); put ("polyphony", prefs.polyphony); put ("playKey", prefs.playKey);
    put ("fanSpeed", prefs.fanSpeed); put ("fxOutPair", prefs.fxOutPair); put ("directMonitor", prefs.directMonitor);
    s.setValue ("hostRoot", disk.getHostRootFolder().getFullPathName());
}

void S6000AudioProcessor::applyPrefsToEngine()
{
    auto& e = engine.settings;
    e.transpose = prefs.sysTranspose;
    e.semitoneTune = prefs.sysSemitone;
    e.fineTune = prefs.sysFine;
    e.masterLevelDb = prefs.sysMasterLevel;
    e.programChange = prefs.midiProgramChange != 0;
    e.multiSelect = prefs.midiMultiSelect != 0;
    e.multiSelectChannel = prefs.midiMultiSelectCh;
    e.extApmController = prefs.extApmCtrl;
    e.mergeBtoA = prefs.mergeBtoA != 0;
    e.rxNotes = prefs.rxNotes != 0; e.rxPitchbend = prefs.rxBend != 0; e.rxControllers = prefs.rxCtrl != 0;
    e.rxAftertouch = prefs.rxAftertouch != 0; e.rxProgramChange = prefs.rxProgram != 0;
    e.interpolation = prefs.interpolation;
    e.maxPolyphony = prefs.polyphony;
    e.playKey = prefs.playKey;
}

//==============================================================================
void S6000AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int block = juce::jmax (512, samplesPerBlock);
    outBus.setSize (numOutputPairs * 2, block);
    fxBus.setSize (numFxChannels * 2, block);
    recIn.setSize (2, block);
    engine.prepare (sampleRate, block);
    effects.prepare (sampleRate, block);
    recorder.prepare (sampleRate);
    mainGain.reset (sampleRate, 0.03);
    phonesGain.reset (sampleRate, 0.03);
    mainGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue ("mainVolume")->load(), -60.0f));

    if (! autoloadDone)
    {
        autoloadDone = true;
        if (prefs.autoload && memory.samples.isEmpty())
        {
            // AUTOLOAD folder in the root of the S6000 HD (case insensitive)
            for (auto& f : disk.getHostRootFolder().findChildFiles (juce::File::findDirectories, false))
                if (f.getFileName().equalsIgnoreCase ("AUTOLOAD")) { disk.loadHostFolder (f); break; }
        }
    }
}

void S6000AudioProcessor::releaseResources() {}

bool S6000AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& s = layouts.outputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    for (auto& s : layouts.inputBuses)
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo() && s != juce::AudioChannelSet::mono()) return false;
    return true;
}

void S6000AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    if (outBus.getNumSamples() < n)
    {
        outBus.setSize (numOutputPairs * 2, n, false, false, true);
        fxBus.setSize (numFxChannels * 2, n, false, false, true);
        recIn.setSize (2, n, false, false, true);
    }

    // capture the record input before the (shared) channels are cleared
    bool haveInput = false;
    if (getBusCount (true) > 0 && getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        if (in.getNumChannels() > 0)
        {
            recIn.copyFrom (0, 0, in, 0, 0, n);
            recIn.copyFrom (1, 0, in, juce::jmin (1, in.getNumChannels() - 1), 0, n);
            haveInput = true;
        }
    }
    if (! haveInput) { recIn.clear (0, 0, n); recIn.clear (1, 0, n); }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    if (! powerOn.load())
    {
        outPeakL = outPeakR = 0;
        midi.clear();
        return;
    }

    for (int c = 0; c < outBus.getNumChannels(); ++c) outBus.clear (c, 0, n);
    for (int c = 0; c < fxBus.getNumChannels(); ++c) fxBus.clear (c, 0, n);

    engine.auditionGain = juce::Decibels::decibelsToGain (params.getRawParameterValue ("phonesLevel")->load(), -60.0f);

    {
        const juce::SpinLock::ScopedTryLockType tl (memory.lock);
        if (tl.isLocked())
        {
            engine.render (outBus, fxBus, midi, n);
            if (auto* m = memory.getCurrentMulti())
            {
                if (params.getRawParameterValue ("fxBypass")->load() < 0.5f)
                {
                    const int pair = juce::jlimit (0, numOutputPairs - 1, prefs.fxOutPair);
                    effects.process (m->fx, fxBus, outBus.getWritePointer (pair * 2), outBus.getWritePointer (pair * 2 + 1), n);
                }
            }
        }
    }

    // input monitoring (FX IN/OUTS > L/R DIRECT LEVEL)
    if (prefs.directMonitor > 0 && haveInput)
    {
        const float g = (float) prefs.directMonitor * 0.01f;
        outBus.addFrom (0, 0, recIn, 0, 0, n, g);
        outBus.addFrom (1, 0, recIn, 1, 0, n, g);
    }

    // UTILITIES > TEST TONE : 1 kHz at -12 dB
    if (testTone.load())
    {
        const double inc = juce::MathConstants<double>::twoPi * 1000.0 / currentSampleRate;
        float* l = outBus.getWritePointer (0);
        float* r = outBus.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.25f * (float) std::sin (testTonePhase);
            testTonePhase += inc;
            l[i] += v; r[i] += v;
        }
        testTonePhase = std::fmod (testTonePhase, juce::MathConstants<double>::twoPi);
    }

    // MAIN VOLUME affects outputs 1/2 only (as on the hardware)
    mainGain.setTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue ("mainVolume")->load(), -60.0f));
    {
        float* l = outBus.getWritePointer (0);
        float* r = outBus.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float g = mainGain.getNextValue(); l[i] *= g; r[i] *= g; }
    }

    auto mainOut = getBusBuffer (buffer, false, 0);
    for (int pair = 0; pair < numOutputPairs; ++pair)
    {
        const bool busOn = pair > 0 && pair < getBusCount (false) && getBus (false, pair) != nullptr && getBus (false, pair)->isEnabled();
        if (pair == 0 || ! busOn)
        {
            for (int c = 0; c < juce::jmin (2, mainOut.getNumChannels()); ++c)
                mainOut.addFrom (c, 0, outBus, pair * 2 + c, 0, n);
        }
        else
        {
            auto aux = getBusBuffer (buffer, false, pair);
            for (int c = 0; c < juce::jmin (2, aux.getNumChannels()); ++c)
                aux.copyFrom (c, 0, outBus, pair * 2 + c, 0, n);
        }
    }

    // RECORD
    if (recorder.source == Recorder::MainOuts)
        recorder.process (mainOut.getReadPointer (0), mainOut.getReadPointer (juce::jmin (1, mainOut.getNumChannels() - 1)), n, midi);
    else
        recorder.process (recIn.getReadPointer (0), recIn.getReadPointer (1), n, midi);

    outPeakL = juce::jmax (mainOut.getMagnitude (0, 0, n), outPeakL.load() * 0.8f);
    outPeakR = juce::jmax (mainOut.getMagnitude (juce::jmin (1, mainOut.getNumChannels() - 1), 0, n), outPeakR.load() * 0.8f);
    midi.clear();
}

//==============================================================================
void S6000AudioProcessor::retire (juce::ReferenceCountedObject* o)
{
    if (o == nullptr) return;
    const juce::ScopedLock sl (graveyardLock);
    graveyard.add (o);
}

void S6000AudioProcessor::timerCallback()
{
    // MIDI program changes (from the audio thread)
    SamplerEngine::ProgramChangeRequest r;
    bool changed = false;
    while (engine.popProgramChange (r))
    {
        auto* m = memory.getCurrentMulti();
        if (m == nullptr) continue;
        Program::Ptr target;
        for (auto* p : memory.programs) if (p->data.midiProgram == r.program + 1) { target = p; break; }
        if (target == nullptr) continue;
        const juce::SpinLock::ScopedLockType sl (memory.lock);
        for (size_t i = 0; i < m->parts.size(); ++i)
        {
            auto& part = m->parts[i];
            if (part.programName.empty()) continue;
            if (part.midiChannel == r.channel || (prefs.mergeBtoA && part.midiChannel == r.channel + 16))
            {
                part.programName = target->data.name;
                m->resolved[i] = target;
                changed = true;
            }
        }
    }
    juce::ignoreUnused (changed);

    // free objects the audio thread no longer references
    const juce::ScopedLock sl (graveyardLock);
    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointerUnchecked (i)->getReferenceCount() <= 1)
            graveyard.remove (i);
}

//==============================================================================
juce::ValueTree S6000AudioProcessor::saveSession()
{
    juce::ValueTree session ("SESSION");
    juce::ReferenceCountedArray<Sample> samples;
    juce::ReferenceCountedArray<Program> programs;
    juce::ReferenceCountedArray<Multi> multis;
    {
        // only copy pointers under the lock: encoding must not block the audio thread
        const juce::SpinLock::ScopedLockType sl (memory.lock);
        samples = memory.samples; programs = memory.programs; multis = memory.multis;
    }
    session.setProperty ("currentMulti", memory.currentMulti.load(), nullptr);

    juce::FlacAudioFormat flac;
    for (auto* s : samples)
    {
        if (s->flacCacheVersion != s->version.load() || s->flacCache.getSize() == 0)
        {
            s->flacCache.reset();
            auto* mos = new juce::MemoryOutputStream (s->flacCache, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (mos, juce::jlimit (8000.0, 192000.0, s->sampleRate),
                                                                              (unsigned int) s->numChannels(), 24, {}, 0));
            if (w == nullptr) { delete mos; continue; }
            w->writeFromAudioSampleBuffer (s->buffer, 0, s->numFrames());
            w.reset();
            s->flacCacheVersion = s->version.load();
        }
        juce::ValueTree t ("SAMPLE");
        t.setProperty ("name", s->name, nullptr);
        t.setProperty ("rate", s->sampleRate, nullptr);
        t.setProperty ("root", s->rootNote, nullptr);
        t.setProperty ("semi", s->semitone, nullptr);
        t.setProperty ("cents", s->cents, nullptr);
        t.setProperty ("start", (juce::int64) s->start, nullptr);
        t.setProperty ("end", (juce::int64) s->end, nullptr);
        t.setProperty ("loop", s->loopEnabled, nullptr);
        t.setProperty ("ls", (juce::int64) s->loopStart, nullptr);
        t.setProperty ("le", (juce::int64) s->loopEnd, nullptr);
        t.setProperty ("lx", s->loopXfade, nullptr);
        t.setProperty ("ld", s->loopDirection, nullptr);
        t.setProperty ("mode", s->playMode, nullptr);
        t.setProperty ("flac", s->flacCache, nullptr);
        session.appendChild (t, nullptr);
    }
    for (auto* p : programs)
    {
        const auto b = fmt::writeAkp (p->data);
        juce::ValueTree t ("PROGRAM");
        t.setProperty ("name", p->getName(), nullptr);
        t.setProperty ("akp", juce::MemoryBlock (b.data(), b.size()), nullptr);
        session.appendChild (t, nullptr);
    }
    for (auto* m : multis)
    {
        fmt::MultiData md;
        md.name = m->name.toStdString();
        md.parts = m->parts;
        md.fxBlob = m->fx.serialise();
        const auto b = fmt::writeAkm (md);
        juce::ValueTree t ("MULTI");
        t.setProperty ("name", m->name, nullptr);
        t.setProperty ("akm", juce::MemoryBlock (b.data(), b.size()), nullptr);
        session.appendChild (t, nullptr);
    }
    return session;
}

void S6000AudioProcessor::restoreSession (const juce::ValueTree& session)
{
    juce::ReferenceCountedArray<Sample> samples;
    juce::ReferenceCountedArray<Program> programs;
    juce::ReferenceCountedArray<Multi> multis;

    for (auto t : session)
    {
        if (t.hasType ("SAMPLE"))
        {
            auto* mb = t.getProperty ("flac").getBinaryData();
            if (mb == nullptr) continue;
            std::unique_ptr<juce::AudioFormatReader> rd (disk.formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (*mb, false)));
            if (rd == nullptr) continue;
            Sample::Ptr s = new Sample();
            s->name = t["name"].toString();
            s->buffer.setSize ((int) juce::jlimit (1u, 2u, rd->numChannels), (int) rd->lengthInSamples);
            rd->read (&s->buffer, 0, (int) rd->lengthInSamples, 0, true, true);
            s->sampleRate = (double) t.getProperty ("rate", rd->sampleRate);
            s->rootNote = t["root"]; s->semitone = t["semi"]; s->cents = t["cents"];
            s->start = (juce::int64) t["start"]; s->end = (juce::int64) t["end"];
            s->loopEnabled = t["loop"]; s->loopStart = (juce::int64) t["ls"]; s->loopEnd = (juce::int64) t["le"];
            s->loopXfade = t["lx"]; s->loopDirection = t["ld"]; s->playMode = t["mode"];
            s->sanitiseMarkers();
            s->flacCache = *mb;
            s->flacCacheVersion = s->version.load();
            samples.add (s);
        }
        else if (t.hasType ("PROGRAM"))
        {
            auto* mb = t.getProperty ("akp").getBinaryData();
            if (mb == nullptr) continue;
            const auto* d = static_cast<const uint8_t*> (mb->getData());
            fmt::ProgramData pd; std::string e;
            if (! fmt::parseAkp (fmt::Bytes (d, d + mb->getSize()), pd, e)) continue;
            pd.name = t["name"].toString().toStdString();
            Program::Ptr p = new Program();
            p->data = pd;
            programs.add (p);
        }
        else if (t.hasType ("MULTI"))
        {
            auto* mb = t.getProperty ("akm").getBinaryData();
            if (mb == nullptr) continue;
            const auto* d = static_cast<const uint8_t*> (mb->getData());
            fmt::MultiData md; std::string e;
            if (! fmt::parseAkm (fmt::Bytes (d, d + mb->getSize()), md, e)) continue;
            Multi::Ptr m = new Multi();
            m->name = t["name"].toString();
            for (size_t i = 0; i < md.parts.size() && i < m->parts.size(); ++i) m->parts[i] = md.parts[i];
            m->fx.deserialise (md.fxBlob);
            multis.add (m);
        }
    }

    const juce::SpinLock::ScopedLockType sl (memory.lock);
    for (auto* s : memory.samples) retire (s);
    for (auto* p : memory.programs) retire (p);
    for (auto* m : memory.multis) retire (m);
    memory.samples = samples;
    memory.programs = programs;
    memory.multis = multis;
    if (memory.multis.isEmpty()) { Multi::Ptr m = new Multi(); m->name = "NEW MULTI 1"; memory.multis.add (m); }
    memory.currentMulti = juce::jlimit (0, memory.multis.size() - 1, (int) session.getProperty ("currentMulti", 0));
    memory.resolveAll();
}

void S6000AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("S6000STATE");
    root.setProperty ("os", s6k::osVersion, nullptr);
    root.appendChild (params.copyState(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);
    if (prefs.saveSession) root.appendChild (saveSession(), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    root.writeToStream (mos);
}

void S6000AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! root.hasType ("S6000STATE")) return;
    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);
    auto ui = root.getChildWithName ("UI");
    if (ui.isValid()) uiState = ui.createCopy();
    auto session = root.getChildWithName ("SESSION");
    if (session.isValid())
    {
        autoloadDone = true;
        restoreSession (session);
        juce::MessageManager::callAsync ([this, alive = std::weak_ptr<int> (aliveToken)]
        {
            if (alive.lock() != nullptr && onSessionRestored) onSessionRestored();
        });
    }
}

//==============================================================================
juce::AudioProcessorEditor* S6000AudioProcessor::createEditor()
{
    return new S6000AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new S6000AudioProcessor();
}
