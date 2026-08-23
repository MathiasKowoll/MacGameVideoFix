/*
 * Marvel's Spider-Man 2 -- why the characters stand in their bind pose.
 *
 * A T-pose means the skinning never ran, or ran and produced nothing. On a
 * D3D12 title that is a compute pass, and the two ways it goes quiet are:
 *
 *   1. the pipeline state for it was never created, because
 *      CreateComputePipelineState failed and the game did not check;
 *   2. it was created and the dispatch produced zeros.
 *
 * Only the first is visible from outside the game, and it is visible cheaply:
 * every failed CreateComputePipelineState, CreateGraphicsPipelineState and
 * CreateRootSignature is a line here. If the log is empty of those, the fault
 * is not in pipeline creation and the next question is a different one --
 * possibly not a graphics question at all, since an engine that skins on the
 * CPU with wide SIMD would show the same symptom for reasons no D3D hook can
 * see.
 *
 * This observes. It changes nothing.
 *
 * The carrier is amd_ags_x64.dll: the game imports it statically, so this
 * loads before the renderer starts, and AMD's GPU services library has nothing
 * to do with skinning.
 *
 * D3D12 does not arrive through d3d12.dll here. The executable has no import
 * of it at all -- it comes through sl.interposer.dll, NVIDIA Streamline, which
 * is a drop-in exporting D3D12CreateDevice. Nioh 3 does the same thing, and the
 * lesson from it is that hooking d3d12.dll alone finds nothing.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\sm2-observe.log"

static const char *exe_tag_(void)
{
    static char tag[64];
    if (!tag[0])
    {
        char path[MAX_PATH];
        const char *base = path, *p;
        DWORD len = GetModuleFileNameA(NULL, path, sizeof(path) - 1);
        if (!len) return "?";
        path[len] = 0;
        for (p = path; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
        lstrcpynA(tag, base, sizeof(tag));
    }
    return tag;
}

static CRITICAL_SECTION log_lock;
static BOOL log_ready;

static void logf_(const char *fmt, ...)
{
    char buf[1024], line[1152];
    va_list ap;
    HANDLE h;
    DWORD wrote;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    n = _snprintf(line, sizeof(line) - 2, "[%s] %s\r\n", exe_tag_(), buf);
    if (n < 0) return;

    if (log_ready) EnterCriticalSection(&log_lock);
    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, line, (DWORD)n, &wrote, NULL);
        CloseHandle(h);
    }
    if (log_ready) LeaveCriticalSection(&log_lock);
}

/* ------------------------------------------------------------------ hooking */

static void *patch_vtable_slot(void *obj, int slot, void *replacement)
{
    void **vtbl = *(void ***)obj;
    void *was = vtbl[slot];
    DWORD old;

    if (was == replacement) return NULL;
    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old))
        return NULL;
    vtbl[slot] = replacement;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
    return was;
}

/* ------------------------------------------------------- what we are told */

/*
 * ID3D12Device vtable, the slots this cares about.
 *
 * 10 CreateGraphicsPipelineState, 11 CreateComputePipelineState,
 * 13 CheckFeatureSupport, 16 CreateRootSignature. Counted from the header, and
 * the two this project has already used on other titles -- 27
 * CreateCommittedResource and 32 OpenSharedHandle -- land where this ordering
 * predicts, which is the cheapest confirmation available.
 */
#define SLOT_CREATE_GRAPHICS_PSO 10
#define SLOT_CREATE_COMPUTE_PSO  11
#define SLOT_CHECK_FEATURE       13
#define SLOT_CREATE_ROOT_SIG     16

static HRESULT (WINAPI *real_create_graphics_pso)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *real_create_compute_pso)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *real_check_feature)(void *, UINT, void *, UINT);
static HRESULT (WINAPI *real_create_root_sig)(void *, UINT, const void *, SIZE_T, REFIID, void **);

static LONG graphics_ok, graphics_bad, compute_ok, compute_bad, rootsig_ok, rootsig_bad;

/* Bounded per kind, so a failing renderer cannot fill the disk while still
 * reporting the first of everything. */
static BOOL tell_(LONG *counter, LONG limit)
{
    return InterlockedIncrement(counter) <= limit;
}

