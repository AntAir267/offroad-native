# Hard Truck 2 native: design notes

A native Linux port of Hard Truck 2: King of the Road (SoftLab-NSK / Buka, 1998-2002; "Дальнобойщики
2"), GOG 1.3, by static recompilation of the original king.exe: the game's own x86 code is
translated into C and built against a stand-in for Windows and DirectX 7 on SDL3 + Vulkan. The
same method as 1NSANE: Recompiled and td6-native, whose translator and runtime this project forks.
Bring your own game: the repository holds only our tools and runtime, never the game's code, data
or the generated C.

## Decisions

### 1. Goal (Ant, 2026-10-26)
"Decompile this to run on Linux with Vulkan and add native controller support." As the other
ports: faithful by default (physics, AI, timing, look; original bugs kept except crashes), extras
as options.

### 2. Translation and runtime: fork of td6-native (2026-10-26)
Why: king.exe is the same kind of program as TD6.exe (32-bit MSVC, statically linked CRT,
DirectX 7 through DirectDrawCreateEx). tools/ is td6-native's translator unchanged at the fork;
runtime/ is its runtime minus the TD6-only parts (bink.c, wma.c; widescreen.c and padgame.c are
empty stand-ins until the game's own hooks are found). builds/ht2_gog13.py holds this build's
knowledge (addresses only).

### 3. The game and its files
- Source: Ant's GOG installer setup_hard_truck_2_king_of_the_road_1.3_amd_rdna_fix_(52932).exe in
  ~/Downloads/Games, unpacked with innoextract (never run) into
  ~/.local/share/hardtruck2-native/game (554 MB; the installer's tmp/, app/, commonappdata/ and
  __redist/ left out). GOG's ddraw.dll there is its "DirectX 1-7 wrapper" (dxcfg.exe/ini
  configure it): not used, our runtime is DDRAW.dll.
- king.exe: 2,908,160 bytes, sha256 4412a5f6..., linked 2002-03-19, base 0x400000, no
  relocations, entry 0x61f57a. .text 0x401000 (2.3 MB), .rdata 0x64a000, .data 0x66b000,
  .rsrc 0x77f000. MSVC with a static CRT and MFC-style classes (CTempGdiObject); many debug
  strings with C++ function signatures (AI, players, orders, police, mafia).
- Imports: KERNEL32 124, USER32 114, GDI32 36, DDRAW (DirectDrawCreateEx, DirectDrawEnumerateA),
  DINPUT (DirectInputCreateA), DSOUND #1, WINMM (timeGetTime, mmio*), MSVFW32 (ICOpen,
  ICDecompress, ICSendMessage, ICClose: the movies), WSOCK32 (8 by ordinal: network play),
  ADVAPI32 (registry writes), ole32 (CoCreateInstance, structured storage), OLEAUT32 #4 #6,
  WINSPOOL (printer), SHELL32 (drag and drop), COMCTL32 #17. DirectPlay error strings: network
  play through DirectPlay (CoCreateInstance) [guess].
- Settings: TRUCK.INI ([ENV] xres/yres/fullscreen/d3d, [RENDER] fog, xpyramid/ypyramid = the
  view's half-angles [guess], [EFFECTS], [JOYSTICK], [DRVn] per display driver), kmjin.ini (keys
  [guess]), WEATHER.INI.
- Movies: kotr.avi, kotrDemo.avi (and KotrNewgame.avi named in the exe), Indeo Video 5 (IV50),
  decoded by the game through Video for Windows (ICDecompress): FFmpeg has an Indeo 5 decoder.
- Music: MUSIC\track01l.mp3..track12, found with "?*.mp3"; how the game decodes MP3 is not known
  yet (no ACM or DirectShow import; maybe loaded at run time) [guess].
- Data: COMMON\, ENV\ (maps: *.b3d models, *.res resources, *.way roads), MENV, MTB, MTR, *.sch
  schemes, *.bne, *.fon fonts, LOAD.RES, MENU.SFT.

