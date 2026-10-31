/* Widescreen: the race's world wider than the game's 4:3 mode, height of view kept (Hor+).

   The game stays in 1024x768, so its screens and HUD keep their layout: everything flat is placed
   by its 4:3 coordinates (vk.c: c->ox, the side space; the HUD's side pieces go to the edges). OffRoad.exe
   transforms and clips the world itself and hands the renderer screen coordinates (XYZRHW). Its
   focal lengths are the viewport's width / 640 (0x4f1350, both axes) and it clips to the viewport.
   So the cameras the race sets up (0x4d94b0, viewport_hook) get a viewport wider by ws_aspect / (4/3) about the same
   centre, focal lengths scaled back: the 4:3 picture stays as it was and the world carries on past its
   sides, x from below 0 to above 1024. The renderer squeezes those back into the viewport and
   spreads it [verified: decompile of 0x4f1350, 0x4d94b0; frames 2026-10-30].

   Not finished, so off by default: DESIGN.md section 6. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

static int ws_on, allowed;
static float ws_want;            /* a set aspect, 0 = follow the screen */
float ws_aspect = 4.0f / 3.0f;   /* current target aspect (width / height) */
float ws_world_aspect;           /* y/x scale of the widened camera's projection */
int ws_race_depth, ws_hud_depth, ws_menu_depth;   /* (TD6's placement rules: unused here) */

static void parse(void)
{
    const char *v = option_str("widescreen", "off");
    ws_want = 0;
    if (!strcasecmp(v, "off") || !strcasecmp(v, "no") || !strcmp(v, "0")) {
        ws_on = 0;
    } else if (!strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcmp(v, "1")) {
        ws_on = 1;
    } else {
        float a = 0, b = 0;
        if (sscanf(v, "%f:%f", &a, &b) == 2 && a > 0 && b > 0) ws_want = a / b;
        else if (sscanf(v, "%f", &a) == 1 && a > 1.0f) ws_want = a;
        ws_on = ws_want > 0;
        if (!ws_on)
            LOG(LOG_ALWAYS, "options: widescreen \"%s\" is not on, off or an aspect like 16:9", v);
    }
    ws_aspect = ws_on && ws_want > 0 ? ws_want : 4.0f / 3.0f;
}

static int viewport_hook(CPU *c)
{
    float k = ws_on ? ws_aspect / (4.0f / 3.0f) : 1.0f;
    /* who sets it: the race's cameras (0x4d94b0, and 0x524f90, 0x525880, which set them again for the
       race's scripted views, from what is already widened); screens and the HUD's own cameras are left 4:3 */
    uint32_t from = R32(c->esp);
    if (k <= 1.001f || from < 0x4d94b0 || from >= 0x4d9900)
        return 0;
    uint32_t p = R32(c->esp + 4), fx = R32(p), fy = R32(p + 4), width = R32(p + 0x24);
    union { float f; uint32_t u; } a = {RF32(p) / k}, b = {RF32(p + 4) / k}, n = {RF32(p + 0x24) * k};
    W32(p, a.u);
    W32(p + 4, b.u);
    W32(p + 0x24, n.u);
    F_004f1350_orig(c);
    W32(p, fx);
    W32(p + 4, fy);
    W32(p + 0x24, width);
    return 1;
}

void widescreen_init(void)
{
    allowed = !deterministic || getenv("ORR_WIDESCREEN");
    parse();
    if (ws_on && !allowed)
        ws_on = 0;
    hook_viewport = viewport_hook;
}

void widescreen_options(void)
{
    if (allowed) parse();
}

int widescreen_on(void) { return ws_on; }

/* The renderer tells us the shape of the picture area (window or screen). */
void widescreen_screen(int w, int h)
{
    if (!ws_on || ws_want > 0 || w <= 0 || h <= 0)
        return;
    float a = (float)w / (float)h;
    ws_aspect = a > 4.0f / 3.0f ? a : 4.0f / 3.0f;
}

/* The x scale a projection gets: divided by the widening for a perspective one used with a
   viewport as wide as the render target */
float widescreen_projection_x(float x, int perspective, uint32_t vp_x, uint32_t vp_w, uint32_t rt_w, float y)
{
    if (!ws_on || ws_aspect <= 4.0f / 3.0f + 0.001f || !perspective || vp_x != 0 || vp_w != rt_w || x == 0.0f)
        return x;
    float wx = x / (ws_aspect / (4.0f / 3.0f));
    ws_world_aspect = y / wx;
    return wx;
}
