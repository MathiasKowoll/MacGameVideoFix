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
 * DLSS. The game asks ntdll for wine_get_version in five places, and at one of
 * them -- the one that decides whether NVIDIA Streamline is created at all --
 * finding it means Streamline is never initialised, slSetD3DDevice answers
 * "not initialised", and the menu says DLSS is not supported. Only that call
 * is told wine_get_version does not exist: it is recognised by the code after
 * it (test rax,rax; jne; cmp [rip+x],r14), not by an address. The other four
 * still see Wine; what they change is not known. Measured 2026-09-30 on
 * D3DMetal 4.0b2: slInit returns 0, DLSS is offered, and on it looks right --
 * D3DMetal carries Streamline's DLSS to MetalFX. Frame generation does not
 * work.
 *
 * Carried on amd_fidelityfx_loader_dx12.dll, which the game imports; every
 * export is forwarded to the renamed original (runtime/build-proxy.sh).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <windows.h>
#include <stdio.h>
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

/* The rule: a geometry shader that streams out, or has no pixel shader. */
static BOOL refuse(BOOL gs, BOOL ps, UINT so_entries)
{
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
    return real_gfx(dev, desc, iid, out);
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
     * is followed by `test rax,rax; jne; cmp [rip+x],r14`. Only that caller is
     * told there is no wine_get_version; the game's other four checks still
     * see Wine. */
    if ((ULONG_PTR)name > 0xffff && !strcmp(name, "wine_get_version"))
    {
        const unsigned char *ret = __builtin_return_address(0);
        if (readable(ret, 8) && ret[0] == 0x48 && ret[1] == 0x85 && ret[2] == 0xc0 && ret[3] == 0x75
            && ret[5] == 0x4c && ret[6] == 0x39 && ret[7] == 0x35)
        {
            logline("wine_get_version hidden from the Streamline check");
            return NULL;
        }
#ifdef W3_PROBE
        logline("wine_get_version asked from elsewhere, answered");
#endif
    }
    f = o_GetProcAddress(m, name);
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
    iat_hook("KERNEL32.dll", "GetProcAddress", (void *)p_GetProcAddress, (void **)&o_GetProcAddress);
#ifdef W3_PROBE
    hook_imports();
#endif
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
