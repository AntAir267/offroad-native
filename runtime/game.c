/* Off-Road's own fixes and hooks (builds/orr_zoom.py HOOKS).

   Draw distance: VIDEO's Draw Distance row picks a far plane of 16000, 21000 or 26000 at the race's load
   (0x4d8630: 0x5d0e78), which the cameras take from there (0x4a54b0 from the track's camera file, 0x4d94b0 the
   race's own) [verified: decompile]. The More Options row raises what they read. Graphics only: what the
   game's physics and AI see does not use it [guess: to be checked against a run]. */
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

static int far_plane_hook(CPU *c)
{
    (void)c;
    const char *v = option_str("draw_distance", "original");
    float k = !strcasecmp(v, "far") ? 1.5f : !strcasecmp(v, "extreme") ? 2.5f : 1.0f;
    static float base;
    float now = RF32(0x5d0e78);
    if (now != base * 1.5f && now != base * 2.5f)
        base = now;                      /* (the game set it afresh for this race) */
    union { float f; uint32_t u; } u = {base * k};
    W32(0x5d0e78, u.u);
    return 0;
}

void game_init(void)
{
    if (!widescreen_allowed())
        return;
    hook_cameras_file = far_plane_hook;
    hook_race_cameras = far_plane_hook;
}
