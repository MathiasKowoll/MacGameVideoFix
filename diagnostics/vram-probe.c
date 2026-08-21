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
/*
 * Answer from a cache, and time what it costs when we do not.
 *
 * The first theory was memory pressure, and the log killed it: 75 GB of budget
 * against 751 MB in use, never once full. What it showed instead is the rate --
 * 2600 calls a second early on, rising past 9400 by the ninetieth. That is not
 * a tight loop, which would manage millions; it is a loop whose every
 * iteration is expensive.
 *
 * Which changes what is wrong. Unreal probably always polled this hard, and on
 * Windows the call is cheap. If D3DMetal's costs a hundred microseconds, ten
 * thousand of them is the whole thread -- which is exactly what the spindump
 * showed, one thread saturated and the renderer starving behind it.
 *
 * So: measure the real call, and serve repeats from a cache. Video memory
 * figures do not change meaningfully inside a frame, and a caller polling
 * thousands of times a second is not reacting to any of them.
 */
#define CACHE_WINDOW_MS 100

struct vram_cache
{
    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    ULONGLONG taken;
    BOOL valid;
};
static struct vram_cache cache[2];          /* one per memory segment group */
static const BOOL serve_from_cache = TRUE;
static const BOOL fix_reservations = TRUE;
static UINT64 requested_reservation[2];
static LONG served, measured;

/*
 * Who is asking.
 *
 * Three theories about the values are dead: the budget is never full, the call
 * is not expensive, and the game never reserves anything. The loop is real --
 * two hundred million iterations a second -- so the next useful question is
 * not what it reads but where it lives.
 *
 * A return address names that outright. Collected into a small table with
 * counts rather than logged, because at this rate logging each one would
 * write gigabytes; the table is dumped on a timer instead.
 */
#define MAX_CALLERS 24
static struct { ULONG_PTR rva; LONG64 count; } callers[MAX_CALLERS];
static LONG caller_count;
static CRITICAL_SECTION caller_lock;
static ULONGLONG last_dump;

static void note_caller(void *ret)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    ULONG_PTR rva;
    int i;

    if (!ret || (BYTE *)ret < base) return;
    rva = (ULONG_PTR)((BYTE *)ret - base);

    EnterCriticalSection(&caller_lock);
    for (i = 0; i < caller_count; ++i)
        if (callers[i].rva == rva) { callers[i].count++; goto done; }
    if (caller_count < MAX_CALLERS)
    {
        callers[caller_count].rva = rva;
        callers[caller_count].count = 1;
        ++caller_count;
    }
done:
    LeaveCriticalSection(&caller_lock);
}

static void dump_callers(void)
{
    ULONGLONG now = GetTickCount64();
    int i;

    if (now - last_dump < 5000) return;
    EnterCriticalSection(&caller_lock);
    if (now - last_dump >= 5000)
    {
        last_dump = now;
        logf_("  call sites so far (%d distinct):", caller_count);
        for (i = 0; i < caller_count; ++i)
            logf_("    +0x%llx  %lld calls",
                  (unsigned long long)callers[i].rva,
                  (long long)callers[i].count);
    }
    LeaveCriticalSection(&caller_lock);
}
static ULONGLONG total_call_us;

