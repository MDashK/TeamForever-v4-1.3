# Third-party code in x360/

| Path | Component | License |
|---|---|---|
| `external/ogg-vorbis/libogg`, `external/ogg-vorbis/libvorbis` | libogg / libvorbis, Xiph.Org Foundation (sources only) | BSD-style, see each `COPYING` |
| `external/SDL2/include` | SDL 2.32 public headers, Sam Lantinga and contributors | zlib, see the header comments |
| `sdl2lite/` | SDL2-lite: an implementation of part of SDL2's API on the Xbox 360 XDK, written for the Sonic 3 A.I.R. Xbox 360 port and extended here | Same terms as this repository |
| `runtime/rxdk_patched/` | `fileio.c` / `dirio.c` from RXDK-360's libc (Team Resurgent), patched for XDK-style `game:\` paths | GPL-3.0-or-later (see the file headers) |
| `tools/rxdk_upstream/coff2elf.py` | From RXDK-360 (Team Resurgent), used by `tools/fix_rxdk_ppc64.py` | RXDK-360's license (GPL-3.0-or-later) |

The release zips made by `tools/make_release.ps1` also contain `7za.exe` (7-Zip by Igor Pavlov, GNU LGPL 2.1) and, for Sonic 2 Absolute, Team Forever's `absolute.data` (with the Retrun extractor by nextvolume, ValleyBell and Xeeynamo); both come from Team Forever's own setup package and are not part of this repository.

No Microsoft XDK headers, libraries or tools are included: building needs an installed Xbox 360 SDK and RXDK-360 (see `README.md`).
