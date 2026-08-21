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

static void log_hr(const char *what, HRESULT hr);

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

static HRESULT (WINAPI *real_D3D11CreateDevice)(void *, UINT, HMODULE, UINT, const UINT *,
                                                UINT, UINT, void **, UINT *, void **);
static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);
static HRESULT (WINAPI *real_CoCreateInstance)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
static HANDLE (WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                         DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *real_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static HANDLE (WINAPI *real_FindFirstFileExW)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID,
                                              FINDEX_SEARCH_OPS, LPVOID, DWORD);

static LONG open_failures;      /* capped, so one missing file cannot flood the log */
static LONG mf_shutdowns;

/* The game builds movie paths from "DATA:" + "FILE/MOVIE" + "%s/%s/%s" +
 * ".webm", so match on the folder as well as the extension -- a lookup that
 * never reaches the filename still tells us it tried. */
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static BOOL is_movie_path(const WCHAR *p)
{
    static const WCHAR *needles[] = { L"MOVIE", L"movie", L"Movie", L"webm", L"WEBM" };
    unsigned i;
    if (!p) return FALSE;
    for (i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i)
        if (wcsstr(p, needles[i])) return TRUE;
    return FALSE;
}
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
 * The game creates a second D3D11 device purely for video, then asks it for
 * ID3D11VideoDevice and its context for ID3D11VideoContext. Disassembly shows
 * the whole video subsystem gives up if either fails -- which matches what we
 * see: it never opens a movie, and retries once a frame forever.
 *
 * So ask the same two questions on the same objects and log the answers. This
 * only queries interfaces; the game's own calls are untouched.
 */
static const GUID iid_video_device  = { 0x10ec4d5b, 0x975a, 0x4689,
                                        { 0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33 } };
static const GUID iid_video_context = { 0x61f21c45, 0x3c0e, 0x4a74,
                                        { 0x9c, 0xea, 0x67, 0x10, 0x0d, 0x9a, 0xd5, 0xe4 } };

static void probe_interface(const char *label, IUnknown *obj, const GUID *iid)
{
    IUnknown *out = NULL;
    HRESULT hr;

    if (!obj) { logf_("  %s: nothing to ask", label); return; }
    hr = IUnknown_QueryInterface(obj, iid, (void **)&out);
    if (SUCCEEDED(hr) && out)
    {
        logf_("  %s: AVAILABLE", label);
        IUnknown_Release(out);
    }
    else
    {
        logf_("  %s: NOT AVAILABLE", label);
        log_hr("    QueryInterface", hr);
    }
}

/* ------------------------------------------------------- video stubs --- */

/*
 * The game refuses to start its video player unless the D3D11 device hands it
 * ID3D11VideoDevice and its context ID3D11VideoContext. Under D3DMetal both
 * come back E_NOINTERFACE, so the player gives up before opening a file.
 *
 * Answering those two queries with stubs does not implement video. It answers
 * the question that decides whether implementing it is worth doing: which of
 * the eighty-odd methods does the game actually call once it gets past the
 * gate? Every stub logs its own name and refuses.
 *
 * One vtable slot is replaced -- QueryInterface -- rather than proxying the
 * whole device. Routing an entire D3D interface through this module is what
 * broke rendering when it was tried on Mortal Shell 2.
 */

/* Literal pointers, so a per-frame call does not write the log a thousand times. */
static const char *seen_stubs[128];
static LONG seen_count;

static void stub_called(const char *what)
{
    LONG i, n = seen_count;
    for (i = 0; i < n && i < (LONG)ARRAY_COUNT(seen_stubs); ++i)
        if (seen_stubs[i] == what) return;
    if (n < (LONG)ARRAY_COUNT(seen_stubs))
    {
        seen_stubs[n] = what;
        seen_count = n + 1;
        logf_("STUB  %s", what);
    }
}

struct stub_object
{
    void **vtbl;
    LONG refcount;
};

static struct stub_object stub_video_device;
static struct stub_object stub_video_context;

