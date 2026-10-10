/*
 * w3-streamout-fix.c -- The Witcher 3 Remastered (5.00b): refuse the one
 * pipeline D3DMetal cannot compile, and let the game offer DLSS.
 *
 * WHAT IT FIXES. Since patch 5.00b the game hangs on a black window at full
 * CPU. During startup it creates a stream-output graphics pipeline -- a
 * geometry shader and no pixel shader -- and compiling that crashes Apple's
 * shader converter (libmetalirconverter) inside D3DMetal. The game's crash
 * handler then deadlocks, which is the black window.
 *
 * So pipelines with a geometry shader that either stream out or have no pixel
 * shader are refused with E_INVALIDARG before they reach D3DMetal. The game
 * carries on without them. The diagnosis and the rule are
 * github.com/tholtman1-del/witcher3-crossover-fix (MIT); this is the same
 * rule, carried the way this repository carries its other fixes.
 *
 * WHERE IT HOOKS. The game ships NVIDIA Streamline, which hides the title's own
 * device, so this creates a device of its own as soon as d3d12.dll is loaded,
 * patches the pipeline creators of D3DMetal's device class -- which the
 * title's device shares -- and releases it. Both creators are covered:
 * CreateGraphicsPipelineState (slot 10) and CreatePipelineState (slot 47).
 *
 * DLSS. The game asks ntdll for wine_get_version in several places (five in
 * the 5.00b build of 2026-09-30, six in build 25773555 of 2026-10-08), and at
 * one of them -- the one that decides whether NVIDIA Streamline is created at
 * all -- finding it means Streamline is never initialised, slSetD3DDevice
 * answers "not initialised", and the menu says DLSS is not supported. Only that
 * call is told wine_get_version does not exist: it is recognised by the code
 * after it, not by an address -- test rax,rax; jne rel8; cmp [rip+x], r64. The
 * register is NOT part of the rule: it was r14 in the 2026-09-30 build and r13
 * after the 2026-10-08 update, which is how a byte-exact match silently stopped
 * working. In build 25773555 exactly one of the six calls has that shape. The
 * others still see Wine. A call that does not match is logged with its offset
 * and the bytes after it, so the next build that moves the code is diagnosed
 * from the log alone. Measured 2026-09-30 on D3DMetal 4.0b2: slInit returns 0,
 * DLSS is offered, and on it looks right -- D3DMetal carries Streamline's DLSS
 * to MetalFX. Frame generation does not work.
 *
 * RAY TRACING, LEFT OFF ON PURPOSE. In build 25773555 the menu shows ray
 * tracing off. Measured 2026-10-09: of the other Wine checks, the call that
 * returns to witcher3.exe+0x1ee5630 -- it sets a flag the renderer reads in
 * some fourteen places -- is the one that turns it off; hidden from that call
 * alone, ray tracing comes back. It is NOT hidden here, because with ray
 * tracing on Metal refuses a pipeline in the title's growing ray-tracing state
 * object (the one created with ALLOW_STATE_OBJECT_ADDITIONS, recreated larger
 * as content streams in): "Encountered unlowered function call to
 * air.atomic.global.cmpxchg.weak.i64", a 64-bit compare-exchange the M4's GPU
 * does not have. D3DMetal 4.0b2 then calls abort() -- a crash report when that
 * lands on the main thread, a hang on the loading screen when it lands on a
 * compile worker. Refusing that state object makes the title fault instead,
 * and no setting avoids it: global illumination cannot be turned off while ray
 * tracing is on, and the same state objects are created whatever the
 * ray-tracing settings say. Every step is reproducible with the W3_RTPROBE
 * build below.
 *
 * Carried on amd_fidelityfx_loader_dx12.dll, which the game imports; every
 * export is forwarded to the renamed original (runtime/build-proxy.sh).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PATH "C:\\w3-streamout-fix.log"

static FILE *logf_;
static LONG refused, seen_gfx, seen_stream;

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (!logf_) return;
    va_start(ap, fmt); vfprintf(logf_, fmt, ap); va_end(ap);
    fputc('\n', logf_); fflush(logf_);
}

static BOOL readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof(mi))) return FALSE;
    if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return FALSE;
    return (const char *)p + n <= (const char *)mi.BaseAddress + mi.RegionSize;
}

/* A D3D12_SHADER_BYTECODE that holds a shader. */
static BOOL has_shader(const char *bc)
{
    const void *p = *(const void *const *)bc;
    SIZE_T n = *(const SIZE_T *)(bc + 8);
    return n >= 4 && readable(p, 4) && memcmp(p, "DXBC", 4) == 0;
}

#ifdef W3_RTPROBE
static BOOL allow_gs_without_ps;
static LONG let_through;
#endif

/* The rule: a geometry shader that streams out, or has no pixel shader. */
static BOOL refuse(BOOL gs, BOOL ps, UINT so_entries)
{
#ifdef W3_RTPROBE
    if (allow_gs_without_ps && gs && !so_entries && !ps)
    {
        logline("let through geometry-shader pipeline %ld with no pixel shader and no stream output",
                InterlockedIncrement(&let_through));
        return FALSE;
    }
#endif
    return gs && (so_entries || !ps);
}

typedef HRESULT (WINAPI *create_fn)(void *, const void *, REFIID, void **);
static create_fn real_gfx, real_stream;

/* D3D12_GRAPHICS_PIPELINE_STATE_DESC on x64: VS at 8, PS 24, DS 40, HS 56,
 * GS 72, then D3D12_STREAM_OUTPUT_DESC at 88 with NumEntries at 96. */
