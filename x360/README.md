# x360: Xbox 360 build of Sonic 1 Forever and Sonic 2 Absolute

Everything specific to the Xbox 360 port lives here, inside the engine repository: the repository root is the TeamForever RSDKv4 1.3 source used for the PS3 port, with this port's (small) engine changes, all marked `RETRO_PLATFORM == RETRO_XBOX_360` unless noted (see "Engine changes" below).

One engine, two XEX files, as on the PS3: `S1F_BUILD` (Sonic 1 Forever) and `S2A_BUILD` (Sonic 2 Absolute).

## Requirements

| Component | Notes |
|---|---|
| Xbox 360 SDK 2.0.21256 | Headers and libraries (not included here) |
| RXDK-360 | The `clang` PlatformToolset for the `Xbox 360` platform |
| Visual Studio 2022 with "Desktop development with C++" | MSBuild (`build/build.cmd` expects `E:\Microsoft Visual Studio\2022\Professional`; edit `MSBUILD` there for another install) |
| Python 3 | Only for `tools/fix_rxdk_ppc64.py` and `tools/addr2sym.py` |
| Xenia Canary | Optional, for testing; with `allow_game_relative_writes = true` in its config (the game writes settings and saves next to the XEX) |

Toolchain: the same as the Sonic 3 A.I.R. Xbox 360 port: `Xbox 360` platform with the **`clang`** PlatformToolset of RXDK-360 (Clang 24, `powerpc64-unknown-xbox360`, libc++/picolibc), XDK headers, C++17.

