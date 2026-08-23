/*
 * Four calls that answer a game badly enough to end it, and the answers they
 * should have given. Two are D3D12, one is DXGI, one is GDI.
 *
 * Each guard is inert where its fault is absent: a game that never asks the
 * question never notices this is here. They live together because they are the
 * same kind of repair -- the layer below returns something a caller cannot use,
 * or does not return at all, and the caller does not check.
 *
 * Three of them make the answer match what Windows would have said, so they can
 * run in front of any game. The fourth does not: it puts resolutions into a
 * list that the display genuinely cannot do, which is a useful lie for exactly
 * one game and a lie for every other. It is named to the executable it is for.
 *
 * ONE: a root signature that is not there.
 *
 * A game may reasonably ask whether a compiled shader carries an embedded root
 * signature, and the way to ask is to hand the whole container to
 * D3D12CreateRootSignatureDeserializer and see what comes back. On Windows a
 * container with no RTS0 part returns E_INVALIDARG and the caller carries on.
 *
 * Under D3DMetal that call does not return an error. It reads a field at
 * offset 4 of the part it did not find and the process dies -- silently, with
 * no dialog, no Wine backtrace and nothing in any log. Measured on TEENAGE
 * MUTANT NINJA TURTLES: SPLINTERED FATE, which asks the question about the
 * first shader it loads and never survives the answer:
 *
 *     RS [#1] CreateRootSignatureDeserializer, 3224 bytes, first dword DXBC
 *     CRASH  ACCESS_VIOLATION reading 0x4
 *            in D3DMetal.framework/.../libmetalirconverter.dylib
 *
 * The container in question holds SFI0, ISG1, OSG1, PSV0, STAT, HASH and DXIL.
 * There is no RTS0 in it, and there is not meant to be.
 *
 * So this looks first. Walking a DXBC container is reading a count and an
 * array of offsets, it touches nothing but the caller's own bytes, and if the
 * part is absent the answer is the one Windows gives. When the part IS there,
 * the call goes through untouched -- this adds a check, not a reimplementation.
 *
 * THE CARRIER. The game imports fmod.dll statically, which is audio and has
 * nothing to do with rendering. The original is renamed fmod_real.dll and every
 * export is forwarded to it.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\d3d12-guards.log"

static void logf_(const char *fmt, ...)
{
    char buf[512], line[640];
    va_list ap;
    HANDLE h;
    DWORD wrote;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    n = _snprintf(line, sizeof(line) - 2, "%s\r\n", buf);
    if (n < 0) return;
    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, line, (DWORD)n, &wrote, NULL);
    CloseHandle(h);
}

/*
 * Is there an RTS0 part in this container?
 *
 * DXBC layout: "DXBC", a 16-byte digest, a version, the total size, a part
 * count, then that many 32-bit offsets. Each part is a four-character code, a
 * size, and its bytes. Every read below is bounded by the length the caller
 * gave us, because a guard that can itself run off the end is not a guard.
 */
static BOOL has_root_signature(const void *blob, SIZE_T len)
{
    const unsigned char *b = (const unsigned char *)blob;
    unsigned int count, i;

    if (!b || len < 36) return FALSE;
    if (b[0] != 'D' || b[1] != 'X' || b[2] != 'B' || b[3] != 'C') return FALSE;

    count = *(const unsigned int *)(b + 28);
    if (count > 64) return FALSE;                       /* not a real container */
    if (len < 32 + (SIZE_T)count * 4) return FALSE;

    for (i = 0; i < count; ++i)
    {
        unsigned int off = *(const unsigned int *)(b + 32 + i * 4);
        if (off > len || len - off < 8) continue;
        if (b[off] == 'R' && b[off+1] == 'T' && b[off+2] == 'S' && b[off+3] == '0')
            return TRUE;
    }
    return FALSE;
}

