/*
  ==============================================================================
   S6000 Emulation - Apple .dmg (UDIF) to raw image converter
   Supports chunk types: zero-fill, raw, ignore, ADC, zlib (UDZO).
   bzip2 (UDBZ) / LZFSE (ULFO) / LZMA images report an error suggesting
   "hdiutil convert in.dmg -format UDRW -o out.dmg".
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "DiskDevice.h"
#include <algorithm>

namespace s6k::disk
{
namespace
{
    Bytes base64Decode (const char* s, size_t n)
    {
        static int8_t table[256];
        static bool init = false;
        if (! init)
        {
            std::memset (table, -1, sizeof (table));
            const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            for (int i = 0; i < 64; ++i) table[(uint8_t) a[i]] = (int8_t) i;
            init = true;
        }
        Bytes out;
        out.reserve (n * 3 / 4);
        uint32_t acc = 0; int bits = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const int8_t v = table[(uint8_t) s[i]];
            if (v < 0) { if (s[i] == '=') break; continue; }
            acc = (acc << 6) | (uint32_t) v;
            bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back ((uint8_t) ((acc >> bits) & 0xFF)); }
        }
        return out;
    }

    bool adcDecompress (const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen)
    {
        size_t i = 0, o = 0;
        while (i < inLen && o < outLen)
        {
            const uint8_t b = in[i];
            if (b & 0x80)
            {
                size_t len = (size_t) (b & 0x7F) + 1;
                if (i + 1 + len > inLen) len = inLen - i - 1;
                len = std::min (len, outLen - o);
                std::memcpy (out + o, in + i + 1, len);
                o += len; i += 1 + len;
            }
            else
            {
                size_t len, dist;
                if (b & 0x40)
                {
                    if (i + 2 >= inLen) return false;
                    len = (size_t) (b & 0x3F) + 4;
                    dist = ((size_t) in[i + 1] << 8 | in[i + 2]) + 1;
                    i += 3;
                }
                else
                {
                    if (i + 1 >= inLen) return false;
                    len = (size_t) ((b & 0x3F) >> 2) + 3;
                    dist = (((size_t) (b & 0x03) << 8) | in[i + 1]) + 1;
                    i += 2;
                }
                if (dist > o) return false;
                for (size_t k = 0; k < len && o < outLen; ++k, ++o)
                    out[o] = out[o - dist];
            }
        }
        return true;
    }

    struct Writer
    {
        std::FILE* f = nullptr;
        ~Writer() { if (f) std::fclose (f); }
        bool at (uint64_t off)
        {
           #if defined (_WIN32)
            return _fseeki64 (f, (long long) off, SEEK_SET) == 0;
           #else
            return fseeko (f, (off_t) off, SEEK_SET) == 0;
           #endif
        }
    };
} // namespace

bool isDmg (Device& d)
{
    if (d.size() < 1024) return false;
    uint8_t k[4];
    return d.read (d.size() - 512, k, 4) && std::memcmp (k, "koly", 4) == 0;
}

bool convertDmgToRaw (const std::string& dmgPath, const std::string& outPath, InflateFn inflate, std::string& error)
{
    FileDevice in (dmgPath);
    if (! in.isOpen() || ! isDmg (in)) { error = "NOT A DMG FILE"; return false; }

    uint8_t koly[512];
    in.read (in.size() - 512, koly, 512);
    const uint64_t dataForkOffset = be64 (koly + 24);
    const uint64_t xmlOffset = be64 (koly + 216);
    const uint64_t xmlLength = be64 (koly + 224);
    const uint64_t sectorCount = be64 (koly + 492);

    if (xmlLength == 0 || xmlLength > 64 * 1024 * 1024 || xmlOffset + xmlLength > in.size())
    {
        error = "DMG: NO BLKX TABLE (SPARSE/OLD FORMAT)";
        return false;
    }

    Bytes xml = in.readBytes (xmlOffset, (size_t) xmlLength);
    std::string x (xml.begin(), xml.end());

    // collect mish blocks
    std::vector<Bytes> mishes;
    size_t pos = x.find ("<key>blkx</key>");
    if (pos == std::string::npos) pos = 0;
    while (true)
    {
        const size_t a = x.find ("<data>", pos);
        if (a == std::string::npos) break;
        const size_t b = x.find ("</data>", a);
        if (b == std::string::npos) break;
        Bytes m = base64Decode (x.data() + a + 6, b - a - 6);
        if (m.size() >= 204 && std::memcmp (m.data(), "mish", 4) == 0) mishes.push_back (std::move (m));
        pos = b + 7;
    }
    if (mishes.empty()) { error = "DMG: NO PARTITION DATA"; return false; }

    Writer w;
    w.f = std::fopen (outPath.c_str(), "wb");
    if (w.f == nullptr) { error = "DMG: CANNOT WRITE TEMP FILE"; return false; }

    Bytes src, dst;
    for (auto& m : mishes)
    {
        const uint64_t baseSector = be64 (m.data() + 8);
        const uint64_t dataOffset = be64 (m.data() + 24);
        const uint32_t numChunks = be32 (m.data() + 200);
        for (uint32_t c = 0; c < numChunks && 204 + (size_t) (c + 1) * 40 <= m.size(); ++c)
        {
            const uint8_t* ch = m.data() + 204 + c * 40;
            const uint32_t type = be32 (ch);
            const uint64_t sec = be64 (ch + 8), cnt = be64 (ch + 16);
            const uint64_t coff = be64 (ch + 24), clen = be64 (ch + 32);
            if (type == 0xFFFFFFFF) break;
            if (type == 0x7FFFFFFE || cnt == 0) continue;

            const uint64_t outOff = (baseSector + sec) * 512;
            const uint64_t outLen = cnt * 512;
            if (outLen > 256ull * 1024 * 1024) { error = "DMG: CHUNK TOO LARGE"; return false; }

            if (type == 0 || type == 2)
            {
                dst.assign ((size_t) outLen, 0);
            }
            else
            {
                src = in.readBytes (dataForkOffset + dataOffset + coff, (size_t) clen);
                if (src.size() != clen) { error = "DMG: TRUNCATED FILE"; return false; }
                dst.assign ((size_t) outLen, 0);
                if (type == 1)
                    std::memcpy (dst.data(), src.data(), (size_t) std::min (clen, outLen));
                else if (type == 0x80000004)
                {
                    if (! adcDecompress (src.data(), src.size(), dst.data(), dst.size())) { error = "DMG: ADC ERROR"; return false; }
                }
                else if (type == 0x80000005)
                {
                    if (! inflate || ! inflate (src.data(), src.size(), dst.data(), dst.size())) { error = "DMG: ZLIB ERROR"; return false; }
                }
                else
                {
                    error = type == 0x80000006 ? "DMG: BZIP2 NOT SUPPORTED - USE UDRW/UDZO"
                          : type == 0x80000007 ? "DMG: LZFSE NOT SUPPORTED - USE UDRW/UDZO"
                                               : "DMG: UNKNOWN COMPRESSION";
                    return false;
                }
            }
            if (! w.at (outOff) || std::fwrite (dst.data(), 1, dst.size(), w.f) != dst.size())
            {
                error = "DMG: DISK FULL";
                return false;
            }
        }
    }

    if (sectorCount > 0)
    {
        // make sure the output has the full length (without touching written data)
        std::fseek (w.f, 0, SEEK_END);
       #if defined (_WIN32)
        const uint64_t cur = (uint64_t) _ftelli64 (w.f);
       #else
        const uint64_t cur = (uint64_t) ftello (w.f);
       #endif
        if (cur < sectorCount * 512)
        {
            uint8_t z = 0;
            w.at (sectorCount * 512 - 1);
            std::fwrite (&z, 1, 1, w.f);
        }
    }
    return true;
}

} // namespace s6k::disk
