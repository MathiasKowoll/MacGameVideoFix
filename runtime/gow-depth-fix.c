/*
 * gow-depth-fix.c -- God of War (2018): white specks on characters.
 *
 * WHAT IT FIXES. Characters, Kratos first, are covered in white specks in dim
 * light. The game draws a depth-only prepass and then draws the same geometry
 * again into its G-buffer with the depth test set to EQUAL: a pixel survives only
 * if its depth matches the prepass exactly. On Windows the GPU computes the same
 * position bit for bit in both passes. Translated to Metal -- by DXMT or by
 * D3DMetal -- the two passes need not agree to the last bit, and the pixels that
 * disagree are discarded before any lighting runs: the specks.
 *
 * Found by a r/macgaming poster ("God Of War 2018 White Dot Fix (through
 * Crossover)") with a d3d11.dll proxy that bisected the game's shaders and
 * states live: the one depth-stencil state with an EQUAL test (depth writes off,
 * stencil on) was it, and GREATER_EQUAL in its place removed every speck. Their
 * proxy is d3d11.dll and needs a DLL override and a copy of DXMT's d3d11.dll.
 *
 * WHAT THIS DOES. The same change, made where the state is created: a depth-
 * stencil state whose depth test is EQUAL is created with GREATER_EQUAL instead
 * (or LESS_EQUAL, or left alone -- gow-depth-fix.ini, `equal_depth`). Nothing else
 * changes. Every EQUAL state seen is logged with its write mask and stencil use,
 * so a second one would show up rather than be changed unnoticed.
 *
 * The risk the poster names: something drawn after the prepass that was not in
 * it, in front of a character, would now draw through. Watch for that.
 *
 * WHERE IT HOOKS. GoW.exe imports d3d11.dll directly, so its import table is
 * already bound when this loads; D3D11CreateDevice and D3D11CreateDeviceAndSwap-
 * Chain are redirected there, and the device that comes back -- DXMT's or
 * D3DMetal's -- has CreateDepthStencilState hooked on its class.
 *
 * Carried on libSceJobManager.dll, which the game imports and no runner ships;
 * every export is forwarded to the renamed original (runtime/build-proxy.sh).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11_4.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOG_NAME "gow-depth-fix.log"
#define INI_NAME "gow-depth-fix.ini"
#define DEV_IDX(m) (offsetof(ID3D11DeviceVtbl, m) / sizeof(void *))

static HMODULE g_self;
static FILE *g_log;
static SRWLOCK g_lock = SRWLOCK_INIT;
static D3D11_COMPARISON_FUNC g_equal_to = D3D11_COMPARISON_GREATER_EQUAL;   /* 0: leave EQUAL alone */
static volatile LONG g_equal_seen, g_changed;
static struct { void **vtbl; unsigned int index; void *orig; } g_hooks[16];
static unsigned int g_hook_count;

static void log_msg(const char *fmt, ...)
{
    va_list args;

    if (!g_log)
        return;
    AcquireSRWLockExclusive(&g_lock);
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
    ReleaseSRWLockExclusive(&g_lock);
}

static void path_next_to_self(char *out, size_t len, const char *file)
{
    char *slash;

    GetModuleFileNameA(g_self, out, (DWORD)len);
    slash = strrchr(out, '\\');
    if (slash)
        slash[1] = 0;
    else
        out[0] = 0;
    strncat(out, file, len - strlen(out) - 1);
}