static HRESULT WINAPI stub_QueryInterface(void *self, REFIID iid, void **out)
{
    struct stub_object *obj = self;
    if (IsEqualGUID(iid, &IID_IUnknown)
        || (obj == &stub_video_device  && IsEqualGUID(iid, &iid_video_device))
        || (obj == &stub_video_context && IsEqualGUID(iid, &iid_video_context)))
    {
        InterlockedIncrement(&obj->refcount);
        *out = obj;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stub_AddRef(void *self)
{
    return InterlockedIncrement(&((struct stub_object *)self)->refcount);
}

static ULONG WINAPI stub_Release(void *self)
{
    LONG n = InterlockedDecrement(&((struct stub_object *)self)->refcount);
    return n < 0 ? 0 : n;      /* static objects: never actually freed */
}

#include "video-stubs.h"

/* Slot 0 of the real objects, saved so everything else still works. */
static HRESULT (WINAPI *real_device_qi)(void *, REFIID, void **);
static HRESULT (WINAPI *real_context_qi)(void *, REFIID, void **);

static HRESULT WINAPI device_qi(void *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &iid_video_device))
    {
        stub_called("ID3D11Device::QueryInterface(ID3D11VideoDevice) -> stub");
        InterlockedIncrement(&stub_video_device.refcount);
        *out = &stub_video_device;
        return S_OK;
    }
    return real_device_qi(self, iid, out);
}

static HRESULT WINAPI context_qi(void *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &iid_video_context))
    {
        stub_called("ID3D11DeviceContext::QueryInterface(ID3D11VideoContext) -> stub");
        InterlockedIncrement(&stub_video_context.refcount);
        *out = &stub_video_context;
        return S_OK;
    }
    return real_context_qi(self, iid, out);
}

/* Replace one entry in an object's vtable, returning what was there. */
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

/*
 * The reader accepts NV12 and then the game never asks for a frame, so it
 * gives up somewhere in between. The obvious suspect is the texture it would
 * decode into: D3DMetal is known not to handle NV12 video textures -- that is
 * what winevideo's mfplat patch works around for Electra.
 *
 * Slot 5 of ID3D11Device is CreateTexture2D. Log what it is asked for and
 * whether it succeeds. DXGI_FORMAT_NV12 is 103, P010 is 104.
 */
static HRESULT (WINAPI *real_create_texture2d)(void *, const void *, const void *, void **);

static HRESULT WINAPI device_create_texture2d(void *self, const void *desc,
                                              const void *initial, void **texture)
{
    HRESULT hr = real_create_texture2d(self, desc, initial, texture);
    /* D3D11_TEXTURE2D_DESC: width, height, mips, array, then format. */
    UINT format = desc ? ((const UINT *)desc)[4] : 0;

    if (FAILED(hr) || format == 103 || format == 104)
    {
        const char *name = format == 103 ? "NV12" : format == 104 ? "P010" : "";
        logf_("ID3D11Device::CreateTexture2D(format=%u %s) %s",
              format, name, FAILED(hr) ? "FAILED" : "ok");
        if (FAILED(hr)) log_hr("  result", hr);
    }
    return hr;
}

static void install_video_stubs(void *device, void *context)
{
    stub_video_device.vtbl = vd_vtbl;
    stub_video_context.vtbl = vc_vtbl;

    if (device && !real_device_qi)
    {
        real_device_qi = patch_vtable_slot(device, 0, device_qi);
        logf_("  video device stub: %s", real_device_qi ? "installed" : "COULD NOT PATCH");
        real_create_texture2d = patch_vtable_slot(device, 5, device_create_texture2d);
        logf_("  CreateTexture2D watch: %s", real_create_texture2d ? "installed" : "COULD NOT PATCH");
    }
    if (context && !real_context_qi)
    {
        real_context_qi = patch_vtable_slot(context, 0, context_qi);
        logf_("  video context stub: %s", real_context_qi ? "installed" : "COULD NOT PATCH");
    }
}

static HRESULT WINAPI my_D3D11CreateDevice(void *adapter, UINT driver_type, HMODULE software,
                                           UINT flags, const UINT *levels, UINT num_levels,
                                           UINT sdk, void **device, UINT *level, void **context)
{
    HRESULT hr = real_D3D11CreateDevice(adapter, driver_type, software, flags, levels,
                                        num_levels, sdk, device, level, context);
    logf_("D3D11CreateDevice(driver_type=%u, flags=0x%x)", driver_type, flags);
    log_hr("result", hr);
    if (SUCCEEDED(hr))
    {
        if (level) logf_("  feature level: 0x%x", *level);
        probe_interface("ID3D11VideoDevice", device ? *(IUnknown **)device : NULL,
                        &iid_video_device);
        probe_interface("ID3D11VideoContext", context ? *(IUnknown **)context : NULL,
                        &iid_video_context);
        install_video_stubs(device ? *(void **)device : NULL,
                            context ? *(void **)context : NULL);
    }
    return hr;
}

