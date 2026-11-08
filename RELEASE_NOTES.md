# S6000 v2.14.0 — first public release

AKAI S6000 sampler emulation (OS Ver. 2.14) for macOS — **AU · VST3 · AAX · Standalone**.
Created by **© dominikHop**.

## Downloads
- `S6000-v2.14.0-macOS-AU.zip` — Audio Unit (Logic, GarageBand, Ableton, …)
- `S6000-v2.14.0-macOS-VST3.zip` — VST3 (Ableton, Cubase, Reaper, Studio One, …)
- `S6000-v2.14.0-macOS-AAX.zip` — AAX (unsigned: Pro Tools Developer only)
- `S6000-v2.14.0-macOS-Standalone.zip` — Standalone app

Universal binaries (Apple Silicon + Intel).

## What's inside
- Full S6000 front panel with OLED display, F1–F16, DATA wheel, keypad and POWER switch
- LOAD, MULTI, EDIT PROGRAM, EDIT SAMPLE, RECORD, FX (EB20), SAVE, UTILITIES — following the original manual
- Reads Akai S1000/S3000 CD-ROMs, hard disks and floppies (`.iso`, `.img`, `.bin`, `.dmg`) and S5000/S6000 `.AKP/.AKM/.WAV`
- 128 voices, 26 filters, APM modulation, sample editing with UNDO, recording
- Zoom 50–200 % (remembered)

## Install
See the [README](README.md#installation-macos). The builds are not notarized: if macOS blocks them, run
`xattr -dr com.apple.quarantine <path to the plug-in>`.

## Known limitations
- Mac HFS disk images and bzip2/LZFSE `.dmg` files are not supported
- "Virtual" (direct-to-disk) samples are loaded into RAM
