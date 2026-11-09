/*
  ==============================================================================
   S6000 Emulation - Akai S1000 / S3000 disk reader
     * Hard disk & CD-ROM : 8 KB blocks, partitions A..Z, volumes, FAT
     * Floppy (DD 800K / HD 1.6M) : 1 KB blocks, 64 entries
     * Deep scan fallback : finds S1000 sample headers in raw blocks
   The partition / FAT offsets follow the publicly documented layout used by
   akaiutil / akaitools.  Every file read is validated against its header;
   if a FAT chain looks inconsistent the reader falls back to contiguous
   blocks (library CD-ROMs are written sequentially).
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "DiskDevice.h"
#include "../Formats/AkaiFormats.h"
#include <algorithm>

namespace s6k::disk
{
namespace
{
    constexpr uint32_t hdBlock = 0x2000;
    constexpr uint32_t flBlock = 0x0400;
    constexpr uint32_t volTableOffset = 0xCA;   // 100 volume entries x 16 bytes
    constexpr uint32_t fatOffsetHd = 0x70A;     // FAT directly after the volume table
    constexpr uint32_t maxVolumes = 100;

    struct AkaiPart
    {
        DevicePtr dev;
        uint64_t base = 0;
        uint32_t blockSize = hdBlock;
        uint32_t numBlocks = 0;
        std::vector<uint16_t> fat;

        Bytes read (uint32_t startBlock, uint64_t length, bool useFat) const
        {
            Bytes out;
            out.resize ((size_t) length);
            uint64_t done = 0;
            uint32_t b = startBlock;
            uint32_t guard = 0;
            while (done < length && guard++ < 200000)
            {
                if (b == 0 || b >= numBlocks) return {};
                const uint64_t n = std::min<uint64_t> (blockSize, length - done);
                if (! dev->read (base + (uint64_t) b * blockSize, out.data() + done, (size_t) n)) return {};
                done += n;
                if (done >= length) break;
                uint32_t nb = b + 1;
                if (useFat && b < fat.size())
                {
                    const uint16_t v = fat[b];
                    if (v != 0 && (v & 0xC000) == 0 && v < numBlocks && v != b) nb = v;
                }
                b = nb;
            }
            return out;
        }
    };

    bool validFileHeader (const Bytes& data, uint8_t type)
    {
        if (data.empty()) return false;
        const uint8_t t = type & 0x7F;
        if (t == 's') return fmt::isS1000SampleHeader (data.data(), data.size());
        if (t == 'p') return fmt::isS1000ProgramHeader (data.data(), data.size());
        return true;
    }

    VNodePtr makeFileNode (const std::shared_ptr<AkaiPart>& part, const uint8_t* e)
    {
        const uint8_t type = e[16];
        const uint32_t size = le24 (e + 17);
        const uint32_t start = le16 (e + 20);
        const uint8_t t = type & 0x7F;
        const bool s3k = (type & 0x80) != 0;

        auto n = std::make_shared<VNode>();
        n->name = trim (fmt::akaiToAscii (e, 12));
        n->size = size;
        if (t == 's')      { n->type = VNode::Type::Sample;  n->kind = s3k ? VNode::Kind::S3000Sample  : VNode::Kind::S1000Sample; }
        else if (t == 'p') { n->type = VNode::Type::Program; n->kind = s3k ? VNode::Kind::S3000Program : VNode::Kind::S1000Program; }
        else               { n->type = VNode::Type::Other;   n->kind = VNode::Kind::None; }

        n->readContent = [part, start, size, type]()
        {
            Bytes d = part->read (start, size, true);
            if (! validFileHeader (d, type))
            {
                Bytes c = part->read (start, size, false);
                if (validFileHeader (c, type)) return c;
            }
            return d;
        };
        return n;
    }

    bool validFileEntry (const uint8_t* e, uint32_t numBlocks)
    {
        if (! fmt::isValidAkaiName (e, 12)) return false;
        const uint8_t t = e[16] & 0x7F;
        if (t != 's' && t != 'p' && t != 'x' && t != 'd' && t != 'q' && t != 't' && t != 'm' && t != 'c') return false;
        const uint32_t size = le24 (e + 17), start = le16 (e + 20);
        return size > 0 && size < 0x4000000 && start > 0 && start < numBlocks;
    }

    bool validVolumeEntry (const uint8_t* e, uint32_t numBlocks)
    {
        const uint16_t type = le16 (e + 12), start = le16 (e + 14);
        if (type == 0) return false;
        return type <= 8 && start > 0 && start < numBlocks && fmt::isValidAkaiName (e, 12);
    }

    void populateVolume (const std::shared_ptr<AkaiPart>& part, VNode& node, uint32_t startBlock, int maxEntries)
    {
        Bytes dir = part->read (startBlock, (uint64_t) maxEntries * 24, true);
        if (dir.empty()) dir = part->read (startBlock, (uint64_t) maxEntries * 24, false);
        int invalidRun = 0;
        for (int i = 0; i < maxEntries && (size_t) (i + 1) * 24 <= dir.size(); ++i)
        {
            const uint8_t* e = dir.data() + i * 24;
            if (validFileEntry (e, part->numBlocks))
            {
                node.addChild (makeFileNode (part, e));
                invalidRun = 0;
            }
            else if (++invalidRun > 160) break;
        }
    }

    bool parsePartitionHeader (DevicePtr dev, uint64_t off, std::shared_ptr<AkaiPart>& out, int& numVolumes)
    {
        const uint64_t hdrLen = 3 * hdBlock;
        if (off + hdrLen > dev->size()) return false;
        Bytes h = dev->readBytes (off, (size_t) hdrLen);
        if (h.empty()) return false;

        uint32_t blocks = le16 (h.data());
        const uint32_t devBlocks = (uint32_t) std::min<uint64_t> ((dev->size() - off) / hdBlock, 0xFFFF);
        if (blocks < 4 || blocks > devBlocks) blocks = devBlocks;   // tolerate odd size words

        numVolumes = 0;
        int bad = 0;
        for (uint32_t v = 0; v < maxVolumes; ++v)
        {
            const uint8_t* e = h.data() + volTableOffset + v * 16;
            if (validVolumeEntry (e, blocks)) ++numVolumes;
            else if (le16 (e + 12) != 0) ++bad;
        }
        if (numVolumes == 0 || bad > numVolumes) return false;

        out = std::make_shared<AkaiPart>();
        out->dev = dev;
        out->base = off;
        out->blockSize = hdBlock;
        out->numBlocks = blocks;
        const size_t fatEntries = std::min<size_t> (blocks, (h.size() - fatOffsetHd) / 2);
        out->fat.resize (fatEntries);
        for (size_t i = 0; i < fatEntries; ++i) out->fat[i] = le16 (h.data() + fatOffsetHd + i * 2);
        return true;
    }

    VNodePtr buildPartitionNode (const std::shared_ptr<AkaiPart>& part, const std::string& name)
    {
        Bytes h = part->dev->readBytes (part->base, volTableOffset + maxVolumes * 16);
        auto root = std::make_shared<VNode>();
        root->name = name;
        root->type = VNode::Type::Disk;
        root->populate = [part, h] (VNode& n)
        {
            for (uint32_t v = 0; v < maxVolumes; ++v)
            {
                const uint8_t* e = h.data() + volTableOffset + v * 16;
                if (! validVolumeEntry (e, part->numBlocks)) continue;
                const uint16_t type = le16 (e + 12), start = le16 (e + 14);
                auto vol = std::make_shared<VNode>();
                vol->name = trim (fmt::akaiToAscii (e, 12));
                if (vol->name.empty()) vol->name = "VOLUME " + std::to_string (v + 1);
                vol->type = VNode::Type::Folder;
                const int entries = (type == 1) ? 126 : 510;
                vol->populate = [part, start, entries] (VNode& vn) { populateVolume (part, vn, start, entries); };
                n.addChild (vol);
            }
        };
        return root;
    }

    std::vector<MountedImage> mountAkaiHd (DevicePtr dev, const std::string& label)
    {
        std::vector<MountedImage> res;
        uint64_t off = 0;
        int index = 0;
        while (off + 3 * hdBlock <= dev->size() && index < 32)
        {
            std::shared_ptr<AkaiPart> part;
            int vols = 0;
            if (! parsePartitionHeader (dev, off, part, vols))
            {
                if (index == 0) return res;
                // chain broken: scan forward for the next partition header
                bool found = false;
                for (uint64_t s = off; s + 3 * hdBlock <= dev->size(); s += hdBlock)
                {
                    uint8_t probe[16];
                    if (! dev->read (s + volTableOffset, probe, 16)) break;
                    if (! validVolumeEntry (probe, 0xFFFF)) continue;
                    if (parsePartitionHeader (dev, s, part, vols)) { off = s; found = true; break; }
                }
                if (! found) break;
            }
            const std::string partName = label + " " + std::string (1, (char) ('A' + index));
            res.push_back ({ partName, "AKAI S1000/S3000", buildPartitionNode (part, partName) });
            off += (uint64_t) part->numBlocks * hdBlock;
            ++index;
        }
        return res;
    }

    std::vector<MountedImage> mountAkaiFloppy (DevicePtr dev, const std::string& label)
    {
        std::vector<MountedImage> res;
        const uint64_t sz = dev->size();
        if (sz < 700 * 1024 || sz > 1800 * 1024) return res;

        auto part = std::make_shared<AkaiPart>();
        part->dev = dev;
        part->blockSize = flBlock;
        part->numBlocks = (uint32_t) (sz / flBlock);

        Bytes dir = dev->readBytes (0, 64 * 24);
        int valid = 0, bad = 0;
        for (int i = 0; i < 64; ++i)
        {
            const uint8_t* e = dir.data() + i * 24;
            if (validFileEntry (e, part->numBlocks)) ++valid;
            else if (e[16] != 0) ++bad;
        }
        if (valid == 0 || bad > valid) return res;

        Bytes fat = dev->readBytes (64 * 24, part->numBlocks * 2);
        part->fat.resize (fat.size() / 2);
        for (size_t i = 0; i < part->fat.size(); ++i) part->fat[i] = le16 (fat.data() + i * 2);

        auto root = std::make_shared<VNode>();
        root->name = label;
        root->type = VNode::Type::Disk;
        root->populate = [part, dir] (VNode& n)
        {
            for (int i = 0; i < 64; ++i)
            {
                const uint8_t* e = dir.data() + i * 24;
                if (validFileEntry (e, part->numBlocks)) n.addChild (makeFileNode (part, e));
            }
        };
        res.push_back ({ label, sz > 1000 * 1024 ? "AKAI S1000 FLOPPY HD" : "AKAI S1000 FLOPPY DD", root });
        return res;
    }
} // namespace

std::vector<MountedImage> mountAkai (DevicePtr dev, const std::string& label)
{
    auto hd = mountAkaiHd (dev, label);
    if (! hd.empty()) return hd;
    return mountAkaiFloppy (dev, label);
}

VNodePtr deepScanAkaiSamples (DevicePtr dev, const std::string& label)
{
    const uint64_t limit = std::min<uint64_t> (dev->size(), 4ull * 1024 * 1024 * 1024);
    auto root = std::make_shared<VNode>();
    root->name = label;
    root->type = VNode::Type::Disk;

    std::vector<std::pair<uint64_t, uint32_t>> hits;
    uint8_t hdr[192];
    for (uint64_t off = 0; off + sizeof (hdr) <= limit; off += flBlock)
    {
        if (! dev->read (off, hdr, sizeof (hdr))) break;
        if (hdr[0] != 3) continue;
        if (! fmt::isS1000SampleHeader (hdr, sizeof (hdr))) continue;
        const uint32_t numWords = le32 (hdr + 0x1A);
        const uint32_t size = 150 + numWords * 2;
        hits.push_back ({ off, size });
        off += ((uint64_t) size / flBlock) * flBlock;
        if (hits.size() > 20000) break;
    }
    if (hits.empty()) return nullptr;

    for (auto& h : hits)
    {
        uint8_t nm[12];
        dev->read (h.first + 3, nm, 12);
        auto n = std::make_shared<VNode>();
        n->name = trim (fmt::akaiToAscii (nm, 12));
        n->type = VNode::Type::Sample;
        n->kind = VNode::Kind::S1000Sample;
        n->size = h.second;
        const uint64_t off = h.first; const uint32_t sz = h.second;
        n->readContent = [dev, off, sz]() { return dev->readBytes (off, std::min<uint64_t> (sz, dev->size() - off)); };
        root->addChild (n);
    }
    root->populated = true;
    return root;
}

} // namespace s6k::disk
