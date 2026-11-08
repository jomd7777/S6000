/*
  ==============================================================================
   S6000 Emulation - Disk manager (JUCE side)
     * DISK LIST : host disk (your computer) + every mounted image partition
     * mounting  : .iso .img .dmg .bin .cdr .hds .toast ... in a background thread
     * LOAD      : multis / programs / samples / folders (S1000, S3000, S5000, S6000)
     * SAVE      : .AKM / .AKP / .WAV (with loop + root note in the smpl chunk)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#pragma once
#include "Model.h"
#include "../Disk/DiskDevice.h"

namespace s6k
{
class DiskManager : private juce::Thread
{
public:
    explicit DiskManager (Memory& m);
    ~DiskManager() override;

    //==========================================================================
    struct Disk
    {
        juce::String name, format;
        disk::VNodePtr root;
        bool writable = false;
        juce::File hostFolder;     // for writable host disks
    };

    const std::vector<Disk>& getDisks() const { return disks; }
    int getCurrentDiskIndex() const { return currentDisk; }
    void selectDisk (int index);
    void refreshDisks();
    bool unmount (int index);

    /** Current folder stack for the LOAD browser. back() is the open folder. */
    std::vector<disk::VNodePtr>& getPath() { return path; }
    disk::VNodePtr getCurrentFolder() const { return path.empty() ? nullptr : path.back(); }
    std::vector<disk::VNodePtr> getVisibleItems (bool includeOther) const;
    void openFolder (disk::VNodePtr f);
    bool closeFolder();

    void setHostRootFolder (const juce::File& f);
    juce::File getHostRootFolder() const { return hostRoot; }

    //==========================================================================
    // background jobs
    struct LoadOptions
    {
        bool multis = true, programs = true, samples = true;
        bool allInFolder = false;
        bool withChildren = true;    // multi -> programs -> samples
    };

    void mountImage (const juce::File& f);
    void loadItem (disk::VNodePtr item, LoadOptions o);
    void loadFolder (disk::VNodePtr folder, LoadOptions o);
    void loadHostFolder (const juce::File& folder);   // AUTOLOAD
    void loadHostFile (const juce::File& file);       // drag & drop
    void auditionFromDisk (disk::VNodePtr item, std::function<void (Sample::Ptr)> done);
    void cancel() { cancelRequested = true; }

    bool isBusy() const { return busy.load(); }
    juce::String getStatus() const { const juce::ScopedLock sl (statusLock); return status; }
    juce::String getLastError() const { const juce::ScopedLock sl (statusLock); return lastError; }
    int getProgressPercent() const { return progress.load(); }

    std::function<void()> onJobFinished;     // called on the message thread
    std::function<void (const juce::String&)> onError;

    //==========================================================================
    // decoding helpers (thread safe)
    Sample::Ptr decodeSample (disk::VNode& n, juce::String& err);
    Sample::Ptr decodeAudioFile (const fmt::Bytes& data, const juce::String& name, juce::String& err);

    // saving (message thread)
    bool saveSample (const Sample& s, const juce::File& folder, juce::String& err);
    bool saveProgram (const Program& p, const juce::File& folder, bool withSamples, juce::String& err);
    bool saveMulti (const Multi& m, const juce::File& folder, bool withPrograms, juce::String& err);
    bool saveAll (const juce::File& folder, juce::String& err);

    static juce::String safeFileName (const juce::String& n);
    static disk::InflateFn getInflater();

    juce::AudioFormatManager formats;

private:
    Memory& mem;
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0);
    std::vector<Disk> disks;
    std::vector<disk::VNodePtr> path;
    int currentDisk = 0;
    juce::File hostRoot;

    juce::CriticalSection jobLock, statusLock;
    std::vector<std::function<void()>> jobs;
    std::atomic<bool> busy { false }, cancelRequested { false };
    std::atomic<int> progress { 0 };
    juce::String status, lastError;

    void run() override;
    void addJob (std::function<void()> j);
    void setStatus (const juce::String& s);
    void fail (const juce::String& e);

    disk::VNodePtr makeHostNode (const juce::File& f);
    disk::VNodePtr findSibling (disk::VNode& from, const juce::String& name, disk::VNode::Type t);
    disk::VNodePtr findSampleAnywhere (disk::VNode& from, const juce::String& name);

    struct Batch
    {
        juce::ReferenceCountedArray<Sample> samples;
        juce::ReferenceCountedArray<Program> programs;
        juce::ReferenceCountedArray<Multi> multis;
    };
    void loadProgramInto (disk::VNode& n, bool withSamples, Batch& b);
    void loadMultiInto (disk::VNode& n, bool withChildren, Batch& b);
    void loadSampleInto (disk::VNode& n, Batch& b);
    void loadFolderInto (disk::VNode& folder, const LoadOptions& o, Batch& b, int depth);
    void commit (Batch& b);
    static Program::Ptr programFromData (const fmt::ProgramData& d);
};

} // namespace s6k
