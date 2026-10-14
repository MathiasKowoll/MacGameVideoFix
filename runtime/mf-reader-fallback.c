/*
 * mf-reader-fallback.c -- hand a refused video to our own ASF reader.
 *
 * The whole job: when Media Foundation refuses a stream in
 * MFCreateSourceReaderFromByteStream, try mgvf-asf-reader.dll (asf-reader.c)
 * from the same folder, and return its reader instead of the refusal. Nothing
 * else is touched -- no device, no vtable, no other call. A stream Media
 * Foundation can open never reaches the fallback.
 *
 * Built as a proxy of a DLL the game imports, forwarding every export to the
 * original kept as <name>_real.dll. For Devil May Cry 5 that is amd_ags_x64.dll.
 *
 * WHY. RE Engine plays some of its videos -- Devil May Cry 5's skill previews
 * -- as VC-1 in ASF, and no CrossOver decodes VC-1. On stable 26.3 the staged
 * codec answers it; on an engine without it the game is handed a null reader
 * and dereferences it (see the wiki page RE-Engine-VC1). Persona 5 Strikers
 * measured the reader on CrossOver Preview 27 with nothing staged.
 *
 * A file mf-no-asf-reader.txt beside the game turns the fallback off.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#define LOGFILE "C:\\mgvf-mf-fallback.log"
#define LOG_MAX 200

static LONG log_lines;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    HANDLE h;
    DWORD w;

    if (InterlockedIncrement(&log_lines) > LOG_MAX) return;
    n = snprintf(buf, sizeof(buf), "[mf-fallback] ");
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n++] = '\r'; buf[n++] = '\n';
    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, n, &w, NULL);
    CloseHandle(h);
}

typedef HRESULT (WINAPI *create_fn)(void *, void *, void **);
static create_fn real_create;
static create_fn asf_reader_create;

static BOOL load_asf_reader_(void)
{
    static LONG tried;
    WCHAR path[MAX_PATH], *slash;
    HMODULE self = NULL, mod;

    if (InterlockedExchange(&tried, 1)) return asf_reader_create != NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(void *)load_asf_reader_, &self) ||
        !GetModuleFileNameW(self, path, MAX_PATH))
        return FALSE;
    slash = wcsrchr(path, L'\\');
    if (!slash || (slash - path) + 32 >= MAX_PATH) return FALSE;

    lstrcpyW(slash + 1, L"mf-no-asf-reader.txt");
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
    {
        logf_("off (mf-no-asf-reader.txt)");
        return FALSE;
    }
    lstrcpyW(slash + 1, L"mgvf-asf-reader.dll");
    mod = LoadLibraryW(path);
    if (!mod)
    {
        logf_("mgvf-asf-reader.dll not loaded (err %lu) -- install the fix again", GetLastError());
        return FALSE;
    }
    asf_reader_create = (create_fn)(void *)GetProcAddress(mod, "MGVF_CreateAsfReader");
    logf_("our ASF reader: %s", asf_reader_create ? "loaded" : "loaded, but it exports no MGVF_CreateAsfReader");
    return asf_reader_create != NULL;
}

static HRESULT WINAPI my_create(void *stream, void *attrs, void **reader)
{
    static LONG calls, ours;
    HRESULT hr = real_create(stream, attrs, reader);
    LONG n = InterlockedIncrement(&calls);
    if (n == 1) logf_("MFCreateSourceReaderFromByteStream -> 0x%08lx (first call)", hr);
    if (FAILED(hr) && stream && reader && load_asf_reader_())
    {
        HRESULT mine = asf_reader_create(stream, attrs, reader);
        LONG k = InterlockedIncrement(&ours);
        if (k <= 5 || FAILED(mine))
            logf_("Media Foundation refused a stream (0x%08lx); our ASF reader -> 0x%08lx (%ld so far)",
                  hr, mine, k);
        if (SUCCEEDED(mine)) hr = mine;
    }
    return hr;
}

/* The game's own import of the function, replaced in its import table. */
static void *hook_import(const char *dll, const char *func, void *replacement)
{
    HMODULE base = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;

    if (!dir->VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)base + dir->VirtualAddress); imp->Name; imp++)
    {
        IMAGE_THUNK_DATA *orig, *iat;
        if (lstrcmpiA((const char *)((BYTE *)base + imp->Name), dll) != 0) continue;
        orig = (IMAGE_THUNK_DATA *)((BYTE *)base + imp->OriginalFirstThunk);
        iat  = (IMAGE_THUNK_DATA *)((BYTE *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; orig++, iat++)
        {
            IMAGE_IMPORT_BY_NAME *by;
            void *original;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            by = (IMAGE_IMPORT_BY_NAME *)((BYTE *)base + orig->u1.AddressOfData);
            if (lstrcmpiA((const char *)by->Name, func) != 0) continue;
            original = (void *)iat->u1.Function;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return NULL;
            iat->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return original;
        }
    }
    return NULL;
}

/* For a game that resolves the function by name instead. */
static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI my_GetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC proc = real_GetProcAddress(module, name);
    if (proc && name && ((ULONG_PTR)name >> 16) &&
        lstrcmpA(name, "MFCreateSourceReaderFromByteStream") == 0)
    {
        if (!real_create) real_create = (create_fn)(void *)proc;
        return (FARPROC)(void *)my_create;
    }
    return proc;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        void *was;
        DisableThreadLibraryCalls(inst);
        was = hook_import("MFReadWrite.dll", "MFCreateSourceReaderFromByteStream", (void *)my_create);
        if (was) real_create = (create_fn)was;
        *(void **)&real_GetProcAddress =
            hook_import("KERNEL32.dll", "GetProcAddress", (void *)my_GetProcAddress);
        if (!real_GetProcAddress)
            *(FARPROC *)&real_GetProcAddress =
                GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
        logf_("armed: MFCreateSourceReaderFromByteStream %s", was ? "hooked in the import table"
              : "not imported; watched through GetProcAddress");
    }
    return TRUE;
}