/*
 * TWO: a compute-only device that cannot be created.
 *
 * D3D_FEATURE_LEVEL_1_0_CORE is the minimum a caller passes when it wants a
 * device for compute alone -- Unreal asks for one to run its neural-network
 * back end on DirectML, separately from the device it renders with. Wine's
 * d3d12 answers E_INVALIDARG, because that feature level is not implemented.
 *
 * A caller that does not check dereferences the device it did not get. That is
 * worth guarding on its own account, and the retry below is cheap.
 *
 * It is NOT what killed TORMENTED SOULS 2, and the record here said so for a
 * while. That game does ask twice -- the log shows both retries succeeding --
 * and it went on crashing at 0xfffffffffffffff8 regardless. The address had
 * the same shape as a null device and the coincidence held the diagnosis up
 * for two runs. What actually caused it is guard FOUR. A guard that fires is
 * not evidence that it fixed anything.
 *
 * MinimumFeatureLevel is a floor, not a request. A caller asking for at least
 * 1_0_CORE is satisfied by anything above it, so when that specific level is
 * refused this asks again for 11_0 and hands back what comes. The device is
 * more capable than the one requested, never less.
 *
 * If the second attempt fails too, the original error is returned unchanged --
 * this turns a crash into a failure the caller can see, which is the least it
 * should do and sometimes all it can.
 */
#define D3D_FEATURE_LEVEL_1_0_CORE_ 0x1000
#define D3D_FEATURE_LEVEL_11_0_     0xb000

static HRESULT (WINAPI *real_create_device)(void *, UINT, REFIID, void **);
static LONG core_retried, core_failed;
static HRESULT (WINAPI *real_enumoutputs)(void *, UINT, void **);
static HRESULT WINAPI my_modelist(void *, UINT, UINT, UINT *, void *);
static BOOL wants_a_16_9_mode(void);   /* defined with the guard it belongs to */
static HRESULT (WINAPI *real_modelist)(void *, UINT, UINT, UINT *, void *);

/* IDXGIAdapter slot 7 is EnumOutputs; IDXGIOutput slot 8 GetDisplayModeList. */
/* Returns TRUE when the slot points at repl once this returns, whether or not
 * this call is what put it there. The original is written through *was only by
 * the call that actually installed the hook -- a slot already pointing at repl
 * leaves *was alone.
 *
 * That distinction is the whole reason this takes an out-parameter. Returning
 * the original directly cannot express "already patched" and "could not patch"
 * as different answers: both come back NULL, and a caller that stores the
 * result unconditionally erases the pointer an earlier call saved, leaving the
 * vtable aimed at a hook whose way back is gone. The next call through that
 * slot is an indirect call to address zero. */
static BOOL patch_slot(void *obj, int slot, void *repl, void **was)
{
    void **vtbl = *(void ***)obj;
    DWORD old;
    if (vtbl[slot] == repl) return TRUE;          /* ours already; *was untouched */
    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
    *was = vtbl[slot];
    vtbl[slot] = repl;
    VirtualProtect(&vtbl[slot], sizeof(void *), old, &old);
    return TRUE;
}

static HRESULT WINAPI my_enumoutputs(void *self, UINT i, void **out)
{
    HRESULT hr = real_enumoutputs(self, i, out);
    if (SUCCEEDED(hr) && out && *out && !real_modelist)
        patch_slot(*out, 8, (void *)my_modelist, (void **)&real_modelist);
    return hr;
}

static HRESULT WINAPI my_create_device(void *adapter, UINT min_level, REFIID iid, void **out)
{
    HRESULT hr = real_create_device(adapter, min_level, iid, out);
    if (adapter && !real_enumoutputs && wants_a_16_9_mode())
        patch_slot(adapter, 7, (void *)my_enumoutputs, (void **)&real_enumoutputs);

    if (SUCCEEDED(hr) || min_level != D3D_FEATURE_LEVEL_1_0_CORE_) return hr;

    {
        HRESULT again = real_create_device(adapter, D3D_FEATURE_LEVEL_11_0_, iid, out);
        if (SUCCEEDED(again))
        {
            if (InterlockedIncrement(&core_retried) <= 8)
                logf_("1_0_CORE refused with 0x%08lx -- asked again for 11_0 and got a "
                      "device, which satisfies the floor that was requested",
                      (unsigned long)hr);
            return again;
        }
        if (InterlockedIncrement(&core_failed) <= 8)
            logf_("1_0_CORE refused with 0x%08lx and 11_0 refused with 0x%08lx -- "
                  "returning the original error rather than nothing",
                  (unsigned long)hr, (unsigned long)again);
        if (out) *out = NULL;
    }
    return hr;
}

