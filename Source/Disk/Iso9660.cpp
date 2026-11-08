/*
  ==============================================================================
   S6000 Emulation - ISO9660 / Joliet CD-ROM reader
   (S5000/S6000 library CDs, .iso images, raw .bin via RawCdDevice)
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "DiskDevice.h"

namespace s6k::disk
{
namespace
{
    struct IsoCtx
    {
        DevicePtr dev;
        uint32_t blockSize = 2048;
        bool joliet = false;
    };

    std::string ucs2beToUtf8 (const uint8_t* p, size_t n)
    {
        std::string s;
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            uint32_t c = be16 (p + i);
            if (c >= 0xD800 && c <= 0xDFFF) { c = '?'; }
            if (c < 0x80) s.push_back ((char) c);
            else if (c < 0x800) { s.push_back ((char) (0xC0 | (c >> 6))); s.push_back ((char) (0x80 | (c & 0x3f))); }
            else { s.push_back ((char) (0xE0 | (c >> 12))); s.push_back ((char) (0x80 | ((c >> 6) & 0x3f))); s.push_back ((char) (0x80 | (c & 0x3f))); }
        }
        return s;
    }

    std::string cleanName (std::string s, bool isDir)
    {
        if (auto semi = s.find (';'); semi != std::string::npos) s = s.substr (0, semi);
        if (! isDir && ! s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    void populateDir (const std::shared_ptr<IsoCtx>& ctx, VNode& node, uint32_t extent, uint32_t length, int depth)
    {
        if (depth > 32 || length == 0 || length > 64 * 1024 * 1024) return;
        Bytes dir = ctx->dev->readBytes ((uint64_t) extent * ctx->blockSize, length);
        if (dir.empty()) return;

        size_t pos = 0;
        while (pos < dir.size())
        {
            const uint8_t len = dir[pos];
            if (len == 0)
            {
                // records never span sectors: jump to next sector
                pos = ((pos / ctx->blockSize) + 1) * ctx->blockSize;
                continue;
            }
            if (pos + len > dir.size() || len < 34) break;
            const uint8_t* r = dir.data() + pos;
            const uint32_t ext = le32 (r + 2);
            const uint32_t sz  = le32 (r + 10);
            const uint8_t flags = r[25];
            const uint8_t nameLen = r[32];
            const uint8_t* nm = r + 33;
            pos += len;

            if (nameLen == 1 && (nm[0] == 0 || nm[0] == 1)) continue; // . and ..
            if ((flags & 0x01) != 0) continue;                        // hidden / associated

            const bool isDir = (flags & 0x02) != 0;
            std::string name = ctx->joliet ? ucs2beToUtf8 (nm, nameLen) : std::string ((const char*) nm, nameLen);
            name = cleanName (name, isDir);
            if (name.empty()) continue;

            char date[32];
            std::snprintf (date, sizeof (date), "%02d/%02d/%02d %02d:%02d", r[20], r[19], (1900 + r[18]) % 100, r[21], r[22]);

            auto child = std::make_shared<VNode>();
            child->name = name;
            child->size = sz;
            child->dateTime = date;

            if (isDir)
            {
                child->type = VNode::Type::Folder;
                child->populate = [ctx, ext, sz, depth] (VNode& n) { populateDir (ctx, n, ext, sz, depth + 1); };
            }
            else
            {
                classifyByName (*child);
                child->readContent = [ctx, ext, sz]() { return ctx->dev->readBytes ((uint64_t) ext * ctx->blockSize, sz); };
            }
            node.addChild (child);
        }
    }
} // namespace

VNodePtr mountIso9660 (DevicePtr dev, const std::string& label, std::string& fmt)
{
    if (dev->size() < 17 * 2048) return nullptr;

    uint8_t pvd[2048];
    bool foundPvd = false;
    uint8_t chosen[2048];
    bool joliet = false;

    for (uint32_t s = 16; s < 64; ++s)
    {
        uint8_t vd[2048];
        if (! dev->read ((uint64_t) s * 2048, vd, 2048)) break;
        if (std::memcmp (vd + 1, "CD001", 5) != 0) break;
        if (vd[0] == 255) break;
        if (vd[0] == 1 && ! foundPvd) { std::memcpy (pvd, vd, 2048); foundPvd = true; }
        if (vd[0] == 2 && vd[88] == 0x25 && vd[89] == 0x2F && (vd[90] == 0x40 || vd[90] == 0x43 || vd[90] == 0x45))
        {
            std::memcpy (chosen, vd, 2048);
            joliet = true;
        }
    }
    if (! foundPvd) return nullptr;
    if (! joliet) std::memcpy (chosen, pvd, 2048);

    auto ctx = std::make_shared<IsoCtx>();
    ctx->dev = dev;
    ctx->blockSize = le16 (chosen + 128);
    if (ctx->blockSize == 0) ctx->blockSize = 2048;
    ctx->joliet = joliet;

    const uint8_t* rootRec = chosen + 156;
    const uint32_t rootExt = le32 (rootRec + 2);
    const uint32_t rootLen = le32 (rootRec + 10);

    std::string volId = trim (std::string ((const char*) pvd + 40, 32));

    auto root = std::make_shared<VNode>();
    root->name = volId.empty() ? label : volId;
    root->type = VNode::Type::Disk;
    root->populate = [ctx, rootExt, rootLen] (VNode& n) { populateDir (ctx, n, rootExt, rootLen, 0); };
    fmt = joliet ? "ISO9660 (JOLIET)" : "ISO9660";
    return root;
}

} // namespace s6k::disk
