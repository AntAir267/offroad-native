/* port.h: Off-Road: Redneck Racing as the AntiStatic runtime sees it (the toolkit's
   runtime/win32/port_example.h describes each value). */
#pragma once

#define PORT_TITLE "Off-Road: Redneck Racing"
#define PORT_SLUG "offroad-native"
#define PORT_ENV "ORR"

/* the game's folder as the program sees it, and its program */
#define PORT_VROOT "C:\\OFFROAD"
#define PORT_START_DIR ""
#define PORT_EXE "OffRoad.exe"
#define PORT_CMDLINE_ARGS ""
#define PORT_VOLUME_LABEL "OFFROAD"

/* program memory above OffRoad.exe's image (0x400000..0x5ee000) and its translated tables */
#define PORT_ARENA_START 0x02400000u

/* two joysticks: one per player in a two-player race (padgame.c) */
#define PORT_PAD_JOYS 2

/* --test runs: the mixer's clock is the virtual clock as it stood at each presented frame (what
   this game's own runtime did, so its scripted runs' sound stays as recorded) */
#define PORT_TEST_SOUND_AT_FRAME 1

/* The toolkit's D3D7 renderer (runtime/d3d7): this game's defaults and the features its game code
   feeds (port_example.h and vk.c's head describe each) */
#define PORT_DEFAULT_RESOLUTION "native"
#define PORT_DEFAULT_FOG 0
#define PORT_DEFAULT_TEXTURES "smooth"
#define PORT_DEFAULT_COLOUR_PRECISION "high"
#define PORT_DDRAW_8BIT 1
#define PORT_WS_BG_DEPTH 1
#define PORT_D3D7_SCREEN_WORLD 1
#define PORT_D3D7_SSAO 1
#define PORT_D3D7_BAKED_LIGHT 1
#define PORT_D3D7_WATER 1
#define PORT_D3D7_SCENE_OBJECTS 1
#define PORT_D3D7_DEPTHBACK 1
#define PORT_D3D7_STENCIL 1
#define PORT_D3D7_AA_MODES 1
#define PORT_D3D7_TAA_HP_HISTORY 1
#define PORT_D3D7_SMOKE_STEADY 1
#define PORT_D3D7_FAST_SCREEN 1
#define PORT_D3D7_SMOKE_DENSITY 3.0f
#define PORT_D3D7_UNITS_PER_METRE 100.0f

/* widescreen.c: this game's enhancements are off in plain test runs (the lockstep check), unless ORR_WIDESCREEN */
int widescreen_allowed(void);
