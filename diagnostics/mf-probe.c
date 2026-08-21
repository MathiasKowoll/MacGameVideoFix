/*
 * mf-probe — watch what a game asks Media Foundation for, and what it gets.
 *
 * For the silent failure: the game reaches its cutscene, the screen stays
 * black, nothing crashes, and no log is written anywhere. Media Foundation
 * returned an HRESULT the game swallowed, and there is no way to see it from
 * the outside.
 *
 * A WINEDEBUG trace shows it, but buried in hundreds of thousands of lines and
 * only if you can launch the game by hand. This is narrower and sharper: the
 * game imports the functions we care about by name, so we replace the pointers
 * in its import table and log each call with its result. It works when the
 * game is started normally, from Steam.
 *
 * Hooking the import table rather than scanning for byte patterns matters
 * here. The IAT is a documented structure, so this does not depend on which
 * compiler built the game or on anything moving between updates.
 *
 * Read-only: every hook calls through to the real function and returns its
 * result unchanged. Nothing is altered.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\mf-probe.log"

static CRITICAL_SECTION log_lock;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = snprintf(buf, sizeof(buf) - 2, "[%6lu] ", (unsigned long)GetTickCount());
    n += vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
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

/* HRESULTs worth naming, so the log reads without a lookup table. */
static const char *hr_name(HRESULT hr)
{
    switch (hr)
    {
    case S_OK:                              return "S_OK";
    case E_FAIL:                            return "E_FAIL";
    case E_INVALIDARG:                      return "E_INVALIDARG";
    case E_NOTIMPL:                         return "E_NOTIMPL";
    case E_OUTOFMEMORY:                     return "E_OUTOFMEMORY";
    case REGDB_E_CLASSNOTREG:               return "REGDB_E_CLASSNOTREG";
    case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:  return "MF_E_UNSUPPORTED_BYTESTREAM_TYPE";
    case MF_E_UNSUPPORTED_SCHEME:           return "MF_E_UNSUPPORTED_SCHEME";
    case MF_E_INVALIDMEDIATYPE:             return "MF_E_INVALIDMEDIATYPE";
    case MF_E_TOPO_CODEC_NOT_FOUND:         return "MF_E_TOPO_CODEC_NOT_FOUND";
    case MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS:
                                            return "MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS";
    case MF_E_NOT_FOUND:                    return "MF_E_NOT_FOUND";
    case MF_E_ATTRIBUTENOTFOUND:            return "MF_E_ATTRIBUTENOTFOUND";
    default:                                return NULL;
    }
}

static void log_hr(const char *what, HRESULT hr)
{
    const char *name = hr_name(hr);
    if (name) logf_("  %s -> %s", what, name);
    else      logf_("  %s -> 0x%08lx", what, (unsigned long)hr);
}

/* A four-character-code subtype prints as its tag; anything else as a GUID. */
static void describe_subtype(const char *label, const GUID *g)
{
    static const GUID base = { 0x00000000, 0x0000, 0x0010,
                               { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
    if (!g) { logf_("  %s: (none)", label); return; }

    if (g->Data2 == base.Data2 && g->Data3 == base.Data3 &&
        !memcmp(g->Data4, base.Data4, 8) && (g->Data1 >> 24))
    {
        logf_("  %s: '%c%c%c%c'", label,
              (char)(g->Data1 & 0xff), (char)((g->Data1 >> 8) & 0xff),
              (char)((g->Data1 >> 16) & 0xff), (char)((g->Data1 >> 24) & 0xff));
        return;
    }
    logf_("  %s: {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", label,
          (unsigned long)g->Data1, g->Data2, g->Data3,
          g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
          g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static void log_wstr(const char *label, const WCHAR *w)
{
    char buf[512];
    if (!w) { logf_("  %s: (null)", label); return; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf) - 1, NULL, NULL);
    buf[sizeof(buf) - 1] = 0;
    logf_("  %s: %s", label, buf);
}

/* The two attributes the source resolver keys on. If neither is set, it has
 * nothing to match a byte-stream handler against. */
static void log_bytestream_tags(IMFByteStream *stream)
{
    IMFAttributes *attrs = NULL;
    WCHAR *value = NULL;
    UINT32 len = 0;

    if (!stream) return;
    if (FAILED(IMFByteStream_QueryInterface(stream, &IID_IMFAttributes, (void **)&attrs)))
    {
        logf_("  byte stream exposes no IMFAttributes");
        return;
    }
    if (SUCCEEDED(IMFAttributes_GetAllocatedString(attrs, &MF_BYTESTREAM_ORIGIN_NAME, &value, &len)))
    {
        log_wstr("origin name", value);
        CoTaskMemFree(value);
    }
    else logf_("  origin name: NOT SET");

    if (SUCCEEDED(IMFAttributes_GetAllocatedString(attrs, &MF_BYTESTREAM_CONTENT_TYPE, &value, &len)))
    {
        log_wstr("content type", value);
        CoTaskMemFree(value);
    }
    else logf_("  content type: NOT SET");

    IMFAttributes_Release(attrs);
}

/* ---------------------------------------------------------------- hooks --- */

static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);
static HRESULT (WINAPI *real_MFStartup)(ULONG, DWORD);
static HRESULT (WINAPI *real_MFShutdown)(void);
static HRESULT (WINAPI *real_MFCreateDXGIDeviceManager)(UINT *, void **);
static HRESULT (WINAPI *real_MFCreateAttributes)(IMFAttributes **, UINT32);
static HRESULT (WINAPI *real_MFCreateFile)(MF_FILE_ACCESSMODE, MF_FILE_OPENMODE,
                                           MF_FILE_FLAGS, LPCWSTR, IMFByteStream **);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromByteStream)(IMFByteStream *,
                                                                 IMFAttributes *,
                                                                 IMFSourceReader **);
static HRESULT (WINAPI *real_MFTEnumEx)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO *,
                                        const MFT_REGISTER_TYPE_INFO *,
                                        IMFActivate ***, UINT32 *);

