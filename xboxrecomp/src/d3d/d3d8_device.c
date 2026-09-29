/**
 * D3D8→D3D11 Compatibility Device Implementation
 *
 * Implements the Xbox D3D8 IDirect3DDevice8 interface using D3D11.
 * The game's translated RenderWare code calls D3D8 methods through
 * COM vtables; this layer translates those calls to D3D11 equivalents.
 *
 * Architecture:
 * - D3D11 device and swap chain created during initialization
 * - Render state tracking: D3D8 states mapped to D3D11 state objects
 * - Texture/buffer management: D3D8 resource handles wrap D3D11 resources
 * - Fixed-function pipeline: emulated via D3D11 shaders (the Xbox D3D8
 *   pipeline is configurable but not fully programmable)
 *
 * Build: Requires Windows SDK with d3d11.h and dxgi.h
 */

#include <time.h>
#include "d3d8_internal.h"
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Internal device state
 * ================================================================ */

/* Maximum tracked render states, texture stages, and transforms */
#define MAX_RENDER_STATES    256
#define MAX_TEXTURE_STAGES   4
#define MAX_TSS_STATES       32
#define MAX_TRANSFORMS       512
#define MAX_LIGHTS           8

typedef struct D3D8DeviceState {
    /* D3D11 objects */
    ID3D11Device            *d3d11_device;
    ID3D11DeviceContext     *d3d11_context;
    IDXGISwapChain          *swap_chain;

    /* Default render targets */
    ID3D11RenderTargetView  *default_rtv;
    ID3D11DepthStencilView  *default_dsv;
    ID3D11Texture2D         *default_depth;

    /* Part 183: the title renders into an offscreen scene target (default_rtv
     * above) at the configured resolution; the swap chain is only the
     * window's output, and every present scales the scene into it. */
    ID3D11Texture2D          *scene_tex;      /* resolved, sampleable */
    ID3D11ShaderResourceView *scene_srv;
    ID3D11Texture2D          *scene_ms;       /* multisampled, when anti-aliasing */
    UINT                      msaa, msaa_requested;
    ID3D11Texture2D          *swap_tex;
    ID3D11RenderTargetView   *swap_rtv;
    UINT                      swap_w, swap_h;
    UINT                      guest_w, guest_h;   /* the title's own backbuffer size */

    /* Window; width/height are the scene target's size */
    HWND                    hwnd;
    UINT                    width;
    UINT                    height;
    D3DFORMAT               backbuffer_format;

    /* State tracking */
    DWORD                   render_states[MAX_RENDER_STATES];
    DWORD                   tss[MAX_TEXTURE_STAGES][MAX_TSS_STATES];
    D3DMATRIX               transforms[MAX_TRANSFORMS];
    D3DVIEWPORT8            viewport;
    D3DMATERIAL8            material;
    D3DLIGHT8               lights[MAX_LIGHTS];
    BOOL                    light_enable[MAX_LIGHTS];

    /* Current shader/FVF */
    DWORD                   vertex_shader;
    DWORD                   pixel_shader;

    /* Scene state */
    BOOL                    in_scene;

    /* Reference count */
    LONG                    ref_count;
} D3D8DeviceState;

/* Global device instance (Xbox has a single D3D device) */
static D3D8DeviceState g_device_state;
static IDirect3DDevice8 g_device;
static BOOL g_device_initialized = FALSE;

/* The texture the title's frames are rendered into: the scene target, not the
 * swap chain (part 183). Frame dumps, hashes and readbacks read it so that a
 * capture is the rendered frame at the render resolution, whatever size the
 * window happens to be. AddRef'd, like IDXGISwapChain_GetBuffer. */
static HRESULT host_present(void);

static void scene_resolve(D3D8DeviceState *s);

static HRESULT scene_texture_get(ID3D11Texture2D **out)
{
    scene_resolve(&g_device_state);
    *out = g_device_state.scene_tex;
    if (!*out) return E_FAIL;
    ID3D11Texture2D_AddRef(*out);
    return S_OK;
}

/* Current resource bindings */
static IDirect3DVertexBuffer8 *g_cur_vb = NULL;
static UINT                    g_cur_vb_stride = 0;
static IDirect3DIndexBuffer8  *g_cur_ib = NULL;
static UINT                    g_cur_ib_base_vertex = 0;
static IDirect3DBaseTexture8  *g_cur_textures[4] = { NULL };

/* Forward declarations */
static const IDirect3DDevice8Vtbl g_device_vtbl;
static void up_ring_shutdown(void);

/* ================================================================
 * Public frame pump (called from recompiled game code)
 * ================================================================ */
/* Pump the window's message queue without presenting.
 *
 * Presenting is driven by the title's frame boundary now, but the window still
 * has to stay responsive between boundaries, and the two must not be welded
 * together: presenting on a timer to keep messages flowing is what put a
 * freshly cleared buffer on screen. */
static DWORD g_last_present_tick = 0;

/* Milliseconds since the last actual Present, from any caller. The pump uses
 * this so its safety-net present only fires when the title has genuinely
 * stopped producing frames, rather than on a fixed period that would cut in
 * between a frame's clear and its draw. */
DWORD d3d8_MsSincePresent(void)
{
    DWORD now = GetTickCount();
    if (g_last_present_tick == 0) g_last_present_tick = now;
    return now - g_last_present_tick;
}

/* Sample the back buffer at an arbitrary point, for diagnosis. */
/* Write the whole back buffer out as a .bmp.
 *
 * Sampling two pixels is enough to tell a cleared buffer from a filled one,
 * but not to tell whether something small -- a line of text, say -- was drawn
 * somewhere in between. XBOX_D3D_DUMP=<path-prefix> saves complete frames so
 * the question can be answered by looking rather than inferring. */
/*
 * Cheap fingerprint of the current back buffer, or 0 if it cannot be read.
 *
 * Exists for XBOX_D3D_DUMP_ONCHANGE. Catching something that appears for a
 * single frame otherwise means dumping every present -- hundreds of identical
 * 900 KB images of a static screen, and the interesting one buried among them.
 * Sampling a sparse grid is enough: two frames that differ anywhere a person
 * could see will differ here, and an unchanged screen hashes identically.
 */
unsigned d3d8_BackbufferHash(void)
{
    ID3D11Texture2D *back = NULL, *stage = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE m;
    unsigned hash = 0;

    if (!g_device_state.swap_chain || !g_device_state.d3d11_device) return 0;
    if (FAILED(scene_texture_get(&back)) || !back) return 0;
    ID3D11Texture2D_GetDesc(back, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(g_device_state.d3d11_device,
            &td, NULL, &stage)) || !stage) { ID3D11Texture2D_Release(back); return 0; }
    ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
        (ID3D11Resource *)stage, (ID3D11Resource *)back);
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
            (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
        unsigned y, x;
        hash = 2166136261u;                 /* FNV-1a */
        for (y = 0; y < td.Height; y += 4) {
            const unsigned char *r = (const unsigned char *)m.pData + y * m.RowPitch;
            for (x = 0; x < td.Width; x += 4) {
                hash ^= r[x * 4 + 0]; hash *= 16777619u;
                hash ^= r[x * 4 + 1]; hash *= 16777619u;
                hash ^= r[x * 4 + 2]; hash *= 16777619u;
            }
        }
        if (hash == 0) hash = 1;            /* 0 means "could not read" */
        ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                                  (ID3D11Resource *)stage, 0);
    }
    ID3D11Texture2D_Release(stage);
    ID3D11Texture2D_Release(back);
    return hash;
}

void d3d8_DumpBackbuffer(const char *path)
{
    ID3D11Texture2D *back = NULL, *stage = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE m;
    FILE *f;
    unsigned w, h, y, x, row, imgsz;
    unsigned char hdr[54];

    if (!g_device_state.swap_chain || !g_device_state.d3d11_device) return;
    if (FAILED(scene_texture_get(&back)) || !back) return;
    ID3D11Texture2D_GetDesc(back, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(g_device_state.d3d11_device,
            &td, NULL, &stage)) || !stage) { ID3D11Texture2D_Release(back); return; }
    ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
        (ID3D11Resource *)stage, (ID3D11Resource *)back);
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
            (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
        w = td.Width; h = td.Height; row = w * 3;
        row = (row + 3) & ~3u;              /* BMP rows are 4-byte aligned */
        imgsz = row * h;
        f = fopen(path, "wb");
        if (f) {
            memset(hdr, 0, sizeof hdr);
            hdr[0] = 'B'; hdr[1] = 'M';
            *(unsigned *)(hdr + 2)  = 54 + imgsz;
            *(unsigned *)(hdr + 10) = 54;
            *(unsigned *)(hdr + 14) = 40;
            *(int *)(hdr + 18) = (int)w;
            *(int *)(hdr + 22) = (int)h;   /* positive = bottom-up */
            *(unsigned short *)(hdr + 26) = 1;
            *(unsigned short *)(hdr + 28) = 24;
            *(unsigned *)(hdr + 34) = imgsz;
            fwrite(hdr, 1, sizeof hdr, f);
            for (y = 0; y < h; y++) {
                const unsigned char *src =
                    (const unsigned char *)m.pData + (h - 1 - y) * m.RowPitch;
                unsigned char pad[4] = {0,0,0,0};
                /* The swap chain is DXGI_FORMAT_R8G8B8A8_UNORM, so the source
                 * bytes are R,G,B,A -- but BMP stores B,G,R. Copying the first
                 * three bytes straight through (as this did) swaps red and
                 * blue in every capture, which is why the XBOX_TEST_CLEAR teal
                 * read back as (153,153,0) and why the EA logo looked blue in
                 * dumps while being correct on screen. */
                for (x = 0; x < w; x++) {
                    unsigned char bgr[3];
                    bgr[0] = src[x * 4 + 2];   /* B */
                    bgr[1] = src[x * 4 + 1];   /* G */
                    bgr[2] = src[x * 4 + 0];   /* R */
                    fwrite(bgr, 1, 3, f);
                }
                fwrite(pad, 1, row - w * 3, f);
            }
            fclose(f);
            fprintf(stderr, "  [D3D] wrote %ux%u frame to %s\n", w, h, path);
            fflush(stderr);
        }
        ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
            (ID3D11Resource *)stage, 0);
    }
    ID3D11Texture2D_Release(stage);
    ID3D11Texture2D_Release(back);
}

void d3d8_DebugSampleBackbuffer(const char *tag)
{
    ID3D11Texture2D *back = NULL;
    if (!g_device_state.swap_chain || !g_device_state.d3d11_device) return;
    if (FAILED(scene_texture_get(&back)) || !back) return;
    {
        D3D11_TEXTURE2D_DESC td;
        ID3D11Texture2D *stage = NULL;
        ID3D11Texture2D_GetDesc(back, &td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        if (SUCCEEDED(ID3D11Device_CreateTexture2D(g_device_state.d3d11_device,
                &td, NULL, &stage)) && stage) {
            D3D11_MAPPED_SUBRESOURCE m;
            ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
                (ID3D11Resource *)stage, (ID3D11Resource *)back);
            if (SUCCEEDED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
                    (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
                const unsigned char *px = (const unsigned char *)m.pData;
                unsigned cx = td.Width / 2, cy = td.Height / 2;
                fprintf(stderr, "  [D3D] sample @%s: tl=%02X%02X%02X ctr=%02X%02X%02X\n",
                        tag, px[2], px[1], px[0],
                        px[cy * m.RowPitch + cx * 4 + 2],
                        px[cy * m.RowPitch + cx * 4 + 1],
                        px[cy * m.RowPitch + cx * 4 + 0]);
                fflush(stderr);
                ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                    (ID3D11Resource *)stage, 0);
            }
            ID3D11Texture2D_Release(stage);
        }
    }
    ID3D11Texture2D_Release(back);
}

void d3d8_PumpMessages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) ExitProcess(0);
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* The guest framebuffer the video player writes into, if any. Stored rather
 * than passed, because the upload has to happen inside the present path: the
 * swap chain is DXGI_SWAP_EFFECT_DISCARD and there are three Present call
 * sites, so a copy made before d3d8_PresentFrame is destroyed by any Present
 * that lands in between. */
static const void *g_guest_fb_src   = NULL;
static const void *g_guest_fb_alt   = NULL;   /* the other half of the flip */
static unsigned    g_guest_fb_pitch = 0;
static unsigned    g_guest_fb_w     = 0;
static unsigned    g_guest_fb_h     = 0;

void d3d8_SetGuestFramebuffer(const void *src, unsigned pitch,
                              unsigned w, unsigned h)
{
    g_guest_fb_src = src; g_guest_fb_pitch = pitch;
    g_guest_fb_w = w;     g_guest_fb_h = h;
}

/* The title double-buffers its video surface and alternates between two
 * addresses, so uploading a fixed one shows a stale frame every other present
 * -- which reads as texture glitching rather than as a missed frame. Register
 * both and let the present pick whichever was written most recently. */
void d3d8_SetGuestFramebufferAlt(const void *alt)
{
    g_guest_fb_alt = alt;
}

/* The video player writes the guest framebuffer through LockRect; everything
 * else the title draws goes through the push buffer to the host render target.
 * Uploading the guest buffer on every present therefore pasted the last video
 * frame over every screen that followed: after START skipped the attract movie
 * the frontend issued ~10,000 draws a second behind a frozen picture of the
 * intro's first frame (part 180). The upload now runs only while the video is
 * actually locking the buffer -- the player locks once per decoded frame, ~30 a
 * second against ~70 presents, so 30 idle presents (~0.4 s) means it stopped. */
#define GUEST_FB_IDLE_PRESENTS 30u
static unsigned g_present_seq     = 0;
static unsigned g_guest_fb_lock_seq = 0;
static unsigned g_guest_fb_locks  = 0;

void d3d8_NoteGuestFramebufferLock(void)
{
    g_guest_fb_lock_seq = g_present_seq;
    g_guest_fb_locks++;
}

static int guest_fb_active(void)
{
    static int was_active = 0;
    int active = g_guest_fb_locks != 0
              && g_present_seq - g_guest_fb_lock_seq <= GUEST_FB_IDLE_PRESENTS;
    if (active != was_active) {
        fprintf(stderr, "  [FB] video framebuffer %s after %u locks (present %u)\n",
                active ? "active: presenting the video"
                       : "idle: presenting rendered frames",
                g_guest_fb_locks, g_present_seq);
        fflush(stderr);
        was_active = active;
    }
    return active;
}

/* Cheap freshness probe: a few sparse samples down the middle of the surface.
 * Whichever buffer's samples changed since the last present is the one the
 * title just blitted into. */
static unsigned guest_fb_sig(const void *base, unsigned pitch, unsigned h)
{
    const unsigned char *p = (const unsigned char *)base;
    unsigned sig = 2166136261u, y;
    if (!p) return 0;
    for (y = 0; y < h; y += 16) {
        const unsigned char *r = p + (size_t)y * pitch;
        unsigned k;
        for (k = 0; k < 64; k += 4) {
            sig ^= r[(pitch / 2) + k];
            sig *= 16777619u;
        }
    }
    return sig;
}

static const void *guest_fb_freshest(void)
{
    static unsigned sig_a = 0, sig_b = 0;
    static int seeded = 0;
    unsigned a, b;
    const void *pick;

    if (!g_guest_fb_alt) return g_guest_fb_src;

    a = guest_fb_sig(g_guest_fb_src, g_guest_fb_pitch, g_guest_fb_h);
    b = guest_fb_sig(g_guest_fb_alt, g_guest_fb_pitch, g_guest_fb_h);
    if (!seeded) { seeded = 1; sig_a = a; sig_b = b; return g_guest_fb_src; }

    /* Two rules, and both are needed.
     *
     * 1. Never copy a buffer that is changing right now -- that is the one the
     *    title is mid-blit into, and it lands a torn frame (new rows above, old
     *    below, a band across the picture).
     * 2. Never show a frame older than one already shown. Picking "whichever is
     *    stable" satisfies rule 1 but not rule 2: with the video at ~30 fps and
     *    presents at ~37, both buffers are often idle, and the stable one is
     *    sometimes the *older* of the two. Measured: 7 of 60 consecutive
     *    presents displayed a frame that had already been superseded, which is
     *    the residual glitching. So track when each buffer last changed and
     *    take the newest idle one, refusing to go backwards. */
    {
        static unsigned gen_a = 0, gen_b = 0, now = 0, shown = 0;
        int a_busy, b_busy;
        unsigned cand_gen;

        now++;
        if (a != sig_a) gen_a = now;
        if (b != sig_b) gen_b = now;
        sig_a = a; sig_b = b;

        a_busy = (gen_a == now);
        b_busy = (gen_b == now);

        if (a_busy && b_busy)      { pick = NULL; cand_gen = 0; }
        else if (a_busy)           { pick = g_guest_fb_alt; cand_gen = gen_b; }
        else if (b_busy)           { pick = g_guest_fb_src; cand_gen = gen_a; }
        else if (gen_a >= gen_b)   { pick = g_guest_fb_src; cand_gen = gen_a; }
        else                       { pick = g_guest_fb_alt; cand_gen = gen_b; }

        if (pick && cand_gen < shown) pick = NULL;   /* would go backwards */
        else if (pick)                shown = cand_gen;
    }
    return pick;
}