static HRESULT WINAPI my_create_compute_pso(void *self, const void *desc,
                                            REFIID iid, void **out)
{
    HRESULT hr = real_create_compute_pso(self, desc, iid, out);
    if (SUCCEEDED(hr)) InterlockedIncrement(&compute_ok);
    else
    {
        static LONG told;
        InterlockedIncrement(&compute_bad);
        if (tell_(&told, 40))
        {
            /* D3D12_COMPUTE_PIPELINE_STATE_DESC: pRootSignature at 0,
             * CS.pShaderBytecode at 8, CS.BytecodeLength at 16. */
            const char *d = (const char *)desc;
            logf_("FAIL CreateComputePipelineState -> 0x%08lx  root=%p  CS %llu bytes",
                  (unsigned long)hr,
                  desc ? *(void *const *)(d + 0) : NULL,
                  (unsigned long long)(desc ? *(const SIZE_T *)(d + 16) : 0));
        }
    }
    return hr;
}

static HRESULT WINAPI my_create_graphics_pso(void *self, const void *desc,
                                             REFIID iid, void **out)
{
    HRESULT hr = real_create_graphics_pso(self, desc, iid, out);
    if (SUCCEEDED(hr)) InterlockedIncrement(&graphics_ok);
    else
    {
        static LONG told;
        InterlockedIncrement(&graphics_bad);
        if (tell_(&told, 40))
        {
            /* D3D12_GRAPHICS_PIPELINE_STATE_DESC: pRootSignature 0, VS at 8. */
            const char *d = (const char *)desc;
            logf_("FAIL CreateGraphicsPipelineState -> 0x%08lx  root=%p  VS %llu bytes",
                  (unsigned long)hr,
                  desc ? *(void *const *)(d + 0) : NULL,
                  (unsigned long long)(desc ? *(const SIZE_T *)(d + 16) : 0));
        }
    }
    return hr;
}

static HRESULT WINAPI my_create_root_sig(void *self, UINT node, const void *blob,
                                         SIZE_T len, REFIID iid, void **out)
{
    HRESULT hr = real_create_root_sig(self, node, blob, len, iid, out);
    if (SUCCEEDED(hr)) InterlockedIncrement(&rootsig_ok);
    else
    {
        static LONG told;
        InterlockedIncrement(&rootsig_bad);
        if (tell_(&told, 20))
            logf_("FAIL CreateRootSignature -> 0x%08lx  %llu bytes",
                  (unsigned long)hr, (unsigned long long)len);
    }
    return hr;
}

/*
 * Feature queries, reported once per feature id.
 *
 * A renderer that asks whether it may use wave intrinsics, or which shader
 * model it has, and is told something surprising will take a different path
 * without saying so. The answer is more interesting than the question here, so
 * both are recorded, and the first four bytes of the returned structure are
 * enough for the ones that matter.
 */
static HRESULT WINAPI my_check_feature(void *self, UINT feature, void *data, UINT size)
{
    HRESULT hr = real_check_feature(self, feature, data, size);
    static LONG seen[64];
    if (feature < 64 && InterlockedCompareExchange(&seen[feature], 1, 0) == 0)
        logf_("CheckFeatureSupport(%u, %u bytes) -> 0x%08lx  first dword 0x%08lx",
              feature, size, (unsigned long)hr,
              (SUCCEEDED(hr) && data && size >= 4) ? *(const unsigned long *)data : 0);
    return hr;
}

/* ----------------------------------------------------------- device arrival */

static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, REFIID, void **);
static BOOL device_watched;

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT min_level,
                                           REFIID iid, void **device)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, min_level, iid, device);

    logf_("D3D12CreateDevice(min level 0x%x) -> 0x%08lx%s",
          min_level, (unsigned long)hr, device && *device ? "" : "  (no device)");

    if (SUCCEEDED(hr) && device && *device && !device_watched)
    {
        device_watched = TRUE;
        real_create_graphics_pso = patch_vtable_slot(*device, SLOT_CREATE_GRAPHICS_PSO,
                                                     my_create_graphics_pso);
        real_create_compute_pso  = patch_vtable_slot(*device, SLOT_CREATE_COMPUTE_PSO,
                                                     my_create_compute_pso);
        real_check_feature       = patch_vtable_slot(*device, SLOT_CHECK_FEATURE,
                                                     my_check_feature);
        real_create_root_sig     = patch_vtable_slot(*device, SLOT_CREATE_ROOT_SIG,
                                                     my_create_root_sig);
        logf_("watching pipeline creation: graphics %s, compute %s, root sig %s, features %s",
              real_create_graphics_pso ? "yes" : "NO",
              real_create_compute_pso  ? "yes" : "NO",
              real_create_root_sig     ? "yes" : "NO",
              real_check_feature       ? "yes" : "NO");
    }
    return hr;
}

