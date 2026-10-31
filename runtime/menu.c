/* The port's options in the game's own menu: GAME OPTIONS > VIDEO > More Options.

   The front end's screens are widgets in a table by id (0x4702f0; the table is [0x5ce2c4]+0x4b0, 0x190
   ids; the game uses most above 0x142 too: 0x159, 0x15e, 0x15f, 0x161..0x168 are free [verified: no immediate of them in its code]). 0x488330 makes VIDEO's: a screen 0x12f and rows 0x130..0x136, Back 0x137, each a
   0xc4-byte widget (0x46b0e0) with a handler: fastcall(widget, message), the message's first dword its kind, the
   widget's id at +0x28 [verified: disassembly of 0x4874f0, 0x487850 the Shadows row, 0x4881d0 Back]:
   a row: 0 made, 0x456950(id, label; x, y, 0, 0, width, height, items, &value, 0, count, 0), items being pairs of
   text and value; 3 pressed (a sound); 4 focused (the help line 0x458d00, then 0x457fd0); 5 unfocused
   0x4582c0; 6, 7 down and up (0x458970).
   a button: 0 made, 0x457740(id, label; x, y, 0, 0, width, height, 0), then 0x45ce10(id); 3 pressed: Back's
   closes its screen (0x458cb0) and opens the one before under 0x118 (0x458730); 4, 5, 6, 7 as a row.
   a screen (0x46a4d0): 0 made, the title on 0x119 (0x458c60), its rows added (0x458730), the ones in use made
   selectable (0x4588d0), the focus given (0x458810).
   The new widgets are made by the game's own functions in the ids above its own, so they have its arrows, sounds,
   help line and controller support. VIDEO's new button takes the room under Brightness, whose y (in data,
   0x5b95c8) moves up 14 to fit above Back. */
#include <string.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

#define G_NEW 0x56cd7au                  /* operator new */
#define BUTTON_MORE 0x15eu
#define PAGE 0x15fu
#define PAGE_BACK 0x159u
#define ROW_FIRST 0x161u               /* up to 0x168 */
#define VIDEO 0x12fu
#define PARENT 0x118u

typedef struct {
    const char *key, *default_value, *label, *hint;
    int n;
    const char *text[4], *value[4];
    uint32_t items, cell;
    int shown;
} Row;

static Row rows[] = {
    {"widescreen", "on", "Widescreen", "Turn Widescreen On or Off", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0},
    {"antialiasing", "off", "Anti-Aliasing", "Smoother Edges - Asks More Of The Graphics Card", 2, {"Off", "4x"}, {"off", "4x"}, 0, 0, 0},
    {"textures", "smooth", "Texture Filtering", "Sharper Textures At A Distance", 2, {"Original", "Smooth"}, {"original", "smooth"}, 0, 0, 0},
    {"colour_precision", "high", "Colour Precision", "Less Banding In The Sky And Fog", 2, {"Original", "High"}, {"original", "high"}, 0, 0, 0},
    {"draw_distance", "far", "Far Draw Distance", "See Further - From The Next Race", 3, {"Original", "Far", "Extreme"}, {"original", "far", "extreme"}, 0, 0, 0},
    {"ambient_occlusion", "on", "Ambient Occlusion", "Soft Shadows In Corners And Under Things", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0},
    {"volumetric_dust", "on", "Volumetric Dust", "Real Clouds Of Dust And Exhaust Smoke", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0},
};
#define NROWS ((int)(sizeof rows / sizeof *rows))

static float fl_y(int i) { return 90.0f + 40.0f * (float)i; }
static uint32_t fu(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

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

static uint32_t help_more, help_back, text_more, text_title, text_back;

METHOD(row_handler, 0)
{
    uint32_t e = c->ecx, d = c->edx, id = R32(c->ecx + 0x28);
    int i = (int)id - (int)ROW_FIRST;
    if (i < 0 || i >= NROWS)
        return 1;
    Row *r = &rows[i];
    switch (R32(c->edx)) {
    case 0:
        LOG(LOG_API, "row %s made, id %x", r->label, id);
        c->ecx = id;
        c->edx = gstrdup(r->label);
        guest_call(c, 0x456950, 11, fu(145.0f), fu(fl_y(i)), 0u, 0u, fu(350.0f), R32(0x57c200), r->items, r->cell, 0u, (uint32_t)r->n, 0u);
        c->ecx = e;
        c->edx = d;
        break;
    case 3: fast(c, 0x458cd0, 0x4b, 0); break;
    case 4:
        fast(c, 0x458d00, gstrdup(r->hint), 0);
        fast(c, 0x457fd0, id, 0);
        break;
    case 5: fast(c, 0x4582c0, id, 0); break;
    case 6:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, id, 1);
        break;
    case 7:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, id, 0);
        break;
    }
    return 1;
}

/* More Options and the page's Back: a button that leaves one screen for another */
METHOD(button_handler, 0)
{
    uint32_t e = c->ecx, d = c->edx, id = R32(c->ecx + 0x28);
    int more = id == BUTTON_MORE;
    switch (R32(c->edx)) {
    case 0:
        c->ecx = id;
        c->edx = more ? text_more : text_back;
        guest_call(c, 0x457740, 7, fu(210.0f), fu(more ? 375.0f : R32(0x57c1ec) ? RF32(0x57c1ec) : 420.0f), 0u, 0u,
                   R32(0x57c1e4), R32(0x57c1e8), 0u);
        c->ecx = e;
        c->edx = d;
        fast(c, 0x45ce10, id, 0);
        break;
    case 3:
        fast(c, 0x458cd0, 0x4b, 0);
        fast(c, 0x458cb0, more ? VIDEO : PAGE, 0);
        fast(c, 0x458730, PARENT, more ? PAGE : VIDEO);
        break;
    case 4:
        fast(c, 0x458d00, more ? help_more : help_back, 0);
        fast(c, 0x457fd0, id, 0);
        break;
    case 5: fast(c, 0x4582c0, id, 0); break;
    case 6:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, id, 1);
        break;
    case 7:
        fast(c, 0x458cd0, 0x4a, 0);
        fast(c, 0x458970, id, 0);
        break;
    }
    return 1;
}

