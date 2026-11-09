/*
  ==============================================================================
   S6000 Emulation - Akai file formats
   (c) dominkHop  -  github.com/jomd7777/S6000
  ==============================================================================
*/
#include "AkaiFormats.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace s6k::fmt
{
namespace
{
    inline uint16_t le16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    inline uint32_t le32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline int s8 (uint8_t v) { return (int) (int8_t) v; }
    inline uint8_t u8c (int v, int lo, int hi) { return (uint8_t) (int8_t) std::clamp (v, lo, hi); }

    void put32 (Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
    void putId (Bytes& b, const char* id) { b.insert (b.end(), id, id + 4); }

    std::string trimRight (std::string s)
    {
        while (! s.empty() && ((unsigned char) s.back() <= ' ')) s.pop_back();
        return s;
    }

    struct Chunk { char id[5] = {}; const uint8_t* data = nullptr; uint32_t size = 0; };

    std::vector<Chunk> listChunks (const uint8_t* p, size_t n)
    {
        std::vector<Chunk> out;
        size_t pos = 0;
        while (pos + 8 <= n)
        {
            Chunk c;
            std::memcpy (c.id, p + pos, 4);
            c.size = le32 (p + pos + 4);
            c.data = p + pos + 8;
            if (pos + 8 + c.size > n) c.size = (uint32_t) (n - pos - 8);
            out.push_back (c);
            pos += 8 + c.size + (c.size & 1 ? 0 : 0);
        }
        return out;
    }

    inline uint8_t at (const Chunk& c, size_t i, uint8_t def = 0) { return i < c.size ? c.data[i] : def; }

    std::string readCString (const uint8_t* p, size_t maxLen)
    {
        std::string s;
        for (size_t i = 0; i < maxLen && p[i] != 0; ++i)
            s.push_back ((p[i] >= 32 && p[i] < 127) ? (char) p[i] : '_');
        return trimRight (s);
    }

    void putName (Bytes& b, const std::string& s, size_t len)
    {
        for (size_t i = 0; i < len; ++i) b.push_back (i < s.size() ? (uint8_t) s[i] : 0);
    }
} // namespace

//==============================================================================
std::string akaiToAscii (const uint8_t* p, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t c = p[i];
        if (c <= 9) s.push_back ((char) ('0' + c));
        else if (c == 10) s.push_back (' ');
        else if (c <= 36) s.push_back ((char) ('A' + c - 11));
        else if (c == 37) s.push_back ('#');
        else if (c == 38) s.push_back ('+');
        else if (c == 39) s.push_back ('-');
        else if (c == 40) s.push_back ('.');
        else s.push_back ('_');
    }
    return s;
}

void asciiToAkai (const std::string& s, uint8_t* out, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        char c = i < s.size() ? (char) std::toupper ((unsigned char) s[i]) : ' ';
        uint8_t v = 10;
        if (c >= '0' && c <= '9') v = (uint8_t) (c - '0');
        else if (c >= 'A' && c <= 'Z') v = (uint8_t) (11 + c - 'A');
        else if (c == '#') v = 37; else if (c == '+') v = 38; else if (c == '-') v = 39; else if (c == '.') v = 40;
        out[i] = v;
    }
}

bool isValidAkaiName (const uint8_t* p, size_t n)
{
    bool anyNonSpace = false;
    for (size_t i = 0; i < n; ++i)
    {
        if (p[i] > 40) return false;
        if (p[i] != 10) anyNonSpace = true;
    }
    return anyNonSpace;
}

bool isS1000SampleHeader (const uint8_t* p, size_t n)
{
    if (n < 150 || p[0] != 3 || p[1] > 1) return false;
    if (p[2] < 12 || p[2] > 127) return false;
    if (! isValidAkaiName (p + 3, 12)) return false;
    const uint32_t words = le32 (p + 0x1A), start = le32 (p + 0x1E), end = le32 (p + 0x22);
    if (words < 2 || words > 0x2000000) return false;
    if (start > words || end > words) return false;
    const uint32_t rate = le16 (p + 0x8A);
    if (rate != 0 && (rate < 4000 || rate > 50000)) return false;
    return true;
}

