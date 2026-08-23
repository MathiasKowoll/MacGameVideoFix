/*
 * gfx-observe -- the first thing to put on a game that misbehaves.
 *
 * Not for one title. This answers the question every investigation here has
 * started from: *what does this game ask the graphics stack for, and what is
 * it told no about?*
 *
 * Every fault this project has fixed had that shape. A game asks for an
 * interface that is not implemented and does not check the result. It asks for
 * a texture format that cannot be created and carries on with a null. It is
 * told a call succeeded when on Windows it would have failed, and walks off
 * the end of a loop. From outside, all of those look like "the video is black"
 * or "the characters are in a T-pose" -- and none of them can be told apart by
 * watching the screen.
 *
 * So this logs refusals, and it logs answers. Not calls: a renderer makes
 * millions, and a log of everything is a log of nothing. What is recorded is
 *
 *   - every call that FAILED, bounded per call site and deduplicated by shape;
 *   - every capability query and the answer given, once per distinct query;
 *   - every interface a game asks a device for and does not get;
 *   - every module it loads, and every function it looks up and does not find;
 *   - a ratio at the end, because "forty failures" means nothing without
 *     knowing whether four hundred succeeded.
 *
 * It observes. It changes nothing, has no levers, and reads no environment
 * variables -- a game started from a running Steam never sees a variable set
 * afterwards, and a diagnostic that silently does not apply is worse than none.
 *
 * BUILDING. It rides in on a DLL the game already loads and that has nothing
 * to do with rendering:
 *
 *     SOURCE=gfx-observe.c diagnostics/build-probe.sh <the game's carrier.dll>
 *
 * Good carriers seen so far: amd_ags_x64.dll, libxess.dll, libogg_64.dll,
 * GfeSDK.dll, dinput8.dll. Prefer one imported statically -- it loads before
 * the renderer starts.
 *
 * READING IT. An empty log is a finding: it means the graphics stack refused
 * nothing, and whatever is wrong is not something a D3D hook can see. A game
 * that skins on the CPU with wide SIMD would show a T-pose and leave this file
 * silent.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\gfx-observe.log"

/* ------------------------------------------------------------------ logging */

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

/*
 * One line per distinct thing, not per call.
 *
 * Bounding by call count is the wrong bound for a renderer: the first two
 * dozen calls are startup, and the one that matters happens later. Keyed on
 * what actually distinguishes a case -- a call site plus two numbers -- every
 * distinct case is reported once and the total stays bounded by how many
 * distinct cases exist.
 */
static BOOL first_time(UINT tag, UINT a, UINT b)
{
    static struct { UINT tag, a, b; } seen[256];
    static LONG count;
    LONG i, n = count;

    for (i = 0; i < n && i < 256; ++i)
        if (seen[i].tag == tag && seen[i].a == a && seen[i].b == b) return FALSE;
    if (n >= 256) return FALSE;
    seen[n].tag = tag; seen[n].a = a; seen[n].b = b;
    count = n + 1;
    return TRUE;
}

/* Counters, reported as a ratio on the way out. */
typedef struct { const char *what; LONG ok, bad; } tally;
static tally counts[] = {
    { "D3D11 CreateBuffer" }, { "D3D11 CreateTexture2D" }, { "D3D11 CreateTexture3D" },
    { "D3D11 CreateInputLayout" }, { "D3D11 CreateVertexShader" },
    { "D3D11 CreatePixelShader" }, { "D3D11 CreateComputeShader" },
    { "D3D11 CreateUnorderedAccessView" }, { "D3D11 OpenSharedResource" },
    { "D3D12 CreateCommittedResource" }, { "D3D12 CreateGraphicsPipelineState" },
    { "D3D12 CreateComputePipelineState" }, { "D3D12 CreateRootSignature" },
    { "D3D12 CreateCommandQueue" }, { "D3D12 CreateDescriptorHeap" },
    { "D3D12 CreateHeap" }, { "D3D12 CreatePlacedResource" },
    { "D3D12 OpenSharedHandle" },
};
enum {
    C_BUFFER, C_TEX2D, C_TEX3D, C_LAYOUT, C_VS, C_PS, C_CS, C_UAV, C_SHARED11,
    C_COMMITTED, C_GFXPSO, C_CSPSO, C_ROOTSIG, C_QUEUE, C_HEAPDESC, C_HEAP,
    C_PLACED, C_SHARED12, C_COUNT
};