static HRESULT (WINAPI *real_deserialize)(const void *, SIZE_T, REFIID, void **);
static HRESULT (WINAPI *real_deserialize_v)(const void *, SIZE_T, REFIID, void **);
static LONG refused, passed;

static HRESULT WINAPI my_deserialize(const void *blob, SIZE_T len, REFIID iid, void **out)
{
    if (!has_root_signature(blob, len))
    {
        if (InterlockedIncrement(&refused) <= 8)
            logf_("no RTS0 in a %llu byte container -- answering E_INVALIDARG "
                  "instead of letting it be dereferenced",
                  (unsigned long long)len);
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    InterlockedIncrement(&passed);
    return real_deserialize(blob, len, iid, out);
}

static HRESULT WINAPI my_deserialize_v(const void *blob, SIZE_T len, REFIID iid, void **out)
{
    if (!has_root_signature(blob, len))
    {
        if (InterlockedIncrement(&refused) <= 8)
            logf_("no RTS0 in a %llu byte container (versioned) -- answering "
                  "E_INVALIDARG", (unsigned long long)len);
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    InterlockedIncrement(&passed);
    return real_deserialize_v(blob, len, iid, out);
}

/*
 * THREE: an enumeration that finished and then said it failed.
 *
 * EnumDisplayMonitors returns FALSE for two different reasons: the callback
 * asked to stop, or the enumeration failed. A caller cannot tell them apart
 * from the return value alone -- but it can be told apart from inside, because
 * the callback's own answers are visible here.
 *
 * Measured on TORMENTED SOULS 2: one monitor is visited, it is described
 * correctly at 2056x1329, GetMonitorInfo flags it MONITORINFOF_PRIMARY, the
 * callback returns TRUE every time -- and EnumDisplayMonitors then returns
 * FALSE. An engine reading that as "no monitors" keeps an empty list, and the
 * index of a primary monitor it never found stays at -1.
 *
 * So when the callback never asked to stop and at least one monitor was
 * visited, this reports the success that actually happened. When the callback
 * did ask to stop, FALSE is the right answer and it is passed through
 * unchanged -- the distinction is the whole point, and guessing it would be
 * worse than not guarding at all.
 */
static BOOL (WINAPI *real_EnumDisplayMonitors)(HDC, const RECT *, MONITORENUMPROC, LPARAM);
static LONG monitors_seen, enum_corrected;

/* What one enumeration needs to know about itself. It lives on the stack of
 * whichever thread made the call and travels as the LPARAM, so two threads
 * enumerating at once cannot see each other's. Holding the caller's callback
 * in a global instead would hand one caller's context pointer to another
 * caller's function, and that function would write through it. */
struct enum_call {
    MONITORENUMPROC proc;
    LPARAM          data;
    LONG            seen;
    LONG            stopped;
};

static BOOL CALLBACK watching_proc(HMONITOR mon, HDC dc, LPRECT r, LPARAM p)
{
    struct enum_call *c = (struct enum_call *)p;
    BOOL keep_going = c->proc(mon, dc, r, c->data);
    c->seen++;
    if (!keep_going) c->stopped++;
    return keep_going;
}

static BOOL WINAPI my_EnumDisplayMonitors(HDC dc, const RECT *clip,
                                          MONITORENUMPROC proc, LPARAM data)
{
    struct enum_call c;
    BOOL ok;

    if (!proc) return real_EnumDisplayMonitors(dc, clip, proc, data);

    c.proc = proc; c.data = data; c.seen = 0; c.stopped = 0;
    ok = real_EnumDisplayMonitors(dc, clip, watching_proc, (LPARAM)&c);
    InterlockedExchangeAdd(&monitors_seen, c.seen);

    if (!ok && c.seen > 0 && c.stopped == 0)
    {
        if (InterlockedIncrement(&enum_corrected) <= 8)
            logf_("EnumDisplayMonitors visited %ld monitor(s), the callback never asked "
                  "to stop, and it still returned FALSE -- reporting TRUE", c.seen);
        return TRUE;
    }
    return ok;
}

/*
 * FOUR: a display mode list with no 16:9 in it.
 *
 * TORMENTED SOULS 2 walks the resolutions its RHI reports, computes the aspect
 * ratio of each, and keeps only those falling strictly between 1.76 and 1.79 --
 * which is 16:9 and nothing else. The two bounds are doubles in its .rdata at
 * RVA 0x7e30f20 and 0x7e30f28, and the comparison is two comisd instructions
 * with no fallback after them.
 *
 * A MacBook display is not 16:9. This screen is 2056x1329, an aspect of 1.547,
 * and every mode the system offers for it is 1.6 or 1.547. Not one of the
 * thirteen resolutions the RHI reports passes the filter, so the array the
 * game keeps them in stays empty, the search for a current mode returns
 * INDEX_NONE, and indexing an empty array with -1 reads eight bytes below
 * zero. The RHI is fine, the modes are fine, the monitor is fine: the game
 * simply has no branch for a screen that is not widescreen.
 *
 * So this makes sure 16:9 modes are on offer. What triggers it is not the size
 * of any particular screen -- it is that nothing in the list passes the filter,
 * which is true of every display that is not 16:9: 16:10, 3:2, 4:3, ultrawide,
 * and this one. On a 16:9 display the loop finds a mode and returns having
 * changed nothing.
 *
 * It adds the whole 16:9 ladder that fits inside the desktop rather than one
 * token entry, so the resolution menu has real choices in it. That costs the
 * game nothing: it renders into a swap chain of whatever size it asks for, as
 * it already does here.
 *
 * The one thing here that belongs to this game is the 1.76/1.79 window, read
 * out of its own .rdata. A game that filtered for some other aspect would need
 * its own number; the shape of the repair would be the same.
 *
 * DXGI_MODE_DESC is 28 bytes: Width 0, Height 4, RefreshRate numerator 8 and
 * denominator 12, Format 16, ScanlineOrdering 20, Scaling 24.
 */
#define MODE_DESC_SIZE 28

static HRESULT (WINAPI *real_modelist)(void *, UINT, UINT, UINT *, void *);
static LONG modes_added;

/* The window the game accepts, read out of its own .rdata. Anything inside it
 * is what the filter calls acceptable. */
#define ASPECT_LOW  1.76
#define ASPECT_HIGH 1.79

/* The image name of this process, without its directory. Read once in DllMain,
 * before a hook can be reached, so nothing here needs synchronising. */
static char exe_name[MAX_PATH];

/* Guard FOUR is the only one that does not make an answer more truthful. The
 * other three say what Windows says; this one puts resolutions into a list that
 * the display cannot actually do. That is the right trade for a game whose only
 * alternative is dying on an empty array, and the wrong one for a game that
 * would have been fine, so it runs for named executables and nowhere else.
 *
 * A new game belongs on this list once its crash has been traced to the same
 * empty-array shape -- not because it is Unreal, and not because it also fails
 * on a laptop. */
static BOOL wants_a_16_9_mode(void)
{
    /* Matched as a prefix, because the process that loads this is not the one
     * whose name is on the folder. Unreal ships a small launcher at the top
     * level and does the work in Binaries/Win64 under a decorated name --
     * TormentedSouls2.exe starts TormentedSouls2-Win64-Shipping.exe, and it is
     * the second one that loads the carrier and the second one that crashed.
     * Naming only the launcher would leave this guard dormant in the process
     * that needs it, which is a fix that does nothing and reports nothing. */
    static const char *const named[] = { "TormentedSouls2" };
    unsigned i;
    for (i = 0; i < sizeof(named) / sizeof(named[0]); ++i)
    {
        int len = lstrlenA(named[i]);
        if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE,
                           exe_name, len, named[i], len) == CSTR_EQUAL)
            return TRUE;
    }
    return FALSE;
}

