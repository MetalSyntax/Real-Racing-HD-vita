# Real Racing HD — Port progress (iOS Mach-O)

> Phases section kept identical to `PORTING_PLAN.md`. Console run #1 has not happened yet: everything
> below is desk work (build, hosttest, scripts).

## Phases

**Legend.** `[x]` = done and verified at the desk (build output, hosttest log, script output; the evidence
is cited on the item). `[~]` = implemented, pending verification on the real Vita (the item says exactly
which log line / behaviour to check). No item is left `[ ]`. Hosttest = the same loader built for Linux
ARM32 softfp in Docker (`hosttest/run.sh`, log `build/hosttest-data/realracinghd/logs/realracinghd_001.log`).

### Phase 1: Repo and tools
- [x] IPA extracted, `game_app` symlink, loader merged, CMake/`hle.h` adapted to `realracinghd` (TITLEID `PSVRRHD01`, `EXE_NAME "RealRacingHD"`, `APP_DELEGATE "RealRacingAppDelegate"`).
- [x] Executable decrypted (cryptid 0; `otool -l` → `LC_ENCRYPTION_INFO cryptid 0`).
- [x] `rebase.bin` + `docs/macho_report.txt` generated (`python3 -I tools/macho_prep.py game_app/RealRacingHD ux0_data/realracinghd/rebase.bin --report docs/macho_report.txt` → `Rebase: 3550 palabras`).
- [x] (new) Runtime replaced by the armv7 donor: `source/` from Block-Breaker-3-Unlimited-vita (Thumb-2 literal pools, LC_DYLD_INFO binding, ObjC host classes, uikit.c), plus `tools/macho_prep.py`, `annotate.py`, `gen_cxx.py` from the same donor. BB3-only patches (SFX.bar, Irrlicht hooks) dropped; `source/patch.c` rewritten for this game.

### Phase 2: Decompilation and inventory
- [x] `decompiled/macho/` dumps (frameworks, ObjC classes, symbols, imports). `imports.txt` regenerated: `nm -u` fails on this armv7 file, so it is the union of `otool -Iv` indirect symbols (365, identical to the macho_prep list) and `llvm-objdump --macho --bind --lazy-bind --weak-bind` (adds 39 classrefs/vtables/`dyld_stub_binder`) = **409 symbols**.
- [x] Ghidra pseudo-C: `decompiled/RealRacingHD_armv7/ghidra/RealRacingHD.c` (4.8 MB, 2028 functions, 1 failed; Ghidra 12.1.4 headless + `tools/ExportDecompiled.java`, JDK 21; gitignored). Full `otool -tV` listing + `tools/annotate.py` used for all analysis below.
- [x] Imports classified (libc/math/pthread, GL, ObjC/Foundation/CF, audio, UIKit/CG, C++, network/GameKit/crypto, zlib) by `python3 -I tools/imports_check.py`.
- [x] Table `docs/imports.md` (symbol → shim file → status). Result: **409 imports: 333 real, 38 dynamic (ObjC classes created at bind), 38 stub, 0 missing**.

### Phase 3: Real engine analysis
- [x] Engine by symbols: Firemint's own C++ engine, no middleware engine (0 hits for cocos2d/Unity/SIO2/oolong/Box2D/Bullet/Lua/FMOD/Flurry/OpenFeint/ScoreLoop). Evidence in `symbols.txt`: `CGlobal` (whole game state; `game_*`, `menu_*`, `renderer_*`, `scene_*`), `Mint3DTexture` (28), `M3GLoader` (18, JSR-184 `.m3g` models — J2ME heritage, `system_bHasMMAPI`), `Render3d`, `CndiPhoneContainer` (29, iOS platform layer), `CC_*` (368, Cloudcell online/achievements), `fmBonjourWrapper`, `fmUserMusic`, `SoundEffectPool`, `BackgroundTrackMgr`.
- [x] Lifecycle: `main` → `UIApplicationMain(argc, argv, nil, nil)` (0x43dc) → MainWindow.nib (NIBArchive: `RealRacingAppDelegate` with outlets `window`, `glView` (EAGLView via UIClassSwapper) and `gameViewController` (GameViewController + 3 UITextFields)) → `-[RealRacingAppDelegate applicationDidFinishLaunching:]` → `-[EAGLView startAnimation]` → frame driver = `+[NSTimer scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:]` → `-[EAGLView drawView]` → `CndiPhoneContainer::DoLoop` (CADisplayLink branch also present; hosttest shows the NSTimer branch at 1/60 s, later 0.0455 s). Accelerometer: `-[EAGLView accelerometer:didAccelerate:]`.
- [x] Resource opening: `CFileSystem::OpenFile` tries `GetDocPath()/<name>` (Documents) then `GetPath("/res/<name>")` = `[[NSBundle mainBundle] resourcePath]` + `stringByStandardizingPath`; PNGs via `CGlobal::renderer_LoadTextureFromFile` → `asset_LoadFile` → `.pvr` → `Mint3DTexture::LoadFromPvr`, else `LoadTextureFromMemory` → `-[Texture2D(Image) initWithImageData:]` → UIImage/CGImage → CGBitmapContext. Audio files through `CFURLCreateFromFileSystemRepresentation` + `AudioFileOpenURL` (`OpenAudioFile`, `OpenAudioFileBMP`). Saves `profile.dat`, `syncdata.dat`, `leaguedata.dat`, `*replaydata.dat` via `SaveFile` → `NSSearchPathForDirectoriesInDomains(Documents)`.
- [x] Middleware decided: OpenAL (real, openal-soft), zlib (`compress2`/`uncompress` → VitaSDK zlib), AudioQueue/AudioFile (real, coreaudio.c), Cloudcell/HTTP/Bonjour (`CFNetService*`, `CFReadStream*`, `CFHTTP*`) = stubs "network unavailable", GameKit = absent at runtime (iOS 3.2 reported; `GKLocalPlayer` probes return nil), `CC_SHA512` = stub (only `game_GetWinnersCircleURL`), AddressBook/CoreLocation/SystemConfiguration = not imported as functions.

