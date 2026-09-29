/**
 * NV2A-native draw path (part 182).
 *
 * The push-buffer translator (nv2a_pgraph_d3d11.c) runs the title's vertex
 * programs on the CPU and now also turns its register-combiner setup into an
 * HLSL pixel shader (nv2a_psh.c). Neither fits the fixed-function FVF path:
 * the combiners read four float4 texture coordinates, both vertex colours and
 * the fog factor. This file supplies the rest -- a pass-through vertex shader
 * for pre-transformed NV2A vertices with perspective-correct interpolation,
 * a compiled pixel-shader cache keyed by the combiner state, and the draw
 * itself -- while reusing the device's vertex ring, render states, samplers
 * and bound textures. Every shim draw rebinds its own shaders and constant
 * buffers, so nothing set here leaks into them.
 */
#include "d3d8_internal.h"
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

UINT d3d8_UpRingUpload(const void *data, UINT size, ID3D11Buffer **buf);
void d3d8_UpRingReserve(UINT total, UINT pieces);

/* Vertex layout written by nv2a_pgraph_d3d11.c (ProgVertex): screen x, y,
 * z (0..1) and rhw; oD0; oD1; the fog factor; oT0..oT3. */
static const char g_vs_src[] =
    "cbuffer NvVS : register(b0) { float4 screen; };\n"
    "struct VSIn {\n"
    "    float4 pos : POSITION; float4 d0 : COLOR0; float4 d1 : COLOR1; float fog : FOG;\n"
    "    float4 t0 : TEXCOORD0; float4 t1 : TEXCOORD1; float4 t2 : TEXCOORD2; float4 t3 : TEXCOORD3;\n"
    "};\n"
    "struct VSOut {\n"
    "    float4 pos : SV_POSITION; float4 d0 : COLOR0; float4 d1 : COLOR1; float fog : FOG;\n"
    "    float4 t0 : TEXCOORD0; float4 t1 : TEXCOORD1; float4 t2 : TEXCOORD2; float4 t3 : TEXCOORD3;\n"
    "};\n"
    "VSOut main(VSIn i) {\n"
    "    VSOut o;\n"
    /* Undo the divide by w so the rasteriser interpolates perspective-correctly. */
    /* rhw is never 0 and keeps w's sign (nv2a_pgraph_d3d11.c clamps w away
     * from zero), so a vertex behind the eye gets a negative clip w and the
     * rasteriser clips its triangles at the near plane, as xemu relies on. */
    "    float w = 1.0 / i.pos.w;\n"
    "    o.pos = float4((i.pos.x / screen.x * 2.0 - 1.0) * w,\n"
    "                   (1.0 - i.pos.y / screen.y * 2.0) * w, i.pos.z * w, w);\n"
    "    o.d0 = i.d0; o.d1 = i.d1; o.fog = i.fog;\n"
    "    o.t0 = i.t0; o.t1 = i.t1; o.t2 = i.t2; o.t3 = i.t3;\n"
    "    return o;\n"
    "}\n";

static ID3D11VertexShader *g_vs;
static ID3D11InputLayout  *g_layout;
static ID3D11Buffer       *g_vs_cb, *g_ps_cb;
static UINT                g_ps_cb_size;
static int                 g_init_failed;

#define NV_PS_CACHE 2048
static struct { uint64_t key; ID3D11PixelShader *ps; } g_ps_cache[NV_PS_CACHE];
static int g_ps_count;

static ID3D11Buffer *make_cb(ID3D11Device *dev, UINT size)
{
    D3D11_BUFFER_DESC bd;
    ID3D11Buffer *b = NULL;
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = (size + 15) & ~15u;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &b))) return NULL;
    return b;
}