static HRESULT WINAPI my_gfx(void *dev, const void *desc, REFIID iid, void **out)
{
    const char *d = desc;
    InterlockedIncrement(&seen_gfx);
    if (d && refuse(has_shader(d + 72), has_shader(d + 24), *(const UINT *)(d + 96)))
    {
        LONG n = InterlockedIncrement(&refused);
        logline("refused stream-output pipeline %ld (graphics desc, %u entries)", n, *(const UINT *)(d + 96));
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
#ifdef W3_RTPROBE
    {
        HRESULT hr = real_gfx(dev, desc, iid, out);
        if (FAILED(hr)) logline("CreateGraphicsPipelineState failed: %08lx", (unsigned long)hr);
        return hr;
    }
#else
    return real_gfx(dev, desc, iid, out);
#endif
}

/* ID3D12Device2::CreatePipelineState. Every subobject starts pointer-aligned
 * with its type; a bytecode or stream-output payload follows at +8. PS is type
 * 2, GS type 5, STREAM_OUTPUT type 7 with NumEntries at +16. Matched only where
 * the payload really is a shader, so a stray 2 or 5 elsewhere reads as nothing. */
static HRESULT WINAPI my_stream(void *dev, const void *desc, REFIID iid, void **out)
{
    SIZE_T size, off;
    const char *st;
    BOOL gs = FALSE, ps = FALSE;
    UINT so = 0;
    InterlockedIncrement(&seen_stream);
    if (!desc) return real_stream(dev, desc, iid, out);
    size = *(const SIZE_T *)desc;
    st = *(const char *const *)((const char *)desc + 8);
    if (!st || size >= (1u << 20) || !readable(st, size)) return real_stream(dev, desc, iid, out);
    for (off = 0; off + 24 <= size; off += 8)
    {
        UINT type = *(const UINT *)(st + off);
        if (type == 2 && has_shader(st + off + 8)) ps = TRUE;
        else if (type == 5 && has_shader(st + off + 8)) gs = TRUE;
        else if (type == 7 && readable(*(const void *const *)(st + off + 8), 1)
                 && *(const UINT *)(st + off + 16) && *(const UINT *)(st + off + 16) < 256)
            so = *(const UINT *)(st + off + 16);
    }
    if (refuse(gs, ps, so))
    {
        LONG n = InterlockedIncrement(&refused);
        logline("refused stream-output pipeline %ld (stream desc, %u entries)", n, so);
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
#ifdef W3_RTPROBE
    {
        HRESULT hr = real_stream(dev, desc, iid, out);
        if (FAILED(hr)) logline("CreatePipelineState failed: %08lx", (unsigned long)hr);
        return hr;
    }
#else
    return real_stream(dev, desc, iid, out);
#endif
}

#ifdef W3_RTPROBE
/* Diagnostic build only (W3_RTPROBE), never shipped: MGVF_CFLAGS=-DW3_RTPROBE.
 * After the 2026-10-08 update the title keeps EnableRT=true in its settings
 * but shows ray tracing off, on the same engine and D3DMetal 4.0b2 that ran it
 * with ray tracing on the 30th. This build logs what D3DMetal answers the
 * title's CheckFeatureSupport (RaytracingTier 11 -- DXR 1.1 is offered), and
 * takes marker files beside the DLL, each changing one thing:
 *   w3-hide-wine-all.txt         hide wine_get_version from more callers: from
 *                                every one, or only from the witcher3.exe
 *                                offsets (hex) written in it -- 1ee5630 is the
 *                                ray-tracing gate in build 25773555
 *   w3-allow-gs-without-ps.txt   let geometry shaders with no pixel shader and
 *                                no stream output through
 *   w3-strip-state-object-additions.txt
 *   w3-refuse-state-objects.txt  see the state-object hooks
 *   w3-nvapi-rt-caps-none.txt    see the NVAPI wrapper
 * What it found is in the header. */
static BOOL hide_all;
/* Offsets in witcher3.exe read from the marker file; none means every caller. */
static unsigned long hide_at[16];
static int hide_at_count;
static BOOL hidden_here(unsigned long off)
{
    int i;
    if (!hide_at_count) return TRUE;
    for (i = 0; i < hide_at_count; i++) if (hide_at[i] == off) return TRUE;
    return FALSE;
}
static HRESULT (WINAPI *real_cfs)(void *, int, void *, UINT);
static volatile LONG seen_feature[64];
static HRESULT WINAPI my_cfs(void *dev, int feature, void *data, UINT size)
{
    HRESULT hr = real_cfs(dev, feature, data, size);
    if (feature >= 0 && feature < 64 && !InterlockedExchange(&seen_feature[feature], 1))
    {
        const int *v = data;
        if (feature == 27 && SUCCEEDED(hr) && size >= 12)
            logline("CheckFeatureSupport OPTIONS5: RaytracingTier %d, RenderPassesTier %d", v[2], v[1]);
        else if (feature == 7 && SUCCEEDED(hr) && size >= 4)
            logline("CheckFeatureSupport SHADER_MODEL: 0x%x", v[0]);
        else if (feature == 32 && SUCCEEDED(hr) && size >= 8)
            logline("CheckFeatureSupport OPTIONS7: MeshShaderTier %d, SamplerFeedbackTier %d", v[0], v[1]);
        else
            logline("CheckFeatureSupport feature %d: hr %08lx", feature, (unsigned long)hr);
    }
    return hr;
}

/* After ray tracing is offered the title dies at the first level's loading
 * screen with no exception reaching its own handler, no dump and nothing from
 * wine -- with Metal 4 on and off alike. What follows says how it leaves:
 * the ray-tracing state objects and their results, the device-removed reason
 * if it is ever asked, every exception that passes, and who ends the process. */
static void where(const void *addr, char *out, size_t n)
{
    HMODULE m = NULL;
    char path[MAX_PATH];
    const char *leaf;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           addr, &m) && m && GetModuleFileNameA(m, path, sizeof(path)))
    {
        leaf = strrchr(path, '\\');
        snprintf(out, n, "%s+0x%lx", leaf ? leaf + 1 : path, (unsigned long)((const char *)addr - (const char *)m));
    }
    else snprintf(out, n, "%p", addr);
}