### Phase 4: Loader and ObjC bootstrap (GATE: log reaches the app delegate)
- [x] `macho_load()` maps `__TEXT`/`__DATA` at `0x98000000`, applies `rebase.bin`, binds pointers. Hosttest: `Mach-O: TEXT 0x00001000+0xD2000 DATA 0x000D3000+0x7000 -> real 0x98001000`, `rebase applied: 3550 words`.
- [x] Every import resolves: hosttest `imports: 1064 slots, 0 unimplemented`; `tools/imports_check.py` → `MISSING` count 0.
- [x] `macho_run_inits()`: hosttest `running 6 global constructors... global constructors: 6 done`.
- [x] ObjC: guest classes from `__objc_classlist`, **categories from `__objc_catlist` (new, objc.c `category_method`: Texture2D(Image/Text/Drawing), UIDevice(Hardware))**, Foundation host classes, `NSUserDefaults` persisted to `saves/NSUserDefaults.txt` (new), `NSBundle resourcePath/infoDictionary` (new). Hosttest reaches the run loop with all of them.
- [x] Delegate called: hosttest `[BOOT] nib: GameViewController 0x814a1640` / `-> applicationDidFinishLaunching:` / `[BOOT] delegate ok (applicationDidFinishLaunching returned)` / `run loop started`.
- [~] Same gate on the Vita: console run #1 log must show `rebase applied: 3550 words`, `imports: 1064 slots, 0 unimplemented`, `global constructors: 6 done`, `[BOOT] delegate ok`.

### Phase 5: Graphics (GATE: first frame on screen)
- [x] vitaGL softfp (`SOFTFP_ABI=1`, toolchain is vitasdk-softfp: `arm-vita-eabi-gcc -v` → `--with-float=softfp`), EAGLContext/CAEAGLLayer/renderbuffer shims on the virtual FBO: hosttest `EAGLContext initWithAPI:1`, `EAGL drawable renderbuffer = 1 (768x1024)`, `EAGL window framebuffer = 2`.
- [x] Orientation/aspect: iPad renderbuffer 768x1024 (portrait, `VIEW_W/H` in hle.h) rotated to landscape and shown 4:3 centered 725x544 (default) or stretched 960x544 (SELECT). Decision: 4:3 pillarbox by default (no distortion), stretch as option. Hosttest frames `build/hosttest-data/frames/*.ppm`: Firemint splash and 3D main menu upright, pillarboxed. The first frames were 180° rotated with the textbook mapping → `ROTATE_180 = 1` default (gles.c `land_to_view`).
- [~] Orientation on the Vita: Mesa (hosttest) and vitaGL may differ in FBO Y convention (see Skater-Nation hosttest note). Check: splash text "FIREMINT" reads normally. If upside down set `ROTATE_180 = 0` in `controls.txt`; if mirrored, report it (needs a code fix in `land_to_view`).
- [x] `presentRenderbuffer:` → `gles_present` → `vglSwapBuffers`; frame loop driven by the run loop timers (NSTimer + new CADisplayLink shim in uikit.c).
- [x] Textures: CgBI via `cgbi.c` (`python3 -I tools/cgbi_check.py` → `8 PNG CgBI idénticos a la referencia`), 1000 standard RGBA PNGs via new UIImage/CGImage/CGBitmapContext path (libpng, CTM honoured: hosttest `CGContextDrawImage 256x37 into 256x64 bpp 4: ctm [1 0 -0 -1 0 37]`), PVR v2: 685 PVRTC 2bpp, 10 PVRTC 4bpp, 77 RGBA5551, 7 RGBA4444, 1 RGB565 → `glCompressedTexImage2D`/`glTexImage2D` (vitaGL supports PVRTC, `textures.c`). `GL_FIXED` arrays converted (hosttest `GL array 1: ... (converted)`).
- [~] PVRTC on hardware: Mesa cannot upload PVRTC (hosttest stderr `glCompressedTexImage2D formato 0x8C01 ...: error GL 0x500`, white 3D menu). Check the 3D garage/menu is textured on the Vita; `[GL]` warnings near `glCompressedTexImage2D` = report.
- [x] GLES2: not used (`EAGLContext initWithAPI:1` = GLES 1.1 only; no `glCreateShader` import).
- [~] First frame on the Vita: Firemint splash within ~5 s of boot.

