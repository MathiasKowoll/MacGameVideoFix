/*
 * ue5-runtime-fix — two unrelated faults that stop Unreal titles working
 * under CrossOver on Apple Silicon, carried in one DLL.
 *
 * They affect different games, and each half does nothing when its fault is
 * absent: the VPx patch writes nothing if no matching code is found, and the
 * node guard never fires if the game does not walk adapter nodes. So carrying
 * both costs nothing and saves having to choose.
 *
 *   1. VP9 cutscenes crash on D3D12 — part one, below.
 *   2. The game runs and then freezes after a while, anywhere — part two,
 *      further down.
 *
 * ---------------------------------------------------------------------------
 *
 * Part one: make Electra's VPx decoder take its CPU output
 * path, so VP9 cutscenes play under CrossOver instead of crashing.
 *
 * The problem
 * -----------
 * Apple's D3DMetal does not implement ID3DDestructionNotifier. Electra's D3D12
 * output buffer pool asks every resource for it and uses the answer without
 * checking the HRESULT, so the first VP9 frame dereferences a null vtable:
 *
 *     Res = Resource->QueryInterface(__uuidof(ID3DDestructionNotifier), &N);
 *     check(SUCCEEDED(Res));            // compiled out in Shipping builds
 *     N->RegisterDestructionCallback(); // null deref
 *
 * H.264 and H.265 can avoid that pool with Electra.Win.H264UseOldOutputPath.
 * VPx has no such CVar -- the shipped binaries contain only the H264 and H265
 * ones -- so VP9 on D3D12 has no way out through configuration.
 *
 * The fix
 * -------
 * Electra decides whether to use the pool by comparing the D3D version against
 * 12000, in code the compiler emits as:
 *
 *     cmp dword [rbp+disp], 12000
 *     jl  <cpu path>
 *
 * Raising that threshold to INT_MAX makes the comparison always take the CPU
 * branch -- the same path every D3D11 machine already uses, and the same one
 * H.264 reaches through its CVar. Nothing else changes: the decoder still
 * decodes VP9 with its own libvpx, and Unreal presents the frames normally.
 *
 * We patch the immediate rather than the jump so the edit is a plain four-byte
 * write with no relative displacement to recompute.
 *
 * This is engine code, not game code, so it is not specific to any one title.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>
#include <stdarg.h>
#include <stdio.h>

#define ELECTRA_D3D12_VERSION   0x2EE0        /* 12000 */
#define UNREACHABLE_VERSION     0x7FFFFFFF

/* The running executable's file name, cached. Used for the log prefix and,
 * more importantly, to decide which halves of this DLL apply here. */
static const char *process_name(void)
{
    static char who[64];
    if (!who[0])
    {
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
        const char *base = path;
        while (len-- > 0)
            if (path[len] == '\\') { base = path + len + 1; break; }
        lstrcpynA(who, base, sizeof(who));
        if (!who[0]) lstrcpynA(who, "?", sizeof(who));
    }
    return who;
}

static void logf_(const char *fmt, ...)
{
    char buf[512];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    n = snprintf(buf, sizeof(buf) - 2, "[%s] ", process_name());
    if (n < 0) n = 0;
    va_start(ap, fmt);
    {
        int m = vsnprintf(buf + n, sizeof(buf) - 2 - n, fmt, ap);
        if (m < 0) { va_end(ap); return; }
        n += m;
    }
    va_end(ap);
    buf[n] = '\n';

    /* A game under CrossOver cannot be attached to, so leave a trail.
     *
     * Every line carries the process name. One bottle usually holds several
     * games, and a log that does not say who wrote each entry cannot be read
     * at all once two of them have run -- which is exactly the state it was
     * found in the first time two titles shared a bottle. */
    h = CreateFileA("C:\\ue5-runtime-fix.log", FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n + 1, &written, NULL);
    CloseHandle(h);
}

/* Is `at` a jl? Both encodings appear: rel8 near the flag assignment, rel32
 * where the skipped block is large. We only need to recognise them -- the jump
 * itself is left alone. */
static BOOL is_jl(const BYTE *at)
{
    return at[0] == 0x7C || (at[0] == 0x0F && at[1] == 0x8C);
}

/* Raise a `cmp dword [rbp+disp], 12000` to INT_MAX. `imm` points at the
 * four-byte immediate. */
static BOOL raise_threshold(BYTE *imm)
{
    DWORD old;

    if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    *(DWORD *)imm = UNREACHABLE_VERSION;
    VirtualProtect(imm, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), imm, 4);
    return TRUE;
}

/*
 * Scan the executable's own code for Electra's VPx version checks.
 *
 * We match on the compare being against a *stack slot*: the VPx decoder keeps
 * the version in a local, while the H.264 and H.265 decoders compare a register
 * (3d / 81 f8). That distinction matters -- H.264 already has its CVar and must
 * be left alone.
 *
 * A pattern scan rather than fixed offsets, because a game update moves
 * everything: between two builds of the same title the crash site alone shifted
 * by 0x2C70.
 */