static int nv_init(void)
{
    ID3D11Device *dev = d3d8_GetD3D11Device();
    ID3DBlob *blob = NULL, *err = NULL;
    HRESULT hr;
    static const D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,   0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "FOG",      0, DXGI_FORMAT_R32_FLOAT,          0,  48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  68, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  84, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 100, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    if (g_vs) return 1;
    if (g_init_failed || !dev) return 0;
    hr = D3DCompile(g_vs_src, strlen(g_vs_src), "vs_nv2a", NULL, NULL, "main", "vs_5_0",
                    0, 0, &blob, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "[NV2A-PSH] vertex shader failed: %s\n",
                err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        if (err) ID3D10Blob_Release(err);
        g_init_failed = 1;
        return 0;
    }
    hr = ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(blob),
                                         ID3D10Blob_GetBufferSize(blob), NULL, &g_vs);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateInputLayout(dev, el, (UINT)(sizeof el / sizeof el[0]),
                                            ID3D10Blob_GetBufferPointer(blob),
                                            ID3D10Blob_GetBufferSize(blob), &g_layout);
    ID3D10Blob_Release(blob);
    g_vs_cb = make_cb(dev, 16);
    if (FAILED(hr) || !g_vs || !g_layout || !g_vs_cb) {
        fprintf(stderr, "[NV2A-PSH] vertex stage setup failed (hr %08lX)\n", (unsigned long)hr);
        g_init_failed = 1;
        return 0;
    }
    return 1;
}

/* Shader compiles and the time they took, for the frame-rate log. */
volatile long g_nv_compiles = 0;
volatile double g_nv_compile_ms = 0;
static double nv_now_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER q;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q);
    return (double)q.QuadPart * 1000.0 / (double)f.QuadPart;
}

static ID3D11PixelShader *ps_find(unsigned long long key)
{
    int i;
    for (i = 0; i < g_ps_count; i++)
        if (g_ps_cache[i].key == key)
            return g_ps_cache[i].ps;
    return NULL;
}

int d3d8_nv2a_has_ps(unsigned long long key)
{
    return ps_find(key) != NULL;
}

int d3d8_nv2a_add_ps(unsigned long long key, const char *hlsl, int len)
{
    ID3D11Device *dev = d3d8_GetD3D11Device();
    ID3DBlob *blob = NULL, *err = NULL;
    ID3D11PixelShader *ps = NULL;
    HRESULT hr;
    static int failures;

    if (!dev || g_ps_count >= NV_PS_CACHE) return 0;
    {
        double t0 = nv_now_ms();
        hr = D3DCompile(hlsl, (SIZE_T)len, "ps_nv2a", NULL, NULL, "main", "ps_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
        g_nv_compiles++;
        g_nv_compile_ms += nv_now_ms() - t0;
    }
    if (FAILED(hr)) {
        if (failures++ < 4)
            fprintf(stderr, "[NV2A-PSH] pixel shader %016llX failed: %s\n--- source ---\n%s\n",
                    (unsigned long long)key,
                    err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?", hlsl);
        if (err) ID3D10Blob_Release(err);
        return 0;
    }
    if (err) ID3D10Blob_Release(err);
    hr = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(blob),
                                        ID3D10Blob_GetBufferSize(blob), NULL, &ps);
    ID3D10Blob_Release(blob);
    if (FAILED(hr) || !ps) return 0;
    g_ps_cache[g_ps_count].key = key;
    g_ps_cache[g_ps_count].ps = ps;
    g_ps_count++;
    return 1;
}

static void cb_write(ID3D11DeviceContext *ctx, ID3D11Buffer *b, const void *data, UINT size)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)b, 0,
                                          D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, data, size);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)b, 0);
    }
}

/* The rasteriser state for a draw's raster bits: bit 0 depth clip, bits 2:1
 * cull (0 none, 1 front, 2 back), bit 3 front face counter-clockwise (D3D11
 * sense); bit 4 here is the window-clip scissor. NV2A SET_ZMIN_MAX_CONTROL
 * in clamp mode clamps a pixel beyond the depth range instead of dropping it
 * (xemu discards only in cull mode); D3D11 clips at the far plane unless
 * DepthClipEnable is off, which deleted the character-select riders: their z
 * lands just past 2^24-1. */
