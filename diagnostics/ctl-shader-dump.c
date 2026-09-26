/*
 * ctl-shader-dump.c -- Control Resonant: record every shader the title hands
 * to D3D12, and nothing else.
 *
 * WHY. Interior-mapped windows render their fake rooms with the right shapes
 * and flat, saturated colours on D3DMetal, with Metal 4 on and off alike. The
 * ray-box maths works and the cubemap read does not, so the next question is
 * which shader draws them and what it asks of the texture. This answers the
 * first half: every graphics, compute and stream pipeline the title creates
 * leaves its shader bytecode in C:\ctl-shaders\, once per distinct shader, and
 * a line in C:\ctl-shaders\pipelines.log naming the shaders each pipeline uses.
 *
 * WHERE IT HOOKS. The title never imports d3d12: NVIDIA Streamline
 * (sl.interposer.dll) and NRI load it themselves and look the device call up
 * with GetProcAddress. So GetProcAddress is watched in every module loaded
 * when this starts, and the device that comes back -- D3DMetal's own, beneath
 * Streamline's proxy -- has its pipeline creators patched at class level.
 * Hooking inside Streamline is what broke Ronin; this stays below it.
 *
 * WHAT IT CHANGES. Nothing the title receives: every call goes through with
 * its own arguments and its own result. It only reads the descriptions.
 *
 * Carried on PxFoundation_x64.dll with runtime/build-proxy.sh.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define DIR "C:\\ctl-shaders"

static CRITICAL_SECTION cs;
static FILE *logf;
static LONG pipelines, written;

static void logline(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    if (!logf) return;
#ifdef CTL_FIX
    /* The fix build keeps its log short: no line per pipeline. */
    if (!strncmp(fmt, "stream #", 8) || !strncmp(fmt, "gfx #", 5) || !strncmp(fmt, "cs #", 4)) return;
#endif
    GetLocalTime(&t);
    EnterCriticalSection(&cs);
    fprintf(logf, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); vfprintf(logf, fmt, ap); va_end(ap);
    fputc('\n', logf); fflush(logf);
    LeaveCriticalSection(&cs);
}

/* FNV-1a over the whole container: the same shader always gets the same name. */
static unsigned long long fnv(const unsigned char *p, SIZE_T n)
{
    unsigned long long h = 0xcbf29ce484222325ULL;
    while (n--) { h ^= *p++; h *= 0x100000001b3ULL; }
    return h;
}

/* A D3D12_SHADER_BYTECODE is {pointer, size}. Returns the hash, or 0 when
 * there is no shader there. Writes the file the first time a hash is seen. */
static unsigned long long keep(const char *stage, const void *code, SIZE_T len)
{
    char path[MAX_PATH];
    unsigned long long h;
    DWORD attr;
    HANDLE f;
    if (!code || len < 32 || len > (64u << 20)) return 0;
    if (memcmp(code, "DXBC", 4) != 0) return 0;
    h = fnv(code, len);
#ifdef CTL_FIX
    (void)path; (void)attr; (void)f; (void)stage;
    return h;
#endif
    snprintf(path, sizeof(path), DIR "\\%s_%016llx.dxbc", stage, h);
    attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES) return h;
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD w;
        WriteFile(f, code, (DWORD)len, &w, NULL);
        CloseHandle(f);
        InterlockedIncrement(&written);
    }
    return h;
}

static BOOL readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof(mi))) return FALSE;
    if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return FALSE;
    return (const char *)p + n <= (const char *)mi.BaseAddress + mi.RegionSize;
}

static LONG device_seen;

/* ---- the three pipeline creators ---------------------------------------- */
typedef HRESULT (WINAPI *create_fn)(void *, const void *, REFIID, void **);
static create_fn real_gfx, real_cs, real_stream;

/* D3D12_GRAPHICS_PIPELINE_STATE_DESC on x64: root signature, then VS, PS, DS,
 * HS and GS as {pointer, size} at 8, 24, 40, 56 and 72. */