static HRESULT note(int which, HRESULT hr, const char *detail, UINT a, UINT b)
{
    if (SUCCEEDED(hr)) InterlockedIncrement(&counts[which].ok);
    else
    {
        InterlockedIncrement(&counts[which].bad);
        if (first_time((UINT)(0x1000 + which), a, b))
            logf_("REFUSED  %-34s -> 0x%08lx   %s",
                  counts[which].what, (unsigned long)hr, detail ? detail : "");
    }
    return hr;
}

/* ------------------------------------------------------------------ hooking */

static void *patch(void *obj, int slot, void *replacement)
{
    void **vtbl = *(void ***)obj;
    void *was = vtbl[slot];
    DWORD old;
    if (was == replacement) return NULL;
    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old)) return NULL;
    vtbl[slot] = replacement;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
    return was;
}

/*
 * Rewrite one imported function in this process's own import table, by name or
 * by ordinal.
 *
 * By ordinal matters and is easy to forget: Wo Long imports d3d12 as ordinal
 * 101 with no name anywhere, and a by-name walk finds nothing at all.
 */
static void *hook_import(const char *dll, const char *func, WORD ordinal, void *repl)
{
    HMODULE base = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;

    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)((char *)base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;

    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + rva); imp->Name; ++imp)
    {
        IMAGE_THUNK_DATA *orig, *iat;
        if (lstrcmpiA((const char *)base + imp->Name, dll) != 0) continue;
        orig = (IMAGE_THUNK_DATA *)((char *)base + (imp->OriginalFirstThunk
                                    ? imp->OriginalFirstThunk : imp->FirstThunk));
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
                void *was;
                if (!VirtualProtect(&iat->u1.Function, sizeof(void *),
                                    PAGE_READWRITE, &old)) continue;
                was = (void *)(ULONG_PTR)iat->u1.Function;
                iat->u1.Function = (ULONG_PTR)repl;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                return was;
            }
        }
    }
    return NULL;
}

/* --------------------------------------------------------------- D3D11 side */

/*
 * ID3D11Device: 3 CreateBuffer, 5 CreateTexture2D, 6 CreateTexture3D,
 * 8 CreateUnorderedAccessView, 11 CreateInputLayout, 12 CreateVertexShader,
 * 15 CreatePixelShader, 18 CreateComputeShader, 28 OpenSharedResource,
 * 29 CheckFormatSupport.
 */
static HRESULT (WINAPI *r11_buffer)(void *, const void *, const void *, void **);
static HRESULT (WINAPI *r11_tex2d)(void *, const void *, const void *, void **);
static HRESULT (WINAPI *r11_tex3d)(void *, const void *, const void *, void **);
static HRESULT (WINAPI *r11_uav)(void *, void *, const void *, void **);
static HRESULT (WINAPI *r11_layout)(void *, const void *, UINT, const void *, SIZE_T, void **);
static HRESULT (WINAPI *r11_vs)(void *, const void *, SIZE_T, void *, void **);
static HRESULT (WINAPI *r11_ps)(void *, const void *, SIZE_T, void *, void **);
static HRESULT (WINAPI *r11_cs)(void *, const void *, SIZE_T, void *, void **);
static HRESULT (WINAPI *r11_shared)(void *, HANDLE, REFIID, void **);
static HRESULT (WINAPI *r11_fmt)(void *, UINT, UINT *);
static HRESULT (WINAPI *r11_qi)(void *, REFIID, void **);