static void log_stack(const char *what)
{
    void *frames[16];
    USHORT i, n = RtlCaptureStackBackTrace(1, 16, frames, NULL);
    char at[MAX_PATH + 32];
    for (i = 0; i < n; i++)
    {
        where(frames[i], at, sizeof(at));
        logline("  %s #%u %s", what, i, at);
    }
}

static HRESULT (WINAPI *real_compute)(void *, const void *, REFIID, void **);
static HRESULT WINAPI my_compute(void *dev, const void *desc, REFIID iid, void **out)
{
    HRESULT hr = real_compute(dev, desc, iid, out);
    if (FAILED(hr)) logline("CreateComputePipelineState failed: %08lx", (unsigned long)hr);
    return hr;
}

static HRESULT (WINAPI *real_gdr)(void *);
static LONG gdr_logged;
static HRESULT WINAPI my_gdr(void *dev)
{
    HRESULT hr = real_gdr(dev);
    if (hr != S_OK && InterlockedIncrement(&gdr_logged) <= 5)
    {
        logline("GetDeviceRemovedReason: %08lx", (unsigned long)hr);
        log_stack("asked from");
    }
    return hr;
}

/* D3D12_STATE_OBJECT_DESC: Type, NumSubobjects, pSubobjects; each
 * D3D12_STATE_SUBOBJECT is a type and a pointer, 16 bytes. Written BEFORE the
 * call: on 2026-10-09 D3DMetal aborted the process inside one, and a line
 * written after the call never appeared. Subobject types are counted; the
 * flags of a STATE_OBJECT_CONFIG (0) and a PIPELINE_CONFIG1 (12), the
 * recursion depth of a PIPELINE_CONFIG (10) and the payload of a SHADER_CONFIG
 * (9) are written out. */
static void log_state_object(const char *what, LONG n, const void *desc)
{
    const char *d = desc, *sub;
    UINT count, i, seen[16] = { 0 };
    char line[256];
    int len;
    if (!d || !readable(d, 16)) { logline("%s %ld: no readable desc", what, n); return; }
    count = *(const UINT *)(d + 4);
    sub = *(const char *const *)(d + 8);
    logline("%s %ld: type %d, %u subobjects", what, n, *(const int *)d, count);
    if (!sub || count > 4096 || !readable(sub, (SIZE_T)count * 16)) return;
    for (i = 0; i < count; i++)
    {
        UINT type = *(const UINT *)(sub + 16 * i);
        const UINT *p = *(const UINT *const *)(sub + 16 * i + 8);
        if (type < 16) seen[type]++;
        if (!readable(p, 8)) continue;
        if (type == 0) logline("  state object config flags 0x%x", p[0]);
        else if (type == 9) logline("  shader config: payload %u, attributes %u", p[0], p[1]);
        else if (type == 10) logline("  pipeline config: recursion %u", p[0]);
        else if (type == 12) logline("  pipeline config1: recursion %u, flags 0x%x", p[0], p[1]);
    }
    len = snprintf(line, sizeof(line), "  subobject types:");
    for (i = 0; i < 16 && len < (int)sizeof(line) - 16; i++)
        if (seen[i]) len += snprintf(line + len, sizeof(line) - len, " %u x%u", i, seen[i]);
    logline("%s", line);
}

/* The third state object the title creates asks for
 * D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS (0x4), and D3DMetal
 * 4.0b2 aborted the process inside that call on 2026-10-09 while the two
 * without it were created. With w3-strip-state-object-additions.txt beside
 * this DLL the flag is cleared for the call: the subobject's own pointer is
 * pointed at a copy of the config and put back afterwards, so association
 * subobjects that name other entries by address still find them. Measured the
 * same night: clearing it changes nothing -- the abort is the 64-bit
 * compare-exchange in that object's shaders, not the flag. */
static BOOL strip_additions;
static BOOL strip_config(const void *desc, void ***slot, void **was, UINT *copy)
{
    const char *d = desc, *sub;
    UINT count, i;
    MEMORY_BASIC_INFORMATION mi;
    if (!strip_additions || !d || !readable(d, 16)) return FALSE;
    count = *(const UINT *)(d + 4);
    sub = *(const char *const *)(d + 8);
    if (!sub || count > 4096 || !readable(sub, (SIZE_T)count * 16)) return FALSE;
    for (i = 0; i < count; i++)
    {
        void **p = (void **)(sub + 16 * i + 8);
        const UINT *cfg = *p;
        if (*(const UINT *)(sub + 16 * i) != 0 || !readable(cfg, 4) || !(cfg[0] & 0x4)) continue;
        if (!VirtualQuery(p, &mi, sizeof(mi))
            || !(mi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)))
        {
            logline("  could not clear ALLOW_STATE_OBJECT_ADDITIONS: subobjects are read-only");
            return FALSE;
        }
        *copy = cfg[0] & ~0x4u;
        *slot = p;
        *was = *p;
        *p = copy;
        logline("  ALLOW_STATE_OBJECT_ADDITIONS cleared for this call (flags 0x%x -> 0x%x)", cfg[0], *copy);
        return TRUE;
    }
    return FALSE;
}