static void nv_set_raster(ID3D11DeviceContext *ctx, ID3D11Device *dev, int raster)
{
    static ID3D11RasterizerState *rs[32];
    int k = (raster & 0x0F) | (d3d8_WindowClipOn() ? 0x10 : 0);
    if (!rs[k]) {
        D3D11_RASTERIZER_DESC rd;
        int cull = (k >> 1) & 3;
        memset(&rd, 0, sizeof rd);
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = cull == 1 ? D3D11_CULL_FRONT : cull == 2 ? D3D11_CULL_BACK : D3D11_CULL_NONE;
        rd.FrontCounterClockwise = (k & 8) ? TRUE : FALSE;
        rd.DepthClipEnable = (k & 1) ? TRUE : FALSE;
        rd.ScissorEnable = (k & 0x10) ? TRUE : FALSE;   /* rectangle set by d3d8_states_apply */
        ID3D11Device_CreateRasterizerState(dev, &rd, &rs[k]);
    }
    if (rs[k]) ID3D11DeviceContext_RSSetState(ctx, rs[k]);
}

HRESULT d3d8_nv2a_draw(D3DPRIMITIVETYPE prim, UINT prim_count, const void *verts, UINT stride,
                       unsigned long long ps_key, const void *ps_consts, UINT ps_consts_size,
                       int raster)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Device *dev = d3d8_GetD3D11Device();
    ID3D11PixelShader *ps = ps_find(ps_key);
    ID3D11Buffer *vb = NULL;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    UINT nv, off;
    float screen[4];

    if (!ctx || !dev || !ps || !nv_init()) return E_FAIL;
    switch (prim) {
    case D3DPT_TRIANGLELIST: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; nv = prim_count * 3; break;
    case D3DPT_LINELIST:     topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;     nv = prim_count * 2; break;
    case D3DPT_LINESTRIP:    topo = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;    nv = prim_count + 1; break;
    case D3DPT_POINTLIST:    topo = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;    nv = prim_count;     break;
    default: return E_INVALIDARG;
    }
    off = d3d8_UpRingUpload(verts, nv * stride, &vb);
    if (off == (UINT)-1 || !vb) return E_OUTOFMEMORY;

    if (!g_ps_cb || g_ps_cb_size < ps_consts_size) {
        if (g_ps_cb) ID3D11Buffer_Release(g_ps_cb);
        g_ps_cb = make_cb(dev, ps_consts_size);
        g_ps_cb_size = g_ps_cb ? ps_consts_size : 0;
        if (!g_ps_cb) return E_OUTOFMEMORY;
    }
    screen[0] = (float)d3d8_GetBackbufferWidth();
    screen[1] = (float)d3d8_GetBackbufferHeight();
    if (screen[0] <= 0.0f) screen[0] = 640.0f;
    if (screen[1] <= 0.0f) screen[1] = 480.0f;
    screen[2] = screen[3] = 0.0f;
    cb_write(ctx, g_vs_cb, screen, sizeof screen);
    cb_write(ctx, g_ps_cb, ps_consts, ps_consts_size);

    d3d8_states_apply();                 /* blend, depth, raster, samplers */
    /* NV2A SET_ZMIN_MAX_CONTROL: in clamp mode a pixel beyond the depth
     * range is clamped, not dropped (xemu discards only in cull mode).
     * D3D11 clips at the far plane unless DepthClipEnable is off, which
     * deleted the character-select riders: their z lands just past 2^24-1. */
    nv_set_raster(ctx, dev, raster);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
    ID3D11DeviceContext_IASetInputLayout(ctx, g_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, topo);
    ID3D11DeviceContext_VSSetShader(ctx, g_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &g_vs_cb);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &g_ps_cb);
    ID3D11DeviceContext_Draw(ctx, nv, 0);
    return S_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Vertex programs on the GPU (part 183) -- see d3d8_nv2a_vsh.c.
 * ══════════════════════════════════════════════════════════════════════ */