static const char *guid_text(REFIID iid)
{
    static char buf[4][40];
    static LONG turn;
    char *out = buf[InterlockedIncrement(&turn) & 3];
    const GUID *g = (const GUID *)iid;
    if (!g) return "(null)";
    _snprintf(out, sizeof(buf[0]),
              "%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
              (unsigned long)g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
              g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return out;
}

static HRESULT WINAPI h11_tex2d(void *s, const void *d, const void *init, void **o)
{
    const UINT *u = (const UINT *)d;
    char detail[96];
    HRESULT hr = r11_tex2d(s, d, init, o);
    if (d) _snprintf(detail, sizeof(detail), "%ux%u format=%u bind=0x%x misc=0x%x",
                     u[0], u[1], u[4], u[8], u[10]);
    else detail[0] = 0;
    return note(C_TEX2D, hr, detail, d ? u[4] : 0, d ? (u[8] | (u[10] << 16)) : 0);
}
static HRESULT WINAPI h11_tex3d(void *s, const void *d, const void *i, void **o)
{ return note(C_TEX3D, r11_tex3d(s, d, i, o), "", d ? ((const UINT *)d)[3] : 0, 0); }
static HRESULT WINAPI h11_buffer(void *s, const void *d, const void *i, void **o)
{
    const UINT *u = (const UINT *)d;
    char detail[80];
    HRESULT hr = r11_buffer(s, d, i, o);
    if (d) _snprintf(detail, sizeof(detail), "%u bytes usage=%u bind=0x%x misc=0x%x",
                     u[0], u[1], u[2], u[4]);
    else detail[0] = 0;
    return note(C_BUFFER, hr, detail, d ? u[2] : 0, d ? u[4] : 0);
}
static HRESULT WINAPI h11_uav(void *s, void *r, const void *d, void **o)
{ return note(C_UAV, r11_uav(s, r, d, o), "", d ? ((const UINT *)d)[0] : 0, 0); }
static HRESULT WINAPI h11_layout(void *s, const void *e, UINT n, const void *b, SIZE_T l, void **o)
{ return note(C_LAYOUT, r11_layout(s, e, n, b, l, o), "", n, (UINT)l); }
static HRESULT WINAPI h11_vs(void *s, const void *b, SIZE_T l, void *k, void **o)
{ return note(C_VS, r11_vs(s, b, l, k, o), "", (UINT)l, 0); }
static HRESULT WINAPI h11_ps(void *s, const void *b, SIZE_T l, void *k, void **o)
{ return note(C_PS, r11_ps(s, b, l, k, o), "", (UINT)l, 0); }
static HRESULT WINAPI h11_cs(void *s, const void *b, SIZE_T l, void *k, void **o)
{ return note(C_CS, r11_cs(s, b, l, k, o), "", (UINT)l, 0); }
static HRESULT WINAPI h11_shared(void *s, HANDLE h, REFIID iid, void **o)
{ return note(C_SHARED11, r11_shared(s, h, iid, o), guid_text(iid), 0, 0); }

/* The answer is the interesting half: a format the game is told it cannot
 * sample is a path it will take without saying so. */
static HRESULT WINAPI h11_fmt(void *s, UINT fmt, UINT *support)
{
    HRESULT hr = r11_fmt(s, fmt, support);
    if (first_time(0x2001, fmt, 0))
        logf_("CheckFormatSupport(%u) -> 0x%08lx  support=0x%08lx",
              fmt, (unsigned long)hr,
              (SUCCEEDED(hr) && support) ? (unsigned long)*support : 0);
    return hr;
}

/* Every interface the game asks the device for, and whether it got one. */
static HRESULT WINAPI h11_qi(void *s, REFIID iid, void **o)
{
    HRESULT hr = r11_qi(s, iid, o);
    const GUID *g = (const GUID *)iid;
    if (g && first_time(0x2002, (UINT)g->Data1, (UINT)(g->Data2 | (g->Data3 << 16))))
        logf_("%s  D3D11 device QueryInterface %s -> 0x%08lx",
              FAILED(hr) ? "REFUSED " : "        ", guid_text(iid), (unsigned long)hr);
    return hr;
}

static void watch_d3d11_device(void *dev)
{
    if (!dev) return;
    r11_qi     = patch(dev,  0, h11_qi);
    r11_buffer = patch(dev,  3, h11_buffer);
    r11_tex2d  = patch(dev,  5, h11_tex2d);
    r11_tex3d  = patch(dev,  6, h11_tex3d);
    r11_uav    = patch(dev,  8, h11_uav);
    r11_layout = patch(dev, 11, h11_layout);
    r11_vs     = patch(dev, 12, h11_vs);
    r11_ps     = patch(dev, 15, h11_ps);
    r11_cs     = patch(dev, 18, h11_cs);
    r11_shared = patch(dev, 28, h11_shared);
    r11_fmt    = patch(dev, 29, h11_fmt);
    logf_("watching the D3D11 device");
}

/* --------------------------------------------------------------- D3D12 side */

/*
 * ID3D12Device: 8 CreateCommandQueue, 10 CreateGraphicsPipelineState,
 * 11 CreateComputePipelineState, 13 CheckFeatureSupport,
 * 14 CreateDescriptorHeap, 16 CreateRootSignature, 27 CreateCommittedResource,
 * 28 CreateHeap, 29 CreatePlacedResource, 32 OpenSharedHandle.
 */
static HRESULT (WINAPI *r12_queue)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *r12_gfxpso)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *r12_cspso)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *r12_feature)(void *, UINT, void *, UINT);
static HRESULT (WINAPI *r12_heapdesc)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *r12_rootsig)(void *, UINT, const void *, SIZE_T, REFIID, void **);
static HRESULT (WINAPI *r12_committed)(void *, const void *, UINT, const void *, UINT,
                                       const void *, REFIID, void **);