/* The rows shown, when the screen has come in (as 0x487410, 0x4874f0's callback through 0x478130) */
METHOD(page_shown, 0)
{
    fast(c, 0x4585d0, PAGE_BACK, 0);
    for (int i = 0; i < NROWS; i++)
        fast(c, 0x4585d0, ROW_FIRST + (uint32_t)i, 0);
    return 1;
}

METHOD(video_shown, 0)
{
    fast(c, 0x4585d0, BUTTON_MORE, 0);
    return 1;
}

static void when_shown(CPU *c, uint32_t fn)
{
    c->ecx = R32(R32(0x5ce2c4) + 0x4a4);
    guest_call(c, 0x478130, 1, fn);
}

METHOD(page_handler, 0)
{
    if (R32(c->edx) == 0) {
        fast(c, 0x458c60, 0x119, text_title);
        fast(c, 0x458730, PAGE, PAGE_BACK);
        for (int i = 0; i < NROWS; i++)
            fast(c, 0x458730, PAGE, ROW_FIRST + (uint32_t)i);
        for (int i = 0; i < NROWS; i++)
            fast(c, 0x4588d0, ROW_FIRST + (uint32_t)i, 0);
        fast(c, 0x4588d0, PAGE_BACK, 0);
        fast(c, 0x458810, PAGE_BACK, 0);
        uint32_t e = c->ecx;
        when_shown(c, thunk_addr(page_shown, "more options shown"));
        c->ecx = e;
    }
    return 1;
}

static uint32_t new_icon(CPU *c, uint32_t id)
{
    uint32_t mem = guest_call(c, G_NEW, 1, 0x48u);
    c->ecx = mem;
    return guest_call(c, 0x453d10, 2, gstrdup("icon_video_gamma"), id);
}

static void new_widget(CPU *c, uint32_t table, uint32_t id, uint32_t handler)
{
    uint32_t icon = new_icon(c, id), mem = guest_call(c, G_NEW, 1, 0xc4u);
    c->ecx = mem;
    uint32_t w = guest_call(c, 0x46b0e0, 13, 0u, 0u, 0u, 0u, handler, icon, 0u, 0u, 2u, 2u, 1u, 1u, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, w, id);
}

static int row_value(const Row *r)
{
    const char *v = option_str(r->key, r->default_value);
    for (int k = 0; k < r->n; k++)
        if (!strcasecmp(v, r->value[k]))
            return k;
    return 0;
}

static int video_rows_hook(CPU *c)
{
    uint32_t table = c->ecx;
    F_00488330_orig(c);
    text_more = gstrdup("More Options");
    text_title = gstrdup("MORE OPTIONS");
    text_back = gstrdup("Back");
    help_more = gstrdup("Widescreen, Anti-Aliasing And More");
    help_back = gstrdup("Return To The Video Options");
    for (int i = 0; i < NROWS; i++) {
        Row *r = &rows[i];
        r->items = galloc((uint32_t)r->n * 8);
        for (int k = 0; k < r->n; k++) {
            W32(r->items + 8 * (uint32_t)k, gstrdup(r->text[k]));
            W32(r->items + 8 * (uint32_t)k + 4, (uint32_t)k);
        }
        r->cell = galloc(4);
        r->shown = row_value(r);
        W32(r->cell, (uint32_t)r->shown);
        new_widget(c, table, ROW_FIRST + (uint32_t)i, thunk_addr(row_handler, "more options row"));
    }
    uint32_t button = thunk_addr(button_handler, "more options button");
    new_widget(c, table, BUTTON_MORE, button);
    new_widget(c, table, PAGE_BACK, button);
    uint32_t icon = new_icon(c, PAGE), mem = guest_call(c, G_NEW, 1, 0xa4u);
    c->ecx = mem;
    uint32_t page = guest_call(c, 0x46a4d0, 7, 0u, 0u, 0u, 0u, thunk_addr(page_handler, "more options page"), icon, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, page, PAGE);
    c->ecx = table;
    W32(0x5b95c8, fu(331.0f));
    return 1;
}

static int video_screen_hook(CPU *c)
{
    int made = R32(c->edx) == 0;
    F_004874f0_orig(c);
    if (made) {
        fast(c, 0x458730, VIDEO, BUTTON_MORE);
        fast(c, 0x4588d0, BUTTON_MORE, 0);
        uint32_t e = c->ecx;
        when_shown(c, thunk_addr(video_shown, "video shown"));
        c->ecx = e;
    }
    return 1;
}

/* once a frame: a row's value changed: it applies at once and is kept */
void menu_frame(void)
{
    int changed = 0;
    for (int i = 0; i < NROWS; i++) {
        Row *r = &rows[i];
        if (!r->cell || (int)R32(r->cell) == r->shown)
            continue;
        r->shown = (int)R32(r->cell);
        if (r->shown >= 0 && r->shown < r->n)
            option_set(r->key, r->value[r->shown]);
        changed = 1;
    }
    if (changed) {
        extern void options_changed(void);
        options_save();
        options_changed();
    }
}

void menu_init(void)
{
    if (!widescreen_allowed())           /* (test runs stay as the original, for the lockstep check) */
        return;
    hook_video_rows = video_rows_hook;
    hook_video_screen = video_screen_hook;
}
