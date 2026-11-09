/*
  ==============================================================================
   S6000 Emulation - Disk manager
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "DiskManager.h"

namespace s6k
{
using disk::VNode;
using disk::VNodePtr;

//==============================================================================
DiskManager::DiskManager (Memory& m) : juce::Thread ("S6000 Disk"), mem (m)
{
    formats.registerBasicFormats();
    hostRoot = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("S6000");
    hostRoot.createDirectory();
    refreshDisks();
    startThread (juce::Thread::Priority::background);
}

DiskManager::~DiskManager()
{
    cancelRequested = true;
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

disk::InflateFn DiskManager::getInflater()
{
    return [] (const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) -> bool
    {
        auto* in = new juce::MemoryInputStream (src, srcLen, false);
        juce::GZIPDecompressorInputStream gz (in, true, juce::GZIPDecompressorInputStream::zlibFormat, (juce::int64) dstLen);
        size_t done = 0;
        while (done < dstLen)
        {
            const int r = gz.read (dst + done, (int) juce::jmin<size_t> (dstLen - done, 1 << 20));
            if (r <= 0) break;
            done += (size_t) r;
        }
        return done == dstLen;
    };
}

juce::String DiskManager::safeFileName (const juce::String& n)
{
    auto s = juce::File::createLegalFileName (n.trim());
    return s.isEmpty() ? juce::String ("UNTITLED") : s;
}

//==============================================================================
VNodePtr DiskManager::makeHostNode (const juce::File& f)
{
    auto n = std::make_shared<VNode>();
    n->name = f.getFileName().toStdString();
    n->hostPath = f.getFullPathName().toStdString();
    n->dateTime = f.getLastModificationTime().formatted ("%d/%m/%y %H:%M").toStdString();

    if (f.isDirectory())
    {
        n->type = VNode::Type::Folder;
        n->populate = [this, f] (VNode& node)
        {
            auto dirs = f.findChildFiles (juce::File::findDirectories, false);
            auto files = f.findChildFiles (juce::File::findFiles, false);
            dirs.sort(); files.sort();
            for (auto& d : dirs)
                if (! d.isHidden()) node.addChild (makeHostNode (d));
            for (auto& x : files)
            {
                if (x.isHidden()) continue;
                auto c = makeHostNode (x);
                if (c->type != VNode::Type::Other) node.addChild (c);
            }
        };
    }
    else
    {
        disk::classifyByName (*n);
        n->size = (uint64_t) f.getSize();
        n->readContent = [f]()
        {
            juce::MemoryBlock mb;
            f.loadFileAsData (mb);
            const auto* p = static_cast<const uint8_t*> (mb.getData());
            return fmt::Bytes (p, p + mb.getSize());
        };
    }
    return n;
}

void DiskManager::refreshDisks()
{
    std::vector<Disk> keepImages;
    for (auto& d : disks) if (! d.writable && d.name != "COMPUTER") keepImages.push_back (d);
    disks.clear();

    {
        Disk d;
        d.name = "S6000 HD";
        d.format = "HOST FOLDER";
        d.root = makeHostNode (hostRoot);
        d.root->name = "S6000 HD";
        d.root->type = VNode::Type::Disk;
        d.writable = true;
        d.hostFolder = hostRoot;
        disks.push_back (d);
    }
    {
        Disk d;
        d.name = "COMPUTER";
        d.format = "HOST FILE SYSTEM";
        d.root = std::make_shared<VNode>();
        d.root->name = "COMPUTER";
        d.root->type = VNode::Type::Disk;
        d.root->populate = [this] (VNode& node)
        {
            using SL = juce::File::SpecialLocationType;
            for (auto loc : { SL::userHomeDirectory, SL::userDesktopDirectory, SL::userDocumentsDirectory, SL::userMusicDirectory })
            {
                auto f = juce::File::getSpecialLocation (loc);
                if (f.isDirectory())
                {
                    auto c = makeHostNode (f);
                    node.addChild (c);
                }
            }
            auto dl = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
            if (dl.isDirectory()) node.addChild (makeHostNode (dl));
            juce::Array<juce::File> roots;
           #if JUCE_MAC
            roots = juce::File ("/Volumes").findChildFiles (juce::File::findDirectories, false);
           #else
            juce::File::findFileSystemRoots (roots);
           #endif
            for (auto& r : roots)
            {
                auto c = makeHostNode (r);
                if (c->name.empty()) c->name = r.getFullPathName().toStdString();
                node.addChild (c);
            }
        };
        d.writable = false;
        disks.push_back (d);
    }
    for (auto& d : keepImages) disks.push_back (d);
    currentDisk = juce::jlimit (0, (int) disks.size() - 1, currentDisk);
    path.clear();
    path.push_back (disks[(size_t) currentDisk].root);
}

void DiskManager::setHostRootFolder (const juce::File& f)
{
    if (! f.isDirectory()) return;
    hostRoot = f;
    refreshDisks();
}

void DiskManager::selectDisk (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) disks.size())) return;
    currentDisk = index;
    path.clear();
    path.push_back (disks[(size_t) index].root);
}

bool DiskManager::unmount (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) disks.size())) return false;
    if (disks[(size_t) index].writable || disks[(size_t) index].name == "COMPUTER") return false;
    disks.erase (disks.begin() + index);
    if (currentDisk >= (int) disks.size() || currentDisk == index) selectDisk (0);
    else if (currentDisk > index) --currentDisk;
    return true;
}

std::vector<VNodePtr> DiskManager::getVisibleItems (bool includeOther) const
{
    std::vector<VNodePtr> out;
    auto f = getCurrentFolder();
    if (f == nullptr) return out;
    f->ensurePopulated();
    for (auto& c : f->children)
        if (includeOther || c->type != VNode::Type::Other) out.push_back (c);
    return out;
}

void DiskManager::openFolder (VNodePtr f)
{
    if (f == nullptr) return;
    if (f->kind == VNode::Kind::DiskImage)
    {
        mountImage (juce::File (juce::String (f->hostPath)));
        return;
    }
    if (f->isContainer()) path.push_back (f);
}

bool DiskManager::closeFolder()
{
    if (path.size() <= 1) return false;
    path.pop_back();
    return true;
}

//==============================================================================
void DiskManager::addJob (std::function<void()> j)
{
    {
        const juce::ScopedLock sl (jobLock);
        jobs.push_back (std::move (j));
    }
    cancelRequested = false;
    busy = true;
    notify();
}

void DiskManager::run()
{
    while (! threadShouldExit())
    {
        std::function<void()> job;
        {
            const juce::ScopedLock sl (jobLock);
            if (! jobs.empty()) { job = std::move (jobs.front()); jobs.erase (jobs.begin()); }
        }
        if (job)
        {
            busy = true;
            progress = 0;
            job();
            bool more;
            { const juce::ScopedLock sl (jobLock); more = ! jobs.empty(); }
            if (! more)
            {
                busy = false;
                juce::MessageManager::callAsync ([this, w = std::weak_ptr<int> (aliveToken)] { if (w.lock() != nullptr && onJobFinished) onJobFinished(); });
            }
            continue;
        }
        wait (500);
    }
}

void DiskManager::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

void DiskManager::fail (const juce::String& e)
{
    {
        const juce::ScopedLock sl (statusLock);
        lastError = e;
    }
    juce::MessageManager::callAsync ([this, e, w = std::weak_ptr<int> (aliveToken)] { if (w.lock() != nullptr && onError) onError (e); });
}

//==============================================================================
void DiskManager::mountImage (const juce::File& f)
{
    addJob ([this, f]
    {
        setStatus ("MOUNTING " + f.getFileName().toUpperCase());
        auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("S6000");
        temp.createDirectory();
        std::string err;
        auto mounts = disk::mountImageFile (f.getFullPathName().toStdString(), getInflater(), err, temp.getFullPathName().toStdString());
        if (mounts.empty()) { fail (juce::String (err.empty() ? "UNKNOWN FORMAT" : err)); return; }
        juce::MessageManager::callAsync ([this, mounts, w = std::weak_ptr<int> (aliveToken)]
        {
            if (w.lock() == nullptr) return;
            int first = -1;
            for (auto& m : mounts)
            {
                Disk d;
                d.name = juce::String (m.displayName);
                d.format = juce::String (m.formatName);
                d.root = m.root;
                d.writable = false;
                // replace an existing mount with the same name
                bool replaced = false;
                for (size_t i = 0; i < disks.size(); ++i)
                    if (disks[i].name == d.name && ! disks[i].writable) { disks[i] = d; replaced = true; if (first < 0) first = (int) i; }
                if (! replaced) { disks.push_back (d); if (first < 0) first = (int) disks.size() - 1; }
            }
            if (first >= 0) selectDisk (first);
        });
    });
}

VNodePtr DiskManager::findSibling (VNode& from, const juce::String& name, VNode::Type t)
{
    auto parent = from.parent.lock();
    if (parent == nullptr) return nullptr;
    parent->ensurePopulated();
    const auto want = name.trim();
    for (auto& c : parent->children)
    {
        if (c->type != t) continue;
        juce::String cn (c->name);
        if (cn.trim().equalsIgnoreCase (want) || cn.upToLastOccurrenceOf (".", false, false).trim().equalsIgnoreCase (want))
            return c;
    }
    return nullptr;
}

VNodePtr DiskManager::findSampleAnywhere (VNode& from, const juce::String& name)
{
    // 1) same folder / volume (normal case)
    if (auto s = findSibling (from, name, VNode::Type::Sample)) return s;

    // 2) anywhere on the same disk / partition (libraries sometimes share samples between volumes)
    auto node = from.parent.lock();
    if (node == nullptr) return nullptr;
    while (auto up = node->parent.lock()) node = up;

    const auto want = name.trim();
    std::vector<VNodePtr> stack { node };
    int visited = 0;
    while (! stack.empty() && visited < 5000 && ! cancelRequested)
    {
        auto cur = stack.back();
        stack.pop_back();
        ++visited;
        cur->ensurePopulated();
        for (auto& c : cur->children)
        {
            if (c->type == VNode::Type::Sample)
            {
                juce::String cn (c->name);
                if (cn.trim().equalsIgnoreCase (want) || cn.upToLastOccurrenceOf (".", false, false).trim().equalsIgnoreCase (want))
                    return c;
            }
            else if (c->isContainer() && c->kind != VNode::Kind::DiskImage)
                stack.push_back (c);
        }
    }
    return nullptr;
}

Sample::Ptr DiskManager::decodeAudioFile (const fmt::Bytes& data, const juce::String& name, juce::String& err)
{
    auto stream = std::make_unique<juce::MemoryInputStream> (data.data(), data.size(), false);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::move (stream)));
    if (reader == nullptr) { err = "UNSUPPORTED AUDIO FILE"; return nullptr; }
    if (reader->lengthInSamples > (juce::int64) 0x7FFFFFF) { err = "SAMPLE TOO LONG"; return nullptr; }

    Sample::Ptr s = new Sample();
    s->name = name.substring (0, 20);
    const int ch = juce::jlimit (1, 2, (int) reader->numChannels);
    const int len = (int) reader->lengthInSamples;
    s->buffer.setSize (ch, juce::jmax (2, len));
    s->buffer.clear();
    reader->read (&s->buffer, 0, len, 0, true, ch > 1);
    s->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    s->start = 0;
    s->end = s->numFrames();

    const auto& md = reader->metadataValues;
    if (md.containsKey ("MidiUnityNote")) s->rootNote = juce::jlimit (0, 127, md["MidiUnityNote"].getIntValue());
    if (md["NumSampleLoops"].getIntValue() > 0)
    {
        s->loopStart = md["Loop0Start"].getLargeIntValue();
        s->loopEnd = md["Loop0End"].getLargeIntValue() + 1;
        s->loopEnabled = s->loopEnd > s->loopStart + 1;
        if (md["Loop0Type"].getIntValue() == 1) s->loopDirection = 1;
        s->playMode = fmt::PlayLoopInRelease;
    }
    else
    {
        s->loopStart = 0; s->loopEnd = s->numFrames();
        s->playMode = fmt::PlayNoLoop;
    }
    s->sanitiseMarkers();
    return s;
}

Sample::Ptr DiskManager::decodeSample (VNode& n, juce::String& err)
{
    if (! n.readContent) { err = "CANNOT READ"; return nullptr; }
    const auto data = n.readContent();
    if (data.empty()) { err = "READ ERROR"; return nullptr; }

    if (n.kind == VNode::Kind::S1000Sample || n.kind == VNode::Kind::S3000Sample)
    {
        fmt::SampleData sd;
        std::string e;
        if (! fmt::parseS1000Sample (data, n.kind == VNode::Kind::S3000Sample, sd, e)) { err = juce::String (e); return nullptr; }
        Sample::Ptr s = new Sample();
        s->name = juce::String (sd.name).trim();
        s->buffer.setSize (1, (int) sd.ch[0].size());
        s->buffer.copyFrom (0, 0, sd.ch[0].data(), (int) sd.ch[0].size());
        s->sampleRate = sd.sampleRate;
        s->rootNote = sd.rootNote; s->semitone = sd.semitone; s->cents = sd.cents;
        s->start = sd.start; s->end = sd.end;
        s->loopEnabled = sd.loopEnabled; s->loopStart = sd.loopStart; s->loopEnd = sd.loopEnd;
        s->playMode = sd.playMode;
        s->sanitiseMarkers();
        return s;
    }

    juce::String nm = juce::String (n.name).upToLastOccurrenceOf (".", false, false);
    if (nm.isEmpty()) nm = juce::String (n.name);
    return decodeAudioFile (data, nm, err);
}

Program::Ptr DiskManager::programFromData (const fmt::ProgramData& d)
{
    Program::Ptr p = new Program();
    p->data = d;
    if (p->data.keygroups.empty()) p->data.keygroups.push_back (fmt::KeygroupData());
    return p;
}

void DiskManager::loadSampleInto (VNode& n, Batch& b)
{
    if (cancelRequested) return;
    juce::String err;
    setStatus ("LOADING: " + juce::String (n.name).toUpperCase());
    auto s = decodeSample (n, err);
    if (s == nullptr) { fail (err + " : " + juce::String (n.name)); return; }

    size_t pending = mem.bytesUsed();
    for (auto* x : b.samples) pending += x->bytesUsed();
    if (pending + s->bytesUsed() > mem.capacityBytes) { fail ("MEMORY FULL"); cancelRequested = true; return; }

    for (int i = 0; i < b.samples.size(); ++i)
        if (b.samples[i]->name.equalsIgnoreCase (s->name)) { b.samples.set (i, s); return; }
    b.samples.add (s);
}

void DiskManager::loadProgramInto (VNode& n, bool withSamples, Batch& b)
{
    if (cancelRequested || ! n.readContent) return;
    setStatus ("LOADING: " + juce::String (n.name).toUpperCase());
    const auto data = n.readContent();
    fmt::ProgramData pd;
    std::string e;
    bool ok;
    if (n.kind == VNode::Kind::Akp)
    {
        ok = fmt::parseAkp (data, pd, e);
        pd.name = juce::String (n.name).upToLastOccurrenceOf (".", false, false).toStdString();
    }
    else
        ok = fmt::parseS1000Program (data, pd, e, n.kind == VNode::Kind::S3000Program ? 1 : 0);
    if (! ok) { fail (juce::String (e) + " : " + juce::String (n.name)); return; }

    auto p = programFromData (pd);
    for (int i = 0; i < b.programs.size(); ++i)
        if (b.programs[i]->getName().equalsIgnoreCase (p->getName())) { b.programs.remove (i); break; }
    b.programs.add (p);

    if (! withSamples) return;
    juce::StringArray wanted;
    for (auto& kg : pd.keygroups)
        for (auto& z : kg.zones)
            if (! z.sampleName.empty()) wanted.addIfNotAlreadyThere (juce::String (z.sampleName).trim(), true);

    juce::StringArray missing;
    for (auto& w : wanted)
    {
        if (cancelRequested) return;
        bool have = false;
        for (auto* s : b.samples) if (s->name.trim().equalsIgnoreCase (w)) have = true;
        if (have) continue;
        setStatus ("SEARCHING: " + w);
        if (auto sn = findSampleAnywhere (n, w))
            loadSampleInto (*sn, b);
        else if (mem.findSample (w) == nullptr)
            missing.add (w);
    }
    // Akai S1000/S3000: a zone whose sample is not on the disk is a leftover placeholder -> remove it
    if (n.kind != VNode::Kind::Akp && ! missing.isEmpty())
    {
        juce::StringArray stillMissing;
        for (auto& kgd : p->data.keygroups)
            for (int z = 0; z < 4; ++z)
            {
                const juce::String nm = juce::String (kgd.zones[z].sampleName).trim();
                if (nm.isEmpty() || ! missing.contains (nm, true)) continue;
                bool usedElsewhere = false;
                for (int o = 0; o < 4; ++o) if (o != z && ! kgd.zones[o].sampleName.empty() && ! missing.contains (juce::String (kgd.zones[o].sampleName).trim(), true)) usedElsewhere = true;
                if (z > 0 || usedElsewhere) kgd.zones[z] = fmt::ZoneData();
                else stillMissing.addIfNotAlreadyThere (nm, true);
            }
        missing = stillMissing;
    }
    if (! missing.isEmpty())
        fail (juce::String (pd.name) + ": " + juce::String (missing.size()) + " SAMPLE(S) NOT FOUND\n"
              + missing.joinIntoString (", ").substring (0, 120));
}

void DiskManager::loadMultiInto (VNode& n, bool withChildren, Batch& b)
{
    if (cancelRequested || ! n.readContent) return;
    setStatus ("LOADING: " + juce::String (n.name).toUpperCase());
    fmt::MultiData md;
    std::string e;
    if (! fmt::parseAkm (n.readContent(), md, e)) { fail (juce::String (e)); return; }

    Multi::Ptr m = new Multi();
    m->name = juce::String (n.name).upToLastOccurrenceOf (".", false, false).substring (0, 20);
    for (size_t i = 0; i < md.parts.size() && i < m->parts.size(); ++i) m->parts[i] = md.parts[i];
    m->fx.deserialise (md.fxBlob);
    b.multis.add (m);

    if (! withChildren) return;
    for (auto& part : md.parts)
    {
        if (cancelRequested) return;
        const juce::String pn = juce::String (part.programName).trim();
        bool have = false;
        for (auto* p : b.programs) if (p->getName().equalsIgnoreCase (pn)) have = true;
        if (have) continue;
        if (auto node = findSibling (n, pn, VNode::Type::Program)) loadProgramInto (*node, true, b);
    }
}

void DiskManager::loadFolderInto (VNode& folder, const LoadOptions& o, Batch& b, int depth)
{
    if (depth > 16 || cancelRequested) return;
    folder.ensurePopulated();
    const int total = (int) folder.children.size();
    int idx = 0;
    for (auto& c : folder.children)
    {
        if (cancelRequested) return;
        if (depth == 0) progress = total > 0 ? (100 * idx++) / total : 0;
        if (c->isContainer()) continue;
        if (c->type == VNode::Type::Multi && o.multis) loadMultiInto (*c, false, b);
        else if (c->type == VNode::Type::Program && o.programs) loadProgramInto (*c, false, b);
        else if (c->type == VNode::Type::Sample && o.samples) loadSampleInto (*c, b);
    }
    for (auto& c : folder.children)
        if (c->isContainer() && c->kind != VNode::Kind::DiskImage) loadFolderInto (*c, o, b, depth + 1);
}

void DiskManager::commit (Batch& b)
{
    auto shared = std::make_shared<Batch> (std::move (b));
    juce::MessageManager::callAsync ([this, shared, w = std::weak_ptr<int> (aliveToken)]
    {
        if (w.lock() == nullptr) return;
        auto& bt = *shared;
        bool autoAssign = false;
        {
            const juce::SpinLock::ScopedLockType sl (mem.lock);
            for (auto* s : bt.samples)
            {
                bool replaced = false;
                for (int i = 0; i < mem.samples.size(); ++i)
                    if (mem.samples.getObjectPointer (i)->name.equalsIgnoreCase (s->name)) { mem.samples.set (i, s); replaced = true; break; }
                if (! replaced) mem.samples.add (s);
            }
            for (auto* p : bt.programs)
            {
                bool replaced = false;
                for (int i = 0; i < mem.programs.size(); ++i)
                    if (mem.programs.getObjectPointer (i)->getName().equalsIgnoreCase (p->getName())) { mem.programs.set (i, p); replaced = true; break; }
                if (! replaced) mem.programs.add (p);
            }
            if (bt.multis.size() > 0)
            {
                // drop the untouched default multi
                if (mem.multis.size() == 1 && mem.multis.getObjectPointer (0)->numUsedParts() == 0) mem.multis.clear();
                for (auto* m : bt.multis)
                {
                    bool replaced = false;
                    for (int i = 0; i < mem.multis.size(); ++i)
                        if (mem.multis.getObjectPointer (i)->name.equalsIgnoreCase (m->name)) { mem.multis.set (i, m); replaced = true; break; }
                    if (! replaced) mem.multis.add (m);
                }
                mem.currentMulti = mem.multis.indexOf (bt.multis.getFirst().get());
            }
            else if (bt.programs.size() > 0)
                if (auto* cm = mem.getCurrentMulti())
                    if (cm->numUsedParts() == 0)
                    {
                        cm->parts[0].programName = bt.programs.getFirst()->data.name;
                        autoAssign = true;
                    }
            mem.resolveAll();
        }
        juce::ignoreUnused (autoAssign);
        if (onJobFinished) onJobFinished();
    });
}

void DiskManager::loadItem (VNodePtr item, LoadOptions o)
{
    if (item == nullptr) return;
    addJob ([this, item, o]
    {
        Batch b;
        auto parent = item->parent.lock();
        if (o.allInFolder && parent != nullptr)
        {
            parent->ensurePopulated();
            for (auto& c : parent->children)
            {
                if (cancelRequested) break;
                if (c->type != item->type) continue;
                if (c->type == VNode::Type::Multi) loadMultiInto (*c, o.withChildren, b);
                else if (c->type == VNode::Type::Program) loadProgramInto (*c, o.withChildren, b);
                else if (c->type == VNode::Type::Sample) loadSampleInto (*c, b);
            }
        }
        else if (item->type == VNode::Type::Multi) loadMultiInto (*item, o.withChildren, b);
        else if (item->type == VNode::Type::Program) loadProgramInto (*item, o.withChildren, b);
        else if (item->type == VNode::Type::Sample) loadSampleInto (*item, b);
        setStatus ({});
        commit (b);
    });
}

void DiskManager::loadFolder (VNodePtr folder, LoadOptions o)
{
    if (folder == nullptr) return;
    addJob ([this, folder, o]
    {
        Batch b;
        loadFolderInto (*folder, o, b, 0);
        setStatus ({});
        commit (b);
    });
}

void DiskManager::loadHostFolder (const juce::File& folder)
{
    if (folder.isDirectory()) loadFolder (makeHostNode (folder), LoadOptions());
}

void DiskManager::loadHostFile (const juce::File& file)
{
    if (! file.existsAsFile()) return;
    auto parent = makeHostNode (file.getParentDirectory());
    parent->ensurePopulated();
    for (auto& c : parent->children)
        if (juce::String (c->hostPath) == file.getFullPathName())
        {
            if (c->kind == VNode::Kind::DiskImage) mountImage (file);
            else if (c->type != VNode::Type::Other) loadItem (c, LoadOptions());
            return;
        }
}

void DiskManager::auditionFromDisk (VNodePtr item, std::function<void (Sample::Ptr)> done)
{
    if (item == nullptr || item->type != VNode::Type::Sample) return;
    addJob ([this, item, done]
    {
        juce::String err;
        auto s = decodeSample (*item, err);
        juce::MessageManager::callAsync ([s, done] { if (done) done (s); });
    });
}

//==============================================================================
bool DiskManager::saveSample (const Sample& s, const juce::File& folder, juce::String& err)
{
    folder.createDirectory();
    auto f = folder.getChildFile (safeFileName (s.name) + ".WAV");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) { err = "CANNOT WRITE " + f.getFileName(); return false; }

    juce::StringPairArray md;
    md.set ("MidiUnityNote", juce::String (s.rootNote));
    md.set ("MidiPitchFraction", "0");
    md.set ("Manufacturer", "0");
    md.set ("Product", "0");
    md.set ("SamplePeriod", juce::String ((int) (1.0e9 / s.sampleRate)));
    md.set ("SmpteFormat", "0");
    md.set ("SmpteOffset", "0");
    md.set ("NumSampleLoops", s.loopEnabled ? "1" : "0");
    md.set ("SamplerData", "0");
    if (s.loopEnabled)
    {
        md.set ("Loop0Identifier", "0");
        md.set ("Loop0Type", juce::String (s.loopDirection == 1 ? 1 : 0));
        md.set ("Loop0Start", juce::String (s.loopStart));
        md.set ("Loop0End", juce::String (juce::jmax<juce::int64> (s.loopStart, s.loopEnd - 1)));
        md.set ("Loop0Fraction", "0");
        md.set ("Loop0PlayCount", "0");
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), s.sampleRate, (unsigned int) s.numChannels(), 16, md, 0));
    if (w == nullptr) { err = "WAV WRITER ERROR"; return false; }
    os.release();
    w->writeFromAudioSampleBuffer (s.buffer, 0, s.numFrames());
    return true;
}

bool DiskManager::saveProgram (const Program& p, const juce::File& folder, bool withSamples, juce::String& err)
{
    folder.createDirectory();
    const auto bytes = fmt::writeAkp (p.data);
    auto f = folder.getChildFile (safeFileName (p.getName()) + ".AKP");
    if (! f.replaceWithData (bytes.data(), bytes.size())) { err = "CANNOT WRITE " + f.getFileName(); return false; }
    if (withSamples)
        for (auto& row : p.resolved)
            for (auto& s : row)
                if (s != nullptr && ! folder.getChildFile (safeFileName (s->name) + ".WAV").existsAsFile())
                    if (! saveSample (*s, folder, err)) return false;
    return true;
}

bool DiskManager::saveMulti (const Multi& m, const juce::File& folder, bool withPrograms, juce::String& err)
{
    folder.createDirectory();
    fmt::MultiData md;
    md.name = m.name.toStdString();
    for (auto& p : m.parts) if (! p.programName.empty()) md.parts.push_back (p);
    md.fxBlob = m.fx.serialise();
    const auto bytes = fmt::writeAkm (md);
    auto f = folder.getChildFile (safeFileName (m.name) + ".AKM");
    if (! f.replaceWithData (bytes.data(), bytes.size())) { err = "CANNOT WRITE " + f.getFileName(); return false; }
    if (withPrograms)
        for (auto& p : m.resolved)
            if (p != nullptr && ! saveProgram (*p, folder, true, err)) return false;
    return true;
}

bool DiskManager::saveAll (const juce::File& folder, juce::String& err)
{
    for (auto* s : mem.samples) if (! saveSample (*s, folder, err)) return false;
    for (auto* p : mem.programs) if (! saveProgram (*p, folder, false, err)) return false;
    for (auto* m : mem.multis) if (! saveMulti (*m, folder, false, err)) return false;
    return true;
}

} // namespace s6k