static HRESULT (WINAPI *r12_heap)(void *, const void *, REFIID, void **);
static HRESULT (WINAPI *r12_placed)(void *, void *, UINT64, const void *, UINT,
                                    const void *, REFIID, void **);
static HRESULT (WINAPI *r12_shared)(void *, HANDLE, REFIID, void **);
static HRESULT (WINAPI *r12_qi)(void *, REFIID, void **);

/* D3D12_RESOURCE_DESC: Dimension 0, Width 16 (UINT64), Height 24, Format 32,
 * Layout 44, Flags 48. */
static void describe_resource(const void *desc, char *out, size_t n)
{
    const char *d = (const char *)desc;
    if (!desc) { if (n) out[0] = 0; return; }
    _snprintf(out, n, "%ux%u format=%u dim=%u flags=0x%x",
              (UINT)(*(const UINT64 *)(d + 16)), *(const UINT *)(d + 24),
              *(const UINT *)(d + 32), *(const UINT *)(d + 0), *(const UINT *)(d + 48));
}

static HRESULT WINAPI h12_committed(void *s, const void *heap, UINT hf, const void *desc,
                                    UINT state, const void *clear, REFIID iid, void **o)
{
    char detail[96];
    HRESULT hr = r12_committed(s, heap, hf, desc, state, clear, iid, o);
    describe_resource(desc, detail, sizeof(detail));
    return note(C_COMMITTED, hr, detail,
                desc ? *(const UINT *)((const char *)desc + 32) : 0,
                desc ? *(const UINT *)((const char *)desc + 48) : 0);
}
/*
 * Does this game put two resources in the same memory?
 *
 * A placed resource is the caller taking responsibility for its own
 * allocation, and a caller that reuses a range for a second resource is
 * aliasing -- legal, common in engines that manage their own heaps, and
 * correct only if an aliasing barrier separates the two uses. A barrier that
 * is ignored produces data that is right sometimes and stale other times,
 * varying with the allocation pattern rather than with anything on screen.
 *
 * That is a different fault from a mistranslated shader, and the two are told
 * apart by exactly one question: does the game alias at all? So count it.
 * Sixty-four slots per heap is enough to catch the practice without pretending
 * to track the whole allocator.
 */