static HRESULT WINAPI my_gfx(void *dev, const void *desc, REFIID iid, void **out)
{
    HRESULT hr = real_gfx(dev, desc, iid, out);
    const char *d = desc;
    if (d)
    {
        unsigned long long vs = keep("vs", *(void **)(d + 8),  *(SIZE_T *)(d + 16));
        unsigned long long ps = keep("ps", *(void **)(d + 24), *(SIZE_T *)(d + 32));
        unsigned long long ds = keep("ds", *(void **)(d + 40), *(SIZE_T *)(d + 48));
        unsigned long long hs = keep("hs", *(void **)(d + 56), *(SIZE_T *)(d + 64));
        unsigned long long gs = keep("gs", *(void **)(d + 72), *(SIZE_T *)(d + 80));
        logline("gfx #%ld %p hr %08lx vs %016llx ps %016llx ds %016llx hs %016llx gs %016llx",
                InterlockedIncrement(&pipelines), out ? *out : NULL, (unsigned long)hr, vs, ps, ds, hs, gs);
    }
    return hr;
}

/* D3D12_COMPUTE_PIPELINE_STATE_DESC: root signature, then CS at 8. */
static HRESULT WINAPI my_cs(void *dev, const void *desc, REFIID iid, void **out)
{
    HRESULT hr = real_cs(dev, desc, iid, out);
    const char *d = desc;
    if (d)
        logline("cs #%ld %p hr %08lx cs %016llx", InterlockedIncrement(&pipelines), out ? *out : NULL,
                (unsigned long)hr, keep("cs", *(void **)(d + 8), *(SIZE_T *)(d + 16)));
    return hr;
}


/* ---- EXPERIMENT: hide pipelines by pixel shader --------------------------
 * C:\ctl-shaders\kill.txt lists pixel-shader hashes, one per line. A pipeline
 * whose pixel shader is listed is created with every render target's write
 * mask at 0: the shader still runs, and nothing it computes reaches the
 * screen. Nothing else about the pipeline changes. Used to find which shader
 * draws the window interiors by taking candidates away. */
static unsigned long long kill_list[4096];
static int kill_n;
static BOOL kill_blended;       /* a line "blended" in kill.txt: mute every blending pipeline */
static LONG killed;

static void load_kill_list(void)
{
    FILE *f = fopen(DIR "\\kill.txt", "r");
    char line[128];
    if (!f) return;
    while (kill_n < 4096 && fgets(line, sizeof(line), f))
    {
        unsigned long long h;
        if (!strncmp(line, "blended", 7)) { kill_blended = TRUE; continue; }
        if (sscanf(line, "%*[^0-9a-fA-F]%llx", &h) == 1 || sscanf(line, "%llx", &h) == 1)
            if (h) kill_list[kill_n++] = h;
    }
    fclose(f);
}

static BOOL is_killed(unsigned long long h)
{
    int i;
    for (i = 0; i < kill_n; i++) if (kill_list[i] == h) return TRUE;
    return FALSE;
}

/* The BLEND subobject is type 8; its D3D12_BLEND_DESC (4-byte aligned) starts
 * right after the type: two BOOLs, then eight 40-byte render-target blocks
 * whose last UINT8 of interest, RenderTargetWriteMask, sits at +36. A slot is
 * taken for BLEND only when every field looks like one. */
