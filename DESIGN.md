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

### 6. Widescreen: on by default (2026-10-30)
The game stays in its 1024x768 mode, so screens, HUD and movies keep their layout in the middle (the
runtime's side space, c->ox), and only the race's cameras widen. widescreen.c:
- viewport_hook, 0x4f1350 called from 0x4d94b0: viewport wider by the aspect's ratio, focal lengths
  divided by it, so the 4:3 picture stays and the world carries on past it. Not from 0x524f90 or
  0x525880: they re-set from the camera's stored values, already widened. Afterwards the camera's
  +0x268/+0x26c go back to 1.6: they size the HUD's gauges (0x4a5a90).
- clip_rect_hook, 0x4f6b20: the renderer's clip rectangle (+0x3ec..+0x3f8, every polygon is clipped
  to it, 0x4f5240). This was what kept the world at x 0..1024 [verified: draw dump, then frames].
- camera_flat_hook, 0x4f17d0: origin and scale for flat drawings as in 4:3, else the HUD follows the
  camera's width.
vk.c (the Off-Road block in the draw): pretransformed x turns straight into pixels, so the world lands in
the side space with only the scissor grown. A frame is a race if the last one drew past the picture.
Flat drawings have one rhw for all vertices (HUD and menus 1, the sky's backdrop 2): in a race one filling
the width fills the screen (backdrop, the pause tint), and one in the outer thirds goes to that edge.
Dropped: a wide game mode (ResolutionX 1366). Its HUD and menus scale by width / 640 and height / 480 and
the gauge by width on both axes, off the bottom of the screen.
Verified at 1920x1080 on the 890M: race, HUD, pause menu, menus and movies. Not checked: split screen.
0x413830 reads the camera's width / 640 for its size (a world effect [guess: sprites]): may be 1/3 wider.
Off via options.txt `widescreen = off` only: no row in the game's Video screen yet (GAME-GOALS wants one).

### 7. Controller and prompts (2026-10-30)
padgame.c, see its head comment. The joystick is laid out as Windows does an Xbox pad (X, Y left stick, Z
triggers). First start writes the user folder's Configs\p1controll.cfg: controller active; RT
accelerate, LT brake, A handbrake, X rear view, Y camera, RB/LB gears, B nitro, force feedback on. This
replaces Ant's old RB/LB on accelerate and brake. Flag bytes: the second is Force Feedback [verified: screen].
Front end: A select, B back, left stick and D-pad move, Start is Escape. Race: Start opens and closes
the menu; B is nitro there, not back. Movies: A, B or Start skip.
Prompts: while the controller is the device used last, text_hook (every line of text, 0x4b5540) shows Xbox
names on the controller screen and "(A) Continue", "(B) Back", "(A) Done", "(A) Race", "(A) Accept",
in the game's own font and English only; they go back to plain when the keyboard is used. No button icons.
"Waiting For Input - Escape To Cancel" stays: Escape is what cancels there.
Verified with the scripted pad, off-screen. A real pad and rumble are not tested.

### 8. Not done yet (next)
Port options in the game's own Video screen (GAME-GOALS; menu.c is empty, settings are options.txt
for now). Play through by Ant. Real audio device and real pad, only scripted so far.
Network play (DirectPlay stand-in has no connections). Frame-time and CPU measurements not taken. The
Championship and later divisions not played beyond the first race's start.

### 9. fncheck baseline (2026-10-30)
7050 pass, 15202 skipped, 20 FAIL runs over 8 functions: x87 extended precision against doubles
(0x56dc5a, 0x56dc64, 0x574b16, 0x574d1c) and low bytes of eax left from fnstsw-style sequences (0x4b4da0,
0x515c70, 0x523550, 0x52d8e0) [guess: the same harmless classes as the other ports; not each checked].
tests/fncheck_baseline.txt.

## T7
tools/dev/translator_check.sh runs fncheck and lockstep. On the T7 third_party/sdl3 and .venv are links to
hardtruck2-native's, and the game is at the usual path.

## Test recipe
    SIZE=1024x768 bash tools/dev/shot.sh work/s 5700 300 "100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*2600"
Frame dumps land in work/s as .ppm. (Escapes after the movies move the menu's selection to Exit Game,
so the UPs reach Time Trial.)