bool isS1000ProgramHeader (const uint8_t* p, size_t n)
{
    if (n < 300 || p[0] != 1) return false;
    if (! isValidAkaiName (p + 3, 12)) return false;
    return p[150] == 2 || (n >= 384 && p[192] == 2);
}

//==============================================================================
bool parseS1000Sample (const Bytes& f, bool isS3000, SampleData& s, std::string& error)
{
    if (! isS1000SampleHeader (f.data(), f.size())) { error = "NOT AN AKAI SAMPLE"; return false; }
    const uint8_t* p = f.data();
    uint32_t words = le32 (p + 0x1A);

    size_t hdr = isS3000 ? 192 : 150;
    // tolerate either header size by checking the file length
    if (f.size() == 192 + (size_t) words * 2) hdr = 192;
    else if (f.size() == 150 + (size_t) words * 2) hdr = 150;

    const size_t avail = f.size() > hdr ? (f.size() - hdr) / 2 : 0;
    words = (uint32_t) std::min<size_t> (words, avail);
    if (words == 0) { error = "EMPTY SAMPLE"; return false; }

    s.name = trimRight (akaiToAscii (p + 3, 12));
    s.rootNote = p[2];
    s.cents = s8 (p[0x14]);
    s.semitone = s8 (p[0x15]);
    uint32_t rate = le16 (p + 0x8A);
    if (rate == 0) rate = p[1] ? 44100 : 22050;
    s.sampleRate = rate;
    s.numChannels = 1;
    s.ch[0].resize (words);
    for (uint32_t i = 0; i < words; ++i)
        s.ch[0][i] = (float) (int16_t) le16 (p + hdr + i * 2) / 32768.0f;

    s.start = std::min<int64_t> (le32 (p + 0x1E), words);
    s.end = le32 (p + 0x22);
    if (s.end <= s.start || s.end > words) s.end = words;

    const int loops = p[0x10];
    const int first = std::min<int> (p[0x11], 7);
    const uint8_t* lp = p + 0x26 + first * 12;
    const uint32_t loopAt = le32 (lp), loopLen = le32 (lp + 6);
    switch (p[0x13])
    {
        case 0: s.playMode = PlayLoopInRelease; break;
        case 1: s.playMode = PlayLoopUntilRelease; break;
        case 2: s.playMode = PlayNoLoop; break;
        default: s.playMode = PlayOneShot; break;
    }
    if (loops > 0 && loopLen > 0 && loopAt <= words && loopLen <= loopAt && p[0x13] < 2)
    {
        s.loopEnabled = true;
        s.loopEnd = loopAt;
        s.loopStart = loopAt - loopLen;
    }
    else if (s.playMode != PlayOneShot)
        s.playMode = PlayNoLoop;
    return true;
}

