/*
 * ctl-window-fix.c -- CONTROL Resonant: hide the window interiors D3DMetal
 * cannot draw.
 *
 * WHAT IT FIXES. On D3DMetal 4.0b2 the lit rooms behind the city's windows
 * render as flat, saturated colours -- violet, cyan, yellow -- that spread to
 * neighbouring windows as the camera moves. One pixel shader draws them, the
 * one whose entry point is `interiorPS`: it picks a room panorama out of an
 * unbounded Texture2D[] with a NonUniformResourceIndex.
 *
 * Two faults meet there. D3DMetal runs that read as if the index were uniform
 * -- a rebuilt shader that serves each index on its own drew the rooms as they
 * look on Windows, which proves it -- and even so, after a minute of play the
 * colours came back, with every non-uniform shader rebuilt, so a second fault
 * lies outside the shaders (texture streaming is the suspect, not measured).
 * diagnostics/ctl-window-shaders.md has all eleven rounds.
 *
 * So this hides interiorPS: every pipeline built with it is created with each
 * render target's write mask at 0. The windows are dark glass; nothing else in
 * the frame changes. A game update that changes the shader changes its hash,
 * and this then matches nothing and does nothing.
 *
 * WHERE IT HOOKS. NVIDIA Streamline hides the title's own device, so this
 * creates a device of its own once d3d12.dll is loaded, patches the pipeline
 * creators of D3DMetal's device class -- which the title's device shares --
 * and releases it. Nothing inside Streamline is touched.
 *
 * Carried on PxFoundation_x64.dll, which the game imports; every export is
 * forwarded to the renamed original (runtime/build-proxy.sh).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define LOG_PATH "C:\\ctl-window-fix.log"

/* FNV-1a of interiorPS's whole DXBC container, as CONTROL Resonant ships it. */
static const unsigned long long hidden[] = { 0xcc6f0f9fb056f7f5ULL };

static FILE *logf_;
static LONG muted, unmatched;

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (!logf_) return;
    va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap);
    fputc('\n', logf_); fflush(logf_);
}

static unsigned long long fnv(const unsigned char *p, SIZE_T n)
{
    unsigned long long h = 0xcbf29ce484222325ULL;
    while (n--) { h ^= *p++; h *= 0x100000001b3ULL; }
    return h;
}

static BOOL readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof(mi))) return FALSE;
    if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return FALSE;
    return (const char *)p + n <= (const char *)mi.BaseAddress + mi.RegionSize;
}

static BOOL is_hidden(const void *code, SIZE_T len)
{
    unsigned long long h;
    size_t i;
    if (!readable(code, 4) || memcmp(code, "DXBC", 4) != 0 || !readable(code, len)) return FALSE;
    h = fnv(code, len);
    for (i = 0; i < sizeof(hidden) / sizeof(hidden[0]); i++) if (hidden[i] == h) return TRUE;
    return FALSE;
}

/* A D3D12_BLEND_DESC: two BOOLs, then eight 40-byte render-target blocks whose
 * RenderTargetWriteMask sits at +36. */
static void mute(char *blend)
{
    int i;
    for (i = 0; i < 8; i++) ((unsigned char *)blend)[8 + i * 40 + 36] = 0;
}

/* In a pipeline stream the BLEND subobject is type 8, its payload four bytes
 * after the type. A slot counts only when every field looks like a blend
 * description; with IndependentBlend off the title leaves entries 1-7 zero. */
static char *find_blend(char *s, SIZE_T size)
{
    SIZE_T off;
    for (off = 0; off + 4 + 8 + 8 * 40 <= size; off += 8)
    {
        char *b;
        int i, ok = 1;
        if (*(const UINT *)(s + off) != 8) continue;
        b = s + off + 4;
        if (*(const UINT *)b > 1 || *(const UINT *)(b + 4) > 1) continue;
        for (i = 0; i < 8 && ok; i++)
        {
            const UINT *rt = (const UINT *)(b + 8 + i * 40);
            if (rt[0] > 1 || rt[1] > 1 || rt[2] > 21 || rt[3] > 21 || rt[4] > 5) ok = 0;
            if (i == 0 && (rt[2] == 0 || rt[4] == 0)) ok = 0;
            if ((unsigned char)b[8 + i * 40 + 36] > 15) ok = 0;
        }
        if (ok) return b;
    }
    return NULL;
}

typedef HRESULT (WINAPI *create_fn)(void *, const void *, REFIID, void **);
static create_fn real_gfx, real_stream;

/* ID3D12Device::CreateGraphicsPipelineState. D3D12_GRAPHICS_PIPELINE_STATE_DESC
 * on x64: the pixel shader at 24, the blend description at 120. */
