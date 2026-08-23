/*
 * Answer for a root signature that is not there.
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

#define LOGFILE "C:\\rootsig-guard.log"

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

/* Rewrite one imported function in this process's own import table. */
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

static DWORD WINAPI worker(void *unused)
{
    void *was;
    (void)unused;

    was = hook_import("d3d12.dll", "D3D12CreateRootSignatureDeserializer",
                      (void *)my_deserialize);
    if (was) real_deserialize = was;
    was = hook_import("d3d12.dll", "D3D12CreateVersionedRootSignatureDeserializer",
                      (void *)my_deserialize_v);
    if (was) real_deserialize_v = was;

    logf_("rootsig-guard: deserializer %s, versioned %s",
          real_deserialize ? "guarded" : "not imported",
          real_deserialize_v ? "guarded" : "not imported");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    }
    else if (reason == DLL_PROCESS_DETACH && (refused || passed))
        logf_("totals: %ld containers had no RTS0 and were refused, %ld had one "
              "and went through untouched", refused, passed);
    return TRUE;
}
