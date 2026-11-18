/*
 * launcher.h -- the window the game opens with, and its settings file.
 *
 * Start game / Settings / Exit. The settings (disc image, save folder,
 * resolution, aspect ratio, fullscreen) live in an .ini file beside the
 * executable, named after it ("SSX Tricky.ini").
 */
#ifndef SSX_LAUNCHER_H
#define SSX_LAUNCHER_H

#include <windows.h>
#include <stdint.h>

#include "controls.h"

typedef struct LauncherConfig {
    char iso[MAX_PATH];   /* disc image; empty until the player chooses one */
    char hdd[MAX_PATH];   /* emulated hard disk folder: relative to the exe, or absolute */
    int  width, height;   /* render resolution */
    int  widescreen;      /* 0 = 4:3, 1 = 16:9 (the game's own widescreen mode) */
    int  fullscreen;      /* borderless; Alt+Enter toggles in game */
    int  aniso;           /* 0 = the game's own texture filtering, or 2/4/8/16 */
    int  msaa;            /* anti-aliasing samples: 1 (off), 2, 4, 8 */
    int  show_fps;        /* frame rate in the title bar */
    int  log_file;        /* write "<exe name>.log" for bug reports */
    ControlMap controls;  /* keyboard and controller bindings */
} LauncherConfig;

/* The XBE entry point this executable was recompiled from. A disc image is
 * accepted only if its default.xbe has the same one: the translated code is
 * for exactly one build, and any other would crash rather than run. */
void launcher_init(uint32_t expected_entry_point);

/* Per-monitor DPI awareness, so windows are sized in real pixels. Call once,
 * before any window is created. */
void launcher_enable_dpi_awareness(void);

void launcher_config_path(char *out, size_t out_sz);
void launcher_log_path(char *out, size_t out_sz);     /* "<exe name>.log" */
/* Resolve the save folder setting to an absolute path. */
void launcher_hdd_path(const LauncherConfig *cfg, char *out, size_t out_sz);
/* Open a folder (created if missing) or a file in Explorer / its program. */
void launcher_open_path(HWND owner, const char *path, BOOL folder);
void launcher_config_load(LauncherConfig *cfg);     /* defaults when missing */
BOOL launcher_config_save(const LauncherConfig *cfg);

/* TRUE if `path` is a disc image of the build this executable runs. On
 * failure `why` holds one sentence for the player. */
BOOL launcher_check_iso(const char *path, char *why, size_t why_sz);

/* Show the launcher. TRUE when the player chose Start game (cfg then holds
 * the saved settings); FALSE when they closed it. */
BOOL launcher_run(LauncherConfig *cfg);

#endif /* SSX_LAUNCHER_H */