#include "d3d8_nv2a_vsh.h"

#define VSH_CACHE 512
static struct { uint64_t key; ID3D11VertexShader *vs; ID3D10Blob *code; } g_vsh[VSH_CACHE];
static int g_nvsh;
#define IL_CACHE 1024
static struct { uint64_t key; ID3D11InputLayout *il; } g_il[IL_CACHE];
static int g_nil;
static ID3D11Buffer *g_vsc_cb, *g_vsp_cb, *g_ib_ring;
static UINT g_ib_off;
static float g_vsc_last[VSHCPU_CONSTANTS][4];
static int g_vsc_valid;
#define IB_RING_SIZE (16u * 1024u * 1024u)

static uint64_t fnv64(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) { h ^= *b++; h *= 1099511628211ull; }
    return h;
}

/* The shader for this program and these input kinds, compiled on first use.
 * A program that fails to compile is remembered, so it is not retried. */
static int vsh_get(const Nv2aVshDraw *d)
{
    static char src[262144];
    ID3D11Device *dev = d3d8_GetD3D11Device();
    uint8_t kind[16];
    uint64_t key = d->prog_hash;
    ID3D10Blob *code = NULL, *err = NULL;
    HRESULT hr;
    int a, i, len;

    for (a = 0; a < 16; a++) kind[a] = (d->inputs & (1u << a)) ? d->attr[a].kind : 0xFF;
    key = fnv64(key, kind, sizeof kind);
    for (i = 0; i < g_nvsh; i++)
        if (g_vsh[i].key == key) return g_vsh[i].vs ? i : -1;
    if (g_nvsh >= VSH_CACHE || !dev) return -1;
    i = g_nvsh++;
    g_vsh[i].key = key;
    g_vsh[i].vs = NULL;
    g_vsh[i].code = NULL;
    len = nv2a_vsh_hlsl(d->prog, d->prog_len, d->inputs, kind, src, (int)sizeof src);
    if (len <= 0) return -1;
    /* IEEE strictness keeps the isnan/isfinite tests the CPU path relies on. */
    {
        double t0 = nv_now_ms();
        hr = D3DCompile(src, (SIZE_T)len, "vs_nv2a_prog", NULL, NULL, "main", "vs_5_0",
                        D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &err);
        g_nv_compiles++;
        g_nv_compile_ms += nv_now_ms() - t0;
    }
    if (FAILED(hr)) {
        static int told;
        if (told++ < 4)
            fprintf(stderr, "[NV2A-VSH] program %016llX did not compile: %s\n--- source ---\n%s\n",
                    (unsigned long long)key, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?", src);
        if (err) ID3D10Blob_Release(err);
        return -1;
    }
    if (err) ID3D10Blob_Release(err);
    hr = ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(code),
                                         ID3D10Blob_GetBufferSize(code), NULL, &g_vsh[i].vs);
    if (FAILED(hr)) { ID3D10Blob_Release(code); g_vsh[i].vs = NULL; return -1; }
    g_vsh[i].code = code;
    {
        static int count;
        if (++count <= 64 || (count % 64) == 0)
            fprintf(stderr, "[NV2A-VSH] compiled vertex program %d (%d instructions)\n",
                    count, d->prog_len);
    }
    return i;
}