//==============================================================================
bool parseS1000Program (const Bytes& f, ProgramData& prg, std::string& error, int isS3000)
{
    if (! isS1000ProgramHeader (f.data(), f.size())) { error = "NOT AN AKAI PROGRAM"; return false; }
    const uint8_t* p = f.data();

    // record size: S1000 = 150 bytes, S3000 = 192 bytes (header and every keygroup)
    size_t stride;
    const bool kg150 = f.size() >= 300 && p[150] == 2;
    const bool kg192 = f.size() >= 384 && p[192] == 2;
    if (isS3000 == 1)      stride = kg192 ? 192 : 150;
    else if (isS3000 == 0) stride = kg150 ? 150 : 192;
    else                   stride = kg150 ? 150 : 192;

    // GROUPS (number of keygroups) lives at offset 42 of the program header
    const int declared = p[42];
    const size_t maxKgs = (declared >= 1 && declared <= 99) ? (size_t) declared : 99;

    // ---- program header (S1000 / S3000 layout)
    //  15 PRGNUM  17 POLYPH  25 PRLOUD  26 V_LOUD  33 LFORAT  34 LFODEP  35 LFODEL
    //  36 MWLDEP  37 PRSDEP  38 VELDEP  39 B_PTCH  41 KXFADE  42 GROUPS
    prg = ProgramData();
    prg.name = trimRight (akaiToAscii (p + 3, 12));
    prg.midiProgram = std::min<int> (p[15], 127);
    prg.polyphony = std::clamp<int> (p[17] + 1, 1, 128);
    prg.loudness = std::min (100, (int) p[25] * 100 / 99);
    prg.velSens = std::clamp (s8 (p[26]) * 2, -100, 100);          // velocity -> loudness
    prg.lfo1.waveform = 1;                                           // S1000 LFO is a triangle
    prg.lfo1.rate = std::min (100, (int) p[33] * 100 / 99);
    prg.lfo1.depth = std::min (100, (int) p[34] * 100 / 99);
    prg.lfo1.delay = std::min (100, (int) p[35] * 100 / 99);
    prg.lfo1.modwheel = std::min (100, (int) p[36] * 100 / 99);
    prg.lfo1.aftertouch = std::min (100, (int) p[37] * 100 / 99);
    prg.bendUp = prg.bendDown = std::clamp<int> (p[39], 0, 24);
    prg.keygroupXfade = p[41] != 0;
    prg.modSource[ProgramData::PitchMod1] = 9;                       // LFO1 -> pitch (vibrato)
    prg.modSource[ProgramData::FiltMod1] = 5;                        // velocity -> filter
    prg.modSource[ProgramData::FiltMod2] = 8;                        // keyboard -> filter
    prg.modSource[ProgramData::FiltMod3] = 4;                        // aftertouch -> filter
    prg.keygroups.clear();

    for (size_t off = stride; off + 0x22 + 4 * 24 <= f.size() && p[off] == 2 && prg.keygroups.size() < maxKgs; off += stride)
    {
        // ---- keygroup
        //  3 LONOTE 4 HINOTE 5 tune cents 6 tune semi 7 FILFRQ 8 K_FREQ 9 V_FREQ 10 P_FREQ 11 E_FREQ
        //  12..15 ENV1 A D S R  16 V_ATT1 17 V_REL1 18 O_REL1 19 K_DAR1
        //  20..23 ENV2 A D S R  24 V_ATT2 ...  30 VXFADE 31 VZONES  34 zones (4 x 24 bytes)
        const uint8_t* k = p + off;
        KeygroupData kg;
        auto pct = [] (uint8_t v) { return std::min (100, (int) v * 100 / 99); };
        kg.lowNote = std::clamp<int> (k[3], 0, 127);
        kg.highNote = std::clamp<int> (k[4], kg.lowNote, 127);
        kg.fine = std::clamp (s8 (k[5]), -50, 50);
        kg.semi = std::clamp (s8 (k[6]), -36, 36);
        kg.filterMode = 0;
        kg.cutoff = pct (k[7]);
        kg.filterKeyTrack = std::clamp (s8 (k[8]) * 36 / 12, -36, 36);
        kg.filterMod1 = std::clamp (s8 (k[9]) * 2, -100, 100);
        kg.filterMod3 = std::clamp (s8 (k[10]) * 2, -100, 100);
        kg.filt.depth = std::clamp (s8 (k[11]) * 2, -100, 100);
        kg.amp.attack  = pct (k[12]);
        kg.amp.decay   = pct (k[13]);
        kg.amp.sustain = pct (k[14]);
        kg.amp.release = pct (k[15]);
        kg.amp.velAttack = std::clamp (s8 (k[16]) * 2, -100, 100);
        kg.amp.onVelRelease = std::clamp (s8 (k[17]) * 2, -100, 100);
        kg.amp.offVelRelease = std::clamp (s8 (k[18]) * 2, -100, 100);
        kg.amp.keyscale = std::clamp (s8 (k[19]) * 2, -100, 100);
        kg.filt.attack  = pct (k[20]);
        kg.filt.decay   = pct (k[21]);
        kg.filt.sustain = pct (k[22]);
        kg.filt.release = pct (k[23]);
        kg.filt.velAttack = std::clamp (s8 (k[24]) * 2, -100, 100);
        kg.zoneXfade = k[30] != 0;
        kg.pitchMod1 = prg.lfo1.depth > 0 || prg.lfo1.modwheel > 0 ? 50 : 0;   // vibrato routing

        // number of velocity zones actually used: zones beyond it hold leftover names
        // (factory test waves SQUARE / SAWTOOTH / PULSE) and must NOT be loaded
        const int vzones = k[31];
        const int usedZones = (vzones >= 1 && vzones <= 4) ? vzones : 4;

        for (int z = 0; z < usedZones; ++z)
        {
            const uint8_t* zp = k + 0x22 + z * 24;
            if (zp + 24 > p + f.size()) break;
            if (! isValidAkaiName (zp, 12)) continue;
            ZoneData& zd = kg.zones[z];
            zd.sampleName = trimRight (akaiToAscii (zp, 12));
            zd.lowVel = std::min<int> (zp[12], 127);
            zd.highVel = std::clamp<int> (zp[13], zd.lowVel, 127);
            zd.fine = std::clamp (s8 (zp[14]), -50, 50);
            zd.semi = std::clamp (s8 (zp[15]), -36, 36);
            zd.level = std::clamp (s8 (zp[16]) / 4, -12, 12);         // +-50 -> +-12 dB
            zd.filter = std::clamp (s8 (zp[17]) * 2, -100, 100);
            zd.pan = std::clamp (s8 (zp[18]), -50, 50);
            switch (zp[19])
            {
                case 1:  zd.playback = PlayLoopInRelease; break;
                case 2:  zd.playback = PlayLoopUntilRelease; break;
                case 3:  zd.playback = PlayNoLoop; break;
                case 4:  zd.playback = PlayOneShot; break;
                default: zd.playback = PlayAsSample; break;
            }
        }
        prg.keygroups.push_back (kg);
    }
    if (prg.keygroups.empty()) { error = "PROGRAM HAS NO KEYGROUPS"; return false; }
    return true;
}

