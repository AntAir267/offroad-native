/* Off-Road's side of the AntiStatic runtime: the hooks the toolkit core calls (its weak defaults
   are replaced here), and what the port's own copies of the core used to do for this game. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "sound.h"
#include "pad.h"
#include "gfx.h"

void ddraw_init(void), vk_stop(void);
void widescreen_init(void), padgame_init(void), bink_init(void), menu_init(void);
void game_options(void), widescreen_options(void), vk_options(void), d3d_options(void);
void light_file_opened(const char *name);
uint32_t dplay_cocreate(uint32_t clsid, uint32_t riid, uint32_t ppv);
const char *padgame_device_name(const char *name);
void padgame_frame(unsigned long frame);
void settings_put(const char *key, const char *name, uint32_t type, const void *data, uint32_t size);
int settings_has(const char *key, const char *name);

/* comments for keys written to the file the first time */
static const char *const help[][2] = {
    {"display", "display: window or fullscreen (Alt+Enter switches too)"},
    {"resolution", "resolution: native (drawn at the screen's own resolution), original (the game's own "
                   "640x480 / 800x600, scaled up) or a multiple of the game's size such as 2"},
    {"widescreen", "widescreen: on (the race's world as wide as the screen, the same height of view; menus and movies stay 4:3), off "
                   "(4:3 as shipped), or a shape such as 16:9. GAME OPTIONS > VIDEO has it too"},
    {"baked_lighting", "baked_lighting: on (the ray-traced shadows and shade) or off (the game's own light)"},
    {"car_lights", "car_lights: on (headlights and taillights light what is around them at night and dusk, with baked_lighting) or off"},
    {"car_light_shadows", "car_light_shadows: on (the headlights cast ray-traced shadows of the scenery, where the graphics card traces rays) or off"},
    {"car_shadows", "car_shadows: on (cars cast shadows from the sun and from each other's headlights, with baked_lighting) or off"},
    {"camera_stick", "camera_stick: on (the right stick swings the race camera round the car) or off"},
    {"frame_rate", "frame_rate: 60 (one frame a tick, as the game steps), 120, 144, or display (the screen's): frames between ticks with camera and cars blended"},
    {"water", "water: simulated (shallow-water simulation on the GPU, with baked_lighting) or original"},
    {"water_area", "water_area: small (64 m) or large (128 m), the simulated area"},
    {"fog", "fog: off (none but the last 5 percent before the far plane) or original"},
    {"map_lights", "map_lights: on (the lamps on the tracks light what is around them at night and dusk, baked with their shadows) or off"},
    {"night", "night: original, dark or darkest (with baked_lighting)"},
    {"streamer_mode", "streamer_mode: on hides the paint jobs with the Confederate flag; each loads the car's plain paint"},
    {"music_level", "music_level, engine_level, effects_level: 0..15, 10 as the game made them (music starts at 7)"},
    {"engine_level", "engine_level: see music_level"},
    {"effects_level", "effects_level: see music_level"},
    {"antialiasing", "antialiasing: off, fxaa, taa (the port's own temporal one), fsr (AMD's FSR 2), dlss (NVIDIA RTX), 2x or 4x (drawn larger and scaled down)"},
    {"render_scale", "render_scale: native, quality, balanced, performance or auto (taa, fsr, dlss: the 3D world drawn smaller and scaled up)"},
    {"textures", "textures: original (as the game asks) or smooth (trilinear and anisotropic filtering on "
                 "the 3D world's textures: sharper roads at a distance)"},
    {"colour_precision", "colour_precision: original (16-bit colour, as the game draws) or high (the 3D "
                         "world drawn with more precision, then dithered: less banding in the sky and fog)"},
    {"vsync", "vsync: on (frames wait for the screen) or off"},
    {"frame_limit", "frame_limit: off, screen (3 under the screen's refresh, for G-Sync / FreeSync) or a rate"},
    {"render_thread", "render_thread: on (drawing on a thread of its own: faster) or off"},
    {"rumble", "rumble: on (the game's force feedback on the controller's motors) or off"},
    {"controller_deadzone", "controller_deadzone: the sticks' dead zone against resting drift, percent"},
    {"renderer", "renderer: original (the D3D stand-in lights the meshes) or native (the GPU does: being built)"},
    {NULL, NULL}};

/* The first start (no options.txt yet, outside test runs): the toolkit writes a file listing every
   option, commented out at its default, from these two */
static const char *const def[][2] = {
        {"display", "window"}, {"resolution", "native"}, {"widescreen", "on"}, {"antialiasing", "off"},
        {"textures", "smooth"}, {"colour_precision", "high"}, {"ambient_occlusion", "on"}, {"volumetric_dust", "on"}, {"baked_lighting", "on"}, {"night", "original"}, {"car_lights", "on"}, {"fog", "off"}, {"water", "simulated"}, {"water_area", "large"}, {"frame_rate", "display"}, {"camera_stick", "on"}, {"car_shadows", "on"}, {"car_light_shadows", "on"}, {"map_lights", "on"}, {"streamer_mode", "off"}, {"music_level", "7"}, {"engine_level", "10"}, {"effects_level", "10"}, {"draw_distance", "far"}, {"vsync", "on"},
        {"frame_limit", "off"}, {"render_thread", "on"}, {"rumble", "on"}, {"controller_deadzone", "6"},
    {NULL, NULL}};
