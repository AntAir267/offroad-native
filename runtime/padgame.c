/* What the port does with controllers in Off-Road (pad.h).

   The game keeps its bindings per device name in Configs\p1controll.cfg, as DirectInput's instance
   name, "Controller (XBOX 360 For Windows) (Port 0)" here: 620 bytes, a float version, device count,
   the active one, then per device its name (100 bytes), 4 flag bytes and 10 action names of 20 bytes
   (steering, steering, accelerate, brake, handbrake, rear view, camera, gear up, gear down, nitrous)
   [verified: 0x461a10, 0x461b10, 0x461180]. So every controller is shown under that name, and the
   first start's copy of the game's file gets the triggers (JOYSTICK_YNEG / YPOS, the two together on
   DirectInput's Y, pad.c) and the bumpers for the gears. The game's Controller Config screen changes
   them from then on. The front end reads the controller itself: FESelect on buttons 1-8, the pointer
   on the stick [verified: 0x461f00]. What it lacks is here: Start is Escape (the race's menu
   "bind InGameMenu KEY_ESCAPE", 0x460970), and any of A, B, Start skips a movie. */
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "pad.h"
#include "input.h"

#define PAD_DEVICE "Controller (XBOX 360 For Windows)"

const char *padgame_device_name(const char *name) { (void)name; return PAD_DEVICE; }
int ifc_rumble_wanted(void) { return 1; }

static void write_bindings(void)
{
    char in[1200], out[1200];
    FILE *f;
    snprintf(out, sizeof out, "%s/configs/p1controll.cfg", user_dir);
    if ((f = fopen(out, "rb"))) {
        fclose(f);
        return;
    }
    if (!path_to_host("Configs\\p1controll.cfg", in, sizeof in, 0) || !(f = fopen(in, "rb")))
        return;
    uint8_t d[620];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n != sizeof d)
        return;
    for (size_t o = 12; o + 100 + 4 + 200 <= n; o += 100 + 4 + 200) {
        if (strncmp((char *)d + o, PAD_DEVICE, sizeof PAD_DEVICE - 1))
            continue;
        static const struct { int action; const char *name; } set[] = {
            {2, "JOYSTICK_YNEG"}, {3, "JOYSTICK_YPOS"}, {7, "GAMEPAD_6"}, {8, "GAMEPAD_5"}};
        for (size_t k = 0; k < sizeof set / sizeof *set; k++) {
            char *a = (char *)d + o + 104 + set[k].action * 20;
            memset(a, 0, 20);
            strcpy(a, set[k].name);
        }
        path_to_host("Configs\\p1controll.cfg", out, sizeof out, 1);
        if ((f = fopen(out, "wb"))) {
            fwrite(d, 1, sizeof d, f);
            fclose(f);
            LOG(LOG_ALWAYS, "controller bindings: triggers and bumpers, in %s", out);
        }
    }
}

void padgame_frame(unsigned long frame)
{
    (void)frame;
    extern void mss_frame(void);
    extern void win_key(int dik, int down);
    extern int bink_active(void);
    mss_frame();                        /* 3D gains follow the listener */
    static int esc;
    int want = pad_count() && (pad_held(PAD_START) ||
                               (bink_active() && (pad_held(PAD_A) || pad_held(PAD_B))));
    if (want != esc) {
        esc = want;
        win_key(0x01, want);            /* DIK_ESCAPE */
    }
}

void padgame_init(void) { write_bindings(); }