static void log_guid(const char *label, const GUID *g)
{
    logf_("  %s: {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", label,
          (unsigned long)g->Data1, g->Data2, g->Data3,
          g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
          g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/*
 * Between MFStartup and MFShutdown the game called nothing we were watching,
 * so whatever fails is not a Media Foundation call. That leaves asking COM for
 * a class -- a decoder requested by its exact CLSID, which Wine may not have
 * registered -- or simply failing to open the file. Watch both.
 *
 * Only failures are logged: a game asks COM for plenty of things that work.
 */
static HRESULT WINAPI my_CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD context,
                                          REFIID iid, void **out)
{
    HRESULT hr = real_CoCreateInstance(clsid, outer, context, iid, out);
    if (FAILED(hr))
    {
        logf_("CoCreateInstance FAILED");
        log_guid("clsid", clsid);
        log_hr("result", hr);
    }
    return hr;
}

static HANDLE WINAPI my_CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                                    LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                                    DWORD flags, HANDLE template_file)
{
    HANDLE h = real_CreateFileW(name, access, share, sa, disposition, flags, template_file);

    if (h == INVALID_HANDLE_VALUE)
    {
        /* Every failure, up to a limit: whatever the video system cannot find
         * may not be the movie itself. */
        DWORD err = GetLastError();
        if (InterlockedIncrement(&open_failures) <= 60)
        {
            logf_("CreateFileW FAILED (error %lu)", (unsigned long)err);
            log_wstr("path", name);
        }
        SetLastError(err);
    }
    else if (is_movie_path(name))
    {
        logf_("CreateFileW ok");
        log_wstr("path", name);
    }
    return h;
}

static HANDLE WINAPI my_FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
    HANDLE h = real_FindFirstFileW(name, data);
    if (is_movie_path(name))
    {
        DWORD err = GetLastError();
        logf_("FindFirstFileW %s", h == INVALID_HANDLE_VALUE ? "NOT FOUND" : "found");
        log_wstr("path", name);
        SetLastError(err);
    }
    return h;
}

static HANDLE WINAPI my_FindFirstFileExW(LPCWSTR name, FINDEX_INFO_LEVELS level,
                                         LPVOID data, FINDEX_SEARCH_OPS op,
                                         LPVOID filter, DWORD flags)
{
    HANDLE h = real_FindFirstFileExW(name, level, data, op, filter, flags);
    if (is_movie_path(name))
    {
        DWORD err = GetLastError();
        logf_("FindFirstFileExW %s", h == INVALID_HANDLE_VALUE ? "NOT FOUND" : "found");
        log_wstr("path", name);
        SetLastError(err);
    }
    return h;
}

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

/* The last run showed MFStartup every 16-17 ms -- once a frame, always S_OK.
 * That is a per-frame tick, not a retry, and 2200 copies of it drown the log.
 * Report the first few and then only the count. */
static LONG mf_startups;

/*
 * Where the call came from.
 *
 * The game gives up between MFStartup and MFShutdown without calling anything
 * we can hook: no file, no COM, no other Media Foundation function. Guessing
 * which API to watch next costs a launch each time. The return address costs
 * nothing and says exactly which function to disassemble.
 */
static void log_caller(const char *what, void *ret)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    if (ret && (BYTE *)ret > base)
        logf_("  %s called from +0x%llx", what, (unsigned long long)((BYTE *)ret - base));
    else
        logf_("  %s called from %p (outside the exe)", what, ret);
}

static HRESULT WINAPI my_MFStartup(ULONG version, DWORD flags)
{
    HRESULT hr = real_MFStartup(version, flags);
    LONG n = InterlockedIncrement(&mf_startups);
    if (n <= 6 || FAILED(hr))
    {
        logf_("MFStartup(version=0x%lx, flags=%lu)  [#%ld]",
              (unsigned long)version, (unsigned long)flags, n);
        log_caller("MFStartup", __builtin_return_address(0));
        log_hr("result", hr);
    }
    else if (n == 7)
        logf_("MFStartup ... (further successful calls counted, not logged)");
    return hr;
}

