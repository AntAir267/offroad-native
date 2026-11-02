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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "options.h"
#include "widescreen.h"
#include "funcs.h"

#define G_NEW 0x56cd7au                  /* operator new */
#define BUTTON_MORE 0x15eu
#define PAGE 0x15fu
#define PAGE_BACK 0x159u
#define ROW_FIRST 0x161u               /* up to 0x168 */
/* more ids than the game has room for: table_new_hook makes its table larger; 0x190 itself is its count */
#define ROW_MORE 0x191u
#define AUDIO_PAGE 0x1a0u
#define AUDIO_MORE 0x1a1u
#define AUDIO_BACK 0x1a2u
#define AUDIO 0x120u
#define LIGHT_PAGE 0x1a3u
#define LIGHT_MORE 0x1a4u
#define LIGHT_BACK 0x1a5u
#define VIDEO 0x12fu
#define PARENT 0x118u

typedef struct {
    const char *key, *default_value, *label, *hint;
    int n;
    const char *text[4], *value[4];
    uint32_t items, cell;
    int shown;
    int audio;                         /* its page: 0 VIDEO's More Options, 1 AUDIO's (n is then how many steps, the value 0..n-1), 2 Lighting */
} Row;

static Row rows[] = {
    {"widescreen", "on", "Widescreen", "Turn Widescreen On or Off", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 0},
    {"antialiasing", "off", "Anti-Aliasing", "Smoother Edges - Asks More Of The Graphics Card", 2, {"Off", "4x"}, {"off", "4x"}, 0, 0, 0, 0},
    {"textures", "smooth", "Texture Filtering", "Sharper Textures At A Distance", 2, {"Original", "Smooth"}, {"original", "smooth"}, 0, 0, 0, 0},
    {"colour_precision", "high", "Colour Precision", "Less Banding In The Sky And Fog", 2, {"Original", "High"}, {"original", "high"}, 0, 0, 0, 0},
    {"draw_distance", "far", "Far Draw Distance", "See Further - From The Next Race", 3, {"Original", "Far", "Extreme"}, {"original", "far", "extreme"}, 0, 0, 0, 0},
    {"ambient_occlusion", "on", "Ambient Occlusion", "Soft Shadows In Corners And Under Things", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 0},
    {"volumetric_dust", "on", "Volumetric Dust", "Real Clouds Of Dust And Exhaust Smoke", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 0},
    {"frame_rate", "display", "Frame Rate", "Frames Between The Game's 60 Ticks A Second", 4, {"60", "120", "144", "Screen"}, {"60", "120", "144", "display"}, 0, 0, 0, 0},
    {"camera_stick", "on", "Right Stick Camera", "The Right Stick Swings The Camera Round The Car", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 0},
    {"streamer_mode", "off", "Streamer Mode", "Hides The Confederate Flag Paint Jobs - From The Next Load", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 0},
    {"baked_lighting", "on", "Ray-Traced Lighting", "Real Shadows And Shade", 2, {"Original", "Baked"}, {"off", "on"}, 0, 0, 0, 2},
    {"night", "original", "Night", "How Dark Night Races Are - With Ray-Traced Lighting", 3, {"Original", "Dark", "Darkest"}, {"original", "dark", "darkest"}, 0, 0, 0, 2},
    {"car_lights", "on", "Car Lights", "Headlights And Taillights Light The Road At Night", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 2},
    {"car_light_shadows", "on", "Car Light Shadows", "Ray-Traced Shadows From Headlights - From The Next Race", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 2},
    {"car_shadows", "on", "Car Shadows", "Cars Cast Shadows From The Sun And Headlights", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 2},
    {"map_lights", "on", "Track Lights", "The Lamps On The Tracks Light Them At Night", 2, {"Off", "On"}, {"off", "on"}, 0, 0, 0, 2},
    {"music_level", "7", "Music", "Music Against Everything Else", 16, {0}, {0}, 0, 0, 0, 1},
    {"engine_level", "10", "Engines", "Every Car's Engine, Yours And The Others", 16, {0}, {0}, 0, 0, 0, 1},
    {"effects_level", "10", "Effects", "Crashes, Skids, Crowds, Announcer And Menus", 16, {0}, {0}, 0, 0, 0, 1},
};
#define NROWS ((int)(sizeof rows / sizeof *rows))

static uint32_t row_id(int i) { return i < 8 ? ROW_FIRST + (uint32_t)i : ROW_MORE + (uint32_t)(i - 8); }
static int row_of(uint32_t id) { return id >= ROW_MORE ? (int)(id - ROW_MORE) + 8 : (int)id - (int)ROW_FIRST; }
/* (its place among its page's rows, ten on the page: closer) */
static float fl_y(int i)
{
    int place = 0, total = 0;
    for (int k = 0; k < NROWS; k++)
        if (rows[k].audio == rows[i].audio) {
            place += k < i;
            total++;
        }
    return 90.0f + (total > 9 ? 29.0f : total > 8 ? 32.0f : total > 7 ? 36.0f : 40.0f) * (float)place;
}
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