int port_option_default(int i, const char **key, const char **value)
{
    if (i < 0 || i >= (int)(sizeof def / sizeof def[0]) - 1)
        return 0;
    *key = def[i][0];
    *value = def[i][1];
    return 1;
}
const char *port_option_help(const char *key)
{
    for (int h = 0; help[h][0]; h++)
        if (!strcmp(help[h][0], key))
            return help[h][1];
    return NULL;
}

/* The game's own settings on the first start: 1024x768, 32-bit, the primary display driver (it
   would otherwise ask for them in its launcher) */
static void registry_defaults(void)
{
    static const char key[] = "HKLM\\Software\\Rage\\Offroad\\Options";
    if (settings_has(key, "ResolutionX"))
        return;
    static const uint32_t x = 1024, y = 768, depth = 32, tex32 = 1;
    settings_put(key, "Driver", 1, "Primary", 8);
    settings_put(key, "ResolutionX", 4, &x, 4);
    settings_put(key, "ResolutionY", 4, &y, 4);
    settings_put(key, "Colour Depth", 4, &depth, 4);
    settings_put(key, "Textures32", 4, &tex32, 4);
}

/* after the registry is loaded, before the window's display layer */
void port_graphics_init(void)
{
    registry_defaults();
    ddraw_init();
}

/* the toolkit's per-frame call: nothing for this game (its frame work is in game.c's hooks) */
void game_frame(unsigned long frame) { (void)frame; }

/* game.c's game_init runs these first, in the order the port's own start-up had them */
void port_game_init_first(void)
{
    widescreen_init();
    padgame_init();
    bink_init();
}
void port_runtime_ready(void) { menu_init(); }
void port_at_exit(void) { vk_stop(); }           /* the render thread done before the process ends */

void port_options_changed(void)
{
    game_options();
    widescreen_options();
    vk_options();
    d3d_options();
}

/* The track's lighting files: light.c follows which track and layout the game opens */
int port_file_hidden(const char *winpath)
{
    light_file_opened(winpath);
    return 0;
}
uint32_t port_cocreate(uint32_t clsid, uint32_t iid, uint32_t ppv) { return dplay_cocreate(clsid, iid, ppv); }

const char *port_pad_name(int joy) { (void)joy; return padgame_device_name(NULL); }
void port_pad_frame(unsigned long frame) { padgame_frame(frame); }

/* Miles volume classes: the engines are class 2 (Audio > More Options' engine level).
   Which samples are engines, by file: r1k, r2k, r4k, neutral, gear, rev [guess: names; not told
   apart by the code]. A converted copy comes back under a temporary "wav..." name: it keeps the
   class of the file read just before. */
int port_miles_class(const char *path, int previous)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            b = p + 1;
    int named = strncasecmp(b, "wav", 3) != 0 && strstr(path, "Audio") != NULL;
    if (!named)
        return previous;
    return !strncasecmp(b, "r1k", 3) || !strncasecmp(b, "r2k", 3) || !strncasecmp(b, "r4k", 3) ||
           !strncasecmp(b, "neutral", 7) || !strncasecmp(b, "gear", 4) || !strncasecmp(b, "rev", 3) ? 2 : 0;
}
void miles_set_class_gain(int cls, float gain);
void mix_options(void)
{
    miles_set_class_gain(1, (float)atoi(option_str("music_level", "7")) / 10.0f);
    miles_set_class_gain(2, (float)atoi(option_str("engine_level", "10")) / 10.0f);
    miles_set_class_gain(0, (float)atoi(option_str("effects_level", "10")) / 10.0f);
}

/* The renderer's say on this game's draws (runtime/d3d7) */
/* the water's spray, a quarter of its sheets (game.c's splash_draw_hook): their flecks 4 times as strong
   (additive-ish blend 5/6, no depth write, a 64x64 texture, at least 16 vertices, white) */
float port_d3d7_alpha_gain(const D3DState *st, const SwVertex *v, int nv)
{
    const uint32_t *r = st->rs;
    return r[RS_ALPHABLENDENABLE] && r[RS_SRCBLEND] == 5 && r[RS_DESTBLEND] == 6 && !r[RS_ZWRITEENABLE] && st->tex[0] &&
                   st->tex[0]->width == 64 && st->tex[0]->height == 64 && nv >= 16 && (v[0].rgba[0] & 0xffffffu) == 0xffffffu
               ? 4.0f
               : 0.0f;
}
/* the race's countdown, GET READY, 3, 2, 1, GO, laid out a little right of the middle [verified: ORR_HUD_LOG,
   centres 525 and 558 of 1024]: centred in widescreen */
int port_ws_hud_centre(const D3DState *st, float x0, float x1, float y0, float y1)
{
    return st->tex[0] && st->tex[0]->width == 256 && y0 >= 130.0f && y1 <= 300.0f && y1 - y0 >= 70.0f && x0 >= 150.0f &&
           x1 <= 880.0f;
}
/* native_land.c's terrain A/B check records the draws of both runs */
extern int native_ab_mode;
void native_ab_record(uint32_t kind, const void *p, size_t n);
int port_d3d7_ab_mode(void) { return native_ab_mode; }
void port_d3d7_ab_record(uint32_t kind, const void *p, size_t n) { native_ab_record(kind, p, n); }