/* Presents so far; draws issued now belong to dump frame f<this>. */
unsigned d3d8_PresentSeq(void)
{
    return g_present_seq;
}

void d3d8_PresentFrame(void)
{
    /* Pump Windows messages */
    MSG msg;

    g_present_seq++;
    if (g_guest_fb_src && guest_fb_active()) {
        const void *fb = guest_fb_freshest();
        /* fb == NULL is passed straight through: it means "hold", and the
         * upload re-presents the staging copy rather than re-reading a buffer
         * the title may be writing. */
        d3d8_PresentGuestFramebuffer(fb, g_guest_fb_pitch,
                                     g_guest_fb_w, g_guest_fb_h);
    }

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) ExitProcess(0);
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* XBOX_TEST_CLEAR=1 clears the default RTV to a known colour immediately
     * before the readback. This exercises the RTV -> back buffer -> readback
     * chain with no draw involved, splitting 'the draw fails' from 'the target
     * we render to is not the surface we present and sample'. */
    {
        static int want = -1;
        if (want < 0) {
            const char *e = getenv("XBOX_TEST_CLEAR");
            want = (e && e[0] == '1') ? 1 : 0;
        }
        if (want && g_device_state.d3d11_context && g_device_state.default_rtv) {
            const FLOAT teal[4] = { 0.0f, 0.6f, 0.6f, 1.0f };
            ID3D11DeviceContext_ClearRenderTargetView(g_device_state.d3d11_context,
                g_device_state.default_rtv, teal);
        }
    }

    /* Sampled before Present, not after: with DXGI_SWAP_EFFECT_DISCARD the
     * back buffer's contents are undefined once Present returns, so a
     * readback taken afterwards reports the discard, not the frame. */
    /* XBOX_D3D_DUMP=<prefix> saves whole frames as <prefix>NNN.bmp.
     *
     * Two sampled pixels can tell a cleared buffer from a filled one, but not
     * whether something small -- a line of text -- was drawn somewhere in
     * between. This answers that by looking instead of inferring. */
    {
        /* Spread the captures across the run, not just the opening frames.
         * The first version only ever dumped the first few and concluded the
         * screen stayed white -- it does not; it changes a few seconds in, and
         * sampling only the start hid that completely. */
        static unsigned seen = 0, dn = 0;
        /* XBOX_D3D_DUMP_MAX raises the 24-capture cap. The title reaches its
         * second screen around frame 170 and can then stop presenting
         * entirely, so the interesting frames are the last ones a run
         * produces, not the first 24. */
        static unsigned g_dump_cap = 0;
        if (!g_dump_cap) {
            const char *m = getenv("XBOX_D3D_DUMP_MAX");
            g_dump_cap = (m && atoi(m) > 0) ? (unsigned)atoi(m) : 24u;
        }
        const char *pre = getenv("XBOX_D3D_DUMP");
        if (pre) {
            unsigned every = 60;
            const char *e = getenv("XBOX_D3D_DUMP_EVERY");
            /* XBOX_D3D_DUMP_FROM skips the first N presents before capturing.
             * Without it, catching something that appears for a single frame
             * several seconds in means dumping every frame from zero -- which
             * blows the capture cap long before the interesting window and
             * writes hundreds of megabytes of the same static screen. */
            static unsigned from = 0xFFFFFFFFu;
            if (from == 0xFFFFFFFFu) {
                const char *f = getenv("XBOX_D3D_DUMP_FROM");
                from = f ? (unsigned)atoi(f) : 0u;
            }
            if (e) { unsigned v = (unsigned)atoi(e); if (v) every = v; }
            /* XBOX_D3D_DUMP_ONCHANGE=1 dumps only when the image actually
             * changes, which is the only practical way to catch a screen that
             * appears for one frame in the middle of a static one. */
            static int onchange = -1;
            static unsigned last_hash = 0;
            int changed = 1;
            if (onchange < 0) {
                const char *c = getenv("XBOX_D3D_DUMP_ONCHANGE");
                onchange = (c && c[0] == '1') ? 1 : 0;
            }
            if (onchange) {
                unsigned h = d3d8_BackbufferHash();
                changed = (h != 0 && h != last_hash);
                if (h) last_hash = h;
            }
            if (changed && seen >= from && ((seen - from) % every) == 0
                && dn < g_dump_cap) {
                char path[512];
                snprintf(path, sizeof path, "%s%03u_f%05u.bmp", pre, dn++, seen);
                d3d8_DumpBackbuffer(path);
                /* Stamp each dump with wall-clock seconds. Frame indices alone
                 * cannot answer "how long was this on screen" once the frame
                 * rate stops being 60 Hz -- and it does: the frontend runs at
                 * about 6. */
                if (getenv("XBOX_PRESENT_TIME")) {
                    static clock_t t0 = 0;
                    if (!t0) t0 = clock();
                    fprintf(stderr, "[PRESENTT] dump %s at t=%.2fs (present %u)\n",
                            path, (double)(clock() - t0) / (double)CLOCKS_PER_SEC,
                            seen);
                    fflush(stderr);
                }
            }
            seen++;
        }
    }

    /* Read back what was actually rasterised.
     *
     * Draw counts cannot distinguish 'issued and rasterised' from 'issued and
     * dropped', so sample the surface itself: four points, once every 120
     * frames. */
    {
        static unsigned frame = 0;
        static int want = -1;
        if (want < 0) {
            const char *e = getenv("XBOX_D3D_READBACK");
            want = (e && e[0] == '1') ? 1 : 0;
        }
        /* Off by default: this allocates a staging texture and does a full
         * CopyResource on the pump thread, which is far too heavy to leave in
         * the frame path. XBOX_D3D_READBACK=1 turns it back on. */
        if (want && g_device_state.swap_chain && g_device_state.d3d11_device &&
            (frame <= 6 || (frame % 120) == 0) && ++frame <= 600) {
            ID3D11Texture2D *back = NULL;
            HRESULT hr = scene_texture_get(&back);
            if (SUCCEEDED(hr) && back) {
                D3D11_TEXTURE2D_DESC td;
                ID3D11Texture2D_GetDesc(back, &td);
                td.Usage = D3D11_USAGE_STAGING;
                td.BindFlags = 0;
                td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                td.MiscFlags = 0;
                {
                    ID3D11Texture2D *stage = NULL;
                    if (SUCCEEDED(ID3D11Device_CreateTexture2D(
                            g_device_state.d3d11_device, &td, NULL, &stage)) && stage) {
                        D3D11_MAPPED_SUBRESOURCE m;
                        ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
                            (ID3D11Resource *)stage, (ID3D11Resource *)back);
                        if (SUCCEEDED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
                                (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
                            const unsigned char *px = (const unsigned char *)m.pData;
                            unsigned cx = td.Width / 2, cy = td.Height / 2;
                            fprintf(stderr, "  [D3D] frame %u backbuffer %ux%u fmt=%d:"
                                    " tl=%02X%02X%02X ctr=%02X%02X%02X\n",
                                    frame, td.Width, td.Height, (int)td.Format,
                                    px[2], px[1], px[0],
                                    px[cy * m.RowPitch + cx * 4 + 2],
                                    px[cy * m.RowPitch + cx * 4 + 1],
                                    px[cy * m.RowPitch + cx * 4 + 0]);
                            fflush(stderr);
                            ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                                (ID3D11Resource *)stage, 0);
                        }
                        ID3D11Texture2D_Release(stage);
                    }
                }
                ID3D11Texture2D_Release(back);
            }
        }
    }
    /* Present the backbuffer (VSync = 1) */
    g_last_present_tick = GetTickCount();
    if (g_device_state.swap_chain)
        { static unsigned c = 0; if (++c == 1 || (c % 300) == 0) { fprintf(stderr, "  [PRESENT] PresentFrame #%u\n", c); fflush(stderr); } }
        host_present();
}

/* ================================================================
 * Internal accessors (used by d3d8_resources/shaders/states)
 * ================================================================ */

IDirect3DDevice8    *d3d8_GetDevice(void) { return &g_device; }
ID3D11Device        *d3d8_GetD3D11Device(void) { return g_device_state.d3d11_device; }
ID3D11DeviceContext *d3d8_GetD3D11Context(void) { return g_device_state.d3d11_context; }
IDXGISwapChain      *d3d8_GetSwapChain(void) { return g_device_state.swap_chain; }
ID3D11RenderTargetView *d3d8_GetDefaultRTV(void) { return g_device_state.default_rtv; }
HWND                 d3d8_GetHWND(void) { return g_device_state.hwnd; }
/* The title's own backbuffer size (640x480), which is the space its
 * vertices and pre-transformed positions are in -- not the scene target's. */
UINT                 d3d8_GetBackbufferWidth(void)
    { return g_device_state.guest_w ? g_device_state.guest_w : g_device_state.width; }
UINT                 d3d8_GetBackbufferHeight(void)
    { return g_device_state.guest_h ? g_device_state.guest_h : g_device_state.height; }

/* Scene-target pixels per title pixel, for anything specified in the title's
 * pixel units: scissor and clip rectangles. 1 in every automated run. */
void d3d8_GetGuestScale(float *sx, float *sy)
{
    const D3D8DeviceState *s = &g_device_state;
    *sx = (s->guest_w && s->width)  ? (float)s->width  / (float)s->guest_w : 1.0f;
    *sy = (s->guest_h && s->height) ? (float)s->height / (float)s->guest_h : 1.0f;
}
const DWORD         *d3d8_GetRenderStates(void) { return g_device_state.render_states; }
const DWORD         *d3d8_GetTSS(DWORD stage) { return (stage < MAX_TEXTURE_STAGES) ? g_device_state.tss[stage] : NULL; }
const D3DMATRIX     *d3d8_GetTransform(D3DTRANSFORMSTATETYPE type) {
    return ((DWORD)type < MAX_TRANSFORMS) ? &g_device_state.transforms[(DWORD)type] : NULL;
}

const D3DLIGHT8     *d3d8_GetLight(DWORD index) {
    return (index < MAX_LIGHTS) ? &g_device_state.lights[index] : NULL;
}

BOOL                 d3d8_GetLightEnable(DWORD index) {
    return (index < MAX_LIGHTS) ? g_device_state.light_enable[index] : FALSE;
}

const D3DMATERIAL8  *d3d8_GetMaterial(void) {
    return &g_device_state.material;
}

UINT                 d3d8_GetNumLights(void) {
    return MAX_LIGHTS;
}

/* ================================================================
 * D3D11 initialization helpers
 * ================================================================ */

/*
 * Host output window.
 *
 * On Xbox there is no window at all -- a title scans out straight to the
 * framebuffer, so D3DPRESENT_PARAMETERS::hDeviceWindow is meaningless and
 * SSX Tricky leaves it null. DXGI, though, requires a real HWND for the
 * swap chain's OutputWindow. So when the title supplies no window we make
 * one here: on PC the window *is* the TV.
 *
 * Created on whichever thread first initialises the device, and given a
 * plain WndProc that swallows WM_CLOSE into a flag rather than tearing the
 * window down underneath the renderer -- the recompiled game owns the
 * shutdown path, and destroying its output surface from a message handler
 * would pull the swap chain out from under a frame in flight.
 */
static HWND  s_host_window = NULL;
static HMENU s_host_menu = NULL;
static BOOL  s_host_window_close_requested = FALSE;

BOOL d3d8_HostWindowCloseRequested(void) { return s_host_window_close_requested; }
HWND d3d8_GetHostWindow(void)            { return s_host_window; }

/*
 * Host display settings (part 183).
 *
 * Set by the launcher before the title creates its device. The title always
 * renders its own 640x480 frame; render_w x render_h is the size of the scene
 * target that frame is rasterised into, so the 3D is drawn at that
 * resolution rather than upscaled afterwards. The aspect is the shape the
 * frame is shown at: 16:9 is only right when the title has itself been told
 * the display is widescreen, because it then renders anamorphic -- a wider
 * field of view squeezed into 640x480, for the TV to stretch back out (see
 * xbox_SetVideoFlags). Zero sizes mean the title's own size, which is what
 * every automated run uses, so captures there are unchanged.
 */
static UINT s_cfg_render_w = 0, s_cfg_render_h = 0;
static int  s_cfg_widescreen = 0;
static int  s_cfg_fullscreen = 0;
static int  s_fullscreen = 0;
static WINDOWPLACEMENT s_windowed_placement;

/* Requested anti-aliasing (samples per pixel), picked up at the next frame
 * boundary; the show-FPS flag and a pending screenshot path, all set from the
 * window's thread and read on the render thread. */
static volatile LONG s_want_msaa = 1;
static volatile LONG s_show_fps = 0;
static volatile LONG s_frames = 0;
/* Texture decode time, filled in by the push-buffer translator. */
volatile long g_tex_uploads_log = 0;
volatile double g_tex_upload_ms = 0;
static DWORD s_start_tick = 0;
static CRITICAL_SECTION s_shot_lock;
static volatile LONG s_shot_lock_ready = 0;
static WCHAR s_shot_path[MAX_PATH];
static volatile LONG s_toast_tick = 0;
static WCHAR s_toast[128];

static D3D8HostUiHooks s_ui_hooks;

void d3d8_SetHostDisplay(unsigned render_w, unsigned render_h,
                         int widescreen, int fullscreen)
{
    s_cfg_render_w   = render_w;
    s_cfg_render_h   = render_h;
    s_cfg_widescreen = widescreen ? 1 : 0;
    s_cfg_fullscreen = fullscreen ? 1 : 0;
}

void d3d8_SetHostUiHooks(const D3D8HostUiHooks *hooks)
{
    if (hooks) s_ui_hooks = *hooks;
    else memset(&s_ui_hooks, 0, sizeof s_ui_hooks);
}

void d3d8_SetMsaa(int samples)
{
    InterlockedExchange(&s_want_msaa, (samples == 2 || samples == 4 || samples == 8) ? samples : 1);
}
int  d3d8_GetMsaa(void)          { return (int)s_want_msaa; }
void d3d8_SetShowFps(int on)     { InterlockedExchange(&s_show_fps, on ? 1 : 0); }
int  d3d8_GetShowFps(void)       { return (int)s_show_fps; }
int  d3d8_HostIsWidescreen(void) { return s_cfg_widescreen; }
int  d3d8_HostIsFullscreen(void) { return s_fullscreen; }

static void shot_lock_init(void)
{
    if (InterlockedCompareExchange(&s_shot_lock_ready, 1, 0) == 0)
        InitializeCriticalSection(&s_shot_lock);
    else
        while (s_shot_lock_ready != 1) Sleep(0);
}

/* Saved as PNG at the end of the next frame, from the scene target. */
void d3d8_RequestScreenshot(const wchar_t *path)
{
    shot_lock_init();
    EnterCriticalSection(&s_shot_lock);
    lstrcpynW(s_shot_path, path ? path : L"", MAX_PATH);
    LeaveCriticalSection(&s_shot_lock);
}

/* A short note in the title bar for a few seconds ("Screenshot saved"). */
void d3d8_HostToast(const wchar_t *text)
{
    lstrcpynW(s_toast, text ? text : L"", 128);
    InterlockedExchange(&s_toast_tick, (LONG)GetTickCount());
}