/*
 * A game can reach Media Foundation without going through its import table.
 * If it does, the hooks below never fire and the log looks like "nothing
 * happened" when the truth is "we were not watching". This says which is which.
 */
static FARPROC WINAPI my_GetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC proc = real_GetProcAddress(module, name);

    if ((ULONG_PTR)name > 0xFFFF &&
        (name[0] == 'M' && name[1] == 'F'))
    {
        char path[MAX_PATH] = "?";
        GetModuleFileNameA(module, path, sizeof(path) - 1);
        logf_("GetProcAddress(%s) from %s -> %s", name, path, proc ? "ok" : "NOT FOUND");
    }
    return proc;
}

static HRESULT WINAPI my_MFStartup(ULONG version, DWORD flags)
{
    HRESULT hr = real_MFStartup(version, flags);
    logf_("MFStartup(version=0x%lx, flags=%lu)", (unsigned long)version, (unsigned long)flags);
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI my_MFShutdown(void)
{
    logf_("MFShutdown");
    return real_MFShutdown();
}

static HRESULT WINAPI my_MFCreateDXGIDeviceManager(UINT *token, void **manager)
{
    HRESULT hr = real_MFCreateDXGIDeviceManager(token, manager);
    logf_("MFCreateDXGIDeviceManager");
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI my_MFCreateAttributes(IMFAttributes **attrs, UINT32 size)
{
    HRESULT hr = real_MFCreateAttributes(attrs, size);
    logf_("MFCreateAttributes(size=%u)", size);
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI my_MFCreateFile(MF_FILE_ACCESSMODE access, MF_FILE_OPENMODE open,
                                      MF_FILE_FLAGS flags, LPCWSTR url,
                                      IMFByteStream **stream)
{
    HRESULT hr = real_MFCreateFile(access, open, flags, url, stream);
    logf_("MFCreateFile");
    log_wstr("url", url);
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(IMFByteStream *stream,
                                                            IMFAttributes *attrs,
                                                            IMFSourceReader **reader)
{
    HRESULT hr;
    UINT32 count = 0;

    logf_("MFCreateSourceReaderFromByteStream");
    log_bytestream_tags(stream);

    if (attrs)
    {
        IUnknown *unk = NULL;
        UINT32 value = 0;
        IMFAttributes_GetCount(attrs, &count);
        logf_("  attributes: %u", count);
        if (SUCCEEDED(IMFAttributes_GetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER,
                                               &IID_IUnknown, (void **)&unk)) && unk)
        {
            logf_("  MF_SOURCE_READER_D3D_MANAGER: SET  <- asking for D3D-backed decode");
            IUnknown_Release(unk);
        }
        if (SUCCEEDED(IMFAttributes_GetUINT32(attrs, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, &value)))
            logf_("  MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS: %u", value);
        if (SUCCEEDED(IMFAttributes_GetUINT32(attrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, &value)))
            logf_("  MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING: %u", value);
    }
    else logf_("  attributes: (none)");

    hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI my_MFTEnumEx(GUID category, UINT32 flags,
                                   const MFT_REGISTER_TYPE_INFO *input,
                                   const MFT_REGISTER_TYPE_INFO *output,
                                   IMFActivate ***activate, UINT32 *count)
{
    HRESULT hr = real_MFTEnumEx(category, flags, input, output, activate, count);
    logf_("MFTEnumEx");
    describe_subtype("input ", input ? &input->guidSubtype : NULL);
    describe_subtype("output", output ? &output->guidSubtype : NULL);
    logf_("  found: %u", (count && SUCCEEDED(hr)) ? *count : 0);
    log_hr("result", hr);
    return hr;
}

/* ------------------------------------------------------------ iat patch --- */

/*
 * Replace one entry in the main module's import address table.
 *
 * The thunk arrays come in pairs: OriginalFirstThunk still holds the names the
 * linker recorded, FirstThunk holds the addresses the loader filled in. We look
 * the name up in the first and write the second.
 */
static void *hook_import(const char *dll, const char *func, void *replacement)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *desc;
    DWORD rva;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

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

#define HOOK(dll, name)                                                       \
    do {                                                                      \
        real_##name = hook_import(dll, #name, (void *)my_##name);             \
        logf_("  %-40s %s", #name, real_##name ? "hooked" : "not imported");  \
    } while (0)

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    logf_("");
    logf_("=== mf-probe attached ===");
    HOOK("KERNEL32.dll", GetProcAddress);
    HOOK("MFPlat.DLL", MFStartup);
    HOOK("MFPlat.DLL", MFShutdown);
    HOOK("MFPlat.DLL", MFCreateAttributes);
    HOOK("MFPlat.DLL", MFCreateDXGIDeviceManager);
    HOOK("MFPlat.DLL", MFCreateFile);
    HOOK("MFPlat.DLL", MFTEnumEx);
    HOOK("MFReadWrite.dll", MFCreateSourceReaderFromByteStream);
    logf_("");
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