/* The 16:9 ladder, largest first. Every one of these is exactly 16:9, so every
 * one of them survives the filter. Only the ones that fit inside the desktop
 * are offered: a resolution larger than the panel is not a choice, it is a
 * complaint waiting to happen. If the desktop is smaller than all of them the
 * smallest goes in anyway -- an oversized mode beats an empty list, which is
 * the thing that ends the process. */
static const struct { UINT w, h; } ladder[] = {
    { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 },
    { 1600,  900 }, { 1366,  768 }, { 1280,  720 },
};
#define LADDER_N ((UINT)(sizeof(ladder) / sizeof(ladder[0])))

/* How many of the ladder fit on a desktop this size. Never zero. */
static UINT ladder_fits(UINT dw, UINT dh)
{
    UINT i, n = 0;
    for (i = 0; i < LADDER_N; ++i)
        if (ladder[i].w <= dw && ladder[i].h <= dh) ++n;
    return n ? n : 1;
}

static BOOL desktop_size(UINT *w, UINT *h, UINT *hz)
{
    /* DEVMODEW: dmSize at 68, dmPelsWidth 172, dmPelsHeight 176, frequency 184. */
    static UINT cw, ch, chz;
    if (!cw)
    {
        unsigned char dm[220];
        memset(dm, 0, sizeof(dm));
        *(WORD *)(dm + 68) = (WORD)sizeof(dm);
        if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, (DEVMODEW *)dm)) return FALSE;
        cw  = *(const DWORD *)(dm + 172);
        ch  = *(const DWORD *)(dm + 176);
        chz = *(const DWORD *)(dm + 184);
        if (!chz) chz = 60;
    }
    if (!cw || !ch) return FALSE;
    *w = cw; *h = ch; *hz = chz;
    return TRUE;
}