/*
 * Counting overlaps by hand was wrong twice over: sizes were guessed -- taken
 * only for buffers, left at zero for textures, so everything at offset zero
 * looked like it collided -- and freed heaps were never removed, so an
 * allocator reusing an address looked like aliasing. Both produced confident
 * nonsense.
 *
 * The direct signal costs nothing to read: a game that aliases must issue an
 * aliasing barrier, and barriers go through one call on the command list.
 * Counting those answers the question without knowing a single resource size.
 */
static LONG barriers_alias, barriers_transition, barriers_uav;

static HRESULT WINAPI h12_placed(void *s, void *hp, UINT64 off, const void *desc, UINT st,
                                 const void *cv, REFIID iid, void **o)
{
    char detail[96];
    HRESULT hr = r12_placed(s, hp, off, desc, st, cv, iid, o);
    describe_resource(desc, detail, sizeof(detail));
    if (SUCCEEDED(hr) && desc)
    {
    }
    return note(C_PLACED, hr, detail,
                desc ? *(const UINT *)((const char *)desc + 32) : 0, 0);
}
static HRESULT WINAPI h12_gfxpso(void *s, const void *d, REFIID iid, void **o)
{
    HRESULT hr = r12_gfxpso(s, d, iid, o);
    char detail[64];
    _snprintf(detail, sizeof(detail), "VS %llu bytes",
              (unsigned long long)(d ? *(const SIZE_T *)((const char *)d + 16) : 0));
    return note(C_GFXPSO, hr, detail, 0, 0);
}
static HRESULT WINAPI h12_cspso(void *s, const void *d, REFIID iid, void **o)
{
    HRESULT hr = r12_cspso(s, d, iid, o);
    char detail[64];
    _snprintf(detail, sizeof(detail), "CS %llu bytes",
              (unsigned long long)(d ? *(const SIZE_T *)((const char *)d + 16) : 0));
    return note(C_CSPSO, hr, detail, 0, 0);
}
static HRESULT WINAPI h12_rootsig(void *s, UINT n, const void *b, SIZE_T l, REFIID iid, void **o)
{ return note(C_ROOTSIG, r12_rootsig(s, n, b, l, iid, o), "", (UINT)l, 0); }
static HRESULT WINAPI h12_queue(void *s, const void *d, REFIID iid, void **o)
{ return note(C_QUEUE, r12_queue(s, d, iid, o), "", d ? ((const UINT *)d)[0] : 0, 0); }
static HRESULT WINAPI h12_heapdesc(void *s, const void *d, REFIID iid, void **o)
{ return note(C_HEAPDESC, r12_heapdesc(s, d, iid, o), "", d ? ((const UINT *)d)[0] : 0, 0); }
static HRESULT WINAPI h12_heap(void *s, const void *d, REFIID iid, void **o)
{ return note(C_HEAP, r12_heap(s, d, iid, o), "", 0, 0); }
static HRESULT WINAPI h12_shared(void *s, HANDLE h, REFIID iid, void **o)
{ return note(C_SHARED12, r12_shared(s, h, iid, o), guid_text(iid), 0, 0); }

/*
 * The name of a feature id, for the ones worth reading at a glance.
 *
 * Not a complete table -- the numbers are stable and the header has them all.
 * These are the ones whose answers have changed an investigation here.
 */
static const char *feature_name(UINT f)
{
    switch (f)
    {
    case 0:  return "D3D12_OPTIONS";
    case 1:  return "ARCHITECTURE";
    case 2:  return "FEATURE_LEVELS";
    case 3:  return "FORMAT_SUPPORT";
    case 4:  return "MULTISAMPLE_QUALITY_LEVELS";
    case 6:  return "GPU_VIRTUAL_ADDRESS_SUPPORT";
    case 7:  return "SHADER_MODEL";
    case 8:  return "D3D12_OPTIONS1";
    case 12: return "ROOT_SIGNATURE";
    case 16: return "ARCHITECTURE1";
    case 18: return "D3D12_OPTIONS2";
    case 21: return "D3D12_OPTIONS3";
    case 23: return "D3D12_OPTIONS4";
    case 27: return "D3D12_OPTIONS5";
    case 29: return "D3D12_OPTIONS6";
    case 31: return "D3D12_OPTIONS7";
    case 39: return "D3D12_OPTIONS10";
    case 41: return "D3D12_OPTIONS12";
    case 46: return "D3D12_OPTIONS17";
    default: return "";
    }
}