//==============================================================================
//  .AKP  (RIFF 'APRG')
//    prg  : [1] midi program  [2] num keygroups
//    out  : [1] loudness [2] ampmod1 [3] ampmod2 [4..6] panmod1..3 [7] velsens
//    tune : [1] semi [2] fine [3..14] user detune C..B [15] bend up [16] bend down
//           [17] bend mode [18] aftertouch  [19] template [20] key
//    lfo  : [1] wave [2] rate [3] delay [4] depth [5] sync/retrig [7] modwheel
//           [8] aftertouch [9] rate mod [10] delay mod [11] depth mod
//    kgrp : kloc(16) env(18)x3 filt(10) zone(48)x4
//==============================================================================
bool parseAkp (const Bytes& f, ProgramData& prg, std::string& error)
{
    if (f.size() < 12 || std::memcmp (f.data(), "RIFF", 4) != 0 || std::memcmp (f.data() + 8, "APRG", 4) != 0)
    {
        error = "NOT AN AKP FILE";
        return false;
    }
    prg = ProgramData();
    prg.keygroups.clear();
    int lfoIndex = 0;

    for (auto& c : listChunks (f.data() + 12, f.size() - 12))
    {
        const std::string id (c.id, 4);
        if (id == "prg ")
        {
            prg.midiProgram = at (c, 1);
            prg.keygroupXfade = at (c, 3) != 0;
            prg.polyphony = at (c, 4) > 0 ? at (c, 4) : 32;
        }
        else if (id == "mods")
        {
            for (int i = 0; i < ProgramData::NumModInputs && (size_t) (2 + i * 2) < c.size; ++i)
                prg.modSource[i] = std::min<int> (at (c, (size_t) (2 + i * 2)), 14);
        }
        else if (id == "out ")
        {
            prg.loudness = at (c, 1, 85); prg.ampMod1 = at (c, 2); prg.ampMod2 = at (c, 3);
            prg.panMod1 = s8 (at (c, 4)); prg.panMod2 = s8 (at (c, 5)); prg.panMod3 = s8 (at (c, 6));
            prg.velSens = s8 (at (c, 7, 25));
        }
        else if (id == "tune")
        {
            prg.semi = s8 (at (c, 1)); prg.fine = s8 (at (c, 2));
            for (int i = 0; i < 12; ++i) prg.userTune[i] = s8 (at (c, (size_t) 3 + i));
            prg.bendUp = at (c, 15, 2); prg.bendDown = at (c, 16, 2);
            prg.bendMode = at (c, 17); prg.aftertouchBend = s8 (at (c, 18));
            prg.tuneTemplate = at (c, 19); prg.key = at (c, 20);
        }
        else if (id == "lfo ")
        {
            LfoData& l = lfoIndex++ == 0 ? prg.lfo1 : prg.lfo2;
            l.waveform = std::min<int> (at (c, 1, 1), 8); l.rate = at (c, 2, 43); l.delay = at (c, 3); l.depth = at (c, 4);
            l.syncOrRetrigger = at (c, 5); l.modwheel = at (c, 7); l.aftertouch = at (c, 8);
            l.rateMod = s8 (at (c, 9)); l.delayMod = s8 (at (c, 10)); l.depthMod = s8 (at (c, 11));
        }
        else if (id == "kgrp")
        {
            KeygroupData kg;
            int envIndex = 0, zoneIndex = 0;
            for (auto& s : listChunks (c.data, c.size))
            {
                const std::string sid (s.id, 4);
                if (sid == "kloc")
                {
                    kg.lowNote = std::clamp<int> (at (s, 4, 21), 0, 127);
                    kg.highNote = std::clamp<int> (at (s, 5, 127), kg.lowNote, 127);
                    kg.semi = s8 (at (s, 6)); kg.fine = s8 (at (s, 7));
                    kg.fxOverride = at (s, 8); kg.fxSend = at (s, 9);
                    kg.pitchMod1 = s8 (at (s, 10)); kg.pitchMod2 = s8 (at (s, 11)); kg.ampMod = s8 (at (s, 12));
                    kg.zoneXfade = at (s, 13) != 0; kg.muteGroup = at (s, 14);
                }
                else if (sid == "env ")
                {
                    if (envIndex == 2)
                    {
                        AuxEnvData& a = kg.aux;
                        for (int i = 0; i < 4; ++i) { a.rate[i] = at (s, (size_t) 1 + i); a.level[i] = at (s, (size_t) 5 + i); }
                        a.velRate1 = s8 (at (s, 10)); a.keyRate2 = s8 (at (s, 11)); a.keyRate4 = s8 (at (s, 12)); a.velLevel = s8 (at (s, 13));
                    }
                    else
                    {
                        EnvData& e = envIndex == 0 ? kg.amp : kg.filt;
                        e.attack = at (s, 1); e.decay = at (s, 3, 50); e.release = at (s, 4, 15); e.sustain = at (s, 7, 100);
                        e.velAttack = s8 (at (s, 8)); e.depth = s8 (at (s, 9)); e.keyscale = s8 (at (s, 10));
                        e.onVelRelease = s8 (at (s, 12)); e.offVelRelease = s8 (at (s, 13));
                    }
                    ++envIndex;
                }
                else if (sid == "filt")
                {
                    kg.filterMode = std::min<int> (at (s, 1), 25); kg.cutoff = std::min<int> (at (s, 2, 100), 100);
                    kg.resonance = std::min<int> (at (s, 3), 12); kg.filterKeyTrack = s8 (at (s, 4));
                    kg.filterMod1 = s8 (at (s, 5)); kg.filterMod2 = s8 (at (s, 6)); kg.filterMod3 = s8 (at (s, 7));
                    kg.attenuation = std::min<int> (at (s, 8), 5);
                }
                else if (sid == "zone" && zoneIndex < 4)
                {
                    ZoneData& z = kg.zones[zoneIndex++];
                    const size_t len = std::min<size_t> (at (s, 1), 20);
                    if (s.size >= 2 + len) z.sampleName = readCString (s.data + 2, len);
                    z.lowVel = at (s, 34, 0); z.highVel = std::max<int> (at (s, 35, 127), z.lowVel);
                    z.fine = s8 (at (s, 36)); z.semi = s8 (at (s, 37)); z.filter = s8 (at (s, 38));
                    z.pan = s8 (at (s, 39)); z.playback = std::min<int> (at (s, 40, 4), 4); z.output = at (s, 41);
                    z.level = s8 (at (s, 42)); z.keyTrack = at (s, 43, 1) != 0;
                    z.velStart = (int16_t) (s.size >= 46 ? le16 (s.data + 44) : 0);
                }
            }
            if (prg.keygroups.size() < 99) prg.keygroups.push_back (kg);
        }
    }
    if (prg.keygroups.empty()) prg.keygroups.push_back (KeygroupData());
    return true;
}