static HRESULT WINAPI my_modelist(void *self, UINT fmt, UINT flags, UINT *count, void *modes)
{
    HRESULT hr;
    UINT w = 0, h = 0, hz = 0, i, have, room, added = 0, big_w = 0, big_h = 0;

    if (!desktop_size(&w, &h, &hz)) return real_modelist(self, fmt, flags, count, modes);

    if (!modes)                                   /* the counting call */
    {
        hr = real_modelist(self, fmt, flags, count, NULL);
        if (SUCCEEDED(hr) && count) *count += ladder_fits(w, h);  /* room for ours */
        return hr;
    }

    /* The filling call. Hold back the slots we may need, fill the rest normally. */
    room = ladder_fits(w, h);
    have = *count;
    /* Never write past what the caller allocated. It asked for a count first
     * and this said "that many plus room", but a caller is free to hand back a
     * smaller buffer than it was told about, and appending into one would be a
     * far worse bug than the crash being repaired. */
    if (room > have) room = have;
    *count = have - room;
    hr = real_modelist(self, fmt, flags, count, modes);
    if (FAILED(hr)) { *count = have; return hr; }

    {
        /* Say what is actually on offer, with the right stride. A mode list
         * read at the wrong stride reports resolutions nobody offered, and a
         * conclusion drawn from it is worse than no conclusion. */
        static LONG told;
        UINT tallest = 0, widest = 0;
        for (i = 0; i < *count; ++i)
        {
            const unsigned char *m = (const unsigned char *)modes + (size_t)i * MODE_DESC_SIZE;
            UINT mw = *(const UINT *)(m + 0), mh = *(const UINT *)(m + 4);
            if (mh > tallest) tallest = mh;
            if (mw > widest) widest = mw;
        }
        if (InterlockedIncrement(&told) <= 4)
            logf_("mode list for format %u: %u modes, largest %ux%u; the screen is %ux%u",
                  fmt, *count, widest, tallest, w, h);
    }
    for (i = 0; i < *count; ++i)
    {
        const unsigned char *m = (const unsigned char *)modes + (size_t)i * MODE_DESC_SIZE;
        UINT mw = *(const UINT *)(m + 0), mh = *(const UINT *)(m + 4);
        double aspect;
        if (!mh) continue;
        aspect = (double)mw / (double)mh;
        if (aspect > ASPECT_LOW && aspect < ASPECT_HIGH)
        {
            static LONG said;
            if (InterlockedIncrement(&said) <= 4)
                logf_("        a 16:9 mode is already on offer (%ux%u), adding nothing",
                      mw, mh);
            return hr;
        }
    }
    /* Nothing on offer is 16:9 and this game keeps nothing else, so the array
     * it is about to build would come out empty. Give it the whole ladder that
     * fits, not just enough to survive: the player gets a resolution menu, not
     * a single forced entry. */
    for (i = 0; i < LADDER_N && added < room; ++i)
    {
        unsigned char *m;
        if ((ladder[i].w > w || ladder[i].h > h) && !(added == 0 && i + 1 == LADDER_N))
            continue;
        m = (unsigned char *)modes + (size_t)(*count) * MODE_DESC_SIZE;
        if (*count) memcpy(m, modes, MODE_DESC_SIZE);  /* inherit format and scaling */
        else memset(m, 0, MODE_DESC_SIZE);
        *(UINT *)(m + 0)  = ladder[i].w;
        *(UINT *)(m + 4)  = ladder[i].h;
        *(UINT *)(m + 8)  = hz;
        *(UINT *)(m + 12) = 1;
        *(UINT *)(m + 16) = fmt;
        *count += 1;
        if (!added) { big_w = ladder[i].w; big_h = ladder[i].h; }  /* largest first */
        ++added;
    }
    if (InterlockedIncrement(&modes_added) <= 8)
        logf_("not one of the %u modes on offer is 16:9, and this game keeps "
              "nothing else -- added %u of them, largest %ux%u @ %u Hz",
              *count - added, added, big_w, big_h, hz);
    return hr;
}