static HRESULT WINAPI my_MFShutdown(void)
{
    LONG n = InterlockedIncrement(&mf_shutdowns);
    if (n <= 6)
    {
        logf_("MFShutdown  [#%ld]", n);
        log_caller("MFShutdown", __builtin_return_address(0));
    }
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

/*
 * Watch the reader itself. Creating it succeeding tells us nothing about
 * whether frames come out -- and the retry loop says they do not.
 *
 * IMFSourceReader slot 7 is SetCurrentMediaType, slot 9 is ReadSample. Two
 * slots, not a proxy, for the same reason as the device.
 */
static HRESULT (WINAPI *real_set_media_type)(void *, DWORD, DWORD *, IMFMediaType *);
static HRESULT (WINAPI *real_read_sample)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                          LONGLONG *, IMFSample **);
static LONG read_samples, read_failures;

static HRESULT WINAPI reader_set_media_type(void *self, DWORD stream, DWORD *reserved,
                                            IMFMediaType *type)
{
    HRESULT hr = real_set_media_type(self, stream, reserved, type);
    GUID subtype;

    logf_("IMFSourceReader::SetCurrentMediaType(stream=%lu)", (unsigned long)stream);
    if (type && SUCCEEDED(IMFAttributes_GetGUID((IMFAttributes *)type, &MF_MT_SUBTYPE, &subtype)))
        describe_subtype("asked for", &subtype);
    log_hr("result", hr);
    return hr;
}

static HRESULT WINAPI reader_read_sample(void *self, DWORD stream, DWORD flags,
                                         DWORD *actual, DWORD *sample_flags,
                                         LONGLONG *timestamp, IMFSample **sample)
{
    HRESULT hr = real_read_sample(self, stream, flags, actual, sample_flags, timestamp, sample);
    LONG n = InterlockedIncrement(&read_samples);

    if (FAILED(hr))
    {
        if (InterlockedIncrement(&read_failures) <= 8)
        {
            logf_("IMFSourceReader::ReadSample  [#%ld]", n);
            log_hr("result", hr);
        }
    }
    else if (n <= 3)
    {
        logf_("IMFSourceReader::ReadSample  [#%ld] ok, sample %s", n,
              (sample && *sample) ? "delivered" : "NULL");
        if (sample_flags) logf_("  flags: 0x%lx", (unsigned long)*sample_flags);
    }
    return hr;
}

/*
 * Everything the game asks the reader succeeds, and then it stops -- right
 * after reading the native media type. So the answer is inside that object:
 * some attribute it needs that winegstreamer does not set.
 *
 * Rather than guess which, list them all. A name for the ones worth
 * recognising, the raw GUID for anything else.
 */
static const struct { const char *name; GUID guid; } media_type_keys[] =
{
 {"MF_MT_MAJOR_TYPE",        {0x48eba18e,0xf8c9,0x4687,{0xbf,0x11,0x0a,0x74,0xc9,0xf9,0x6a,0x8f}}},
 {"MF_MT_SUBTYPE",           {0xf7e34c9a,0x42e8,0x4714,{0xb7,0x4b,0xcb,0x29,0xd7,0x2c,0x35,0xe5}}},
 {"MF_MT_FRAME_SIZE",        {0x1652c33d,0xd6b2,0x4012,{0xb8,0x34,0x72,0x03,0x08,0x49,0xa3,0x7d}}},
 {"MF_MT_FRAME_RATE",        {0xc459a2e8,0x3d2c,0x4e44,{0xb1,0x32,0xfe,0xe5,0x15,0x6c,0x7b,0xb0}}},
 {"MF_MT_PIXEL_ASPECT_RATIO",{0xc6376a1e,0x8d0a,0x4027,{0xbe,0x45,0x6d,0x9a,0x0a,0xd3,0x9b,0xb6}}},
 {"MF_MT_INTERLACE_MODE",    {0xe2724bb8,0xe676,0x4806,{0xb4,0xb2,0xa8,0xd6,0xef,0xb4,0x4c,0xcd}}},
 {"MF_MT_DEFAULT_STRIDE",    {0x644b4e48,0x1e02,0x4516,{0xb0,0xeb,0xc0,0x1c,0xa9,0xd4,0x9a,0xc6}}},
 {"MF_MT_AVG_BITRATE",       {0x20332624,0xfb0d,0x4d9e,{0xbd,0x0d,0xcb,0xf6,0x78,0x6c,0x10,0x2e}}},
 {"MF_MT_ALL_SAMPLES_INDEPENDENT",{0xc9173739,0x5e56,0x461c,{0xb7,0x13,0x46,0xfb,0x99,0x5c,0xb9,0x5f}}},
 {"MF_MT_FIXED_SIZE_SAMPLES",{0xb8ebefaf,0xb718,0x4e04,{0xb0,0xa9,0x11,0x67,0x75,0xe3,0x32,0x1b}}},
 {"MF_MT_SAMPLE_SIZE",       {0xdad3ab78,0x1990,0x408b,{0xbc,0xe2,0xeb,0xa6,0x73,0xda,0xcc,0x10}}},
 {"MF_MT_COMPRESSED",        {0x3afd0cee,0x18f2,0x4ba5,{0xa1,0x10,0x8b,0xea,0x50,0x2e,0x1f,0x92}}},
 {"MF_MT_VIDEO_NOMINAL_RANGE",{0xc21b8ee5,0xb956,0x4071,{0x8d,0xaf,0x32,0x5e,0xdf,0x5c,0xab,0x11}}},
 {"MF_MT_YUV_MATRIX",        {0x3e23d650,0xc083,0x4ea4,{0xaa,0x2f,0x38,0x72,0xc0,0xa1,0xe9,0xc1}}},
 {"MF_MT_VIDEO_PRIMARIES",   {0xdbfbe4d7,0x0740,0x4ee0,{0x81,0x92,0x85,0x0a,0xb0,0xe2,0x19,0x35}}},
 {"MF_MT_TRANSFER_FUNCTION", {0x5fb0fce9,0xbe5c,0x4935,{0xa8,0x11,0xec,0x83,0x8f,0x8e,0xed,0x93}}},
 {"MF_MT_VIDEO_ROTATION",    {0xc380465d,0x2271,0x428c,{0x9b,0x83,0xec,0xea,0x3b,0x4a,0x85,0xc1}}},
};