/* gow-depth-fix.ini: "equal_depth = greater_equal | less_equal | off". */
static const char *read_settings(void)
{
    char path[MAX_PATH], line[256];
    FILE *f;

    path_next_to_self(path, sizeof(path), INI_NAME);
    if (!(f = fopen(path, "r")))
        return "greater_equal (no " INI_NAME ")";
    while (fgets(line, sizeof(line), f))
    {
        char *value = strchr(line, '=');

        if (line[0] == '#' || !value || strncmp(line, "equal_depth", 11))
            continue;
        value++;
        while (*value == ' ' || *value == '\t')
            value++;
        value[strcspn(value, " \t\r\n#;")] = 0;
        if (!strcmp(value, "off"))
            g_equal_to = 0;
        else if (!strcmp(value, "less_equal"))
            g_equal_to = D3D11_COMPARISON_LESS_EQUAL;
        else
            g_equal_to = D3D11_COMPARISON_GREATER_EQUAL;
    }
    fclose(f);
    return g_equal_to == 0 ? "off" : g_equal_to == D3D11_COMPARISON_LESS_EQUAL ? "less_equal" : "greater_equal";
}

/* ---------------------------------------------------------------- vtable hooks */

static void *orig_fn(void *obj, unsigned int index)
{
    void **vtbl = *(void ***)obj, *fn = NULL;
    unsigned int i;

    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_hook_count; i++)
        if (g_hooks[i].vtbl == vtbl && g_hooks[i].index == index)
            fn = g_hooks[i].orig;
    ReleaseSRWLockShared(&g_lock);
    return fn;
}

static void hook_slot(void *obj, unsigned int index, void *hook)
{
    void **vtbl = *(void ***)obj;
    DWORD old;
    unsigned int i;

    AcquireSRWLockExclusive(&g_lock);
    for (i = 0; i < g_hook_count; i++)
        if (g_hooks[i].vtbl == vtbl && g_hooks[i].index == index)
            goto done;
    if (g_hook_count == ARRAYSIZE(g_hooks) || !vtbl[index] || vtbl[index] == hook
            || !VirtualProtect(&vtbl[index], sizeof(void *), PAGE_READWRITE, &old))
        goto done;
    g_hooks[g_hook_count].vtbl = vtbl;
    g_hooks[g_hook_count].index = index;
    g_hooks[g_hook_count].orig = vtbl[index];
    g_hook_count++;
    vtbl[index] = hook;
    VirtualProtect(&vtbl[index], sizeof(void *), old, &old);
done:
    ReleaseSRWLockExclusive(&g_lock);
}

static HRESULT STDMETHODCALLTYPE hk_CreateDepthStencilState(ID3D11Device *device,
        const D3D11_DEPTH_STENCIL_DESC *desc, ID3D11DepthStencilState **state)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D11Device *, const D3D11_DEPTH_STENCIL_DESC *, ID3D11DepthStencilState **)
            = orig_fn(device, DEV_IDX(CreateDepthStencilState));
    D3D11_DEPTH_STENCIL_DESC changed;

    if (!desc || !desc->DepthEnable || desc->DepthFunc != D3D11_COMPARISON_EQUAL)
        return orig(device, desc, state);
    if (InterlockedIncrement(&g_equal_seen) <= 16)
        log_msg("EQUAL depth state: depth writes %s, stencil %s%s",
                desc->DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? "on" : "off",
                desc->StencilEnable ? "on" : "off", g_equal_to ? ", created with the replacement test" : "");
    if (!g_equal_to)
        return orig(device, desc, state);
    changed = *desc;
    changed.DepthFunc = g_equal_to;
    InterlockedIncrement(&g_changed);
    return orig(device, &changed, state);
}

static void hook_device(IUnknown *device)
{
    static const IID *ifaces[] = { &IID_ID3D11Device, &IID_ID3D11Device1, &IID_ID3D11Device2,
                                   &IID_ID3D11Device3, &IID_ID3D11Device4, &IID_ID3D11Device5 };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(ifaces); i++)
    {
        IUnknown *iface = NULL;

        if (FAILED(device->lpVtbl->QueryInterface(device, ifaces[i], (void **)&iface)) || !iface)
            continue;
        hook_slot(iface, DEV_IDX(CreateDepthStencilState), (void *)hk_CreateDepthStencilState);
        iface->lpVtbl->Release(iface);
    }
}