/* Closing the window ends the game at once: the title has no quit path of
 * its own (a console is switched off), and waiting on its threads is what
 * left the window unclosable -- WM_CLOSE used to set a flag nothing read. */
void d3d8_HostExit(void)
{
    fflush(stdout);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 0);
}

/* Borderless fullscreen on the window's monitor, or back to the saved
 * window. Only the window changes; the swap chain follows the new client
 * size at the next present (host_output_resize), and the scene target --
 * the render resolution -- does not change at all. The menu bar is hidden
 * in fullscreen. Runs on the window's thread. */
static void host_window_set_fullscreen(HWND h, int on)
{
    if (!h || on == s_fullscreen) return;
    if (on) {
        MONITORINFO mi;
        s_windowed_placement.length = sizeof s_windowed_placement;
        GetWindowPlacement(h, &s_windowed_placement);
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        if (s_host_menu) SetMenu(h, NULL);
        SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongPtrW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        if (s_host_menu) SetMenu(h, s_host_menu);
        SetWindowPlacement(h, &s_windowed_placement);
        SetWindowPos(h, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    s_fullscreen = on;
}

void d3d8_HostSetFullscreen(int on)
{
    host_window_set_fullscreen(s_host_window, on ? 1 : 0);
}

/* Resize the window so its client area -- the picture -- is w x h, leaving
 * fullscreen or a maximised state first. Runs on the window's thread. */
void d3d8_HostSetClientSize(unsigned w, unsigned h)
{
    HWND hw = s_host_window;
    RECT r;
    if (!hw || !w || !h) return;
    if (s_fullscreen) host_window_set_fullscreen(hw, 0);
    if (IsZoomed(hw)) ShowWindow(hw, SW_RESTORE);
    r.left = 0; r.top = 0; r.right = (LONG)w; r.bottom = (LONG)h;
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(hw, GWL_STYLE), GetMenu(hw) != NULL,
                       (DWORD)GetWindowLongPtrW(hw, GWL_EXSTYLE));
    SetWindowPos(hw, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

/* Title bar: the name, frames per second if asked for, and any short note. */
static void host_window_update_title(HWND h)
{
    static LONG last_frames = 0;
    static DWORD last_tick = 0;
    static UINT fps = 0;
    WCHAR title[256];
    DWORD now = GetTickCount();
    LONG frames = s_frames;

    if (last_tick && now - last_tick >= 900) {
        fps = (UINT)((frames - last_frames) * 1000u / (now - last_tick));
        last_frames = frames;
        last_tick = now;
    } else if (!last_tick) {
        last_tick = now;
        last_frames = frames;
    }
    lstrcpyW(title, L"SSX Tricky");
    if (s_show_fps) {
        WCHAR part[32];
        wsprintfW(part, L"  \x00B7  %u fps", fps);
        lstrcatW(title, part);
    }
    if (s_toast_tick && now - (DWORD)s_toast_tick < 3000 && s_toast[0]) {
        lstrcatW(title, L"  \x00B7  ");
        lstrcatW(title, s_toast);
    }
    {
        WCHAR cur[256];
        GetWindowTextW(h, cur, 256);
        if (lstrcmpW(cur, title) != 0) SetWindowTextW(h, title);
    }
}

static LRESULT CALLBACK host_wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CLOSE:
            s_host_window_close_requested = TRUE;
            d3d8_HostExit();
            return 0;
        case WM_DESTROY:
            s_host_window = NULL;
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;   /* the renderer owns every pixel; avoid flicker */
        case WM_TIMER:
            host_window_update_title(h);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == 0 && lp == 0 && s_ui_hooks.on_command) {
                s_ui_hooks.on_command(h, LOWORD(wp));
                return 0;
            }
            break;
        case WM_INITMENUPOPUP:
            if (s_ui_hooks.on_init_menu) s_ui_hooks.on_init_menu((HMENU)wp);
            return 0;
        case WM_ENTERMENULOOP:
        case WM_EXITMENULOOP:
            if (s_ui_hooks.on_menu_loop) s_ui_hooks.on_menu_loop(msg == WM_ENTERMENULOOP);
            break;
        case WM_KEYDOWN:
            if (s_ui_hooks.on_key && !(lp & (1 << 30)) && s_ui_hooks.on_key(h, (UINT)wp))
                return 0;
            break;
        case WM_SYSKEYDOWN:
            /* Alt+Enter toggles borderless fullscreen. DXGI's own handler
             * (exclusive mode) is switched off in the device setup. */
            if (wp == VK_RETURN && (lp & (1 << 29))) {
                host_window_set_fullscreen(h, !s_fullscreen);
                return 0;
            }
            break;
        case WM_SYSCHAR:
            if (wp == VK_RETURN) return 0;   /* no "invalid key" beep */
            break;
        case WM_SETCURSOR:
            if (s_fullscreen && LOWORD(lp) == HTCLIENT) {
                SetCursor(NULL);
                return TRUE;
            }
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND host_window_create_here(UINT width, UINT height)
{
    static const WCHAR *cls = L"XboxRecompOutput";
    WNDCLASSEXW wc;
    HINSTANCE inst = GetModuleHandleW(NULL);
    MONITORINFO mi;
    POINT origin = { 0, 0 };
    RECT r;
    HWND h;
    LONG ww, wh, x, y;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = host_wnd_proc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls;
    /* Re-registering is harmless if a previous device init already did it. */
    RegisterClassExW(&wc);

    s_host_menu = s_ui_hooks.create_menu ? s_ui_hooks.create_menu() : NULL;

    /* Size the *client* area to the requested output. A window that would
     * not fit the monitor's work area is shrunk, keeping its shape; the
     * scene is then scaled down into it (and shown 1:1 in fullscreen). */
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    {
        RECT frame = { 0, 0, 0, 0 };
        LONG maxw, maxh;
        AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, s_host_menu != NULL);
        maxw = (mi.rcWork.right - mi.rcWork.left) - (frame.right - frame.left);
        maxh = (mi.rcWork.bottom - mi.rcWork.top) - (frame.bottom - frame.top);
        if (maxw > 0 && maxh > 0 && ((LONG)width > maxw || (LONG)height > maxh)) {
            double kx = (double)maxw / width, ky = (double)maxh / height;
            double k = kx < ky ? kx : ky;
            width  = (UINT)(width * k);
            height = (UINT)(height * k);
        }
    }
    r.left = 0; r.top = 0; r.right = (LONG)width; r.bottom = (LONG)height;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, s_host_menu != NULL);
    ww = r.right - r.left;
    wh = r.bottom - r.top;
    x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;

    h = CreateWindowExW(0, cls, L"SSX Tricky",
                        WS_OVERLAPPEDWINDOW, x, y, ww, wh,
                        NULL, s_host_menu, inst, NULL);
    if (!h) {
        fprintf(stderr, "D3D8: could not create output window (error %lu)\n",
                GetLastError());
        return NULL;
    }

    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    s_host_window = h;
    SetTimer(h, 1, 500, NULL);
    if (s_cfg_fullscreen)
        host_window_set_fullscreen(h, 1);
    fprintf(stderr, "D3D8: created output window %ux%u%s%s (hwnd=%p)\n",
            width, height, s_fullscreen ? ", fullscreen" : "",
            s_host_menu ? ", menu" : "", (void *)h);
    fflush(stderr);
    return h;
}

/*
 * The window lives on a thread of its own (part 183). It used to belong to
 * whichever thread created the device and was only serviced when a frame was
 * presented, so during a load, while a menu was open, or while the window was
 * dragged, nothing answered it -- which is also why closing it did nothing.
 * Now its messages are always handled, and a menu or a resize never stalls
 * the renderer. DXGI is content with a window owned by another thread as long
 * as that thread pumps, and nothing here makes the window thread wait on the
 * render thread.
 */
static HANDLE s_ui_ready = NULL;
static UINT   s_ui_w, s_ui_h;

static DWORD WINAPI host_ui_thread(LPVOID unused)
{
    MSG msg;
    (void)unused;
    host_window_create_here(s_ui_w, s_ui_h);
    SetEvent(s_ui_ready);
    if (!s_host_window) return 1;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

static HWND host_window_create(UINT width, UINT height)
{
    HANDLE t;
    if (!s_start_tick) s_start_tick = GetTickCount();
    if (s_host_window) return s_host_window;
    s_ui_w = width;
    s_ui_h = height;
    s_ui_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    t = CreateThread(NULL, 0, host_ui_thread, NULL, 0, NULL);
    if (!t || !s_ui_ready) return NULL;
    CloseHandle(t);
    WaitForSingleObject(s_ui_ready, 15000);
    return s_host_window;
}

/* ── Scene-to-window scaling ───────────────────────────────────────── */

/* One full-screen triangle sampling a texture: used to scale the scene into
 * the window, and the video player's 640x480 frames into a larger scene. */
static const char s_blit_hlsl[] =
    "Texture2D t : register(t0);\n"
    "SamplerState s : register(s0);\n"
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "    V o;\n"
    "    o.uv = float2((id << 1) & 2, id & 2);\n"
    "    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n"
    "float4 ps(V i) : SV_Target { return float4(t.Sample(s, i.uv).rgb, 1); }\n";

static ID3D11VertexShader    *s_blit_vs;
static ID3D11PixelShader     *s_blit_ps;
static ID3D11SamplerState    *s_blit_smp;
static ID3D11RasterizerState *s_blit_rs;

static int blit_init(void)
{
    static int failed = 0;
    ID3D11Device *dev = g_device_state.d3d11_device;
    ID3D10Blob *code = NULL, *err = NULL;
    D3D11_SAMPLER_DESC sd;
    D3D11_RASTERIZER_DESC rd;

    if (s_blit_vs && s_blit_ps && s_blit_smp && s_blit_rs) return 1;
    if (failed || !dev) return 0;
    failed = 1;   /* cleared on success; never retried every frame */

    if (FAILED(D3DCompile(s_blit_hlsl, sizeof s_blit_hlsl - 1, "blit", NULL, NULL,
                          "vs", "vs_5_0", 0, 0, &code, &err))) goto fail;
    if (FAILED(ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &s_blit_vs))) goto fail;
    ID3D10Blob_Release(code); code = NULL;
    if (FAILED(D3DCompile(s_blit_hlsl, sizeof s_blit_hlsl - 1, "blit", NULL, NULL,
                          "ps", "ps_5_0", 0, 0, &code, &err))) goto fail;
    if (FAILED(ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &s_blit_ps))) goto fail;
    ID3D10Blob_Release(code); code = NULL;

    memset(&sd, 0, sizeof sd);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &s_blit_smp))) goto fail;

    memset(&rd, 0, sizeof rd);
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(ID3D11Device_CreateRasterizerState(dev, &rd, &s_blit_rs))) goto fail;

    failed = 0;
    return 1;
fail:
    if (err) {
        fprintf(stderr, "D3D8: blit shader: %s\n", (const char *)ID3D10Blob_GetBufferPointer(err));
        ID3D10Blob_Release(err);
    } else {
        fprintf(stderr, "D3D8: could not create the scaling blit\n");
    }
    if (code) ID3D10Blob_Release(code);
    fflush(stderr);
    return 0;
}

/* Draw srv into rtv over vp. Everything the blit touches is saved first and
 * put back afterwards: the translator's state cache assumes the pipeline
 * still holds what it last set, so a present must leave no trace. */
static void blit_draw(ID3D11ShaderResourceView *srv, ID3D11RenderTargetView *rtv,
                      const D3D11_VIEWPORT *vp)
{
    ID3D11DeviceContext *ctx = g_device_state.d3d11_context;
    ID3D11RenderTargetView *o_rtv = NULL;
    ID3D11DepthStencilView *o_dsv = NULL;
    D3D11_VIEWPORT o_vp;
    UINT o_nvp = 1;
    ID3D11VertexShader *o_vs = NULL;
    ID3D11PixelShader *o_ps = NULL;
    ID3D11InputLayout *o_il = NULL;
    D3D11_PRIMITIVE_TOPOLOGY o_topo;
    ID3D11ShaderResourceView *o_srv = NULL, *none = NULL;
    ID3D11SamplerState *o_smp = NULL;
    ID3D11BlendState *o_bs = NULL;
    FLOAT o_bf[4];
    UINT o_sm = 0;
    ID3D11DepthStencilState *o_ds = NULL;
    UINT o_sref = 0;
    ID3D11RasterizerState *o_rs = NULL;

    if (!ctx || !srv || !rtv || !blit_init()) return;

    ID3D11DeviceContext_OMGetRenderTargets(ctx, 1, &o_rtv, &o_dsv);
    ID3D11DeviceContext_RSGetViewports(ctx, &o_nvp, &o_vp);
    ID3D11DeviceContext_VSGetShader(ctx, &o_vs, NULL, NULL);
    ID3D11DeviceContext_PSGetShader(ctx, &o_ps, NULL, NULL);
    ID3D11DeviceContext_IAGetInputLayout(ctx, &o_il);
    ID3D11DeviceContext_IAGetPrimitiveTopology(ctx, &o_topo);
    ID3D11DeviceContext_PSGetShaderResources(ctx, 0, 1, &o_srv);
    ID3D11DeviceContext_PSGetSamplers(ctx, 0, 1, &o_smp);
    ID3D11DeviceContext_OMGetBlendState(ctx, &o_bs, o_bf, &o_sm);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx, &o_ds, &o_sref);
    ID3D11DeviceContext_RSGetState(ctx, &o_rs);

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, vp);
    ID3D11DeviceContext_IASetInputLayout(ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, s_blit_vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, s_blit_ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &s_blit_smp);
    ID3D11DeviceContext_OMSetBlendState(ctx, NULL, NULL, 0xFFFFFFFFu);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, NULL, 0);
    ID3D11DeviceContext_RSSetState(ctx, s_blit_rs);
    ID3D11DeviceContext_Draw(ctx, 3, 0);
    /* Unbind the source before its texture can be a render target again. */
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &none);

    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &o_rtv, o_dsv);
    if (o_nvp) ID3D11DeviceContext_RSSetViewports(ctx, 1, &o_vp);
    ID3D11DeviceContext_VSSetShader(ctx, o_vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, o_ps, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx, o_il);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, o_topo);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &o_srv);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &o_smp);
    ID3D11DeviceContext_OMSetBlendState(ctx, o_bs, o_bf, o_sm);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, o_ds, o_sref);
    ID3D11DeviceContext_RSSetState(ctx, o_rs);

    if (o_rtv) ID3D11RenderTargetView_Release(o_rtv);
    if (o_dsv) ID3D11DepthStencilView_Release(o_dsv);
    if (o_vs)  ID3D11VertexShader_Release(o_vs);
    if (o_ps)  ID3D11PixelShader_Release(o_ps);
    if (o_il)  ID3D11InputLayout_Release(o_il);
    if (o_srv) ID3D11ShaderResourceView_Release(o_srv);
    if (o_smp) ID3D11SamplerState_Release(o_smp);
    if (o_bs)  ID3D11BlendState_Release(o_bs);
    if (o_ds)  ID3D11DepthStencilState_Release(o_ds);
    if (o_rs)  ID3D11RasterizerState_Release(o_rs);
}

/* (Re)acquire the swap chain's back buffer and a view of it. */
static HRESULT swap_targets_create(D3D8DeviceState *s)
{
    DXGI_SWAP_CHAIN_DESC d;
    HRESULT hr;

    hr = IDXGISwapChain_GetBuffer(s->swap_chain, 0, &IID_ID3D11Texture2D,
                                  (void **)&s->swap_tex);
    if (FAILED(hr)) return hr;
    hr = ID3D11Device_CreateRenderTargetView(s->d3d11_device,
            (ID3D11Resource *)s->swap_tex, NULL, &s->swap_rtv);
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(IDXGISwapChain_GetDesc(s->swap_chain, &d))) {
        s->swap_w = d.BufferDesc.Width;
        s->swap_h = d.BufferDesc.Height;
    }
    return S_OK;
}

