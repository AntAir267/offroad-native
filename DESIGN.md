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

### 6. Widescreen: unfinished, off by default (2026-10-30)
Plan: stay in the 1024x768 mode so the screens, HUD and movies keep their layout (the runtime's side
space, c->ox), and widen only the race's cameras. Tried first and dropped: a wide game mode
(ResolutionX 1366; needs the mode listed in EnumDisplayModes and accepted by SetDisplayMode, else the
game falls back to 640x480x16). The 3D is then a true picture (focal lengths are width / 640 on both
axes, so it is Vert-), but the HUD and menus scale by width / 640 and height / 480, and the gauge by width
on both axes, running off the bottom [verified: frames at 1920x1080].
Done: widescreen.c viewport_hook gives the cameras set from 0x4d94b0 a viewport wider by the aspect's
ratio with the focal lengths divided by it [verified: log, focal 7.5, width 1365.33, centre 512]; 0x524f90
and 0x525880 re-set from the camera's stored values, so must not be scaled again. vk.c places flat
drawings (one rhw for all vertices: HUD and menus 1, the sky's backdrop 2): width-filling ones fill the
screen; race HUD pieces in the outer thirds go to the edges once the world passes the 4:3 picture.
The pretransformed path turns x straight into pixels, so x past 0..1024 lands in the side space with
only the scissor grown. Menus and movies came out right at 16:9.
Open: world vertices still arrive clipped to x 0..1024 [verified: draw dump, frame 3600], so
wide_world never trips, nothing goes to the edges, and the HUD is cut at the 4:3 edge. Something after
0x4f1350 clips to the mode's width. Not the camera's +0x1dc/+0x1e4 (those follow the width). Next
place to look: the rectangle table this+0xcd0 built by 0x4d90f0 from the mode's size, pointed to by
this+0x24.. in 0x4d94b0, and the code in the world's transform that reads it.

### 7. Controller
padgame.c, see its head comment. Ant's own Configs\p1controll.cfg has the Xbox pad as device 1 (his
binds RB accelerate, LB brake); the user folder's first copy gets triggers on accelerate and brake and
the bumpers on the gears. Verified with the scripted pad: movie skip, menus, new Championship through
to a race with 5 AI cars, RB accelerate and stick steering. Triggers [verified: scripted run 2026-10-30,
RT 30 and 46 mph climbing, 53 falling to 42 under LT; how proportional they are not measured].

### 8. Not done yet (next)
Port options in the game's own Game Options menu (GAME-GOALS; menu.c is empty, settings are options.txt
for now). Widescreen (6). Play through by Ant. Real audio device and real pad, only scripted so far.
Network play (DirectPlay stand-in has no connections). Lockstep against the Unicorn reference
(tools/lockstep.py, build/offroad_ref) not run yet. Frame-time and CPU measurements not taken. The
Championship and later divisions not played beyond the first race's start.

### 9. fncheck baseline (2026-10-30)
7050 pass, 15202 skipped, 20 FAIL runs over 8 functions: x87 extended precision against doubles
(0x56dc5a, 0x56dc64, 0x574b16, 0x574d1c) and low bytes of eax left from fnstsw-style sequences (0x4b4da0,
0x515c70, 0x523550, 0x52d8e0) [guess: the same harmless classes as the other ports; not each checked].
tests/fncheck_baseline.txt.

## Test recipe
    SIZE=1024x768 bash tools/dev/shot.sh work/s 5700 300 "100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*2600"
Frame dumps land in work/s as .ppm. (Escapes after the movies move the menu's selection to Exit Game,
so the UPs reach Time Trial.)