static int apply(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    int found = 0, patched = 0;
    unsigned i;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        BYTE *p, *end;

        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        p = base + sec->VirtualAddress;
        end = p + sec->Misc.VirtualSize - 16;

        for (; p < end; ++p)
        {
            BYTE *imm, *jump;

            if (p[0] != 0x81) continue;

            if (p[1] == 0x7D)          /* cmp dword [rbp+disp8],  imm32 */
            {
                imm = p + 3;
                jump = p + 7;
            }
            else if (p[1] == 0xBD)     /* cmp dword [rbp+disp32], imm32 */
            {
                imm = p + 6;
                jump = p + 10;
            }
            else continue;

            if (*(DWORD *)imm != ELECTRA_D3D12_VERSION) continue;
            if (!is_jl(jump)) continue;      /* not the shape we mean */

            ++found;
            if (raise_threshold(imm))
            {
                ++patched;
                logf_("  raised threshold at +0x%llx",
                      (unsigned long long)(imm - base));
            }
            else
            {
                logf_("  could not write at +0x%llx",
                      (unsigned long long)(imm - base));
            }
            p = jump;
        }
    }

    logf_("VPx version checks: %d found, %d patched", found, patched);
    if (!found)
        logf_("nothing matched -- this build may not be affected, or Unreal's "
              "code generation changed. Nothing was modified.");
    return patched;
}


/* ---------------------------------------------------- the node walk --- */

/*
 * Part two: stop D3DMetal answering for adapter nodes that do not exist.
 *
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
 * it. The game runs, then freezes after a while, wherever it happens to be.
 *
 * Refusing an index the adapter does not have ends it. The refusal fires once
 * per session: the caller takes the node count from that answer and stops
 * asking.
 *
 * Not every Unreal title emits that loop. Of eight checked, two did, both from
 * the same studio and engine build; diagnostics/find-node-walk.py says which.
 * The guard is inert on the rest.
 */
static const GUID iid_adapter3 = { 0x645967a4, 0x1392, 0x4310,
                                   { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };

static HRESULT (WINAPI *real_query_vram)(void *, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *);
static LONG refusals;

static HRESULT WINAPI guarded_query_vram(void *self, UINT node,
                                         DXGI_MEMORY_SEGMENT_GROUP group,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    /* One GPU means one node, so zero is the only valid index, and
     * DXGI_ERROR_INVALID_CALL is what Windows returns past the end. */
    if (node != 0)
    {
        if (InterlockedIncrement(&refusals) == 1)
            logf_("node %u does not exist -- refused, which ends the caller's walk", node);
        return DXGI_ERROR_INVALID_CALL;
    }
    return real_query_vram(self, node, group, info);
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

static void guard_adapter(void *adapter)
{
    IDXGIAdapter3 *a3 = NULL;

    if (!adapter || real_query_vram) return;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)adapter, &iid_adapter3, (void **)&a3)) || !a3)
        return;
    real_query_vram = patch_vtable_slot(a3, 14, guarded_query_vram);
    IDXGIAdapter3_Release(a3);
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

static HRESULT (WINAPI *real_CreateDXGIFactory)(REFIID, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory1)(REFIID, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory2)(UINT, REFIID, void **);

static HRESULT WINAPI my_CreateDXGIFactory(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

static HRESULT WINAPI my_CreateDXGIFactory1(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory1(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

static HRESULT WINAPI my_CreateDXGIFactory2(UINT flags, REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory2(flags, iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

/* Replace one entry in the main module's import address table. */
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

static void install_node_guard(void)
{
    real_CreateDXGIFactory  = hook_import("dxgi.dll", "CreateDXGIFactory",  my_CreateDXGIFactory);
    real_CreateDXGIFactory1 = hook_import("dxgi.dll", "CreateDXGIFactory1", my_CreateDXGIFactory1);
    real_CreateDXGIFactory2 = hook_import("dxgi.dll", "CreateDXGIFactory2", my_CreateDXGIFactory2);
    logf_("node guard: %s",
          (real_CreateDXGIFactory || real_CreateDXGIFactory1 || real_CreateDXGIFactory2)
          ? "armed" : "this game creates no DXGI factory by name");
}

/* Which halves each title actually needs.
 *
 * One DLL serves every game, and that is worth keeping: a single file to build,
 * ship and reason about. What is not worth keeping is every half acting
 * wherever its byte pattern happens to match, because matching is not the same
 * as belonging -- and a change made to help one title then silently changes
 * every other one that matches too.
 *
 * So each half is asked to act by name. Narrowing Electra's patch for one game
 * cannot alter what happens in another, and a title that only ever froze is
 * never patched for a crash it does not have.
 *
 * A title not listed here gets both halves, which is how a new game is tried
 * for the first time. The log says so, so an unexpected result is traceable to
 * this table rather than mistaken for a measurement. */
struct policy
{
    const char *exe;
    BOOL electra;     /* raise Electra's VPx GPU-buffer threshold */
    BOOL node_guard;  /* refuse adapter nodes that do not exist */
};

static const struct policy policies[] =
{
    { "MortalShell2-Win64-Shipping.exe",         TRUE,  FALSE },
    { "BeastOfReincarnation-Win64-Shipping.exe", TRUE,  FALSE },
    { "Iris-Win64-Shipping.exe",                 FALSE, TRUE  },
    { "Chronos-Win64-Shipping.exe",              FALSE, TRUE  },
};

static DWORD WINAPI worker(LPVOID unused)
{
    const char *me = process_name();
    size_t i;
    BOOL known = FALSE;
    struct policy want = { NULL, TRUE, TRUE };

    (void)unused;

    for (i = 0; i < sizeof(policies) / sizeof(policies[0]); i++)
    {
        if (lstrcmpiA(me, policies[i].exe) == 0)
        {
            want = policies[i];
            known = TRUE;
            break;
        }
    }

    if (!known)
        logf_("not a title this build knows -- arming both halves; "
              "report what happens rather than trusting it");

    if (want.electra)
        apply();
    else
        logf_("electra patch: not wanted here");

    if (want.node_guard)
        install_node_guard();
    else
        logf_("node guard: not wanted here");

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    HANDLE thread;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        /* Off the loader lock: DllMain must not do real work. */
        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