### 4. What the runtime adds for this game (2026-10-26)
- Translator: `enter`, `xlatb`, 16-bit `shld/shrd`, `bt/bts/btr/btc` (memory with a register
  offset: the CRT's charset bitmaps), moves from/to `ds`/`es` (flat, 0x23), and the MMX of the one
  routine that has it (0x5fe182, a colour fade: movd, punpcklbw, pmullw, paddusw, psraw,
  packsswb, emms; registers in CPU.mm[]). The 8-bit `div` had a `%%` bug (fixed here, also in
  td6-native's copy [guess: same bug there, not checked]). Result: 12,271 functions, no
  untranslated instruction. Jump tables in builds/ht2_gog13.py [verified: disassembly].
- Windows: king.exe is an MFC 4.2 application (AfxFrameOrView42s windows, CBT-hook subclassing,
  OnIdle game loop). USER32/GDI32 and kernel32x.c come from li2-native (LA Rush's multi-window
  layer); inifile.c (li2-native's, plus section/key listing: kmjin.ini's joystick sections are
  read by listing their keys); winmisc.c (string resources, OLE BSTRs, file mappings in files.c,
  and failing stand-ins for WSOCK32, WINSPOOL, drag and drop); LoadLibrary/GetProcAddress find
  any runtime function by name (li2-native's approach): the game loads dsound.dll itself.
- dplay.c: an IDirectPlay4A with no connections. The game stops with a fatal error without one
  (D1gate.cpp) [verified: run]. No network play.
- storage.c: OLE structured storage (real compound files, read and written): player profiles
  (DRIVER.pl1: streams settings, ver, $dev_000, storage listofgames) and the shipped movie.mvp.
  Checked: our files' directories read back correctly with an independent script.
- mmio: opening by a file handle (adwInfo[0]) as well as by name and from memory.
- ddraw: SetCooperativeLevel accepts NORMAL with EXCLUSIVE|FULLSCREEN (the game asks for both).
- game.c: hooks for this game. image_ready (0x5b9ec0): see the comment there (an on-demand,
  non-square image drawn as a surface had no pixels in Direct3D mode).
- Renderer: the shipped TRUCK.INI has d3d=off (the 8-bit software renderer, which draws through
  GDI in a window here: nothing shown yet); d3d=on uses DirectDraw 7 + Direct3D 7 (HAL device,
  640x480x16 for the menus) on the Vulkan renderer. Tests use a TRUCK.INI with d3d=on in the
  test user folder.

### Open (see the handoff)
- Movies: the game reads kotr.avi itself (mmio) and decodes through MSVFW32 (Indeo 5), and asks
  GetAVIInfo.dll (an ATL COM server shipped with the game, CLSID {568df44f-...}, interface
  {5f3b918f-...} through IID 0x67e348 [guess: which is the class and which the interface]) for the
  file's layout. Both missing: the game skips the movies with "avi subsystem error".
- C++ exceptions: not supported (tdor3-native's eh.c + tools/eh.py would bring them).
- Reference: run the original under Proton to compare (no Wine or virtual display installed;
  GE-Proton and gamescope are).

### 5. Renderer and settings (2026-10-26)
- The game as GOG ships it (TRUCK.INI d3d=off, cres=1) is the port's: menus are 8-bit
  palettized DirectDraw at 800x600 (mode search 0x5d4240: 1 byte per pixel =
  DDPF_PALETTEINDEXED8), presented each frame by Blt(primary <- an 800x600 P8 system-memory back
  buffer); the loading screen too; the 3D world is Direct3D 7 (640x480x16, DrawPrimitive /
  DrawIndexedPrimitive[Strided]) even with d3d=off [verified: run logs, frame dumps]. The runtime
  offers 8-bit modes and the Vulkan upload turns indexes into colours (ddraw_palette_argb;
  SetEntries marks 8-bit surfaces dirty). The software rasterizer (swrast, used when Vulkan is
  off, e.g. a plain --test run) has no palettes: test runs that look at frames set
  HT2_GPU_OFFSCREEN=1.
- d3d=on: the loading screen becomes a Direct3D session (640x480x16, or 800x600x16 with cres=2)
  and its first overlay image crashes at 0x5d6f45 (a non-square on-demand 16-bit image copied
  from NULL). The original does the same under Wine (GE-Proton 10-32, gamescope headless,
  work/ref-wine: its logs; the game copy and Proton prefix were deleted - to redo, copy the game folder there with the test TRUCK.INI, drop GOG's ddraw.dll, and run GE-Proton's proton script with STEAM_COMPAT_DATA_PATH in that folder under gamescope --backend headless) [verified]: GOG ships d3d=off probably for this. game.c's image_ready hook
  avoids the crash, but the menus then show the loading screen's 16-bit surfaces in their 8-bit
  screens (black). Not pursued: d3d=off already gives Direct3D for the 3D world.
- fullscreen must be on: the game's windowed mode draws through GDI (nothing shown). game.c makes
  the user folder's TRUCK.INI from the game's at the first start and keeps fullscreen=on; the
  port's own window (or full screen) shows the game's picture scaled.
- The test clock: the game calibrates its clock over Sleep(2000) at start-up (0x6035f0, against
  timeGetTime); --test runs' Sleep on the main thread moves the virtual clock (other threads'
  Sleep is real time: the music thread retries DirectShow every 0.4 s).

### 6. Controllers (2026-10-26, padgame.c)
- The game reads joysticks with DirectInput (c_dfDIJoystick, listed once at start-up; dead zone
  20 % except X) and maps them with kmjin.ini (section = DirectInput product name; "J A2+",
  "J B1", "J povB1"). The keyboard comes through window messages.
- Every controller is shown as "Xbox Controller"; the user folder's kmjin.ini is the game's with
  our section first (written when missing). Layout: left stick steers, RT accelerates, LT brakes
  (the triggers are one axis: the profile says ACCEL = J A2+, which the game's screen shows as
  A2-), RB/LB gear up/down, A parking brake, B horn, X start engine, Y hook trailer, Back map, L3
  headlights, D-pad up pager / down wipers / right load cargo / left unload. The camera actions
  take buttons only (axes 4/5 and button 10 are not accepted there), so while driving the right
  stick and R3 are keys: look left/right (Del/PgDn), camera closer/further (Home/End), outside
  view (V). Start is Escape while driving; in the 8-bit menus D-pad/left stick are the arrow
  keys, A/Start Return, B Escape (leaves a sub-screen, menu.c), RB Tab (next group of buttons).
- A new player profile's device is the controller when one is connected (hook on 0x5275c0: as
  the CONTROLLERS screen's arrow, 0x530730(1) and 0x512ce0 on the game object, then [EFFECTS]
  control=1). An existing profile keeps its choice.
- Verified headless with the scripted controller: menus, a fresh install driving with RT and
  the left stick, look right, outside view. Not yet tried with a real pad by Ant.
- Rumble (options.txt rumble = on|off, default on): the controller is a force-feedback joystick.
  The game's effects [verified: its log strings and CreateEffect calls]: cFFBEF_offRoad (sine,
  rough ground: the big motor, up to full on grass), cFFBEF_bounce (short constant forces: bumps),
  a square wave, cFFBEF_Constant (endless constant force: a wheel's steering torque, left off the
  motors), ToCenter / ResistOnMove (spring, damper: not offered, the game goes on without them).

### 7. Resolution and widescreen (2026-10-26)
- resolution = native (the default here): the renderer draws render targets at the size the
  picture has in the window (vk.c, from td6-native); the game keeps its own 640x480 / 800x600
  coordinates. Its GRAPHICS screen still offers 640x480 to 1600x1200 at 16 bits.
- widescreen = on (default; off, or an aspect such as 16:9): Hor+ (widescreen.c). The world
  camera's projection is made by the game from a fixed horizontal view ([EFFECTS] xpyramid,
  camera +0x58; y scale = x scale * viewport width/height: 1.02152 / 1.52041 for the 640x430
  driving viewport) [verified: run log]. d3d.c keeps the program's x scale and, for a
  perspective projection with a full-width viewport, uses it divided by the widening
  (widescreen_projection_x); vk.c spreads draws made with such a projection across the wider
  picture (proj_widened) and keeps everything else 4:3 in the middle: the instrument bar, the
  pager, messages, menus (8-bit screens), the small 101x76 view (its own projection). GetTransform
  gives the program its own matrix back.