**One-time step:** RXDK-360 needs the local fixes of `tools/fix_rxdk_ppc64.py` (ELF64 XDK libraries, MSVC helpers, the non-VMX `memcpy` without which D3D hangs the GPU on real hardware, `game:\` paths for `open`/`fopen`). Run it again after reinstalling or updating RXDK-360:

```bash
python -I x360\tools\fix_rxdk_ppc64.py --rxdk E:\RXDK-360
```

### The work folder

The game data can't be in the repository, so the tools in `tools/` take it from a **work folder**: by default the folder that contains the repository (override with `-WorkDir <folder>` or the environment variable `RSDKV4_X360_WORKDIR`; see `tools/paths.ps1`). It holds:

| In the work folder | Used by |
|---|---|
| `Sonic the Hedgehog Forever\` (`Data.rsdk`, `mods\Sonic 1 Forever`) | `deploy_data.ps1`, `make_package.ps1`, `make_release.ps1` (the mod) |
| `Sonic the Hedgehog 2 Absolute\` (`Data\`, `Scripts\`, as made by its setup script) | `deploy_data.ps1`, `make_package.ps1` |
| `Sonic2Absolute\` (Team Forever's PS3 setup package: `absolute.data`, `7za.exe`) | `make_release.ps1` |
| `xenia_canary_windows\` | `xenia_run.ps1`, and F5 in Visual Studio (MSBuild property / environment variable `XeniaDir` to use another one) |
| `hardware_test_vN\`, `release_<version>\` | Created by `make_package.ps1` / `make_release.ps1` |

## Build, deploy, test

```bash
x360\build\build.cmd
```

(from the repository root) builds `x360runtime`, `oggvorbis`, `sdl2lite`, then `rsdkv4_s1f` and `rsdkv4_s2a` (Release; `build.cmd Debug ...` or `build.cmd Release <project> ...` for a subset). Each game's `default.xex` goes to `x360\deploy\<Game>\` (`Sonic1Forever`, `Sonic2Absolute`), next to its `default.elf` (symbols, for `tools/addr2sym.py`; not needed on the console). Or open `x360\RSDKv4.X360.sln` in Visual Studio 2022 (`Release | Xbox 360`; F5 starts the selected game in Xenia).

```bash
powershell -ExecutionPolicy Bypass -File x360\tools\deploy_data.ps1
```

copies the game data from the work folder next to each XEX: `Data.rsdk` + `mods\` for Sonic 1 Forever, `Data\` + `Scripts\` + `mods\` for Sonic 2 Absolute.

```bash
powershell -ExecutionPolicy Bypass -File x360\tools\xenia_run.ps1 -Xex x360\deploy\Sonic1Forever\default.xex -Seconds 30 -Screenshot shot.png -Keys "12:START,16:A" -Shots "14,20"
```

runs a game in Xenia, optionally pressing controller buttons through Xenia's keyboard emulation (`--keyboard_mode=1`; START, A, B, X, Y, BACK, LB, RB, UP, DOWN, LEFT, RIGHT), and takes screenshots.

```bash
powershell -ExecutionPolicy Bypass -File x360\tools\make_package.ps1 -Name hardware_test_vN -Notes <notes.txt>
```

makes `<work folder>\hardware_test_vN\` with both game folders for the console (without symbols, logs, settings or saves).

```bash
powershell -ExecutionPolicy Bypass -File x360\tools\make_release.ps1 -Version 1.0 -SourceUrl https://github.com/<user>/<repo>
```

makes `<work folder>\release_1.0\` with the two redistributable zips, which contain no SEGA data (the user adds their own APK or `Data.rsdk`):
- **Sonic 1 Forever (Xbox 360).zip**: `Sonic1Forever\` (`default.xex` + the Sonic 1 Forever mod), `Install S1F - Xbox 360.bat` (copies `Data.rsdk` / `Data.rsdk.xmf`, or extracts it from the APK, into the game folder), `7za.exe`, `readme.txt`.
- **Sonic 2 Absolute (Xbox 360).zip**: `Sonic2Absolute\default.xex`, `Install S2A - Xbox 360.bat`, `absolute.data`, `7za.exe`, `readme.txt`. The script is Team Forever's PC/PS3 setup: it extracts `Data.rsdk.xmf` from the Sonic 2 APK, unpacks it with Retrun, applies the Absolute patch and the scripts from `absolute.data`, and leaves `Sonic2Absolute\` (`default.xex`, `Data`, `Scripts`, `mods`) ready to copy to the console. `absolute.data` comes from Team Forever's PS3 package (`<work folder>\Sonic2Absolute`), repacked without the PS3's `SonicAbsolute.pkg`. Checked: the patch files and scripts in it are identical to the hand-installed folder tested on hardware.

The templates (setup scripts, readmes) are in `release/`. The setup scripts call `.\retrun.exe` / `.\7za.exe` explicitly: with `NoDefaultCurrentDirectoryInExePath` set, cmd doesn't look in the current folder.

| Path | Contents |
|---|---|
| `build/x360.toolset.props` | clang toolset, XDK headers, C++17, DWARF. Import **before** `Microsoft.Cpp.props` |
| `build/x360.common.props` | Paths (`$(RSDKRoot)`, `$(X360Root)`), include directories, `_XBOX`, force-include of `include/x360_compat.h`, and the header-change invalidation (RXDK's `ClangCompile` has no header dependency tracking; `NativeObjects/ps3/*.cpp` count as headers, as `All.cpp` includes them) |
| `build/rsdkv4.app.props` | The application, shared by `rsdkv4_s1f.vcxproj` / `rsdkv4_s2a.vcxproj`: engine sources, `-fsigned-char` (the engine expects a signed `char`, PowerPC's is unsigned), XDK libraries |
| `sdl2lite/` | SDL2 for the 360 (from the S3AIR port, extended for RSDKv4): SDL2's own headers (`external/SDL2/include`) with the functions the engine uses implemented on the XDK |
| `port/x360_platform.cpp` | Log (`X360_WriteLog`, behind the engine's `PrintLog`), crash handler, return to the dashboard on exit |
| `port/theoraplay_x360_stub.cpp` | Video playback stand-in: every decode fails cleanly (no `.ogv` in either game) |
| `external/ogg-vorbis` | libogg / libvorbis sources (built as `oggvorbis.lib`, pure C) |
| `runtime/` | `x360runtime.lib` (`expf`/`logf`/`powf`/`log2f`, `nothrow` new/delete), plus the RXDK libc patches used by `fix_rxdk_ppc64.py` |

## SDL2-lite additions for RSDKv4

| Area | Implementation |
|---|---|
| `SDL_Renderer` / `SDL_Texture` (`SDL_x360_render.cpp`) | Draws straight to the 1280x720 back buffer of the shared D3D9 device. Streaming textures are linear (`D3DFMT_LIN_R5G6B5` for the engine's RGB565 frame buffer) and double-buffered; `SDL_LockTexture` hands out the buffer the GPU isn't reading (`BlockUntilNotBusy`). Upscaling with the "sharp bilinear" shader: 426x240 becomes 1278x720, exactly 3x. `SDL_X360_SetPresentInterval` sets vsync (`D3DRS_PRESENTINTERVAL`) |
| `SDL_AudioCVT`, `SDL_AudioStream`, `SDL_LoadWAV_RW` (`SDL_x360_audiocvt.cpp`) | Any SDL sample format in either byte order, mono/stereo, linear-interpolation resampling. OGG (via `ov_read`) and WAV give little endian samples, the output is big endian |
| Audio device ids (`SDL_x360_audio.cpp`) | As in SDL2: `SDL_OpenAudioDevice` returns 2, the legacy `SDL_LockAudio` family works on device 1. RSDKv4 opens with the former and locks with the latter, so on the PC its locks do nothing; `PlayMusic` never unlocks, which deadlocked the audio thread when both were the same device |
| Game controllers (`SDL_x360_input.cpp`) | `SDL_GameControllerGetButton`/`GetAxis` on XInput, `SDL_CONTROLLERDEVICEADDED`/`REMOVED` events, an all-zero keyboard state, no haptics |
| Files (`SDL_x360_rwops.cpp`) | `SDL_RWFromFile` on POSIX `open`/`read` (one kernel call per request; the runtime's stdio reads in tiny blocks, which made loading slow on the console in S3AIR), `SDL_RWFromMem`. `SDL_X360_NormalizePath` turns the engine's paths (`game:\/mods/x/Data/...`, `Data/Game/...`) into `game:\mods\x\Data\...` |

## Engine changes (all under `RETRO_PLATFORM == RETRO_XBOX_360` unless noted)

| File | Change |
|---|---|
| `RetroEngine.hpp` | `_XBOX` → `RETRO_XBOX_360` (checked before `_WIN32`, which the toolchain also defines); SDL2 path with `<SDL.h>` (SDL2-lite); `BASE_PATH "game:\\"`; no networking. Scripts see `engine.platformID == RETRO_XBOX_360` (2), which the Forever scripts already treat as a standard (non-mobile) platform |
| `RetroEngine.cpp` | With VSync the present paces the loop (no timer busy-wait on top: in S3AIR that made frames miss the vertical blank). Frame profiler: every 600 frames a `Perf` line in the log (frame time, update+render, flip, frames over 17.5 ms) |
| `Audio.cpp` | Stereo output (the PS3 port used mono), 1024-frame buffers (~70 ms latency with XAudio2's 3 queued buffers) |
| `Music.hpp` / `Music.cpp` | 16 MB per music stream buffer instead of 32 MB (largest track: ~2 MB); music loads on a thread, as on the PS3 |
| `ModAPI.cpp` | The PS3's POSIX `stat`/`opendir` code instead of `std::filesystem` (its Windows variant doesn't match RXDK's libc++), with normalized paths |
| `Userdata.cpp` | Fullscreen forced (as on PS3); Select defaults to BACK (XInput can't read the guide button); `SData.bin`/`SGame.bin`/`UData.bin` stored little endian, i.e. the same files as on the PC |
| `Debug.hpp` | `PrintLog` always goes to `game:\rsdkv4.log` (written by a background thread) and to `DbgPrint` (`xenia.log`), instead of opening `log.txt` for every line |
| `Reader.hpp`, `Renderer.cpp` (all platforms) | 3D model (`R3D`) floats were read as raw host-order floats: now read as little endian (`FileReadFloat`). A real big-endian bug of the PS3 port |

Checked and fine on big endian: the data pack reader and its MD5 (byte-assembled), scene/tile/object/GIF/palette loading, the script compiler and bytecode reader, string lists (UTF-16 assembled from bytes), save structures (all `int`).

## State (19/10/2026)

In Xenia, both games boot in ~1.5 s to their title screens, with music and sound effects (checked in an audio dump: stereo, no byte-swap noise), menus, save select and gameplay (Green Hill / Emerald Hill, attract demos, death and respawn), controller input, and save files. `hardware_test_v1` is the first package for the console.

**Hardware v1 (19/10, RGH):** both games at a constant 60.2 FPS (`Perf`: Sonic 1 Forever ~3.7 ms update+render per frame, Sonic 2 Absolute ~8.7 ms; the rest is waiting for vsync). Two problems, fixed in v2:
- EXIT froze the console (S1F) or crashed (S2A), with `settings.ini` cut short: `ReleaseAudioDevice` freed the music stream, the SFX buffers and `musicMutex` while the XAudio2 feeder thread kept running the mixing callback. It now closes the audio device first (`Audio.hpp`, all SDL2 platforms).
- Missing WAV sound effects: `make_package.ps1` excluded `*.wav` (meant for `audio_dump.wav`). It now only excludes the dump, and checks that every file of the original game data is in the package.

The credits of both games are the original SEGA ones (stage tiles / sprite sheets); there is no "ported by" text to edit.

Not done: video playback (`.ogv`, unused by both games), keyboard-only dev features (dev menu, F-keys).

## Diagnostics

- `game:\rsdkv4.log`: everything `PrintLog` writes, with timestamps, `Perf` lines every 600 frames, and `!!! CRASH` records (exception, registers, back-trace). Symbolize with `python -I x360\tools\addr2sym.py x360\deploy\<Game>\default.elf --log rsdkv4.log` (the `.elf` of the same build).
- `game:\dump_audio.txt` present → the first 20 s of audio output also go to `game:\audio_dump.wav`.
- In Xenia, `DbgPrint` lines appear in `xenia.log` (`RSDKv4: ...`, `SDL2-lite: ...`).
