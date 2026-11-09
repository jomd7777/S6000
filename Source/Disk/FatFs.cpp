/*
  ==============================================================================
   S6000 Emulation - FAT12 / FAT16 / FAT32 reader (with long file names)
   The S5000/S6000 formats floppies, Zip/Jaz and hard disks as DOS FAT.
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "DiskDevice.h"
#include <algorithm>

namespace s6k::disk
{
namespace
{
    struct FatCtx
    {
        DevicePtr dev;
        int fatBits = 16;
        uint32_t bytesPerSector = 512, sectorsPerCluster = 1;
        uint64_t fatOffset = 0;      // bytes
        uint64_t rootDirOffset = 0;  // FAT12/16 fixed root dir
        uint32_t rootDirBytes = 0;
        uint64_t dataOffset = 0;
        uint32_t rootCluster = 0;    // FAT32
        uint32_t clusterCount = 0;
        Bytes fat;                   // cached FAT (first copy)

        uint32_t clusterBytes() const { return bytesPerSector * sectorsPerCluster; }

        uint32_t next (uint32_t c) const
        {
            if (fatBits == 12)
            {
                const size_t o = c + c / 2;
                if (o + 1 >= fat.size()) return 0xFFFFFFFF;
                const uint16_t v = le16 (fat.data() + o);
                const uint32_t r = (c & 1) ? (v >> 4) : (v & 0x0FFF);
                return r >= 0xFF8 ? 0xFFFFFFFF : r;
            }
            if (fatBits == 16)
            {
                if ((size_t) c * 2 + 1 >= fat.size()) return 0xFFFFFFFF;
                const uint32_t r = le16 (fat.data() + c * 2);
                return r >= 0xFFF8 ? 0xFFFFFFFF : r;
            }
            if ((size_t) c * 4 + 3 >= fat.size()) return 0xFFFFFFFF;
            const uint32_t r = le32 (fat.data() + (size_t) c * 4) & 0x0FFFFFFF;
            return r >= 0x0FFFFFF8 ? 0xFFFFFFFF : r;
        }

        Bytes readChain (uint32_t first, uint64_t maxBytes) const
        {
            Bytes out;
            if (first < 2) return out;
            const uint32_t cb = clusterBytes();
            uint32_t c = first;
            uint32_t guard = 0;
            out.reserve ((size_t) std::min<uint64_t> (maxBytes, 512ull * 1024 * 1024));
            while (c >= 2 && c < clusterCount + 2 && out.size() < maxBytes && guard++ < 0x1000000)
            {
                // read runs of consecutive clusters in one go
                uint32_t run = 1;
                while (out.size() + (uint64_t) run * cb < maxBytes)
                {
                    const uint32_t n = next (c + run - 1);
                    if (n != c + run) break;
                    ++run;
                }
                const uint64_t want = std::min<uint64_t> ((uint64_t) run * cb, maxBytes - out.size());
                const size_t old = out.size();
                out.resize (old + (size_t) want);
                if (! dev->read (dataOffset + (uint64_t) (c - 2) * cb, out.data() + old, (size_t) want))
                {
                    out.resize (old);
                    break;
                }
                c = next (c + run - 1);
            }
            return out;
        }
    };

    std::string lfnPart (const uint8_t* e)
    {
        static const int offs[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        std::string s;
        for (int o : offs)
        {
            const uint16_t c = le16 (e + o);
            if (c == 0x0000 || c == 0xFFFF) break;
            if (c < 0x80) s.push_back ((char) c);
            else if (c < 0x800) { s.push_back ((char) (0xC0 | (c >> 6))); s.push_back ((char) (0x80 | (c & 0x3f))); }
            else { s.push_back ((char) (0xE0 | (c >> 12))); s.push_back ((char) (0x80 | ((c >> 6) & 0x3f))); s.push_back ((char) (0x80 | (c & 0x3f))); }
        }
        return s;
    }

    void parseDir (const std::shared_ptr<FatCtx>& ctx, VNode& node, const Bytes& dir, int depth)
    {
        std::vector<std::string> lfnParts;
        for (size_t p = 0; p + 32 <= dir.size(); p += 32)
        {
            const uint8_t* e = dir.data() + p;
            if (e[0] == 0x00) break;
            if (e[0] == 0xE5) { lfnParts.clear(); continue; }
            const uint8_t attr = e[11];
            if (attr == 0x0F)
            {
                if (e[0] & 0x40) lfnParts.clear();
                lfnParts.push_back (lfnPart (e));
                continue;
            }
            if (attr & 0x08) { lfnParts.clear(); continue; } // volume label

            std::string name;
            if (! lfnParts.empty())
            {
                for (auto it = lfnParts.rbegin(); it != lfnParts.rend(); ++it) name += *it;
                lfnParts.clear();
            }
            else
            {
                std::string base = trim (std::string ((const char*) e, 8));
                std::string ext  = trim (std::string ((const char*) e + 8, 3));
                if (! base.empty() && (uint8_t) base[0] == 0x05) base[0] = (char) 0xE5;
                name = ext.empty() ? base : base + "." + ext;
            }
            if (name == "." || name == ".." || name.empty()) continue;

            uint32_t cluster = le16 (e + 26);
            if (ctx->fatBits == 32) cluster |= (uint32_t) le16 (e + 20) << 16;
            const uint32_t size = le32 (e + 28);

            const uint16_t t = le16 (e + 22), d = le16 (e + 24);
            char date[32];
            std::snprintf (date, sizeof (date), "%02d/%02d/%02d %02d:%02d", d & 31, (d >> 5) & 15, (1980 + (d >> 9)) % 100, t >> 11, (t >> 5) & 63);

            auto child = std::make_shared<VNode>();
            child->name = name;
            child->size = size;
            child->dateTime = date;

            if (attr & 0x10)
            {
                child->type = VNode::Type::Folder;
                child->populate = [ctx, cluster, depth] (VNode& n)
                {
                    if (depth > 32) return;
                    parseDir (ctx, n, ctx->readChain (cluster, 16 * 1024 * 1024), depth + 1);
                };
            }
            else
            {
                classifyByName (*child);
                child->readContent = [ctx, cluster, size]() { return ctx->readChain (cluster, size); };
            }
            node.addChild (child);
        }
    }
} // namespace

VNodePtr mountFat (DevicePtr dev, const std::string& label, std::string& fmt)
{
    uint8_t bs[512];
    if (! dev->read (0, bs, 512)) return nullptr;

    const uint32_t bps = le16 (bs + 11);
    const uint32_t spc = bs[13];
    const uint32_t reserved = le16 (bs + 14);
    const uint32_t numFats = bs[16];
    const uint32_t rootEnt = le16 (bs + 17);
    uint32_t totSec = le16 (bs + 19);
    if (totSec == 0) totSec = le32 (bs + 32);
    uint32_t fatSz = le16 (bs + 22);
    if (fatSz == 0) fatSz = le32 (bs + 36);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return nullptr;
    if (spc == 0 || (spc & (spc - 1)) != 0) return nullptr;
    if (numFats < 1 || numFats > 4 || reserved == 0 || fatSz == 0 || totSec == 0) return nullptr;

    auto ctx = std::make_shared<FatCtx>();
    ctx->dev = dev;
    ctx->bytesPerSector = bps;
    ctx->sectorsPerCluster = spc;

    const uint32_t rootDirSectors = ((rootEnt * 32) + (bps - 1)) / bps;
    const uint64_t firstData = reserved + (uint64_t) numFats * fatSz + rootDirSectors;
    if (firstData >= totSec) return nullptr;
    ctx->clusterCount = (uint32_t) ((totSec - firstData) / spc);
    ctx->fatBits = ctx->clusterCount < 4085 ? 12 : ctx->clusterCount < 65525 ? 16 : 32;

    ctx->fatOffset = (uint64_t) reserved * bps;
    ctx->rootDirOffset = ctx->fatOffset + (uint64_t) numFats * fatSz * bps;
    ctx->rootDirBytes = rootDirSectors * bps;
    ctx->dataOffset = firstData * bps;
    ctx->rootCluster = ctx->fatBits == 32 ? le32 (bs + 44) : 0;

    const uint64_t fatBytes = std::min<uint64_t> ((uint64_t) fatSz * bps, 64ull * 1024 * 1024);
    ctx->fat = dev->readBytes (ctx->fatOffset, (size_t) fatBytes);
    if (ctx->fat.empty()) return nullptr;

    // media descriptor sanity check
    if (ctx->fat[0] < 0xF0) return nullptr;

    std::string volLabel = trim (std::string ((const char*) bs + (ctx->fatBits == 32 ? 71 : 43), 11));
    if (volLabel == "NO NAME") volLabel.clear();

    auto root = std::make_shared<VNode>();
    root->name = volLabel.empty() ? label : volLabel;
    root->type = VNode::Type::Disk;
    root->populate = [ctx] (VNode& n)
    {
        Bytes dir = ctx->fatBits == 32 ? ctx->readChain (ctx->rootCluster, 16 * 1024 * 1024)
                                        : ctx->dev->readBytes (ctx->rootDirOffset, ctx->rootDirBytes);
        parseDir (ctx, n, dir, 0);
    };
    fmt = ctx->fatBits == 12 ? "FAT12" : ctx->fatBits == 16 ? "FAT16" : "FAT32";
    return root;
}

} // namespace s6k::disk