static void dump_media_type(const char *label, IMFMediaType *type)
{
    IMFAttributes *attrs = (IMFAttributes *)type;
    UINT32 count = 0, i;

    if (!type) { logf_("  %s: (null)", label); return; }
    if (FAILED(IMFAttributes_GetCount(attrs, &count)))
    {
        logf_("  %s: cannot be enumerated", label);
        return;
    }
    logf_("  %s: %u attributes", label, count);

    for (i = 0; i < count; ++i)
    {
        PROPVARIANT value;
        GUID key;
        const char *name = NULL;
        unsigned k;

        PropVariantInit(&value);
        if (FAILED(IMFAttributes_GetItemByIndex(attrs, i, &key, &value))) continue;

        for (k = 0; k < ARRAY_COUNT(media_type_keys); ++k)
            if (IsEqualGUID(&key, &media_type_keys[k].guid)) { name = media_type_keys[k].name; break; }

        if (!name) { log_guid("    (unnamed)", &key); }
        else switch (value.vt)
        {
        case VT_UI4:
            logf_("    %-28s %lu", name, (unsigned long)value.ulVal);
            break;
        case VT_UI8:
            /* The paired 32-bit fields -- size, rate, aspect -- read as high x low. */
            logf_("    %-28s %lu / %lu", name,
                  (unsigned long)(value.uhVal.QuadPart >> 32),
                  (unsigned long)(value.uhVal.QuadPart & 0xffffffff));
            break;
        case VT_CLSID:
            describe_subtype(name, value.puuid);
            break;
        default:
            logf_("    %-28s (type %u)", name, value.vt);
            break;
        }
        PropVariantClear(&value);
    }
}

/* Between SetCurrentMediaType and the first ReadSample the game reads the
 * negotiated type back and picks streams. Watch those too, so an empty gap
 * means the gap is elsewhere. */
static HRESULT (WINAPI *real_set_stream_selection)(void *, DWORD, BOOL);
static HRESULT (WINAPI *real_get_native_type)(void *, DWORD, DWORD, IMFMediaType **);
static HRESULT (WINAPI *real_get_current_type)(void *, DWORD, IMFMediaType **);
static LONG reader_chatter;

static HRESULT WINAPI reader_set_stream_selection(void *self, DWORD stream, BOOL selected)
{
    HRESULT hr = real_set_stream_selection(self, stream, selected);
    if (InterlockedIncrement(&reader_chatter) <= 12)
    {
        logf_("IMFSourceReader::SetStreamSelection(stream=0x%lx, selected=%d)",
              (unsigned long)stream, selected);
        log_hr("  result", hr);
    }
    return hr;
}

static HRESULT WINAPI reader_get_native_type(void *self, DWORD stream, DWORD index,
                                             IMFMediaType **type)
{
    HRESULT hr = real_get_native_type(self, stream, index, type);
    if (InterlockedIncrement(&reader_chatter) <= 12)
    {
        logf_("IMFSourceReader::GetNativeMediaType(stream=%lu, index=%lu)",
              (unsigned long)stream, (unsigned long)index);
        log_hr("  result", hr);
        if (SUCCEEDED(hr) && type) dump_media_type("native type", *type);
    }
    return hr;
}

