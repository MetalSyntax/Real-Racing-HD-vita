# Real Racing HD — PS Vita port (iOS Mach-O loader)

Unofficial PS Vita port of **Real Racing HD 1.04** (Firemint, iPad, 2010). The original armv7 iOS
executable runs on the Vita through a custom Mach-O loader (Objective-C mini-runtime, UIKit/CoreAudio/
OpenGL ES shims over vitaGL). No game code or data is included: you need your own **decrypted** IPA.

Status: **pre-alpha, never run on hardware yet.** On the desk (Linux ARM32 hosttest) it boots, plays the
intro cutscene with music and reaches the main menu. See `port_progress.md`.

## Requirements

- PS Vita/PS TV with HENkaku/Ensō, `kubridge.skprx`, and `libshacccg.suprx` (ShaRKBR33D).
- `RealRacingHD 1.04.ipa`, **decrypted** (FairPlay `cryptid 0`). Tested only with build 1.04 (`fmBuildNumber 06241337`).

## Setup

1. Install `realracinghd.vpk` (kubridge.skprx must be in `ur0:tai/config.txt`).
2. Copy the contents of your decrypted Real Racing HD **1.04** `.app` folder (from your own IPA) to `ux0:data/realracinghd/game_app/`.
3. Launch. The first boot moves the 2148 files of `res/` into `rb/00..3f/` with a progress bar (one time, a few minutes; the Vita's exFAT looks names up linearly, so a flat `res/` would make every load take minutes). If it is interrupted it resumes on the next boot.

A different game version is refused at boot (the rebase table in the VPK is made for 1.04).

### Development

`tools/stage_data.sh` builds `ux0_data/realracinghd/` (game_app/ already split into `rb/XX/`, `rebase.bin`, controls.txt) and `python3 -I tools/deploy_data.py` uploads it (re-run until it prints `rb/.done written`). A `ux0:data/realracinghd/rebase.bin` overrides the one in the VPK. The build needs `ux0_data/realracinghd/rebase.bin` (run `stage_data.sh` first).

## Controls (defaults, editable in `ux0:data/realracinghd/controls.txt`)

| Vita | Game |
|---|---|
| Left stick / D-pad left-right | Steering (sent as iPad tilt) |
| ✕ / R | Accelerate (touch bottom-right) |
| □ / L | Brake (touch bottom-left) |
| START | Pause (touch top-right) |
| ○ | Back (touch top-left) |
| SELECT | 4:3 centered / stretched picture |
| Front touch | iPad touch screen |

## Build

`psvita-toolkit build` (VitaSDK softfp at `~/vitasdk`) → `build/realracinghd.vpk`.
Desk test without a console: `hosttest/run.sh build && hosttest/run.sh run 300`.

## Known limitations

Online features (Cloudcell leaderboards, Bonjour multiplayer, Game Center), custom liveries from the photo
library and iPod music selection are unavailable (stubbed). No trophies yet.

## Credits

Loader lineage: Inotia2-vita / ios-loader-boilerplate, Block-Breaker-3-Unlimited-vita (armv7 runtime),
Skater-Nation-vita (hosttest), vitaGL (Rinnegatamante), libmpg123, openal-soft. Real Racing HD © Firemint/EA.
