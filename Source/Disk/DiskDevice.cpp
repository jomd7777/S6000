/*
  ==============================================================================
   S6000 Emulation - Disk layer : devices, classification, image detection
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "DiskDevice.h"
#include <algorithm>
#include <cctype>

namespace s6k::disk
{
//==============================================================================
static int fseek64 (std::FILE* f, uint64_t off)
{
   #if defined (_WIN32)
    return _fseeki64 (f, (long long) off, SEEK_SET);
   #else
    return fseeko (f, (off_t) off, SEEK_SET);
   #endif
}

static uint64_t ftell64 (std::FILE* f)
{
   #if defined (_WIN32)
    return (uint64_t) _ftelli64 (f);
   #else
    return (uint64_t) ftello (f);
   #endif
}

FileDevice::FileDevice (const std::string& path)
{
   #if defined (_WIN32)
    // UTF-8 path -> wide path
    std::wstring w;
    {
        size_t i = 0;
        while (i < path.size())
        {
            uint32_t c = (uint8_t) path[i];
            int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : 3;
            c &= extra == 0 ? 0x7f : extra == 1 ? 0x1f : extra == 2 ? 0x0f : 0x07;
            for (int k = 0; k < extra && i + 1 + k < path.size(); ++k)
                c = (c << 6) | ((uint8_t) path[i + 1 + k] & 0x3f);
            i += 1 + extra;
            if (c >= 0x10000) { c -= 0x10000; w.push_back ((wchar_t) (0xD800 + (c >> 10))); w.push_back ((wchar_t) (0xDC00 + (c & 0x3ff))); }
            else w.push_back ((wchar_t) c);
        }
    }
    fp = _wfopen (w.c_str(), L"rb");
   #else
    fp = std::fopen (path.c_str(), "rb");
   #endif
    if (fp != nullptr)
    {
        std::fseek (fp, 0, SEEK_END);
        fileSize = ftell64 (fp);
        fseek64 (fp, 0);
    }
}

FileDevice::~FileDevice()
{
    if (fp != nullptr) std::fclose (fp);
}

bool FileDevice::read (uint64_t offset, void* dest, size_t n)
{
    if (fp == nullptr || offset + n > fileSize) return false;
    std::lock_guard<std::mutex> g (lock);
    if (fseek64 (fp, offset) != 0) return false;
    return std::fread (dest, 1, n, fp) == n;
}

//==============================================================================
int RawCdDevice::detectRawCd (Device& d)
{
    if (d.size() < 2352 * 20 || (d.size() % 2352) != 0) return 0;
    uint8_t s[24];
    if (! d.read (2352 * 16, s, sizeof (s))) return 0;
    static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    if (std::memcmp (s, sync, 12) != 0) return 0;
    const int mode = s[15];
    return mode == 2 ? 24 : 16;
}

bool RawCdDevice::read (uint64_t offset, void* dest, size_t n)
{
    auto* out = static_cast<uint8_t*> (dest);
    while (n > 0)
    {
        const uint64_t sector = offset / 2048;
        const size_t inSector = (size_t) (offset % 2048);
        const size_t chunk = std::min (n, (size_t) 2048 - inSector);
        if (! parent->read (sector * 2352 + (uint64_t) dataOffset + inSector, out, chunk))
            return false;
        out += chunk; offset += chunk; n -= chunk;
    }
    return true;
}

//==============================================================================
std::string toUpper (std::string s)
{
    for (auto& c : s) c = (char) std::toupper ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char) s[a] <= ' ') ++a;
    while (b > a && (unsigned char) s[b - 1] <= ' ') --b;
    return s.substr (a, b - a);
}

void classifyByName (VNode& n)
{
    auto dot = n.name.find_last_of ('.');
    const std::string ext = dot == std::string::npos ? std::string() : toUpper (n.name.substr (dot + 1));

    if (ext == "WAV")                     { n.type = VNode::Type::Sample;  n.kind = VNode::Kind::Wav; }
    else if (ext == "AIF" || ext == "AIFF") { n.type = VNode::Type::Sample;  n.kind = VNode::Kind::Aiff; }
    else if (ext == "AKP")                { n.type = VNode::Type::Program; n.kind = VNode::Kind::Akp; }
    else if (ext == "AKM")                { n.type = VNode::Type::Multi;   n.kind = VNode::Kind::Akm; }
    else if (ext == "ISO" || ext == "IMG" || ext == "DMG" || ext == "BIN" || ext == "CDR"
             || ext == "HDS" || ext == "TOAST" || ext == "IMA" || ext == "RAW" || ext == "DSK")
                                          { n.type = VNode::Type::Disk;    n.kind = VNode::Kind::DiskImage; }
    else                                  { n.type = VNode::Type::Other;   n.kind = VNode::Kind::None; }
}

//==============================================================================
// Apple partition map (found inside many .dmg / Mac CD images)
static std::vector<std::pair<uint64_t, uint64_t>> findApmPartitions (Device& d, std::vector<std::string>& types)
{
    std::vector<std::pair<uint64_t, uint64_t>> parts;
    uint8_t b[512];
    if (! d.read (0, b, 512) || b[0] != 'E' || b[1] != 'R') return parts;
    uint32_t blockSize = be16 (b + 2);
    if (blockSize == 0) blockSize = 512;
    if (! d.read (512, b, 512) || b[0] != 'P' || b[1] != 'M') return parts;
    const uint32_t count = std::min<uint32_t> (be32 (b + 4), 64);
    for (uint32_t i = 1; i <= count; ++i)
    {
        if (! d.read ((uint64_t) i * 512, b, 512) || b[0] != 'P' || b[1] != 'M') break;
        const uint64_t start = (uint64_t) be32 (b + 8) * 512;
        const uint64_t len   = (uint64_t) be32 (b + 12) * 512;
        std::string type ((const char*) b + 48, strnlen ((const char*) b + 48, 32));
        if (type == "Apple_partition_map" || type == "Apple_Free" || type == "Apple_Driver43"
            || type == "Apple_Driver_ATAPI" || type == "Apple_Patches" || type == "Apple_Driver")
            continue;
        if (start + len <= d.size() && len > 0) { parts.push_back ({ start, len }); types.push_back (type); }
    }
    (void) blockSize;
    return parts;
}

// MBR partitions (PC formatted Zip/Jaz/HD as used by S5000/S6000)
static std::vector<std::pair<uint64_t, uint64_t>> findMbrPartitions (Device& d)
{
    std::vector<std::pair<uint64_t, uint64_t>> parts;
    uint8_t b[512];
    if (! d.read (0, b, 512) || b[510] != 0x55 || b[511] != 0xAA) return parts;

    // a FAT boot sector also ends with 55AA – check whether sector 0 is a BPB
    const bool looksLikeBpb = (b[0] == 0xEB || b[0] == 0xE9) && le16 (b + 11) >= 512 && le16 (b + 11) <= 4096
                              && b[13] != 0 && b[16] >= 1 && b[16] <= 2;
    if (looksLikeBpb) return parts;

    for (int i = 0; i < 4; ++i)
    {
        const uint8_t* e = b + 446 + i * 16;
        const uint8_t type = e[4];
        const uint64_t lba = le32 (e + 8), cnt = le32 (e + 12);
        if (type == 0 || cnt == 0) continue;
        if (type == 0x05 || type == 0x0F) // extended partition: walk EBR chain
        {
            uint64_t ebrBase = lba * 512, ebr = ebrBase;
            for (int guard = 0; guard < 64; ++guard)
            {
                uint8_t x[512];
                if (! d.read (ebr, x, 512) || x[510] != 0x55 || x[511] != 0xAA) break;
                const uint8_t* l = x + 446;
                if (l[4] != 0 && le32 (l + 12) != 0)
                    parts.push_back ({ ebr + (uint64_t) le32 (l + 8) * 512, (uint64_t) le32 (l + 12) * 512 });
                const uint8_t* n = x + 462;
                if (n[4] == 0 || le32 (n + 12) == 0) break;
                ebr = ebrBase + (uint64_t) le32 (n + 8) * 512;
            }
            continue;
        }
        const uint64_t start = lba * 512, len = cnt * 512;
        if (start < d.size())
            parts.push_back ({ start, std::min (len, d.size() - start) });
    }
    return parts;
}

std::vector<MountedImage> mountDevice (DevicePtr dev, const std::string& label, std::string& errorOut)
{
    std::vector<MountedImage> result;
    if (dev == nullptr || dev->size() < 512) { errorOut = "IMAGE TOO SMALL"; return result; }

    // 1) raw 2352-byte CD sectors
    if (int off = RawCdDevice::detectRawCd (*dev))
        dev = std::make_shared<RawCdDevice> (dev, off);

    std::string fmt;

    // 2) ISO9660 / Joliet CD-ROM
    if (auto r = mountIso9660 (dev, label, fmt))
    {
        result.push_back ({ label, fmt, r });
        // hybrid discs may also carry an Akai partition after the ISO track – ignore
        return result;
    }

    // 3) Apple partition map (from .dmg / Mac CDs)
    {
        std::vector<std::string> types;
        auto apm = findApmPartitions (*dev, types);
        int idx = 0;
        for (auto& p : apm)
        {
            auto sub = std::make_shared<OffsetDevice> (dev, p.first, p.second);
            const std::string partLabel = label + " [" + std::string (1, (char) ('A' + idx++)) + "]";
            std::string f2;
            if (auto r = mountIso9660 (sub, partLabel, f2)) result.push_back ({ partLabel, f2, r });
            else if (auto r2 = mountFat (sub, partLabel, f2)) result.push_back ({ partLabel, f2, r2 });
            else
            {
                auto ak = mountAkai (sub, partLabel);
                for (auto& m : ak) result.push_back (m);
                if (ak.empty() && types[(size_t) idx - 1].find ("HFS") != std::string::npos && errorOut.empty())
                    errorOut = "HFS VOLUME (MAC) NOT SUPPORTED";
            }
        }
        if (! result.empty()) return result;
    }

    // 4) MBR partitioned FAT disk
    {
        auto mbr = findMbrPartitions (*dev);
        int idx = 0;
        for (auto& p : mbr)
        {
            auto sub = std::make_shared<OffsetDevice> (dev, p.first, p.second);
            const std::string partLabel = label + " [" + std::string (1, (char) ('A' + idx++)) + "]";
            std::string f2;
            if (auto r = mountFat (sub, partLabel, f2)) result.push_back ({ partLabel, f2, r });
        }
        if (! result.empty()) return result;
    }

    // 5) super-floppy FAT (no partition table)
    if (auto r = mountFat (dev, label, fmt))
    {
        result.push_back ({ label, fmt, r });
        return result;
    }

    // 6) Akai S1000 / S3000 hard disk, CD-ROM or floppy
    {
        auto ak = mountAkai (dev, label);
        if (! ak.empty()) return ak;
    }

    // 7) last resort: scan raw blocks for Akai sample headers
    if (auto r = deepScanAkaiSamples (dev, label))
    {
        result.push_back ({ label + " (DEEP SCAN)", "AKAI RAW SCAN", r });
        return result;
    }

    if (errorOut.empty()) errorOut = "UNKNOWN DISK FORMAT";
    return result;
}

std::vector<MountedImage> mountImageFile (const std::string& path, InflateFn inflate, std::string& errorOut,
                                          const std::string& tempDir)
{
    auto file = std::make_shared<FileDevice> (path);
    if (! file->isOpen()) { errorOut = "CANNOT OPEN FILE"; return {}; }

    std::string label = path;
    if (auto s = label.find_last_of ("/\\"); s != std::string::npos) label = label.substr (s + 1);

    if (isDmg (*file))
    {
        std::string out = tempDir;
        if (! out.empty() && out.back() != '/' && out.back() != '\\') out += "/";
        out += label + ".raw";
        std::string err;
        if (! convertDmgToRaw (path, out, inflate, err)) { errorOut = err; return {}; }
        auto raw = std::make_shared<FileDevice> (out);
        if (! raw->isOpen()) { errorOut = "DMG: CANNOT OPEN TEMP IMAGE"; return {}; }
        return mountDevice (raw, label, errorOut);
    }

    return mountDevice (file, label, errorOut);
}

} // namespace s6k::disk