Bytes writeAkp (const ProgramData& p)
{
    Bytes body;
    auto chunk = [&body] (const char* id, const Bytes& d)
    {
        putId (body, id); put32 (body, (uint32_t) d.size());
        body.insert (body.end(), d.begin(), d.end());
    };

    chunk ("prg ", { 1, (uint8_t) p.midiProgram, (uint8_t) p.keygroups.size(), (uint8_t) (p.keygroupXfade ? 1 : 0), (uint8_t) p.polyphony, 0 });
    chunk ("out ", { 1, (uint8_t) p.loudness, (uint8_t) p.ampMod1, (uint8_t) p.ampMod2, u8c (p.panMod1, -50, 50),
                     u8c (p.panMod2, -50, 50), u8c (p.panMod3, -50, 50), u8c (p.velSens, -100, 100) });
    {
        Bytes t (24, 0);
        t[0] = 1; t[1] = u8c (p.semi, -36, 36); t[2] = u8c (p.fine, -50, 50);
        for (int i = 0; i < 12; ++i) t[(size_t) 3 + i] = u8c (p.userTune[i], -50, 50);
        t[15] = (uint8_t) p.bendUp; t[16] = (uint8_t) p.bendDown; t[17] = (uint8_t) p.bendMode;
        t[18] = u8c (p.aftertouchBend, -12, 12); t[19] = (uint8_t) p.tuneTemplate; t[20] = (uint8_t) p.key;
        chunk ("tune", t);
    }
    for (const LfoData* l : { &p.lfo1, &p.lfo2 })
    {
        Bytes d (14, 0);
        d[0] = 1; d[1] = (uint8_t) l->waveform; d[2] = (uint8_t) l->rate; d[3] = (uint8_t) l->delay; d[4] = (uint8_t) l->depth;
        d[5] = (uint8_t) l->syncOrRetrigger; d[7] = (uint8_t) l->modwheel; d[8] = (uint8_t) l->aftertouch;
        d[9] = u8c (l->rateMod, -100, 100); d[10] = u8c (l->delayMod, -100, 100); d[11] = u8c (l->depthMod, -100, 100);
        chunk ("lfo ", d);
    }
    {
        Bytes m (38, 0);
        m[0] = 1;
        for (int i = 0; i < ProgramData::NumModInputs; ++i) m[(size_t) (2 + i * 2)] = (uint8_t) p.modSource[i];
        chunk ("mods", m);
    }
    for (auto& kg : p.keygroups)
    {
        Bytes k;
        auto sub = [&k] (const char* id, const Bytes& d)
        {
            putId (k, id); put32 (k, (uint32_t) d.size());
            k.insert (k.end(), d.begin(), d.end());
        };
        {
            Bytes d (16, 0);
            d[0] = 1; d[4] = (uint8_t) kg.lowNote; d[5] = (uint8_t) kg.highNote; d[6] = u8c (kg.semi, -36, 36); d[7] = u8c (kg.fine, -50, 50);
            d[8] = (uint8_t) kg.fxOverride; d[9] = (uint8_t) kg.fxSend; d[10] = u8c (kg.pitchMod1, -100, 100); d[11] = u8c (kg.pitchMod2, -100, 100);
            d[12] = u8c (kg.ampMod, -100, 100); d[13] = kg.zoneXfade ? 1 : 0; d[14] = (uint8_t) kg.muteGroup;
            sub ("kloc", d);
        }
        for (const EnvData* e : { &kg.amp, &kg.filt })
        {
            Bytes d (18, 0);
            d[0] = 1; d[1] = (uint8_t) e->attack; d[3] = (uint8_t) e->decay; d[4] = (uint8_t) e->release; d[7] = (uint8_t) e->sustain;
            d[8] = u8c (e->velAttack, -100, 100); d[9] = u8c (e->depth, -100, 100); d[10] = u8c (e->keyscale, -100, 100);
            d[12] = u8c (e->onVelRelease, -100, 100); d[13] = u8c (e->offVelRelease, -100, 100);
            sub ("env ", d);
        }
        {
            Bytes d (18, 0);
            d[0] = 1;
            for (int i = 0; i < 4; ++i) { d[(size_t) 1 + i] = (uint8_t) kg.aux.rate[i]; d[(size_t) 5 + i] = (uint8_t) kg.aux.level[i]; }
            d[10] = u8c (kg.aux.velRate1, -100, 100); d[11] = u8c (kg.aux.keyRate2, -100, 100);
            d[12] = u8c (kg.aux.keyRate4, -100, 100); d[13] = u8c (kg.aux.velLevel, -100, 100);
            sub ("env ", d);
        }
        {
            Bytes d (10, 0);
            d[0] = 1; d[1] = (uint8_t) kg.filterMode; d[2] = (uint8_t) kg.cutoff; d[3] = (uint8_t) kg.resonance;
            d[4] = u8c (kg.filterKeyTrack, -36, 36); d[5] = u8c (kg.filterMod1, -100, 100); d[6] = u8c (kg.filterMod2, -100, 100);
            d[7] = u8c (kg.filterMod3, -100, 100); d[8] = (uint8_t) kg.attenuation;
            sub ("filt", d);
        }
        for (auto& z : kg.zones)
        {
            Bytes d (48, 0);
            d[0] = 1;
            const size_t len = std::min<size_t> (z.sampleName.size(), 20);
            d[1] = (uint8_t) len;
            for (size_t i = 0; i < len; ++i) d[2 + i] = (uint8_t) z.sampleName[i];
            d[34] = (uint8_t) z.lowVel; d[35] = (uint8_t) z.highVel; d[36] = u8c (z.fine, -50, 50); d[37] = u8c (z.semi, -36, 36);
            d[38] = u8c (z.filter, -100, 100); d[39] = u8c (z.pan, -50, 50); d[40] = (uint8_t) z.playback; d[41] = (uint8_t) z.output;
            d[42] = u8c (z.level, -100, 100); d[43] = z.keyTrack ? 1 : 0;
            d[44] = (uint8_t) (z.velStart & 0xFF); d[45] = (uint8_t) ((z.velStart >> 8) & 0xFF);
            sub ("zone", d);
        }
        chunk ("kgrp", k);
    }

    Bytes out;
    putId (out, "RIFF"); put32 (out, (uint32_t) body.size() + 4); putId (out, "APRG");
    out.insert (out.end(), body.begin(), body.end());
    return out;
}