static const char *find_blend(const char *s, SIZE_T size);
static BOOL mute_blend(char *s, SIZE_T size)
{
    SIZE_T off;
    for (off = 0; off + 4 + 8 + 8 * 40 <= size; off += 8)
    {
        const char *b;
        int i, ok = 1;
        if (*(const UINT *)(s + off) != 8) continue;
        b = s + off + 4;
        if (*(const UINT *)b > 1 || *(const UINT *)(b + 4) > 1) continue;
        for (i = 0; i < 8 && ok; i++)
        {
            const UINT *rt = (const UINT *)(b + 8 + i * 40);
            /* Only the first entry has to be a real one: with IndependentBlend
             * off, Control Resonant leaves the other seven all zero. */
            if (rt[0] > 1 || rt[1] > 1 || rt[2] > 21 || rt[3] > 21 || rt[4] > 5) ok = 0;
            if (i == 0 && (rt[2] == 0 || rt[4] == 0)) ok = 0;
            if ((unsigned char)b[8 + i * 40 + 36] > 15) ok = 0;
        }
        if (!ok) continue;
        for (i = 0; i < 8; i++) ((unsigned char *)b)[8 + i * 40 + 36] = 0;
        return TRUE;
    }
    return FALSE;
}


/* ---- EXPERIMENT: replace a pixel shader -----------------------------------
 * C:\ctl-shaders\replace_<16 hex>.dxbc, if present, is used instead of the
 * pixel shader with that hash. If the pipeline cannot be created with it, the
 * original is used and the log says so -- the title never loses the pipeline. */
struct replacement { unsigned long long hash; void *code; SIZE_T len; };
static struct replacement repls[256];
static int repl_n;
static LONG replaced, repl_failed;

