/*
 * hostui.h -- the game window's menu bar (part 183). See hostui.c.
 */
#ifndef SSX_HOSTUI_H
#define SSX_HOSTUI_H

#include "launcher.h"

/* Install the menu before the title creates its device. `cfg` stays the live
 * settings; changes are written back to the .ini when `persist_changes`. */
void hostui_install(LauncherConfig *cfg, BOOL persist_changes);

#endif /* SSX_HOSTUI_H */