//==============================================================================
//  .AKM  (RIFF 'AMLT')   mlti: [1..20] name  [21] number of parts
//                         part: [1..20] program  [22] midi ch  [23] mute
//                               [24] level [25] pan [26] output [27] fx ch
//                               [28] fx send [29] semi [30] fine [31] low [32] high
//==============================================================================
bool parseAkm (const Bytes& f, MultiData& m, std::string& error)
{
    if (f.size() < 12 || std::memcmp (f.data(), "RIFF", 4) != 0 || std::memcmp (f.data() + 8, "AMLT", 4) != 0)
    {
        error = "NOT AN AKM FILE";
        return false;
    }
    m = MultiData();
    int idx = 0;
    for (auto& c : listChunks (f.data() + 12, f.size() - 12))
    {
        const std::string id (c.id, 4);
        if (id == "mlti" && c.size > 21)
            m.name = readCString (c.data + 1, 20);
        else if (id == "mfx ")
            m.fxBlob.assign (c.data, c.data + c.size);
        else if (id == "part" && c.size > 21)
        {
            PartData pd;
            pd.programName = readCString (c.data + 1, 20);
            const int ch = at (c, 22, (uint8_t) idx);
            pd.midiChannel = ch < 32 ? ch : idx % 32;
            pd.mute = at (c, 23) != 0;
            pd.level = std::min<int> (at (c, 24, 80), 100);
            pd.pan = std::clamp (s8 (at (c, 25)), -50, 50);
            pd.output = at (c, 26); pd.fxChannel = std::min<int> (at (c, 27), 4); pd.fxSend = std::min<int> (at (c, 28, 25), 100);
            pd.semi = s8 (at (c, 29)); pd.fine = s8 (at (c, 30));
            pd.lowKey = std::min<int> (at (c, 31, 21), 127); pd.highKey = std::clamp<int> (at (c, 32, 127), pd.lowKey, 127);
            if (! pd.programName.empty()) m.parts.push_back (pd);
            ++idx;
        }
    }
    return true;
}

