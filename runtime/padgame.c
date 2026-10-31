/* The controller in Off-Road (pad.h): its bindings, what the front end does with it, and its names and
   prompts on screen.

   The game keeps its bindings per device name in Configs\p1controll.cfg, as DirectInput's instance name,
   "Controller (XBOX 360 For Windows) (Port 0)" here: 620 bytes, a float version, device count, the active
   one, then per device its name (100 bytes), 4 flag bytes and 10 action names of 20 bytes (steer left,
   steer right, accelerate, brake, handbrake, rear view, camera, gear up, gear down, nitrous)
   [verified: 0x461a10, 0x461b10, 0x461180]. So the game's joysticks (pad.c: any controller, always there) are shown
   under that name, and the first start writes the user folder's copies with them active and an Xbox layout.
   The game's own controller screen changes it from then on.

   The front end reads the controller itself: FESelect on buttons 1-8, LookRight_Left and LookUp_Down
   on the left stick (0x461f00, once the player's device is a controller) [verified: decompile]. bind_hook
   leaves A selecting and makes B FEEscape, and pad frames add the D-pad as the arrow keys and Start as Escape
   (the race's menu is "bind InGameMenu KEY_ESCAPE", 0x460970); A, B, Start skip a movie.

   Names and prompts: every line of text goes through 0x4b5540. The controller screen shows an input's name
   (GAMEPAD_6: the font has no underscore), and the buttons say what they do; while a controller is the device
   used last, text_hook swaps those strings for ones with button icons. A text is made when its screen
   is [guess: retained, 0x4b7690], so a screen keeps the prompts it opened with until it is opened again.

   The icons are characters of the game's own fonts, so they line up, fade and move with the text. Text is
   drawn in one batch per font texture (0x4b68d0 only adds vertices) [verified: disassembly], so they
   must be in the font's image. The font loader (0x4b7bc0) takes "<font>_512.tga" if it exists, with the same
   texture coordinates as the 256 one; the first start writes those in the user folder for Impact_italic_med
   and _small, every language: the game's image doubled, the icons in the free rows below it
   (runtime/pad_icons.h, tools/make_pad_icons.py; below 195 of 256 rows are free in med, 114 in small
   [verified: their .dat]). font_chars_hook adds the characters 0x10.. when a font's .dat is read: records as
   0x4b7a30 makes them (+0 advance, +4 code, +0xc texture coordinates from its +8). glyph_draw_hook gives
   those white, with twice the text's alpha, since they would otherwise be tinted and dimmed like the text. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "runtime.h"
#include "pad.h"
#include "input.h"
#include "funcs.h"
#include "pad_icons.h"
#include <zlib.h>
#include <sys/stat.h>

#define PAD_DEVICE "Controller (XBOX 360 For Windows)"
#define DEVICE_SIZE (100 + 4 + 200)

const char *padgame_device_name(const char *name) { (void)name; return PAD_DEVICE; }
int ifc_rumble_wanted(void) { return 1; }

static void pad_block(uint8_t *b, int port)
{
    static const char *const actions[10] = {"X-axis", "X-axis", "JOYSTICK_ZNEG", "JOYSTICK_ZPOS", "GAMEPAD_1",
                                            "GAMEPAD_3", "GAMEPAD_4", "GAMEPAD_6", "GAMEPAD_5", "GAMEPAD_2"};
    memset(b, 0, DEVICE_SIZE);
    snprintf((char *)b, 100, PAD_DEVICE " (Port %d)", port);
    static const uint8_t flags[4] = {1, 1, 1, 1};    /* the second is Force Feedback [verified: its screen]; the rest as Windows wrote them */
    memcpy(b + 100, flags, 4);
    for (int i = 0; i < 10; i++)
        strcpy((char *)b + 104 + i * 20, actions[i]);
}

/* the user folder's p1controll.cfg and p2controll.cfg, once: the game's keyboard entry, then the two
   joysticks (pad.c), player 1 on the first and player 2 on the second. The active device's number less one
   is the joystick the game binds to (0x460970). */
