/* What the port does with controllers in Off-Road (pad.h). Menu keys and the game's control
   profiles come once the game's input code has been read. */
#include "runtime.h"
#include "pad.h"

const char *padgame_device_name(const char *name) { return name; }
int ifc_rumble_wanted(void) { return 1; }
void padgame_frame(unsigned long frame) { (void)frame; }
void padgame_init(void) {}