Bytes writeAkm (const MultiData& m)
{
    Bytes body;
    auto chunk = [&body] (const char* id, const Bytes& d)
    {
        putId (body, id); put32 (body, (uint32_t) d.size());
        body.insert (body.end(), d.begin(), d.end());
    };
    {
        Bytes d; d.push_back (1); putName (d, m.name, 20); d.push_back ((uint8_t) m.parts.size());
        d.resize (24, 0);
        chunk ("mlti", d);
    }
    for (auto& p : m.parts)
    {
        Bytes d; d.push_back (1); putName (d, p.programName, 20);
        d.resize (36, 0);
        d[22] = (uint8_t) p.midiChannel; d[23] = p.mute ? 1 : 0; d[24] = (uint8_t) p.level; d[25] = u8c (p.pan, -50, 50);
        d[26] = (uint8_t) p.output; d[27] = (uint8_t) p.fxChannel; d[28] = (uint8_t) p.fxSend;
        d[29] = u8c (p.semi, -36, 36); d[30] = u8c (p.fine, -50, 50); d[31] = (uint8_t) p.lowKey; d[32] = (uint8_t) p.highKey;
        chunk ("part", d);
    }
    if (! m.fxBlob.empty()) chunk ("mfx ", m.fxBlob);
    Bytes out;
    putId (out, "RIFF"); put32 (out, (uint32_t) body.size() + 4); putId (out, "AMLT");
    out.insert (out.end(), body.begin(), body.end());
    return out;
}

} // namespace s6k::fmt