/*
 * The whole structure, not its first field.
 *
 * "First dword 1" for D3D12_OPTIONS1 says wave operations are supported and
 * stops exactly where it gets interesting: the two fields after it are
 * WaveLaneCountMin and WaveLaneCountMax, and a shader written for one wave
 * width running at another produces results that are wrong rather than
 * missing. That is the difference between a character in its bind pose and a
 * character whose limbs are in the wrong places, and the first dword cannot
 * tell them apart.
 *
 * Sixteen dwords covers every structure in the enum.
 */
static HRESULT WINAPI h12_feature(void *s, UINT feature, void *data, UINT size)
{
    HRESULT hr = r12_feature(s, feature, data, size);

    if (first_time(0x2003, feature, size))
    {
        char dump[220];
        int at = 0;
        UINT i, words = size / 4;
        if (words > 16) words = 16;
        dump[0] = 0;
        if (SUCCEEDED(hr) && data)
            for (i = 0; i < words && at < (int)sizeof(dump) - 12; ++i)
                at += _snprintf(dump + at, sizeof(dump) - at, "%08lx ",
                                ((const unsigned long *)data)[i]);
        logf_("%s  CheckFeatureSupport %2u %-27s %3u bytes -> 0x%08lx  %s",
              FAILED(hr) ? "REFUSED " : "        ",
              feature, feature_name(feature), size, (unsigned long)hr, dump);
    }
    return hr;
}

static HRESULT WINAPI h12_qi(void *s, REFIID iid, void **o)
{
    HRESULT hr = r12_qi(s, iid, o);
    const GUID *g = (const GUID *)iid;
    if (g && first_time(0x2004, (UINT)g->Data1, (UINT)(g->Data2 | (g->Data3 << 16))))
        logf_("%s  D3D12 device QueryInterface %s -> 0x%08lx",
              FAILED(hr) ? "REFUSED " : "        ", guid_text(iid), (unsigned long)hr);
    return hr;
}

static void (WINAPI *r12_barrier)(void *, UINT, const void *);

/* D3D12_RESOURCE_BARRIER: Type at 0, Flags at 4, union from 8.
 * Type 0 TRANSITION, 1 ALIASING, 2 UAV. Each element is 32 bytes wide. */
static void WINAPI h12_barrier(void *self, UINT count, const void *bar)
{
    UINT i;
    const char *b = (const char *)bar;
    for (i = 0; b && i < count; ++i)
    {
        UINT type = *(const UINT *)(b + (size_t)i * 32);
        if (type == 1)
        {
            if (InterlockedIncrement(&barriers_alias) == 1)
                logf_("the game issues ALIASING barriers -- it reuses memory "
                      "between resources, and correctness depends on those "
                      "barriers being honoured");
        }
        else if (type == 2) InterlockedIncrement(&barriers_uav);
        else InterlockedIncrement(&barriers_transition);
    }
    r12_barrier(self, count, bar);
}

static HRESULT (WINAPI *r12_cmdlist)(void *, UINT, UINT, void *, void *, REFIID, void **);

static HRESULT WINAPI h12_cmdlist(void *s, UINT node, UINT type, void *alloc,
                                  void *pso, REFIID iid, void **out)
{
    HRESULT hr = r12_cmdlist(s, node, type, alloc, pso, iid, out);
    if (SUCCEEDED(hr) && out && *out && !r12_barrier)
    {
        r12_barrier = patch(*out, 26, h12_barrier);
        logf_("watching resource barriers: %s", r12_barrier ? "yes" : "NO");
    }
    return hr;
}