static void load_replacements(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(DIR "\\replace_*.dxbc", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        char path[MAX_PATH];
        unsigned long long hash;
        HANDLE f;
        DWORD size, got;
        if (repl_n >= 256 || sscanf(fd.cFileName, "replace_%16llx", &hash) != 1) continue;
        snprintf(path, sizeof(path), DIR "\\%s", fd.cFileName);
        f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        size = GetFileSize(f, NULL);
        repls[repl_n].code = HeapAlloc(GetProcessHeap(), 0, size);
        if (repls[repl_n].code && ReadFile(f, repls[repl_n].code, size, &got, NULL) && got == size)
        {
            repls[repl_n].hash = hash;
            repls[repl_n].len = size;
            logline("replacement for pixel shader %016llx: %lu bytes", hash, (unsigned long)size);
            repl_n++;
        }
        CloseHandle(f);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static const struct replacement *replacement_for(unsigned long long h)
{
    int i;
    for (i = 0; i < repl_n; i++) if (repls[i].hash == h) return &repls[i];
    return NULL;
}

/* The same search as mute_blend, read-only: the BLEND payload, or NULL. */
static const char *find_blend(const char *s, SIZE_T size)
{
    SIZE_T off;
    for (off = 0; off + 4 + 8 + 8 * 40 <= size; off += 8)
    {
        const char *b;
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

/* RENDER_TARGET_FORMATS is type 15: eight DXGI formats, then the count. */
static void rt_formats(const char *s, SIZE_T size, char *out, size_t n)
{
    SIZE_T off;
    out[0] = 0;
    for (off = 0; off + 4 + 36 <= size; off += 8)
    {
        const UINT *f;
        UINT count, i;
        size_t o = 0;
        if (*(const UINT *)(s + off) != 15) continue;
        f = (const UINT *)(s + off + 4);
        count = f[8];
        if (count > 8) continue;
        for (i = 0; i < count && o < n - 8; i++) o += snprintf(out + o, n - o, "%s%u", i ? "," : "", f[i]);
        return;
    }
}

/* D3D12_PIPELINE_STATE_STREAM_DESC is {size, pointer}. The stream is a run of
 * subobjects, each a type aligned to a pointer and then its payload. Rather
 * than carry a size table for every subobject type -- which differs between
 * SDK revisions -- this looks at every pointer-aligned slot for a shader
 * stage type followed by a {pointer, size} whose pointer really starts with
 * a DXBC container. A false match would need both. */
static HRESULT WINAPI my_stream(void *dev, const void *desc, REFIID iid, void **out)
{
    HRESULT hr;
    /* Decide on hiding before the call: the pixel shader is found the same way
     * the logging below finds it, and a listed one gets a muted copy. */
    if (repl_n && desc)
    {
        SIZE_T size = *(const SIZE_T *)desc, off;
        const char *st = *(const char *const *)((const char *)desc + 8);
        if (st && size < (1u << 20) && readable(st, size))
            for (off = 0; off + 24 <= size; off += 8)
            {
                const void *code; SIZE_T len;
                const struct replacement *r;
                if (*(const UINT *)(st + off) != 2) continue;
                code = *(const void *const *)(st + off + 8);
                len = *(const SIZE_T *)(st + off + 16);
                if (!readable(code, 4) || memcmp(code, "DXBC", 4) != 0 || !readable(code, len)) break;
                if (!(r = replacement_for(fnv(code, len)))) break;
                {
                    char *copy = HeapAlloc(GetProcessHeap(), 0, size);
                    SIZE_T d2[2];
                    memcpy(copy, st, size);
                    *(const void **)(copy + off + 8) = r->code;
                    *(SIZE_T *)(copy + off + 16) = r->len;
                    d2[0] = size; d2[1] = (SIZE_T)copy;
                    hr = real_stream(dev, d2, iid, out);
                    HeapFree(GetProcessHeap(), 0, copy);
                    if (SUCCEEDED(hr))
                    {
                        InterlockedIncrement(&replaced);
                        logline("EXPERIMENT: pixel shader %016llx replaced -> %08lx", r->hash, (unsigned long)hr);
                        goto logged;
                    }
                    InterlockedIncrement(&repl_failed);
                    logline("EXPERIMENT: pixel shader %016llx replacement REFUSED %08lx, using the original",
                            r->hash, (unsigned long)hr);
                }
                break;
            }
    }
    if (kill_blended && desc)
    {
        SIZE_T size = *(const SIZE_T *)desc;
        const char *st = *(const char *const *)((const char *)desc + 8);
        const char *b;
        if (st && size < (1u << 20) && readable(st, size) && (b = find_blend(st, size))
            && *(const UINT *)(b + 8) == 1)                /* RT0 BlendEnable */
        {
            char *copy = HeapAlloc(GetProcessHeap(), 0, size);
            SIZE_T d2[2];
            BOOL muted;
            memcpy(copy, st, size);
            muted = mute_blend(copy, size);
            d2[0] = size; d2[1] = (SIZE_T)copy;
            hr = real_stream(dev, d2, iid, out);
            if (muted) InterlockedIncrement(&killed);
            HeapFree(GetProcessHeap(), 0, copy);
            logline("EXPERIMENT: blending pipeline muted -> %08lx", (unsigned long)hr);
            goto logged;
        }
    }
    if (kill_n && desc)
    {
        SIZE_T size = *(const SIZE_T *)desc, off;
        const char *st = *(const char *const *)((const char *)desc + 8);
        if (st && size < (1u << 20) && readable(st, size))
            for (off = 0; off + 24 <= size; off += 8)
            {
                const void *code; SIZE_T len;
                if (*(const UINT *)(st + off) != 2) continue;
                code = *(const void *const *)(st + off + 8);
                len = *(const SIZE_T *)(st + off + 16);
                if (!readable(code, 4) || memcmp(code, "DXBC", 4) != 0 || !readable(code, len)) continue;
                if (is_killed(fnv(code, len)))
                {
                    char *copy = HeapAlloc(GetProcessHeap(), 0, size);
                    SIZE_T d2[2];
                    BOOL muted;
                    memcpy(copy, st, size);
                    muted = mute_blend(copy, size);
                    {
                        /* The raw stream, for reading its layout offline. */
                        static LONG dumped;
                        if (InterlockedIncrement(&dumped) <= 6)
                        {
                            char path[MAX_PATH]; HANDLE h; DWORD w;
                            snprintf(path, sizeof(path), DIR "\\stream_%016llx_%lu.bin",
                                     fnv(code, len), (unsigned long)size);
                            h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                            if (h != INVALID_HANDLE_VALUE) { WriteFile(h, st, (DWORD)size, &w, NULL); CloseHandle(h); }
                        }
                    }
                    d2[0] = size; d2[1] = (SIZE_T)copy;
                    hr = real_stream(dev, d2, iid, out);
                    logline("EXPERIMENT: pixel shader %016llx hidden (blend %s) -> %08lx",
                            fnv(code, len), muted ? "muted" : "NOT FOUND, unchanged", (unsigned long)hr);
                    if (muted) InterlockedIncrement(&killed);
                    HeapFree(GetProcessHeap(), 0, copy);
                    goto logged;
                }
                break;
            }
    }
    hr = real_stream(dev, desc, iid, out);
logged:
    static const char *stage[26] = { 0, "vs", "ps", "ds", "hs", "gs", "cs",
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, "as", "ms" };
    char line[512];
    size_t o = 0;
    if (desc)
    {
        SIZE_T size = *(const SIZE_T *)desc, off;
        const char *s = *(const char *const *)((const char *)desc + 8);
        if (s && readable(s, size))
        {
            for (off = 0; off + 24 <= size; off += 8)
            {
                UINT type = *(const UINT *)(s + off);
                const void *code; SIZE_T len; unsigned long long h;
                if (type >= 26 || !stage[type]) continue;
                code = *(const void *const *)(s + off + 8);
                len = *(const SIZE_T *)(s + off + 16);
                if (!readable(code, 4) || memcmp(code, "DXBC", 4) != 0) continue;
                if (!readable(code, len)) continue;
                h = keep(stage[type], code, len);
                if (h && o < sizeof(line) - 40)
                    o += snprintf(line + o, sizeof(line) - o, " %s %016llx", stage[type], h);
            }
        }
        line[o] = 0;
        {
            char fmts[64] = "";
            const char *b = s && readable(s, size) ? find_blend(s, size) : NULL;
            if (s && readable(s, size)) rt_formats(s, size, fmts, sizeof(fmts));
            logline("stream #%ld %p hr %08lx%s | blend %s rt [%s]", InterlockedIncrement(&pipelines),
                    out ? *out : NULL, (unsigned long)hr, o ? line : " (no shader found)",
                    !b ? "?" : *(const UINT *)(b + 8) ? "on" : "off", fmts);
        }
    }
    return hr;
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

/* ID3D12Device slot 10 CreateGraphicsPipelineState, 11 CreateComputePipelineState;
 * ID3D12Device2 slot 47 CreatePipelineState. */
static void log_env(void)
{
    char *env = GetEnvironmentStringsA(), *e;
    int n = 0;
    if (!env) return;
    for (e = env; *e; e += strlen(e) + 1)
        if (!strncmp(e, "D3DM_", 5) || !strncmp(e, "CX_GRAPHICS_BACKEND", 19)) { logline("env: %s", e); n++; }
    if (!n) logline("env: no D3DM_ variable visible to the game");
    FreeEnvironmentStringsA(env);
}

/* D3D12_FEATURE_D3D12_OPTIONS5 is 27; RaytracingTier is its third field. */
static void log_dxr_tier(void *dev)
{
    UINT o5[3] = { 0, 0, 0 };
    HRESULT hr = ((HRESULT (WINAPI *)(void *, UINT, void *, UINT))(*(void ***)dev)[13])(dev, 27, o5, sizeof(o5));
    logline("D3D12 OPTIONS5 -> %08lx: RaytracingTier %u (0 = none, 10 = 1.0, 11 = 1.1)", (unsigned long)hr, o5[2]);
}

static void watch_device(void *dev)
{
    static const GUID iid_device2 =
        { 0x30baa41e, 0xb15b, 0x475c, { 0xa0, 0xbb, 0x1a, 0xf5, 0xc5, 0xb6, 0x43, 0x28 } };
    static LONG done;
    void *d2 = NULL;
    BOOL a, b, c = FALSE;
    if (!dev) return;
    InterlockedExchange(&device_seen, 1);
    if (InterlockedExchange(&done, 1)) return;
    a = patch_slot(dev, 10, (void *)my_gfx, (void **)&real_gfx);
    b = patch_slot(dev, 11, (void *)my_cs, (void **)&real_cs);
    if (SUCCEEDED(((HRESULT (WINAPI *)(void *, REFIID, void **))(*(void ***)dev)[0])(dev, &iid_device2, &d2)) && d2)
    {
        c = patch_slot(d2, 47, (void *)my_stream, (void **)&real_stream);
        ((ULONG (WINAPI *)(void *))(*(void ***)d2)[2])(d2);
    }
    log_dxr_tier(dev);
    logline("device %p: graphics %s, compute %s, stream %s", dev,
            a ? "hooked" : "NOT hooked", b ? "hooked" : "NOT hooked", c ? "hooked" : "NOT hooked");
}

/* ---- finding the device ---------------------------------------------------- */
static HRESULT (WINAPI *real_create)(void *, UINT, REFIID, void **);
static HRESULT WINAPI my_create(void *adapter, UINT level, REFIID iid, void **out)
{
    HRESULT hr = real_create(adapter, level, iid, out);
    logline("D3D12CreateDevice(level %x) -> %08lx %p", level, (unsigned long)hr, out ? *out : NULL);
    if (SUCCEEDED(hr) && out && *out) watch_device(*out);
    return hr;
}

static FARPROC (WINAPI *real_gpa)(HMODULE, LPCSTR);
static FARPROC WINAPI my_gpa(HMODULE mod, LPCSTR name)
{
    FARPROC p = real_gpa(mod, name);
    if (p && (ULONG_PTR)name > 0xffff && lstrcmpA(name, "D3D12CreateDevice") == 0)
    {
        char who[MAX_PATH] = "";
        GetModuleFileNameA(mod, who, sizeof(who));
        if (!real_create) real_create = (void *)p;
        if ((void *)p == (void *)real_create)
        {
            logline("GetProcAddress(D3D12CreateDevice) in %s: watched", who);
            return (FARPROC)my_create;
        }
    }
    return p;
}

/* The IAT of one module: replace kernel32's GetProcAddress. */
static int hook_gpa_in(HMODULE base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;
    int n = 0;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS *)((char *)base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return 0;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + rva); imp->Name; ++imp)
    {
        IMAGE_THUNK_DATA *orig, *iat;
        const char *dll = (const char *)base + imp->Name;
        if (lstrcmpiA(dll, "KERNEL32.dll") != 0 && _strnicmp(dll, "api-ms-win-core-libraryloader", 29) != 0)
            continue;
        orig = (IMAGE_THUNK_DATA *)((char *)base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)((char *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; ++orig, ++iat)
        {
            IMAGE_IMPORT_BY_NAME *nm;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            nm = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig->u1.AddressOfData);
            if (lstrcmpA((const char *)nm->Name, "GetProcAddress") != 0) continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) continue;
            iat->u1.Function = (ULONG_PTR)my_gpa;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            n++;
        }
    }
    return n;
}

static int hook_create_import_in(HMODULE base);

static void hook_everywhere(void)
{
    static const char *names[] = { NULL, "sl.interposer.dll", "sl.common.dll", "NRI.dll",
                                   "amd_fidelityfx_loader_dx12.dll", "d3d12SDKLayers.dll" };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        HMODULE m = names[i] ? GetModuleHandleA(names[i]) : GetModuleHandleA(NULL);
        if (m) logline("%s: GetProcAddress %d slot, D3D12CreateDevice import %d slot",
                       names[i] ? names[i] : "the executable", hook_gpa_in(m), hook_create_import_in(m));
    }
}

/* The import of D3D12CreateDevice in one module's IAT. sl.common.dll and
 * NRI.dll import it directly; GetProcAddress never sees them ask. */
static int hook_create_import_in(HMODULE base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    DWORD rva;
    int n = 0;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS *)((char *)base + dos->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return 0;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + rva); imp->Name; ++imp)
    {
        IMAGE_THUNK_DATA *orig, *iat;
        if (lstrcmpiA((const char *)base + imp->Name, "d3d12.dll") != 0) continue;
        orig = (IMAGE_THUNK_DATA *)((char *)base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)((char *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; ++orig, ++iat)
        {
            IMAGE_IMPORT_BY_NAME *nm;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            nm = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig->u1.AddressOfData);
            if (lstrcmpA((const char *)nm->Name, "D3D12CreateDevice") != 0) continue;
            if ((void *)(ULONG_PTR)iat->u1.Function == (void *)my_create) continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) continue;
            if (!real_create) real_create = (void *)(ULONG_PTR)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)my_create;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            n++;
        }
    }
    return n;
}

