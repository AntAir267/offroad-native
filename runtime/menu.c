/* The port's options in the game's own menu: a Widescreen row on GAME OPTIONS > VIDEO.

   The front end's screens are widgets in a table by id (0x4702f0; the table is [0x5ce2c4]+0x4b0, 0x190
   ids). 0x488330 makes VIDEO's: a screen 0x12f and rows 0x130..0x136, Back 0x137, each a 0xc4-byte widget
   (0x46b0e0) with a handler: fastcall(widget, message), the message's first dword its kind
   [verified: disassembly of 0x4874f0, 0x487850, the Shadows row]:
   0 made: 0x456950(id, label; x, y, 0, 0, width, height, items, &value, 0, count, 0), items being
   pairs of text and value; 3 highlighted (a sound); 4 focused (the help line 0x458d00, then 0x457fd0);
   5 unfocused 0x4582c0; 6, 7 down and up (0x458970).
   The screen's own handler 0x4874f0 adds the rows (0x458730) and makes the ones in use
   selectable (0x4588d0). So the new row is one more of these, built by the game's own functions, with
   Brightness moved up (y 345 to 331: its y lives in data, 0x5b95c8) to make room above Back. */
#include <string.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

#define G_NEW 0x56cd7au                 /* operator new */
#define ROW_ID 0x18fu                    /* free: the game's ids stop before it */
#define ROW_Y 375.0f
#define GAMMA_Y 331.0f

static uint32_t items, cell, label, hint;
static int shown;

static uint32_t fast(CPU *c, uint32_t fn, uint32_t a, uint32_t b)
{
    uint32_t e = c->ecx, d = c->edx;
    c->ecx = a;
    c->edx = b;
    uint32_t r = guest_call(c, fn, 0);
    c->ecx = e;
    c->edx = d;
    return r;
}

static uint32_t fu(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

METHOD(row_handler, 0)
{
    uint32_t e = c->ecx, d = c->edx;
    switch (R32(c->edx)) {
    case 0:
        c->ecx = ROW_ID;
        c->edx = label;
        guest_call(c, 0x456950, 11, fu(180.0f), fu(ROW_Y), 0u, 0u, R32(0x57c1fc), R32(0x57c200), items, cell, 0u, 2u, 0u);
        c->ecx = e;
        c->edx = d;
        break;
    case 3: fast(c, 0x458cd0, 0x4b, 0); break;
    case 4:
        fast(c, 0x458d00, hint, 0);
        fast(c, 0x457fd0, ROW_ID, 0);
        break;
    case 5: fast(c, 0x4582c0, ROW_ID, 0); break;
    case 6:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, ROW_ID, 1);
        break;
    case 7:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, ROW_ID, 0);
        break;
    }
    return 1;
}

static int video_rows_hook(CPU *c)
{
    uint32_t table = c->ecx, e = table;
    F_00488330_orig(c);
    items = galloc(16);
    W32(items, gstrdup("Off"));
    W32(items + 4, 0);
    W32(items + 8, gstrdup("On"));
    W32(items + 12, 1);
    cell = galloc(4);
    W32(cell, widescreen_on() ? 1 : 0);
    shown = widescreen_on() ? 1 : 0;
    label = gstrdup("Widescreen");
    hint = gstrdup("Turn Widescreen On or Off");

    uint32_t icon_mem = guest_call(c, G_NEW, 1, 0x48u), w_mem = guest_call(c, G_NEW, 1, 0xc4u);
    c->ecx = icon_mem;
    uint32_t icon = guest_call(c, 0x453d10, 2, gstrdup("icon_video_gamma"), ROW_ID);
    c->ecx = w_mem;
    uint32_t w = guest_call(c, 0x46b0e0, 13, 0u, 0u, 0u, 0u, thunk_addr(row_handler, "widescreen row"), icon, 0u, 0u, 2u, 2u,
                            1u, 1u, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, w, ROW_ID);
    c->ecx = e;
    W32(0x5b95c8, fu(GAMMA_Y));
    return 1;
}

static int video_screen_hook(CPU *c)
{
    int made = R32(c->edx) == 0;
    F_004874f0_orig(c);
    if (made) {
        fast(c, 0x458730, 0x12f, ROW_ID);
        fast(c, 0x4588d0, ROW_ID, 0);
    }
    return 1;
}

/* once a frame: the row's value changed, it applies at once and is kept */
void menu_frame(void)
{
    if (!cell || (int)R32(cell) == shown)
        return;
    shown = (int)R32(cell);
    option_set("widescreen", shown ? "on" : "off");
    options_save();
    widescreen_options();
}

void menu_init(void)
{
    if (!widescreen_allowed())           /* (test runs stay as the original, for the lockstep check) */
        return;
    hook_video_rows = video_rows_hook;
    hook_video_screen = video_screen_hook;
}