/*
 * The scene target and its depth buffer, at the render size, with `msaa`
 * samples per pixel (part 183). With anti-aliasing the title draws into a
 * multisampled texture that is resolved into scene_tex, the sampleable copy
 * everything downstream reads (the present, captures, screenshots). A count
 * the adapter cannot do falls back to the next lower one.
 */
static void scene_targets_release(D3D8DeviceState *s)
{
    if (s->default_dsv)   { ID3D11DepthStencilView_Release(s->default_dsv); s->default_dsv = NULL; }
    if (s->default_depth) { ID3D11Texture2D_Release(s->default_depth); s->default_depth = NULL; }
    if (s->default_rtv)   { ID3D11RenderTargetView_Release(s->default_rtv); s->default_rtv = NULL; }
    if (s->scene_srv)     { ID3D11ShaderResourceView_Release(s->scene_srv); s->scene_srv = NULL; }
    if (s->scene_ms)      { ID3D11Texture2D_Release(s->scene_ms); s->scene_ms = NULL; }
    if (s->scene_tex)     { ID3D11Texture2D_Release(s->scene_tex); s->scene_tex = NULL; }
}

static HRESULT scene_targets_create(D3D8DeviceState *s, UINT msaa)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ID3D11Device *dev = s->d3d11_device;
    D3D11_TEXTURE2D_DESC td;
    HRESULT hr;

    while (msaa > 1) {
        UINT qc = 0, qd = 0;
        if (SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(dev,
                DXGI_FORMAT_R8G8B8A8_UNORM, msaa, &qc)) && qc &&
            SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(dev,
                DXGI_FORMAT_D24_UNORM_S8_UINT, msaa, &qd)) && qd)
            break;
        msaa >>= 1;
    }
    if (msaa < 1) msaa = 1;

    memset(&td, 0, sizeof td);
    td.Width = s->width;
    td.Height = s->height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &s->scene_tex);
    if (FAILED(hr)) return hr;
    hr = ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)s->scene_tex,
                                               NULL, &s->scene_srv);
    if (FAILED(hr)) return hr;

    if (msaa > 1) {
        td.SampleDesc.Count = msaa;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &s->scene_ms);
        if (FAILED(hr)) return hr;
    }
    hr = ID3D11Device_CreateRenderTargetView(dev,
            (ID3D11Resource *)(s->scene_ms ? s->scene_ms : s->scene_tex),
            NULL, &s->default_rtv);
    if (FAILED(hr)) return hr;

    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = msaa;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &s->default_depth);
    if (FAILED(hr)) return hr;
    hr = ID3D11Device_CreateDepthStencilView(dev, (ID3D11Resource *)s->default_depth,
                                             NULL, &s->default_dsv);
    if (FAILED(hr)) return hr;

    ID3D11DeviceContext_ClearRenderTargetView(s->d3d11_context, s->default_rtv, black);
    ID3D11DeviceContext_ClearDepthStencilView(s->d3d11_context, s->default_dsv,
                                              D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(s->d3d11_context, 1, &s->default_rtv, s->default_dsv);
    s->msaa = msaa;
    return S_OK;
}

/* Bring scene_tex up to date with a multisampled frame. */
static void scene_resolve(D3D8DeviceState *s)
{
    if (s->scene_ms && s->scene_tex)
        ID3D11DeviceContext_ResolveSubresource(s->d3d11_context,
            (ID3D11Resource *)s->scene_tex, 0, (ID3D11Resource *)s->scene_ms, 0,
            DXGI_FORMAT_R8G8B8A8_UNORM);
}

/* Follow the window: a resize, maximise or fullscreen toggle changes the
 * client area, and the swap chain is resized to match so the output is never
 * stretched by the window system. A minimised window keeps its buffers. */
static void host_output_resize(void)
{
    D3D8DeviceState *s = &g_device_state;
    RECT rc;
    UINT cw, ch;
    HRESULT hr;

    if (!s->swap_chain || !s->hwnd || !GetClientRect(s->hwnd, &rc)) return;
    cw = (UINT)(rc.right - rc.left);
    ch = (UINT)(rc.bottom - rc.top);
    if (!cw || !ch) return;
    if (s->swap_rtv && cw == s->swap_w && ch == s->swap_h) return;

    if (s->swap_rtv) { ID3D11RenderTargetView_Release(s->swap_rtv); s->swap_rtv = NULL; }
    if (s->swap_tex) { ID3D11Texture2D_Release(s->swap_tex); s->swap_tex = NULL; }
    hr = IDXGISwapChain_ResizeBuffers(s->swap_chain, 0, cw, ch, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        fprintf(stderr, "D3D8: resizing the output to %ux%u failed: 0x%08lX\n",
                cw, ch, (unsigned long)hr);
    swap_targets_create(s);
}

/* Write scene_tex as a PNG through WIC (part 183). */
static HRESULT save_scene_png(D3D8DeviceState *s, const WCHAR *path)
{
    ID3D11Texture2D *stage = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE m;
    IWICImagingFactory *f = NULL;
    IWICStream *st = NULL;
    IWICBitmapEncoder *enc = NULL;
    IWICBitmapFrameEncode *fr = NULL;
    IPropertyBag2 *props = NULL;
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    BYTE *bgr = NULL;
    UINT stride, x, y;
    HRESULT hr, co;

    if (!s->scene_tex) return E_FAIL;
    ID3D11Texture2D_GetDesc(s->scene_tex, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    hr = ID3D11Device_CreateTexture2D(s->d3d11_device, &td, NULL, &stage);
    if (FAILED(hr)) return hr;
    ID3D11DeviceContext_CopyResource(s->d3d11_context, (ID3D11Resource *)stage,
                                     (ID3D11Resource *)s->scene_tex);
    hr = ID3D11DeviceContext_Map(s->d3d11_context, (ID3D11Resource *)stage, 0,
                                 D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) { ID3D11Texture2D_Release(stage); return hr; }
    stride = (td.Width * 3 + 3) & ~3u;
    bgr = (BYTE *)malloc((size_t)stride * td.Height);
    if (bgr) {
        for (y = 0; y < td.Height; y++) {
            const BYTE *src = (const BYTE *)m.pData + (size_t)y * m.RowPitch;
            BYTE *dst = bgr + (size_t)y * stride;
            for (x = 0; x < td.Width; x++) {
                dst[x * 3 + 0] = src[x * 4 + 2];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 0];
            }
        }
    }
    ID3D11DeviceContext_Unmap(s->d3d11_context, (ID3D11Resource *)stage, 0);
    ID3D11Texture2D_Release(stage);
    if (!bgr) return E_OUTOFMEMORY;

    co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&f);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateStream(f, &st);
    if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromFilename(st, path, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &enc);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, (IStream *)st, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &fr, &props);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(fr, props);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(fr, td.Width, td.Height);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(fr, &pf);
    if (SUCCEEDED(hr) && !IsEqualGUID(&pf, &GUID_WICPixelFormat24bppBGR)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WritePixels(fr, td.Height, stride,
                                                              stride * td.Height, bgr);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(fr);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
    if (props) IPropertyBag2_Release(props);
    if (fr)    IWICBitmapFrameEncode_Release(fr);
    if (enc)   IWICBitmapEncoder_Release(enc);
    if (st)    IWICStream_Release(st);
    if (f)     IWICImagingFactory_Release(f);
    if (SUCCEEDED(co)) CoUninitialize();
    free(bgr);
    return hr;
}

/* Show the finished scene: fit it, at its display aspect, into the window --
 * centred, with black bars where the window's shape differs -- then present.
 * A 1:1 fit is a plain copy, which is every automated run. Settings changed
 * from the window's menu take effect here, between frames. */
static HRESULT host_present(void)
{
    D3D8DeviceState *s = &g_device_state;
    ID3D11DeviceContext *ctx = s->d3d11_context;
    HRESULT hr;

    if (!s->swap_chain) return E_FAIL;
    if (ctx && s->scene_tex) {
        scene_resolve(s);

        /* A screenshot, if one was asked for. */
        if (s_shot_lock_ready == 1) {
            WCHAR path[MAX_PATH];
            EnterCriticalSection(&s_shot_lock);
            lstrcpynW(path, s_shot_path, MAX_PATH);
            s_shot_path[0] = 0;
            LeaveCriticalSection(&s_shot_lock);
            if (path[0]) {
                HRESULT sh = save_scene_png(s, path);
                d3d8_HostToast(SUCCEEDED(sh) ? L"Screenshot saved" : L"Screenshot failed");
                fprintf(stderr, "D3D8: screenshot %ls: 0x%08lX\n", path, (unsigned long)sh);
            }
        }

        host_output_resize();
        if (s->swap_rtv && s->swap_tex && s->swap_w && s->swap_h) {
            const double aspect = s_cfg_widescreen ? 16.0 / 9.0 : 4.0 / 3.0;
            UINT w = s->swap_w, h = s->swap_h, rw, rh, rx, ry;
            if ((double)w / (double)h > aspect) {
                rh = h; rw = (UINT)(h * aspect + 0.5);
            } else {
                rw = w; rh = (UINT)(w / aspect + 0.5);
            }
            if (rw > w) rw = w;
            if (rh > h) rh = h;
            rx = (w - rw) / 2;
            ry = (h - rh) / 2;
            if (rw != w || rh != h) {
                static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                ID3D11DeviceContext_ClearRenderTargetView(ctx, s->swap_rtv, black);
            }
            if (rw == s->width && rh == s->height) {
                ID3D11DeviceContext_CopySubresourceRegion(ctx,
                    (ID3D11Resource *)s->swap_tex, 0, rx, ry, 0,
                    (ID3D11Resource *)s->scene_tex, 0, NULL);
            } else {
                D3D11_VIEWPORT vp;
                vp.TopLeftX = (FLOAT)rx; vp.TopLeftY = (FLOAT)ry;
                vp.Width = (FLOAT)rw;    vp.Height = (FLOAT)rh;
                vp.MinDepth = 0.0f;      vp.MaxDepth = 1.0f;
                blit_draw(s->scene_srv, s->swap_rtv, &vp);
            }
        }
    }
    {
        /* Sync interval (part 183). The title paces itself to its own 60 Hz
         * frame timer and vertical blank; waiting for the monitor's refresh
         * as well blocked the render thread whenever two of the title's
         * flips fell inside one refresh -- the thread the title's fences
         * wait on -- and cost frames. The window is composed by the desktop,
         * so presenting without waiting does not tear. XBOX_VSYNC=1 waits. */
        static int vsync = -1;
        if (vsync < 0) { const char *e = getenv("XBOX_VSYNC"); vsync = e && e[0] == '1'; }
        static int plog = -1;
        LARGE_INTEGER p0, p1, pf;
        if (plog < 0) { const char *e = getenv("XBOX_FLIP_LOG"); plog = e && e[0] == '1'; }
        if (plog) QueryPerformanceCounter(&p0);
        hr = IDXGISwapChain_Present(s->swap_chain, vsync ? 1 : 0, 0);
        if (plog) {     /* XBOX_FLIP_LOG=1: how long Present itself blocks */
            QueryPerformanceCounter(&p1);
            QueryPerformanceFrequency(&pf);
            fprintf(stderr, "[PRES] t=%.1f present %.2f ms\n",
                    (double)p0.QuadPart * 1000.0 / (double)pf.QuadPart,
                    (double)(p1.QuadPart - p0.QuadPart) * 1000.0 / (double)pf.QuadPart);
        }
    }
    InterlockedIncrement(&s_frames);
    {   /* XBOX_FPS_LOG=1: frames per second every 2 s, for measuring */
        static int on = -1;
        static DWORD t0 = 0;
        static LONG f0 = 0;
        if (on < 0) { const char *e = getenv("XBOX_FPS_LOG"); on = e && e[0] == '1'; }
        if (on) {
            DWORD now = GetTickCount();
            {   /* histogram of present intervals: <12, 12-20, 20-28, 28-40, >40 ms */
                static LARGE_INTEGER qf, qlast;
                static unsigned hist[5];
                LARGE_INTEGER qn;
                double ms;
                if (!qf.QuadPart) QueryPerformanceFrequency(&qf);
                QueryPerformanceCounter(&qn);
                if (qlast.QuadPart) {
                    ms = (double)(qn.QuadPart - qlast.QuadPart) * 1000.0 / (double)qf.QuadPart;
                    hist[ms < 12 ? 0 : ms < 20 ? 1 : ms < 28 ? 2 : ms < 40 ? 3 : 4]++;
                    if (ms >= 40) {
                        /* XBOX_PROFILE=...,stall attributes samples in this window */
                        extern void xbox_profile_note_stall(LONGLONG, LONGLONG);
                        xbox_profile_note_stall(qlast.QuadPart, qn.QuadPart);
                        if (getenv("XBOX_WAIT_LOG"))
                            fprintf(stderr, "[STALL] t=%.1f..%.1f %.1f ms\n",
                                    (double)qlast.QuadPart * 1000.0 / (double)qf.QuadPart,
                                    (double)qn.QuadPart * 1000.0 / (double)qf.QuadPart, ms);
                    }
                }
                qlast = qn;
                if (t0 && now - t0 >= 2000) {
                    extern volatile long g_nv_compiles;
                    extern volatile double g_nv_compile_ms;
                    fprintf(stderr, "[FPS] intervals <12:%u 12-20:%u 20-28:%u 28-40:%u >40:%u; "
                            "%ld shader compiles, %.0f ms; %ld texture uploads, %.0f ms\n",
                            hist[0], hist[1], hist[2], hist[3], hist[4],
                            g_nv_compiles, g_nv_compile_ms, g_tex_uploads_log, g_tex_upload_ms);
                    g_nv_compiles = 0;
                    g_nv_compile_ms = 0;
                    g_tex_uploads_log = 0;
                    g_tex_upload_ms = 0;
                    memset(hist, 0, sizeof hist);
                }
            }
            if (!t0) { t0 = now; f0 = s_frames; }
            else if (now - t0 >= 2000) {
                fprintf(stderr, "[FPS] %.1f at %.0f s\n", (s_frames - f0) * 1000.0 / (now - t0),
                        now / 1000.0 - s_start_tick / 1000.0);
                t0 = now; f0 = s_frames;
            }
        }
    }

    /* Anti-aliasing changed from the menu: rebuild the scene targets now,
     * between frames. The next frame starts with the title's own clear. */
    if (ctx && s->scene_tex && (UINT)s_want_msaa != s->msaa_requested) {
        UINT want = (UINT)s_want_msaa;
        s->msaa_requested = want;
        scene_targets_release(s);
        if (FAILED(scene_targets_create(s, want))) {
            scene_targets_release(s);
            scene_targets_create(s, 1);
        }
        fprintf(stderr, "D3D8: anti-aliasing now %ux\n", s->msaa);
    }
    return hr;
}

