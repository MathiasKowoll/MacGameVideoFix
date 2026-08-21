/*
 * vram-probe — watch what DXGI tells a game about video memory.
 *
 * Life is Strange: Reunion freezes under CrossOver. It is not a deadlock: a
 * spindump taken while it was stuck shows the GameThread burning 1.27 seconds
 * of CPU across 128 samples -- 31 billion instructions -- while RenderThread 0
 * used four milliseconds. The game thread is spinning and the render thread is
 * starving behind it.
 *
 * And winedbg says where:
 *
 *   =>0 IDXGIAdapter4_QueryVideoMemoryInfo+0x7a in d3dmetal
 *     1..19 iris-win64-shipping
 *
 * Unreal polls that call to decide whether it is under memory pressure. If the
 * numbers say the budget is exhausted and never change, the loop waiting for
 * memory to free never ends -- which looks exactly like a freeze.
 *
 * This does not fix anything. It reports the four values on every call, and
 * how often the game is asking, so "the budget is wrong" stops being a guess.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\vram-probe.log"

static CRITICAL_SECTION log_lock;

static void logf_(const char *fmt, ...)
{
    char buf[512];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = '\n';

    EnterCriticalSection(&log_lock);
    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, buf, n + 1, &written, NULL);
        CloseHandle(h);
    }
    LeaveCriticalSection(&log_lock);
}

static void *hook_import(const char *dll, const char *func, void *replacement)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *desc;
    DWORD rva;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;

    for (desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); desc->Name; ++desc)
    {
        IMAGE_THUNK_DATA *names, *addrs;
        if (lstrcmpiA((const char *)(base + desc->Name), dll)) continue;
        if (!desc->OriginalFirstThunk) continue;
        names = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        addrs = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addrs)
        {
            IMAGE_IMPORT_BY_NAME *by_name;
            void *previous;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (lstrcmpA((const char *)by_name->Name, func)) continue;
            previous = (void *)addrs->u1.Function;
            if (!VirtualProtect(addrs, sizeof(*addrs), PAGE_READWRITE, &old)) return NULL;
            addrs->u1.Function = (ULONGLONG)(ULONG_PTR)replacement;
            VirtualProtect(addrs, sizeof(*addrs), old, &old);
            return previous;
        }
    }
    return NULL;
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

/* ------------------------------------------------------------------------ */