- Culling (corrected 2026-10-27): the game draws only what is inside its [RENDER] view pyramid
  (TRUCK.INI xpyramid/ypyramid 1.2/0.9, render object +0xbe8/+0xbf0), separate from the
  projection's [EFFECTS] xpyramid. With only the projection widened, objects outside the 4:3 view
  were dropped or cut along its edge: dead strips at the sides and pop-in there (Ant, real full
  screen). widescreen.c hooks 0x5df9a0 (to the camera +0x58/+0x54) and 0x4e04c0 (to +0x24a0) and
  widens xpyramid by the same factor while they run [verified: frames at 2560x1440 identical to a
  TRUCK.INI xpyramid of 1.6]. Whether the game also uses +0x24a0 for traffic placement is not
  known [guess: render only]. The earlier "same draw counts" check was wrong: dead end, do not
  rely on draw counts for culling. Off in --test runs unless HT2_WIDESCREEN=1.

### 9. Options in the game's own menu (Ant, 2026-10-26)
GAME-GOALS.md (~/DevWork/Games) asks for every setting a port adds to be in the game's own
options menu, in its style; options.txt stays for testing and power users. Ant chose the
game's own menu over an overlay. Defaults: native resolution and widescreen on (Ant's choice; GAME-GOALS allows enhancements
on at launch as long as the original look stays reachable in the options). Everything else
starts original.
Done (menu.c, 2026-10-26): MORE OPTIONS, a second page of the GRAPHICS screen, built only from
the game's own parts (toolkit map: work/decomp/menu/MENU.md). An "aright" arrow + "More" right of
AUTODETECT opens it (keyboard/pad: Up from AUTODETECT, then Return/A). The page is the
CONTROLLERS screen's panel (c\backCtrlOpt.txr at 200,153, its rows made with 0x512eb0 on the
same 20-px grid, DEFAULT c\def, CANCEL c\canc); the device box at the top holds "More Options"
(LARGE.FON) between the arrows, the left one goes back, the right one is disabled. Rows: Display,
Resolution, Widescreen, Anti-aliasing, Textures, Colours, Vsync, Frame limit | Rumble, Dead zone.
Each row's two small cells are merged on the menu surface after the screen's draw (the divider
covered with the cell's own pixels; surface +0x18 = pixels [verified: probe]) and the value is
drawn there (MENU.FON; y given to 0x5e97a0 is the text's bottom [verified: 0x5e995c]).
Return, a click or Right = next value, Left = previous (a copy of the text row's vtable with its
key slot replaced), Escape = back to the first page (same for the page container). Every change
applies at once (options_changed: widescreen, vk_options, win_options, frame limit, dead zone)
and is written to options.txt; CANCEL (cmd 2, which also closes the screen as the game's does)
puts back the values the page had when first opened. render_thread stays file-only (needs a
restart). Hooks: 0x515460 (ctor), 0x515ac0 (OnCommand; the original runs first, it ignores ids
above 0x8a), 0x5162e0 (draw; the card name is blanked while the page shows). Command ids 0x11c..
with tooltips in mess slots 310+.
Findings: the game's panels have art in the focused state only (T_FOCUSED 0x6cd368 has no image),
so with the focus outside a panel it is not drawn and old pixels show; our panel gets the art in
all four states. Arrows move inside a group only; Tab (pad: RB, added) goes to the next group:
from the rows Tab reaches CANCEL, then DEFAULT [verified: runs 78-81].
Escape (pad B) now leaves a main-menu sub-screen (Ant, 2026-10-26: before, only the mouse could,
by clicking another round button): hook on 0x5d8260 (a screen's input). An Escape nothing on the
screen takes comes back from the root as 0x801b (0x5bf520), i.e. OnCommand(0x1b), which no
screen uses (switch tables checked); instead the screen is closed as the main menu's switch does
(0x527130): GRAPHICS/CONTROLLERS/CHANGE DRIVER get OnCommand(1) first (save, may veto via
0x674a28), then hide (vt+0x20), main +0x174 = 0, main vt+0x24. Escape on the MORE OPTIONS page
is taken by the page (back to the first page). Verified headless: GRAPHICS (both pages),
CONTROLLERS, SINGLE PLAYER, EXIT ("no"); Start while driving still opens ON THE ROAD.

### 8. options.txt (2026-10-26)
Written at the first start (outside test runs) listing every option at its default, commented
out (options.c): display, resolution (native default), widescreen (on), antialiasing (off | 4x),
textures (original | smooth), colour_precision (original | high), vsync, frame_limit,
render_thread, rumble, controller_deadzone. Checked in driving frames: 4x (drawn at 2x each way),
smooth and high change the picture and run. Not listed: antialiasing taa / fsr / dlss (td6-native's
temporal anti-aliasing follows TD6's scene; not checked here, FSR 2 and DLSS absent on the 890M),
menu_fps (no menu/driving test for this game: it does nothing), render_scale (temporal only).

### 10. Roadmap after the options page (GAME-GOALS.md, 2026-10-26)
- Fully functional first: a play-through start to finish with no known crashes (Ant testing),
  then the project's checks (lockstep with --seed, fncheck) passing on a race.
- Lockstep (2026-10-26): translated and reference builds match memory exactly through frame 408
  of "skip the movie, single player, new game" (start-up, menus, the options page's
  construction, the start of loading); tools/lockstep.py --seed work/seed --keys
  "200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200" --frames 1800. Made possible
  by: (1) the reference build runs hooked functions as the translated build does (gen/table.c
  hooked_table; Unicorn code hook -> HOOK_TRAMP -> the wrapper), and calls from that translated
  code back into the program run in Unicorn until they return to the pushed return address at
  that stack depth (or above: "ret n"); the stack is not touched (a swapped-in sentinel showed in
  memory); Unicorn only honours `until` in blocks translated afterwards, so the block at the
  return address is dropped first (uc_ctl_remove_cache) - without that the original code ran on
  past the return [verified: runs lockstep2-8]; (2) the --test clock moves only on the main
  thread's readings (other threads see it plus their own), which made two translated runs
  identical. HT2_CALLS_ONLY=1 with --hash-calls lists call sites without hashing (fast).
  Frame 409 (sound thread 1 waking every 191 ms in real time against the main thread) fixed
  2026-10-28: --test runs schedule the game's threads one at a time (sync.c, li2-native's
  scheduler: turns pass at waits, sleeps, thread ends and the main thread's frame ends; timed
  waits time out at frame ends) and the sound clock moves at frame ends only. Lockstep now
  IDENTICAL on all 1800 frames of that run (menus, new game, engine start, driving) [verified:
  T7, work/t7/20261028-004106]. tools/dev/bisect_frame.sh N KEYS finds the first differing call
  in frame N (HT2_CALLS_ONLY for the fast pass).
- fncheck (make check; on the T7 ~2 min): 12040 pass, 16 functions FAIL, all classified harmless
  in tests/fncheck_baseline.txt (x87 status bits, NaN payloads, clock reads, MMX/x87 TOP).
  Compare new runs against that file.
- Overhead, first numbers (2026-10-28, Win Max, NOT quiet: load ~4.5 from other sessions, so
  approximate): off-screen real time at 2560x1440, 4x anti-aliasing, widescreen, HT2_PACE=60,
  parked in the yard: GPU ~8 ms a frame, game thread CPU 5.3-6 ms a frame (HT2_CPU_FROM), the
  process ~0.4 of a core, frames 16.7 ms (slowest ~19). HT2_PROFILE (perf is not allowed here:
  perf_event_paranoid 4; tools/profile.py names nothing, use nm: work/nm.txt): 21.6% of all CPU
  is render_main spinning (2000 pauses each time its queue runs dry, ~550 times a frame), then
  game code (0x5b4670, 0x5af090, 0x5db5c0...), tnl_vertex 2.8%, fpu_fcos/fsin 3.5%.
  Dead end: waking the sleeping render thread only every 32 commands (and spinning 200) starved
  it: 1747 frame-ahead waits, 24 ms frames - because the frame-ahead wait did not wake it (fixed
  2026-10-29, section 11). Next try, on a quiet machine: a shorter spin with
  the per-draw wake kept, measured against this baseline; or handing work over in bigger blocks
  without the thread falling a frame behind.
- Overhead (older note): no measurements yet. To take before any change: frame time, game-thread CPU, GPU
  time and load time (start to menu, menu to driving) on the 890M at Ant's resolution, with the
  render thread on. Likely simple wins: the 8-bit menus upload the whole 800x600 back buffer
  through the palette every frame (only on change would do), per-call lookups in d3d.c.
  Big restructures (direct Vulkan for the game's draws) wait until the game is fully functional.
- Big enhancements (ray tracing, AO, volumetrics) wait for the same; renderer from td6-native
  already has the depth/motion data and toggled passes.

### 11. Native Vulkan renderer (plan, 2026-10-29; Ant's exception in GAME-GOALS.md)
Ant approved building it now: switchable in MORE OPTIONS, the current path the default until he
has played the new one, verified without him on the T7 (same scenes through both paths,
frame-by-frame image comparisons, lockstep, fncheck).

What is there now: the game already draws through our own Vulkan renderer (vk.c, td6-native's),
behind stand-ins for Direct3D 7 (d3d.c: COM methods, state tracking, the fixed-function transform
for CPU-path draws) and DirectDraw (ddraw.c: surfaces, Lock/Unlock copies, 8-bit menus converted
through the palette and uploaded whole every frame), with a command queue to the render thread
(vkthread.c). No DXVK or Wine. So "native" here means removing that D3D/DirectDraw layer and the
per-draw copying, with the game's own engine feeding Vulkan directly.

Inventory [verified: HT2_DRAW_SITES and HT2_PROFILE runs, 2026-10-28/29]:
- CPU while driving: game code 55%, vkthread.c 24% (the render thread's spin, section 10),
  d3d.c 4.6%, fpu.c 4.3% (sin/cos), vk.c 2.3%, Vulkan driver 1.9%. The D3D layer itself is small;
  the queue and its spin are not.
- Draws come from 10 call sites. 0x5b14a0 (DrawIndexedPrimitiveStrided, ~60% of draws, most
  vertices) and 0x5b1000 (DrawPrimitiveStrided, ~35%) are the engine's mesh submitters: strided
  model-space XYZ|NORMAL|TEX1/TEX2 arrays from a vertex record (param +4/+8), Direct3D's transform
  and lighting from matrices set before, clip status (vt+0x6c) and a render state (0x88 =
  CLIPPING) per call, and an optional "global intensity map" (lightmap) stage written per call by
  0x5b0ea0/0x5b0f20/0x5b0f90 into the engine's buffers +0x21f0/+0x41f0 (dynamic). The rest:
  0x5b2000, 0x5b26c0, 0x5c6190 (2 sites), 0x5cd650, 0x5c5c50 (big batches, ~170 vertices),
  0x4cc940, 0x579100 [guess: sky, sun/flares, road marks, HUD-like 3D; to identify].

Design:
1. Option "Renderer: Original / Native" (MORE OPTIONS row, options.txt renderer=original|native,
   default original). Applies at the next level load.
2. Native path, step 1: hooks on 0x5b14a0 and 0x5b1000. They read the game's vertex record and
   index list directly and hand vk.c a compact draw: a GPU-resident mesh (cache keyed by array
   address, count and stride; content hashed when first seen and when the engine's change marks
   say so [guess: to find]; dynamic arrays such as the lightmap stage streamed per draw), the index
   range, the current world/view/projection and the few states that draw changes. No COM calls,
   no D3DState copy into the queue, no per-draw vertex copy. The engine's material and state
   setup (SetRenderState/SetTexture from its own code) still goes through d3d.c at first, and the
   native draw reads the current state; reading the engine's materials directly comes later.
   Same pipelines and shaders as now (tnl.vert/draw.frag), so pictures can match pixel for pixel.
3. Step 2: the other draw sites; the 8-bit menus uploaded only when they change; textures
   uploaded straight from the cluster images; the render thread fed whole frames of compact
   draws (woken per frame, no spin).
4. Step 3: profile both paths on a quiet Win Max; record the numbers here.
Verification for every step (T7, `t7 run --wait`): scripted scenes (menus, GRAPHICS and MORE
OPTIONS, the yard, a drive on keys, look left/right) through both paths with --vk-software
(lavapipe: identical pixels run to run), presents compared frame by frame (identical, or each
difference explained); lockstep on the race run with the native path on (the renderer only
reads game memory); fncheck against tests/fncheck_baseline.txt; --vk-validate on NVIDIA; then
one look on the Win Max (RADV). Risks: dynamic vertex arrays the cache must notice (fall back to
streaming when unsure), state set inside the submitters (clip status, CLIPPING) applied the
same way, and lockstep memory: the hooks must not write game memory.

Step 1 done (2026-10-29): renderer = native (MORE OPTIONS > Renderer; d3d.c native_layout):
draws in the game's own layouts (XYZ, optional normal and colours, at most one 2D texture
coordinate, no blend weights) are packed into tnl.vert's 40-byte record and transformed and lit
on the GPU, any size (vk_tnl_min is TD6's rule); a colour source naming a colour the vertices do
not have takes the material (as mat_source). The existing GPU path only took TD6's exact 0x1d2
layout, so before this every Hard Truck 2 mesh went through the CPU transform. Results [verified:
T7]: 99.2% of draws native (526950 of 531154; the rest TEX2, the lightmapped ones); presents vs
the original path on lavapipe (tools/dev/compare_render.sh): menus identical except the Renderer
row itself, 3D frames differ in ~10% of pixels by under 3% (GPU vs CPU lighting rounding) and in
at most 12 pixels per frame by more (triangle edges); lockstep IDENTICAL on the 1800-frame race
run with renderer = native (work/seed/options.txt); CPU (T7, paced 60, tools/dev/perf_ab.sh):
vertex processing 0.55 -> 0.33 ms a frame, game thread 2.63 -> 2.47 ms, process CPU -3%, GPU the
same. Next: the TEX2 (lightmap) layout; then the queue and its spin (the real cost).
Lightmapped meshes (2026-10-29): XYZ|NORMAL|TEX2 without colours carry their second coordinate
set in the record's colour slots; tnl.vert (flags.x bit 64) reads them back bit for bit
(packUnorm4x8) and gives each stage the set its TEXCOORDINDEX names. Now 100% of draws native
(531154, 0 on the CPU transform) [verified: T7]: presents vs original on lavapipe differ in
13-16% of pixels by under 3% and in at most 13 pixels a frame by more; lockstep IDENTICAL (1800
frames, renderer = native); validation layer loaded, 0 messages over 2200 frames (HT2_VK_VALIDATE
on the RTX 4070); Win Max (RADV) 2560x1440: 682 of 3.7M pixels differ by more than 3% (edges).
Not done: GPU-resident meshes (the draws are small: ~26 vertices on average, so the per-draw
vertex copy is a few KB a frame; the per-draw state snapshot, ~2.4 KB a draw, is the bigger copy).

Where the CPU goes now (2026-10-29, Win Max, renderer = native, render thread on, profile with
other sessions running so shares only): game code 71%, libc 6.3% (copies ~3.3%, the wake's
mutex/cond ~1.7%), vkthread.c 5.1%, fpu.c 4.9% (fsin/fcos 4.2%), vk.c 2.9%, Vulkan driver 2.6%,
d3d.c 2.5%. The state snapshot now goes over only when it changed (HT2_STATE_ALL=1 the old way):
presents identical (11 of 11, lavapipe), game thread 0.98 -> 0.81 ms a frame, process CPU -0.7%
[verified: T7]. So the layers between the game and Vulkan are ~12% of the CPU in all; the rest is
the game's own translated code (x87-heavy: the next big lever is the translator's x87 and flags
code, or hand-written native versions of the hottest game functions 0x5b4670, 0x5db5c0,
0x5af090, 0x5f1190, 0x5f1770...). GPU-resident meshes would save little (small draws).

Queue and spin (2026-10-29, both renderers, it is the default path's render thread): the sleeping
render thread is woken per 32 commands, at present and by every wait for it (vk_sync, the
frame-ahead wait - leaving that one out was the 2026-10-28 dead end's starvation), its spin 100
pauses. T7, paced 60, 2560x1440: process CPU 25.9 -> 22.1 s (thread off: 21.6 s), game thread
2.7 -> 0.9 ms a frame, 10 frame-ahead waits in 3300 frames, frames 16.66 ms; thread on vs off
presents identical on lavapipe (11 of 11); Win Max (RADV) the same picture: 16.65 ms frames,
game thread 0.68 ms, 9 frame-ahead waits [verified: tools/dev/perf_ab.sh, compare_render.sh].

### 12. Native code: the game rewritten function by function (Ant, 2026-10-29: "the whole thing native")
Goal: the translated game code (king.exe's 12271 functions as generated C) replaced by
hand-written native C, working on the same memory and structures, until nothing translated is
left on the paths that run. Every function not yet rewritten stays translated, so the game plays
throughout; HT2_NATIVE=0 turns every native function off (A/B and bisecting).
Order: by CPU cost (HT2_PROFILE; the 40 hottest are 46% of all CPU on 2026-10-29: 0x5b4670,
0x5db5c0, 0x5af090, 0x5f1190, 0x5f1770, 0x5aad50, 0x5f1850, 0x5aabc0, 0x5ab130, 0x5f7f40, ...),
then by subsystem (the engine's renderer feeding Vulkan directly, physics, AI, the menus), so
whole call trees go native and the calls between them become plain C calls.
Rules for a native function:
- Same results bit for bit: x87 arithmetic in f80 (the host's 80-bit long double under the game's
  control word, as the translation does), floats rounded where the original stores them; the
  same memory written; eax (and edx, st(0)) returned as the original returns them; the stack
  popped by the callee's convention. Scratch registers (ecx, edx unless returned) and the dead
  stack below esp need not match.
- Registered as a hook (builds/ht2_gog13.py HOOKS, name n_<what>) in runtime/native_*.c; the
  translated original stays as the fallback.
- Proven before it counts: fncheck --only ADDR with many seeds (original in Unicorn vs native;
  scratch registers and the dead stack left out for native functions), lockstep on the race run
  (memory hashes leave out each thread's stack below its esp, which no code can read), the
  renderer A/B harness (presents identical: native code changes no pixel), and the profile.
Verification infrastructure first: (1) memory hashes without the dead stack; (2) fncheck's
native mode; (3) runtime/native.h (arguments, returns, x87 pushes); then the functions.
Done 2026-10-29: mem_hash leaves out [stack_lo, esp) of every thread (cpu_dead_stacks); fncheck
--native; runtime/native.h (NARG, nret, nret_st0, ncall/ncall_this/ncall_virt into game code);
tools/dev/native_check.sh ADDR... (fncheck --native per function, the lockstep race run, presents
with HT2_NATIVE=0 vs on). Batch 1 (native_math.c: vec_div 0x5aad50, vec_len 0x5aabc0, vec_set
0x41ce10, vec_dot 0x4e1080, list_at 0x4b9bb0): fncheck 0 FAIL each (200 seeds; vec_len first failed
on eax, which the original leaves = its argument: scratch registers that hold a value on purpose
must match), lockstep IDENTICAL, presents identical [verified: T7]. Batch 2 (native_lists.c: the
object table walkers 0x5f1190, 0x5f16f0, 0x5f1770) and batch 3 (vec_zero 0x4042a0, plane_set
0x5b4d90, mat_vec 0x4cb300 - it stores a float into its own argument slot, live memory - and
mat_transpose 0x5abd60): fncheck 0 FAIL each except 0x5f1190 seed 63 of 64, where the random
callback argument is a heap address: Unicorn runs the data as code, the runtime refuses ("not
code"; the translation would too) - a random-input artefact, not a difference; lockstep IDENTICAL
and presents identical with batches 1-2 [verified: T7, local fncheck]; batch 3 on the T7: fncheck 0
FAIL each, lockstep IDENTICAL, presents identical (11 of 11).
Finding: the small helpers gain little alone (the translation is already tight); the gain is in
whole functions whose temporaries live on the stack, e.g. 0x5b4670 (the view-frustum test of a
bounding sphere, 517 instructions, 4.2% of the CPU): native, its 40-odd stack floats become host
variables and its helper calls direct. Hand-transcribing such functions means tracking every esp
offset through pushes - what the translator already does mechanically. So two tracks: (a) the
translator emits native-quality C for every function: stack slots whose address does not escape
promoted to host variables, calls to native helpers made direct, flags computed only where read
(fncheck and lockstep prove each step for all 12271 functions at once); (b) hand-written native
code for subsystems where rewriting adds something (the engine's renderer feeding Vulkan, then
physics, AI), function by function as above.

### 13. Release: the download (Ant, 2026-10-29: "start on packaging"; native rewrite parked)
Ant judged the native rewrite (section 12) overkill: the game uses under half a core; track 1
stops with the fnstsw change. Next is a release, as TD6's and 1NSANE's are:
- The download holds no part of the game. Like td6-native (tools/release/linux.sh, launcher/,
  tools/install.py, the closest relative: same runtime family, FFmpeg) with 1NSANE's innoextract:
  an AppImage whose launcher, the first time, takes the player's GOG installer
  (setup_hard_truck_2_king_of_the_road_1.3_amd_rdna_fix_(52932).exe, unpacked by innoextract
  without running it) or an installed GOG folder, checks the files against the release's hashes
  (builds/ht2_gog13.manifest.sha256), copies them to ~/.local/share/hardtruck2-native/game,
  translates king.exe (bundled Python 3.14 + Capstone, tools/recomp.py), compiles it with the
  bundled Zig against the prebuilt runtime (lib/libruntime.a, SDL3, FFmpeg) and starts it; later
  runs start the game at once until a new download (another VERSION).
- Built with Zig 0.17.0 for glibc 2.31 (runs on Linux from 2020 on); third_party from td6-native
  and 1nsane-native (zig, sdl3-rel, python-standalone, pack-venv, appimagetool, innoextract) and an
  FFmpeg 8.0.1 of our own: decoders indeo5, mp3float, mp3; demuxer mp3; parser mpegaudio; file
  protocol; swscale, swresample (the 12 MP3 tracks, the two Indeo 5 movies).
- Docs: README.md (getting it, what works, what does not: no network play), CREDITS.md and the
  licences the download carries (SDL3, FFmpeg LGPL with its source, Zig/LLVM, Capstone, Python,
  innoextract), BUILDING.md. A public mirror like 1NSANE's needs Ant's yes in this chat.
- Checks before it ships: a clean user folder set up from the GOG installer by the AppImage alone
  (--setup-from, no network), the --test runs and lockstep against that build, then Ant's
  playthrough on the release build.
A Windows build: Ant 2026-10-30 "let's make a windows version"; the name "Hard Truck 2: Recompiled" stays.

Results (2026-10-30) [verified]:
- tools/release/linux.sh builds Hard_Truck_2_Recompiled-0.1.0-alpha-x86_64.AppImage, 124 MB
  (Zig 410 MB and Python 53 MB unpacked are most of it). Run it in a git worktree
  (work/relwt, third_party/* symlinked from the main checkout, PY= the .venv python).
- A clean user folder set up by the AppImage alone (--setup-from the GOG installer,
  --no-launch, HT2_JOBS=6): 341 files checked, translated, compiled and linked in 2m32s. Its
  game folder is byte-identical to the usual one (diff -r).
- The race-start --test run (1800 frames, the translator check's keys) hashes identically every
  frame for: the gcc build linked with the download's FFmpeg (-frounding-math), the release
  build (Zig/clang, no -frounding-math) and the clean-setup build. So -frounding-math is dropped
  from the players' compile, as in TD6 (the game never changes the rounding mode: no fesetround
  in the runtime or translator). The gcc build passes lockstep, so the release build does too by
  equal hashes.
- Two differences that are not bugs: the system FFmpeg (7.x, with asm) and the download's
  (8.0.1, --disable-x86asm) decode the intro movie a shade apart (ICDecompress at 0x5c1193,
  frame 66), and the game folder's path is in the game's memory, so --hash runs only compare
  with the same FFmpeg and the same --game path.
- The launcher's first page, off-screen (SDL_VIDEO_DRIVER=offscreen, HT2_LAUNCHER_SHOT), finds
  the installer in ~/Downloads/Games by itself.
- The icon (assets/icon.png) is drawn lettering like TD6's: the game's own icon is GOG's file
  and can't ship.

The fnstsw change's translator check (T7, 2026-10-30) [verified]: lockstep identical on all 1800
frames, also with HT2_SWCC_POISON=1. Plain fncheck listed 73 FAIL against the baseline's 28, because
the runtime turns the native functions on at start-up even without --native: they and their
callers differ in ecx/edx and below esp, as section 12 allows. With HT2_NATIVE=0: 12040 pass, 28
FAIL, exactly the baseline's functions. tools/dev/translator_check.sh now runs it that way.

Windows zip (2026-10-30) [verified under Wine (Proton 10) only]: tools/release/windows.sh after
linux.sh in the same checkout, 148 MB, cross-built with the Linux Zig; third_party/win is links to
td6-native's toolchain plus our own FFmpeg (same decoders). runtime/win came with the fork; added
localtime_r/timegm stand-ins, tz offset without tm_gmtoff, HOSTFN exported so dlsym(RTLD_DEFAULT) ->
GetProcAddress finds the API stand-ins, the test watchdog Linux-only. All 42 files compile with
-Wpointer-to-int-cast -Wint-to-pointer-cast -Wshift-count-overflow clean (long is 32 bits there).
- Under Wine with tools/dev/wine_lowfree.c: the race-start --test run hashes identically to the
  Linux release build on all 1800 frames (same relative --game and --user on both, since paths
  are in memory), and a frame dump (HT2_GPU_OFFSCREEN) draws correctly on the Radeon via Vulkan.
- The launcher's --setup-from under Wine: unpacks with innoextract.exe, checks the files,
  translates; the C matches Linux's exactly (diff of gen/). Compiling stops: Zig's cache locking
  (NtLockFile) fails under Wine, as in TD6. So the compile step, the launcher's window and the
  real window are UNCHECKED until run on real Windows.
- Both .exe were console programs (zig cc ignores -mwindows: subsystem 3, a console window beside
  the game; TD6's and 1NSANE's scripts have the same flag [guess: same result there, not checked]). Now --subsystem,windows
  -e mainCRTStartup (subsystem 2 verified); the launcher's tools share a windowless console from a
  helper (windowless_console; SDL's background processes lose exit codes). That helper is [guess]
  until run on real Windows.

## Handoff (2026-10-26)
State: playable headless and in a window: menus, loading, driving, keyboard and controller.
Installed with make install (applications menu "Hard Truck 2: King of the Road",
~/.local/opt/hardtruck2-native). Private GitHub repository AntAir267/hardtruck2-native (branch master), made 2026-10-26 at
Ant's request.
Music: dshow.c (DirectShow filter graph: RenderFile decodes the MP3 whole with FFmpeg, played as
a runtime voice; Run/Stop/GetState, EC_COMPLETE for the next track, IBasicAudio volume); the
game plays music in the world only. Movies: getaviinfo.c (IGetAVIInfo natively, after
work/decomp/avi/SPEC.md) + vfw.c (Indeo 5 by FFmpeg behind ICOpen/ICSendMessage/ICDecompress,
24-bit output as the game asks); the intro movie plays with its sound; Start (Escape) skips it.
The BSTR helper 0x5c1090 relies on MultiByteToWideChar writing what fits before failing.
Missing: a test with a real controller; the instrument bar stays 4:3 (black at its sides in widescreen).
Tests: build/ht2 --test --user work/testuser (fresh folder: TRUCK.INI and kmjin.ini are made);
HT2_PAD="frame:BUTTON*hold,frame:AXIS=value*hold" (no PLUG: connected from start-up), HT2_KEYS,
HT2_GPU_OFFSCREEN=1 HT2_DUMP_FRAMES=<dir> HT2_DUMP_EVERY=<n> [HT2_DUMP_FROM=<frame>]. Main menu:
A A starts a new game (driving from about frame 1400 at the virtual 60 fps). Ghidra snapshots in
work/rea (king.exe, GetAVIInfo.dll). Lockstep: tools/lockstep.py --seed <folder with TRUCK.INI>.

## Progress

- 2026-10-26: project made, installer unpacked, translator and runtime forked from td6-native.
  Headless --test runs go: MFC start-up, TRUCK.INI and kmjin.ini, LOAD.RES, DirectPlay, the
  player profile, DirectDraw/Direct3D 7 at 640x480x16, menu fonts, the loading screen's overlay.
- 2026-10-26 (night): native resolution and Hor+ widescreen by default.
- 2026-10-27: fog checked: the game uses vertex fog (linear, start ~13.4, end 60 eye units, colour ffffe6; not from TRUCK.INI [RENDER] fog=, which changed nothing) and the port draws it (fog distances scaled by 0.1 for a test fogged the scene white) [verified: runs 107-108]. So remaining pop-in in the distance is the game's own draw distance ending before its fog is full; TD6's answer (vk_fog_end: fog full at the draw distance, drawdist.c) is not in this port yet [roadmap: draw distance / fog-at-edge option]. Off-screen real-time runs get no input focus: DirectInput driving keys/pad do nothing there (menus work); drive in --test. Ant's profile has [EFFECTS] control=0 (keyboard only): made before a pad was connected, so the pad does not drive until chosen on CONTROLLERS (padgame.c only sets it for new profiles). T7 set up for this project 2026-10-27 (venv, SDL3, game files).
- 2026-10-27: widescreen with the render thread showed the 4:3 view in the middle and other frames at the sides (Ant's screenshot, flicker): vk.c read ws_world_aspect live, but the game sets two widened cameras per frame (world, cockpit), so a frame behind the world's draws failed proj_widened. Now carried in CtxSnap (vkr.ws_world_aspect). HT2_RENDER_THREAD=1 with --test reproduces such bugs; thread on/off presents pixel-identical after the fix. Rule: anything vk.c reads from the game side goes through the snapshot.
- 2026-10-27: Ant's first real full-screen run: the host window kept the first frame window's 100x200 (hidden/shown on Wayland lost the full screen; now handed over before hiding), the mouse in window points not game pixels (now mapped through the picture's place), side culling (pyramid hooks). HT2_DUMP_PRESENT works in a window (swapchain read back).
- 2026-10-26 (night, later): MORE OPTIONS page in the GRAPHICS screen (menu.c); RB = Tab in menus; Escape/B leaves a sub-screen. Lockstep identical on an 1800-frame race run (reference build runs hooks; --test threads take turns).
- 2026-10-26 (evening): music (DirectShow) and movies (IGetAVIInfo + Indeo 5).
- 2026-10-26 (later): menus on Vulkan (8-bit palettes); the keyboard reaches the game (the
  window layer lost its main window when the first frame window was destroyed); loading screen
  and driving; the test clock fix; controllers; make install.
