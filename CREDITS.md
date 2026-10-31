# Credits

- **Hard Truck 2: King of the Road** by SoftLab-NSK (published by Buka). None of the game is in this
  repository or the download: each player translates their own GOG copy.
- The translator and runtime started from Test Drive 6's port, which came from
  [1NSANE: Recompiled](https://github.com/AntAir267/1nsane-recompiled) (public domain).
- The icon is drawn for this project (lettering only, no game art).

Libraries the build uses (none is in this repository; see BUILDING.md and third_party/README.md):

- [SDL3](https://github.com/libsdl-org/SDL) (zlib licence): window, input, controllers, audio
  output. Linked statically.
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1 or later): libavcodec, libavformat, libswscale and
  libswresample decode the game's Indeo 5 movies and its MP3 music. Developer builds use the
  system's shared libraries.
- [Capstone](https://www.capstone-engine.org) (BSD licence): the translator's disassembler.
- [Unicorn](https://www.unicorn-engine.org) (GPL 2): only in the developers' reference build
  (`make build/ht2_ref`) and function checks, which run the original code to check the
  translation; never part of the game.
- Vulkan (the system's loader and driver), zlib.

The AppImage also carries (their licences are in it, under usr/lib/hardtruck2-recompiled/licenses):

- [FFmpeg](https://ffmpeg.org) 8.0.1 (LGPL 2.1 or later), built small and static: only the
  Indeo 5 and MP3 decoders, with its source tarball and build options.
- [Zig](https://ziglang.org) 0.17.0 (MIT licence; LLVM's libc++, libc++abi and libunwind inside it):
  compiles the game on the player's computer.
- [Python](https://www.python.org) 3.14 (PSF licence), from python-build-standalone: runs the
  translator and the installer.
- [innoextract](https://constexpr.org/innoextract/) 1.9 (zlib licence; Boost, bzip2, liblzma and
  libstdc++ inside it): unpacks the GOG installer without running it.
- SDL3 and Capstone, as above.

The Windows zip carries the same, in their Windows builds (Zig for Windows, Python from
python-build-standalone for Windows, Capstone's Windows wheel, innoextract's Windows build), and
[zlib](https://zlib.net) (zlib licence) and the MinGW-w64 runtime (in Zig; its COPYING is in the
zip) linked into the game.