static HRESULT d3d11_create_device_and_swap_chain(
    D3D8DeviceState *state,
    D3DPRESENT_PARAMETERS *pp)
{
    DXGI_SWAP_CHAIN_DESC scd;
    D3D_FEATURE_LEVEL feature_level;
    UINT create_flags = 0;
    HRESULT hr;

#ifdef _DEBUG
    create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    /* XBOX_D3D_DEBUG=1 turns the validation layer on in a release build.
     * Every stage of the draw audits clean -- correct geometry, states,
     * viewport, RTV, shaders, input layout -- and the back buffer is still
     * black, including for a hardcoded quad. At that point the runtime's own
     * diagnosis is worth more than further inspection. */
    {
        const char *e = getenv("XBOX_D3D_DEBUG");
        if (e && e[0] == '1')
            create_flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    /* The title's own backbuffer size, and the scene target it is rendered
     * into (part 183): the configured render size, or the title's size. */
    state->guest_w = pp->BackBufferWidth  ? pp->BackBufferWidth  : 640;
    state->guest_h = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
    state->width   = s_cfg_render_w ? s_cfg_render_w : state->guest_w;
    state->height  = s_cfg_render_h ? s_cfg_render_h : state->guest_h;

    memset(&scd, 0, sizeof(scd));
    scd.BufferCount = pp->BackBufferCount ? pp->BackBufferCount : 1;
    /* Zero: the swap chain takes the window's client size, which is the
     * render size unless the window had to be shrunk to fit the monitor or
     * starts fullscreen. host_output_resize keeps it matched afterwards. */
    scd.BufferDesc.Width = 0;
    scd.BufferDesc.Height = 0;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    /* The title runs on hardware with no window system, so it supplies no
     * output window. Make one sized to the frame it will be shown at. */
    scd.OutputWindow = (pp->hDeviceWindow && IsWindow(pp->hDeviceWindow))
                     ? pp->hDeviceWindow
                     : host_window_create(state->width, state->height);
    if (!scd.OutputWindow) return E_FAIL;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    /* Always windowed: an exclusive-fullscreen swap chain on a synthesised
     * window would take over the display before the game has drawn a frame.
     * Fullscreen is a borderless window instead (host_window_set_fullscreen). */
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        create_flags,
        NULL, 0,
        D3D11_SDK_VERSION,
        &scd,
        &state->swap_chain,
        &state->d3d11_device,
        &feature_level,
        &state->d3d11_context
    );

    if (FAILED(hr)) {
        fprintf(stderr, "D3D8: Failed to create D3D11 device: 0x%08lX\n", hr);
        return hr;
    }

    state->hwnd = scd.OutputWindow;

    /* Alt+Enter is ours (borderless toggle in host_wnd_proc); DXGI's would
     * switch to exclusive mode behind the renderer's back. */
    {
        IDXGIFactory *f = NULL;
        if (SUCCEEDED(IDXGISwapChain_GetParent(state->swap_chain, &IID_IDXGIFactory,
                                               (void **)&f)) && f) {
            IDXGIFactory_MakeWindowAssociation(f, state->hwnd, DXGI_MWA_NO_ALT_ENTER);
            IDXGIFactory_Release(f);
        }
    }

    return S_OK;
}

static HRESULT d3d11_create_render_targets(D3D8DeviceState *state)
{
    HRESULT hr;

    /* The scene target: what the title renders into, at the render size,
     * with the chosen anti-aliasing; sampleable so a present can scale it. */
    state->msaa_requested = (UINT)s_want_msaa;
    hr = scene_targets_create(state, state->msaa_requested);
    if (FAILED(hr)) {
        scene_targets_release(state);
        hr = scene_targets_create(state, 1);
        if (FAILED(hr)) return hr;
    }

    /* The window's output buffer. */
    hr = swap_targets_create(state);
    if (FAILED(hr)) return hr;
    fprintf(stderr, "D3D8: rendering %ux%u (title frame %ux%u), output %ux%u, %s, %ux AA\n",
            state->width, state->height, state->guest_w, state->guest_h,
            state->swap_w, state->swap_h, s_cfg_widescreen ? "16:9" : "4:3", state->msaa);
    fflush(stderr);
    return S_OK;
}