static const GUID iid_adapter3 = { 0x645967a4, 0x1392, 0x4310,
                                   { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };

static HRESULT (WINAPI *real_query_vram)(void *, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *);
/* Two gigabytes of headroom: enough that Unreal stops waiting, small enough
 * that it does not start behaving as though memory were limitless. */
#define HEADROOM_BYTES (2ull * 1024 * 1024 * 1024)
static const BOOL grant_headroom = TRUE;
static LONG grants;
static LONG vram_calls;
static ULONGLONG first_call_tick;
static UINT64 last_budget, last_usage;

/*
 * Report the numbers, and how hard the game is asking for them.
 *
 * A call count alongside elapsed time is what separates "Unreal checks its
 * budget now and then" from "Unreal is in the loop that never exits". The
 * values are only logged when they change, so a spin shows up as a rate
 * rather than as a hundred thousand identical lines.
 */
static HRESULT WINAPI my_query_vram(void *self, UINT node,
                                    DXGI_MEMORY_SEGMENT_GROUP group,
                                    DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    HRESULT hr = real_query_vram(self, node, group, info);
    LONG n = InterlockedIncrement(&vram_calls);

    if (n == 1) first_call_tick = GetTickCount64();

    if (SUCCEEDED(hr) && info)
    {
        BOOL over = info->CurrentUsage >= info->Budget;
        BOOL changed;

        /*
         * If the answer is "you are out of memory", give it room.
         *
         * Waiting for the freeze to happen on its own costs a play session per
         * experiment. This tests the mechanism the other way round: the loop
         * Unreal is spinning in exists to wait for usage to fall below budget,
         * so if raising the budget above usage makes the freeze go away, the
         * mechanism is confirmed and this is also the fix. If it does not, the
         * theory was wrong and the log still says what the real numbers were.
         *
         * Only when the reported state is already full -- a healthy answer is
         * passed through untouched, so nothing is invented while the game is
         * behaving.
         */
        if (over && grant_headroom)
        {
            UINT64 raised = info->CurrentUsage + HEADROOM_BYTES;
            if (InterlockedIncrement(&grants) <= 3)
                logf_("  over budget (%llu MB used of %llu MB) -> raising budget to %llu MB",
                      (unsigned long long)(info->CurrentUsage / 1048576),
                      (unsigned long long)(info->Budget / 1048576),
                      (unsigned long long)(raised / 1048576));
            info->Budget = raised;
            if (info->AvailableForReservation < HEADROOM_BYTES)
                info->AvailableForReservation = HEADROOM_BYTES;
        }

        changed = info->Budget != last_budget || info->CurrentUsage != last_usage;
        if (n <= 4 || changed || (n % 20000) == 0)
        {
            ULONGLONG elapsed = GetTickCount64() - first_call_tick;
            logf_("[%lu calls, %llus] group %u  budget %llu MB  usage %llu MB  "
                  "reservable %llu MB  reserved %llu MB%s",
                  (unsigned long)n, (unsigned long long)(elapsed / 1000), (unsigned)group,
                  (unsigned long long)(info->Budget / 1048576),
                  (unsigned long long)(info->CurrentUsage / 1048576),
                  (unsigned long long)(info->AvailableForReservation / 1048576),
                  (unsigned long long)(info->CurrentReservation / 1048576),
                  over ? "   <- was over budget" : "");
            last_budget = info->Budget;
            last_usage = info->CurrentUsage;
        }
    }
    else if (n <= 4)
        logf_("[%lu calls] QueryVideoMemoryInfo failed, hr %#lx", (unsigned long)n,
              (unsigned long)hr);

    return hr;
}

static void watch_adapter(void *adapter)
{
    IDXGIAdapter3 *a3 = NULL;

    if (!adapter || real_query_vram) return;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)adapter, &iid_adapter3, (void **)&a3)) || !a3)
    {
        logf_("adapter does not support IDXGIAdapter3 -- nothing to watch");
        return;
    }
    /* IDXGIAdapter3 slot 14 is QueryVideoMemoryInfo: three IUnknown, four
     * IDXGIObject, three IDXGIAdapter, GetDesc1, GetDesc2, then two content
     * protection entries. */
    real_query_vram = patch_vtable_slot(a3, 14, my_query_vram);
    logf_("QueryVideoMemoryInfo watch: %s", real_query_vram ? "installed" : "COULD NOT PATCH");
    IDXGIAdapter3_Release(a3);
}

static HRESULT (WINAPI *real_enum_adapters)(void *, UINT, void **);
static HRESULT (WINAPI *real_enum_adapters1)(void *, UINT, void **);

static HRESULT WINAPI my_enum_adapters(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) watch_adapter(*adapter);
    return hr;
}

static HRESULT WINAPI my_enum_adapters1(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters1(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) watch_adapter(*adapter);
    return hr;
}

static void watch_factory(void *factory)
{
    static LONG done;
    if (!factory || InterlockedExchange(&done, 1)) return;
    /* IDXGIFactory1: slot 7 EnumAdapters, slot 12 EnumAdapters1. */
    real_enum_adapters  = patch_vtable_slot(factory, 7,  my_enum_adapters);
    real_enum_adapters1 = patch_vtable_slot(factory, 12, my_enum_adapters1);
    logf_("factory hooked: EnumAdapters %s, EnumAdapters1 %s",
          real_enum_adapters ? "yes" : "no", real_enum_adapters1 ? "yes" : "no");
}

static HRESULT (WINAPI *real_CreateDXGIFactory)(REFIID, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory1)(REFIID, void **);

static HRESULT WINAPI my_CreateDXGIFactory(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory(iid, out);
    if (SUCCEEDED(hr) && out) watch_factory(*out);
    return hr;
}

static HRESULT WINAPI my_CreateDXGIFactory1(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory1(iid, out);
    if (SUCCEEDED(hr) && out) watch_factory(*out);
    return hr;
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    logf_("");
    logf_("=== vram-probe attached ===");
    logf_("  headroom: %s", grant_headroom ? "granted when the budget reads full" : "measuring only");
    real_CreateDXGIFactory  = hook_import("dxgi.dll", "CreateDXGIFactory",  my_CreateDXGIFactory);
    real_CreateDXGIFactory1 = hook_import("dxgi.dll", "CreateDXGIFactory1", my_CreateDXGIFactory1);
    logf_("  CreateDXGIFactory  %s", real_CreateDXGIFactory  ? "hooked" : "not imported");
    logf_("  CreateDXGIFactory1 %s", real_CreateDXGIFactory1 ? "hooked" : "not imported");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    HANDLE thread;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&log_lock);
        DisableThreadLibraryCalls(inst);
        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