static void write_bindings(void)
{
    for (int player = 1; player <= 2; player++) {
        char path[1200], win[64];
        FILE *f;
        snprintf(path, sizeof path, "%s/configs/p%dcontroll.cfg", user_dir, player);
        if ((f = fopen(path, "rb"))) {
            fclose(f);
            continue;
        }
        snprintf(win, sizeof win, "Configs\\p%dcontroll.cfg", player);
        uint8_t d[12 + 3 * DEVICE_SIZE];
        size_t n = 0;
        if (path_to_host(win, path, sizeof path, 0) && (f = fopen(path, "rb"))) {
            n = fread(d, 1, sizeof d, f);
            fclose(f);
        }
        if (n < 12 + DEVICE_SIZE || strncasecmp((char *)d + 12, "keyboard", 8))
            continue;                   /* the game then writes its own, keyboard first */
        uint32_t count = 3, active = (uint32_t)player;
        memcpy(d + 4, &count, 4);
        memcpy(d + 8, &active, 4);
        pad_block(d + 12 + DEVICE_SIZE, 0);
        pad_block(d + 12 + 2 * DEVICE_SIZE, 1);
        path_to_host(win, path, sizeof path, 1);
        if ((f = fopen(path, "wb"))) {
            fwrite(d, 1, sizeof d, f);
            fclose(f);
            LOG(LOG_ALWAYS, "controller bindings written in %s", path);
        }
    }
}

static uint32_t guest(const char *s) { return gstrdup(s); }

/* The race's controls: player 1 is bound to one device (0x460970), the controller. The keyboard's keys are
   bound as well, so it drives too, and whether it is a two-player race tells pad.c who the second
   controller belongs to. */
static int race_controls_hook(CPU *c)
{
    uint32_t game = c->ecx, keep = c->ecx;
    F_00460970_orig(c);
    pad_two_players(R32(game + 0x4ec) == 2);
    uint32_t player = R32(game + 0x10bc);
    if (R32(player + 0x10) >= 1) {
        static uint32_t buf;
        if (!buf)
            buf = galloc(256);
        uint32_t keyboard = R32(player + 4);     /* the list's first */
        for (uint32_t i = 0; i < 10; i++) {
            c->ecx = keyboard;
            if (guest_call(c, 0x461180, 4, i, buf, 0u, R32(player)) & 0xff) {
                c->ecx = R32(0x5d1680);
                guest_call(c, 0x402640, 1, buf);
            }
        }
    }
    c->ecx = keep;
    return 1;
}

static int front_end_hook(CPU *c)
{
    (void)c;
    pad_two_players(0);
    return 0;
}

static int bind_hook(CPU *c)
{
    static uint32_t fe_escape, unused;
    if (!fe_escape) {
        fe_escape = guest("FEEscape");
        unused = guest("GAMEPAD_12");   /* (no such button on the joystick) */
    }
    const char *action = gstr(R32(c->esp + 4)), *input = gstr(R32(c->esp + 8));
    if (action && input && !strcmp(action, "FESelect") && !strncmp(input, "GAMEPAD_", 8)) {
        int b = atoi(input + 8);
        if (b == 2)
            W32(c->esp + 4, fe_escape);
        else if (b > 2)
            W32(c->esp + 8, unused);
    }
    return 0;
}

/* the characters: in the order of tools/make_pad_icons.py */
#define ICON_FIRST 0x10
#define ICON_A "\x10"
#define ICON_B "\x11"
#define ICON_X "\x12"
#define ICON_Y "\x13"
#define ICON_LB "\x14"
#define ICON_RB "\x15"
#define ICON_LT "\x16"
#define ICON_RT "\x17"
#define ICON_BACK "\x18"
#define ICON_START "\x19"
#define ICON_LS "\x1a"
#define ICON_RS "\x1b"
#define ICON_GUIDE "\x1c"
#define ATLAS 512
#define ICON_TOP 396
#define ICON_PITCH (PAD_ICON_CELL + 2)
#define ICON_ROW (ATLAS / ICON_PITCH)

static void icon_cell(int i, int *x, int *y)
{
    *x = 1 + (i % ICON_ROW) * ICON_PITCH;
    *y = ICON_TOP + (i / ICON_ROW) * ICON_PITCH;
}

static int icons_wanted(void) { return !deterministic || pad_count() > 0; }