static ID3D11InputLayout *il_get(int vi, const D3D11_INPUT_ELEMENT_DESC *el, int n)
{
    ID3D11Device *dev = d3d8_GetD3D11Device();
    uint64_t key = fnv64(14695981039346656037ull, &vi, sizeof vi);
    int i;
    for (i = 0; i < n; i++) {
        key = fnv64(key, &el[i].SemanticIndex, sizeof el[i].SemanticIndex);
        key = fnv64(key, &el[i].Format, sizeof el[i].Format);
        key = fnv64(key, &el[i].InputSlot, sizeof el[i].InputSlot);
        key = fnv64(key, &el[i].AlignedByteOffset, sizeof el[i].AlignedByteOffset);
    }
    for (i = 0; i < g_nil; i++)
        if (g_il[i].key == key) return g_il[i].il;
    if (g_nil >= IL_CACHE || !dev) return NULL;
    i = g_nil++;
    g_il[i].key = key;
    g_il[i].il = NULL;
    ID3D11Device_CreateInputLayout(dev, el, (UINT)n,
                                   ID3D10Blob_GetBufferPointer(g_vsh[vi].code),
                                   ID3D10Blob_GetBufferSize(g_vsh[vi].code), &g_il[i].il);
    return g_il[i].il;
}

/* Index ring: as the vertex ring, discarding when it wraps. */
static UINT ib_upload(ID3D11DeviceContext *ctx, const uint32_t *idx, UINT n)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP how = D3D11_MAP_WRITE_NO_OVERWRITE;
    UINT size = n * 4u, off;
    if (!g_ib_ring) {
        D3D11_BUFFER_DESC bd;
        memset(&bd, 0, sizeof bd);
        bd.ByteWidth = IB_RING_SIZE;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(), &bd, NULL, &g_ib_ring)))
            return (UINT)-1;
    }
    if (size > IB_RING_SIZE) return (UINT)-1;
    if (g_ib_off + size > IB_RING_SIZE) { g_ib_off = 0; how = D3D11_MAP_WRITE_DISCARD; }
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)g_ib_ring, 0, how, 0, &m)))
        return (UINT)-1;
    off = g_ib_off;
    memcpy((uint8_t *)m.pData + off, idx, size);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)g_ib_ring, 0);
    g_ib_off = (off + size + 15u) & ~15u;
    return off;
}

int d3d8_nv2a_vsh_ready(const Nv2aVshDraw *d)
{
    return nv_init() && vsh_get(d) >= 0;
}

