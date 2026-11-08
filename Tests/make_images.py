#!/usr/bin/env python3
"""Generates small test disk images for the S6000 disk layer:
   ISO9660, raw 2352 BIN, FAT16 (super floppy + MBR), Akai S1000/S3000 HD (2 partitions),
   Akai S1000 HD floppy and a zlib (UDZO-style) .dmg built from the ISO."""
import struct, math, zlib, base64, sys, os

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)

def akai_name(s, n=12):
    r = []
    for ch in s.upper().ljust(n)[:n]:
        if ch.isdigit(): r.append(ord(ch) - 48)
        elif 'A' <= ch <= 'Z': r.append(11 + ord(ch) - 65)
        elif ch == '#': r.append(37)
        elif ch == '+': r.append(38)
        elif ch == '-': r.append(39)
        elif ch == '.': r.append(40)
        else: r.append(10)
    return bytes(r)

def pcm(n, freq=440.0, rate=44100):
    return b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * freq * i / rate))) for i in range(n))

def s1000_sample(name, n=3000, s3000=False):
    hdr = bytearray(192 if s3000 else 150)
    hdr[0] = 3; hdr[1] = 1; hdr[2] = 60
    hdr[3:15] = akai_name(name)
    hdr[0x0F] = 0x80; hdr[0x10] = 1; hdr[0x11] = 0; hdr[0x13] = 0
    struct.pack_into("<I", hdr, 0x1A, n)
    struct.pack_into("<I", hdr, 0x1E, 0)
    struct.pack_into("<I", hdr, 0x22, n)
    struct.pack_into("<I", hdr, 0x26, n)          # loop at (end)
    struct.pack_into("<I", hdr, 0x26 + 6, n // 2) # loop length
    struct.pack_into("<H", hdr, 0x8A, 44100)
    return bytes(hdr) + pcm(n)

def s1000_program(name, sample, s3000=False):
    stride = 192 if s3000 else 150
    hdr = bytearray(stride); hdr[0] = 1; hdr[3:15] = akai_name(name)
    hdr[42] = 1                                  # GROUPS = 1 keygroup
    if s3000: hdr[150] = 2                       # trap: S3000 header byte 150 looks like a keygroup id
    kg = bytearray(stride); kg[0] = 2; kg[3] = 36; kg[4] = 96
    kg[0x0C] = 5; kg[0x0D] = 40; kg[0x0E] = 99; kg[0x0F] = 30
    kg[0x22:0x2E] = akai_name(sample); kg[0x2E] = 0; kg[0x2F] = 127
    kg[31] = 1                                    # VZONES: one zone used
    for z, leftover in zip(range(1, 4), ["SQUARE", "SAWTOOTH", "PULSE"]):
        kg[0x22 + z * 24: 0x2E + z * 24] = akai_name(leftover)   # factory leftovers must be ignored
    junk = bytearray(stride); junk[0] = 2; junk[0x22:0x2E] = akai_name("PULSE")   # leftover data after the program
    return bytes(hdr) + bytes(kg) + bytes(junk)

def wav(n=2000):
    data = pcm(n)
    fmt = struct.pack("<HHIIHH", 1, 1, 44100, 88200, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body

# ---------------------------------------------------------------- ISO9660
def iso_dirrec(name, lba, size, isdir):
    nm = name.encode()
    ln = 33 + len(nm) + (1 if len(nm) % 2 == 0 else 0)
    r = bytearray(ln)
    r[0] = ln
    struct.pack_into("<I", r, 2, lba); struct.pack_into(">I", r, 6, lba)
    struct.pack_into("<I", r, 10, size); struct.pack_into(">I", r, 14, size)
    r[18:25] = bytes([99, 8, 24, 12, 30, 0, 0])
    r[25] = 2 if isdir else 0
    struct.pack_into("<H", r, 28, 1); struct.pack_into(">H", r, 30, 1)
    r[32] = len(nm); r[33:33 + len(nm)] = nm
    return bytes(r)

def make_iso():
    S = 2048
    w = wav(); akp_placeholder = b"RIFF\x04\x00\x00\x00APRG"
    sectors = {}
    root_lba, sub_lba, wav_lba, akp_lba = 18, 19, 20, 20 + (len(w) + S - 1) // S
    sub = iso_dirrec("\0", sub_lba, S, True) + iso_dirrec("\1", root_lba, S, True) + iso_dirrec("TEST.WAV;1", wav_lba, len(w), False)
    root = iso_dirrec("\0", root_lba, S, True) + iso_dirrec("\1", root_lba, S, True) + iso_dirrec("STRINGS", sub_lba, S, True) + iso_dirrec("PIANO.AKP;1", akp_lba, len(akp_placeholder), False)
    total = akp_lba + 1
    img = bytearray(total * S)
    pvd = bytearray(S); pvd[0] = 1; pvd[1:6] = b"CD001"; pvd[6] = 1
    pvd[40:72] = b"S6000 TEST CD".ljust(32)
    struct.pack_into("<I", pvd, 80, total); struct.pack_into(">I", pvd, 84, total)
    struct.pack_into("<H", pvd, 128, S); struct.pack_into(">H", pvd, 130, S)
    pvd[156:156 + 34] = iso_dirrec("\0", root_lba, S, True)
    term = bytearray(S); term[0] = 255; term[1:6] = b"CD001"; term[6] = 1
    img[16 * S:17 * S] = pvd; img[17 * S:18 * S] = term
    img[root_lba * S:root_lba * S + len(root)] = root
    img[sub_lba * S:sub_lba * S + len(sub)] = sub
    img[wav_lba * S:wav_lba * S + len(w)] = w
    img[akp_lba * S:akp_lba * S + len(akp_placeholder)] = akp_placeholder
    return bytes(img)

# ---------------------------------------------------------------- FAT16
def make_fat16():
    bps, spc, res, nf, rootent, tot = 512, 4, 1, 2, 512, 32768
    fatsz = 32
    bs = bytearray(512)
    bs[0:3] = b"\xEB\x3C\x90"; bs[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHI", bs, 11, bps, spc, res, nf, rootent, tot, 0xF8, fatsz, 32, 2, 0)
    bs[43:54] = b"S6000 DISK "; bs[54:62] = b"FAT16   "
    bs[510] = 0x55; bs[511] = 0xAA
    img = bytearray(tot * bps)
    img[0:512] = bs
    fat = bytearray(fatsz * bps)
    struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)
    root_off = (res + nf * fatsz) * bps
    data_off = root_off + rootent * 32
    cb = bps * spc
    def clus_off(c): return data_off + (c - 2) * cb
    # SAMPLES dir at cluster 2; KICK.WAV fragmented: clusters 3,5 ; long name file in root at 7
    w = wav(1500)  # 3000+44 bytes -> 2 clusters
    struct.pack_into("<H", fat, 2 * 2, 0xFFFF)
    struct.pack_into("<H", fat, 3 * 2, 5); struct.pack_into("<H", fat, 5 * 2, 0xFFFF)
    img[clus_off(3):clus_off(3) + cb] = w[:cb]
    img[clus_off(5):clus_off(5) + len(w) - cb] = w[cb:]
    def entry(name83, attr, clus, size):
        e = bytearray(32); e[0:11] = name83; e[11] = attr
        struct.pack_into("<H", e, 26, clus); struct.pack_into("<I", e, 28, size)
        return e
    sub = entry(b".          ", 0x10, 2, 0) + entry(b"..         ", 0x10, 0, 0) + entry(b"KICK    WAV", 0x20, 3, len(w))
    img[clus_off(2):clus_off(2) + len(sub)] = sub
    # long file name entry "My Long Program.AKP"
    lname = "My Long Program.AKP"
    short = b"MYLONG~1AKP"
    chk = 0
    for c in short: chk = (((chk & 1) << 7) + (chk >> 1) + c) & 0xFF
    chars = [ord(c) for c in lname] + [0]
    while len(chars) % 13: chars.append(0xFFFF)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    lfn = b""
    for i in range(len(parts) - 1, -1, -1):
        e = bytearray(32); e[0] = (i + 1) | (0x40 if i == len(parts) - 1 else 0); e[11] = 0x0F; e[13] = chk
        p = parts[i]
        for k, o in enumerate([1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]):
            struct.pack_into("<H", e, o, p[k])
        lfn += bytes(e)
    akp = b"RIFF\x04\x00\x00\x00APRG"
    struct.pack_into("<H", fat, 7 * 2, 0xFFFF)
    img[clus_off(7):clus_off(7) + len(akp)] = akp
    root = entry(b"SAMPLES    ", 0x10, 2, 0) + lfn + entry(short, 0x20, 7, len(akp))
    img[root_off:root_off + len(root)] = root
    for i in range(nf):
        o = (res + i * fatsz) * bps
        img[o:o + len(fat)] = fat
    return bytes(img)

def make_mbr(fatimg):
    mbr = bytearray(63 * 512)
    struct.pack_into("<BBBBBBBBII", mbr, 446, 0x80, 1, 1, 0, 0x06, 254, 63, 0, 63, len(fatimg) // 512)
    mbr[510] = 0x55; mbr[511] = 0xAA
    return bytes(mbr) + fatimg

# ---------------------------------------------------------------- Akai HD
def akai_partition(blocks, volname, voltype, files):
    B = 0x2000
    img = bytearray(blocks * B)
    struct.pack_into("<H", img, 0, blocks)
    o = 0xCA
    img[o:o + 12] = akai_name(volname); struct.pack_into("<HH", img, o + 12, voltype, 3)
    fat = [0] * blocks
    for i in range(3): fat[i] = 0x4000
    fat[3] = 4; fat[4] = 0xC000            # volume dir uses 2 blocks
    d = bytearray(2 * B)
    for idx, (name, ftype, data, blist) in enumerate(files):
        e = bytearray(24); e[0:12] = akai_name(name); e[12:16] = bytes([0, 0, 0, 0]); e[16] = ftype
        e[17:20] = struct.pack("<I", len(data))[:3]; struct.pack_into("<H", e, 20, blist[0])
        d[idx * 24:idx * 24 + 24] = e
        for k, b in enumerate(blist):
            chunk = data[k * B:(k + 1) * B]
            img[b * B:b * B + len(chunk)] = chunk
            fat[b] = blist[k + 1] if k + 1 < len(blist) else 0xC000
    for i in range(idx + 1, 510): d[i * 24 + 0:i * 24 + 12] = bytes(12)
    img[3 * B:5 * B] = d
    for i, v in enumerate(fat): struct.pack_into("<H", img, 0x70A + i * 2, v)
    return bytes(img)

def make_akai_hd():
    smp = s1000_sample("VIOLIN C3", 6000, s3000=True)    # 12192 bytes -> 2 blocks, fragmented
    prg = s1000_program("VIOLINS", "VIOLIN C3", s3000=True)
    p1 = akai_partition(64, "STRINGS", 3, [("VIOLIN C3", 0xF3, smp, [5, 9]), ("VIOLINS", 0xF0, prg, [7])])
    kick = s1000_sample("KICK", 2000)
    p2 = akai_partition(40, "DRUMS", 1, [("KICK", 0x73, kick, [6])])
    return p1 + p2

def make_akai_floppy():
    B = 1024
    img = bytearray(1638400)
    smp = s1000_sample("FLOPPY SND", 1800)
    e = bytearray(24); e[0:12] = akai_name("FLOPPY SND"); e[16] = 0x73
    e[17:20] = struct.pack("<I", len(smp))[:3]; struct.pack_into("<H", e, 20, 20)
    img[0:24] = e
    for i in range(1, 64): img[i * 24:i * 24 + 12] = bytes([10] * 12)
    img[20 * B:20 * B + len(smp)] = smp
    return bytes(img)

# ---------------------------------------------------------------- DMG
def make_dmg(raw):
    sectors = len(raw) // 512
    chunk_secs = 64
    chunks = []
    data = b""
    for s in range(0, sectors, chunk_secs):
        n = min(chunk_secs, sectors - s)
        part = raw[s * 512:(s + n) * 512]
        if part.count(0) == len(part):
            chunks.append((0x00000002, s, n, len(data), 0))
        else:
            c = zlib.compress(part)
            chunks.append((0x80000005, s, n, len(data), len(c)))
            data += c
    chunks.append((0xFFFFFFFF, sectors, 0, len(data), 0))
    mish = bytearray(204)
    mish[0:4] = b"mish"; struct.pack_into(">I", mish, 4, 1)
    struct.pack_into(">QQQ", mish, 8, 0, sectors, 0)
    struct.pack_into(">I", mish, 200, len(chunks))
    for (t, s, n, off, ln) in chunks:
        mish += struct.pack(">IIQQQQ", t, 0, s, n, off, ln)
    xml = ("<?xml version=\"1.0\"?><plist><dict><key>resource-fork</key><dict><key>blkx</key><array><dict>"
           "<key>Data</key><data>" + base64.b64encode(bytes(mish)).decode() + "</data></dict></array></dict></dict></plist>").encode()
    koly = bytearray(512)
    koly[0:4] = b"koly"; struct.pack_into(">II", koly, 4, 4, 512)
    struct.pack_into(">QQQ", koly, 16, 0, 0, len(data))
    struct.pack_into(">QQ", koly, 216, len(data), len(xml))
    struct.pack_into(">Q", koly, 492, sectors)
    return data + xml + bytes(koly)

def make_bin(iso):
    out = bytearray()
    for i in range(len(iso) // 2048):
        sec = bytearray(2352)
        sec[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
        sec[15] = 1
        sec[16:16 + 2048] = iso[i * 2048:(i + 1) * 2048]
        out += sec
    return bytes(out)

iso = make_iso()
open(os.path.join(out, "test.iso"), "wb").write(iso)
open(os.path.join(out, "test.bin"), "wb").write(make_bin(iso))
fat = make_fat16()
open(os.path.join(out, "fat16.img"), "wb").write(fat)
open(os.path.join(out, "mbr.img"), "wb").write(make_mbr(fat))
open(os.path.join(out, "akai_hd.img"), "wb").write(make_akai_hd())
open(os.path.join(out, "akai_floppy.img"), "wb").write(make_akai_floppy())
open(os.path.join(out, "test.dmg"), "wb").write(make_dmg(iso))
print("images written to", out)