/* Rewrite one imported function in this process's own import table. */
static void *hook_import(const char *dll, const char *func, void *repl);
static void logf_(const char *fmt, ...);

static void *hook_import(const char *dll, const char *func, void *repl)
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
            IMAGE_IMPORT_BY_NAME *n;
            DWORD old;
            void *was;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            n = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig->u1.AddressOfData);
            if (lstrcmpiA((const char *)n->Name, func) != 0) continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old))
                continue;
            was = (void *)(ULONG_PTR)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return was;
        }
    }
    return NULL;
}

/*
 * The device call is often not in the import table at all -- Unreal resolves it
 * with GetProcAddress, and other titles reach it through a drop-in such as
 * NVIDIA Streamline. Watching the lookup catches every one of those.
 */
static HRESULT (WINAPI *real_d3d11_create)(void *, UINT, HMODULE, UINT, const UINT *,
                                           UINT, UINT, void **, UINT *, void **);
static HRESULT WINAPI my_d3d11_create(void *, UINT, HMODULE, UINT, const UINT *,
                                      UINT, UINT, void **, UINT *, void **);

static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI my_GetProcAddress(HMODULE mod, LPCSTR name)
{
    FARPROC p = real_GetProcAddress(mod, name);
    if (!p || !name || (ULONG_PTR)name <= 0xffff) return p;
    if (lstrcmpA(name, "D3D12CreateDevice") == 0)
    {
        if (!real_create_device) real_create_device = (void *)p;
        return (FARPROC)my_create_device;
    }
    /* Not imported by every title -- Unreal resolves it at run time, so the
     * import table alone finds nothing and the adapter never gets watched. */
    if (lstrcmpA(name, "D3D11CreateDevice") == 0)
    {
        if (!real_d3d11_create) real_d3d11_create = (void *)p;
        return (FARPROC)my_d3d11_create;
    }
    return p;
}