static HRESULT WINAPI reader_get_current_type(void *self, DWORD stream, IMFMediaType **type)
{
    HRESULT hr = real_get_current_type(self, stream, type);
    if (InterlockedIncrement(&reader_chatter) <= 12)
    {
        GUID subtype;
        UINT64 size = 0;
        logf_("IMFSourceReader::GetCurrentMediaType(stream=%lu)", (unsigned long)stream);
        if (SUCCEEDED(hr) && type && *type)
        {
            if (SUCCEEDED(IMFAttributes_GetGUID((IMFAttributes *)*type, &MF_MT_SUBTYPE, &subtype)))
                describe_subtype("  subtype", &subtype);
            if (SUCCEEDED(IMFAttributes_GetUINT64((IMFAttributes *)*type, &MF_MT_FRAME_SIZE, &size)))
                logf_("  frame size: %lux%lu",
                      (unsigned long)(size >> 32), (unsigned long)(size & 0xffffffff));
            else
                logf_("  frame size: NOT SET  <- a decoder that reports no size is unusable");
            dump_media_type("current type", *type);
        }
        log_hr("  result", hr);
    }
    return hr;
}

static void hook_source_reader(void *reader)
{
    if (!reader || real_read_sample) return;
    real_set_stream_selection = patch_vtable_slot(reader, 4, reader_set_stream_selection);
    real_get_native_type      = patch_vtable_slot(reader, 5, reader_get_native_type);
    real_get_current_type     = patch_vtable_slot(reader, 6, reader_get_current_type);
    real_set_media_type       = patch_vtable_slot(reader, 7, reader_set_media_type);
    real_read_sample          = patch_vtable_slot(reader, 9, reader_read_sample);
    logf_("  source reader hooks: %s", real_read_sample ? "installed" : "COULD NOT PATCH");
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

    /*
     * Hand the reader a copy of the attributes without the two that ask for
     * D3D-backed decoding.
     *
     * The game sets MF_SOURCE_READER_D3D_MANAGER and
     * MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, which tell Media Foundation to
     * decode into D3D video textures. Nothing under D3DMetal can produce
     * those -- that is the whole reason ID3D11VideoDevice was missing. Without
     * them the reader decodes in software, which is exactly the path
     * winegstreamer's VP9 support already serves.
     *
     * The game's own attribute store is left alone; it may be reused.
     */
    if (attrs && real_MFCreateAttributes)
    {
        IMFAttributes *plain = NULL;
        if (SUCCEEDED(real_MFCreateAttributes(&plain, count + 2)) && plain)
        {
            if (SUCCEEDED(IMFAttributes_CopyAllItems(attrs, plain)))
            {
                IMFAttributes_DeleteItem(plain, &MF_SOURCE_READER_D3D_MANAGER);
                IMFAttributes_DeleteItem(plain, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS);
                stub_called("dropped D3D_MANAGER and HARDWARE_TRANSFORMS -> software decode");
                hr = real_MFCreateSourceReaderFromByteStream(stream, plain, reader);
                IMFAttributes_Release(plain);
                log_hr("result (software)", hr);
                if (SUCCEEDED(hr) && reader) hook_source_reader(*reader);
                return hr;
            }
            IMFAttributes_Release(plain);
        }
    }

    hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
    log_hr("result", hr);
    if (SUCCEEDED(hr) && reader) hook_source_reader(*reader);
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
    HOOK("d3d11.dll", D3D11CreateDevice);
    HOOK("KERNEL32.dll", GetProcAddress);
    HOOK("KERNEL32.dll", CreateFileW);
    HOOK("KERNEL32.dll", FindFirstFileW);
    HOOK("KERNEL32.dll", FindFirstFileExW);
    HOOK("ole32.dll", CoCreateInstance);
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

/* Written on the way out, so the counts land even though the per-frame calls
 * are not logged individually. */
static void report_totals(void)
{
    logf_("");
    logf_("=== totals: %ld MFStartup, %ld MFShutdown, %ld failed file opens, "
          "%ld ReadSample (%ld failed) ===",
          mf_startups, mf_shutdowns, open_failures, read_samples, read_failures);
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
    else if (reason == DLL_PROCESS_DETACH && !reserved)
        report_totals();
    return TRUE;
}