/* <font>_512.tga for each Impact_italic_med and _small in Main.mng that has none yet */
static void write_font_images(void)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/Main.mng", game_dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    uint32_t head[2], listed;
    uint8_t *dir = malloc(1 << 21), *icons = malloc(PAD_ICON_COUNT * PAD_ICON_CELL * PAD_ICON_CELL * 4);
    uLongf icons_size = PAD_ICON_COUNT * PAD_ICON_CELL * PAD_ICON_CELL * 4;
    size_t n = fread(head, 4, 2, f) == 2 ? fread(dir, 1, 1 << 21, f) : 0;
    if (!n || uncompress(icons, &icons_size, pad_icons_z, sizeof pad_icons_z) != Z_OK)
        goto done;
    listed = head[1];
    size_t p = 0;
    for (uint32_t k = 0; k < listed && p < n; k++) {
        const char *name = (const char *)dir + p;
        p += strlen(name) + 1;
        uint32_t e[3];
        memcpy(e, dir + p, 12);
        p += 12;
        const char *us = strstr(name, "_256.tga");
        if (!us || strncasecmp(name, "Fonts\\", 6) || (!strstr(name, "Impact_italic_med") && !strstr(name, "Impact_italic_small")))
            continue;
        char rel[256], out[1400];
        snprintf(rel, sizeof rel, "%.*s_512.tga", (int)(us - name), name);
        for (char *q = rel; *q; q++)
            if (*q == '\\') *q = '/';
        snprintf(out, sizeof out, "%s/%s", user_dir, rel);
        FILE *o = fopen(out, "rb");
        if (o) {
            fclose(o);
            continue;
        }
        uint8_t *z = malloc(e[0]), *t = malloc(e[1]), *a = malloc(18 + ATLAS * ATLAS * 4);
        uLongf tsize = e[1];
        fseek(f, (long)e[2], SEEK_SET);
        if (fread(z, 1, e[0], f) == e[0] && uncompress(t, &tsize, z, e[0]) == Z_OK && tsize >= 18 + 256 * 256 * 4 &&
            t[2] == 2 && t[16] == 32 && t[12] == 0 && t[13] == 1 && t[14] == 0 && t[15] == 1) {
            const uint8_t *src = t + 18 + t[0];
            int top_down = (t[17] & 0x20) != 0;
            memcpy(a, t, 18);
            a[0] = 0;
            a[13] = a[15] = ATLAS >> 8;
            a[12] = a[14] = ATLAS & 255;
            uint8_t *px = a + 18;
            for (int y = 0; y < ATLAS; y++)
                for (int x = 0; x < ATLAS; x++)
                    memcpy(px + 4 * (y * ATLAS + x), src + 4 * ((y / 2) * 256 + x / 2), 4);
            for (int i = 0; i < PAD_ICON_COUNT; i++) {
                int cx, cy;
                icon_cell(i, &cx, &cy);
                const uint8_t *cell = icons + i * PAD_ICON_CELL * PAD_ICON_CELL * 4;
                for (int y = 0; y < PAD_ICON_CELL; y++)
                    for (int x = 0; x < PAD_ICON_CELL; x++) {
                        int row = top_down ? cy + y : ATLAS - 1 - (cy + y);
                        const uint8_t *s = cell + 4 * (y * PAD_ICON_CELL + x);
                        uint8_t *d = px + 4 * (row * ATLAS + cx + x);
                        d[0] = s[2];    /* BGRA */
                        d[1] = s[1];
                        d[2] = s[0];
                        d[3] = s[3];
                    }
            }
            /* (the folder, one level below Fonts) */
            char folder[1400];
            snprintf(folder, sizeof folder, "%s/Fonts", user_dir);
            mkdir(folder, 0755);
            snprintf(folder, sizeof folder, "%s", out);
            *strrchr(folder, '/') = 0;
            mkdir(folder, 0755);
            if ((o = fopen(out, "wb"))) {
                fwrite(a, 1, 18 + ATLAS * ATLAS * 4, o);
                fclose(o);
                LOG(LOG_ALWAYS, "button icons added to %s", rel);
            }
        }
        free(z);
        free(t);
        free(a);
    }
done:
    free(dir);
    free(icons);
    fclose(f);
}

#define G_NEW 0x56cd7au                 /* operator new */

static int font_chars_hook(CPU *c)
{
    uint32_t font = c->ecx, keep_ecx = c->ecx;
    const char *path = gstr(R32(c->esp + 4));
    int wanted = path && (strstr(path, "Impact_italic_med") || strstr(path, "Impact_italic_small"));
    F_004b7a30_orig(c);
    if (!wanted)
        return 1;
    float size = RF32(font);
    union { float f; uint32_t u; } adv = {size};
    for (int i = 0; i < PAD_ICON_COUNT; i++) {
        uint32_t rec = guest_call(c, G_NEW, 1, 0x14u), uv = guest_call(c, G_NEW, 1, 0x28u);
        W32(rec, adv.u);
        W8(rec + 4, (uint8_t)(ICON_FIRST + i));
        W32(rec + 8, 0);
        W32(rec + 0xc, uv);
        W32(rec + 0x10, 0);
        int x, y;
        icon_cell(i, &x, &y);
        float u0 = (float)x / ATLAS, v0 = (float)y / ATLAS, u1 = (float)(x + PAD_ICON_CELL) / ATLAS, v1 = (float)(y + PAD_ICON_CELL) / ATLAS;
        const float t[8] = {u0, v0, u1, v0, u0, v1, u1, v1};
        for (int k = 0; k < 8; k++) {
            union { float f; uint32_t u; } w = {t[k]};
            W32(uv + 8 + 4 * k, w.u);
        }
        W32(font + 0x18 + 4 * (ICON_FIRST + i), rec);
    }
    c->ecx = keep_ecx;
    return 1;
}

