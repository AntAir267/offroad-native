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
Verified at 1920x1080 on the 890M: race, HUD, pause menu, menus and movies. Split screen checked with two pads (7b).
0x413830 reads the camera's width / 640 for its size (a world effect [guess: sprites]): may be 1/3 wider.
GAME OPTIONS > VIDEO has a Widescreen row (menu.c, see its head comment): a real row made by the game's own
functions, so it has the arrows, sounds, help line and controller support. Brightness moved up 14 to fit
(0x5b95c8). It applies at once and saves to options.txt [verified: pad run, off to on, into a race; a
restart reads it]. Text is English only. Not installed in test runs without ORR_WIDESCREEN, so lockstep sees the
original screen. A render target changing shape is cleared, not stretched (rt_sync): the old picture
otherwise stays in the side space, where nothing draws.

### 7. Controller and prompts (2026-10-30)
padgame.c, see its head comment. The joystick is laid out as Windows does an Xbox pad (X, Y left stick, Z
triggers). First start writes the user folder's Configs\p1controll.cfg: controller active; RT
accelerate, LT brake, A handbrake, X rear view, Y camera, RB/LB gears, B nitro, force feedback on. This
replaces Ant's old RB/LB on accelerate and brake. Flag bytes: the second is Force Feedback [verified: screen].
Front end: A select, B back, left stick and D-pad move, Start is Escape. Race: Start opens and closes
the menu; B is nitro there, not back. Movies: A, B or Start skip.
Prompts and icons: while the controller is the device used last, text_hook (every line of text, 0x4b5540)
puts real button icons in: the bindings on the controller screen, and Continue, Done, Accept, Race, Back. They are
characters of the game's own fonts (Impact_italic_med and _small, all languages), in <font>_512.tga images the
first start writes to the user folder from Main.mng: padgame.c's head comment has how. The art is
drawn for this project (tools/make_pad_icons.py, runtime/pad_icons.h), as lr2-native's: no real Xbox art is
in the game. A screen keeps what it opened with, and only the next screen follows a change of device.
"Waiting For Input - Escape To Cancel" stays: Escape is what cancels there.
Verified with the scripted pad, off-screen. A real pad and rumble are not tested.

### 7b. Every controller works (2026-10-31, Ant's rule, GAME-GOALS.md)
The fork from hardtruck2-native had one joystick per controller, and the game binds player 1 to one and lists
them once at start-up: only the first drove, and one plugged in later not at all. pad.c now has two joysticks
that are always there, fed by the real controllers merged (as etm-native; the Win Max shows three devices for
one controller). Joystick 0 is player 1's, joystick 1 player 2's: player 2's menus read only it, so in menus both
get every controller. In a two-player race (0x460970, game+0x4ec == 2) the second connected is joystick 1's
alone, until the front end loads again (0x43a5a0). The race's start re-binds the menus' controls
(0x4d72b0 > 0x461f00), so those binds cannot mark leaving a race [verified: log]. race_controls_hook also binds
the keyboard's keys, so it drives with the controller active.
Verified, scripted: plugged in after start at frame 2900; second of two; keyboard alone; two-player split screen
in widescreen, each pad its own car, through a pause. On a handheld with several devices the "second connected"
in a two-player race may be the same physical controller [guess: not tried].

