# RE notes: `OptionsMenu` — the multi-tab settings screen

Picked as a fresh direction (2026-07-22), continuing straight from the
`ProfileEditor`/`BoardSelectScreen` work. `OptionsMenu` only had 2 functions
named (`OptionsMenu_CacheDisplaySettingsFromWidgets`/
`HandleDisplaySettingWidgetEvent`), both found as byproducts of the
`FEStateMCOverlay` investigation in `RE_NOTES_player_snapshot_system.md`, with
no dedicated file despite clearly being a substantial multi-tab settings
screen.

## The tab-switch master handler

**`OptionsMenu_HandleTabSwitchEvent`** (was `FUN_0009eb80`) — manages up to 5
settings tabs (`this+0x1c` = current tab index, `-1` = none; a
`this+0x1e+tab*3`-shaped per-tab record array). On a specific "apply and
switch" event code, calls the *current* tab's own "cache settings from
widgets" function before switching away from it:

| Tab | Cache function | Settings globals |
|---|---|---|
| 0 | `OptionsMenu_CacheAudioSettingsFromWidgets` | `DAT_001dd814`/`818`/`81c`/`824`/`828`/`830` |
| 1 | `OptionsMenu_CacheControlSettingsFromWidgets` | `DAT_001dd834`/`838`/`848`/`84c` |
| 2 | `OptionsMenu_CacheDisplaySettingsFromWidgets` (pre-existing) | `DAT_001dd83c`/`860`/`878`/`874` |
| 3 | *(none)* | — |
| 4 | *(none — no-op case)* | — |

Tab 3 has no cache step at all, reached via a distinct code path (2 plain
vtable calls) rather than the main switch — suggesting it's a tab with no
persistent settings (e.g. a controller-test/calibration panel) rather than a
4th settings category.

## The 3 tabs' settings, characterized

- **Tab 0 — Audio** (`OptionsMenu_CacheAudioSettingsFromWidgets`, was
  `FUN_0009de50`): reads 7 widget children (types 1-7) and pushes each value
  through 7 consecutive vtable slots on an audio-system-shaped object
  (`*(DAT_001e3c7c+0x730)+0x9c`) — reads as sliders/toggles for volume-shaped
  settings, caching each into a dedicated global, plus a final boolean toggle
  (widget 7, likely mono/stereo or surround-sound).
- **Tab 1 — Controls** (`OptionsMenu_CacheControlSettingsFromWidgets`, was
  `FUN_0009e050`): reads 4 widget children into 4 boolean globals, paired by
  comparison value (widgets 0/1 both check `==5`, widgets 2/3 both check
  `==7`) — reads as 2 related toggle-pairs, plausibly per-player controller
  options (e.g. rumble or axis-invert for player 1 vs. player 2).
- **Tab 2 — Display** (`OptionsMenu_CacheDisplaySettingsFromWidgets`,
  pre-existing name): already documented in
  `RE_NOTES_player_snapshot_system.md` — sets `DAT_001dd83c`/`860`/`878` as
  booleans and `DAT_001dd874` as a 5-level enum. **`DAT_001dd878` is
  confirmed shared with `AggressionManager_LoadPendingStateBuffer`'s own
  branch logic** — a real cross-system connection between a Display-tab
  setting and the save/replay-state loader, not coincidental (see that file
  for the full trace).

## The widget-builder / pause-menu connection

**`OptionsMenu_BuildDisplaySettingsWidgets`** (was `FUN_0009e2f0`) — found
sitting in the exact same code cluster as the Display tab's cache/event
functions. `xrefs_to` showed its **sole caller is the already-named
`UI_BuildPauseMenu`** — confirming the Pause Menu embeds this same
Display-settings widget group directly, not a coincidental reuse of generic
utility code. Tags a `"f3wsngltxt"` single-line-text widget with float
constants that read as a color/position tuple. Structural confidence only —
the decompile is noisy (register-allocation artifacts show up as literal
address-shaped stack values) and individual widget fields weren't traced
field-by-field.

This also explains an odd loose end from `RE_NOTES_boardselect_screen.md`:
`BoardSelectScreen_TickPositionAnimation` ends with an unexplained tail-call
into `UI_BuildPauseMenuText` — both that and this finding point at the same
underlying fact, that several unrelated frontend screens directly embed or
call into the Pause Menu's own construction code, presumably because the
Pause Menu is a shared overlay reachable from many game states.

4 renames total (6 with the 2 pre-existing).

## Still open

- **`OptionsMenu_CacheDisplaySettingsFromWidgets`'s second caller** (found via
  `xrefs_to`, address `0x0009ee44`) and **`OptionsMenu_HandleDisplaySettingWidgetEvent`'s
  caller** (`0x000a212a`) — both in still-unbounded code, not chased this
  pass. Likely a generic per-tab widget-event router (analogous to
  `BoardSelectScreen`'s `HandleSelectionEvent`/`HandleSecondaryEvent` split)
  reached via vtable dispatch.
- **Sibling "HandleXSettingWidgetEvent" functions for tabs 0/1** — only the
  Display tab's event handler is named; Audio and Control tabs likely have
  analogous counterparts not yet located.
- **`OptionsMenu_HandleTabSwitchEvent`'s own jump table** (`PTR_LAB_0009edc8`,
  10 entries, immediately following its body) — read enough to identify as a
  jump table but not decoded entry-by-entry.
- **Tab 3's actual identity** — no cache function, reached via a distinct
  2-vtable-call path; genuinely unidentified (controller test/calibration is
  a guess, not confirmed).
- **The overall screen's entry point** — what constructs this whole
  multi-tab object and where it's reached from in the frontend menu flow
  (`RE_NOTES_frontend_menu_map.md`) wasn't traced this pass.