### Phase 6: Touch
- [x] Front touch → `UITouch` in EAGLView points: `gles_screen_to_view()` inverts the present transform (side bars ignored), input.c/uikit.c deliver Began/Moved/Ended to the EAGLView (`[INPUT] touchesBegan at view (x, y)` logged for the first 10). Hosttest (`HOSTTEST_TOUCH`, run 10): `[INPUT] touchesBegan at view (358, 230)` on the QUICK RACE sign opened the Quick Race screen, a second tap dismissed the Cloudcell popup (frame `docs/hosttest/2026-11-04_quickrace.png`); a tap on the black side bar was ignored as designed.
- [~] On the Vita: tap QUICK RACE on the 3D menu and confirm the item reacts; the logged view point must lie inside 768x1024.
- [x] Rear panel: not sampled (only `SCE_TOUCH_PORT_FRONT`), so it cannot cause stray input.

### Phase 7: Physical controls
- [x] Default mapping (patch.c): left stick X / D-pad → accelerometer tilt (`patch_accel`, gravity vector rotated like a wheel, signs per orientation 3/4), ✕/R accelerate, □/L brake, START pause, ○ back as touches at landscape points, SELECT display mode. Built clean (0 warnings in `source/`).
- [x] Dead zone 10/128, steering 40° at full stick, `TILT_BACK` 0.5, clamped when out of range. Hosttest: `[INPUT] controls.txt: deadzone 10, steer 40 deg` (parser read the staged file).
- [x] `controls.txt` (template `extras/controls.txt`, staged to `ux0:data/realracinghd/controls.txt` only if absent): missing file = defaults (`[INPUT] ... not found: default controls`).
- [~] On the Vita: steering direction (set `STEER_INVERT = 1` if reversed), gas/brake/pause touch points (edit coordinates if they miss the iPad buttons).

