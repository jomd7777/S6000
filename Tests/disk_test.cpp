// Standalone test for the S6000 disk + format layer (no JUCE):
//   g++ -std=c++17 Tests/disk_test.cpp Source/Disk/*.cpp Source/Formats/*.cpp -lz
#include "../Source/Disk/DiskDevice.h"
#include "../Source/Formats/AkaiFormats.h"
#include <zlib.h>
#include <iostream>
#include <cmath>
using namespace s6k;

static bool inflateZ (const uint8_t* s, size_t sl, uint8_t* d, size_t dl)
{
    uLongf out = (uLongf) dl;
    return uncompress (d, &out, s, (uLong) sl) == Z_OK;
}

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::cout << "  FAIL: " #c "\n"; ++failures; } } while (0)

static void walk (disk::VNode& n, int depth, int& samples, int& programs)
{
    n.ensurePopulated();
    for (auto& c : n.children)
    {
        std::cout << std::string ((size_t) depth * 2, ' ') << (c->isContainer() ? "[" : "") << c->name
                  << (c->isContainer() ? "]" : "") << "  " << c->size << "\n";
        if (c->isContainer()) { walk (*c, depth + 1, samples, programs); continue; }
        if (! c->readContent) continue;
        auto data = c->readContent();
        CHECK (data.size() == c->size);
        std::string err;
        using K = disk::VNode::Kind;
        if (c->kind == K::S1000Sample || c->kind == K::S3000Sample)
        {
            fmt::SampleData s;
            bool ok = fmt::parseS1000Sample (data, c->kind == K::S3000Sample, s, err);
            CHECK (ok);
            if (ok) { ++samples; std::cout << std::string ((size_t) depth * 2 + 4, ' ') << "-> sample '" << s.name << "' " << s.ch[0].size() << " frames, loop " << s.loopStart << ".." << s.loopEnd << " rate " << s.sampleRate << "\n";
                      CHECK (std::abs (s.ch[0][100] - std::sin (2 * 3.14159265 * 440 * 100 / 44100.0) * 12000 / 32768) < 0.001); }
        }
        if (c->kind == K::S1000Program || c->kind == K::S3000Program)
        {
            fmt::ProgramData p;
            bool ok = fmt::parseS1000Program (data, p, err, c->kind == K::S3000Program ? 1 : 0);
            CHECK (ok && p.keygroups.size() == 1 && p.keygroups[0].zones[0].sampleName == "VIOLIN C3" && p.keygroups[0].zones[1].sampleName.empty() && p.keygroups[0].zones[3].sampleName.empty());
            CHECK (ok);
            if (ok) { ++programs; std::cout << std::string ((size_t) depth * 2 + 4, ' ') << "-> program '" << p.name << "' kgs " << p.keygroups.size() << " zone1 '" << p.keygroups[0].zones[0].sampleName << "' " << p.keygroups[0].lowNote << "-" << p.keygroups[0].highNote << "\n"; }
        }
        if (c->kind == K::Wav) { CHECK (data.size() > 44 && std::memcmp (data.data(), "RIFF", 4) == 0 && std::memcmp (data.data() + 8, "WAVE", 4) == 0); ++samples; }
        if (c->kind == K::Akp) { CHECK (std::memcmp (data.data() + 8, "APRG", 4) == 0); ++programs; }
    }
}

int main (int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "/tmp/s6kimg";
    struct T { const char* file; int expSamples; int expPrograms; int expParts; };
    T tests[] = { { "test.iso", 1, 1, 1 }, { "test.bin", 1, 1, 1 }, { "test.dmg", 1, 1, 1 },
                  { "fat16.img", 1, 1, 1 }, { "mbr.img", 1, 1, 1 },
                  { "akai_hd.img", 2, 1, 2 }, { "akai_floppy.img", 1, 0, 1 } };
    for (auto& t : tests)
    {
        std::cout << "=== " << t.file << "\n";
        std::string err;
        auto mounts = disk::mountImageFile (dir + "/" + t.file, inflateZ, err, "/tmp");
        CHECK ((int) mounts.size() == t.expParts);
        int s = 0, p = 0;
        for (auto& m : mounts) { std::cout << " <" << m.displayName << "> " << m.formatName << "\n"; walk (*m.root, 1, s, p); }
        if (! err.empty()) std::cout << " err: " << err << "\n";
        CHECK (s == t.expSamples); CHECK (p == t.expPrograms);
    }

    // AKP / AKM round trip
    fmt::ProgramData pd; pd.name = "RT"; pd.loudness = 77; pd.bendUp = 7; pd.lfo2.rate = 66;
    fmt::KeygroupData kg; kg.lowNote = 40; kg.highNote = 50; kg.cutoff = 33; kg.amp.release = 44; kg.filt.depth = -20; kg.aux.level[2] = 55;
    kg.zones[0].sampleName = "MY SAMPLE NAME 20CHR"; kg.zones[0].pan = -12; kg.zones[0].velStart = -300; kg.zones[1].sampleName = "B";
    pd.keygroups = { kg, kg };
    auto bytes = fmt::writeAkp (pd);
    fmt::ProgramData back; std::string e;
    CHECK (fmt::parseAkp (bytes, back, e));
    CHECK (back.loudness == 77 && back.bendUp == 7 && back.lfo2.rate == 66 && back.keygroups.size() == 2);
    CHECK (back.keygroups[1].lowNote == 40 && back.keygroups[1].cutoff == 33 && back.keygroups[1].amp.release == 44);
    CHECK (back.keygroups[0].filt.depth == -20 && back.keygroups[0].aux.level[2] == 55);
    CHECK (back.keygroups[0].zones[0].sampleName == "MY SAMPLE NAME 20CHR" && back.keygroups[0].zones[0].pan == -12 && back.keygroups[0].zones[0].velStart == -300);
    fmt::MultiData md; md.name = "MY MULTI"; fmt::PartData pp; pp.programName = "RT"; pp.midiChannel = 17; pp.pan = -9; md.parts = { pp, pp };
    fmt::MultiData mb; CHECK (fmt::parseAkm (fmt::writeAkm (md), mb, e));
    CHECK (mb.name == "MY MULTI" && mb.parts.size() == 2 && mb.parts[1].midiChannel == 17 && mb.parts[1].pan == -9);

    std::cout << (failures ? "FAILURES: " : "ALL OK ") << failures << "\n";
    return failures ? 1 : 0;
}