/*
 * Say so when a guard did not manage to prevent a crash.
 *
 * A fix whose whole job is stopping a process from dying should be able to
 * report that it died anyway, and where. Without this, "still crashes" and
 * "crashes somewhere else now" look identical from outside, and they are the
 * difference between a wrong diagnosis and an incomplete one.
 *
 * Log-only, bounded, and it changes nothing: the exception carries on to
 * whoever else wants it.
 */
static const char *exception_name(DWORD code)
{
    switch (code)
    {
    case 0xC0000005: return "ACCESS_VIOLATION";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC0000006: return "IN_PAGE_ERROR";
    case 0xC0000094: return "INT_DIVIDE_BY_ZERO";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC0000374: return "HEAP_CORRUPTION";
    default:         return NULL;
    }
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *info)
{
    static LONG told;
    const char *what;
    void *at;
    char module[MAX_PATH] = "?";
    HMODULE owner = NULL;

    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    what = exception_name(info->ExceptionRecord->ExceptionCode);
    if (!what) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&told) > 4) return EXCEPTION_CONTINUE_SEARCH;

    at = info->ExceptionRecord->ExceptionAddress;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)at, &owner) && owner)
        GetModuleFileNameA(owner, module, sizeof(module) - 1);

    logf_("CRASH  %s at %p", what, at);
    logf_("       in %s", owner ? module : "no loaded module");
    if (owner)
        logf_("       offset +0x%llx", (unsigned long long)((char *)at - (char *)owner));
    if (info->ExceptionRecord->ExceptionCode == 0xC0000005
        && info->ExceptionRecord->NumberParameters >= 2)
        logf_("       %s address %p",
              info->ExceptionRecord->ExceptionInformation[0] ? "writing to" : "reading from",
              (void *)info->ExceptionRecord->ExceptionInformation[1]);
    logf_("       guards so far: %ld containers refused, %ld passed, "
          "%ld devices retried, %ld unobtainable",
          refused, passed, core_retried, core_failed);
    return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * The engine hands its adapter to D3D11CreateDevice, and that adapter is the
 * one whose outputs it later asks for. Taking it from the argument needs no
 * guess about how the engine found it.
 */
static HRESULT WINAPI my_d3d11_create(void *adapter, UINT type, HMODULE sw, UINT flags,
                                      const UINT *levels, UINT nlevels, UINT sdk,
                                      void **dev, UINT *got, void **ctx)
{
    if (adapter && !real_enumoutputs && wants_a_16_9_mode())
        patch_slot(adapter, 7, (void *)my_enumoutputs, (void **)&real_enumoutputs);
    return real_d3d11_create(adapter, type, sw, flags, levels, nlevels, sdk, dev, got, ctx);
}

/*
 * Patch the mode list without waiting for the game to ask for one.
 *
 * Catching the engine's own call is a race that can be lost: a title that
 * resolves D3D11CreateDevice during static initialisation has already done so
 * before any thread of ours runs, and the adapter never gets watched. But a
 * COM vtable belongs to the class, not the instance -- so an output obtained
 * here, patched and released, patches the one the engine gets later.
 *
 * IDXGIFactory1 slot 12 is EnumAdapters1, IDXGIAdapter slot 7 EnumOutputs,
 * IDXGIOutput slot 8 GetDisplayModeList. Slot 2 is Release on all of them.
 */
