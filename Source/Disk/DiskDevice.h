/*
  ==============================================================================
   S6000 Emulation  -  Disk layer (pure C++17, no JUCE dependency)
   (c) dominkHop  -  github.com/jomd7777/S6000

   Block devices + virtual file system used by the LOAD / SAVE pages.
   Supports:  host folders (via JUCE wrapper), CD-ROM .iso, raw 2352 .bin/.img,
              FAT12/16/32 (S5000/S6000 DOS disks, MBR partitions),
              Akai S1000/S3000 hard-disk / CD-ROM partitions and floppies,
              Apple .dmg (UDIF: raw / zero / zlib chunks).
  ==============================================================================
*/
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace s6k::disk
{
//==============================================================================
// little/big endian helpers
inline uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint32_t le24 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16); }
inline uint16_t be16 (const uint8_t* p) { return (uint16_t) ((p[0] << 8) | p[1]); }
inline uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
inline uint64_t be64 (const uint8_t* p) { return ((uint64_t) be32 (p) << 32) | be32 (p + 4); }

using Bytes = std::vector<uint8_t>;

//==============================================================================
/** Random-access, thread-safe read-only device. */
class Device
{
public:
    virtual ~Device() = default;
    virtual uint64_t size() const = 0;
    virtual bool read (uint64_t offset, void* dest, size_t numBytes) = 0;

    Bytes readBytes (uint64_t offset, size_t n)
    {
        Bytes b (n);
        if (n > 0 && ! read (offset, b.data(), n))
            b.clear();
        return b;
    }
};
using DevicePtr = std::shared_ptr<Device>;

/** Plain file on the host. */
class FileDevice : public Device
{
public:
    explicit FileDevice (const std::string& path);
    ~FileDevice() override;
    bool isOpen() const { return fp != nullptr; }
    uint64_t size() const override { return fileSize; }
    bool read (uint64_t offset, void* dest, size_t numBytes) override;

private:
    std::FILE* fp = nullptr;
    uint64_t fileSize = 0;
    std::mutex lock;
};

/** Window into another device (partitions). */
class OffsetDevice : public Device
{
public:
    OffsetDevice (DevicePtr p, uint64_t off, uint64_t len) : parent (std::move (p)), base (off), length (len) {}
    uint64_t size() const override { return length; }
    bool read (uint64_t offset, void* dest, size_t n) override
    {
        if (offset + n > length) return false;
        return parent->read (base + offset, dest, n);
    }
private:
    DevicePtr parent; uint64_t base, length;
};

/** Raw CD image (2352 bytes / sector, MODE1 or MODE2 FORM1) exposed as 2048-byte sectors. */
class RawCdDevice : public Device
{
public:
    RawCdDevice (DevicePtr p, int dataOffsetInSector) : parent (std::move (p)), dataOffset (dataOffsetInSector) {}
    uint64_t size() const override { return (parent->size() / 2352) * 2048; }
    bool read (uint64_t offset, void* dest, size_t n) override;
    static int detectRawCd (Device& d); // returns data offset (16 / 24) or 0 if not raw
private:
    DevicePtr parent; int dataOffset;
};

/** Device fully held in RAM. */
class MemoryDevice : public Device
{
public:
    explicit MemoryDevice (Bytes b) : data (std::move (b)) {}
    uint64_t size() const override { return data.size(); }
    bool read (uint64_t offset, void* dest, size_t n) override
    {
        if (offset + n > data.size()) return false;
        std::memcpy (dest, data.data() + offset, n);
        return true;
    }
private:
    Bytes data;
};

//==============================================================================
/** Virtual file-system node shown in LOAD / SAVE file lists. */
struct VNode : public std::enable_shared_from_this<VNode>
{
    enum class Type { Disk, Folder, Multi, Program, Sample, Other };

    enum class Kind
    {
        None,
        HostFile,     // a file on the host file system (path in hostPath)
        Wav, Aiff,    // PCM sample files
        Mp3, Compressed,  // MP3 / FLAC / OGG (decoded to RAM on load)
        Akp, Akm,     // S5000/S6000 program / multi
        S1000Program, S1000Sample, S3000Program, S3000Sample,
        DiskImage     // .iso / .img / .dmg / .bin ... (mountable)
    };

    std::string name;
    Type type = Type::Other;
    Kind kind = Kind::None;
    uint64_t size = 0;
    std::string dateTime;
    std::string hostPath;                  // only for host files
    std::weak_ptr<VNode> parent;

    std::function<Bytes()> readContent;    // full file content
    std::function<void (VNode&)> populate; // lazily fill children

    std::vector<std::shared_ptr<VNode>> children;
    bool populated = false;

    static std::recursive_mutex& populateMutex()
    {
        static std::recursive_mutex m;
        return m;
    }

    /** Thread safe: the LOAD page (message thread) and the loader thread may both browse. */
    void ensurePopulated()
    {
        std::lock_guard<std::recursive_mutex> g (populateMutex());
        if (! populated)
        {
            if (populate) populate (*this);
            populated = true;
        }
    }

    std::shared_ptr<VNode> addChild (std::shared_ptr<VNode> c)
    {
        c->parent = weak_from_this();
        children.push_back (c);
        return c;
    }

    std::string path() const
    {
        if (auto p = parent.lock())
            return p->path() + "/" + name;
        return name;
    }

    bool isContainer() const { return type == Type::Folder || type == Type::Disk; }
};
using VNodePtr = std::shared_ptr<VNode>;

/** Classify a file name by extension (sets type+kind). */
void classifyByName (VNode& n);

std::string toUpper (std::string s);
std::string trim (const std::string& s);

/** zlib inflate callback (the plugin uses juce::GZIPDecompressorInputStream). */
using InflateFn = std::function<bool (const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)>;

//==============================================================================
/** Mounted disk image. */
struct MountedImage
{
    std::string displayName;        // shown in DISK LIST
    std::string formatName;         // "ISO9660", "FAT16", "AKAI S1000 HD", ...
    VNodePtr root;
};

/** Detects the format of an image file and mounts every partition found.
    Returns one MountedImage per partition / file system.  */
std::vector<MountedImage> mountImageFile (const std::string& path, InflateFn inflate, std::string& errorOut,
                                          const std::string& tempDir);

std::vector<MountedImage> mountDevice (DevicePtr dev, const std::string& label, std::string& errorOut);

// individual file-system mounters (return nullptr if not recognised)
VNodePtr mountIso9660 (DevicePtr dev, const std::string& label, std::string& fmt);
VNodePtr mountFat (DevicePtr dev, const std::string& label, std::string& fmt);
std::vector<MountedImage> mountAkai (DevicePtr dev, const std::string& label);
VNodePtr deepScanAkaiSamples (DevicePtr dev, const std::string& label);

/** Converts an Apple UDIF .dmg into a raw image file at outPath. */
bool convertDmgToRaw (const std::string& dmgPath, const std::string& outPath, InflateFn inflate, std::string& error);
bool isDmg (Device& d);

} // namespace s6k::disk