### 7c. Upgrades (2026-10-31, Ant chose all four: sharper picture, further, lighting, ray tracing and dust)
GAME OPTIONS > VIDEO > More Options (menu.c): widescreen, anti-aliasing, texture filtering, colour precision, far draw
distance, ambient occlusion. Ids 0x159, 0x15e, 0x15f, 0x161..0x168 are free [verified: no immediate in the game's
code]; 0x183..0x18e are another screen's, and a scan of only its table calls missed that (an earlier build's row at
0x18f, and a page at 0x186.. overwrote them for a day; never shipped to Ant as more than the row).
Defaults on: filtering, colour precision, AO, draw distance Far. Anti-aliasing stays off: GPU per frame at 2560x1600
on the 890M, other builds running, so rough: base 2.4 ms, AO 3.8, filtering 2.6, colour 2.8, 4x 8.0, Extreme far
2.2 with the game's thread 5.2 against 2.3, all together 14-18.
Draw distance: game.c raises the far plane (0x5d0e78, x1.5 Far, x2.5 Extreme) where the cameras read it. Draws go from
89 to about 118 a frame; speed and lap time equal at the frames compared. The one view compared was close up, so
what it looks like far away has not been judged.
AO: shaders/ssao.frag, vk.c ssao_passes, from the depth buffer at the first flat draw after the world. z = a + b / w
fitted from each depth-writing draw's two ends [verified: a 0.982, b -19.6 logged; z rises to a with distance]. The
inherited ray-traced AO needs geometry this game does not hand over.
T7, 2026-10-31: fncheck and lockstep unchanged (their runs leave the menu page and draw distance out); AO, 4x and Extreme on the RTX 4070, 3900 frames: no validation messages.
Volumetric dust (game.c puff_draw_hook, on by default, row in More Options): the puff classes share a draw, 0x53c260:
world position +0x2c, half size +0x3c, +0x40, colour +0x48 [verified: disassembly]. The tailpipes' are 0x57abcc (0x491070,
the car's Exhaust objects, grey 0xff4f4f4f, fade by darkening); the wheels' 0x57ad34 (0x4a42e0, a surface's colour, fade by
alpha); 0x57acf4 (0x4bc5e0) stays a sprite. A first go took 0x57abcc for wheel dust and painted the exhaust tan (Ant:
"egregious"). The hook sends them to the inherited volumetric renderer in view space and skips the sprite. The camera's
0x4f0cb0 matrix is view and projection together (column 3 the depth). Made up for it: a D3D state with identity world and view
and a projection from the camera. Tailpipe: light grey 0x9a9a9a at 22 % of the alpha. Sizes are the game's. Density 3
(vk.c sm_density). Puff grow must be 1: the renderer's size history is by serial, and the game reuses objects' addresses, so 0
showed a new puff at the size of the one before, a full-screen haze for one frame: Ant's "smoke flickering" [verified: frames
4008-4010, 40 such flashes in 900 frames, 0 after]. Puffs at the camera fade. Sun direction is a fixed guess, not the track's.
Sun flicker: flat draws at any depth counted as "the HUD has started", so a flare could start AO and the smoke's end
early, and could be pinned to a screen edge as it crossed a third. HUD now means rhw 1 and z under 0.02. Not looked at directly:
no test scene had the sun in view.
Stencil (vk.c): never implemented before, though the game uses it: it writes far depth through a stencil mask (a full-screen
ZERO, ONE draw, depth ALWAYS, stencil EQUAL 1), then adds the sky's reflection. Without it the whole depth buffer was wiped and
the sky landed on everything once water was in range: Ant's "sky takes depth priority in water" [verified: frame 4800]. The depth format
now has stencil; sampled depth uses a depth-only view; stencil is dynamic state.
Far draw distance: everything is made from the raised far plane now (widescreen.c, game_far), depth mapping, culling and fog. A first go kept the mapping at the game's number and raised only the culling. That was wrong: the game clips every polygon at z 1 (renderer +0x400, 0x53fc20), so nothing past 260 m ever drew, and moving the fog alone made that look like a short draw distance (Ant). The clouds over the banks that made me keep the mapping were the missing stencil. Sky checked at Extreme on Campy Crystal Lake. The helicopter was not in view at Extreme in one Everglade frame though it was at Far: not looked into.
Fog (Ant: "the further elements are still fogged"): the fade is the game's own, on the CPU, into each vertex: fog = ((1/rhw - cam+0x270) * cam+0x274 - d) * slope + height mist, with +0x274 = 1 / (far - near) [verified: 0x4f1350, readers 0x53fe80, 0x545500, 0x527e60; FogDensity d from the .key, 0x507020]. Raising only what culled left all the extra distance in full fog. widescreen.c now makes +0x274 from the raised far. Original, Far and Extreme compared at one frame: the far hillside goes from grey shapes to full colour.

Frames and ticks (Ant: smoke and sun flicker, seen only in real play). The game steps at 60 ticks a second however fast it draws (0x4db240, timer 0x5d2758; +0x30 the whole ticks in a frame). Ant's real runs draw 157 frames a second (last-run.log). Real-time dumps at 150: the world changes every 2.5 frames, the car and puffs one frame after it, then nothing, so they shake against the world at 60 Hz [verified: differences of consecutive dumps]. Every earlier check was a --test run, one tick a frame, which cannot show this: check flicker with a real-time run (no --test, ORR_PACE). game.c frame_timer_hook now holds a frame until a tick is due; test runs are untouched; ORR_TICK_FRAMES=0 for the old way. It must not use the timer's own wait (second argument): that spins on Sleep(0) and counts from when it last finished, and any beat of ours on top spun longer every frame, game CPU 5 to 14 ms over a lap [verified: ORR_PROFILE, 54 % sched_yield]. Now 5 ms flat. Sun: the mechanism is not looked at with the sun in view; needs Ant's word.

Smoke flicker, second cause (Ant, after the tick fix: sun fixed, smoke still bad). A wheel's puff is born whole and removed while it still has strength (0x419070: +0x70 under +0x78). Faint as a sprite; as a volume at speed, the car leaves each behind in a few frames and lumps came and went about 15 times a second [verified: ORR_SMOKE_DEBUG=1 real-time dumps, consecutive frames]. game.c now grows it into its strength over 10 ticks (age from size) and fades it out before removal, tailpipe puffs too. ORR_SMOKE_DEBUG=1 draws them far more solid than they are.

Water slowdown (Ant): not reproduced on this machine's 890M at 2560x1440, real time, off-screen with the render thread: through a lap with puddles, no frame over 22 ms, and those are not at the puddles. Ant's runs had the GPU flat out at 157 frames a second, which frames and ticks above takes to 60 [guess: that is the cause]. Puffs: up to 287 taken, nearly all tailpipes and far-off AI cars (dist 24000!): they could be culled if the volume cost shows. White spray (0x4a42e0, +0x474 == 1, colour 0xe6e6f0) stays the game's sprite. The puddle splash itself is another class (0x4a1a40). Needs Ant's retest.