/* ------------------------------------------------------- reaching the device */

typedef HRESULT (WINAPI *create_device_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *create_device_swap_fn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
        ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
static create_device_fn g_real_create;
static create_device_swap_fn g_real_create_swap;
static INIT_ONCE g_said = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK say_device(INIT_ONCE *once, void *device, void **context)
{
    log_msg("D3D11 device created; CreateDepthStencilState hooked");
    return TRUE;
}

static HRESULT WINAPI mgvf_D3D11CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE sw, UINT flags,
        const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk, ID3D11Device **device, D3D_FEATURE_LEVEL *level,
        ID3D11DeviceContext **context)
{
    HRESULT hr = g_real_create(adapter, type, sw, flags, levels, count, sdk, device, level, context);

    if (SUCCEEDED(hr) && device && *device)
    {
        hook_device((IUnknown *)*device);
        InitOnceExecuteOnce(&g_said, say_device, NULL, NULL);
    }
    return hr;
}

static HRESULT WINAPI mgvf_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE sw,
        UINT flags, const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk, const DXGI_SWAP_CHAIN_DESC *swap_desc,
        IDXGISwapChain **swap, ID3D11Device **device, D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context)
{
    HRESULT hr = g_real_create_swap(adapter, type, sw, flags, levels, count, sdk, swap_desc, swap, device, level,
            context);

    if (SUCCEEDED(hr) && device && *device)
    {
        hook_device((IUnknown *)*device);
        InitOnceExecuteOnce(&g_said, say_device, NULL, NULL);
    }
    return hr;
}

/* Points the executable's own imports of the two creators at ours. */
static int redirect_imports(HMODULE base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;
    int n = 0;

    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS *)((char *)base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva)
        return 0;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + rva); imp->Name; ++imp)
    {
        IMAGE_THUNK_DATA *orig, *iat;

        if (lstrcmpiA((const char *)base + imp->Name, "d3d11.dll"))
            continue;
        orig = (IMAGE_THUNK_DATA *)((char *)base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)((char *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; ++orig, ++iat)
        {
            IMAGE_IMPORT_BY_NAME *name;
            void *ours;
            DWORD old;

            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                continue;
            name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig->u1.AddressOfData);
            if (!lstrcmpA((const char *)name->Name, "D3D11CreateDevice"))
            {
                ours = (void *)mgvf_D3D11CreateDevice;
                if (!g_real_create)
                    g_real_create = (create_device_fn)(ULONG_PTR)iat->u1.Function;
            }
            else if (!lstrcmpA((const char *)name->Name, "D3D11CreateDeviceAndSwapChain"))
            {
                ours = (void *)mgvf_D3D11CreateDeviceAndSwapChain;
                if (!g_real_create_swap)
                    g_real_create_swap = (create_device_swap_fn)(ULONG_PTR)iat->u1.Function;
            }
            else
                continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old))
                continue;
            iat->u1.Function = (ULONG_PTR)ours;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            n++;
        }
    }
    return n;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char path[MAX_PATH];
        const char *mode;
        int n;

        g_self = instance;
        DisableThreadLibraryCalls(instance);
        path_next_to_self(path, sizeof(path), LOG_NAME);
        g_log = fopen(path, "w");
        mode = read_settings();
        log_msg("God of War depth fix (MGVF, on libSceJobManager.dll): EQUAL depth tests become %s", mode);
        n = redirect_imports(GetModuleHandleA(NULL));
        log_msg("%d D3D11 creator import%s redirected", n, n == 1 ? "" : "s");
    }
    else if (reason == DLL_PROCESS_DETACH && g_log)
    {
        fprintf(g_log, "Exit: %ld EQUAL depth states seen, %ld created with the replacement test\n",
                (long)g_equal_seen, (long)g_changed);
        fclose(g_log);
        g_log = NULL;
    }
    return TRUE;
}