/* With ray tracing on, Metal refuses one of the title's ray-tracing pipelines:
 * "Encountered unlowered function call to air.atomic.global.cmpxchg.weak.i64"
 * (AGXMetalG16X, 2026-10-09) -- a 64-bit compare-exchange Apple GPUs do not
 * have -- and D3DMetal aborts. The title creates the same three state objects
 * whatever its ray-tracing settings say. w3-refuse-state-objects.txt beside
 * this DLL lists, by creation order, the ones to refuse with E_INVALIDARG
 * before D3DMetal sees them: which one carries the instruction, and whether
 * the title survives without it. Measured 2026-10-09: refusing the objects
 * that ask for additions removes the Metal error -- the instruction is there,
 * not in the first two -- and the title, asking again with ever more
 * subobjects (35 up to 167), then faults at witcher3.exe+0x516d0e. */
static int refuse_so[8];
static int refuse_so_count;
/* The word "additions" in that file refuses every state object whose config
 * asks for ALLOW_STATE_OBJECT_ADDITIONS instead: refused by number, the third
 * came straight back as a fourth with the same 35 subobjects. */
static BOOL refuse_so_additions;

static BOOL asks_for_additions(const void *desc)
{
    const char *d = desc, *sub;
    UINT count, i;
    if (!d || !readable(d, 16)) return FALSE;
    count = *(const UINT *)(d + 4);
    sub = *(const char *const *)(d + 8);
    if (!sub || count > 4096 || !readable(sub, (SIZE_T)count * 16)) return FALSE;
    for (i = 0; i < count; i++)
    {
        const UINT *cfg = *(const UINT *const *)(sub + 16 * i + 8);
        if (*(const UINT *)(sub + 16 * i) == 0 && readable(cfg, 4) && (cfg[0] & 0x4)) return TRUE;
    }
    return FALSE;
}

static HRESULT (WINAPI *real_cso)(void *, const void *, REFIID, void **);
static LONG cso_seen;
static HRESULT WINAPI my_cso(void *dev, const void *desc, REFIID iid, void **out)
{
    LONG n = InterlockedIncrement(&cso_seen);
    HRESULT hr;
    void **slot = NULL, *was = NULL;
    UINT copy = 0;
    BOOL stripped;
    if (n <= 40) log_state_object("CreateStateObject", n, desc);
    {
        int i;
        if (refuse_so_additions && asks_for_additions(desc))
        {
            logline("CreateStateObject %ld refused before D3DMetal saw it (asks for additions)", n);
            if (out) *out = NULL;
            return E_INVALIDARG;
        }
        for (i = 0; i < refuse_so_count; i++)
            if (refuse_so[i] == n)
            {
                logline("CreateStateObject %ld refused before D3DMetal saw it", n);
                if (out) *out = NULL;
                return E_INVALIDARG;
            }
    }
    stripped = strip_config(desc, &slot, &was, &copy);
    hr = real_cso(dev, desc, iid, out);
    if (stripped) *slot = was;
    if (FAILED(hr) || n <= 40) logline("CreateStateObject %ld returned %08lx", n, (unsigned long)hr);
    return hr;
}

/* ID3D12Device7::AddToStateObject(addition, grow_from, riid, out). */
static HRESULT (WINAPI *real_add)(void *, const void *, void *, REFIID, void **);
static LONG add_seen;
static HRESULT WINAPI my_add(void *dev, const void *desc, void *from, REFIID iid, void **out)
{
    LONG n = InterlockedIncrement(&add_seen);
    HRESULT hr;
    if (n <= 40) log_state_object("AddToStateObject", n, desc);
    hr = real_add(dev, desc, from, iid, out);
    if (FAILED(hr) || n <= 40) logline("AddToStateObject %ld returned %08lx", n, (unsigned long)hr);
    return hr;
}

static LONG veh_logged, cxx_logged;
static LONG WINAPI rt_veh(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    char at[MAX_PATH + 32];
    /* Thread naming and debug output are not faults. */
    if (r->ExceptionCode == 0x406d1388 || r->ExceptionCode == 0x40010006 || r->ExceptionCode == 0x4001000a)
        return EXCEPTION_CONTINUE_SEARCH;
    if (r->ExceptionCode == 0xe06d7363 && InterlockedIncrement(&cxx_logged) > 5) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&veh_logged) > 40) return EXCEPTION_CONTINUE_SEARCH;
    where(r->ExceptionAddress, at, sizeof(at));
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        logline("exception %08lx at %s, thread %lu: %s %p", (unsigned long)r->ExceptionCode, at, GetCurrentThreadId(),
                r->ExceptionInformation[0] ? "write" : "read", (void *)r->ExceptionInformation[1]);
    else
        logline("exception %08lx at %s, thread %lu", (unsigned long)r->ExceptionCode, at, GetCurrentThreadId());
    return EXCEPTION_CONTINUE_SEARCH;
}

static void (WINAPI *o_ExitProcess)(UINT);
static void WINAPI p_ExitProcess(UINT code)
{
    logline("ExitProcess(%u), thread %lu", code, GetCurrentThreadId());
    log_stack("from");
    o_ExitProcess(code);
}

