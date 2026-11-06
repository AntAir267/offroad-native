# Off-Road: Redneck Racing, native

Off-Road: Redneck Racing (Rage Games / Interplay, 2001) running natively on Linux, on SDL3 and Vulkan.

The game's own `OffRoad.exe` is translated into C on your machine and built against a stand-in for the parts of
Windows it uses: DirectDraw and Direct3D 7 (drawn with Vulkan), DirectInput, DirectSound with Miles, and Bink.
**This repository holds no part of the game.** You need your own copy, and the build makes its translation from it.

## Where it stands

- Menus, races, cars and tracks work: Time Trial, Challenge and Championship races have been played and tested
  with scripted runs. It hasn't yet been played from start to finish, so expect rough edges.
- The game's own logic runs unchanged. A reference build runs the original machine code in an emulator, and its
  memory has to match the translated build's, frame for frame, before a change goes in.
- Linux only for now. It runs on the Steam Deck.

## What it adds

Every addition is a row in the game's own **Game Options** menu (Video > More Options, Audio > More Options,
and More Options > Lighting), and the original look is always one setting away.

- Widescreen at any aspect ratio, any resolution, frame rates above 60 (in-between frames are drawn between the
  game's 60 ticks a second; the game's timing is untouched).
- Any controller, plugged in at any time, with the keyboard still working, and Xbox button prompts in the menus.
- Anti-aliasing: FXAA, TAA, FSR 2, DLSS (NVIDIA RTX only), 2x and 4x supersampling, with a render scale for the
  temporal ones.
- Higher colour precision, texture filtering, ambient occlusion, volumetric dust, a far and an extreme draw
  distance, and a fog switch (off by default).
- Water simulated on the GPU: puddles and the lake have volume and flow, cars push the water aside, and it comes
  back. Reflections, spray and wet ground come from the simulation.
- Ray-traced lighting for the tracks, car headlights that light the road, and car shadows. **The light maps
  aren't included**: they're made from the game's track data, and the tools that export it aren't public yet.
  Without them the game uses its own lighting, and so does the simulated water.
- A streamer mode that hides the paint jobs with the Confederate flag.

## Building

You need: gcc, make, cmake (for SDL3), Python 3 with `capstone` (5.x), Vulkan headers and loader, zlib, and
FFmpeg's development libraries (libavformat, libavcodec, libswscale, libswresample, libavutil).
`glslangValidator` is only needed if you change a shader. Optional: `unicorn` 2.x for the checks below.

1. Install Off-Road: Redneck Racing. The build is made for the ZOOM Platform release (`OffRoad.exe`, 1,798,144 bytes,
   sha256 `15545ebd43a9b1cad09a7047a52c66005f4b5a52f7d8cdf3c2ce5166dc510a64`). Other releases may need changes in
   `builds/`.
2. Build SDL3 3.4 as a static library into `third_party/sdl3` (the commands are in `third_party/README.md`).
3. Translate and build, pointing at the folder with `OffRoad.exe`:

       python3 -m venv .venv && .venv/bin/pip install capstone==5.0.9
       make GAME=/path/to/the/game

   The first build translates the whole program (a few minutes); later builds only recompile what changed.
4. Run `build/offroad`, or `make install GAME=/path/to/the/game` to put it in `~/.local/opt/offroad-native` with
   an applications-menu entry. Its settings and saves live in `~/.local/share/offroad-native`.

FSR 2 is built in when its source is in `third_party/FidelityFX-FSR2`, and DLSS when NVIDIA's SDK is given with
`make DLSS=/path/to/DLSS` (see `third_party/README.md`).

## Checks

`tools/dev/translator_check.sh` runs both of the project's checks: every translated function against the original
code under an emulator (fncheck), and a scripted run of the menus, a race's loading and driving in which the
translated and reference builds must leave the same memory on every frame (lockstep). It needs `unicorn`.

## Licence and credits

The Unlicense: public domain (see `LICENSE`). Off-Road: Redneck Racing belongs to its owners, and nothing of it is
here. See `CREDITS.md` for where the code came from, and `DESIGN.md` for how it all works, with the details of every
fix. Contributions are welcome: please read `CONTRIBUTING.md` first.
