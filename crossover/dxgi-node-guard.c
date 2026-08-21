/*
 * dxgi-node-guard — stop D3DMetal answering for adapter nodes that do not exist.
 *
 * The bug
 * -------
 * Unreal's D3D12 renderer walks the adapter's memory nodes, accumulating
 * across them, and ends the walk when the call fails:
 *
 *     callq *0x70(%rax)     ; IDXGIAdapter3::QueryVideoMemoryInfo, slot 14
 *     testl %eax, %eax
 *     jns   <backwards>     ; keep going while it succeeds
 *
 * On Windows that call returns an error once the index passes the number of
 * nodes, and that is what stops the loop. D3DMetal answers S_OK for every
 * index, so the counter climbs forever -- two hundred million iterations a
 * second, measured -- one thread pinned and everything else starving behind
 * it. The game runs fine and then freezes after a while, wherever it happens
 * to be.
 *
 * The fix
 * -------
 * Refuse an index the adapter does not have, which is what Windows does. The
 * refusal fires once: the caller takes the node count from that answer and
 * stops asking.
 *
 * Why this sits in CrossOver rather than in a game folder
 * ------------------------------------------------------
 * It needs to know nothing about the game. Unlike the video fixes in this
 * repository it does not depend on winevideo either, because it has nothing
 * to do with decoding -- it corrects one DXGI call. So it belongs where every
 * process picks it up rather than being installed per title.
 *
 * This replaces Apple's dxgi.dll, forwarding four of its seven exports
 * untouched and wrapping the three factory entry points, which is the only
 * way to reach an adapter before the game does.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>

/* Loaded on first use rather than at attach: DllMain must not load libraries. */
static HMODULE real_dxgi;
static CRITICAL_SECTION lock;

static HMODULE load_real(void)
{
    WCHAR path[MAX_PATH];
    HMODULE self, loaded;
    DWORD n;
    size_t i;

    if (real_dxgi) return real_dxgi;

    EnterCriticalSection(&lock);
    if (!real_dxgi)
    {
        /* By full path, derived from our own: the renamed original sits beside
         * us, and searching for it by name would find us again. */
        self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)load_real, &self);
        n = GetModuleFileNameW(self, path, MAX_PATH);
        if (n > 8 && n < MAX_PATH - 8)
        {
            i = n - 8;                       /* strlen(L"dxgi.dll") */
            if (!lstrcmpiW(path + i, L"dxgi.dll"))
            {
                lstrcpyW(path + i, L"dxgi_real.dll");
                loaded = LoadLibraryW(path);
                if (loaded) real_dxgi = loaded;
            }
        }
    }
    LeaveCriticalSection(&lock);
    return real_dxgi;
}

static void *real_proc(const char *name)
{
    HMODULE m = load_real();
    return m ? (void *)GetProcAddress(m, name) : NULL;
}

static void *patch_vtable_slot(void *object, unsigned slot, void *replacement)
{
    void **vtbl = *(void ***)object;
    void *previous;
    DWORD old;

    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old)) return NULL;
    previous = vtbl[slot];
    vtbl[slot] = replacement;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
    return previous;
}

/* ---------------------------------------------------------------- guard --- */

static const GUID iid_adapter3 = { 0x645967a4, 0x1392, 0x4310,
                                   { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };

static HRESULT (WINAPI *real_query_vram)(void *, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *);

static HRESULT WINAPI guarded_query_vram(void *self, UINT node,
                                         DXGI_MEMORY_SEGMENT_GROUP group,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    /* A single-GPU adapter has one node, so zero is the only valid index.
     * DXGI_ERROR_INVALID_CALL is what Windows returns past the end. */
    if (node != 0) return DXGI_ERROR_INVALID_CALL;
    return real_query_vram(self, node, group, info);
}

static void guard_adapter(void *adapter)
{
    IDXGIAdapter3 *a3 = NULL;

    if (!adapter || real_query_vram) return;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)adapter, &iid_adapter3, (void **)&a3)))
        return;                               /* nothing to guard on this adapter */
    if (a3)
    {
        /* IDXGIAdapter3 slot 14: three IUnknown, four IDXGIObject, three
         * IDXGIAdapter, GetDesc1, GetDesc2, two content protection entries. */
        real_query_vram = patch_vtable_slot(a3, 14, guarded_query_vram);
        IDXGIAdapter3_Release(a3);
    }
}

static HRESULT (WINAPI *real_enum_adapters)(void *, UINT, void **);
static HRESULT (WINAPI *real_enum_adapters1)(void *, UINT, void **);

static HRESULT WINAPI guarded_enum_adapters(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) guard_adapter(*adapter);
    return hr;
}

static HRESULT WINAPI guarded_enum_adapters1(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters1(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) guard_adapter(*adapter);
    return hr;
}

static void guard_factory(void *factory)
{
    static LONG done;

    if (!factory || InterlockedExchange(&done, 1)) return;
    /* IDXGIFactory1: slot 7 EnumAdapters, slot 12 EnumAdapters1. */
    real_enum_adapters  = patch_vtable_slot(factory, 7,  guarded_enum_adapters);
    real_enum_adapters1 = patch_vtable_slot(factory, 12, guarded_enum_adapters1);
}

/* ------------------------------------------------------------- exports --- */

HRESULT WINAPI CreateDXGIFactory(REFIID iid, void **out)
{
    HRESULT (WINAPI *fn)(REFIID, void **) = real_proc("CreateDXGIFactory");
    HRESULT hr;

    if (!fn) return E_FAIL;
    hr = fn(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void **out)
{
    HRESULT (WINAPI *fn)(REFIID, void **) = real_proc("CreateDXGIFactory1");
    HRESULT hr;

    if (!fn) return E_FAIL;
    hr = fn(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid, void **out)
{
    HRESULT (WINAPI *fn)(UINT, REFIID, void **) = real_proc("CreateDXGIFactory2");
    HRESULT hr;

    if (!fn) return E_FAIL;
    hr = fn(flags, iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&lock);
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