/* Fallback: a device of our own reaches the same class, and a class-level
 * patch covers the title's device too. Only if the title's was not seen. */
static void own_device(void)
{
    static const GUID iid_device =
        { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
    HMODULE d = GetModuleHandleA("d3d12.dll");
    HRESULT (WINAPI *create)(void *, UINT, REFIID, void **);
    void *dev = NULL;
    HRESULT hr;
    if (!d) return;
    create = real_create ? real_create : (void *)real_gpa(d, "D3D12CreateDevice");
    if (!create) return;
    hr = create(NULL, 0xb000, &iid_device, &dev);
    logline("fallback: own D3D12CreateDevice -> %08lx %p", (unsigned long)hr, dev);
    if (SUCCEEDED(hr) && dev)
    {
        watch_device(dev);
        ((ULONG (WINAPI *)(void *))(*(void ***)dev)[2])(dev);
    }
}

/* Modules that load later (sl.common, NRI) are picked up here, for the first
 * minute; each is hooked once. */
static DWORD WINAPI late_modules(void *unused)
{
    static const char *names[] = { "sl.common.dll", "NRI.dll", "sl.dlss.dll", "sl.dlss_g.dll" };
    BOOL seen[4] = { 0 };
    int t;
    size_t i;
    DWORD d3d12_at = 0;
    BOOL fell_back = FALSE;
    (void)unused;
    for (t = 0; t < 30000; t++)                 /* 2 ms steps, one minute */
    {
        for (i = 0; i < 4; i++)
        {
            HMODULE m;
            if (seen[i] || !(m = GetModuleHandleA(names[i]))) continue;
            seen[i] = TRUE;
            logline("%s: GetProcAddress %d slot, D3D12CreateDevice import %d slot (late)", names[i],
                    hook_gpa_in(m), hook_create_import_in(m));
        }
        if (!d3d12_at && GetModuleHandleA("d3d12.dll")) d3d12_at = GetTickCount();
        if (d3d12_at && !fell_back && !device_seen && GetTickCount() - d3d12_at > 3000)
        {
            fell_back = TRUE;
            own_device();
        }
        Sleep(2);
    }
    logline("totals so far: %ld pipelines, %ld distinct shaders written, %ld hidden, %ld replaced, %ld replacements refused", pipelines, written, killed, replaced, repl_failed);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    InitializeCriticalSection(&cs);
    CreateDirectoryA(DIR, NULL);
    logf = fopen(DIR "\\pipelines.log", "a");
    real_gpa = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
    logline("---- ctl-shader-dump loaded");
    log_env();
#ifdef CTL_FIX
    /* The fix: interiorPS draws the window interiors, and on D3DMetal it reads
     * the wrong texture once the title's texture streaming starts. Hidden, the
     * windows are dark glass; nothing else changes. */
    kill_list[kill_n++] = 0xcc6f0f9fb056f7f5ULL;
#else
    load_kill_list();
    load_replacements();
#endif
    logline("kill list: %d pixel shaders to hide", kill_n);
    hook_everywhere();
    CloseHandle(CreateThread(NULL, 0, late_modules, NULL, 0, NULL));
    return TRUE;
}
