/* What the port does with controllers in Off-Road (pad.h). Menu keys and the game's control
   profiles come once the game's input code has been read. */
#include "runtime.h"
#include "pad.h"

/* The game keeps its bindings per device name (Configs\p1controll.cfg: "Controller (XBOX 360 For
   Windows) (Port 0)"): every controller is shown under the name Windows gives an Xbox one. */
const char *padgame_device_name(const char *name) { (void)name; return "Controller (XBOX 360 For Windows)"; }
int ifc_rumble_wanted(void) { return 1; }
void padgame_frame(unsigned long frame)
{
    (void)frame;
    extern void mss_frame(void);
    mss_frame();                        /* 3D gains follow the listener */
}
void padgame_init(void) {}
