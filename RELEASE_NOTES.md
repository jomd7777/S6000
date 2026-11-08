# S6000 v2.14.1 — EDIT SAMPLE redesign

- New waveform display for every EDIT SAMPLE page (manual p.141–177): overview strip + zoomed view,
  LOG / LIN (SAMPLE TOOLS > W/FORM), highlight between START and END that follows the edit points,
  loop region, edit-point flags, slice lines, time ruler.
- Animated white play line (left → right) with ▶ PLAY / ■ STOP status, also for notes played from the
  MIDI keyboard.
- Mouse: drag the edit points, wheel = zoom at the pointer, double-click = whole sample, click the overview = scroll.
- Pages rebuilt like the hardware: MASTER, TRIM (FIND START/END with threshold), CHOP (EXTRACT / ERASE / CUT),
  LOOP (End→|←Start splice view, CROSSFADE and PLAY MODE windows), JOIN / MIX / MAKE STEREO (two waveforms),
  FADE (LINEAR / LOG / SINE curves drawn), TIMESTRETCH / PITCH SHIFT / BPM MATCH (54 presets), RE-SAMPLE
  (quality, resolution, presets), EQ (3 bands, shelf slope and Q windows, response curve).
- Every process runs in the background ("Processing sample", ABORT) and ends with the
  PLAY ORIGINAL / PLAY NEW / KEEP ORIGINAL / KEEP NEW / KEEP BOTH / OVERWRITE + RENAME prompt.

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