static BOOL (WINAPI *o_TerminateProcess)(HANDLE, UINT);
static BOOL WINAPI p_TerminateProcess(HANDLE h, UINT code)
{
    if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId())
    {
        logline("TerminateProcess(self, %u), thread %lu", code, GetCurrentThreadId());
        log_stack("from");
    }
    return o_TerminateProcess(h, code);
}
#endif

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
#ifdef W3_RTPROBE
    logline("CheckFeatureSupport %s", patch_slot(dev, 13, (void *)my_cfs, (void **)&real_cfs) ? "hooked" : "NOT hooked");
    logline("CreateComputePipelineState %s, GetDeviceRemovedReason %s",
            patch_slot(dev, 11, (void *)my_compute, (void **)&real_compute) ? "hooked" : "NOT hooked",
            patch_slot(dev, 37, (void *)my_gdr, (void **)&real_gdr) ? "hooked" : "NOT hooked");
    {
        static const GUID iid_device5 =
            { 0x8b4f173b, 0x2fea, 0x4b80, { 0x8f, 0x58, 0x43, 0x07, 0x19, 0x1a, 0xb9, 0x5d } };
        void *d5 = NULL;
        if (SUCCEEDED(((HRESULT (WINAPI *)(void *, REFIID, void **))(*(void ***)dev)[0])(dev, &iid_device5, &d5)) && d5)
        {
            logline("CreateStateObject %s", patch_slot(d5, 62, (void *)my_cso, (void **)&real_cso) ? "hooked" : "NOT hooked");
            ((ULONG (WINAPI *)(void *))(*(void ***)d5)[2])(d5);
        }
        else logline("no ID3D12Device5: CreateStateObject not hooked");
    }
    {
        static const GUID iid_device7 =
            { 0x5c014b53, 0x68a1, 0x4b9b, { 0x8b, 0xd1, 0xdd, 0x60, 0x46, 0xb9, 0x35, 0x8b } };
        void *d7 = NULL;
        if (SUCCEEDED(((HRESULT (WINAPI *)(void *, REFIID, void **))(*(void ***)dev)[0])(dev, &iid_device7, &d7)) && d7)
        {
            logline("AddToStateObject %s", patch_slot(d7, 66, (void *)my_add, (void **)&real_add) ? "hooked" : "NOT hooked");
            ((ULONG (WINAPI *)(void *))(*(void ***)d7)[2])(d7);
        }
        else logline("no ID3D12Device7: AddToStateObject not offered");
    }
#endif
    if (SUCCEEDED(((HRESULT (WINAPI *)(void *, REFIID, void **))(*(void ***)dev)[0])(dev, &iid_device2, &d2)) && d2)
    {
        b = patch_slot(d2, 47, (void *)my_stream, (void **)&real_stream);
        ((ULONG (WINAPI *)(void *))(*(void ***)d2)[2])(d2);
    }
    ((ULONG (WINAPI *)(void *))(*(void ***)dev)[2])(dev);
    logline("pipeline creators %s / %s", a ? "hooked" : "NOT hooked", b ? "hooked" : "NOT hooked");
}

#ifdef W3_PROBE
/* Diagnostic build only: what Streamline's DLSS check would see. The adapter
 * DXGI reports first (Streamline wants vendor 0x10de), and which NVIDIA pieces
 * are loaded after two minutes of play. */