/* ------------------------------------------------ finding D3D12CreateDevice */

static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI my_GetProcAddress(HMODULE mod, LPCSTR name)
{
    FARPROC p = real_GetProcAddress(mod, name);
    if (p && name && (ULONG_PTR)name > 0xffff
        && lstrcmpA(name, "D3D12CreateDevice") == 0 && !real_D3D12CreateDevice)
    {
        char who[MAX_PATH] = "?";
        GetModuleFileNameA(mod, who, sizeof(who) - 1);
        real_D3D12CreateDevice = (void *)p;
        logf_("D3D12CreateDevice resolved from %s", who);
        return (FARPROC)my_D3D12CreateDevice;
    }
    if (p && name && (ULONG_PTR)name > 0xffff
        && lstrcmpA(name, "D3D12CreateDevice") == 0)
        return (FARPROC)my_D3D12CreateDevice;
    return p;
}

/*
 * Rewrite one imported function in this process's own import table.
 *
 * By name and by ordinal both: Wo Long imports d3d12 by ordinal 101 and a
 * by-name walk finds nothing there. Streamline is imported by name, and
 * delay-loaded, which is why GetProcAddress is watched as well.
 */
static void *hook_import(const char *dll, const char *func, WORD ordinal, void *replacement)
{
    HMODULE base = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;
    void *was = NULL;

    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)((char *)base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + rva); imp->Name; ++imp)
    {
        const char *name = (const char *)base + imp->Name;
        IMAGE_THUNK_DATA *orig, *iat;
        if (lstrcmpiA(name, dll) != 0) continue;
        orig = (IMAGE_THUNK_DATA *)((char *)base + (imp->OriginalFirstThunk
                                                    ? imp->OriginalFirstThunk
                                                    : imp->FirstThunk));
        iat  = (IMAGE_THUNK_DATA *)((char *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; ++orig, ++iat)
        {
            BOOL match;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                match = ordinal && IMAGE_ORDINAL(orig->u1.Ordinal) == ordinal;
            else
            {
                IMAGE_IMPORT_BY_NAME *n =
                    (IMAGE_IMPORT_BY_NAME *)((char *)base + orig->u1.AddressOfData);
                match = func && lstrcmpiA((const char *)n->Name, func) == 0;
            }
            if (!match) continue;
            {
                DWORD old;
                if (!VirtualProtect(&iat->u1.Function, sizeof(void *),
                                    PAGE_READWRITE, &old)) continue;
                was = (void *)(ULONG_PTR)iat->u1.Function;
                iat->u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            }
            return was;
        }
    }
    return NULL;
}

static DWORD WINAPI worker(void *unused)
{
    void *was;
    (void)unused;

    logf_("sm2-observe: watching. Nothing here changes what the game does.");

    was = hook_import("sl.interposer.dll", "D3D12CreateDevice", 0,
                      (void *)my_D3D12CreateDevice);
    if (was) { real_D3D12CreateDevice = was; logf_("hooked sl.interposer.dll!D3D12CreateDevice"); }

    if (!real_D3D12CreateDevice)
    {
        was = hook_import("d3d12.dll", "D3D12CreateDevice", 101,
                          (void *)my_D3D12CreateDevice);
        if (was) { real_D3D12CreateDevice = was; logf_("hooked d3d12.dll!D3D12CreateDevice"); }
    }

    real_GetProcAddress = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                 "GetProcAddress");
    was = hook_import("KERNEL32.dll", "GetProcAddress", 0, (void *)my_GetProcAddress);
    logf_("D3D12CreateDevice %s, GetProcAddress %s",
          real_D3D12CreateDevice ? "hooked at startup" : "waiting for GetProcAddress",
          was ? "hooked" : "not imported");
    return 0;
}

/*
 * A summary on the way out, because the interesting number is a ratio.
 *
 * "Forty compute pipelines failed" means nothing without knowing whether four
 * hundred succeeded or forty-one were attempted.
 */
static void report(void)
{
    logf_("totals: graphics PSO %ld ok / %ld failed, compute PSO %ld ok / %ld failed, "
          "root sig %ld ok / %ld failed",
          graphics_ok, graphics_bad, compute_ok, compute_bad, rootsig_ok, rootsig_bad);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&log_lock);
        log_ready = TRUE;
        DisableThreadLibraryCalls(inst);
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    else if (reason == DLL_PROCESS_DETACH)
        report();
    return TRUE;
}