static int glyph_draw_hook(CPU *c)
{
    uint8_t code = R8(R32(c->ecx + 4) + 4);
    if (code >= ICON_FIRST && code < ICON_FIRST + PAD_ICON_COUNT) {
        uint32_t a = R32(c->esp + 8) >> 24;   /* (unselected text is half see-through: not the icons) */
        a = a * 2 > 255 ? 255 : a * 2;
        W32(c->esp + 8, a << 24 | 0xffffffu);
    }
    return 0;
}

static int text_hook(CPU *c)
{
    static const char *const swaps[][2] = {
        {"GAMEPAD_1", ICON_A}, {"GAMEPAD_2", ICON_B}, {"GAMEPAD_3", ICON_X}, {"GAMEPAD_4", ICON_Y},
        {"GAMEPAD_5", ICON_LB}, {"GAMEPAD_6", ICON_RB}, {"GAMEPAD_7", ICON_BACK}, {"GAMEPAD_8", ICON_START},
        {"GAMEPAD_9", ICON_LS}, {"GAMEPAD_10", ICON_RS}, {"GAMEPAD_11", ICON_GUIDE},
        {"JOYSTICK_1", ICON_A}, {"JOYSTICK_2", ICON_B}, {"JOYSTICK_3", ICON_X}, {"JOYSTICK_4", ICON_Y},
        {"JOYSTICK_5", ICON_LB}, {"JOYSTICK_6", ICON_RB}, {"JOYSTICK_7", ICON_BACK}, {"JOYSTICK_8", ICON_START},
        {"JOYSTICK_ZNEG", ICON_RT}, {"JOYSTICK_ZPOS", ICON_LT},
        {"Z axis -", ICON_RT}, {"Z axis +", ICON_LT}, {"X-axis", ICON_LS},
        {"Continue", ICON_A " Continue"}, {"Done", ICON_A " Done"}, {"Accept", ICON_A " Accept"}, {"Race", ICON_A " Race"},
        {"Back", ICON_B " Back"}, {"ESC TO EXIT", ICON_B " TO EXIT"}, {"DEMO MODE - ESC TO EXIT", "DEMO MODE - " ICON_B " TO EXIT"},
    };
    static uint32_t guest_swaps[sizeof swaps / sizeof *swaps];
    if (!pad_prompts())
        return 0;
    const char *t = gstr(R32(c->esp + 0xc));
    if (!t)
        return 0;
    for (size_t i = 0; i < sizeof swaps / sizeof *swaps; i++)
        if (!strcmp(t, swaps[i][0])) {
            if (!guest_swaps[i])
                guest_swaps[i] = guest(swaps[i][1]);
            W32(c->esp + 0xc, guest_swaps[i]);
            break;
        }
    return 0;
}

void padgame_frame(unsigned long frame)
{
    (void)frame;
    extern void mss_frame(void);
    extern void win_key(int dik, int down);
    extern int bink_active(void);
    mss_frame();                        /* 3D gains follow the listener */
    extern void menu_frame(void);
    menu_frame();
    static const struct { int dik, button; } arrows[4] = {{0xC8, PAD_DUP}, {0xD0, PAD_DDOWN}, {0xCB, PAD_DLEFT},
                                                          {0xCD, PAD_DRIGHT}};
    static int down[4], esc;
    int n = pad_count();
    for (int i = 0; i < 4; i++) {
        int want = n && pad_held(arrows[i].button);
        if (want != down[i]) {
            down[i] = want;
            win_key(arrows[i].dik, want);
            input_used(1);          /* (the key came from the controller) */
        }
    }
    int want = n && (pad_held(PAD_START) || (bink_active() && (pad_held(PAD_A) || pad_held(PAD_B))));
    if (want != esc) {
        esc = want;
        win_key(0x01, want);            /* DIK_ESCAPE */
        input_used(1);
    }
}

void padgame_init(void)
{
    write_bindings();
    hook_bind = bind_hook;
    hook_text = text_hook;
    hook_race_controls = race_controls_hook;
    hook_front_end = front_end_hook;
    if (icons_wanted()) {
        write_font_images();
        hook_font_chars = font_chars_hook;
        hook_glyph_draw = glyph_draw_hook;
    }
}