#include <dxgi.h>
static void probe(void)
{
    static const GUID iid_factory1 =
        { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    static const char *const mods[] = { "sl.interposer.dll", "sl.common.dll", "sl.dlss.dll",
        "nvapi64.dll", "nvngx.dll", "_nvngx.dll", "nvngx_dlss.dll", "dxgi.dll", "d3d12.dll" };
    HRESULT (WINAPI *mk)(REFIID, void **);
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *a = NULL;
    DXGI_ADAPTER_DESC1 d;
    size_t i;
    mk = (void *)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1");
    if (mk && SUCCEEDED(mk(&iid_factory1, (void **)&f)) && f)
    {
        UINT n;
        for (n = 0; SUCCEEDED(f->lpVtbl->EnumAdapters1(f, n, &a)); n++)
        {
            if (SUCCEEDED(a->lpVtbl->GetDesc1(a, &d)))
                logline("adapter %u: vendor %04x device %04x '%ls' flags %u", n, d.VendorId, d.DeviceId,
                        d.Description, d.Flags);
            a->lpVtbl->Release(a);
        }
        f->lpVtbl->Release(f);
    }
    for (i = 0; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        HMODULE m = GetModuleHandleA(mods[i]);
        char path[MAX_PATH] = "";
        if (m) GetModuleFileNameA(m, path, sizeof(path));
        logline("module %-18s %s", mods[i], m ? path : "not loaded");
    }
}

/* ---- what the game asks Streamline and AGS, read from its own import table ---- */

typedef int (*sl_init_fn)(const void *prefs, unsigned long long sdk);
typedef int (*sl_supported_fn)(unsigned int feature, const void *adapter);
typedef int (*sl_setdev_fn)(void *dev);
typedef int (*sl_loaded_fn)(unsigned int feature, int loaded);
typedef HRESULT (WINAPI *sl_mkdev_fn)(void *, UINT, REFIID, void **);
typedef int (*ags_init_fn)(int ver, const void *cfg, void **ctx, void *info);
typedef int (*ags_mkdev_fn)(void *ctx, const void *params, const void *extParams, void *ret);
static sl_init_fn o_slInit;
static sl_supported_fn o_slIsFeatureSupported;
static sl_setdev_fn o_slSetD3DDevice;
static sl_loaded_fn o_slSetFeatureLoaded;
static sl_mkdev_fn o_slD3D12CreateDevice;
static ags_init_fn o_agsInitialize;
static ags_mkdev_fn o_agsCreateDevice;


/* sl::Preferences is only read, never changed: a copy with one field written
 * at a guessed offset replaced its resource allocator and hung the game. */
static int p_slInit(const void *prefs, unsigned long long sdk)
{
    const unsigned char *p = prefs;
    int r;
    logline("slInit(sdk %llx) showConsole %u logLevel %u; +40 %p +48 %p +56 %p +64 %p +72 %p +80 %p",
            sdk, p[32], *(const unsigned *)(p + 36), *(void *const *)(p + 40), *(void *const *)(p + 48),
            *(void *const *)(p + 56), *(void *const *)(p + 64), *(void *const *)(p + 72), *(void *const *)(p + 80));
    r = o_slInit(prefs, sdk);
    logline("slInit -> %d", r);
    return r;
}
static int p_slIsFeatureSupported(unsigned int f, const void *a)
{
    int r = o_slIsFeatureSupported(f, a);
    logline("slIsFeatureSupported(%u) -> %d", f, r);
    return r;
}
static int p_slSetD3DDevice(void *dev)
{
    int r = o_slSetD3DDevice(dev);
    logline("slSetD3DDevice(%p) -> %d", dev, r);
    return r;
}
static int p_slSetFeatureLoaded(unsigned int f, int loaded)
{
    int r = o_slSetFeatureLoaded(f, loaded);
    logline("slSetFeatureLoaded(%u, %d) -> %d", f, loaded, r);
    return r;
}
static HRESULT WINAPI p_slD3D12CreateDevice(void *ad, UINT lvl, REFIID iid, void **out)
{
    HRESULT hr = o_slD3D12CreateDevice(ad, lvl, iid, out);
    logline("sl.interposer D3D12CreateDevice(adapter %p, level %x) -> %08lx", ad, lvl, (unsigned long)hr);
    return hr;
}
static int p_agsInitialize(int ver, const void *cfg, void **ctx, void *info)
{
    int r = o_agsInitialize(ver, cfg, ctx, info);
    logline("agsInitialize(%x) -> %d", ver, r);
    return r;
}
static int p_agsCreateDevice(void *ctx, const void *params, const void *ext, void *ret)
{
    int r = o_agsCreateDevice(ctx, params, ext, ret);
    logline("agsDriverExtensionsDX12_CreateDevice -> %d", r);
    return r;
}

/* NVAPI: the game resolves nvapi_QueryInterface with GetProcAddress, then asks
 * it for each function by id. Every id is logged with whether it came back;
 * the ones below, all of four arguments or fewer, are also wrapped so their
 * status is logged. */
typedef void *(*nv_qi_fn)(unsigned int id);
typedef int (*nv4_fn)(void *, void *, void *, void *);
static nv_qi_fn o_nv_qi;
static const struct { unsigned int id; const char *name; } nv_known[] = {
    { 0x0150e828, "Initialize" }, { 0x2926aaad, "SYS_GetDriverAndBranchVersion" },
    { 0xe5ac921f, "EnumPhysicalGPUs" }, { 0xd8265d24, "GPU_GetArchInfo" },
    { 0xceee8e9f, "GPU_GetFullName" }, { 0x6c2d048c, "GetErrorMessage" },
    { 0x01053fa5, "GetInterfaceVersionString" }, { 0x2ddfb66e, "GPU_GetPCIIdentifiers" },
    { 0x1be0b8e5, "GPU_GetBusId" }, { 0xacc3da0a, "GPU_GetGPUType" },
    { 0x5f608315, "GPU_GetAdapterIdFromPhysicalGpu" }, { 0x7fe4dd35, "GPU_GetLogicalGpuInfo" },
};
#define NV_SLOTS 12
static nv4_fn nv_orig[NV_SLOTS];
#define NV_WRAP(i) static int nv_w##i(void *a, void *b, void *c, void *d) \
    { int r = nv_orig[i](a, b, c, d); logline("  NvAPI_%s -> %d", nv_known[i].name, r); \
      if (nv_known[i].id == 0x2926aaad && !r) logline("    driver %u branch %s", *(unsigned *)a, (char *)b); \
      return r; }
NV_WRAP(0) NV_WRAP(1) NV_WRAP(2) NV_WRAP(3) NV_WRAP(4) NV_WRAP(5)
NV_WRAP(6) NV_WRAP(7) NV_WRAP(8) NV_WRAP(9) NV_WRAP(10) NV_WRAP(11)
static void *const nv_wrappers[NV_SLOTS] = { nv_w0, nv_w1, nv_w2, nv_w3, nv_w4, nv_w5,
    nv_w6, nv_w7, nv_w8, nv_w9, nv_w10, nv_w11 };

static void *p_nv_qi(unsigned int id)
{
    void *f = o_nv_qi(id);
    int i;
    for (i = 0; i < NV_SLOTS; i++)
        if (nv_known[i].id == id)
        {
            logline("nvapi_QueryInterface(%08x %s) -> %p", id, nv_known[i].name, f);
            if (!f) return NULL;
            nv_orig[i] = f;
            return nv_wrappers[i];
        }
    logline("nvapi_QueryInterface(%08x) -> %p", id, f);
    return f;
}

#endif

#if defined(W3_RTPROBE) && !defined(W3_PROBE)
/* Every launch with ray tracing on, D3DMetal 4.0b2 logged "Unsupported:
 * NvAPI_D3D12_GetRaytracingCaps" for opacity micromaps and thread reordering
 * (and an unknown type 6), then "Cannot create pipeline state" and abort().
 * Launches with ray tracing off asked none of it. What D3DMetal actually
 * answers is logged here; with w3-nvapi-rt-caps-none.txt beside this DLL the
 * caps are reported to the title as zero -- no such feature. The id
 * 0x85a6c2a0 is the one D3DMetal's own NVAPI table pairs with that name.
 * Measured 2026-10-09: D3DMetal already answers status 0 with caps 0 for
 * thread reordering and opacity micromaps (-1 for type 6), so NVAPI is not
 * what breaks ray tracing. */
typedef void *(*rt_nv_qi_fn)(unsigned int id);
typedef int (*rt_caps_fn)(void *dev, int type, void *data, size_t size);
static rt_nv_qi_fn rt_o_qi;
static rt_caps_fn rt_o_caps;
static BOOL nv_rt_caps_none;
static unsigned int rt_qi_ids[128];
static LONG rt_qi_count;

static int rt_caps(void *dev, int type, void *data, size_t size)
{
    int r = rt_o_caps(dev, type, data, size);
    unsigned int v = (data && size >= 4 && readable(data, 4)) ? *(const unsigned int *)data : 0xffffffffu;
    logline("NvAPI_D3D12_GetRaytracingCaps(type %d, size %u) -> status %d, caps 0x%x",
            type, (unsigned)size, r, v);
    if (nv_rt_caps_none && data && size && size <= 64)
    {
        memset(data, 0, size);
        logline("  reported to the title as none");
    }
    return r;
}

static void *rt_qi(unsigned int id)
{
    void *f = rt_o_qi(id);
    LONG i, n = rt_qi_count;
    for (i = 0; i < n && i < 128; i++) if (rt_qi_ids[i] == id) break;
    if (i == n && n < 128)
    {
        rt_qi_ids[n] = id;
        rt_qi_count = n + 1;
        logline("nvapi_QueryInterface(%08x) -> %s", id, f ? "offered" : "not offered");
    }
    if (id == 0x85a6c2a0u && f)
    {
        rt_o_caps = (rt_caps_fn)f;
        return (void *)rt_caps;
    }
    return f;
}
#endif

/* ---- DLSS: the Wine check in front of Streamline ---- */

/* Replace one named import of the executable. */
static void iat_hook(const char *dll, const char *name, void *repl, void **was)
{
    char *base = (char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (void *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; d->Name; d++)
    {
        IMAGE_THUNK_DATA *names, *slots;
        if (lstrcmpiA(base + d->Name, dll)) continue;
        names = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        slots = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++)
        {
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp(((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, name)) continue;
            VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            *was = (void *)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            logline("import %s!%s hooked", dll, name);
            return;
        }
    }
    logline("import %s!%s not found", dll, name);
}

typedef FARPROC (WINAPI *gpa_fn)(HMODULE, LPCSTR);
static gpa_fn o_GetProcAddress;
static FARPROC WINAPI p_GetProcAddress(HMODULE m, LPCSTR name)
{
    FARPROC f;
    /* The one Wine check that decides whether Streamline is created: the call
     * is followed by `test rax,rax; jne rel8; cmp [rip+x], r64` -- 48 85 c0,
     * 75 xx, then REX.W (48 or 4c) 39 with a RIP-relative ModRM (mod 00, rm
     * 101), whatever the register. Only that caller is told there is no
     * wine_get_version; the game's other checks still see Wine. */
    if ((ULONG_PTR)name > 0xffff && !strcmp(name, "wine_get_version"))
    {
        const unsigned char *ret = __builtin_return_address(0);
        if (readable(ret, 8) && ret[0] == 0x48 && ret[1] == 0x85 && ret[2] == 0xc0 && ret[3] == 0x75
            && (ret[5] == 0x4c || ret[5] == 0x48) && ret[6] == 0x39 && (ret[7] & 0xc7) == 0x05)
        {
            logline("wine_get_version hidden from the Streamline check");
            return NULL;
        }
        if (readable(ret, 8))
        {
            const unsigned char *exe = (const unsigned char *)GetModuleHandleW(NULL);
#ifdef W3_RTPROBE
            if (hide_all && hidden_here((unsigned long)(ret - exe)))
            {
                logline("wine_get_version hidden at witcher3.exe+0x%lx (every-caller test)", (unsigned long)(ret - exe));
                return NULL;
            }
#endif
            logline("wine_get_version asked at witcher3.exe+0x%lx, answered; next bytes %02x %02x %02x %02x %02x %02x %02x %02x",
                    (unsigned long)(ret - exe), ret[0], ret[1], ret[2], ret[3], ret[4], ret[5], ret[6], ret[7]);
        }
    }
    f = o_GetProcAddress(m, name);
#if defined(W3_RTPROBE) && !defined(W3_PROBE)
    if ((ULONG_PTR)name > 0xffff && !strcmp(name, "nvapi_QueryInterface") && f)
    {
        logline("GetProcAddress(nvapi_QueryInterface) wrapped");
        rt_o_qi = (rt_nv_qi_fn)f;
        return (FARPROC)rt_qi;
    }
#endif
#ifdef W3_PROBE
    if ((ULONG_PTR)name > 0xffff && !strcmp(name, "nvapi_QueryInterface") && f)
    {
        logline("GetProcAddress(nvapi_QueryInterface) -> %p", (void *)f);
        o_nv_qi = (nv_qi_fn)f;
        return (FARPROC)p_nv_qi;
    }
#endif
    return f;
}

#ifdef W3_PROBE
static void hook_imports(void)
{
    iat_hook("sl.interposer.dll", "slInit", (void *)p_slInit, (void **)&o_slInit);
    iat_hook("sl.interposer.dll", "slIsFeatureSupported", (void *)p_slIsFeatureSupported, (void **)&o_slIsFeatureSupported);
    iat_hook("sl.interposer.dll", "slSetD3DDevice", (void *)p_slSetD3DDevice, (void **)&o_slSetD3DDevice);
    iat_hook("sl.interposer.dll", "slSetFeatureLoaded", (void *)p_slSetFeatureLoaded, (void **)&o_slSetFeatureLoaded);
    iat_hook("sl.interposer.dll", "D3D12CreateDevice", (void *)p_slD3D12CreateDevice, (void **)&o_slD3D12CreateDevice);
    iat_hook("amd_ags_x64.dll", "agsInitialize", (void *)p_agsInitialize, (void **)&o_agsInitialize);
    iat_hook("amd_ags_x64.dll", "agsDriverExtensionsDX12_CreateDevice", (void *)p_agsCreateDevice, (void **)&o_agsCreateDevice);
}
#endif

/* Hook the moment d3d12.dll is loaded: the pipeline is built during startup,
 * and a hook that arrives after it is no hook at all. */
static DWORD WINAPI worker(void *unused)
{
    DWORD t;
    (void)unused;
    for (t = 0; t < 120000; t++)
    {
        if (GetModuleHandleA("d3d12.dll")) { hook_class(); break; }
        Sleep(1);
    }
    if (t >= 120000) logline("d3d12.dll never loaded");
#ifdef W3_PROBE
    Sleep(60000);
#else
    Sleep(120000);
#endif
    logline("summary: %ld graphics and %ld stream pipelines seen, %ld refused",
            seen_gfx, seen_stream, refused);
#ifdef W3_PROBE
    probe();
#endif
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    logf_ = fopen(LOG_PATH, "w");
    logline("w3-streamout-fix loaded");
#ifdef W3_RTPROBE
    {
        char p[MAX_PATH + 32];
        DWORD n = GetModuleFileNameA(inst, p, MAX_PATH);
        char *s = n ? strrchr(p, '\\') : NULL;
        if (s)
        {
            FILE *f;
            strcpy(s + 1, "w3-hide-wine-all.txt");
            if ((f = fopen(p, "r")))
            {
                /* Hex offsets anywhere in the file (0x optional) narrow the
                 * test to those callers; a file with none hides from all. */
                char tok[64];
                hide_all = TRUE;
                while (hide_at_count < 16 && fscanf(f, "%63s", tok) == 1)
                {
                    char *end;
                    unsigned long v = strtoul(tok, &end, 16);
                    if (*end == 0 && v > 0xffff) hide_at[hide_at_count++] = v;
                }
                fclose(f);
            }
            strcpy(s + 1, "w3-allow-gs-without-ps.txt");
            allow_gs_without_ps = GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
            strcpy(s + 1, "w3-refuse-state-objects.txt");
            if ((f = fopen(p, "r")))
            {
                char tok[32];
                while (fscanf(f, "%31s", tok) == 1)
                {
                    if (!strcmp(tok, "additions")) refuse_so_additions = TRUE;
                    else if (refuse_so_count < 8 && atoi(tok) > 0) refuse_so[refuse_so_count++] = atoi(tok);
                }
                fclose(f);
            }
            strcpy(s + 1, "w3-strip-state-object-additions.txt");
            strip_additions = GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#ifndef W3_PROBE
            strcpy(s + 1, "w3-nvapi-rt-caps-none.txt");
            nv_rt_caps_none = GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#endif
        }
        if (!hide_all) logline("rt probe: wine hidden from: the Streamline check only");
        else if (!hide_at_count) logline("rt probe: wine hidden from: every caller");
        else
        {
            int i;
            for (i = 0; i < hide_at_count; i++) logline("rt probe: wine hidden from: witcher3.exe+0x%lx", hide_at[i]);
        }
        logline("rt probe: geometry shaders without a pixel shader or stream output: %s",
                allow_gs_without_ps ? "let through" : "refused");
        logline("rt probe: ALLOW_STATE_OBJECT_ADDITIONS: %s", strip_additions ? "cleared" : "passed on");
        {
            int i;
            for (i = 0; i < refuse_so_count; i++) logline("rt probe: state object %d will be refused", refuse_so[i]);
            if (refuse_so_additions) logline("rt probe: state objects asking for additions will be refused");
        }
#ifndef W3_PROBE
        logline("rt probe: NVAPI ray-tracing caps: %s", nv_rt_caps_none ? "reported as none" : "passed on");
#endif
        AddVectoredExceptionHandler(1, rt_veh);
        iat_hook("KERNEL32.dll", "ExitProcess", (void *)p_ExitProcess, (void **)&o_ExitProcess);
        iat_hook("KERNEL32.dll", "TerminateProcess", (void *)p_TerminateProcess, (void **)&o_TerminateProcess);
    }
#endif
    iat_hook("KERNEL32.dll", "GetProcAddress", (void *)p_GetProcAddress, (void **)&o_GetProcAddress);
#ifdef W3_PROBE
    hook_imports();
#endif
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