### Phase 8: Audio
- [x] What the binary uses (symbol evidence): OpenAL for SFX (`SoundEffectPool`, `LoadWAV` → `alBufferData`), AudioFile for WAV loading (`LoadAudioFileData`: `'dfmt'`=`lpcm`, `'bcnt'`, `AudioFileReadBytes`), AudioQueue for music (`BackgroundTrackMgr::SetupQueue/SetupBuffers/QueueCallback`: `'pkub'`, `'pcnt'`, `AudioFileReadPackets`, `'mgic'`, `'cmap'`, `'aqrn'` listener, run-loop callbacks via `CFRunLoopGetCurrent`), AudioSession. No AVAudioPlayer/ExtAudioFile/AudioServices imports.
- [x] Bundle inventory (`python3 -I tools/media_check.py` → `[media] mp3: 12, wav: 74; videos: 0`): 74 WAV 16-bit PCM mono (58 at 12 kHz, 16 at 44.1 kHz), 12 MP3 layer III 160/192 kbps (10 at 44.1 kHz, 2 at 48 kHz). No offline conversion needed: WAV parsed in `coreaudio.c`, MP3 decoded on the Vita with VitaSDK `libmpg123` (AudioFile reports 16-bit stereo LPCM; the game's `QueueCallback` already handles `'lpcm'`). `media_check.py` runs in `tools/stage_data.sh` and fails on any unsupported format.
- [x] AudioQueue → `sceAudioOut` MAIN port 48 kHz (openal-soft keeps the BGM port, Skater-Nation lesson) with linear resampling, volume (`kAudioQueueParam_Volume`), pause, drained/immediate stop, buffers returned to the guest on the main run loop (`coreaudio_poll`). Hosttest: `AudioFileOpenURL(.../music_intro.mp3): mp3->pcm 44100 Hz 2 ch`, `AudioQueue thread: 44100 Hz 2 ch -> port 0 (48 kHz)`, 44 WAV SFX opened (`wav 44100 Hz 1 ch`), `music_intro.mp3` during the intro then `music_menu.mp3` on a second queue at the menu (track switch works), no stall in 8+ minutes.
- [~] On the Vita: hear `music_intro` during the intro and menu music after; SFX on menu taps; no crackle. Log: `[AUDIO] AudioQueue thread`. Errors to look for: `sceAudioOutOpenPort = 0x...`, `[AUDIO] mpg123_read`.
- [~] Suspend/resume: game calls `alcSuspendContext`/`AudioQueuePause` itself (`game_SuspendOpenAL`, `BackgroundTrackMgr::Pause`); check music resumes after PS button / sleep.

### Phase 9: Videos / FMV
- [x] Inventory: **0 video files** in the bundle (`media_check.py`), and `MPMoviePlayerController` is not imported (no `_OBJC_CLASS_$_MPMoviePlayerController` in imports.txt). The intro is in-engine (`cutscene_intro_sequence.banim`, hosttest `loading bez data: cutscene_intro_sequence.banim`). The MediaPlayer link is for iPod music (`MPMusicPlayerController*`, `MPMediaItemProperty*`), answered with an empty library.
- [x] Skip/missing safety: the inherited `MPMoviePlayerController` shim (uikit.c) posts `...PlaybackDidFinishNotification` at once, so a video request can never hang on black; `media_check.py` flags any `.m4v/.mp4/.mov` that a future IPA version might add.
- [~] On the Vita: intro cutscene plays (in-engine) and can be skipped by a tap as on iOS.

### Phase 10: Assets, data, language and saves
- [x] Paths: `NSHomeDirectory`/Documents → `ux0:data/realracinghd/saves/`, `Library/Caches` → `ux0:data/realracinghd/Library/Caches/`, bundle → `game_app/`, `_NSGetExecutablePath` → `game_app/RealRacingHD`. Hosttest: Documents probes `saves/...`, bundle reads `game_app//res/...` succeed.
- [x] Boot checker: `main.c boot_check()` shows a system message dialog with the exact missing path for `game_app/RealRacingHD` and `rebase.bin`, then exits. Builds; hosttest fakes the dialog.
- [x] Language: `NSLocale preferredLanguages` ← `SCE_SYSTEM_PARAM_ID_LANG` (ja, en, fr, es, de, it, nl = the shipped `res/text_*.txt`), English fallback. Hosttest: `NSLocale preferredLanguages: en`.
- [~] Saves survive restart: after finishing a race, `saves/profile.dat` (and `syncdata.dat`) exist and the profile is kept after relaunch. `NSUserDefaults.txt` appears once an option is changed.
- [x] (new) Guest `malloc`/`operator new` return zeroed memory (darwin.c `d_malloc`): CGlobal relies on it (hosttest crash in `Mint3DTexture::Finalise` from `scene_Transition`, fixed).
- [x] (new) Darwin 32-bit `time`/`gettimeofday`/`localtime` wrappers (hosttest crash: 8-byte `time_t` write over a saved register in `SyncDataFile::Erase`).

### Phase 11: Performance
- [x] 444/222/222/166 clocks at boot (`main.c`).
- [~] Frame time and memory: run `psvita-toolkit perf-telemetry` in a race; expect the 768x1024 FBO to be the main GPU cost (lever: lower FBO size later). Watch `vglMemFree` / heap after 3 races (`psvita-toolkit mem-profile`).

### Phase 12: Configurator and options
- [x] Persistent settings: display mode saved to `saves/vita_settings.txt` on SELECT and restored at boot; tuning (dead zone, steering range, inversion, tilt, rotation, button points) in `controls.txt`.
- [~] On the Vita: press SELECT, reboot the game, the chosen picture mode is kept.

### Phase 13: Trophies
- [x] Achievements are the game's own Cloudcell list, not GameKit: `CC_AchievementManager_Class::UnlockAchievementByAchievementId(uint, bool)`, ids **3000–3020 (21)** (`res/cc_achievementicon3000..3020.png`), checked in `CGlobal::game_CheckRaceEndAchievements` / `game_CheckMenuAchievements` / `CheckForCreamOfTheCropAchievement`. GameKit only mirrors them (`SyncToGameCentre`), and it is off on the reported iOS 3.2.
- [~] Plan: hook `UnlockAchievementByAchievementId` (log `[TROPHY] id N`) and map the 21 ids to an `sceNpTrophy` pack once a homebrew trophy pack is made; until then the in-game Achievements screen keeps working offline. Verify on the Vita that the Achievements menu opens.

### Phase 14: LiveArea and VPK
- [x] LiveArea from the game art (`python3 -I tools/make_livearea.py` → `extras/livearea_game/`, gitignored): icon0 128x128 from iTunesArtwork, bg0 840x500 / pic0 960x544 / startup 280x158 from `Default-Landscape.png` (CgBI decoded), all 8-bit indexed, bg0 81 KB. `psvita-toolkit livearea --validate` on these files → all OK. Placeholders in `extras/livearea/` also validate.
- [x] `param.sfo` Title ID `PSVRRHD01`, `psvita-toolkit build` → `[+] Build OK. VPK: .../build/realracinghd.vpk` (0 warnings in `source/`; vitaGL vendored with `._*` filtered in its Makefile).
- [~] VPK installs in VitaShell without warnings and shows the Real Racing HD bubble/LiveArea.

### Phase 15: Real-hardware testing and release
- [~] Full playthrough (career cup, quick race, time trial) without crashes; suspend/resume; Vita 1000/2000/PSTV status.
- [~] Production build: `CMAKE_BUILD_TYPE=Release` / `PORT_TRACE=OFF` once stable (toolkit builds Debug now: `DEBUG_LOADER`).
- [x] README states IPA 1.04 (`fmBuildNumber 06241337`) and that a decrypted IPA is required (`README.md`).

## Console run #1 checklist

**Deploy** (from the port folder; Vita in VitaShell FTP mode at the IP in `.psvita-toolkit.json`):

```sh
psvita-toolkit build                 # -> build/realracinghd.vpk
sh tools/stage_data.sh               # -> ux0_data/realracinghd/ (game_app/, rebase.bin, controls.txt, saves/, logs/)
python3 -I tools/deploy_data.py      # uploads ux0_data/realracinghd/ -> ux0:data/realracinghd/ (never touches saves/)
psvita-toolkit deploy                # VPK -> ux0:/downloads, install it in VitaShell
# run the game, then:
python3 -I tools/deploy_data.py --logs   # pulls logs/realracinghd_NNN.log + .psp2dmp into logs/
```

Prerequisites on the console: `kubridge.skprx`, `ur0:data/libshacccg.suprx`.

**Log lines that prove each subsystem** (newest `logs/realracinghd_NNN.log`):

| Subsystem | Expected line(s) |
|---|---|
| Loader | `Mach-O: TEXT 0x00001000+0xD2000 ...`, `rebase applied: 3550 words`, `imports: 1064 slots, 0 unimplemented`, `global constructors: 6 done` |
| Boot / ObjC | `UIApplicationMain: emulating MainWindow.nib`, `[BOOT] nib: GameViewController 0x...`, `[BOOT] delegate ok`, `run loop started` |
| GL | `vitaGL: FBO 768x1024 -> screen 960x544`, `EAGL drawable renderbuffer = 1 (768x1024)`, `[GL] CGContextDrawImage ...` (3 lines), Firemint splash on screen |
| Audio | `[AUDIO] AudioFileOpenURL(.../sfx_menu_*.wav): wav 44100 Hz 1 ch`, `[AUDIO] AudioFileOpenURL(.../music_intro.mp3): mp3->pcm 44100 Hz 2 ch`, `[AUDIO] AudioQueue thread: 44100 Hz 2 ch -> port N (48 kHz)` + audible music |
| Video | no FMV in this IPA: `loading bez data: cutscene_intro_sequence.banim` (in-engine intro) then `cutscene_menu.banim` |
| Input | `[INPUT] controls.txt: deadzone 10, steer 40 deg`, `[INPUT] touchesBegan at view (x, y)` when tapping |
| Language | `NSLocale preferredLanguages: <xx>` matching the Vita language |

**Likely failures and what to do**

| Symptom | Where to look / fix |
|---|---|
| Message dialog "Missing ..." at boot | Path shown is exact: re-run `stage_data.sh` + `deploy_data.py` (decrypted executable must be `game_app/RealRacingHD`). |
| Exit right after `Mach-O loaded` / crash with PC `0x98xxxxxx` | `psvita-toolkit analyze <dump> --elf build/realracinghd`; PC − 0x98000000 → `decompiled/macho/symbols.txt`, `tools/annotate.py`. A register holding a small value (< 0xDA000) that should be a pointer, or a pointer `0x98xxxxxx` used as an offset = rebase error (`docs/macho_report.txt`, macho_prep PIC rule). |
| `[UNIMPL]` / `objc: -[X y] not implemented` before a crash | Implement that shim (objc.c/uikit.c), it is the to-do list. |
| Black screen but log runs | Check `EAGL drawable renderbuffer`; then `ROTATE_180`/display mode; vitaGL `vglMemFree` exhaustion (768x1024 FBO + depth). |
| Picture upside down / touches hit the wrong place | `ROTATE_180 = 0` in `controls.txt` (picture and touches move together). Mirrored picture = code fix in `gles.c land_to_view`. |
| No music, SFX OK | `sceAudioOutOpenPort = 0x...` → port conflict: try `SCE_AUDIO_OUT_PORT_TYPE_BGM` in `coreaudio.c aq_thread`; `mpg123_read` warnings → MP3 decode. |
| No SFX | openal-soft device: `alcOpenDevice` path (CGlobal::game_Construct); check `alGetError` after `alBufferData`. |
| Steering reversed / car does not move | `STEER_INVERT = 1`; move `CROSS`/`SQUARE` points onto the iPad gas/brake buttons (landscape 1024x768 coordinates). |
| Crash in free()/heap | guest use-after-free → Hero-of-Sparta-II quarantine heap (commit 8845be7) is the donor fix. |
| C++ exception (`_Unwind_SjLj_Resume` fatal) | SjLj unwinding is not implemented; find the `throw` site (`__cxa_throw` caller) and avoid the condition. |

## Session 2026-11-04 (desk work, no console)

- Runtime: replaced the boilerplate with the armv7-proven Block-Breaker-3-Unlimited runtime (objc_rt/uikit/cxx/LC_DYLD_INFO binding, BB3 macho_prep with Thumb-2 literal handling), renamed to `realracinghd`, BB3 game patches removed, `patch.c` rewritten (controls, tilt, settings).
- Imports: `imports.txt` rebuilt (409 = otool -Iv ∪ dyld bind info); new `tools/imports_check.py` + `docs/imports.md`: 0 missing. Added CF (strings/arrays/data/dicts/URLs/time), CGImage/CGBitmapContext/UIGraphics/UIImage (libpng + CgBI), network/Bonjour/HTTP/SHA512 stubs, Darwin extras (ftime, tz, pthread_attr, `_NSGetExecutablePath`, zlib), fixed-point GL (`glAlphaFuncx`...), OpenAL extras, C++ (`gen_cxx.py` now reads imports.txt).
- Audio: real AudioFile (WAV + MP3 via libmpg123) and AudioQueue (MAIN port 48 kHz, resampler, run-loop callbacks); `tools/media_check.py` in staging.
- Video: none in the IPA (0 files, MPMoviePlayerController not imported); skip-safe shim kept.
- Graphics: iPad identity (iPad1,1, iOS 3.2, 768x1024), landscape present with 4:3/stretch, `ROTATE_180`, touch inverse mapping, CADisplayLink shim, ObjC **categories** (were ignored by the donor runtime), NSUserDefaults persistence, NSBundle infoDictionary, language mapping, boot checker dialog.
- Hosttest (Skater-Nation harness adapted: mpg123, scripted touches `HOSTTEST_TOUCH`, stack scan in the crash handler, log flush on crash/SIGTERM). Bugs found and fixed at the desk: (1) PIC-binary `__text` literals rebased as pointers (CGlobal offset 0x20760, crash `game_translateChar`) → macho_prep drops them for PIC binaries (3636 → 3550 words); (2) missing ObjC categories; (3) `stringByStandardizingPath`/`infoDictionary` nil → data files not found (crash `scene_binGetByte`); (4) 64-bit `time_t` write over a saved register (crash after `SyncDataFile::Erase`) → 32-bit Darwin time wrappers; (5) non-zeroed `operator new` memory (crash `Mint3DTexture::Finalise`) → zeroed guest allocations; (6) picture 180° rotated → `ROTATE_180`; (7) NSLock size vs glibc. **Furthest point: boots, in-engine intro with MP3 music via AudioQueue, 44 SFX loaded, 3D main menu upright, scripted touch on QUICK RACE opens the Quick Race / Track Select screen with `music_menu.mp3`; 8+ minutes without crash** (`salida: 124` = timeout; log copy `docs/hosttest/2026-11-04_realracinghd_001.log`).
- Build: `psvita-toolkit build` → `build/realracinghd.vpk` OK; vitaGL Makefile filters `._*`; CMake finds the gitignored game-art LiveArea; `.gitignore` anchored (`/Makefile`) so vendor/vitaGL/Makefile is not dropped by the toolkit copy.
- Docs: README rewritten for the port; this file and PORTING_PLAN.md phases in sync.

## Session 2026-11-09 (console run 001)

- `logs/realracinghd_001.log`: first console boot reaches the menu (Firemint data, cars/tracks, `[INPUT] touchesBegan`), then the log stops dead at 00:31:10 while the garage generates car thumbnails (`custom_final_12.png` missing → next car `sedan_base_1_exterior.pvr`, first sedan). No crash line and no reachable `.psp2dmp`: the Vita build had no exception handler. Hosttest passes the same loop (custom_final_0..47).
- Added a kubridge exception handler (`source/main.c on_exception`): logs `[CRASH]` kind, pc/lr as Mach-O offsets, FAR/FSR, r0-r12/sp and a 16-entry Thumb return-address stack scan, then chains to the default handler (dump still written). Next run: map `macho 0x...` with `decompiled/macho/symbols.txt`.
- Run 002: same cut point (~34 s after launch, thumbnails custom_final_0..15), no `[CRASH]` line, touches still processed → not a crash: the game is still loading behind the Firemint splash and the run was closed. Console is ~3x slower than hosttest (thumbnails 270-370 ms/car vs 100 ms; 48 cars), hosttest reaches the intro cutscene ~5 s after the last thumbnail. DEBUG log lines were synced to SD (~10 ms each, 367 lines): now buffered like INFO.
- Run 003: boot → thumbnails 3x faster (all 48 in 11 s), then the log stops right after `custom_final_47` (still on the Firemint screen). Root cause of the blind logs: the log thread priority `0x10000100 + 60` (= 220) is out of range (lowest user = 191), the thread was never created on the console (hosttest ignores prio) and INFO lines only reached the SD behind a WARN/ERROR. Fixed: priority 191, `SCE_KERNEL_CPU_MASK_USER_ALL`, WARN if creation fails. Next run shows where it really stops (hosttest next step: menu SFX `AudioFileOpenURL(sfx_menu_*.wav)`, menu PNGs, then intro cutscene + `music_intro.mp3`).
- Run 004 (log thread fixed): the game is NOT hung: after the thumbnails it loads menu SFX, GUI PNGs, race SFX and the intro track textures (`dugout.pvr` at +40 s), the point where hosttest starts the intro. It is just slow: a miss in `game_app//res/` costs ~257 ms vs ~2 ms in `saves/` → exFAT linear name lookup in a 2149-file directory (hits ~half). Fix: `darwin_res_buckets()` (darwin.c, called from main) moves res/* into `game_app/rb/00..3f/` once on the console (FNV-1a of lowercase name % 64, max 45 files/bucket), removes res/, and `darwin_path` redirects `res/<name>` (also `//res/`) to the bucket. Read-only bundle (hosttest) stays flat. `deploy_data.py` skips `res` when the console already has `rb`.
- Run 005: stuck before the game starts (black/LiveArea): the on-console res/ migration never finished (FTP showed rb/00..35 created, all empty; res/ intact + 664 `._*` AppleDouble files). Reverted to a marker check: `darwin_res_buckets()` only enables the rb/XX redirect when `game_app/rb/.done` exists. `tools/deploy_data.py push_res()` does the move over FTP (RNFR/RNTO of the files already on the console, upload otherwise, resumable, writes `rb/.done` last); its `res_bucket()` is checked equal to the C one (Dugout.pvr → 0x13, max 45/bucket).
- Run 006: deploy_data.py had stopped at ~500/2149 renames (FTP dropped: console slept), game launched without rb/.done → flat mode → `res/1.dat` already moved → data abort at macho 0x7C14 (null file). The exception handler then looped (4319 `[CRASH]` lines: no default handler, returning re-runs the instruction) → now `sceKernelExitProcess(0)` after logging. Loader mode 2 "PARTIAL": rb/ without .done → stat the bucket file, fall back to res/ when missing.
- Run 007 (PARTIAL rb/ mode, deploy never finished): no crash, Firemint splash for 53 s, reached `menu_trophy_*_small.png` = hosttest line 578 of the 906 before the intro cutscene (hosttest: 4.4 s more, console ~45 s more at this speed). Closed by the user while still loading. Studied sibling ports: Asphalt-6 bug #032 (slow loads: FIOS2 cache, negative path cache, 64 KB stdio, affinity). Applied the missing piece: `setvbuf(_IOFBF, 64 KB)` on read-mode `d_fopen` (newlib default 1 KB = one sceIoRead per KB). vitaGL shader cache is already on (HAVE_SHADER_CACHE, default ux0:data/shader_cache). Ferrari GT (Gameloft) packs data in .bar archives: no huge-directory issue there.
- Run 008: no crash, still PARTIAL. 53 s in it was loading track textures (hosttest line 794/906; intro at 906): slow, not hung. Added a temporary yellow boot load bar (patch_overlay; d_fopen counts res misses vs 749 before the intro; gles_loading_tick re-presents every 200 ms from the GL thread while the game loads synchronously). Hosttest: bar visible in frames 1-3, still reaches the intro and the menu.
- Run 009: loading finished (~110 s, still PARTIAL), music_intro played, NSTimer drawView started; 2.7 s later data abort pc=E000C986 (sceClibMemcpy) lr=0x810338F9 = vitaGL ffp.c:947 reload_ffp_shaders: shark_compile_shader_extended returned NULL for a generated FFP fragment shader and vitaGL memcpy'd from it (FAR=0). Patched vendor/vitaGL ffp.c (vertex + fragment) to call vgl_ffp_compile_failed (source/gles.c): logs the shader source + shark diagnostics (recompiled with log cb) and exits via fatal_error. Fixed log.c: FATAL lines were buffered and lost when fatal_error closed the fd. Next run: fix the shader from the logged error.
- Run 010: `vgl_ffp_compile_failed` logged the failure: `shark E line 20: unexpected undeclared identifier 'texenv0'. Did you mean 'texenv50'?`. The game uses texture unit 1 alone (without unit 0) for reflection passes with `GL_COMBINE`. Without `HAVE_UNPURE_TEXCOORDS`, vitaGL hardcodes `base_texture_id = 0`, causing `mask.tex_env_mode_pass1 = COMBINE` while `mask.tex_env_mode_pass0` stayed 0 (MODULATE), generating `texenv50` in the preamble but `#define pass0_func texenv0`. Fixed: (1) Added `UNPURE_TEXCOORDS=1` to `VITAGL_MAKE_FLAGS_STR` in `CMakeLists.txt`; (2) Patched `vendor/vitaGL/source/ffp.c` for `base_texture_id` vertex attribute dirty checks and dynamic stream indexing; (3) Bumped `FFP_SHADER_CACHE_MAGIC` to 29 in `shared.h`; (4) Hooked `glTexEnvx`/`glTexEnvf` in `source/gles.c` with per-unit tracking (`env_mode[2]`) to prevent `gles_present` from clobbering texture unit environment modes. Built clean to `build/realracinghd.vpk`.
- Run 011 (with run 010's FFP fixes): the shader compiled, the screen went white, then 4.3 s after the first `drawView` there was a data abort on the main thread inside a system memcpy (pc=E008C0C8, src r1=0x3F7FFC01, a float, len 0x780 = 80 verts x stride 24). This is the copy vitaGL makes of a client vertex array (`SAFER_DRAW_SPEEDHACK`, `sceClibMemcpy`). A stack-scan entry points at macho 0x518E (`-[EAGLView drawView]`, the frame's `presentRenderbuffer:`), but that entry may be stale. The exception handler then crashed itself: its 2 KB stack scan read past the main stack top (81B01000). Fixes: (1) `bad_arrays()` in gles.c runs before every glDrawArrays/glDrawElements. It skips a draw whose enabled client array points below 0x81000000 and logs `[GL] draw from macho 0x... skipped: array k ptr ...` (once per caller). (2) The handler now stops its scan at the stack top (main-thread bounds come from sceKernelGetThreadInfo) and also prints loader/vitaGL return addresses (`stack (elf, addr2line)`). If the next crash is not caught by the guard, the bad pointer lives inside vitaGL (e.g. lighting streams): use those ELF frames.
- Run 012: same crash, and this time with an ELF backtrace (new handler): `ctx_present` → `gles_present:729 glClear` → vitaGL `update_scissor_test` → `sceGxmSetFragmentProgram(scissor_test_fragment_program)`. The BSS global itself (0x8158DC3C) held 0x3F7FFC01, a matrix float (≈cos of a small angle). This is a matrix-stack overflow: vitaGL is built with `SKIP_ERROR_HANDLING`, so glPushMatrix has no bounds check, and both `modelview_matrix_stack` (32) and `projection_matrix_stack` (2) start at addresses ending in ...8. Their 113th or 81st entry is exactly 0x8158DC38..78 (tests.c BSS). On iOS an extra push is only a harmless GL_STACK_OVERFLOW. Fix: `g_glPushMatrix`/`g_glPopMatrix` (gles.c) track depth per stack (modelview 32, projection 2, texture 2 per unit). Overflow/underflow is ignored and logged once with the macho caller (`[GL] glPushMatrix overflow ... from macho 0x...`). gles_present uses the same guard and only pops what it pushed.
- Load time (log 012: 124 s Firemint splash → intro, still `PARTIAL rb/XX/`): ~480 bundle opens at ~250 ms each, nearly all still served from the flat 2149-entry `res/` because the FTP rename migration never finished (console sleeps, FTP drops). The bucketing moved to the Mac: `stage_data.sh` stages res/ files straight into `game_app/rb/XX/` + `rb/.done` (hash checked equal to darwin.c for all 2148 files; `res/f/` stays). `deploy_data.py push_rb()` uploads rb/, renaming files still in the console's old res/ instead of re-uploading, and writes `.done` last. Crash in log 012 is the matrix-stack overflow already fixed above (build 21:03 has `g_glPushMatrix`).
- Player install simplified (no PC tools): `rebase.bin` ships in the VPK (`app0:rebase.bin`, CMake `FILE`; ux0 copy wins for dev) with an `EXE1` trailer (exe size + crc32, written by macho_prep.py; the loader refuses another executable). `darwin_res_buckets()` now migrates a plain `.app` on first boot: sceIoDread of res/, sceIoRename into rb/XX (already-moved files count as done), progress bar from main.c `boot_progress` (default framebuffer, auto-suspend disabled), `rb/.done` only if nothing failed; time logged as `[IO] moved N files in S s`. Not measured on console yet. `test_macho_prep.py` cases 0x41C1D8/0x1368BC are from another game (XPlayerLib) and fail regardless.
- Run 013 (bucketed rb/XX): boot → menu in ~30 s (was 124 s). Crash entering a race: data abort in `free` ← `mpg123_delete` ← `af_Close` ← `BackgroundTrackMgr::Teardown` (macho 0x3423). `LoadTrack` closes a queued playlist track at once but keeps its AudioFileID at BG_FileInfo+0x200; Teardown closes it again (iOS returns an error, we double-freed). Fix: coreaudio.c keeps a live-handle table (`af_live`), `AudioFileClose` of an unknown ID returns kAudioFileInvalidFileError. Removed the temporary yellow load bar (patch_overlay / g_load_opens / gles_loading_tick); the first-boot migration bar stays.
- Run 014: in-game race reached, smooth. (1) Entering a race froze ~40 s with no screen update: BackgroundTrackMgr opens the whole playlist (~10 MP3s, VBRI headers that mpg123 does not use for length) and each `mpg123_scan` took 2-4 s through mpg123's unbuffered read() per frame. Fix: MP3s are read via `mpg123_replace_reader_handle` on a FILE* with 64 KB setvbuf (coreaudio.c mp3_rd/mp3_sk). (2) Steering reversed with STEER_INVERT=0: sign flipped in `patch_accel` so the default is right. (3) Minor glitch in the car shadow: pending a photo.
- Shadow glitch (screenshots 2026-11-10 11:01): the car shadow is jagged black spikes on the road = z-fighting. `CarAppearance::renderShadow` draws a road-plane quad with `renderer_setZBias(-64)` → `glPolygonOffsetx(-64, -64)` (GL_POLYGON_OFFSET_FILL enabled at init), tuned for the iPad's `GL_DEPTH_COMPONENT16`; vitaGL renders to DF32M where one unit is ~256x smaller. Fix: `g_glPolygonOffsetx` (gles.c) scales units by `DEPTH_BIAS` (controls.txt, default 256). Run 017 (Vita clock ~1 h behind the Mac): shadow still wrong; the log has no bias line, so it is unknown whether the scaled offset reached GXM. Added a one-time `[GL] glPolygonOffsetx` log line; next run try `DEPTH_BIAS = 4096` in controls.txt; the green speckles on car edges are a separate issue.
