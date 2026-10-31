/* The controller in Off-Road (pad.h): its bindings, what the front end does with it, and its names and
   prompts on screen.

   The game keeps its bindings per device name in Configs\p1controll.cfg, as DirectInput's instance name,
   "Controller (XBOX 360 For Windows) (Port 0)" here: 620 bytes, a float version, device count, the active
   one, then per device its name (100 bytes), 4 flag bytes and 10 action names of 20 bytes (steer left,
   steer right, accelerate, brake, handbrake, rear view, camera, gear up, gear down, nitrous)
   [verified: 0x461a10, 0x461b10, 0x461180]. So every controller is shown under that name, and the first
   start writes the user folder's copy with the controller active and an Xbox layout. The game's own
   controller screen changes it from then on.

   The front end reads the controller itself: FESelect on buttons 1-8, LookRight_Left and LookUp_Down
   on the left stick (0x461f00, once the player's device is a controller) [verified: decompile]. bind_hook
   leaves A selecting and makes B FEEscape, and pad frames add the D-pad as the arrow keys and Start as Escape
   (the race's menu is "bind InGameMenu KEY_ESCAPE", 0x460970); A, B, Start skip a movie.

   Names and prompts: every line of text goes through 0x4b5540. The controller screen shows an input's name
   (GAMEPAD_6: the font has no underscore), and the buttons say what they do; while a controller
   is the device used last, text_hook swaps those strings for the Xbox ones, in the game's own font. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "runtime.h"
#include "pad.h"
#include "input.h"
#include "funcs.h"

#define PAD_DEVICE "Controller (XBOX 360 For Windows)"
#define PAD_ENTRY PAD_DEVICE " (Port 0)"
#define DEVICE_SIZE (100 + 4 + 200)

const char *padgame_device_name(const char *name) { (void)name; return PAD_DEVICE; }
int ifc_rumble_wanted(void) { return 1; }

static void pad_block(uint8_t *b)
{
    static const char *const actions[10] = {"X-axis", "X-axis", "JOYSTICK_ZNEG", "JOYSTICK_ZPOS", "GAMEPAD_1",
                                            "GAMEPAD_3", "GAMEPAD_4", "GAMEPAD_6", "GAMEPAD_5", "GAMEPAD_2"};
    memset(b, 0, DEVICE_SIZE);
    strcpy((char *)b, PAD_ENTRY);
    static const uint8_t flags[4] = {1, 1, 1, 1};    /* the second is Force Feedback [verified: its screen]; the rest as Windows wrote them */
    memcpy(b + 100, flags, 4);
    for (int i = 0; i < 10; i++)
        strcpy((char *)b + 104 + i * 20, actions[i]);
}

/* the user folder's Configs\p1controll.cfg, once: the game's keyboard entry, the controller active */
static void write_bindings(void)
{
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/configs/p1controll.cfg", user_dir);
    if ((f = fopen(path, "rb"))) {
        fclose(f);
        return;
    }
    uint8_t d[12 + 2 * DEVICE_SIZE];
    size_t n = 0;
    if (path_to_host("Configs\\p1controll.cfg", path, sizeof path, 0) && (f = fopen(path, "rb"))) {
        n = fread(d, 1, sizeof d, f);
        fclose(f);
    }
    if (n < 12 + DEVICE_SIZE || strncasecmp((char *)d + 12, "keyboard", 8))
        return;                         /* the game then writes its own, keyboard first */
    uint32_t count = 2, active = 1;
    memcpy(d + 4, &count, 4);
    memcpy(d + 8, &active, 4);
    pad_block(d + 12 + DEVICE_SIZE);
    path_to_host("Configs\\p1controll.cfg", path, sizeof path, 1);
    if ((f = fopen(path, "wb"))) {
        fwrite(d, 1, sizeof d, f);
        fclose(f);
        LOG(LOG_ALWAYS, "controller bindings written in %s", path);
    }
}

static uint32_t guest(const char *s) { return gstrdup(s); }

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

static int text_hook(CPU *c)
{
    static const char *const swaps[][2] = {
        {"GAMEPAD_1", "A"}, {"GAMEPAD_2", "B"}, {"GAMEPAD_3", "X"}, {"GAMEPAD_4", "Y"}, {"GAMEPAD_5", "LB"},
        {"GAMEPAD_6", "RB"}, {"GAMEPAD_7", "BACK"}, {"GAMEPAD_8", "START"}, {"GAMEPAD_9", "LEFT STICK BUTTON"},
        {"GAMEPAD_10", "RIGHT STICK BUTTON"}, {"GAMEPAD_11", "GUIDE"},
        {"JOYSTICK_1", "A"}, {"JOYSTICK_2", "B"}, {"JOYSTICK_3", "X"}, {"JOYSTICK_4", "Y"}, {"JOYSTICK_5", "LB"},
        {"JOYSTICK_6", "RB"}, {"JOYSTICK_7", "BACK"}, {"JOYSTICK_8", "START"},
        {"JOYSTICK_ZNEG", "RT"}, {"JOYSTICK_ZPOS", "LT"},
        {"Z axis -", "RT"}, {"Z axis +", "LT"}, {"X-axis", "LEFT STICK"},
        {"Continue", "(A) Continue"}, {"Done", "(A) Done"}, {"Accept", "(A) Accept"}, {"Race", "(A) Race"},
        {"Back", "(B) Back"}, {"ESC TO EXIT", "B TO EXIT"}, {"DEMO MODE - ESC TO EXIT", "DEMO MODE - B TO EXIT"},
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
}