Audio (Ant: music far louder than the car). Not a stack problem: one bug. The game writes volume * master / 127 into every 3D sample [verified: disassembly 0x423d58, 0x424000], 2D samples and streams rely on the driver's master, and mss32.c applied master to all, so effects got master twice (0.55 x 0.55; at the default prefs slider 10 x 7 = 70/127). Mix RMS at full throttle: effects 2200 before, 4000 after; music 5100. Tracks trimmed 3 dB in the stand-in on top. The game's own sliders still work. Not heard by me.

Ray-traced lighting: see 7d.

Crash at Extreme (Ant): the terrain's polygon records live in the object, 16 bytes each, 16384 of them, unchecked; past that they overwrite its other fields (0x540260 on a bad pointer, last-run.log). game.c terrain_begin_hook starts the cursor in a 16 MB block of our own. Measured at Extreme, day, track 1 (ORR_POOL_LOG, sampled): up to 23123 polygons on Dead Man's Drop, 18464 on Campy, 3-6000 vertices against a limit of 32767.

Dirt, after Ant: "the tire dirt you had previously was better": ramp 10 ticks to 4, fade-out shorter (game.c). Not compared side by side; his eyes.

Puddle lag (found from Ant's frame-trace.txt and last-run.log): render wall 25 ms with render CPU 2 ms, so the GPU. Then reproduced with ORR_VK_STATS=2, GPU time a frame at 2560x1440: 5 ms to 24 in the spray. Earlier checks looked at frame wall time only, off-screen, and missed it. Coverage by triangle area: see-through 2.5 screens normally, 25.8 at the peak, three batches of the game's spray sheets. Baked lighting per pixel on these took about 9 ms of it. Now: see-through draws are lit from their corners (draw.vert, light.x 6), and a batch over 5 screens draws every Nth sheet (fans sharing a first vertex; vk.c). Peak 13.6 ms. The spray is a little thinner. Night not explained: Ant saw none there; the one night run with hits had no slow frames. Real play still records itself: frame-trace.txt, "slow frame" lines in last-run.log.

Puddle lag, second round (Ant, on Extreme at night; frame-trace.txt): game CPU 15-19 ms there. Measured at 1440p through a puddle, game CPU mean, max: Far 4.7, 9.4, no frame over 16.7; Extreme at 2.5 times 11.9, 22.2, 133 of 1929 over; Extreme at 2 times 7.7, 15.4, none over. So Extreme is now 2 times the game's distance, and only one splash in three is made (game.c puddle_hit_hook, ret 0x18: six arguments). Spray thinning only on the 64 x 64 spray texture: it had also caught the terrain's blended passes, the checkered floor (Ant).

Car shadows: draw.frag car_box_hit, a soft ray-box test per car, from the sun and from other cars' headlights. Boxes, one size [guess]. A car's own body takes neither its lights nor its shadow, which also fixes the taillight glow landing on the wrong part of the car (Ant). Lamp positions are still one fixed place per car.

Menus: VIDEO > More Options (8 rows, and Lighting's button), its Lighting page (6 rows), AUDIO > More Options (3). 0x45ce10 names the button Escape and B press: only Backs now; More's being named made them loop (Ant).

Efficiency round (Ant: "optimization is the goal"). Frame at Extreme, 1440p, game CPU, ORR_TIME_LOG: was 7.6 ms mean, clouds 1.7, walk 2.1, land 1.5. Clouds drawn for the game's far plane (0x4ea330 fans to 3 x far, split to 3200 units, cost goes with far squared): about 0. -O3 on the generated code: 5.6 to 4.3 ms. Spray: splash_draw_hook draws one of a splash's 4 parts, chosen for its life, flecks 4 times as strong (vk.c, draw.frag bit 11); the old one-in-three and thinning are out, so the game's random numbers are back to its own. GPU in a puddle: 24 ms, then 13.6, now 8.4 at most.

Frames between ticks (game.c, More Options Frame Rate 60, 120, 144, Screen; default Screen): draw-only frames before each normal frame: 0x4d6be0 again with the update hook doing nothing and the times zero, then the flip. Snapshots at the update's end (camera rows and position, each car, wheels through [car+0x508]); blended in view_draw_hook, put back after. Puffs back-stepped by velocity (wheels' only). Up to one tick more input lag. Dumps at real time make frames slow enough that alpha saturates, so check motion with ORR_INT_ALPHA=0.5. See work/spray-interp-spec.md B for what is not blended (spray, helicopter, part arms, HUD, weather: step at 60). Not in two-player, menus, other drivers.

Water simulation (Lighting > Water, Simulated by default, Original the game's; Lighting > Water Area, Large 128 m, Small 64 m; runtime/water.c, shaders water*, vk.c vkr_water_*; replaced the ripple grid and the spray sheets, Ant's "looks kind of bad, no interesting reflections"). Needs the bake's height field: with baked lighting off, Original. What water the game has [verified: .lev flags, .ob3 lists, frames]: puddles (0x4a1710; 2-5 Puddle_1 a track, plus Puddle_RainOnly in rain, none in snow; Campy has none), each 15-30 m on a dead flat cut-out of the terrain at +0x74, so no basin; the lake at height 0 (cells flagged 2: Campy 17179, Dead Man's 1584), which the game never draws as water (0x4d9340 clears view +0x38 bit 1; it shows as flat blue), set now (native_land.c) so the game's waves show past the grid. NO RIVERS anywhere: Everglade's "rivertrack" and Red Rock have none (river_dam is concrete, canyon_water.pcx a wooden tank). So Ant's river prototype was done on the lake. Method, written from the general shallow-water equations: staggered grid, a cell 25 units, one step a tick, numbered from the world modulo the image, so moving the grid copies nothing. A RGBA32F depth, ground, rest level, wet+foam; V RG32F flow on the +x, +z sides. Pass 0: flow from the surface's slope, upstream carrying of the flow, friction, wet/dry sides, clamped to a quarter of a cell a step, so no cell can give more than it has. Pass 1: depth from what crosses each side, the same number on both sides, so volume is kept: 14.4 m3 unchanged read back with 12 ghost cars at 40 m/s and the sources off (ORR_WATER_KEEP). Flux depth capped at 2 m for stability in the deep lake. Sources apart from that: recovery, a seep of 5% of what is missing a second plus 0.6 cm/s (an emptied 8 cm puddle half back in 4.5 s, full in about 10); a film away from its place drains 0.7 cm/s; ground stays wet 50 s. Puddles get an invisible lip of 3 cm round a rough oval, so they hold until a wave tops it. The lake's area edge relaxes to a third of the game's waves, which come in and run up the shore. Cars: every car drawn (a car not drawn pushes nothing); wheels from the car's matrix (the wheel objects' positions are the car's own [verified: log]), a pressure bump along the wheel's path this tick and a push out and along, velocity capped by the limit above (3.75 m/s). The grid sits ahead of the view, moved in steps of 16 cells, follows the first view in two-player. It only runs in the box round the puddles in it, or all of it when the lake is in reach; none there, no cost. Puddles not wholly inside stay the game's decal. Fixed step on game ticks, drawn between the last two states, at most 3 steps a frame: any frame rate is stable (same volumes at every read in real time and --test). Surface: mesh every 2 cells, no vertex buffer, after the world (time_world_hook, so cars are in the depth); reflections marched through a copy of the picture and a half-size distance image, falling back to where the ray goes in the picture's sky; Schlick brightening, refraction, depth tint, baked sun visibility and sky, lamps, headlights. Spray: 3000 small quads thrown from around the wheels, launched by the flow, only from foam; replaces splash draw (the objects are still created: shared RNG). Mismatch: the car's drag, sound and splash still come from the game's own rectangles, wherever the water has gone. Numbers, 890M, 2560x1440, Extreme, puddle in view, 6 ghost cars: GPU frame 6.25 ms mean, 6.96 max, Original 6.32, 7.45 (the game's decal cost as much); simulation 0.03 ms a step (208 x 132 cells), surface 0.15, copies 0.43 (first water view a frame). Worst case, the lake filling the grid, 512 x 512: simulation 0.24 ms, surface 1.83, frame 4.17 mean, 5.67 max. ORR_WATER_LOG=1 timings, state read back, positions; ORR_WATER_TEST=cars,puddle,m/s[,x,z] ghost cars and a fixed camera (puddle -1: at x, z); ORR_WATER_DEBUG; race.sh STEER="880:LEFT*30" (Slaughter track 1 at 878:LEFT*30 is through a real puddle).

Fog (Lighting > Fog, Off by default, Original): game.c fog_*. The land's overlay skipped and the fog object's start set to 4 x far from the land on, mist bit off, so nothing has any; the sky keeps the real values (without them its horizon is a black band: first try nulled the object). Time hooks for world and fog are always installed now (they were only under ORR_TIME_LOG, which hid this).

Anti-aliasing (Ant 2026-11-04: "TAA, FXAA, DLSS, and everything else"). Video > More Options > Anti-Aliasing: Off, FXAA, TAA, FSR 2,
DLSS (only where it can run: the row lists what this machine has), 2x, 4x; Render Scale: Native, Quality, Balanced, Performance, Auto
(TAA, FSR 2, DLSS). The renderer's scene (vk.c, from 1NSANE's lineage) had never run in this port: nothing called vk_scene_begin, so
Colour Precision High did nothing either until now. runtime/taa.c is the game's side: the scene round 0x4daec0 for the race's main
view (one player), tied to the back buffer it began on (VkScene.rt: earlier draws elsewhere stay out). Off-Road's world is screen-space
vertices, so motion comes from the game's own matrix (0x4f0cb0) [verified: still camera reads zero, a moving one spreads from the
middle, the car about zero, ORR_TAA_DEBUG=1]: clip -> the game's screen (the water's mapping, S) -> world (G inverse) -> its move (a
car's matrix then and now, taa_car_hook; the sky with the camera, from the view's start to the land and the clouds) -> last scene's
G -> clip. The game clears its depth inside the view, into the scene's: the copy back clears the back buffer's depth first and covers
the whole target (without that the copy was depth-tested away and the back buffer's stale loading screen showed). FXAA: taa.frag
mode 4, written from the method, in the scene's copy back. 2x: drawn 1.41 x each way. FSR 2: AMD's 2.2.1 source, copied from 1NSANE's
third_party (MIT, not committed: third_party/), built when there. DLSS (2026-11-04, on the T7, RTX 4070, driver 595.99): NVIDIA's SDK v310.9.1 (codeload tarball, 717 MB; git clone ran at 60 KB/s)
in ~/sdk/DLSS-310.9.1 on the T7, built with make DLSS=$HOME/sdk/DLSS-310.9.1 (the static NGX library; libnvidia-ngx-dlss.so.310.9.1
copied into build/, next to the program, where dlss.c points NGX). Not inside the project there: the T7 sync deletes git-ignored
folders that do not also exist here (third_party/DLSS went; FidelityFX-FSR2, present here, stayed). Runs: Native is DLAA, Quality
1707x960, Performance 1280x720 into 2560x1440; motion view right. Validation: about 10 VUID-vkDestroyImage/Buffer "in use" each time
the feature is made: NGX destroys scratch objects it has just recorded, also in a command buffer of its own submitted and waited for
(tried, no change, taken out). Not on the Win Max (no SDK there, and AMD cannot run it); the row leaves it out where it cannot run.
T7, 2560x1440, Extreme, baked lighting and water on, GPU per frame mean: Off 2.62-2.68, TAA 2.82, FSR 2 3.07, DLSS (any scale) 3.0. The water's own passes
take no motion vectors (what is under them keeps theirs) and get the jitter.
DEAD END, 890M (RADV): TAA's history as R16G16B16A16_SFLOAT render targets hung the GPU ring (kernel reset) every run, even a shader
of one colour fetch; in the 16-bit scene's unorm format it runs. A copy back with no depth attachment hung it too. Check the kernel
log (journalctl -k, "ring gfx timeout") after graphics changes: a reset can recover without the run seeing an error.
Numbers, 890M, 2560x1440, Extreme, Slaughter day, GPU per frame mean: Off 4.97 (the scene with Colour Precision High), FXAA 4.75 (no
cost to measure), TAA 5.21, FSR 2 9.64, 2x 8.28, 4x 15.83; TAA Quality 3.71; FSR 2 Quality 6.64, Balanced 5.90, Performance 5.49.

CPU round 2026-11-04 (Ant: "cut CPU time"). ORR_PROFILE samples (perf needs root) of a real-time Challenge race, 4 cars, Everglade
sunset, Extreme, 1440p, Frame Rate 144, grouped by function with addr2line (work/prof.py, not committed): both threads' CPU per frame
10.6 ms -> 6.0-6.7 ms; the game thread's world 6.0 -> 3.3 ms, its ground 3.1 -> 0.6-0.8 ms. Other jobs ran (a backup at 130%), so
frame rates are rough: 105 -> 135 fps against the 144 cap. What it was:
- The screen-space vertices (the whole world): d3d.c unpacked each 40-byte game vertex into a 130-byte SwVertex of floats, the queue
  copied those, vk.c packed them again: 1/3 of all CPU. Now VkVtx (gfx.h, 40 bytes, the game's own layout): XYZRHW|DIFFUSE|SPECULAR|
  TEX2 copied with w = 1/rhw and the colours' red and blue swapped; other layouts convert SwVertex -> VkVtx; the software rasterizer
  keeps SwVertex. vk.c's placement loop for the usual draw without per-vertex branches, the same sums in the same order. [verified:
  frames from the installed build and the new one identical, every pixel, five frames at night]
- Regression of the water round: the lake bit (view +0x38 bit 1) was set on every track, and on cells flagged 8 (thousands on
  Everglade and Slaughter Yard) it makes the land draw a second water layer the game never shows: 2.3 ms a frame, and possibly that
  layer on screen. Now only on tracks with a lake (light_has_lake).
- Regression of the anti-aliasing round: the water drew into the back buffer while the world was in the scene's image, which then
  covered it: no simulated water was visible. Now into the scene's images. And vkr_clear passed no state to scene_target, which the
  back-buffer check would have read: fixed before it bit.
Left, each 0.2-0.5 ms: the walk (marking each cell's objects), 0x527e60 (objects' vertices to the screen, 815 bytes, x87 exact needed),
0x5609f0 (12.7 KB), 0x568d60, 0x52c8f0; libc's waits and copies. At Extreme 1440p the GPU is now the limit (about 7 ms a frame), so
more CPU work saves power, not frames.

Second-race crash (Ant on the Deck, fixed 2026-11-05): with in-between frames on, a new race's first update (from 0x4da670) ran
int_snapshot over the last race's cars, freed; the car list was only cleared when the baked lighting loaded, after that update.
level_load_hook (0x4d9c30, each race's setup) now clears it and the interpolation records. Reproduced and checked with race.sh,
REAL=1, --option frame_rate=144 and STEER="300:ESC,620:DOWN,700:DOWN,800:ENTER,960:UP,1060:ENTER,1400:ENTER,1800:DOWN,1860:DOWN,
2000:ENTER,2300:ENTER,2360:UP,2410:UP,2460:DOWN,2510:DOWN,2590:ENTER,2830:UP,2880:UP,2930:DOWN,2980:DOWN,3030:ENTER,4030:UP*900"
(pause, Exit, Yes, Main Menu, Time Trial again; the pause menu ignores keys during its fade, the quit question defaults to No).
Ant's other Deck reports (2026-11-05), played on the build copied there Nov 4 (md5 64d8da5c2f) before the current one arrived at
22:30: puddles vanishing fits the water drawn under the scene (fixed Nov 4 16:27). Sky in front of things and blocky wall textures:
not reproduced at the Deck's settings (1280x800, 4x, Extreme, darkest, colour high, simulated water) on Red Rock, Slaughter Yard,
Everglade; the lake bit on lakeless tracks makes no visible difference (frames identical); no screenshots. Waiting for a capture.

Restart crash (Ant, fixed 2026-11-02): Restart runs a race's first update from inside the old one (0x4d7690 > 0x4d7d00 > 0x4d7510 > 0x4d7690), the cars going meanwhile; int_snapshot read a car whose wheel list was 0. Only with in-between frames on (a screen over 60 Hz or Frame Rate over 60), so --test never sees it. A nested update now forgets the car list and interpolation records. Test: REAL=1 race.sh with STEER="300:ESC,420:DOWN,470:UP,540:ENTER,660:ENTER" and --option frame_rate=144. Puddles: a new batch of 0x4a1710 calls replaces the list and resets the grid.

Real-time numbers, Challenge with 4 cars, Everglade sunset, 1440p, Extreme, 890M, Frame Rate 144, other jobs on the machine (load 5): anti-aliasing off GPU 7.31 ms mean, 10.85 p99, 128 fps, game CPU world 5.28; anti-aliasing 4x (5120 x 2880) GPU 30.32 mean, 41.82 p99, 32 fps: Ant's whole race ran at this, about 30 fps (his log's frame numbers against time; the slow-frame log stops at 400 lines).

Blocky ground (Ant): the terrain's blended passes were lit by their 5 m corners next to pixels lit per pixel (the spray-saving mode 6): now only small (64 x 64 or less) textures.

All of it together, Extreme, night, Darkest off, 1440p on the 890M, Everglade through a puddle, one car: game CPU 4.4 ms mean, 9.5 max, none over 16.7 (was 7.6 and 22); GPU 5.5 mean, 8.9 max (was 24 at the peak).

Right stick camera (More Options; game.c view_draw_hook): the stick's place is the angle, eased: right looks right, full over looks back, up rises to about 43 degrees, down dips 7. The camera is turned for the drawing and put back after the view, so the game's chase never sees it. A camera within 3 m of the car turns on the spot. Off in a two-player race (both would turn); per player later. Sound still follows the game's camera.

Particle flicker where cars' clouds overlap (Ant found it): overlap passed the march's budget, so every puff's steps changed frame to frame and the farthest were dropped. Budget 10 to 40, coarsening eases in, dropping only at 3 times over, ties in the sort broken by serial (vk.c). Not seen by me with cars overlapping: needs Ant's word.

Countdown: laid out right of the middle by the game (centres 525 and 558 of 1024); centred in vk.c by its size and place. [verified: frames, 3, 2, 1, GO]

Car lights (More Options, Car Lights; light.c, draw.frag): per pixel, after the bake's ratio and not under night's darkening. Cars are collected as they draw (0x497d50, ECX the car) and their matrices read at each view's start (object +0x24: three rows of four, then position; forward is the third row [verified: beams point the right way]). One beam from the front with a cone and fall-off, a red glow behind; fixed offsets, not each car's lamp points from mods.dat. Strength 1 at night, 0.3 at sunset and in rain, off by day. Blended draws do not take them. About 0.3 ms at 1440p.

Track lights (More Options): glowing faces of the lamp_onoff*_add materials clustered into lamps (export.py), bake --lamps: a reach of 40 m, shadows by rays, into <slug>.oll: ground 8 texels a cell, grid as the bake's. Added like car lights: night 1, sunset 0.35. Which lamps the game switches off by time of day (0x4d9a50: light_beam, light_sset) is not followed.

Headlight shadows (More Options, Car Light Shadows): real ray queries where the device has them: draw.frag built with LIGHT_RAYS (Makefile rule) is the one used if rt_ok; the rays are skipped by a block value otherwise. <slug>.rtg: the solid scenery as triangles, no terrain, no cut-outs with holes (trees, fences), so these cast none; bottom level vkr_shadow_terrain, top level in view slot 0, unused by this game. Cars cast none. One ray a pixel in the beam.

Don't wait on a process with pgrep -f on its own name in a shell loop: the loop matches itself (cost an hour).

Debug launcher: tools/dev/race.sh, any location, track, car, paint, time and weather through the Time Trial menus, all cars unlocked (ORR_UNLOCK_ALL hooks 0x4784f0, ORR_RACE_LOOK writes 0x5cea00 at 0x4da920). The header lists the numbers. Keys at 50-frame spacing: 30 dropped some.

Streamer mode (VIDEO > More Options, game.c texture_name_hook): the seven flagged paints load the car's plain paint, also dirty and muddy, for AI cars and previews too. List from the 3D Car Garage chat, each texture looked at: Blackfoot 3, 4 (bug15); Comanche 3, 4 (bug05); Bronco 3 (bug14); Polecat 3 (bug20); Firesole 5 (bug17). The menu still offers those numbers, showing plain.

Night (More Options, Original, Dark, Darkest): no re-bake. draw.frag scales lit colour and specular (the fog) by 1, 0.55, 0.3 at night, flat non-HUD draws (sky) too; lamps, being additive, stay. Needs the baked lighting on.

Audio: AUDIO > More Options (Music, Engines, Effects, 0-15; 10 as made, music starts at 7), mss32.c mix_options; engines by file name: r1k, r2k, r4k, neutral, gear, rev [guess]. The game's own AUDIO > Advanced has a Music Volume too: at the default 16 it clamps at full (slider x base / 12).

Menu table: only 11 widget ids were free. table_new_hook gives the game's table a larger block and new widgets take ids from 0x191 (0x190 is its count).

T7, with these: fncheck baseline unchanged, lockstep identical over 3,600 frames, a real-time race of 5,200 frames on the RTX 4070 with validation on: no messages.

T7, with the fixes above: fncheck and lockstep unchanged; a race with stencil, Extreme, AO and dust on the RTX 4070, 5200 frames: no validation messages.
Not done: ray-traced shadows. Findings (static reading of 1nsane-native's shadows.c):
the RT casters need draws with matrices (vk.c caster requires k), and pretransformed draws have only clipped, visible
triangles, so positions recovered from x, y, w give no casters off screen: shadows would vanish at the edges. Solid
shadows need world meshes hooked before the game's own transform, code not read yet, and the game's own shadows off.

#### 7d. Ray-traced lighting, baked ahead of time (Ant 2026-10-31: "real lightmaps for everything")
Baked offline by rays, read back per pixel at almost no cost: 0.4 ms a frame at 2560x1440 on the 890M (6.8 to 7.25 ms, same frames, ORR_FRAME_TRACE). On by default; VIDEO > More Options, "Ray-Traced Lighting", Original puts the game's light back.

Baking: `tools/bake/bake_all.py [SLUG...]` (needs the rip's tracks in Car Models; about 25 minutes on 16 threads here, 15-25 s a bake). export.py turns a track's GLB into triangles in game units (terrain, opaque and cut-out scenery; cut-outs keep an alpha mask so trees cast tree shadows). bake.c is our own CPU ray caster (BVH, no libraries). One bake per layout per sun direction, looks whose suns are within 1 degree sharing one: 95 files, 421 MB in ~/.local/share/offroad-native/lightmaps (not in git, made from the game), and index.txt: env, layout, look, file, sun, colours from the rip's sidecars. A bake holds: sun visibility at 16 texels a cell (31 cm, capped at 8192), sky visibility and one bounce at 4 a cell, the same three in a grid draped over the terrain, 2 a cell x 12 layers 2 m apart, for objects and cars, and the height field. tools/bake/view.py shows one.

Reading it, no vertex data touched: draw.vert turns each pretransformed vertex back into world space (the camera's matrix inverted, light.c, sent at the start of each race view, 0x4daec0 calling 0x4f0770; only that caller, the menus' car previews have cameras too). draw.frag light_ratio: a pixel within 25 cm of the game's own terrain triangles reads the fine maps, anything else the grid; the normal comes from derivatives; the vertex colour is multiplied by (light now) / (light the game gave it), both clamped by the largest channel as the game does [verified: 0x53fe80], so fog, which the game puts in the same colour and the specular, survives, and unblocked points are exactly the original. Applied to depth-drawn world, opaque or alpha-blended; not to additive draws or flat ones (sky, HUD). Sky takes 65 % of the ambient away at most (ORR_LIGHT_SKY), the game's ambient being a strong fill.

Which bake: `<env>\track_<n>\[redneck_]<env>.ob3` as the game asks for it (files.c, also for files in Levels.mng), weather and time from 0x5cea00, +4 [verified: 0x4d9c30, 0x54e370]. Long tracks (Dead Man's Drop, 45 x 1501 cells) have the grid's layers side by side: 36000 rows is more than an image may have.

Checked: sun direction against the game's own sun on screen (frame 6600: baked 508,72, drawn about 506,64). Races on all six locations with validation on: no messages. A lap at sunset against the original. Not checked: rain, snow and night looks in the game (no menu route to them in a scripted run); they use the same maps with their own colours. Debug: ORR_LIGHT_DEBUG 2 the ratio, 3 world position, 4 sun, sky, N.L; ORR_LIGHT_LOG the sun's screen position.
Known limits: moving things cast nothing (the game's blob shadows stay); keys that animate the sun are taken at tick 0; the bounce's tint is a fixed warm colour [guess], only its amount is measured; helicopter ignored.

### 7e. The terrain by hand (runtime/native_land.c)
Walk 0x54f230 (0x53f680, 0x53f070, 0x53eb50, 0x53f2a0, the waves 0x550ee0, 0x550fe0), land 0x540260 (0x53fc20, 0x53fe80, 0x540a60, 0x541150, 0x5420c0, 0x5430e0, 0x543860, 0x544070) and fog 0x544da0 (0x5450a0) are hand-written C on guest memory. Option native_terrain, default on, options.txt only so far (no menu row); ORR_NATIVE_TERRAIN=0 is the translated code; never when !widescreen_allowed(), so test runs and lockstep keep the original. x87 arithmetic stays x87 (long double, the original's order, the program's control word): that is why there is no last-unit difference anywhere [verified: below]. A faster float32/SIMD version would have to give that up.
Still through guest_call: lights' slots (0x50bcb0 0.2 % of CPU), 0x52aeb0, 0x4f0cb0, the clipper 0x4f5240 (0.9 %), state setters, slot +0x28 of a class other than 0x52b580, 0x52ddd0, 0x4f8ae0, and growing any array (0x54cb10 or the whole original vertex or cell). DAT_5d3e01 == 0 or renderer +0x5a1 set: land and fog go to the original whole [guess: neither happens in play; a line goes in the log if so].
Known difference, one: water vertex +0x40. The original copies a stale stack dword there (0x53eeb0, 0x53f049); ours writes 0. No reader [verified: decompiles of the writers, 0x555550, 0x556750, 0x4a0180; disassembly of the light 0x53a9f0].
ORR_TERRAIN_AB=<N>: every Nth frame walk, land and fog each run twice from the same input, the original then ours with all program memory put back; all program memory (without dead stacks) and every Direct3D call with its vertices and indices are compared, the first differences logged as "terrain ab:" with a summary at exit. A view whose array grew is skipped (the allocator's books are outside program memory). ORR_TERRAIN_AB_BREAK=1 changes one vertex by a last unit, to show the check sees it [verified: reported]. ORR_TERRAIN_AB_FORCE=water,dark: paths no race reaches: V+0x38 bit 1 was never set in any run [verified: 0 water vertices], and dark blends never occurred [verified: count 0].
Result 2026-11-02, 14 unforced race runs on all six locations, tracks 1 and 2, day to night, rain and snow, Far and Extreme, plus 2 forced: no difference but +0x40. tools/dev/race.sh's tracks 3 and 4 did not start a race on a fresh user, so they are not covered. Frame dumps with the option off and on, Everglade 1 at sunset: all 7 identical [verified: md5]. tools/dev/translator_check.sh, run locally: fncheck no failure outside the baseline, lockstep identical on all 3600 frames.
Measured (ORR_TIME_LOG, Dead Man's Drop 1, ms a frame, 2 alternating runs each, other sessions loading the machine): Extreme walk 1.82 to 0.51, land 0.86 to 0.75, world 4.4 to 3.1; Far walk 1.19 to 0.35, land 0.74 to 0.68, world 3.5 to 2.8. What is left in land is mostly the clipper's flushes into d3d.c's draw path; the rest of the world is object drawing (0x527e60, 0x5609f0, 0x52c8f0, 0x54ea20's own loops).

## 8. Not done yet (next)
Resolution and frame limit are options.txt only: More Options has room for two more rows. Play through by Ant. Real audio device and real pad, only scripted so far.
Network play (DirectPlay stand-in has no connections). Frame-time and CPU measurements not taken. The
Championship and later divisions not played beyond the first race's start.

### 9. fncheck baseline (2026-10-30)
7050 pass, 15202 skipped, 20 FAIL runs over 8 functions: x87 extended precision against doubles
(0x56dc5a, 0x56dc64, 0x574b16, 0x574d1c) and low bytes of eax left from fnstsw-style sequences (0x4b4da0,
0x515c70, 0x523550, 0x52d8e0) [guess: the same harmless classes as the other ports; not each checked].
tests/fncheck_baseline.txt.

## Checks on the T7 (2026-10-30)
fncheck: nothing outside the baseline. Lockstep: memory identical on all 3600 frames, menus into a race,
keyboard, widescreen off (test runs default to off). tools/dev/ws_check.sh, the same race with widescreen on and off:
speed and gear equal at frames 3300, 3600, 3900, 4200 [verified: gauge crops], so it does not change the
driving; memory is not compared there. Validation on lavapipe: 20 errors, all vkCreateShaderModule for
features it lacks (ray query, buffer addresses, non-uniform indexing), none from a draw; the shaders
are hardtruck2-native's unchanged. On the RTX 4070: no validation messages through a 3400-frame run into a race. With the Widescreen row (same day): fncheck and lockstep unchanged; switching it on with the pad and racing,
6000 frames on the RTX 4070: no validation messages. With the button icons (2026-10-31): fncheck and lockstep unchanged (keyboard runs do not load the icons in test
mode); pad into a race, 3600 frames on the RTX 4070: no validation messages, 10 font images written.
With the merged controllers (2026-10-31): fncheck and lockstep unchanged; two pads in a two-player race, widescreen,
6300 frames on the RTX 4070: no validation messages. On the T7 the validation messages go to the game's stdout, not its log.

## T7
tools/dev/translator_check.sh runs fncheck and lockstep. On the T7 third_party/sdl3 and .venv are links to
hardtruck2-native's, and the game is at the usual path.

## Test recipe
    SIZE=1024x768 bash tools/dev/shot.sh work/s 5700 300 "100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*2600"
Frame dumps land in work/s as .ppm. (Escapes after the movies move the menu's selection to Exit Game,
so the UPs reach Time Trial.)