static void patch_modelist_via_own_output(void)
{
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HRESULT (WINAPI *create1)(REFIID, void **);
    static const GUID iid_factory1 =
        { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    void *factory = NULL, *adapter = NULL, *output = NULL;
    ULONG (WINAPI *release)(void *);
    HRESULT hr;

    if (!dxgi) return;
    create1 = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!create1) return;
    if (FAILED(create1(&iid_factory1, &factory)) || !factory) return;

    hr = ((HRESULT (WINAPI *)(void *, UINT, void **))(*(void ***)factory)[12])(factory, 0, &adapter);
    if (SUCCEEDED(hr) && adapter)
    {
        hr = ((HRESULT (WINAPI *)(void *, UINT, void **))(*(void ***)adapter)[7])(adapter, 0, &output);
        if (SUCCEEDED(hr) && output)
        {
            patch_slot(output, 8, (void *)my_modelist, (void **)&real_modelist);
            logf_("display mode list %s through an output of our own",
                  real_modelist ? "patched" : "NOT patched");
            release = (ULONG (WINAPI *)(void *))(*(void ***)output)[2];
            release(output);
        }
        else logf_("could not obtain an output to patch through: 0x%08lx", (unsigned long)hr);
        release = (ULONG (WINAPI *)(void *))(*(void ***)adapter)[2];
        release(adapter);
    }
    else logf_("could not obtain an adapter to patch through: 0x%08lx", (unsigned long)hr);
    release = (ULONG (WINAPI *)(void *))(*(void ***)factory)[2];
    release(factory);
}

static DWORD WINAPI worker(void *unused)
{
    void *was;
    (void)unused;

    was = hook_import("d3d12.dll", "D3D12CreateDevice", (void *)my_create_device);
    if (was) real_create_device = was;
    was = hook_import("USER32.dll", "EnumDisplayMonitors", (void *)my_EnumDisplayMonitors);
    if (was) real_EnumDisplayMonitors = was;
    was = hook_import("d3d11.dll", "D3D11CreateDevice", (void *)my_d3d11_create);
    if (was) real_d3d11_create = was;
    real_GetProcAddress = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                 "GetProcAddress");
    hook_import("KERNEL32.dll", "GetProcAddress", (void *)my_GetProcAddress);

    was = hook_import("d3d12.dll", "D3D12CreateRootSignatureDeserializer",
                      (void *)my_deserialize);
    if (was) real_deserialize = was;
    was = hook_import("d3d12.dll", "D3D12CreateVersionedRootSignatureDeserializer",
                      (void *)my_deserialize_v);
    if (was) real_deserialize_v = was;

    AddVectoredExceptionHandler(0, on_exception);
    if (wants_a_16_9_mode()) patch_modelist_via_own_output();
    else logf_("the 16:9 mode guard stays out of %s -- it runs for named "
               "executables only", exe_name[0] ? exe_name : "this process");
    logf_("guards: monitors %s, d3d11 %s", 
          real_EnumDisplayMonitors ? "watched" : "not imported",
          real_d3d11_create ? "hooked at startup" : "waiting for GetProcAddress");
    logf_("d3d12-guards: deserializer %s, versioned %s, device %s",
          real_deserialize ? "guarded" : "not imported",
          real_deserialize_v ? "guarded" : "not imported",
          real_create_device ? "guarded at startup" : "waiting for GetProcAddress");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        {
            char path[MAX_PATH];
            DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
            while (len && path[len - 1] != '\\' && path[len - 1] != '/') --len;
            lstrcpynA(exe_name, path + len, sizeof(exe_name));
        }
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (refused || passed)
            logf_("totals: %ld containers had no RTS0 and were refused, %ld had one "
                  "and went through untouched", refused, passed);
        if (modes_added)
            logf_("totals: %ld display mode lists had no 16:9 mode in them",
                  modes_added);
        if (enum_corrected || monitors_seen)
            logf_("totals: %ld monitors visited, %ld enumerations reported as failed "
                  "that had not", monitors_seen, enum_corrected);
        if (core_retried || core_failed)
            logf_("totals: %ld compute devices obtained by asking for 11_0 instead, "
                  "%ld could not be obtained at all", core_retried, core_failed);
    }
    return TRUE;
}