int d3d8_nv2a_draw_program_gpu(const Nv2aVshDraw *d)
{
    ID3D11DeviceContext *ctx = d3d8_GetD3D11Context();
    ID3D11Device *dev = d3d8_GetD3D11Device();
    ID3D11PixelShader *ps = ps_find(d->ps_key);
    D3D11_INPUT_ELEMENT_DESC el[16];
    struct { const uint8_t *base; UINT stride, size; } grp[16];
    ID3D11Buffer *vbs[16], *cbs[2];
    UINT strides[16], offs[16], ib_off;
    ID3D11InputLayout *il;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    int vi, a, g, ngrp = 0, nel = 0;
    uint16_t done = 0;
    struct { float screen[4], fog[4], flags[4], vattr[16][4]; } params;

    if (!ctx || !dev || !ps || !d->nindices || !nv_init()) return 0;
    switch (d->topology) {
    case D3DPT_TRIANGLELIST: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case D3DPT_LINELIST:     topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;     break;
    case D3DPT_LINESTRIP:    topo = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;    break;
    default: return 0;
    }
    if (!g_vsc_cb) g_vsc_cb = make_cb(dev, sizeof g_vsc_last);
    if (!g_vsp_cb) g_vsp_cb = make_cb(dev, sizeof params);
    if (!g_vsc_cb || !g_vsp_cb) return 0;
    vi = vsh_get(d);
    if (vi < 0) return 0;

    /* Streams: attributes interleaved in one array (same stride, within one
     * vertex of each other) share an upload and an input slot. Taken in
     * address order so each group starts at its lowest element. */
    for (;;) {
        int best = -1;
        for (a = 0; a < 16; a++)
            if ((d->inputs & (1u << a)) && !(done & (1u << a)) &&
                (d->attr[a].kind & 0x0F) != NV2A_VSH_IN_CONST &&
                (best < 0 || d->attr[a].base < d->attr[best].base))
                best = a;
        if (best < 0) break;
        done |= (uint16_t)(1u << best);
        a = best;
        for (g = 0; g < ngrp; g++)
            if (grp[g].stride == d->attr[a].stride && grp[g].stride &&
                d->attr[a].base >= grp[g].base &&
                (UINT)(d->attr[a].base - grp[g].base) < grp[g].stride)
                break;
        if (g == ngrp) {
            grp[g].base = d->attr[a].base;
            grp[g].stride = d->attr[a].stride;
            grp[g].size = 0;
            ngrp++;
        }
        {
            UINT off = (UINT)(d->attr[a].base - grp[g].base);
            if (off + d->attr[a].bytes > grp[g].size) grp[g].size = off + d->attr[a].bytes;
            el[nel].SemanticName = "V";
            el[nel].SemanticIndex = (UINT)a;
            el[nel].Format = (DXGI_FORMAT)d->attr[a].dxgi_format;
            el[nel].InputSlot = (UINT)g;
            el[nel].AlignedByteOffset = off;
            el[nel].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            el[nel].InstanceDataStepRate = 0;
            nel++;
        }
    }
    {
        UINT total = 0;
        for (g = 0; g < ngrp; g++) total += grp[g].size;
        d3d8_UpRingReserve(total, (UINT)ngrp);
    }
    for (g = 0; g < ngrp; g++) {
        offs[g] = d3d8_UpRingUpload(grp[g].base, grp[g].size, &vbs[g]);
        if (offs[g] == (UINT)-1 || !vbs[g]) return 0;
        strides[g] = grp[g].stride;
    }
    il = nel ? il_get(vi, el, nel) : NULL;
    if (nel && !il) return 0;
    ib_off = ib_upload(ctx, d->indices, d->nindices);
    if (ib_off == (UINT)-1) return 0;

    if (!g_vsc_valid || memcmp(g_vsc_last, d->vconst, sizeof g_vsc_last)) {
        memcpy(g_vsc_last, d->vconst, sizeof g_vsc_last);
        cb_write(ctx, g_vsc_cb, g_vsc_last, sizeof g_vsc_last);
        g_vsc_valid = 1;
    }
    memset(&params, 0, sizeof params);
    params.screen[0] = d->screen_w;
    params.screen[1] = d->screen_h;
    params.screen[2] = d->clip_max;
    params.fog[0] = (float)d->fog_mode;
    params.fog[1] = d->fog_p0;
    params.fog[2] = d->fog_p1;
    params.flags[0] = d->specular ? 1.0f : 0.0f;
    params.flags[1] = d->spec_alpha ? 1.0f : 0.0f;
    if (d->attr_const) memcpy(params.vattr, d->attr_const, sizeof params.vattr);
    cb_write(ctx, g_vsp_cb, &params, sizeof params);

    if (!g_ps_cb || g_ps_cb_size < d->ps_consts_size) {
        if (g_ps_cb) ID3D11Buffer_Release(g_ps_cb);
        g_ps_cb = make_cb(dev, d->ps_consts_size);
        g_ps_cb_size = g_ps_cb ? d->ps_consts_size : 0;
        if (!g_ps_cb) return 0;
    }
    cb_write(ctx, g_ps_cb, d->ps_consts, d->ps_consts_size);

    d3d8_states_apply();
    nv_set_raster(ctx, dev, d->raster);
    if (ngrp) ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, (UINT)ngrp, vbs, strides, offs);
    ID3D11DeviceContext_IASetInputLayout(ctx, il);
    ID3D11DeviceContext_IASetIndexBuffer(ctx, g_ib_ring, DXGI_FORMAT_R32_UINT, ib_off);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, topo);
    ID3D11DeviceContext_VSSetShader(ctx, g_vsh[vi].vs, NULL, 0);
    cbs[0] = g_vsc_cb;
    cbs[1] = g_vsp_cb;
    ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 2, cbs);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &g_ps_cb);
    ID3D11DeviceContext_DrawIndexed(ctx, d->nindices, 0, 0);
    return 1;
}