static HRESULT WINAPI my_gfx(void *dev, const void *desc, REFIID iid, void **out)
{
    if (desc && is_hidden(*(void *const *)((const char *)desc + 24), *(const SIZE_T *)((const char *)desc + 32)))
    {
        /* sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC) on x64: no more is read. */
        char copy[656];
        HRESULT hr;
        memcpy(copy, desc, sizeof(copy));
        mute(copy + 120);
        hr = real_gfx(dev, copy, iid, out);
        InterlockedIncrement(&muted);
        logline("interiorPS hidden (graphics) -> %08lx", (unsigned long)hr);
        return hr;
    }
    return real_gfx(dev, desc, iid, out);
}

/* ID3D12Device2::CreatePipelineState: the pixel shader is the subobject of
 * type 2, a {pointer, size} pair after the type. */
static HRESULT WINAPI my_stream(void *dev, const void *desc, REFIID iid, void **out)
{
    SIZE_T size, off;
    const char *st;
    if (!desc) return real_stream(dev, desc, iid, out);
    size = *(const SIZE_T *)desc;
    st = *(const char *const *)((const char *)desc + 8);
    if (!st || size >= (1u << 20) || !readable(st, size)) return real_stream(dev, desc, iid, out);
    for (off = 0; off + 24 <= size; off += 8)
    {
        if (*(const UINT *)(st + off) != 2) continue;
        if (!is_hidden(*(const void *const *)(st + off + 8), *(const SIZE_T *)(st + off + 16))) break;
        {
            char *copy = HeapAlloc(GetProcessHeap(), 0, size), *b;
            SIZE_T d2[2];
            HRESULT hr;
            if (!copy) break;
            memcpy(copy, st, size);
            b = find_blend(copy, size);
            if (b) mute(b);
            else InterlockedIncrement(&unmatched);
            d2[0] = size; d2[1] = (SIZE_T)copy;
            hr = real_stream(dev, d2, iid, out);
            HeapFree(GetProcessHeap(), 0, copy);
            if (b) InterlockedIncrement(&muted);
            logline("interiorPS %s -> %08lx", b ? "hidden" : "found, but no blend state to mute", (unsigned long)hr);
            return hr;
        }
    }
    return real_stream(dev, desc, iid, out);
}

static BOOL patch_slot(void *obj, int slot, void *repl, void **was)
{
    void **vtbl = *(void ***)obj;
    DWORD old;
    if (vtbl[slot] == repl) return TRUE;
    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
    *was = vtbl[slot];
    vtbl[slot] = repl;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
    return TRUE;
}

/* ID3D12Device slot 10 CreateGraphicsPipelineState; ID3D12Device2 slot 47
 * CreatePipelineState. The patch is on the class, so it holds for every device
 * D3DMetal hands out, the title's included. */
static void hook_class(void)
{
    static const GUID iid_device =
        { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
    static const GUID iid_device2 =
        { 0x30baa41e, 0xb15b, 0x475c, { 0xa0, 0xbb, 0x1a, 0xf5, 0xc5, 0xb6, 0x43, 0x28 } };
    HRESULT (WINAPI *create)(void *, UINT, REFIID, void **);
    void *dev = NULL, *d2 = NULL;
    BOOL a, b = FALSE;
    HRESULT hr;
    create = (void *)GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12CreateDevice");
    if (!create) { logline("no D3D12CreateDevice in d3d12.dll"); return; }
    hr = create(NULL, 0xb000, &iid_device, &dev);
    if (FAILED(hr) || !dev) { logline("could not create a device: %08lx", (unsigned long)hr); return; }
    a = patch_slot(dev, 10, (void *)my_gfx, (void **)&real_gfx);
    if (SUCCEEDED(((HRESULT (WINAPI *)(void *, REFIID, void **))(*(void ***)dev)[0])(dev, &iid_device2, &d2)) && d2)
    {
        b = patch_slot(d2, 47, (void *)my_stream, (void **)&real_stream);
        ((ULONG (WINAPI *)(void *))(*(void ***)d2)[2])(d2);
    }
    ((ULONG (WINAPI *)(void *))(*(void ***)dev)[2])(dev);
    logline("pipeline creators %s / %s", a ? "hooked" : "NOT hooked", b ? "hooked" : "NOT hooked");
}

/* Wait for d3d12.dll, then give the title three seconds to create its own
 * device first -- the timing every measured run used. */
static DWORD WINAPI worker(void *unused)
{
    DWORD t, loaded = 0;
    (void)unused;
    for (t = 0; t < 60000; t += 2)
    {
        if (!loaded && GetModuleHandleA("d3d12.dll")) loaded = GetTickCount();
        if (loaded && GetTickCount() - loaded >= 3000) { hook_class(); break; }
        Sleep(2);
    }
    if (!loaded) logline("d3d12.dll never loaded");
    Sleep(120000);
    logline("after two minutes: %ld pipelines hidden, %ld without a blend state", muted, unmatched);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    logf_ = fopen(LOG_PATH, "w");
    logline("ctl-window-fix loaded");
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