static void watch_d3d12_device(void *dev)
{
    if (!dev) return;
    r12_qi        = patch(dev,  0, h12_qi);
    r12_queue     = patch(dev,  8, h12_queue);
    r12_gfxpso    = patch(dev, 10, h12_gfxpso);
    r12_cspso     = patch(dev, 11, h12_cspso);
    r12_feature   = patch(dev, 13, h12_feature);
    r12_heapdesc  = patch(dev, 14, h12_heapdesc);
    r12_rootsig   = patch(dev, 16, h12_rootsig);
    r12_committed = patch(dev, 27, h12_committed);
    r12_heap      = patch(dev, 28, h12_heap);
    r12_placed    = patch(dev, 29, h12_placed);
    r12_shared    = patch(dev, 32, h12_shared);
    r12_cmdlist   = patch(dev, 12, h12_cmdlist);   /* CreateCommandList */
    logf_("watching the D3D12 device");
}

/* --------------------------------------------------- where the devices come from */

static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, REFIID, void **);
static HRESULT (WINAPI *real_D3D11CreateDevice)(void *, UINT, HMODULE, UINT, const UINT *,
                                                UINT, UINT, void **, UINT *, void **);
static BOOL seen11, seen12;

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT level, REFIID iid, void **dev)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, level, iid, dev);
    logf_("%s  D3D12CreateDevice(min level 0x%x) -> 0x%08lx",
          FAILED(hr) ? "REFUSED " : "        ", level, (unsigned long)hr);
    if (SUCCEEDED(hr) && dev && *dev && !seen12) { seen12 = TRUE; watch_d3d12_device(*dev); }
    return hr;
}

static HRESULT WINAPI my_D3D11CreateDevice(void *adapter, UINT type, HMODULE sw, UINT flags,
                                           const UINT *levels, UINT nlevels, UINT sdk,
                                           void **dev, UINT *got, void **ctx)
{
    HRESULT hr = real_D3D11CreateDevice(adapter, type, sw, flags, levels, nlevels, sdk,
                                        dev, got, ctx);
    logf_("%s  D3D11CreateDevice(flags 0x%x) -> 0x%08lx  level 0x%x",
          FAILED(hr) ? "REFUSED " : "        ", flags, (unsigned long)hr, got ? *got : 0);
    if (SUCCEEDED(hr) && dev && *dev && !seen11) { seen11 = TRUE; watch_d3d11_device(*dev); }
    return hr;
}

/*
 * A device does not have to arrive through d3d11.dll or d3d12.dll.
 *
 * NVIDIA Streamline ships sl.interposer.dll, a drop-in exporting both
 * D3D11CreateDevice and D3D12CreateDevice, and a game linked against it has no
 * import of the real thing at all -- Nioh 3 and Marvel's Spider-Man 2 both do
 * this. Hooking only the obvious module finds nothing and looks like a game
 * that never made a device.
 */
static const char *const device_modules[] = {
    "d3d12.dll", "d3d11.dll", "sl.interposer.dll", "nvngx.dll", "amd_fidelityfx_dx12.dll",
};

static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);
static HMODULE (WINAPI *real_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

static FARPROC WINAPI my_GetProcAddress(HMODULE mod, LPCSTR name)
{
    FARPROC p = real_GetProcAddress(mod, name);

    if (!name || (ULONG_PTR)name <= 0xffff) return p;

    /* A lookup that comes back empty is a capability the game just learned it
     * does not have, and it will not say so. */
    if (!p && first_time(0x3001, (UINT)(ULONG_PTR)name, 0))
    {
        char who[MAX_PATH] = "?";
        GetModuleFileNameA(mod, who, sizeof(who) - 1);
        logf_("REFUSED  GetProcAddress(%s) in %s", name, who);
        return p;
    }
    if (!p) return p;

    if (lstrcmpA(name, "D3D12CreateDevice") == 0)
    {
        if (!real_D3D12CreateDevice) real_D3D12CreateDevice = (void *)p;
        return (FARPROC)my_D3D12CreateDevice;
    }
    if (lstrcmpA(name, "D3D11CreateDevice") == 0)
    {
        if (!real_D3D11CreateDevice) real_D3D11CreateDevice = (void *)p;
        return (FARPROC)my_D3D11CreateDevice;
    }
    return p;
}

/* Which middleware actually loads, and which fails to. */
static HMODULE WINAPI my_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    HMODULE h = real_LoadLibraryExW(name, file, flags);
    if (name && !h)
    {
        char narrow[MAX_PATH];
        int i = 0;
        while (i < MAX_PATH - 1 && name[i]) { narrow[i] = (char)(name[i] & 0x7f); ++i; }
        narrow[i] = 0;
        if (first_time(0x3002, (UINT)(ULONG_PTR)name, 0))
            logf_("REFUSED  LoadLibrary(%s) -- not found or not loadable", narrow);
    }
    return h;
}