static void d3d8_init_default_states(D3D8DeviceState *state)
{
    /* Set Xbox D3D8 default render states */
    memset(state->render_states, 0, sizeof(state->render_states));
    state->render_states[D3DRS_ZENABLE]           = 1;
    state->render_states[D3DRS_FILLMODE]          = D3DFILL_SOLID;
    state->render_states[D3DRS_SHADEMODE]         = 2; /* D3DSHADE_GOURAUD */
    state->render_states[D3DRS_ZWRITEENABLE]      = TRUE;
    state->render_states[D3DRS_ALPHATESTENABLE]    = FALSE;
    state->render_states[D3DRS_SRCBLEND]          = D3DBLEND_ONE;
    state->render_states[D3DRS_DESTBLEND]         = D3DBLEND_ZERO;
    state->render_states[D3DRS_CULLMODE]          = D3DCULL_CCW;
    state->render_states[D3DRS_ZFUNC]             = D3DCMP_LESSEQUAL;
    state->render_states[D3DRS_ALPHAREF]          = 0;
    state->render_states[D3DRS_ALPHAFUNC]         = D3DCMP_ALWAYS;
    state->render_states[D3DRS_ALPHABLENDENABLE]   = FALSE;
    state->render_states[D3DRS_FOGENABLE]         = FALSE;
    state->render_states[D3DRS_STENCILENABLE]     = FALSE;
    state->render_states[D3DRS_COLORWRITEENABLE]  = 0x0F;

    /* Texture stage state defaults, per the D3D8 documented table.
     *
     * These were never initialised -- the table was left zeroed -- so every
     * consumer had to guess whether a zero meant 'unset' or a real value, and
     * did it with `tss[X] ? tss[X] : SOME_DEFAULT`. That idiom cannot express
     * D3DTA_DIFFUSE, which *is* zero: setting COLORARG1 to DIFFUSE read back as
     * TEXTURE, so a draw asking for vertex colour sampled an unbound texture
     * instead and came out transparent black. Seeding the real defaults here
     * lets every consumer just read the table.
     */
    {
        int st;
        memset(state->tss, 0, sizeof(state->tss));
        for (st = 0; st < MAX_TEXTURE_STAGES; st++) {
            state->tss[st][D3DTSS_COLOROP]   = (st == 0) ? D3DTOP_MODULATE
                                                         : D3DTOP_DISABLE;
            state->tss[st][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
            state->tss[st][D3DTSS_COLORARG2] = D3DTA_CURRENT;
            state->tss[st][D3DTSS_ALPHAOP]   = (st == 0) ? D3DTOP_SELECTARG1
                                                         : D3DTOP_DISABLE;
            state->tss[st][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
            state->tss[st][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
            state->tss[st][D3DTSS_TEXCOORDINDEX] = (DWORD)st;
            state->tss[st][D3DTSS_ADDRESSU]  = 1; /* D3DTADDRESS_WRAP */
            state->tss[st][D3DTSS_ADDRESSV]  = 1;
            state->tss[st][D3DTSS_MAGFILTER] = 1; /* D3DTEXF_POINT */
            state->tss[st][D3DTSS_MINFILTER] = 1;
            state->tss[st][D3DTSS_MIPFILTER] = 0; /* D3DTEXF_NONE */
        }
    }

    /* Default viewport */
    state->viewport.X = 0;
    state->viewport.Y = 0;
    state->viewport.Width = state->width;
    state->viewport.Height = state->height;
    state->viewport.MinZ = 0.0f;
    state->viewport.MaxZ = 1.0f;

    /* Identity matrices */
    for (int i = 0; i < MAX_TRANSFORMS; i++) {
        memset(&state->transforms[i], 0, sizeof(D3DMATRIX));
        state->transforms[i]._11 = 1.0f;
        state->transforms[i]._22 = 1.0f;
        state->transforms[i]._33 = 1.0f;
        state->transforms[i]._44 = 1.0f;
    }

    state->vertex_shader = 0;
    state->pixel_shader = 0;
    state->in_scene = FALSE;
}

/* ================================================================
 * IDirect3DDevice8 method implementations
 * ================================================================ */

static HRESULT __stdcall dev_QueryInterface(IDirect3DDevice8 *self, const IID *riid, void **ppv)
{
    (void)self; (void)riid; (void)ppv;
    return E_NOINTERFACE;
}

static ULONG __stdcall dev_AddRef(IDirect3DDevice8 *self)
{
    (void)self;
    return InterlockedIncrement(&g_device_state.ref_count);
}

static ULONG __stdcall dev_Release(IDirect3DDevice8 *self)
{
    (void)self;
    LONG ref = InterlockedDecrement(&g_device_state.ref_count);
    if (ref <= 0) {
        /* Cleanup subsystems first */
        up_ring_shutdown();
        d3d8_vsh_shutdown();
        d3d8_combiners_shutdown();
        d3d8_states_shutdown();
        d3d8_shaders_shutdown();

        /* Cleanup D3D11 resources */
        D3D8DeviceState *s = &g_device_state;
        if (s->default_dsv) { ID3D11DepthStencilView_Release(s->default_dsv); s->default_dsv = NULL; }
        if (s->default_depth) { ID3D11Texture2D_Release(s->default_depth); s->default_depth = NULL; }
        if (s->default_rtv) { ID3D11RenderTargetView_Release(s->default_rtv); s->default_rtv = NULL; }
        if (s->scene_srv) { ID3D11ShaderResourceView_Release(s->scene_srv); s->scene_srv = NULL; }
        if (s->scene_tex) { ID3D11Texture2D_Release(s->scene_tex); s->scene_tex = NULL; }
        if (s->scene_ms) { ID3D11Texture2D_Release(s->scene_ms); s->scene_ms = NULL; }
        if (s->swap_rtv) { ID3D11RenderTargetView_Release(s->swap_rtv); s->swap_rtv = NULL; }
        if (s->swap_tex) { ID3D11Texture2D_Release(s->swap_tex); s->swap_tex = NULL; }
        if (s->swap_chain) { IDXGISwapChain_Release(s->swap_chain); s->swap_chain = NULL; }
        if (s->d3d11_context) { ID3D11DeviceContext_Release(s->d3d11_context); s->d3d11_context = NULL; }
        if (s->d3d11_device) { ID3D11Device_Release(s->d3d11_device); s->d3d11_device = NULL; }
        g_device_initialized = FALSE;
    }
    return (ULONG)ref;
}

static HRESULT __stdcall dev_GetDirect3D(IDirect3DDevice8 *self, IDirect3D8 **ppD3D8)
{
    (void)self; (void)ppD3D8;
    /* TODO: return the factory */
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_GetDeviceCaps(IDirect3DDevice8 *self, void *pCaps)
{
    (void)self; (void)pCaps;
    /* TODO: fill with Xbox NV2A capabilities */
    return S_OK;
}

static HRESULT __stdcall dev_GetDisplayMode(IDirect3DDevice8 *self, void *pMode)
{
    (void)self; (void)pMode;
    return S_OK;
}

static HRESULT __stdcall dev_GetCreationParameters(IDirect3DDevice8 *self, void *pParams)
{
    (void)self; (void)pParams;
    return S_OK;
}

static HRESULT __stdcall dev_Reset(IDirect3DDevice8 *self, D3DPRESENT_PARAMETERS *pPP)
{
    (void)self; (void)pPP;
    /* TODO: resize swap chain */
    return S_OK;
}

static DWORD g_d3d_begin_count = 0;
static DWORD g_d3d_end_count = 0;
static DWORD g_d3d_clear_count = 0;
static DWORD g_d3d_draw_count = 0;
static DWORD g_d3d_settransform_count = 0;
static DWORD g_d3d_setrs_count = 0;
static DWORD g_d3d_settexture_count = 0;

static HRESULT __stdcall dev_Present(IDirect3DDevice8 *self, const RECT *src, const RECT *dst, HWND hWnd, void *pDirty)
{
    static DWORD frame_count = 0;
    static DWORD last_tick = 0;
    (void)self; (void)src; (void)dst; (void)hWnd; (void)pDirty;

    frame_count++;
    DWORD now = GetTickCount();
    if (last_tick == 0) last_tick = now;
    if (now - last_tick >= 2000) {
        fprintf(stderr, "  [D3D] %.1fs: %u present (%.1f fps), %u begin, %u end, "
                "%u clear, %u draw, %u xform, %u rs, %u tex\n",
                (now - last_tick) / 1000.0, frame_count,
                frame_count * 1000.0 / (now - last_tick),
                g_d3d_begin_count, g_d3d_end_count,
                g_d3d_clear_count, g_d3d_draw_count,
                g_d3d_settransform_count, g_d3d_setrs_count,
                g_d3d_settexture_count);
        fflush(stderr);
        frame_count = 0;
        g_d3d_begin_count = g_d3d_end_count = 0;
        g_d3d_clear_count = g_d3d_draw_count = 0;
        g_d3d_settransform_count = g_d3d_setrs_count = 0;
        g_d3d_settexture_count = 0;
        last_tick = now;
    }

    /* Pump Windows messages: the game's internal main loop drives rendering,
     * so our external message pump never runs. Process messages here to keep
     * the window responsive and handle input. */
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ExitProcess(0);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    { static unsigned c = 0; if (++c == 1 || (c % 300) == 0) { fprintf(stderr, "  [PRESENT] dev_Present #%u\n", c); fflush(stderr); } }
    return host_present();
}

static HRESULT __stdcall dev_GetBackBuffer(IDirect3DDevice8 *self, INT iBackBuffer, DWORD Type, IDirect3DSurface8 **ppSurface)
{
    (void)self; (void)iBackBuffer; (void)Type; (void)ppSurface;
    /* TODO: wrap back buffer as D3D8 surface */
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_BeginScene(IDirect3DDevice8 *self)
{
    (void)self;
    g_device_state.in_scene = TRUE;
    g_d3d_begin_count++;
    return S_OK;
}

static HRESULT __stdcall dev_EndScene(IDirect3DDevice8 *self)
{
    (void)self;
    g_device_state.in_scene = FALSE;
    g_d3d_end_count++;
    return S_OK;
}

static HRESULT __stdcall dev_Clear(IDirect3DDevice8 *self, DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
    (void)self; (void)Count; (void)pRects; (void)Stencil;
    g_d3d_clear_count++;

    if (Flags & D3DCLEAR_TARGET) {
        float clear_color[4] = {
            ((Color >> 16) & 0xFF) / 255.0f,  /* R */
            ((Color >>  8) & 0xFF) / 255.0f,  /* G */
            ((Color >>  0) & 0xFF) / 255.0f,  /* B */
            ((Color >> 24) & 0xFF) / 255.0f,  /* A */
        };
        ID3D11DeviceContext_ClearRenderTargetView(g_device_state.d3d11_context,
                                                   g_device_state.default_rtv,
                                                   clear_color);
    }

    if (Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) {
        UINT clear_flags = 0;
        if (Flags & D3DCLEAR_ZBUFFER) clear_flags |= D3D11_CLEAR_DEPTH;
        if (Flags & D3DCLEAR_STENCIL) clear_flags |= D3D11_CLEAR_STENCIL;

        ID3D11DeviceContext_ClearDepthStencilView(g_device_state.d3d11_context,
                                                    g_device_state.default_dsv,
                                                    clear_flags, Z, (UINT8)Stencil);
    }

    return S_OK;
}

static HRESULT __stdcall dev_SetTransform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix)
{
    (void)self;
    g_d3d_settransform_count++;
    if ((DWORD)State < MAX_TRANSFORMS && pMatrix) {
        g_device_state.transforms[(DWORD)State] = *pMatrix;
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetTransform(IDirect3DDevice8 *self, D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix)
{
    (void)self;
    if ((DWORD)State < MAX_TRANSFORMS && pMatrix) {
        *pMatrix = g_device_state.transforms[(DWORD)State];
    }
    return S_OK;
}

static HRESULT __stdcall dev_SetRenderState(IDirect3DDevice8 *self, D3DRENDERSTATETYPE State, DWORD Value)
{
    (void)self;
    g_d3d_setrs_count++;
    if ((DWORD)State < MAX_RENDER_STATES) {
        g_device_state.render_states[(DWORD)State] = Value;
    }
    /* Mark combiner state dirty if any PS register combiner state changed */
    if ((DWORD)State >= D3DRS_PSALPHAINPUTS0 && (DWORD)State <= D3DRS_PSINPUTTEXTURE) {
        d3d8_combiners_mark_dirty();
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetRenderState(IDirect3DDevice8 *self, D3DRENDERSTATETYPE State, DWORD *pValue)
{
    (void)self;
    if ((DWORD)State < MAX_RENDER_STATES && pValue) {
        *pValue = g_device_state.render_states[(DWORD)State];
    }
    return S_OK;
}

static HRESULT __stdcall dev_SetTextureStageState(IDirect3DDevice8 *self, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    (void)self;
    if (Stage < MAX_TEXTURE_STAGES && (DWORD)Type < MAX_TSS_STATES) {
        g_device_state.tss[Stage][(DWORD)Type] = Value;
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetTextureStageState(IDirect3DDevice8 *self, DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
{
    (void)self;
    if (Stage < MAX_TEXTURE_STAGES && (DWORD)Type < MAX_TSS_STATES && pValue) {
        *pValue = g_device_state.tss[Stage][(DWORD)Type];
    }
    return S_OK;
}

static HRESULT __stdcall dev_SetTexture(IDirect3DDevice8 *self, DWORD Stage, IDirect3DBaseTexture8 *pTexture)
{
    (void)self;
    g_d3d_settexture_count++;
    if (Stage >= 4) return E_INVALIDARG;
    g_cur_textures[Stage] = pTexture;

    /* Bind SRV to pixel shader */
    if (pTexture) {
        D3D8Texture *tex = (D3D8Texture *)pTexture;
        if (tex->srv) {
            ID3D11DeviceContext_PSSetShaderResources(g_device_state.d3d11_context,
                Stage, 1, &tex->srv);
        }
        /* Mark texture stage as active */
        if (g_device_state.tss[Stage][D3DTSS_COLOROP] == D3DTOP_DISABLE)
            g_device_state.tss[Stage][D3DTSS_COLOROP] = D3DTOP_MODULATE;
    } else {
        ID3D11ShaderResourceView *null_srv = NULL;
        ID3D11DeviceContext_PSSetShaderResources(g_device_state.d3d11_context,
            Stage, 1, &null_srv);
        g_device_state.tss[Stage][D3DTSS_COLOROP] = D3DTOP_DISABLE;
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetTexture(IDirect3DDevice8 *self, DWORD Stage, IDirect3DBaseTexture8 **ppTexture)
{
    (void)self; (void)Stage; (void)ppTexture;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_SetStreamSource(IDirect3DDevice8 *self, UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride)
{
    (void)self;
    if (StreamNumber != 0) return S_OK; /* Only stream 0 supported */
    g_cur_vb = pStreamData;
    g_cur_vb_stride = Stride;

    if (pStreamData) {
        D3D8VertexBuffer *vb = (D3D8VertexBuffer *)pStreamData;
        UINT offset = 0;
        ID3D11DeviceContext_IASetVertexBuffers(g_device_state.d3d11_context,
            0, 1, &vb->d3d11_buffer, &Stride, &offset);
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetStreamSource(IDirect3DDevice8 *self, UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData, UINT *pStride)
{
    (void)self; (void)StreamNumber; (void)ppStreamData; (void)pStride;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_SetIndices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex)
{
    (void)self;
    g_cur_ib = pIndexData;
    g_cur_ib_base_vertex = BaseVertexIndex;

    if (pIndexData) {
        D3D8IndexBuffer *ib = (D3D8IndexBuffer *)pIndexData;
        DXGI_FORMAT fmt = (ib->format == D3DFMT_INDEX32)
            ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        ID3D11DeviceContext_IASetIndexBuffer(g_device_state.d3d11_context,
            ib->d3d11_buffer, fmt, 0);
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetIndices(IDirect3DDevice8 *self, IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex)
{
    (void)self; (void)ppIndexData; (void)pBaseVertexIndex;
    return E_NOTIMPL;
}

static D3D11_PRIMITIVE_TOPOLOGY map_primitive_type(D3DPRIMITIVETYPE pt, UINT count, UINT *out_count)
{
    switch (pt) {
    case D3DPT_TRIANGLELIST:  *out_count = count * 3; return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case D3DPT_TRIANGLESTRIP: *out_count = count + 2; return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case D3DPT_TRIANGLEFAN:   *out_count = count * 3; return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case D3DPT_LINELIST:      *out_count = count * 2; return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case D3DPT_LINESTRIP:     *out_count = count + 1; return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case D3DPT_POINTLIST:     *out_count = count;     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case D3DPT_QUADLIST:      *out_count = count * 6; return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    default:                  *out_count = 0;          return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }
}

/* ================================================================
 * Triangle fan / quad list → triangle list conversion
 *
 * D3D11 doesn't support triangle fans or quad lists.
 * Convert vertex data in-place to triangle list.
 * Returns malloc'd buffer (caller must free) or NULL if no conversion needed.
 * ================================================================ */

static void *convert_fan_or_quad(D3DPRIMITIVETYPE pt, const void *src,
                                  UINT prim_count, UINT stride,
                                  UINT *out_vertex_count)
{
    BYTE *dst;
    const BYTE *s = (const BYTE *)src;
    UINT i;

    if (pt == D3DPT_TRIANGLEFAN) {
        /* Fan: vertex 0 is the hub, each triangle is (0, i+1, i+2) */
        UINT tri_verts = prim_count * 3;
        dst = (BYTE *)malloc(tri_verts * stride);
        if (!dst) return NULL;

        for (i = 0; i < prim_count; i++) {
            memcpy(dst + (i * 3 + 0) * stride, s, stride);                      /* v0 (hub) */
            memcpy(dst + (i * 3 + 1) * stride, s + (i + 1) * stride, stride);   /* v[i+1] */
            memcpy(dst + (i * 3 + 2) * stride, s + (i + 2) * stride, stride);   /* v[i+2] */
        }
        *out_vertex_count = tri_verts;
        return dst;
    }

    if (pt == D3DPT_QUADLIST) {
        /* Quad list: each quad (v0,v1,v2,v3) → 2 triangles (v0,v1,v2), (v0,v2,v3) */
        UINT tri_verts = prim_count * 6;
        dst = (BYTE *)malloc(tri_verts * stride);
        if (!dst) return NULL;

        for (i = 0; i < prim_count; i++) {
            const BYTE *q = s + i * 4 * stride;
            memcpy(dst + (i * 6 + 0) * stride, q + 0 * stride, stride);  /* v0 */
            memcpy(dst + (i * 6 + 1) * stride, q + 1 * stride, stride);  /* v1 */
            memcpy(dst + (i * 6 + 2) * stride, q + 2 * stride, stride);  /* v2 */
            memcpy(dst + (i * 6 + 3) * stride, q + 0 * stride, stride);  /* v0 */
            memcpy(dst + (i * 6 + 4) * stride, q + 2 * stride, stride);  /* v2 */
            memcpy(dst + (i * 6 + 5) * stride, q + 3 * stride, stride);  /* v3 */
        }
        *out_vertex_count = tri_verts;
        return dst;
    }

    return NULL; /* no conversion needed */
}

/* ================================================================
 * DrawPrimitiveUP ring buffer
 *
 * Instead of creating and destroying a D3D11 buffer on every
 * DrawPrimitiveUP call, use a persistent ring buffer.
 * ================================================================ */

/* Part 182: 32 MB. The program-draw path (d3d8_nv2a.c) uploads expanded
 * 116-byte vertices; character select alone sends ~1,000 draws a frame, well
 * past the old 4 MB, so the ring wrapped several times per frame. */
#define UP_RING_BUFFER_SIZE (32 * 1024 * 1024)

static ID3D11Buffer *g_up_ring_buffer = NULL;
static UINT          g_up_ring_offset = 0;

static HRESULT up_ring_init(void)
{
    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = UP_RING_BUFFER_SIZE;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return ID3D11Device_CreateBuffer(g_device_state.d3d11_device, &bd, NULL, &g_up_ring_buffer);
}

static void up_ring_shutdown(void)
{
    if (g_up_ring_buffer) {
        ID3D11Buffer_Release(g_up_ring_buffer);
        g_up_ring_buffer = NULL;
    }
    g_up_ring_offset = 0;
}

/* Upload vertex data to ring buffer, returns offset. Returns (UINT)-1 on failure. */
static UINT up_ring_upload(const void *data, UINT size)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D11_MAP map_type;
    HRESULT hr;
    UINT offset;

    if (!g_up_ring_buffer) {
        if (FAILED(up_ring_init())) return (UINT)-1;
    }

    if (size > UP_RING_BUFFER_SIZE) return (UINT)-1;

    /* Wrap around if not enough space. The wrap must DISCARD: a dynamic
     * buffer accepts only WRITE_DISCARD and WRITE_NO_OVERWRITE, so the old
     * D3D11_MAP_WRITE failed, the draw was dropped, and the next upload wrote
     * at offset 0 with NO_OVERWRITE -- over vertices that draws queued
     * earlier in the frame had not consumed yet. Character select showed it
     * as screen-filling shards that changed every frame (part 182). */
    if (g_up_ring_offset + size > UP_RING_BUFFER_SIZE) {
        g_up_ring_offset = 0;
        map_type = D3D11_MAP_WRITE_DISCARD;
    } else {
        map_type = D3D11_MAP_WRITE_NO_OVERWRITE;
    }

    hr = ID3D11DeviceContext_Map(g_device_state.d3d11_context,
        (ID3D11Resource *)g_up_ring_buffer, 0, map_type, 0, &mapped);
    if (FAILED(hr)) return (UINT)-1;

    offset = g_up_ring_offset;
    memcpy((BYTE *)mapped.pData + offset, data, size);

    ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
        (ID3D11Resource *)g_up_ring_buffer, 0);

    g_up_ring_offset = (offset + size + 15) & ~15;  /* 16-byte align */
    return offset;
}

/* Make the next `total` bytes of uploads (in `pieces` separate uploads) land
 * in one instance of the ring (part 183). A draw that uploads several vertex
 * streams must not wrap between them: the wrap DISCARDs the buffer, so the
 * streams written before it are gone while the draw still points at them --
 * an intermittent frame of screen-filling shards in character select. When
 * they would not fit, the wrap is taken before the first of them. */
void d3d8_UpRingReserve(UINT total, UINT pieces)
{
    UINT need = total + pieces * 16u;
    if (need <= UP_RING_BUFFER_SIZE && g_up_ring_offset + need > UP_RING_BUFFER_SIZE)
        g_up_ring_offset = UP_RING_BUFFER_SIZE;   /* the next upload wraps */
}

/* The ring upload for d3d8_nv2a.c's NV2A-native draws. */
UINT d3d8_UpRingUpload(const void *data, UINT size, ID3D11Buffer **buf)
{
    UINT off = up_ring_upload(data, size);
    *buf = g_up_ring_buffer;
    return off;
}

static HRESULT __stdcall dev_DrawPrimitive(IDirect3DDevice8 *self, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    (void)self;
    g_d3d_draw_count++;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    UINT vertex_count;

    topology = map_primitive_type(PrimitiveType, PrimitiveCount, &vertex_count);
    if (vertex_count == 0) return E_INVALIDARG;

    /* Prepare pipeline: shaders, input layout, constant buffers, render states */
    /* Vertex shader: try programmable VS first, fall back to FVF fixed-function */
    if (!d3d8_vsh_prepare_draw(g_device_state.vertex_shader))
        d3d8_shaders_prepare_draw(g_device_state.vertex_shader);
    d3d8_combiners_prepare_draw(); /* overrides PS if combiner shader is active */
    d3d8_states_apply();

    ID3D11DeviceContext_IASetPrimitiveTopology(g_device_state.d3d11_context, topology);
    ID3D11DeviceContext_Draw(g_device_state.d3d11_context, vertex_count, StartVertex);
    return S_OK;
}

static HRESULT __stdcall dev_DrawIndexedPrimitive(IDirect3DDevice8 *self, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount)
{
    (void)self; (void)MinVertexIndex; (void)NumVertices;
    g_d3d_draw_count++;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    UINT index_count;

    topology = map_primitive_type(PrimitiveType, PrimitiveCount, &index_count);
    if (index_count == 0) return E_INVALIDARG;

    /* Vertex shader: try programmable VS first, fall back to FVF fixed-function */
    if (!d3d8_vsh_prepare_draw(g_device_state.vertex_shader))
        d3d8_shaders_prepare_draw(g_device_state.vertex_shader);
    d3d8_combiners_prepare_draw(); /* overrides PS if combiner shader is active */
    d3d8_states_apply();

    ID3D11DeviceContext_IASetPrimitiveTopology(g_device_state.d3d11_context, topology);
    ID3D11DeviceContext_DrawIndexed(g_device_state.d3d11_context, index_count, StartIndex, (INT)g_cur_ib_base_vertex);
    return S_OK;
}

static HRESULT __stdcall dev_DrawPrimitiveUP(IDirect3DDevice8 *self, D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void *pVertexData, UINT VertexStreamZeroStride)
{
    (void)self;
    g_d3d_draw_count++;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    UINT vertex_count, vb_size, ring_offset;
    const void *draw_data = pVertexData;
    void *converted = NULL;

    if (!pVertexData || !VertexStreamZeroStride) return E_INVALIDARG;

    topology = map_primitive_type(PrimitiveType, PrimitiveCount, &vertex_count);
    if (vertex_count == 0) return E_INVALIDARG;

    /* Convert triangle fans and quad lists to triangle lists */
    if (PrimitiveType == D3DPT_TRIANGLEFAN || PrimitiveType == D3DPT_QUADLIST) {
        converted = convert_fan_or_quad(PrimitiveType, pVertexData,
                                         PrimitiveCount, VertexStreamZeroStride,
                                         &vertex_count);
        if (converted) draw_data = converted;
    }

    vb_size = vertex_count * VertexStreamZeroStride;

    /* Upload to ring buffer */
    ring_offset = up_ring_upload(draw_data, vb_size);
    if (converted) free(converted);

    if (ring_offset == (UINT)-1) return E_OUTOFMEMORY;

    /* Bind ring buffer at the right offset */
    ID3D11DeviceContext_IASetVertexBuffers(g_device_state.d3d11_context,
        0, 1, &g_up_ring_buffer, &VertexStreamZeroStride, &ring_offset);

    /* Vertex shader: try programmable VS first, fall back to FVF fixed-function */
    if (!d3d8_vsh_prepare_draw(g_device_state.vertex_shader))
        d3d8_shaders_prepare_draw(g_device_state.vertex_shader);
    d3d8_combiners_prepare_draw(); /* overrides PS if combiner shader is active */
    d3d8_states_apply();

    ID3D11DeviceContext_IASetPrimitiveTopology(g_device_state.d3d11_context, topology);

    /* One-shot pipeline audit. The translator submits a correct full-screen
     * quad and the draw count climbs, but the back buffer reads pure black,
     * so something between here and the output merger is dropping it. Report
     * what is actually bound rather than assuming. */
    {
        static int audited = 0;
        if (!audited) {
            ID3D11VertexShader *vs = NULL; ID3D11PixelShader *ps = NULL;
            ID3D11InputLayout *il = NULL; ID3D11RenderTargetView *rtv = NULL;
            ID3D11DepthStencilView *dsv = NULL; ID3D11DepthStencilState *dss = NULL;
            ID3D11BlendState *bs = NULL; ID3D11RasterizerState *rs = NULL;
            D3D11_VIEWPORT vp[8]; UINT nvp = 8; UINT sref = 0;
            FLOAT bf[4]; UINT smask = 0;
            audited = 1;
            ID3D11DeviceContext_VSGetShader(g_device_state.d3d11_context, &vs, NULL, NULL);
            ID3D11DeviceContext_PSGetShader(g_device_state.d3d11_context, &ps, NULL, NULL);
            ID3D11DeviceContext_IAGetInputLayout(g_device_state.d3d11_context, &il);
            ID3D11DeviceContext_OMGetRenderTargets(g_device_state.d3d11_context, 1, &rtv, &dsv);
            ID3D11DeviceContext_OMGetDepthStencilState(g_device_state.d3d11_context, &dss, &sref);
            ID3D11DeviceContext_OMGetBlendState(g_device_state.d3d11_context, &bs, bf, &smask);
            ID3D11DeviceContext_RSGetState(g_device_state.d3d11_context, &rs);
            ID3D11DeviceContext_RSGetViewports(g_device_state.d3d11_context, &nvp, vp);
            fprintf(stderr, "  [D3D]   rtv is %s (default_rtv=%p)\n",
                    (rtv == g_device_state.default_rtv) ? "the back buffer"
                                                        : "NOT the back buffer",
                    (void *)g_device_state.default_rtv);
            fprintf(stderr, "  [D3D] draw audit: vs=%p ps=%p layout=%p rtv=%p dsv=%p\n"
                            "  [D3D]   dss=%p bs=%p rs=%p smask=0x%X viewports=%u",
                    (void*)vs, (void*)ps, (void*)il, (void*)rtv, (void*)dsv,
                    (void*)dss, (void*)bs, (void*)rs, smask, nvp);
            if (nvp)
                fprintf(stderr, " vp0=(%.0f,%.0f %.0fx%.0f z %.2f..%.2f)",
                        vp[0].TopLeftX, vp[0].TopLeftY, vp[0].Width, vp[0].Height,
                        vp[0].MinDepth, vp[0].MaxDepth);
            if (dss) {
                D3D11_DEPTH_STENCIL_DESC dd;
                ID3D11DepthStencilState_GetDesc(dss, &dd);
                fprintf(stderr, "\n  [D3D]   depth: enable=%d writemask=%d func=%d",
                        (int)dd.DepthEnable, (int)dd.DepthWriteMask, (int)dd.DepthFunc);
            }
            if (bs) {
                D3D11_BLEND_DESC bd;
                ID3D11BlendState_GetDesc(bs, &bd);
                fprintf(stderr, "\n  [D3D]   blend: enable=%d src=%d dst=%d rtwritemask=0x%X",
                        (int)bd.RenderTarget[0].BlendEnable,
                        (int)bd.RenderTarget[0].SrcBlend,
                        (int)bd.RenderTarget[0].DestBlend,
                        (unsigned)bd.RenderTarget[0].RenderTargetWriteMask);
            }
            if (rs) {
                D3D11_RASTERIZER_DESC rd;
                ID3D11RasterizerState_GetDesc(rs, &rd);
                fprintf(stderr, "\n  [D3D]   raster: cull=%d fill=%d scissor=%d",
                        (int)rd.CullMode, (int)rd.FillMode, (int)rd.ScissorEnable);
            }
            fprintf(stderr, "\n  [D3D]   topology=%d verts=%u stride=%u\n",
                    (int)topology, vertex_count, VertexStreamZeroStride);
            fflush(stderr);
            if (vs) ID3D11VertexShader_Release(vs);
            if (ps) ID3D11PixelShader_Release(ps);
            if (il) ID3D11InputLayout_Release(il);
            if (rtv) ID3D11RenderTargetView_Release(rtv);
            if (dsv) ID3D11DepthStencilView_Release(dsv);
            if (dss) ID3D11DepthStencilState_Release(dss);
            if (bs) ID3D11BlendState_Release(bs);
            if (rs) ID3D11RasterizerState_Release(rs);
        }
    }
    ID3D11DeviceContext_Draw(g_device_state.d3d11_context, vertex_count, 0);

    /* Read the vertices back out of the GPU buffer. Everything CPU-side has
     * checked out, so confirm the bytes the input assembler will actually
     * fetch rather than the bytes we believe we uploaded. */
    {
        static int dumped = 0;
        if (!dumped && g_up_ring_buffer && vb_size <= 256) {
            D3D11_BUFFER_DESC sd;
            ID3D11Buffer *stage = NULL;
            dumped = 1;
            memset(&sd, 0, sizeof(sd));
            sd.ByteWidth = 256;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(ID3D11Device_CreateBuffer(g_device_state.d3d11_device,
                    &sd, NULL, &stage)) && stage) {
                D3D11_BOX box;
                D3D11_MAPPED_SUBRESOURCE m;
                box.left = ring_offset; box.right = ring_offset + vb_size;
                box.top = 0; box.bottom = 1; box.front = 0; box.back = 1;
                ID3D11DeviceContext_CopySubresourceRegion(g_device_state.d3d11_context,
                    (ID3D11Resource *)stage, 0, 0, 0, 0,
                    (ID3D11Resource *)g_up_ring_buffer, 0, &box);
                if (SUCCEEDED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
                        (ID3D11Resource *)stage, 0, D3D11_MAP_READ, 0, &m))) {
                    const float *f = (const float *)m.pData;
                    UINT v, nv = vb_size / VertexStreamZeroStride;
                    fprintf(stderr, "  [D3D] vertices in GPU buffer (offset %u, %u bytes):\n",
                            ring_offset, vb_size);
                    for (v = 0; v < nv && v < 4; v++) {
                        const unsigned char *b = (const unsigned char *)m.pData
                                               + v * VertexStreamZeroStride;
                        memcpy((void *)&f, &b, sizeof b);
                        fprintf(stderr, "  [D3D]   v%u = (%.1f, %.1f, %.1f, %.1f) color=%08X\n",
                                v, ((const float *)b)[0], ((const float *)b)[1],
                                ((const float *)b)[2], ((const float *)b)[3],
                                *(const unsigned *)(b + 16));
                    }
                    fflush(stderr);
                    ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                        (ID3D11Resource *)stage, 0);
                }
                ID3D11Buffer_Release(stage);
            }
        }
    }

    /* Drain whatever the validation layer has to say about that draw. */
    {
        static int drained = 0;
        if (drained < 3 && g_device_state.d3d11_device) {
            ID3D11InfoQueue *iq = NULL;
            if (SUCCEEDED(ID3D11Device_QueryInterface(g_device_state.d3d11_device,
                    &IID_ID3D11InfoQueue, (void **)&iq)) && iq) {
                UINT64 n = ID3D11InfoQueue_GetNumStoredMessages(iq), k;
                drained++;
                for (k = 0; k < n && k < 24; k++) {
                    SIZE_T len = 0;
                    if (SUCCEEDED(ID3D11InfoQueue_GetMessage(iq, k, NULL, &len)) && len) {
                        D3D11_MESSAGE *m = (D3D11_MESSAGE *)malloc(len);
                        if (m && SUCCEEDED(ID3D11InfoQueue_GetMessage(iq, k, m, &len)))
                            fprintf(stderr, "  [D3D-VALID] sev=%d id=%d: %.*s\n",
                                    (int)m->Severity, (int)m->ID,
                                    (int)m->DescriptionByteLength, m->pDescription);
                        free(m);
                    }
                }
                ID3D11InfoQueue_ClearStoredMessages(iq);
                ID3D11InfoQueue_Release(iq);
                fflush(stderr);
            }
        }
    }

    /* Restore previous VB binding if any */
    if (g_cur_vb) {
        D3D8VertexBuffer *vb = (D3D8VertexBuffer *)g_cur_vb;
        UINT restore_offset = 0;
        ID3D11DeviceContext_IASetVertexBuffers(g_device_state.d3d11_context,
            0, 1, &vb->d3d11_buffer, &g_cur_vb_stride, &restore_offset);
    }
    return S_OK;
}

static HRESULT __stdcall dev_DrawIndexedPrimitiveUP(IDirect3DDevice8 *self, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat, const void *pVertexData, UINT VertexStreamZeroStride)
{
    (void)self; (void)MinVertexIndex;
    g_d3d_draw_count++;
    D3D11_PRIMITIVE_TOPOLOGY topology;
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sd;
    ID3D11Buffer *tmp_vb = NULL, *tmp_ib = NULL;
    UINT index_count, vb_size, ib_size, offset = 0;
    UINT idx_bytes;
    DXGI_FORMAT ib_fmt;
    HRESULT hr;

    if (!pVertexData || !pIndexData || !VertexStreamZeroStride) return E_INVALIDARG;

    topology = map_primitive_type(PrimitiveType, PrimitiveCount, &index_count);
    if (index_count == 0) return E_INVALIDARG;

    idx_bytes = (IndexDataFormat == D3DFMT_INDEX32) ? 4 : 2;
    ib_fmt = (IndexDataFormat == D3DFMT_INDEX32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    vb_size = NumVertices * VertexStreamZeroStride;
    ib_size = index_count * idx_bytes;

    /* Create temp vertex buffer */
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = vb_size;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    memset(&sd, 0, sizeof(sd));
    sd.pSysMem = pVertexData;
    hr = ID3D11Device_CreateBuffer(g_device_state.d3d11_device, &bd, &sd, &tmp_vb);
    if (FAILED(hr)) return hr;

    /* Create temp index buffer */
    bd.ByteWidth = ib_size;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = pIndexData;
    hr = ID3D11Device_CreateBuffer(g_device_state.d3d11_device, &bd, &sd, &tmp_ib);
    if (FAILED(hr)) { ID3D11Buffer_Release(tmp_vb); return hr; }

    /* Bind, prepare, draw */
    ID3D11DeviceContext_IASetVertexBuffers(g_device_state.d3d11_context,
        0, 1, &tmp_vb, &VertexStreamZeroStride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(g_device_state.d3d11_context,
        tmp_ib, ib_fmt, 0);

    /* Vertex shader: try programmable VS first, fall back to FVF fixed-function */
    if (!d3d8_vsh_prepare_draw(g_device_state.vertex_shader))
        d3d8_shaders_prepare_draw(g_device_state.vertex_shader);
    d3d8_combiners_prepare_draw(); /* overrides PS if combiner shader is active */
    d3d8_states_apply();

    ID3D11DeviceContext_IASetPrimitiveTopology(g_device_state.d3d11_context, topology);
    ID3D11DeviceContext_DrawIndexed(g_device_state.d3d11_context, index_count, 0, 0);

    /* Cleanup temp buffers */
    ID3D11Buffer_Release(tmp_ib);
    ID3D11Buffer_Release(tmp_vb);

    /* Restore previous bindings */
    if (g_cur_vb) {
        D3D8VertexBuffer *vb = (D3D8VertexBuffer *)g_cur_vb;
        offset = 0;
        ID3D11DeviceContext_IASetVertexBuffers(g_device_state.d3d11_context,
            0, 1, &vb->d3d11_buffer, &g_cur_vb_stride, &offset);
    }
    if (g_cur_ib) {
        D3D8IndexBuffer *ib = (D3D8IndexBuffer *)g_cur_ib;
        DXGI_FORMAT fmt = (ib->format == D3DFMT_INDEX32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        ID3D11DeviceContext_IASetIndexBuffer(g_device_state.d3d11_context,
            ib->d3d11_buffer, fmt, 0);
    }
    return S_OK;
}

static HRESULT __stdcall dev_CreateTexture(IDirect3DDevice8 *self, UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8 **ppTexture)
{
    (void)self; (void)Pool;
    return d3d8_CreateTextureImpl(Width, Height, Levels, Usage, Format, ppTexture);
}

static HRESULT __stdcall dev_CreateVertexBuffer(IDirect3DDevice8 *self, UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8 **ppVertexBuffer)
{
    (void)self; (void)Pool;
    return d3d8_CreateVertexBufferImpl(Length, Usage, FVF, ppVertexBuffer);
}

static HRESULT __stdcall dev_CreateIndexBuffer(IDirect3DDevice8 *self, UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8 **ppIndexBuffer)
{
    (void)self; (void)Pool;
    return d3d8_CreateIndexBufferImpl(Length, Usage, Format, ppIndexBuffer);
}

static HRESULT __stdcall dev_CreateRenderTarget(IDirect3DDevice8 *self, UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, BOOL Lockable, IDirect3DSurface8 **ppSurface)
{
    (void)self; (void)Width; (void)Height; (void)Format; (void)MultiSample; (void)Lockable; (void)ppSurface;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_CreateDepthStencilSurface(IDirect3DDevice8 *self, UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8 **ppSurface)
{
    (void)self; (void)Width; (void)Height; (void)Format; (void)MultiSample; (void)ppSurface;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_SetRenderTarget(IDirect3DDevice8 *self, IDirect3DSurface8 *pRenderTarget, IDirect3DSurface8 *pZStencilSurface)
{
    (void)self; (void)pRenderTarget; (void)pZStencilSurface;
    /* TODO: resolve D3D8 surface to D3D11 RTV/DSV */
    return S_OK;
}

static HRESULT __stdcall dev_GetRenderTarget(IDirect3DDevice8 *self, IDirect3DSurface8 **ppRenderTarget)
{
    (void)self; (void)ppRenderTarget;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_GetDepthStencilSurface(IDirect3DDevice8 *self, IDirect3DSurface8 **ppZStencilSurface)
{
    (void)self; (void)ppZStencilSurface;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_SetViewport(IDirect3DDevice8 *self, const D3DVIEWPORT8 *pViewport)
{
    (void)self;
    if (pViewport) {
        g_device_state.viewport = *pViewport;

        D3D11_VIEWPORT d3d11_vp;
        float sx, sy;
        d3d8_GetGuestScale(&sx, &sy);   /* title pixels -> scene target */
        d3d11_vp.TopLeftX = (FLOAT)pViewport->X * sx;
        d3d11_vp.TopLeftY = (FLOAT)pViewport->Y * sy;
        d3d11_vp.Width    = (FLOAT)pViewport->Width * sx;
        d3d11_vp.Height   = (FLOAT)pViewport->Height * sy;
        d3d11_vp.MinDepth = pViewport->MinZ;
        d3d11_vp.MaxDepth = pViewport->MaxZ;
        ID3D11DeviceContext_RSSetViewports(g_device_state.d3d11_context, 1, &d3d11_vp);
    }
    return S_OK;
}

static HRESULT __stdcall dev_GetViewport(IDirect3DDevice8 *self, D3DVIEWPORT8 *pViewport)
{
    (void)self;
    if (pViewport) *pViewport = g_device_state.viewport;
    return S_OK;
}

static HRESULT __stdcall dev_SetMaterial(IDirect3DDevice8 *self, const D3DMATERIAL8 *pMaterial)
{
    (void)self;
    if (pMaterial) g_device_state.material = *pMaterial;
    return S_OK;
}

static HRESULT __stdcall dev_GetMaterial(IDirect3DDevice8 *self, D3DMATERIAL8 *pMaterial)
{
    (void)self;
    if (pMaterial) *pMaterial = g_device_state.material;
    return S_OK;
}

static HRESULT __stdcall dev_SetLight(IDirect3DDevice8 *self, DWORD Index, const D3DLIGHT8 *pLight)
{
    (void)self;
    if (Index < MAX_LIGHTS && pLight) g_device_state.lights[Index] = *pLight;
    return S_OK;
}

static HRESULT __stdcall dev_GetLight(IDirect3DDevice8 *self, DWORD Index, D3DLIGHT8 *pLight)
{
    (void)self;
    if (Index < MAX_LIGHTS && pLight) *pLight = g_device_state.lights[Index];
    return S_OK;
}

static HRESULT __stdcall dev_LightEnable(IDirect3DDevice8 *self, DWORD Index, BOOL Enable)
{
    (void)self;
    if (Index < MAX_LIGHTS) g_device_state.light_enable[Index] = Enable;
    return S_OK;
}

static HRESULT __stdcall dev_CreateVertexShader(IDirect3DDevice8 *self, const DWORD *pDeclaration, const DWORD *pFunction, DWORD *pHandle, DWORD Usage)
{
    (void)self; (void)pDeclaration; (void)Usage;
    if (!pHandle) return E_INVALIDARG;
    if (!pFunction) return E_INVALIDARG;
    /* Count instructions: each is 4 DWORDs, last has bit 0 of word[3] set (END flag) */
    {
        int i, num_insns = 0;
        for (i = 0; i < 136; i++) {
            num_insns++;
            if (pFunction[i * 4 + 3] & 1) break;  /* END bit in last word */
        }
        return d3d8_vsh_create_shader(pFunction, num_insns, pHandle);
    }
}

static HRESULT __stdcall dev_SetVertexShader(IDirect3DDevice8 *self, DWORD Handle)
{
    (void)self;
    g_device_state.vertex_shader = Handle;
    return S_OK;
}

static HRESULT __stdcall dev_GetVertexShader(IDirect3DDevice8 *self, DWORD *pHandle)
{
    (void)self;
    if (pHandle) *pHandle = g_device_state.vertex_shader;
    return S_OK;
}

static HRESULT __stdcall dev_SetVertexShaderConstant(IDirect3DDevice8 *self, INT Register, const void *pConstantData, DWORD ConstantCount)
{
    (void)self;
    d3d8_vsh_set_constant(Register, pConstantData, ConstantCount);
    return S_OK;
}

static HRESULT __stdcall dev_SetPixelShader(IDirect3DDevice8 *self, DWORD Handle)
{
    (void)self;
    g_device_state.pixel_shader = Handle;
    d3d8_combiners_set_pixel_shader(Handle);
    return S_OK;
}

static HRESULT __stdcall dev_GetPixelShader(IDirect3DDevice8 *self, DWORD *pHandle)
{
    (void)self;
    if (pHandle) *pHandle = g_device_state.pixel_shader;
    return S_OK;
}

static HRESULT __stdcall dev_SetPixelShaderConstant(IDirect3DDevice8 *self, INT Register, const void *pConstantData, DWORD ConstantCount)
{
    (void)self; (void)Register; (void)pConstantData; (void)ConstantCount;
    return S_OK;
}

static void __stdcall dev_SetGammaRamp(IDirect3DDevice8 *self, DWORD Flags, const D3DGAMMARAMP *pRamp)
{
    (void)self; (void)Flags; (void)pRamp;
}

static void __stdcall dev_GetGammaRamp(IDirect3DDevice8 *self, D3DGAMMARAMP *pRamp)
{
    (void)self; (void)pRamp;
}

static HRESULT __stdcall dev_SetPalette(IDirect3DDevice8 *self, DWORD PaletteNumber, const void *pEntries)
{
    (void)self; (void)PaletteNumber; (void)pEntries;
    return S_OK;
}

static HRESULT __stdcall dev_BeginPush(IDirect3DDevice8 *self, DWORD Count, DWORD **ppPush)
{
    (void)self; (void)Count; (void)ppPush;
    /* TODO: Xbox push buffer emulation */
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_EndPush(IDirect3DDevice8 *self, DWORD *pPush)
{
    (void)self; (void)pPush;
    return E_NOTIMPL;
}

static HRESULT __stdcall dev_Swap(IDirect3DDevice8 *self, DWORD Flags)
{
    (void)self; (void)Flags;

    /* Pump Windows messages (same as dev_Present) */
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ExitProcess(0);
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    { static unsigned c = 0; if (++c == 1 || (c % 300) == 0) { fprintf(stderr, "  [PRESENT] dev_BeginPush #%u\n", c); fflush(stderr); } }
    return host_present();
}

/* ================================================================
 * Vtable
 * ================================================================ */

static const IDirect3DDevice8Vtbl g_device_vtbl = {
    dev_QueryInterface,
    dev_AddRef,
    dev_Release,
    dev_GetDirect3D,
    dev_GetDeviceCaps,
    dev_GetDisplayMode,
    dev_GetCreationParameters,
    dev_Reset,
    dev_Present,
    dev_GetBackBuffer,
    dev_BeginScene,
    dev_EndScene,
    dev_Clear,
    dev_SetTransform,
    dev_GetTransform,
    dev_SetRenderState,
    dev_GetRenderState,
    dev_SetTextureStageState,
    dev_GetTextureStageState,
    dev_SetTexture,
    dev_GetTexture,
    dev_SetStreamSource,
    dev_GetStreamSource,
    dev_SetIndices,
    dev_GetIndices,
    dev_DrawPrimitive,
    dev_DrawIndexedPrimitive,
    dev_DrawPrimitiveUP,
    dev_DrawIndexedPrimitiveUP,
    dev_CreateTexture,
    dev_CreateVertexBuffer,
    dev_CreateIndexBuffer,
    dev_CreateRenderTarget,
    dev_CreateDepthStencilSurface,
    dev_SetRenderTarget,
    dev_GetRenderTarget,
    dev_GetDepthStencilSurface,
    dev_SetViewport,
    dev_GetViewport,
    dev_SetMaterial,
    dev_GetMaterial,
    dev_SetLight,
    dev_GetLight,
    dev_LightEnable,
    dev_SetVertexShader,
    dev_GetVertexShader,
    dev_SetVertexShaderConstant,
    dev_SetPixelShader,
    dev_GetPixelShader,
    dev_SetPixelShaderConstant,
    dev_SetGammaRamp,
    dev_GetGammaRamp,
    dev_SetPalette,
    dev_BeginPush,
    dev_EndPush,
    dev_Swap,
};

/* ================================================================
 * Public API
 * ================================================================ */

IDirect3DDevice8 *xbox_GetD3DDevice(void)
{
    return g_device_initialized ? &g_device : NULL;
}

/* ================================================================
 * IDirect3D8 factory implementation
 * ================================================================ */

static IDirect3D8 g_d3d8;
static LONG g_d3d8_ref = 0;

static HRESULT __stdcall d3d8_QueryInterface(IDirect3D8 *self, const IID *riid, void **ppv)
{
    (void)self; (void)riid; (void)ppv;
    return E_NOINTERFACE;
}

static ULONG __stdcall d3d8_AddRef(IDirect3D8 *self)
{
    (void)self;
    return (ULONG)InterlockedIncrement(&g_d3d8_ref);
}

static ULONG __stdcall d3d8_Release(IDirect3D8 *self)
{
    (void)self;
    return (ULONG)InterlockedDecrement(&g_d3d8_ref);
}

static HRESULT __stdcall d3d8_CreateDevice(IDirect3D8 *self, UINT Adapter, DWORD DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPP, IDirect3DDevice8 **ppDevice)
{
    (void)self; (void)Adapter; (void)DeviceType; (void)BehaviorFlags;
    HRESULT hr;

    if (!pPP || !ppDevice) return E_INVALIDARG;

    memset(&g_device_state, 0, sizeof(g_device_state));
    g_device_state.ref_count = 1;

    if (!pPP->hDeviceWindow) pPP->hDeviceWindow = hFocusWindow;

    hr = d3d11_create_device_and_swap_chain(&g_device_state, pPP);
    if (FAILED(hr)) return hr;

    hr = d3d11_create_render_targets(&g_device_state);
    if (FAILED(hr)) return hr;

    d3d8_init_default_states(&g_device_state);

    /* Set initial viewport (D3D11 requires explicit viewport) */
    {
        D3D11_VIEWPORT vp;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width    = (FLOAT)g_device_state.width;
        vp.Height   = (FLOAT)g_device_state.height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ID3D11DeviceContext_RSSetViewports(g_device_state.d3d11_context, 1, &vp);
    }

    /* Initialize shader and state subsystems */
    hr = d3d8_shaders_init();
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8: Shader init failed: 0x%08lX\n", hr);
        return hr;
    }

    hr = d3d8_states_init();
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8: State init failed: 0x%08lX\n", hr);
        return hr;
    }

    hr = d3d8_combiners_init();
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8: Combiner init failed: 0x%08lX\n", hr);
        /* Non-fatal: fall back to fixed-function pixel shaders */
    }

    hr = d3d8_vsh_init();
    if (FAILED(hr)) {
        fprintf(stderr, "D3D8: VSH init failed: 0x%08lX\n", hr);
        /* Non-fatal: fall back to FVF vertex shaders */
    }

    g_device.lpVtbl = &g_device_vtbl;
    g_device_initialized = TRUE;

    *ppDevice = &g_device;
    fprintf(stderr, "D3D8: Device created (%ux%u)\n", g_device_state.width, g_device_state.height);
    return S_OK;
}

static const IDirect3D8Vtbl g_d3d8_vtbl = {
    d3d8_QueryInterface,
    d3d8_AddRef,
    d3d8_Release,
    d3d8_CreateDevice,
};

IDirect3D8 *xbox_Direct3DCreate8(UINT SDKVersion)
{
    (void)SDKVersion;
    g_d3d8.lpVtbl = &g_d3d8_vtbl;
    g_d3d8_ref = 1;
    return &g_d3d8;
}

/* ---------------------------------------------------------------------------
 * d3d8_PresentGuestFramebuffer -- copy guest video memory into the back buffer.
 *
 * The title's video player bypasses the push buffer entirely: it locks the back
 * buffer through the Xbox D3D8 linked into the XBE and writes decoded MPEG
 * pixels straight into guest VRAM. On hardware the GPU scans that out; here the
 * host swap chain is a separate surface, so without this the frames have
 * nowhere to go.
 *
 * Done with raw D3D11 rather than through the D3D8 shim's texture objects: the
 * shim's LockRect hands back its own sys_mem/pitch, which is not a route to the
 * swap chain and produced a wild pointer when used as one.
 *
 * Guest pixels are A8R8G8B8 (B,G,R,A in memory); the swap chain is
 * R8G8B8A8_UNORM, so red and blue are swapped per pixel on the way in.
 * ------------------------------------------------------------------------ */
void d3d8_PresentGuestFramebuffer(const void *src, unsigned pitch,
                                  unsigned w, unsigned h)
{
    static ID3D11Texture2D *stage = NULL;
    static unsigned stage_w = 0, stage_h = 0;
    D3D11_MAPPED_SUBRESOURCE m;
    ID3D11Texture2D *back = NULL;
    unsigned y, x;

    /* src == NULL means "nothing new is safe to copy -- re-present the frame
     * staging already holds". Re-reading guest memory instead would re-copy a
     * buffer that may be mid-write by now, which is where the residual
     * backwards/torn frames came from. */
    if (!w || !h || !g_device_state.d3d11_device
        || !g_device_state.d3d11_context || !g_device_state.swap_chain)
        return;
    if (!src && !stage) return;

    if (stage && (stage_w != w || stage_h != h)) {
        ID3D11Texture2D_Release(stage);
        stage = NULL;
    }
    if (!stage) {
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof td);
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        /* STAGING, not DYNAMIC: a DYNAMIC resource is not a legal participant
         * in CopySubresourceRegion, so the copy was silently doing nothing
         * while the map and the pixel conversion both succeeded -- which is
         * why the source reported 4032 non-black pixels and the screen stayed
         * black. STAGING is the canonical CPU-write-then-copy path. */
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateTexture2D(g_device_state.d3d11_device,
                                                &td, NULL, &stage)) || !stage)
            return;
        stage_w = w; stage_h = h;
    }

    if (src && FAILED(ID3D11DeviceContext_Map(g_device_state.d3d11_context,
            (ID3D11Resource *)stage, 0, D3D11_MAP_WRITE, 0, &m)))
        return;
    if (src) {
    for (y = 0; y < h; y++) {
        const unsigned char *s = (const unsigned char *)src + (size_t)y * pitch;
        unsigned char *d = (unsigned char *)m.pData + (size_t)y * m.RowPitch;
        for (x = 0; x < w; x++) {
            d[x * 4 + 0] = s[x * 4 + 2];   /* R <- guest R */
            d[x * 4 + 1] = s[x * 4 + 1];   /* G */
            d[x * 4 + 2] = s[x * 4 + 0];   /* B <- guest B */
            d[x * 4 + 3] = 0xFF;           /* opaque: the video has no alpha */
        }
    }
    ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                              (ID3D11Resource *)stage, 0);
    }

    /* Into the scene target (part 183). At the title's own size that is the
     * same 1:1 copy as always; a larger scene gets the frame scaled up through
     * a sampleable copy, since a staging texture cannot be sampled. */
    if (SUCCEEDED(scene_texture_get(&back)) && back) {
        if (w == g_device_state.width && h == g_device_state.height &&
            !g_device_state.scene_ms) {
            D3D11_BOX box;
            box.left = 0; box.top = 0; box.front = 0;
            box.right = w; box.bottom = h; box.back = 1;
            ID3D11DeviceContext_CopySubresourceRegion(g_device_state.d3d11_context,
                (ID3D11Resource *)back, 0, 0, 0, 0,
                (ID3D11Resource *)stage, 0, &box);
        } else {
            static ID3D11Texture2D *vtex = NULL;
            static ID3D11ShaderResourceView *vsrv = NULL;
            static unsigned vw = 0, vh = 0;
            if (vtex && (vw != w || vh != h)) {
                ID3D11ShaderResourceView_Release(vsrv); vsrv = NULL;
                ID3D11Texture2D_Release(vtex); vtex = NULL;
            }
            if (!vtex) {
                D3D11_TEXTURE2D_DESC td;
                memset(&td, 0, sizeof td);
                td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (SUCCEEDED(ID3D11Device_CreateTexture2D(g_device_state.d3d11_device,
                        &td, NULL, &vtex)) && vtex
                    && FAILED(ID3D11Device_CreateShaderResourceView(g_device_state.d3d11_device,
                        (ID3D11Resource *)vtex, NULL, &vsrv))) {
                    ID3D11Texture2D_Release(vtex); vtex = NULL;
                }
                vw = w; vh = h;
            }
            if (vtex && vsrv) {
                D3D11_VIEWPORT vp;
                ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
                    (ID3D11Resource *)vtex, (ID3D11Resource *)stage);
                vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f;
                vp.Width  = (FLOAT)g_device_state.width;
                vp.Height = (FLOAT)g_device_state.height;
                vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
                blit_draw(vsrv, g_device_state.default_rtv, &vp);
            }
        }
        ID3D11Texture2D_Release(back);

        /* One line, once: a silent no-op here is indistinguishable from the
         * feature not being wired at all. Report the first copy and how much
         * of the source was actually non-black, so "it ran" and "it had
         * something to show" stay separate questions. */
        if (src) {
            static int told = 0;
            static unsigned calls = 0;
            unsigned nz = 0, sy, sx;
            calls++;
            for (sy = 0; sy < h; sy += 8) {
                const unsigned char *s =
                    (const unsigned char *)src + (size_t)sy * pitch;
                for (sx = 0; sx < w; sx += 8)
                    if (s[sx * 4] | s[sx * 4 + 1] | s[sx * 4 + 2]) nz++;
            }
            /* Report the first frame that actually carries pixels, not the
             * first call: the earliest presents happen long before the video
             * starts blitting, so a one-shot report only ever says "black". */
            if (!told && nz) {
                unsigned dst_nz = 0;
                ID3D11Texture2D *rb = NULL;
                D3D11_TEXTURE2D_DESC rd;
                D3D11_MAPPED_SUBRESOURCE rm;
                told = 1;
                /* Count non-black pixels in the DESTINATION. A hash proves
                 * nothing here -- FNV-1a of an all-zero buffer is still a
                 * non-zero number, which is how the previous check reported
                 * success over a copy that had done nothing. */
                ID3D11Texture2D_GetDesc(back, &rd);
                rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0;
                rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; rd.MiscFlags = 0;
                if (SUCCEEDED(ID3D11Device_CreateTexture2D(
                        g_device_state.d3d11_device, &rd, NULL, &rb)) && rb) {
                    ID3D11DeviceContext_CopyResource(g_device_state.d3d11_context,
                        (ID3D11Resource *)rb, (ID3D11Resource *)back);
                    if (SUCCEEDED(ID3D11DeviceContext_Map(
                            g_device_state.d3d11_context, (ID3D11Resource *)rb,
                            0, D3D11_MAP_READ, 0, &rm))) {
                        unsigned ry, rx;
                        for (ry = 0; ry < rd.Height; ry += 8) {
                            const unsigned char *r =
                                (const unsigned char *)rm.pData + (size_t)ry * rm.RowPitch;
                            for (rx = 0; rx < rd.Width; rx += 8)
                                if (r[rx*4] | r[rx*4+1] | r[rx*4+2]) dst_nz++;
                        }
                        ID3D11DeviceContext_Unmap(g_device_state.d3d11_context,
                                                  (ID3D11Resource *)rb, 0);
                    }
                    ID3D11Texture2D_Release(rb);
                }
                fprintf(stderr, "  [FB] call %u: source %u/%u non-black, "
                        "destination %u non-black after copy (%ux%u pitch %u)\n",
                        calls, nz, (h / 8) * (w / 8), dst_nz, w, h, pitch);
                /* Raw source bytes from a row inside the picture. The green
                 * channel reads zero in the finished image, and guessing at the
                 * pixel format from the finished image is how that gets
                 * misdiagnosed -- look at what the guest actually wrote. */
                {
                    const unsigned char *r0 =
                        (const unsigned char *)src + (size_t)(h / 2) * pitch;
                    unsigned k;
                    fprintf(stderr, "  [FB] source row %u bytes:", h / 2);
                    for (k = 0; k < 32; k++)
                        fprintf(stderr, " %02X", r0[(w / 2) * 4 + k]);
                    fprintf(stderr, "\n");
                }
                fflush(stderr);
            }
        }
    }
}

/* True once the video player has registered a guest framebuffer, so the
 * present pacing can follow the video instead of the 500 ms safety net. */
int d3d8_HasGuestFramebuffer(void)
{
    return g_guest_fb_src != NULL;
}

/* True while the video player is still locking the guest framebuffer: its
 * frames reach the screen only through timed presents, so flip-paced
 * presenting (nv2a pgraph, part 182) must not suppress those. */
int d3d8_GuestFramebufferActive(void)
{
    return g_guest_fb_src != NULL && g_guest_fb_locks != 0
        && g_present_seq - g_guest_fb_lock_seq <= GUEST_FB_IDLE_PRESENTS;
}