static HRESULT WINAPI my_query_vram(void *self, UINT node,
                                    DXGI_MEMORY_SEGMENT_GROUP group,
                                    DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    LONG n = InterlockedIncrement(&vram_calls);
    HRESULT hr;

    note_caller(__builtin_return_address(0));
    if ((n & 0xFFFFF) == 0) dump_callers();
    ULONGLONG now = GetTickCount64();
    unsigned slot = (group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL) ? 0 : 1;

    if (n == 1) first_call_tick = now;

    if (serve_from_cache && info && cache[slot].valid
        && now - cache[slot].taken < CACHE_WINDOW_MS)
    {
        *info = cache[slot].info;
        InterlockedIncrement(&served);
        return S_OK;
    }

    {
        LARGE_INTEGER a, b, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&a);
        hr = real_query_vram(self, node, group, info);
        QueryPerformanceCounter(&b);
        if (freq.QuadPart)
        {
            LONG m = InterlockedIncrement(&measured);
            total_call_us += (b.QuadPart - a.QuadPart) * 1000000 / freq.QuadPart;
            if (m <= 3 || (m % 500) == 0)
                logf_("  real call #%ld averaged %lluus", m,
                      (unsigned long long)(total_call_us / m));
        }
    }

    if (SUCCEEDED(hr) && info && slot < 2)
    {
        cache[slot].info = *info;
        cache[slot].taken = now;
        cache[slot].valid = TRUE;
    }

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
        /*
         * Make the reservation fields mean what they mean on Windows.
         *
         * D3DMetal fills all four with the same number except CurrentUsage:
         * budget 76677 MB, reservable 76677 MB, reserved 76677 MB. On Windows
         * CurrentReservation is what the application asked for through
         * SetVideoMemoryReservation and is zero until it does, and
         * AvailableForReservation is a fraction of the budget rather than all
         * of it.
         *
         * That matters because the caller is in a genuine tight loop -- two
         * hundred million calls a second once answers are cheap -- so it is
         * not waiting on cost, it is waiting on a value. A reservation that
         * reads as the whole budget, and never moves however much is asked
         * for, is a value nothing can satisfy.
         */
        if (fix_reservations)
        {
            info->CurrentReservation = requested_reservation[slot];
            if (info->AvailableForReservation > info->Budget / 2)
                info->AvailableForReservation = info->Budget / 2;
        }

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

/*
 * Remember what was reserved, so CurrentReservation can report it back. Slot
 * 15 of IDXGIAdapter3 is SetVideoMemoryReservation, immediately after
 * QueryVideoMemoryInfo.
 */
static HRESULT (WINAPI *real_set_reservation)(void *, UINT, DXGI_MEMORY_SEGMENT_GROUP, UINT64);

static HRESULT WINAPI my_set_reservation(void *self, UINT node,
                                         DXGI_MEMORY_SEGMENT_GROUP group, UINT64 bytes)
{
    HRESULT hr = real_set_reservation(self, node, group, bytes);
    unsigned slot = (group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL) ? 0 : 1;
    static LONG seen;

    if (slot < 2) requested_reservation[slot] = bytes;
    if (InterlockedIncrement(&seen) <= 6)
        logf_("SetVideoMemoryReservation(group %u, %llu MB) -> %#lx",
              (unsigned)group, (unsigned long long)(bytes / 1048576), (unsigned long)hr);
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
    real_set_reservation = patch_vtable_slot(a3, 15, my_set_reservation);
    logf_("QueryVideoMemoryInfo watch: %s, SetVideoMemoryReservation: %s",
          real_query_vram ? "installed" : "COULD NOT PATCH",
          real_set_reservation ? "installed" : "COULD NOT PATCH");
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
    logf_("  cache:    %s", serve_from_cache ? "repeats served within 100ms" : "every call passed through");
    real_CreateDXGIFactory  = hook_import("dxgi.dll", "CreateDXGIFactory",  my_CreateDXGIFactory);
    real_CreateDXGIFactory1 = hook_import("dxgi.dll", "CreateDXGIFactory1", my_CreateDXGIFactory1);
    logf_("  CreateDXGIFactory  %s", real_CreateDXGIFactory  ? "hooked" : "not imported");
    logf_("  CreateDXGIFactory1 %s", real_CreateDXGIFactory1 ? "hooked" : "not imported");
    return 0;
}

static void report_totals(void)
{
    LONG total = vram_calls;
    logf_("");
    logf_("=== totals: %ld calls, %ld served from cache (%ld%%), %ld real, "
          "%lluus average ===",
          total, served, total ? (served * 100 / total) : 0, measured,
          measured ? (unsigned long long)(total_call_us / measured) : 0ull);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    HANDLE thread;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH && !reserved) report_totals();
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&log_lock);
        InitializeCriticalSection(&caller_lock);
        DisableThreadLibraryCalls(inst);
        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
