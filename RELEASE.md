# Real Racing HD (PS Vita) — Beta Release v1.0

First public release of the native iOS **Mach-O loader** port of *Real Racing HD* (Firemint) for the PlayStation Vita. On real hardware the game boots to the main menu in about 30 seconds and races run smoothly, with music, sound effects, analog steering and a choice of 4:3 or stretched 16:9 picture.

---

## 📦 Release Package Contents

1. **`realracinghd.vpk`** (v01.00): PlayStation Vita application package with the Mach-O loader, vitaGL renderer, CoreAudio/OpenAL audio layer and physical input handling. It also contains **`rebase.bin`** (`app0:rebase.bin`), so no PC tools are needed.

---

## 🛠️ What is rebase.bin?

`Real Racing HD` is a non-PIE armv7 executable linked at a fixed address. The loader maps it at `0x98000000`, and `rebase.bin` lists the **3,550** absolute 32-bit pointers in its data that must be moved to the new base. It contains offsets only, no game code. It is generated from your executable with:

```bash
python3 -I tools/macho_prep.py game_app/RealRacingHD   # run by tools/stage_data.sh
```

It ends with an `EXE1` trailer (executable size + CRC32). The loader checks it and refuses any executable other than **1.04**. A `ux0:data/realracinghd/rebase.bin` overrides the built-in one (for development).

---

## 🚀 Player Installation Guide

1. **PS Vita requirements**:
   - Custom Firmware (**Enso 3.60 / 3.65** recommended).
   - `kubridge.skprx` under `*KERNEL` in `ur0:tai/config.txt`.
   - `libshacccg.suprx` in `ur0:data/` (obtainable via **ShaRKBR33D**).

2. **Game assets**:
   - Extract `Payload/RealRacingHD.app/` from your decrypted iOS **1.04** `.ipa` (FairPlay `cryptid 0`).
   - Copy its contents to `ux0:data/realracinghd/game_app/`.
   - Layout:
     ```text
     ux0:data/realracinghd/
     ├── game_app/      (contents of RealRacingHD.app: RealRacingHD, res/, Info.plist, ...)
     ├── controls.txt   (created on first launch)
     ├── saves/         (created on first launch)
     └── logs/          (created on first launch, one log per run)
     ```

3. **Install the VPK**: transfer and install `realracinghd.vpk` with VitaShell.

4. **First launch**: the 2,148 files of `game_app/res/` are moved once into `game_app/rb/00`…`rb/3f` with a progress bar (a few minutes). If it is interrupted, it resumes on the next boot.

---

## 🎮 Controls

| PS Vita Input | Action |
| :--- | :--- |
| **Front Touchscreen** | iPad touch screen (menus, HUD) |
| **Left Stick / D-Pad ← →** | Steering (sent as iPad tilt) |
| **Cross (✖) / R** | Accelerate |
| **Square (◼) / L** | Brake |
| **Start** | Pause |
| **Circle (●)** | Back |
| **Select** | Toggle 4:3 centered / stretched picture |

Touch points, dead zone, steering angle and inversion can be edited in `ux0:data/realracinghd/controls.txt`.

---

## 📋 Changelog (v1.0)

- **Loader**: armv7 Mach-O runtime with ObjC categories, LC_DYLD_INFO binding and Thumb-2 literal handling → **3,550** rebase words, **1,064** import slots, 0 unimplemented, 6 global constructors.
- **Rebase**: PIC `__text` literals were rebased as pointers (crash in `game_translateChar`) → `macho_prep.py` drops them for PIC binaries.
- **Load times**: exFAT looks up names linearly in the 2,149-file `res/` (~250 ms per miss) → files are bucketed into `rb/XX/` on first boot. Boot to menu went from ~124 s to ~30 s.
- **Graphics**: vitaGL FFP shader failed to compile when the game used texture unit 1 alone (`undeclared identifier 'texenv0'`) → fixed in the vendored vitaGL.
- **Graphics**: crash in `glClear` → `update_scissor_test` caused by a matrix-stack overflow (vitaGL built with `SKIP_ERROR_HANDLING`) → fixed.
- **Audio**: double free in `BackgroundTrackMgr::Teardown` (AudioFileID closed twice) → live-handle table in `coreaudio.c`.
- **Audio**: ~40 s freeze when entering a race while the MP3 playlist was scanned with unbuffered reads → MP3s are read through a 64 KB buffered `FILE*`.
- **Controls**: steering direction corrected for the default `STEER_INVERT = 0`.
- **Graphics**: `glPolygonOffsetx` units are scaled from 16-bit to float depth (`DEPTH_BIAS`, logged as `[GL] glPolygonOffsetx`) as a first step on the car shadow.
- **Debugging**: kubridge exception handler logs `[CRASH]` with Mach-O offsets, registers and a stack scan; fixed the log thread priority, which had left the console logs empty.
- **Install**: `rebase.bin` is embedded in the VPK, with an executable check and a boot dialog for missing files.

---

## ⚠️ Known Issues

- One-time `res/` migration on first boot takes a few minutes.
- **Car shadow (under investigation)**: jagged black spikes on the road under the cars (depth fighting with the track). `DEPTH_BIAS` in `controls.txt` scales the game's depth offset; it does not fix it yet.
- Green/cyan speckles on the edges of some cars.
- The race-entry freeze fix and the steering direction fix are in this build but have not been confirmed on console yet.
- Online features (Cloudcell leaderboards, Bonjour multiplayer, Game Center), photo-library liveries and iPod music selection are disabled.
- No trophies yet.

Installation and build instructions: see [README.md](README.md).