static DWORD WINAPI worker(void *unused)
{
    size_t i;
    void *was;
    (void)unused;

    logf_("gfx-observe: watching only. Nothing here changes what the game does.");

    for (i = 0; i < sizeof(device_modules) / sizeof(device_modules[0]); ++i)
    {
        was = hook_import(device_modules[i], "D3D12CreateDevice", 101,
                          (void *)my_D3D12CreateDevice);
        if (was && !real_D3D12CreateDevice)
        {
            real_D3D12CreateDevice = was;
            logf_("hooked %s!D3D12CreateDevice", device_modules[i]);
        }
        was = hook_import(device_modules[i], "D3D11CreateDevice", 0,
                          (void *)my_D3D11CreateDevice);
        if (was && !real_D3D11CreateDevice)
        {
            real_D3D11CreateDevice = was;
            logf_("hooked %s!D3D11CreateDevice", device_modules[i]);
        }
    }

    real_GetProcAddress = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                 "GetProcAddress");
    real_LoadLibraryExW = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                 "LoadLibraryExW");
    hook_import("KERNEL32.dll", "GetProcAddress", 0, (void *)my_GetProcAddress);
    hook_import("KERNEL32.dll", "LoadLibraryExW", 0, (void *)my_LoadLibraryExW);

    logf_("D3D11 %s, D3D12 %s -- anything not imported statically is caught "
          "through GetProcAddress",
          real_D3D11CreateDevice ? "hooked at startup" : "not imported",
          real_D3D12CreateDevice ? "hooked at startup" : "not imported");
    return 0;
}

/*
 * The ratio, on the way out.
 *
 * This is the line to read first. A call with failures and no successes is a
 * capability that is simply absent; one with both is a game asking for
 * something specific and being refused only that. The difference decides what
 * to look at next, and neither number means anything alone.
 */
/*
 * Report on a timer, not only on the way out.
 *
 * A game closed from Steam, or killed, or crashed, never runs DLL_PROCESS_DETACH,
 * and the totals are the half of this file that says what did NOT happen --
 * which is exactly what is lost. One line every half minute costs nothing and
 * survives any ending.
 */
static void report(void);

static DWORD WINAPI ticker(void *unused)
{
    (void)unused;
    for (;;)
    {
        Sleep(30000);
        report();
    }
}

static void report(void)
{
    int i;
    BOOL any = FALSE;
    logf_("---- totals ----");
    for (i = 0; i < C_COUNT; ++i)
    {
        if (!counts[i].ok && !counts[i].bad) continue;
        any = TRUE;
        logf_("  %-34s %6ld ok  %6ld refused%s",
              counts[i].what, counts[i].ok, counts[i].bad,
              counts[i].bad && !counts[i].ok ? "   << never once succeeded" : "");
    }
    if (!any)
        logf_("  nothing was created through a watched call. Either the device "
              "never arrived, or it arrives somewhere this does not look.");
    logf_("  resource barriers: %ld transition, %ld uav, %ld ALIASING%s",
          barriers_transition, barriers_uav, barriers_alias,
          barriers_alias ? "   << the game aliases memory"
          : (barriers_transition || barriers_uav)
              ? "   << none, and barriers ARE being counted"
              : "   << none of any kind, so this was not watching");
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
        CloseHandle(CreateThread(NULL, 0, ticker, NULL, 0, NULL));
    }
    else if (reason == DLL_PROCESS_DETACH)
        report();
    return TRUE;
}
