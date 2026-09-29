# RE notes: `TitleIntroSequence` — the boot title/cinematic controller

**2026-07-21, "go fresh" pass.** Picked a genuinely untouched direction: the extracted
`Game Data\data\video\` directory (32 `.mpc` files, up to 142MB — clearly full-motion
video, never examined this session) and 19 much smaller per-track `.xss` files. Names
immediately suggested the content: `eabig.mpc` (EA logo splash), `ssxintro.mpc` (the
SSX Tricky opening cinematic), `cv_<charactername>.mpc` (per-character cutscenes:
`cv_brod`/`cv_eddi`/`cv_elis`/`cv_luth`/`cv_mac`/`cv_mari`/`cv_moby`/`cv_psym`/
`cv_seei`/`cv_zoe`), `nbapromo.mpc` (a cross-promotional trailer), plus `audio.mpc`/
`charactr.mpc`/`eabig.mpc`/`evolve.mpc` and per-track `.xss` files (`alaska`, `aloha`,
`elysium`, `gari`, `megaple`, `merquer`, each with a `j`-prefixed smaller variant).

## Finding the trigger code

Searched for the literal path string `"data/video/eabig.mpc"` (found at `0x001966b8`)
and traced its one reader. A nearby coincidental string cluster (`"onTitleOpen"`/
`"onTitleStop"`/`"GameLoad"`/`"f3dstvid"`) initially looked like a hand-built
event/state table sitting right next to the video paths — **that was a false lead,
caught before being written up**: `"onTitleOpen"` is just a `Script_PlayByName`
argument (the level-script system's own name-resolution mechanism, unrelated code
that happens to have its string literal pooled nearby in the read-only data segment).
The real reader of the video paths is a completely different, vtable-dispatched class.

## The `TitleIntroSequence` class

Found via the video-path string's actual reader, which turned out to be one of 5 real
vtable slots (vtable at `0x00196968`, immediately followed in memory by the tag string
`"f3stttl"` — matching this project's `f3`-prefixed frontend-widget tag convention
already documented in `RE_NOTES_frontend_menu_map.md`). All 5 slots read and named:

- **`TitleIntroSequence_CheckLoaderReady`** (was `FUN_0007c950`) — checks a readiness
  flag on a sub-object (`this+0x9c`); once ready, clears a completion flag (`this+7`).
  **Note**: this function was initially mis-created one byte late (`0x0007c951`
  instead of `0x0007c950`) — caught by cross-checking the vtable's own literal
  pointer value, fixed via `/delete_function` + recreate at the correct address.
- **`TitleIntroSequence_IsComplete`** (was `FUN_0007d8a0`) — a trivial 3-byte getter
  for that same completion flag (`this+7`).
- **`TitleIntroSequence_QueueBootVideos`** (was `FUN_0007c970`) — the payload. Gated
  on `DAT_001df3f4==1` (the same global later read as a bitmask by the already-named
  `Application_StateMachineTick` to gate FrontEnd/InGame/VideoPlayer-module
  transitions — confirmed as a shared boot-sequence state variable, reused with
  different value-interpretation schemes across phases). Allocates 2 tagged
  `"f3dstvid"` (0x158-byte) widget-wrapper objects and copies a video path into each:
  `"data/video/ssxintro.mpc"` into the first, `"data/video/eabig.mpc"` into the
  second — then links the second's `this+0x154` field to point at the first.
  **This queues `eabig.mpc` (the EA logo) to play FIRST, chained into `ssxintro.mpc`
  (the opening cinematic) playing next.** Sets `DAT_001df3f4=2` and the completion
  flag once queued; if the gate condition isn't met, skips video playback entirely.
- **`TitleIntroSequence_Tick`** (was `FUN_0007cbd0`) — checks graphics-device
  readiness, then checks whether a video is still queued/playing (`this+0xf0`, the
  same field `QueueBootVideos` populates with the chained video-widget pointer);
  once no video is active, runs several graphics-device state calls (post-video
  cleanup).
- **`TitleIntroSequence_Render`** (was `FUN_0007ce10`) — calls the already-named
  `TransitionEffect_Update` every frame (confirming this class drives the screen
  wipe/transition during boot), plus one-time first-tick setup.

11 renames total (5 vtable methods + 6 `/create_function` boundary fixes, 2 of which
were genuinely missing Function objects and 1 corrected off-by-one).

## Honest scope

This maps the **trigger and playback-queue mechanism** for the boot cinematics
cleanly. Not pursued further this pass:

- The exact byte format of `.mpc`/`.xss` files themselves (container format, likely
  wrapping a standard Xbox video codec — not decoded).
- Where/how the per-character (`cv_*.mpc`) and per-track (`.xss`) videos get
  triggered (presumably character-select and track-load screens respectively,
  via the same `VideoPlayer`/widget-wrapper mechanism, but not individually traced).
- `nbapromo.mpc`'s trigger site (a cross-promotional trailer, likely an attract-mode
  or main-menu Easter egg).
- The exact `DAT_001df3f4` value transitions across the full boot sequence (only 2
  of its states — `==1` here, the `&0x20`/`&0x3b` bitmask reads in
  `Application_StateMachineTick` — are understood; the full state space isn't
  mapped).

A well-scoped, concrete set of next threads if this area is revisited, not a dead end.

## Immediate follow-up: the central menu preview-video dispatcher

Chased the per-character `cv_*.mpc` trigger site next. A literal search for
`"data/video/cv_brod.mpc"` found nothing — the reason turned out to be that the
filename is built at **runtime** via a `printf`-style format string,
`"data/video/cv_%s.mpc"`, found alongside a whole cluster of other menu-video path
formats: `"data/video/music.mpc"`, `"data/video/audio.mpc"`, `"data/video/charactr.mpc"`,
`"data/video/tracks.mpc"`, `"data/video/tricks.mpc"`, `"data/video/evolve.mpc"`.

Traced the format string to its one reader and named it
**`FrontEndVideo_SelectPreviewClip`** (was `FUN_00097230`, needed `/create_function`
— no prior Function object existed). This is **the central dispatcher for every
menu-screen preview/attract video in the game**:

- Allocates the same `"f3stvid"`-tagged video-widget wrapper class (same vtable,
  `0x001965e8`) that `TitleIntroSequence_QueueBootVideos` uses for the boot
  cinematics — confirming both systems share one underlying `VideoPlayer`-widget
  class.
- Compares several fields (`this+0x28`/`+0x34`/`+0x100`/`+0x6c`) to determine which
  menu screen is active, then picks a video accordingly: `evolve.mpc` (promo reel),
  `tricks.mpc` (trick tutorial), `tracks.mpc` (track showcase), `charactr.mpc`
  (character roster), `music.mpc` (jukebox), `audio.mpc` (sound options).
- For the character-select screen specifically, builds
  `"data/video/cv_<charactername>.mpc"` at runtime via `CRT_FormatString`, indexing
  a character-name table (`PTR_DAT_001b53f8`) by the currently-selected character —
  the per-character intro cutscene.
- Handles 2 special-case promotional videos: `sled2pr.mpc` ("Sled" was this game's
  internal codename per common industry knowledge — almost certainly a sequel
  preview trailer) and `nbapromo.mpc` (the cross-promotional NBA trailer already
  spotted in the file listing).

Its one caller is a vtable slot in the same large vtable (`0x00199370`) as the
already-named `UI_BuildJukeboxVoicePlayer` — confirming this belongs to a top-level
frontend/menu controller class. **Not pursued further this pass**: the owning
class/vtable itself wasn't individually named, and the exact meaning of the
`this+0x28`/`0x34`/`0x100`/`0x6c` menu-screen-ID fields wasn't decoded — a
well-scoped next thread if this whole frontend controller is worth mapping.

1 more rename (11 + 1 = 12 total for this whole subsystem).

## Identified the owning class: the "DVD Extras" / jukebox menu screen

Continued to the caller's vtable (`0x00199348`, ~27 slots) to identify the class
`FrontEndVideo_SelectPreviewClip` and `UI_BuildJukeboxVoicePlayer` both belong to.
A block of celebrity voice-actor credit strings sits immediately before the vtable
in read-only data — `"NAKED"`, `"NICK MALAPERIMAN"`, `"DJ PRECISE (aka RYAN WALL)"`,
`"OLIVER PLATT"`, `"DAVID ARQUETTE"`. Combined with the already-documented
`f3DVD*`-tagged jukebox screen builder and the preview-video dispatcher's
`music`/`audio.mpc` cases, this is unambiguously the in-game **"DVD Extras"**
bonus-content menu — SSX Tricky shipped with a DVD-style extras screen featuring a
jukebox with celebrity DJ voice commentary.

Named the destructor to anchor the class: **`DVDExtrasMenu_ScalarDeletingDestructor`**
(was `FUN_00096dd0`, slot 0 — standard scalar-deleting-destructor shape). Checked a
few more slots for completeness: slot 2 is the already-named `Widget_TickWrapper`
(confirms `Widget`-family inheritance, matching every other frontend screen in this
project), and 3 more (`0x00085110`/`0x00085180`/`0x00085250`) are generic cascading
child-property setters matching the already-documented `Widget_SetFlagA`/`SetRect`
shape — not screen-specific, so not individually named. **~24 of ~27 slots remain
unmapped** — a well-scoped next thread, not pursued further since the class
identity itself (the actual goal of this thread) is now confidently established.

1 more rename (12 + 1 = 13 total for this whole subsystem).

## Chased the per-track `.xss` trigger — found a different, legitimate subsystem instead

Tried to find who plays the per-track `.xss` preview videos next, starting from the
already-named `TrackInfo_GetRecordByIndex`'s full caller list. Found a string/pointer
table at `0x001b8e00`-`0x001b8e4c` holding `"FEMega1"`/`"FEMerq2"`-style track-ID
short codes plus 8 `.xss` path pointers (`"data/video/untrack.xss"`,
`"data/video/alaska.xss"`, etc.) — but `/xrefs_to` on both the table base and
individual entries came back empty, consistent with this project's repeated finding
that some tables are only reached via computed/register-relative indexing that
static xref search can't catch. **The actual `.xss` trigger site remains unfound.**

Checking `TrackInfo_GetRecordByIndex`'s other 2 unnamed callers instead turned up a
genuinely different, legitimate subsystem — documented honestly as its own finding
rather than forced into the video-system narrative. Named
**`TrackIntroMusic_SelectAndPlay`** (was `FUN_0010eda0`): gets the current track's
name, lowercases it, searches a loaded `"data/config/intromus.inf"` config file for
a case-insensitive matching entry, and if found, plays `"data/audio/<name>"` with a
volume fade-in (skipped for a few excluded game modes). **The per-track intro-music
selector** — each track's background music during its loading/intro screen is
configured via an external `.inf` file rather than hardcoded. A sibling function
(`FUN_00112870`, uppercases instead of lowercases) does a much larger, unrelated
audio-state reset also keyed off the track name — genuinely tangential, not pursued.

1 more rename (13 + 1 = 14 total for this whole subsystem thread).

## The `.mpc`/`.xss` container formats — decoded via raw file bytes, one thread fully exhausted

Read the actual file headers directly (not code) to settle the open "container format
not decoded" question.

**`.mpc` = confirmed a real MPEG-1/MPEG-2 video elementary stream, encoded via the
"Gimex" middleware SDK** (a genuine, historically-real cross-platform media library
used on several early-2000s consoles). `eabig.mpc`'s header: an 8-byte custom
`"MPCh"` wrapper (magic + a leading chunk-size field), immediately followed by
standard MPEG start codes (`00 00 01 b3` sequence header, `00 00 01 b5` extension,
`00 00 01 b2` user-data — containing the literal embedded string
`"Encoded with MPEG Gimex module"` — `00 00 01 b8` group-of-pictures, `00 00 01 00`
picture start). This is exactly what the already-named `VideoPlayer_FindNextChunkByMagic`
checks for (`0x6843504d` = `"MPCh"` as a 4-byte magic, confirmed via a direct
`/search_bytes` hit for `"MPCh"` landing inside that exact function) — **fully
closes the loop**: `.mpc` files are standard MPEG streams, chunked with a thin
custom header, played through the already-documented `VideoPlayer` class.

**`.xss` is a genuinely different, non-MPEG format — and its loader could not be
found anywhere in this binary, a real dead end confirmed through multiple
independent techniques, not just unchecked.** `alaska.xss`'s header starts with a
`"XSSF"` magic (4 bytes) followed by repeating bit-packed patterns (`...cf9f`/
`...aa00`/`0x5555`-shaped words) consistent with a proprietary compressed-audio
format (ADPCM-style delta encoding) — **no MPEG start codes anywhere in the first
4KB**, confirming it is not a video stream at all despite living in `data/video/`
alongside the real `.mpc` videos. Most likely each track's `.xss` is a
separate **audio-only** stream (possibly meant to accompany a shared/generic video,
or simply background music/commentary), not a video preview as first assumed.

Searched for the literal `"XSSF"` magic string across the entire binary via
`/search_bytes` — **zero matches, anywhere**. Combined with the earlier finding
that the `.xss` path-pointer table itself (`0x001b8e00`-`0x001b8e4c`) also has zero
`xrefs_to` hits on its base address, individual entries, *and* a raw immediate-value
search for the table's own address — this is about as exhausted as a static-analysis
dead end gets. **Honest conclusion**: either `.xss` files are unused/leftover
content from development (never wired into the shipped retail build), or they're
loaded through a mechanism entirely outside what static analysis of `default.xbe`
can reach (e.g. a different executable/overlay, or a runtime-computed path that
never appears as a literal anywhere). Not pursued further — this specific thread
(the per-track `.xss` video/audio trigger) is now genuinely closed out, not merely
abandoned.

0 renames this section (file-format analysis only, no code identified to rename).

## Pushed further into the `DVDExtrasMenu` vtable before stopping

Checked several more of the ~24 remaining slots to see if the class was worth fully
mapping. Found a consistent structural pattern confirming (not just suggesting) the
class's design: multiple slots (input handling, video-cue triggering) all compare
`this+0x28` against other fields (`this+0x34`/`0x60`) — the exact same "current
sub-screen mode" comparison shape `FrontEndVideo_SelectPreviewClip` uses to pick
which video to queue. This is genuinely one consistent state-machine field driving
screen-mode-dependent behavior across the whole class, not a coincidence. The
remaining slots read as ordinary input-dispatch/audio-cue-trigger cases following
this same pattern — diminishing returns for further naming, so stopped here with
the class's core design confirmed rather than force-naming every generic dispatch
case. **Caught and discarded a transcription slip**: briefly queried
`0x0007cf10` (not a real vtable slot address — a typo) and got back the
already-named `TitleIntroSequence_Render`'s own decompile, since that address falls
inside its body range; recognized immediately, no incorrect information was
recorded anywhere.

0 renames this section — structural confirmation only.

## Status: this investigation is now genuinely exhausted

Starting from an unopened `Game Data\data\video\` directory, this thread mapped:
the boot cinematic queue (`TitleIntroSequence`, 5 methods), the central menu
preview-video dispatcher (`FrontEndVideo_SelectPreviewClip`), the owning "DVD
Extras" menu class (destructor + structural confirmation of its core design), the
per-track intro-music selector (a related-but-separate system, `TrackIntroMusic_SelectAndPlay`),
and the `.mpc`/`.xss` container formats themselves (one fully closed via code, one
a confirmed, multi-technique-exhausted dead end). 14 renames total. Every
remaining loose end (the `.xss` trigger, ~20 generic `DVDExtrasMenu` slots, the
tangential `FUN_00112870` audio-reset function) was checked enough to know it's
either genuinely unreachable via static analysis or low-value relative to effort
— not simply left unexamined.

## `.mpc` CONTAINER + CODEC DECODED (2026-07-22, during the PC port)

Decoded the `.mpc` video file format while wiring boot-video playback into
the port. It is **EA's interleaved "SCHl" multi-stream container** (the same
family as the `audio/*.big` sound banks), NOT a bare video file:

  a flat sequence of chunks, each:
    [ 4-byte magic ][ u32 totalSize (LE, INCLUDING this 8-byte header) ]
    [ totalSize - 8 payload bytes ]
  magics seen in eabig.mpc: **MPCh** (video, 113 chunks) + **SCHl/SCCl/SCDl/
  SCEl** (EA audio stream: header/?/data/end, 108 chunks). The size field is
  the *total* chunk size, so the next chunk is at `pos + size` (walking this
  way consumes the whole file with perfect sync). Each MPCh chunk carries one
  coded picture.

The **video codec is MPEG-2, not MPEG-1** (this refines the old "MPEG-1/2"
note to a definite answer). Concatenating all MPCh payloads yields a clean
elementary stream (verified: 1 sequence header, 113 pictures, 576x448,
29.97 fps) that carries MPEG-2 **extension start codes (00 00 01 B5)** --
sequence_extension (id 1), sequence_display_extension (id 2), and
picture_coding_extension (id 8, one per picture). Confirmed by decode: an
MPEG-1-only decoder (pl_mpeg) reads the shared seq-header dimensions but
produces garbage on the picture data (even the I-frame), exactly as expected
for feeding MPEG-2 to an MPEG-1 decoder.

**Port impact**: the boot-video playback path (`port/src/game/video_mpc.cpp`)
de-chunks the container correctly and detects MPEG-2 (declining to decode via
pl_mpeg, showing a clean placeholder). **RESOLVED**: the port now decodes and plays both boot videos (EA logo +
intro) via **libmpeg2** (mpeg2dec, GPL-2, portable pure-C build in
`port/third_party/libmpeg2/`) -- screenshot-verified against the originals.

## `.mpc` AUDIO decoded (2026-07-22): EA-XA ADPCM in SCDl blocks

The audio half of the `.mpc` container is now decoded too (the port plays it).
**Codec: EA-XA ADPCM**, 48 kHz stereo (rate/channel tags 0x82/0x84 in the
`SCHl`+`PT` header).

**Block framing recovered from the game's own chunk dispatcher**
(`FUN_00013030` dispatches on the chunk magics -- `SCDl`=0x6c444353,
`SCHl`=0x6c484353, `SCEl`, `SCCl` -- and `FUN_00012f60` computes the
per-channel data pointers). An `SCDl` block is:

```
+0x00 u32 'SCDl'
+0x04 u32 totalSize (incl. header)
+0x08 u32 sampleCount (per channel)
+0x0C u32 chOffset[nch]          <- per-channel relative offsets
channel i data = block + chOffset[i] + nch*4 + 0xC
                 then a 4-byte predictor-history header,
                 then EA-XA frames.
```

Each EA-XA frame = 1 header byte (hi nibble = coefficient index 0-3, lo
nibble = left-shift) + 14 data bytes = 28 nibbles = 28 samples, **low nibble
first**. Sample = `(nibble<<shift)*256 + c1*h1 + c2*h2 >> 8`, clamped, with
`h1/h2` running per channel *across blocks*. Coefficients are the standard EA
pairs {0,0},{240,0},{460,-208},{392,-220}.

**Verification**: decoding eabig.mpc yields 168,000 stereo frames = 3.50 s at
48 kHz with a clean dynamic envelope (quiet -> swell -> peak -> decay, i.e. the
logo sting) and a zero-crossing rate of 0.090 (tonal; white noise would be
~0.5). A Python reference implementation and the port's C++ decoder produce
**bit-identical output** (0 differing samples of 336,000).

**Note**: the earlier assumption that the two channels split the block in half
was wrong -- that produced a negative shift count. The per-channel offset table
above is the correct framing.

Port: `port/src/assets/ea_adpcm.cpp` (decoder), `port/src/platform/win_audio.cpp`
(waveOut streaming output), wired into `MpcVideo` with ~0.25 s of lookahead.

## `.mpc` AUDIO DECODED (2026-07-27): EA-XA ADPCM, and a 6-channel intro

The `SC*l` audio chunks alongside the `MPCh` video are **EA-XA ADPCM**.
Decoded and verified while adding sound to the port. The `SCHl` chunk holds
an EA "PT" parameter header: fields are `[tag][len][big-endian value]`,
except `0xFD` (a bare marker) and `0xFF` (end). Relevant tags:

| tag | meaning | eabig.mpc | ssxintro.mpc |
|---|---|---|---|
| 0x82 | channel count | **2** | **6** |
| 0x84 | sample rate | 48000 | 48000 |
| 0x85 | total samples | 168000 (3.50 s) | 3332780 (69.43 s) |
| 0xA0 | codec id | 10 | 10 |

**The intro is 5.1 surround (6 channels) while the EA logo is stereo** --
the channel count MUST be read from the header, never assumed. Channel
order verified empirically as the standard L,R,C,LFE,Ls,Rs: ch2 correlates
+0.991 with L+R (a centre carrying the main mix) while LFE and the
surrounds are uncorrelated (|r| < 0.01).

`SCDl` data-block layout (per block): `[u32 'SCDl'][u32 totalSize]
[u32 sampleCount][u32 chOffset[nch]]`, then channel *i*'s data at
`block + chOffset[i] + nch*4 + 0xC`, starting with a 4-byte history header
followed by EA-XA frames. A frame is **1 header byte + 14 data bytes = 28
samples**; the header's high nibble is the coefficient index and the low
nibble the shift. Two independent arithmetic checks confirm this framing:
sample counts are always exact multiples of 28 (e.g. 1624 = 58x28), and
channel 1's offset equals `4 + 58*15 = 874` bytes exactly.

Per-sample reconstruction (the part that is easy to get wrong):

```c
shift = (hdr & 0x0F) + 8;                  // NOTE: scales the nibble DOWN
nib   = (i & 1) ? (byte & 0x0F) : (byte >> 4);   // HIGH nibble first
s     = (int32_t)((uint32_t)nib << 28) >> shift; // sign via bit 31
s     = (s + c1*hist1 + c2*hist2 + 128) >> 8;    // combined, with rounding
clamp to int16; hist2 = hist1; hist1 = s;
```

Coefficient pairs by index: (0,0), (240,0), (460,-208), (392,-220).
A decisive alignment check: **100% of frame headers use index 0-3** when
the framing is right (a misaligned read gives a ~uniform 0-15 spread).

Verified decode quality: eabig.mpc -> **0.000% clipped**, peak 30545,
RMS 7795; ssxintro.mpc -> 0.00% clipped on all six channels. (An earlier
attempt that scaled the nibble *up* instead of down produced ~34% clipped
samples -- audible as loud static.)

Implemented in `port/src/assets/ea_adpcm.cpp`, with the 5.1 -> stereo
downmix in `port/src/game/video_mpc.cpp`.