static uint32_t help_more, help_back, help_more_audio, help_back_audio, help_more_light, help_back_light, text_light, text_light_title, text_more, text_title, text_back;

METHOD(row_handler, 0)
{
    uint32_t e = c->ecx, d = c->edx, id = R32(c->ecx + 0x28);
    int i = row_of(id);
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

/* More Options and the pages' Backs: a button that leaves one screen for another */
METHOD(button_handler, 0)
{
    uint32_t e = c->ecx, d = c->edx, id = R32(c->ecx + 0x28);
    int more = id == BUTTON_MORE || id == AUDIO_MORE || id == LIGHT_MORE;
    /* from one screen to the other, each way */
    uint32_t from = id == BUTTON_MORE ? VIDEO : id == AUDIO_MORE ? AUDIO : id == LIGHT_MORE ? PAGE : id == PAGE_BACK ? PAGE : id == AUDIO_BACK ? AUDIO_PAGE : LIGHT_PAGE;
    uint32_t to = id == BUTTON_MORE ? PAGE : id == AUDIO_MORE ? AUDIO_PAGE : id == LIGHT_MORE ? LIGHT_PAGE : id == PAGE_BACK ? VIDEO : id == AUDIO_BACK ? AUDIO : PAGE;
    uint32_t help = id == BUTTON_MORE ? help_more : id == AUDIO_MORE ? help_more_audio : id == LIGHT_MORE ? help_more_light : id == PAGE_BACK ? help_back : id == AUDIO_BACK ? help_back_audio : help_back_light;
    switch (R32(c->edx)) {
    case 0:
        c->ecx = id;
        c->edx = id == LIGHT_MORE ? text_light : more ? text_more : text_back;
        guest_call(c, 0x457740, 7, fu(210.0f), fu(id == AUDIO_MORE ? 300.0f : id == LIGHT_MORE ? 378.0f : more ? 375.0f : R32(0x57c1ec) ? RF32(0x57c1ec) : 420.0f), 0u, 0u,
                   R32(0x57c1e4), R32(0x57c1e8), 0u);
        c->ecx = e;
        c->edx = d;
        /* (what Escape and B press on the screen is the button named last to this: only a Back, or they loop, Ant) */
        if (!more)
            fast(c, 0x45ce10, id, 0);
        break;
    case 3:
        fast(c, 0x458cd0, 0x4b, 0);
        fast(c, 0x458cb0, from, 0);
        fast(c, 0x458730, PARENT, to);
        break;
    case 4:
        fast(c, 0x458d00, help, 0);
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
    fast(c, 0x4585d0, LIGHT_MORE, 0);
    for (int i = 0; i < NROWS; i++)
        if (rows[i].audio == 0)
            fast(c, 0x4585d0, row_id(i), 0);
    return 1;
}

METHOD(light_page_shown, 0)
{
    fast(c, 0x4585d0, LIGHT_BACK, 0);
    for (int i = 0; i < NROWS; i++)
        if (rows[i].audio == 2)
            fast(c, 0x4585d0, row_id(i), 0);
    return 1;
}

METHOD(audio_page_shown, 0)
{
    fast(c, 0x4585d0, AUDIO_BACK, 0);
    for (int i = 0; i < NROWS; i++)
        if (rows[i].audio == 1)
            fast(c, 0x4585d0, row_id(i), 0);
    return 1;
}

METHOD(audio_shown, 0)
{
    fast(c, 0x4585d0, AUDIO_MORE, 0);
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

static void page_made(CPU *c, uint32_t page, uint32_t back, int audio, uint32_t title, uint32_t shown)
{
    fast(c, 0x458c60, 0x119, title);
    fast(c, 0x458730, page, back);
    for (int i = 0; i < NROWS; i++)
        if (rows[i].audio == audio)
            fast(c, 0x458730, page, row_id(i));
    for (int i = 0; i < NROWS; i++)
        if (rows[i].audio == audio)
            fast(c, 0x4588d0, row_id(i), 0);
    fast(c, 0x4588d0, back, 0);
    fast(c, 0x458810, back, 0);
    uint32_t e = c->ecx;
    when_shown(c, shown);
    c->ecx = e;
}

METHOD(page_handler, 0)
{
    if (R32(c->edx) == 0) {
        page_made(c, PAGE, PAGE_BACK, 0, text_title, thunk_addr(page_shown, "more options shown"));
        fast(c, 0x458730, PAGE, LIGHT_MORE);
        fast(c, 0x4588d0, LIGHT_MORE, 0);
    }
    return 1;
}

METHOD(light_page_handler, 0)
{
    if (R32(c->edx) == 0)
        page_made(c, LIGHT_PAGE, LIGHT_BACK, 2, text_light_title, thunk_addr(light_page_shown, "lighting shown"));
    return 1;
}

METHOD(audio_page_handler, 0)
{
    if (R32(c->edx) == 0)
        page_made(c, AUDIO_PAGE, AUDIO_BACK, 1, text_title, thunk_addr(audio_page_shown, "audio options shown"));
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
    help_more_audio = gstrdup("Music, Engines And Effects");
    help_back_audio = gstrdup("Return To The Audio Options");
    text_light = gstrdup("Lighting");
    text_light_title = gstrdup("LIGHTING");
    help_more_light = gstrdup("Ray-Traced Lighting, Nights And Lights");
    help_back_light = gstrdup("Return To More Options");
    for (int i = 0; i < NROWS; i++) {
        Row *r = &rows[i];
        r->items = galloc((uint32_t)r->n * 8);
        for (int k = 0; k < r->n; k++) {
            char num[8];
            snprintf(num, sizeof num, "%d", k);
            W32(r->items + 8 * (uint32_t)k, gstrdup(r->audio == 1 ? num : r->text[k]));
            W32(r->items + 8 * (uint32_t)k + 4, (uint32_t)k);
        }
        r->cell = galloc(4);
        r->shown = r->audio == 1 ? atoi(option_str(r->key, r->default_value)) : row_value(r);
        W32(r->cell, (uint32_t)r->shown);
        new_widget(c, table, row_id(i), thunk_addr(row_handler, "more options row"));
    }
    uint32_t button = thunk_addr(button_handler, "more options button");
    new_widget(c, table, BUTTON_MORE, button);
    new_widget(c, table, PAGE_BACK, button);
    uint32_t icon = new_icon(c, PAGE), mem = guest_call(c, G_NEW, 1, 0xa4u);
    c->ecx = mem;
    uint32_t page = guest_call(c, 0x46a4d0, 7, 0u, 0u, 0u, 0u, thunk_addr(page_handler, "more options page"), icon, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, page, PAGE);
    new_widget(c, table, AUDIO_MORE, button);
    new_widget(c, table, AUDIO_BACK, button);
    icon = new_icon(c, AUDIO_PAGE);
    mem = guest_call(c, G_NEW, 1, 0xa4u);
    c->ecx = mem;
    page = guest_call(c, 0x46a4d0, 7, 0u, 0u, 0u, 0u, thunk_addr(audio_page_handler, "audio options page"), icon, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, page, AUDIO_PAGE);
    new_widget(c, table, LIGHT_MORE, button);
    new_widget(c, table, LIGHT_BACK, button);
    icon = new_icon(c, LIGHT_PAGE);
    mem = guest_call(c, G_NEW, 1, 0xa4u);
    c->ecx = mem;
    page = guest_call(c, 0x46a4d0, 7, 0u, 0u, 0u, 0u, thunk_addr(light_page_handler, "lighting page"), icon, 0u);
    c->ecx = table;
    guest_call(c, 0x470240, 2, page, LIGHT_PAGE);
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

static int audio_screen_hook(CPU *c)
{
    int made = R32(c->edx) == 0;
    F_0043da20_orig(c);
    if (made) {
        fast(c, 0x458730, AUDIO, AUDIO_MORE);
        fast(c, 0x4588d0, AUDIO_MORE, 0);
        uint32_t e = c->ecx;
        when_shown(c, thunk_addr(audio_shown, "audio shown"));
        c->ecx = e;
    }
    return 1;
}

/* The game's table of widgets has 0x190 ids and a count after them, and no free ids left for more than 11 widgets. Anything else
   finds a widget through the table's address and its id, unchecked [verified: 0x470240 and its readers], so a larger block
   does: the constructor is given one. */
static int table_new_hook(CPU *c)
{
    uint32_t mem = guest_call(c, G_NEW, 1, 0x1000u);
    memset(gptr(mem), 0, 0x1000);
    c->ecx = mem;
    F_004701f0_orig(c);
    c->eax = mem;
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
        char num[8];
        snprintf(num, sizeof num, "%d", r->shown);
        if (r->shown >= 0 && r->shown < r->n)
            option_set(r->key, r->audio == 1 ? num : r->value[r->shown]);
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
    hook_audio_screen = audio_screen_hook;
    hook_table_new = table_new_hook;
}
