# Building

Developers build from source; players use the AppImage (README.md), which does all of this for
them on first run.

You need gcc, make, Python 3 with Capstone (`pip install capstone`), the Vulkan loader and
headers, FFmpeg's development libraries (libavformat, libavcodec, libswscale, libswresample,
libavutil), zlib, and SDL3 3.4.18 or later built as a static library in third_party/sdl3
(third_party/README.md says how). glslangValidator is needed only after a shader changes.

1. Put the game's files in ~/.local/share/hardtruck2-native/game from your own GOG copy, with
   either the installer or an installed folder:

       python3 tools/install.py setup_hard_truck_2_king_of_the_road_1.3_amd_rdna_fix_(52932).exe

   Every file is checked against builds/ht2_gog13.manifest.sha256; only GOG's 1.3 release is
   supported (king.exe's hash is in builds/ht2_gog13.py).
2. `make` translates king.exe into gen/ and builds build/ht2. `make install` adds it to the
   applications menu.
3. `build/ht2` runs the game in a window. `--test` makes a run deterministic (for --hash and
   frame dumps); DESIGN.md describes the checks (fncheck, the Unicorn reference build and
   lockstep).

The player download: tools/release/linux.sh OUT_DIR, in a checkout of its own, with the pieces
third_party/README.md lists. Then tools/release/windows.sh OUT_DIR in the
same checkout makes the Windows zip, cross-built with Zig (no Windows needed; third_party/win).
tools/dev/wine_lowfree.c says how to test it under Wine.
