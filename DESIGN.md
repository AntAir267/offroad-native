# Off-Road: Redneck Racing native: design notes

A native Linux port of Off-Road: Redneck Racing (Rage / Interplay, 2001), by static recompilation of
the original OffRoad.exe: the game's own x86 code is translated into C and built against a stand-in
for Windows, DirectX 7, Miles and Bink on SDL3 + Vulkan. Same method as Hard Truck 2 native and
td6-native. Bring your own game: the repository holds only our tools and runtime, never the game's
code, data or the generated C.

## Decisions

### 1. Goal (Ant, 2026-10-30)
"Off-Road Redneck racing, recomp/vulkan, same standards as other projects." See GAME-GOALS.md:
faithful by default, options in the game's own menu, overhead removed, enhancements later.

### 2. Translation and runtime: fork of hardtruck2-native (2026-10-30)
OffRoad.exe is the same kind of program (32-bit MSVC, static CRT, DirectDraw/D3D7). tools/ is the
translator unchanged; runtime/ is HT2's minus storage, DirectShow, Video for Windows and its native
function rewrites, plus etm-native's dsound.c and mss32.c (voices) and td6-native's bink.c.

### 3. The game and its files
- ~/Games/Off-Road - Redneck Racing (ZOOM Platform release, read only; linked as
  ~/.local/share/offroad-native/game). Writes go to the user folder.
- OffRoad.exe: 1,798,144 bytes, sha256 15545ebd..., linked 2001-04-10, same file as OffRoadBump.exe
  (bump mapping on); OffRoadNormal.exe and OffRoadMatrox.exe are other builds of the same code, not
  used. Base 0x400000, entry 0x56ed2d. The CD check (0x505e60) ignores the label in this release
  [verified: decompile]. The folder's DDraw.dll, D3DImm.dll, d3d8.dll are ZOOM's wrappers: not used.
- Existing decompile: ~/DevWork/offroad-rip/work/ghidra/decomp (one file per function, index.tsv).
- Setup.exe writes HKLM\Software\Rage\Offroad\Options; with no ResolutionX the game runs Setup.exe
  and quits (0x4c7650), so advapi32.c settings_defaults gives it 1024x768x32, Textures32.
- Data: Main.mng, Levels.mng (zlib archives, read by the game itself), Audio\Samples, Movies\OffRoadIntro.

### 4. What the stand-ins do for this game
- Miles 6.0m (mss32.c): see its head comment. Only "Miles Fast 2D Positional Audio" is offered.
- Bink (bink.c): the record is +8 frames, +0xc current; formats 3 (32-bit) and 10 (565).
  BinkGetSummary must write only width and height: the game's buffer is near the frame's end, and
  clearing 0x80 bytes overwrote a return address [verified: crash 2026-10-30].
- DirectPlay (dplay.c, HT2's, no connections): the Network Play screen uses it; without the object
  it crashed at 0x47e320 [verified: run 2026-10-30]. Network play itself is not done.

### 5. Checks done
- Ghidra cross-check of discovery against offroad-rip's decompile: 29 of its functions missing; 23
  added to builds/orr_zoom.py EXTRA_ENTRIES (the other 6 are fragments jumped into in the CRT's
  floating-point code or an import thunk) [verified: disassembly, 2026-10-30].
- 2026-10-30, --test on the GPU off-screen (tools/dev/shot.sh): intro movies, main menu, Time Trial
  through vehicle select and track setup, loading, countdown, driving Everglade Encounter for 40 s
  with scripted keys, Network Play screen; no crash; sound mix not silent throughout (ORR_SOUND_WAV).

## Test recipe
    SIZE=1024x768 bash tools/dev/shot.sh work/s 5700 300 "100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*2600"
Frame dumps land in work/s as .ppm. (Escapes after the movies move the menu's selection to Exit Game,
so the UPs reach Time Trial.)
