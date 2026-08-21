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
#include <d3d11.h>
#include <mferror.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

static void *vd_vtbl[], *vc_vtbl[], *vpe_vtbl[], *vp_vtbl[], *vpiv_vtbl[], *vpov_vtbl[];

static struct stub_object stub_video_device;
static struct stub_object stub_video_context;
static struct stub_object stub_vp_enumerator;
static struct stub_object stub_vp_processor;
static struct stub_object stub_vp_input_view;
static struct stub_object stub_vp_output_view;
static struct stub_object stub_dxgi_buffer;

static const char *dxgi_format_name(UINT f);
static void *patch_vtable_slot(void *object, unsigned slot, void *replacement);
static void upload_frame(IMFSample *sample);

/* The frame we put in front of the game, and the device it belongs to. */
static ID3D11Device *video_device;
static ID3D11DeviceContext *video_context;
static ID3D11Texture2D *frame_texture;
static ID3D11Texture2D *game_shared_texture;
static UINT shared_width, shared_height;
static HRESULT (WINAPI *real_texture_qi)(void *, REFIID, void **);
static HRESULT WINAPI texture_qi(void *self, REFIID iid, void **out);
static UINT frame_width, frame_height, frame_stride;
static UINT texture_width, texture_height;   /* what frame_texture actually is */
static const BOOL probe_colour = TRUE;       /* diagnostic build: see upload_frame */
static BYTE *frame_scratch;
static CRITICAL_SECTION frame_lock;
static LONG frames_uploaded;
static void *dxgibuf_vtbl[];

/* Filled in once the MFPlat imports are wrapped; index 1 is MFCreateMediaType,
 * which reader_set_media_type needs to build its RGB32 request. */
static void *real_mf[9];
#define real_MFCreateMediaType ((HRESULT (WINAPI *)(IMFMediaType **))real_mf[1])

static HRESULT WINAPI stub_QueryInterface(void *self, REFIID iid, void **out)
{
    struct stub_object *obj = self;
    if (IsEqualGUID(iid, &IID_IUnknown)
        || (obj == &stub_video_device  && IsEqualGUID(iid, &iid_video_device))
        || (obj == &stub_video_context && IsEqualGUID(iid, &iid_video_context))
        || obj == &stub_vp_enumerator || obj == &stub_vp_processor
        || obj == &stub_vp_input_view || obj == &stub_vp_output_view
        || obj == &stub_dxgi_buffer)
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

/*
 * A real enough video processor to get the game to the blit.
 *
 * With the abort branches defeated the player runs: it decodes, delivers
 * samples, and plays audio. What it cannot do is put a picture on screen,
 * because converting the decoded NV12 into the BGRA texture it draws is the
 * video processor's job, and refusing to create one leaves it with nothing to
 * convert with.
 *
 * So answer those calls. The objects below carry only what their getters have
 * to give back; everything else still logs and refuses, which keeps the log
 * honest about what is actually needed.
 */
/* The descriptor the enumerator was created from, echoed back on request.
 * Using the real type rather than a byte count: guessing the size wrong here
 * writes past the caller's struct, and D3D11_VIDEO_PROCESSOR_CAPS is 36 bytes,
 * not the 44 an eleven-UINT memset would have written. */
static D3D11_VIDEO_PROCESSOR_CONTENT_DESC vp_content_desc;

/* The resources the views were created over, so GetResource can answer and
 * the blit can say what it was handed. */
static void *input_view_resource;
static void *output_view_resource;

static HRESULT WINAPI vd_CreateVideoProcessorEnumerator(void *self, const void *desc,
                                                        void **out)
{
    (void)self;
    stub_called("ID3D11VideoDevice::CreateVideoProcessorEnumerator -> ours");
    if (desc) vp_content_desc = *(const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *)desc;
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_enumerator.refcount);
    *out = &stub_vp_enumerator;
    return S_OK;
}

static HRESULT WINAPI vd_CreateVideoProcessor(void *self, void *enumerator, UINT rate,
                                              void **out)
{
    (void)self; (void)enumerator; (void)rate;
    stub_called("ID3D11VideoDevice::CreateVideoProcessor -> ours");
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_processor.refcount);
    *out = &stub_vp_processor;
    return S_OK;
}

static HRESULT WINAPI vd_CreateVideoProcessorInputView(void *self, void *resource,
                                                       void *enumerator, const void *desc,
                                                       void **out)
{
    (void)self; (void)enumerator; (void)desc;
    stub_called("ID3D11VideoDevice::CreateVideoProcessorInputView -> ours");
    input_view_resource = resource;
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_input_view.refcount);
    *out = &stub_vp_input_view;
    return S_OK;
}

static HRESULT WINAPI vd_CreateVideoProcessorOutputView(void *self, void *resource,
                                                        void *enumerator, const void *desc,
                                                        void **out)
{
    (void)self; (void)enumerator; (void)desc;
    stub_called("ID3D11VideoDevice::CreateVideoProcessorOutputView -> ours");
    output_view_resource = resource;
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_output_view.refcount);
    *out = &stub_vp_output_view;
    return S_OK;
}

/*
 * The check the game gates on. It asks about one format at a time and insists
 * on D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT, which is bit 1; report both
 * input and output so either question is answered.
 */
static HRESULT WINAPI vpe_CheckVideoProcessorFormat(void *self, UINT format, UINT *flags)
{
    (void)self;
    if (!flags) return E_INVALIDARG;
    *flags = 0x1 | 0x2;                 /* INPUT | OUTPUT */
    logf_("  CheckVideoProcessorFormat(%u %s) -> INPUT|OUTPUT",
          format, dxgi_format_name(format));
    return S_OK;
}

static HRESULT WINAPI vpe_GetVideoProcessorCaps(void *self, D3D11_VIDEO_PROCESSOR_CAPS *caps)
{
    (void)self;
    stub_called("ID3D11VideoProcessorEnumerator::GetVideoProcessorCaps");
    if (!caps) return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));
    /* No optional features, which is true. But a processor that reports no
     * rate conversions and no input streams is one a caller cannot use, so
     * claim the single one it needs. */
    caps->RateConversionCapsCount = 1;
    caps->MaxInputStreams = 1;
    caps->MaxStreamStates = 1;
    return S_OK;
}

static HRESULT WINAPI vpe_GetVideoProcessorContentDesc(void *self,
                                                      D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    (void)self;
    stub_called("ID3D11VideoProcessorEnumerator::GetVideoProcessorContentDesc");
    if (!desc) return E_INVALIDARG;
    *desc = vp_content_desc;
    return S_OK;
}

static HRESULT WINAPI vpe_GetVideoProcessorRateConversionCaps(
        void *self, UINT index, D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    (void)self; (void)index;
    stub_called("ID3D11VideoProcessorEnumerator::GetVideoProcessorRateConversionCaps");
    if (!caps) return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));   /* no past or future frames, no telecine */

    /*
     * The game walks these looking for one specific bit and gives up with
     * E_FAIL when no entry has it:
     *
     *     callq *0x50(%rax)        ; this function
     *     testb $0x2, 0x27(%rbp)   ; caps is at 0x1f(%rbp), so +8: ProcessorCaps
     *     jne   <found>
     *     ...
     *     movl  $0x80004005, %eax  ; E_FAIL
     *
     * Bit 1 of ProcessorCaps is DEINTERLACE_BOB. Claiming it is honest enough:
     * bob deinterlacing a progressive frame is a copy, which is exactly what
     * this content needs.
     */
    caps->ProcessorCaps = D3D11_VIDEO_PROCESSOR_PROCESSOR_CAPS_DEINTERLACE_BOB;
    return S_OK;
}

static HRESULT WINAPI vpiv_GetResource(void *self, void **resource)
{
    (void)self;
    if (resource) *resource = input_view_resource;
    return S_OK;
}

static HRESULT WINAPI vpov_GetResource(void *self, void **resource)
{
    (void)self;
    if (resource) *resource = output_view_resource;
    return S_OK;
}

/* Describe a texture we were handed, so the log says what the conversion is
 * actually between. ID3D11Texture2D::GetDesc is slot 10. */
/*
 * Describe a resource we were handed.
 *
 * Slot 10 of ID3D11Texture2D is GetDesc, but only for a Texture2D: a buffer
 * or a 3D texture puts something else there, and calling it would report
 * nonsense rather than say so. Slot 7, GetType, is on ID3D11Resource itself
 * and tells us which we have. The pointer is printed either way -- "(none)"
 * previously covered both a null argument and a resource this could not read,
 * which are very different things.
 */
static void describe_resource(const char *label, void *resource)
{
    void (WINAPI *get_type)(void *, UINT *);
    void (WINAPI *get_desc)(void *, UINT *);
    UINT dimension = 0;
    UINT desc[11] = { 0 };

    if (!resource) { logf_("    %s: NULL", label); return; }

    get_type = (*(void ***)resource)[7];
    get_type(resource, &dimension);
    if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        logf_("    %s: %p, dimension %u (not a 2D texture)", label, resource, dimension);
        return;
    }

    get_desc = (*(void ***)resource)[10];
    get_desc(resource, desc);
    logf_("    %s: %p %ux%u format=%u %s bind=0x%x misc=0x%x%s",
          label, resource, desc[0], desc[1], desc[4], dxgi_format_name(desc[4]),
          desc[8], desc[10],
          resource == (void *)game_shared_texture ? "  <- the shared one" : "");
}

/*
 * The conversion itself.
 *
 * This build only reports what it was given. The game never created an NV12
 * texture -- the only two it made are BGRA -- so where the decoded sample
 * actually lands is the thing to establish before writing a converter for it.
 * Returning S_OK keeps the player running so the rest of the sequence stays
 * visible.
 */
static LONG blit_calls;

static HRESULT WINAPI vc_VideoProcessorBlt(void *self, void *processor, void *output_view,
                                           UINT frame, UINT stream_count, const void *streams)
{
    (void)self; (void)processor; (void)output_view; (void)streams;

    if (InterlockedIncrement(&blit_calls) <= 3)
    {
        logf_("ID3D11VideoContext::VideoProcessorBlt(frame=%u, streams=%u)", frame, stream_count);
        describe_resource("input ", input_view_resource);
        describe_resource("output", output_view_resource);
    }

    /*
     * Both sides are BGRA of the same size, so the conversion the real
     * processor would do is already done: copy.
     */
    EnterCriticalSection(&frame_lock);
    if (frame_texture && video_context)
    {
        /* CopyResource requires identical dimensions; mismatched, it does
         * nothing at all and says nothing either. */
        if (output_view_resource)
        {
            void (WINAPI *get_desc)(void *, UINT *) = (*(void ***)output_view_resource)[10];
            UINT out[11] = { 0 };
            get_desc(output_view_resource, out);
            if (out[0] == texture_width && out[1] == texture_height)
                ID3D11DeviceContext_CopyResource(video_context,
                        (ID3D11Resource *)output_view_resource,
                        (ID3D11Resource *)frame_texture);
            else if (blit_calls <= 3)
                logf_("    sizes differ: ours %ux%u, output %ux%u -- not copying",
                      texture_width, texture_height, out[0], out[1]);
        }

        /*
         * And into the texture the D3D12 renderer actually samples. TYPELESS
         * and UNORM of the same base format are copy-compatible, and the
         * dimensions are checked rather than assumed.
         */
        if (game_shared_texture
            && shared_width == texture_width && shared_height == texture_height)
        {
            ID3D11DeviceContext_CopyResource(video_context,
                    (ID3D11Resource *)game_shared_texture, (ID3D11Resource *)frame_texture);
            if (blit_calls <= 3) logf_("    also copied into the shared texture");
        }
        else if (blit_calls <= 3 && game_shared_texture)
            logf_("    shared texture is %ux%u, ours %ux%u -- not copying",
                  shared_width, shared_height, texture_width, texture_height);
        ID3D11DeviceContext_Flush(video_context);
    }
    else if (blit_calls <= 3)
        logf_("    nothing to copy (texture %p, output %p)",
              (void *)frame_texture, output_view_resource);
    LeaveCriticalSection(&frame_lock);
    return S_OK;
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

static const char *dxgi_format_name(UINT f)
{
    switch (f)
    {
    case 87:  return "B8G8R8A8_UNORM";
    case 88:  return "B8G8R8X8_UNORM";
    case 90:  return "B8G8R8A8_TYPELESS";
    case 91:  return "B8G8R8A8_UNORM_SRGB";
    case 28:  return "R8G8B8A8_UNORM";
    case 103: return "NV12";
    case 104: return "P010";
    default:  return "";
    }
}

/*
 * Log every one of these, not only the failures.
 *
 * Helper B builds a D3D11_TEXTURE2D_DESC on the stack and calls this, and its
 * return value is what the caller tests. The earlier filter -- failures and
 * NV12 only -- meant a successful BGRA creation said nothing, and there was no
 * way to tell "succeeded" from "never called".
 *
 * The second descriptor it builds carries MiscFlags 2, D3D11_RESOURCE_MISC_SHARED:
 * a texture the D3D12 renderer can also see. That is the interesting one,
 * because sharing a resource between a D3D11 device and a D3D12 one is exactly
 * what a translation layer is least likely to support.
 */
static LONG texture_calls;

/*
 * The game makes two textures per video: one BGRA render target, which is
 * where the video processor is asked to write, and one BGRA TYPELESS carrying
 * D3D11_RESOURCE_MISC_SHARED, which is the one its D3D12 renderer opens by
 * handle and samples. Something has to move the frame from the first to the
 * second, and with a stubbed processor nothing does.
 *
 * Remember both, so the blit can fill the one that is actually read.
 */


static HRESULT WINAPI device_create_texture2d(void *self, const void *desc,
                                              const void *initial, void **texture)
{
    HRESULT hr = real_create_texture2d(self, desc, initial, texture);
    const UINT *d = desc;

    if (d && (FAILED(hr) || InterlockedIncrement(&texture_calls) <= 12))
    {
        /* width, height, mips, array, format, sample count, sample quality,
         * usage, bind flags, cpu access, misc flags */
        logf_("ID3D11Device::CreateTexture2D %ux%u format=%u %s bind=0x%x misc=0x%x  %s",
              d[0], d[1], d[4], dxgi_format_name(d[4]), d[8], d[10],
              FAILED(hr) ? "FAILED" : "ok");
        if (d[10] & 2) logf_("    MiscFlags carries D3D11_RESOURCE_MISC_SHARED");
        if (FAILED(hr)) log_hr("    result", hr);
    }

    /*
     * Hold on to the shared one, with a reference this time.
     *
     * The game copies out of the render target into a destination that is
     * NULL -- its own log line reads "dst: NULL, src: <the render target>" --
     * so the frame never reaches the texture its D3D12 renderer samples, and
     * what shows on screen is that texture's uninitialised contents. Filling
     * it is the job the real video processor would have done.
     *
     * Storing the pointer raw is what crashed the game at the menu: it builds
     * a fresh pair for every clip and releases the old ones. So AddRef what we
     * keep and Release what we drop.
     */
    if (SUCCEEDED(hr) && d && texture && *texture && d[0] > 64 && d[1] > 64 && (d[10] & 2))
    {
        ID3D11Texture2D *previous;
        EnterCriticalSection(&frame_lock);
        previous = game_shared_texture;
        game_shared_texture = (ID3D11Texture2D *)*texture;
        shared_width = d[0];
        shared_height = d[1];
        ID3D11Texture2D_AddRef(game_shared_texture);
        LeaveCriticalSection(&frame_lock);
        if (!real_texture_qi)
        {
            real_texture_qi = patch_vtable_slot(*texture, 0, texture_qi);
            logf_("    texture QueryInterface watch: %s",
                  real_texture_qi ? "installed" : "COULD NOT PATCH");
        }
        if (previous) ID3D11Texture2D_Release(previous);
        logf_("    kept as the texture the D3D12 side reads");
    }
    return hr;
}

/*
 * Does the game ever get a handle for the shared texture?
 *
 * Magenta written into both of its 2560x1440 textures does not reach the
 * screen, but the GPTK counter moved when a D3D11 copy was added, so both
 * APIs are going through the same backend and a share ought to work. The
 * remaining possibility is upstream of the copy entirely: the game asks the
 * texture for IDXGIResource and then for a shared handle, and if that fails
 * its D3D12 side never opens the texture at all -- nothing written into it
 * could ever show, and no error would appear anywhere we have been looking.
 *
 * IDXGIResource slot 8 is GetSharedHandle. Reaching it means intercepting the
 * QueryInterface that produces the IDXGIResource first.
 */
static const GUID iid_dxgi_resource = { 0x035f3ab4, 0x482e, 0x4e50,
                                        { 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b } };

static HRESULT (WINAPI *real_res_get_shared_handle)(void *, HANDLE *);

static HRESULT WINAPI res_get_shared_handle(void *self, HANDLE *handle)
{
    HRESULT hr = real_res_get_shared_handle(self, handle);
    logf_("IDXGIResource::GetSharedHandle -> %s", SUCCEEDED(hr) ? "ok" : "FAILED");
    if (SUCCEEDED(hr) && handle) logf_("    handle: %p", *handle);
    else log_hr("    result", hr);
    return hr;
}

static HRESULT WINAPI texture_qi(void *self, REFIID iid, void **out)
{
    HRESULT hr = real_texture_qi(self, iid, out);

    if (IsEqualGUID(iid, &iid_dxgi_resource))
    {
        stub_called("ID3D11Texture2D::QueryInterface(IDXGIResource)");
        if (SUCCEEDED(hr) && out && *out && !real_res_get_shared_handle)
        {
            real_res_get_shared_handle = patch_vtable_slot(*out, 8, res_get_shared_handle);
            logf_("  GetSharedHandle watch: %s",
                  real_res_get_shared_handle ? "installed" : "COULD NOT PATCH");
        }
    }
    return hr;
}

/*
 * What the game copies on its own. ID3D11DeviceContext slot 47 is
 * CopyResource and slot 46 CopySubresourceRegion; if it ever moves the frame
 * from the render target to the shared texture itself, it shows up here.
 */
static void (WINAPI *real_ctx_copy_resource)(void *, void *, void *);

static void WINAPI ctx_copy_resource(void *self, void *dst, void *src)
{
    static LONG seen;
    if (InterlockedIncrement(&seen) <= 6)
    {
        logf_("ID3D11DeviceContext::CopyResource");
        describe_resource("dst", dst);
        describe_resource("src", src);
    }
    real_ctx_copy_resource(self, dst, src);
}

/* The views built on those textures, so a failure one step later is visible
 * too. Slot 7 is CreateShaderResourceView, slot 9 CreateRenderTargetView. */
static HRESULT (WINAPI *real_create_srv)(void *, void *, const void *, void **);
static HRESULT (WINAPI *real_create_rtv)(void *, void *, const void *, void **);

static HRESULT WINAPI device_create_srv(void *self, void *res, const void *desc, void **view)
{
    HRESULT hr = real_create_srv(self, res, desc, view);
    if (FAILED(hr)) { logf_("ID3D11Device::CreateShaderResourceView FAILED"); log_hr("  result", hr); }
    return hr;
}

static HRESULT WINAPI device_create_rtv(void *self, void *res, const void *desc, void **view)
{
    HRESULT hr = real_create_rtv(self, res, desc, view);
    if (FAILED(hr)) { logf_("ID3D11Device::CreateRenderTargetView FAILED"); log_hr("  result", hr); }
    return hr;
}

/* --------------------------------------------- D3D-backed samples --- */

/*
 * Hand the game frames it can actually use.
 *
 * The executable references IMFDXGIBuffer, ID3D11Texture2D and IDXGIResource,
 * and neither IMF2DBuffer nor IMFMediaBuffer. It only knows how to present a
 * sample backed by a D3D texture: query the buffer for IMFDXGIBuffer, take the
 * texture, wrap it in a VideoProcessorInputView, blit.
 *
 * Dropping MF_SOURCE_READER_D3D_MANAGER is what made decoding work, and it is
 * also what leaves the samples in plain memory. Rather than choose between
 * them, decode in software and put each frame into a texture ourselves, then
 * answer the IMFDXGIBuffer query with it.
 *
 * The game's own two textures are BGRA, so asking the reader for RGB32 instead
 * of NV12 lets winegstreamer do the colour conversion -- in code written for
 * it -- and leaves us only an upload. A scalar NV12 converter here would be
 * 3.7 million pixels a frame.
 */
static const GUID iid_dxgi_buffer = { 0xe7174cfa, 0x1c9e, 0x48b1,
                                      { 0x88, 0x66, 0x62, 0x62, 0x26, 0xbf, 0xc2, 0x58 } };

static HRESULT WINAPI dxgibuf_GetResource(void *self, REFIID iid, void **out)
{
    HRESULT hr;
    (void)self;
    if (!out) return E_INVALIDARG;
    EnterCriticalSection(&frame_lock);
    hr = frame_texture ? ID3D11Texture2D_QueryInterface(frame_texture, iid, out)
                       : E_FAIL;
    LeaveCriticalSection(&frame_lock);
    if (FAILED(hr)) stub_called("IMFDXGIBuffer::GetResource had no texture to give");
    return hr;
}

static HRESULT WINAPI dxgibuf_GetSubresourceIndex(void *self, UINT *index)
{
    (void)self;
    if (!index) return E_INVALIDARG;
    *index = 0;                      /* one texture, one subresource */
    return S_OK;
}

static HRESULT WINAPI dxgibuf_GetUnknown(void *self, REFIID guid, REFIID iid, void **out)
{
    (void)self; (void)guid; (void)iid;
    if (out) *out = NULL;
    return MF_E_ATTRIBUTENOTFOUND;
}

static HRESULT WINAPI dxgibuf_SetUnknown(void *self, REFIID guid, IUnknown *unk)
{
    (void)self; (void)guid; (void)unk;
    return S_OK;
}

static void *dxgibuf_vtbl[] =
{
    stub_QueryInterface, stub_AddRef, stub_Release,
    dxgibuf_GetResource,
    dxgibuf_GetSubresourceIndex,
    dxgibuf_GetUnknown,
    dxgibuf_SetUnknown,
};

/* Slot 0 of the buffer the reader hands out, so a query for IMFDXGIBuffer
 * reaches us. One slot, as everywhere else in this file. */
static HRESULT (WINAPI *real_buffer_qi)(void *, REFIID, void **);

static HRESULT WINAPI buffer_qi(void *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &iid_dxgi_buffer))
    {
        stub_called("IMFMediaBuffer::QueryInterface(IMFDXGIBuffer) -> ours");
        InterlockedIncrement(&stub_dxgi_buffer.refcount);
        *out = &stub_dxgi_buffer;
        return S_OK;
    }
    return real_buffer_qi(self, iid, out);
}

/*
 * NV12 to BGRA.
 *
 * The reader refused RGB32 with MF_E_TOPO_CODEC_NOT_FOUND, so the samples
 * arrive as NV12 and the conversion has to happen here after all: a full-size
 * luma plane followed by half-resolution interleaved chroma.
 *
 * BT.709 limited range, which is what 2560x1440 content is, in integer
 * arithmetic. Coefficients are the usual ones scaled by 256.
 */
static void nv12_to_bgra(const BYTE *nv12, UINT stride, BYTE *bgra, UINT width, UINT height)
{
    const BYTE *chroma = nv12 + (size_t)stride * height;
    UINT x, y;

    for (y = 0; y < height; ++y)
    {
        const BYTE *luma_row = nv12 + (size_t)stride * y;
        const BYTE *chroma_row = chroma + (size_t)stride * (y / 2);
        BYTE *out = bgra + (size_t)width * 4 * y;

        for (x = 0; x < width; ++x)
        {
            int c = luma_row[x] - 16;
            int d = chroma_row[(x & ~1u)] - 128;
            int e = chroma_row[(x & ~1u) + 1] - 128;
            int r = (298 * c + 459 * e + 128) >> 8;
            int g = (298 * c - 55 * d - 136 * e + 128) >> 8;
            int b = (298 * c + 541 * d + 128) >> 8;

            out[0] = (BYTE)(b < 0 ? 0 : b > 255 ? 255 : b);
            out[1] = (BYTE)(g < 0 ? 0 : g > 255 ? 255 : g);
            out[2] = (BYTE)(r < 0 ? 0 : r > 255 ? 255 : r);
            out[3] = 0xff;
            out += 4;
        }
    }
}

/* Copy one decoded frame into the texture the game will be handed. */
static void upload_frame(IMFSample *sample)
{
    IMFMediaBuffer *buffer = NULL;
    BYTE *data = NULL;
    DWORD length = 0;
    HRESULT hr;
    LONG n;

    if (!sample) return;
    if (!video_device || !video_context)
    {
        stub_called("no D3D11 device to upload frames to");
        return;
    }

    hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (FAILED(hr) || !buffer)
    {
        stub_called("ConvertToContiguousBuffer failed");
        return;
    }

    if (!real_buffer_qi)
    {
        real_buffer_qi = patch_vtable_slot(buffer, 0, buffer_qi);
        logf_("  media buffer hook: %s", real_buffer_qi ? "installed" : "COULD NOT PATCH");
    }

    n = InterlockedIncrement(&frames_uploaded);
    hr = IMFMediaBuffer_Lock(buffer, &data, NULL, &length);
    if (FAILED(hr) || !data)
    {
        if (n <= 3) { logf_("  buffer Lock failed"); log_hr("    result", hr); }
        IMFMediaBuffer_Release(buffer);
        return;
    }

    EnterCriticalSection(&frame_lock);

    if (!frame_width || !frame_height)
    {
        if (n <= 3)
            logf_("  frame size unknown (%ux%u) -- nothing to upload into",
                  frame_width, frame_height);
    }
    else
    {
        /*
         * Throw away a texture built for a different clip.
         *
         * This game ships 2560x1440 cutscenes and 960x540 interface clips in
         * the same folder, so the size changes within a session. Creating the
         * texture once and keeping it means the small clip's geometry drives
         * a conversion sized for the large one -- writing megabytes past the
         * scratch buffer -- while CopyResource between mismatched sizes is a
         * silent no-op. Both were confirmed before either was seen.
         */
        if (frame_texture && (texture_width != frame_width || texture_height != frame_height))
        {
            logf_("  clip changed size: %ux%u -> %ux%u, rebuilding",
                  texture_width, texture_height, frame_width, frame_height);
            ID3D11Texture2D_Release(frame_texture);
            frame_texture = NULL;
            free(frame_scratch);
            frame_scratch = NULL;
        }

        if (!frame_texture)
        {
            D3D11_TEXTURE2D_DESC desc;
            memset(&desc, 0, sizeof(desc));
            desc.Width = frame_width;
            desc.Height = frame_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            hr = ID3D11Device_CreateTexture2D(video_device, &desc, NULL, &frame_texture);
            logf_("  frame texture %ux%u BGRA: %s", frame_width, frame_height,
                  SUCCEEDED(hr) ? "created" : "FAILED");
            if (FAILED(hr)) { log_hr("    result", hr); frame_texture = NULL; }
            else { texture_width = frame_width; texture_height = frame_height; }
        }

        if (!frame_scratch)
            frame_scratch = malloc((size_t)frame_width * frame_height * 4);

        if (frame_texture && frame_scratch)
        {
            UINT stride = frame_stride ? frame_stride : frame_width;
            DWORD needed = stride * frame_height * 3 / 2;

            if (length < needed)
            {
                if (n <= 3)
                    logf_("  sample is %lu bytes, NV12 at %ux%u stride %u needs %lu",
                          (unsigned long)length, frame_width, frame_height, stride,
                          (unsigned long)needed);
            }
            else
            {
                /*
                 * Is there a picture in here at all?
                 *
                 * A black screen has two very different causes: a frame that
                 * never reaches the display, or a frame that is genuinely
                 * black. Averaging the luma plane separates them for the cost
                 * of one pass. NV12 luma is 16 for black and 235 for white, so
                 * anything near 16 means the decoder handed us nothing.
                 */
                if (n <= 2)
                {
                    unsigned long long sum = 0;
                    UINT yy, xx, lo = 255, hi = 0;
                    for (yy = 0; yy < frame_height; yy += 8)
                        for (xx = 0; xx < frame_width; xx += 8)
                        {
                            BYTE v = data[(size_t)stride * yy + xx];
                            sum += v;
                            if (v < lo) lo = v;
                            if (v > hi) hi = v;
                        }
                    logf_("  luma: average %llu, range %u..%u  (%s)",
                          sum / (((unsigned long long)frame_height / 8) * (frame_width / 8)),
                          lo, hi, hi <= 20 ? "black -- the decoder gave us nothing"
                                          : "there is a picture here");
                }

                nv12_to_bgra(data, stride, frame_scratch, frame_width, frame_height);

                /*
                 * One question is still open and everything else depends on
                 * it: do writes made through the D3D11 device reach the
                 * texture the D3D12 renderer samples at all?
                 *
                 * The copy into the shared texture happens, the frame has real
                 * content, and the video rectangle is still flat grey -- which
                 * is what an untouched texture looks like. If D3DMetal backs
                 * its D3D11 and D3D12 sides with separate resources, the share
                 * silently produces two unrelated textures and nothing written
                 * here can ever appear.
                 *
                 * Magenta settles it. Nothing in this game is magenta.
                 */
                if (probe_colour)
                {
                    size_t px = (size_t)frame_width * frame_height;
                    BYTE *q = frame_scratch;
                    while (px--) { q[0] = 0xff; q[1] = 0x00; q[2] = 0xff; q[3] = 0xff; q += 4; }
                    if (n <= 1) logf_("  PROBE: frame replaced with solid magenta");
                }
                ID3D11DeviceContext_UpdateSubresource(video_context,
                        (ID3D11Resource *)frame_texture, 0, NULL,
                        frame_scratch, frame_width * 4, 0);
                if (n <= 3)
                    logf_("  frame %ld converted and uploaded (%lu bytes in)",
                          n, (unsigned long)length);
            }
        }
    }

    LeaveCriticalSection(&frame_lock);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
}

static void install_video_stubs(void *device, void *context)
{
    stub_video_device.vtbl    = vd_vtbl;
    stub_video_context.vtbl   = vc_vtbl;
    stub_vp_enumerator.vtbl   = vpe_vtbl;
    stub_vp_processor.vtbl    = vp_vtbl;
    stub_vp_input_view.vtbl   = vpiv_vtbl;
    stub_vp_output_view.vtbl  = vpov_vtbl;
    stub_dxgi_buffer.vtbl     = dxgibuf_vtbl;

    if (device && !real_device_qi)
    {
        real_device_qi = patch_vtable_slot(device, 0, device_qi);
        logf_("  video device stub: %s", real_device_qi ? "installed" : "COULD NOT PATCH");
        real_create_texture2d = patch_vtable_slot(device, 5, device_create_texture2d);
        real_create_srv       = patch_vtable_slot(device, 7, device_create_srv);
        real_create_rtv       = patch_vtable_slot(device, 9, device_create_rtv);
        logf_("  resource watch: %s", real_create_texture2d ? "installed" : "COULD NOT PATCH");
    }
    if (context && !real_context_qi)
    {
        real_context_qi = patch_vtable_slot(context, 0, context_qi);
        logf_("  video context stub: %s", real_context_qi ? "installed" : "COULD NOT PATCH");
        real_ctx_copy_resource = patch_vtable_slot(context, 47, ctx_copy_resource);
        logf_("  CopyResource watch: %s", real_ctx_copy_resource ? "installed" : "COULD NOT PATCH");
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
        if (!video_device && device)  video_device  = *(ID3D11Device **)device;
        if (!video_context && context) video_context = *(ID3D11DeviceContext **)context;
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
/*
 * Every Media Foundation answer is correct -- NV12 at 2560x1440, 30000/1001,
 * duration 10.01s -- and the game still tears the player down and starts
 * over. So whatever decides that is the game's own code, and the only useful
 * question left is where it lives.
 *
 * A return address gives one frame. RtlCaptureStackBackTrace gives the chain,
 * using the unwind tables, which is reliable on x64 where frame pointers are
 * not. Printed as offsets, so they go straight into llvm-objdump.
 */
static void log_stack(const char *what)
{
    void *frames[10];
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    USHORT n, i;

    n = RtlCaptureStackBackTrace(1, (ULONG)ARRAY_COUNT(frames), frames, NULL);
    logf_("  call stack for %s:", what);
    for (i = 0; i < n; ++i)
    {
        BYTE *addr = frames[i];
        if (addr > base && addr - base < 0x10000000)
            logf_("    +0x%llx", (unsigned long long)(addr - base));
        else
            logf_("    %p  (outside the exe)", addr);
    }
}

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
    HRESULT hr = E_FAIL;
    GUID subtype;
    UINT64 size = 0;

    logf_("IMFSourceReader::SetCurrentMediaType(stream=%lu)", (unsigned long)stream);
    if (type && SUCCEEDED(IMFAttributes_GetGUID((IMFAttributes *)type, &MF_MT_SUBTYPE, &subtype)))
        describe_subtype("asked for", &subtype);
    if (type && SUCCEEDED(IMFAttributes_GetUINT64((IMFAttributes *)type, &MF_MT_FRAME_SIZE, &size)))
    {
        frame_width  = (UINT)(size >> 32);
        frame_height = (UINT)(size & 0xffffffff);
    }

    /*
     * Ask for RGB32 instead. The frames have to end up in a BGRA texture
     * either way -- both of the game's own are BGRA -- and letting the reader
     * convert means winegstreamer does it rather than a scalar loop here.
     *
     * A copy of the request, never the game's own object: adding to the type
     * it passes in is what turned a working SetCurrentMediaType into
     * MF_E_TOPO_CODEC_NOT_FOUND once already.
     */
    if (type && real_MFCreateAttributes)
    {
        IMFMediaType *rgb = NULL;
        if (SUCCEEDED(real_MFCreateMediaType(&rgb)) && rgb)
        {
            if (SUCCEEDED(IMFMediaType_CopyAllItems(type, (IMFAttributes *)rgb))
                && SUCCEEDED(IMFAttributes_SetGUID((IMFAttributes *)rgb, &MF_MT_SUBTYPE,
                                                   &MFVideoFormat_RGB32)))
            {
                hr = real_set_media_type(self, stream, reserved, rgb);
                if (SUCCEEDED(hr))
                {
                    stub_called("asked the reader for RGB32 instead of NV12");
                    log_hr("result (RGB32)", hr);
                    IMFMediaType_Release(rgb);
                    return hr;
                }
                logf_("  RGB32 refused, falling back to what the game asked for");
                log_hr("    result", hr);
            }
            IMFMediaType_Release(rgb);
        }
    }

    hr = real_set_media_type(self, stream, reserved, type);
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
    if (SUCCEEDED(hr) && sample && *sample) upload_frame(*sample);
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
        if (SUCCEEDED(hr) && type && reader_chatter <= 4)
            dump_media_type("native type", *type);
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
            {
                UINT32 stride = 0;
                logf_("  frame size: %lux%lu",
                      (unsigned long)(size >> 32), (unsigned long)(size & 0xffffffff));
                /* The type the reader actually settled on is the authority on
                 * both of these, more than the one the game asked for. */
                frame_width  = (UINT)(size >> 32);
                frame_height = (UINT)(size & 0xffffffff);
                if (SUCCEEDED(IMFAttributes_GetUINT32((IMFAttributes *)*type,
                                                      &MF_MT_DEFAULT_STRIDE, &stride)))
                    frame_stride = stride > 0x7fffffff ? (UINT)(-(INT32)stride) : stride;
            }
            else
                logf_("  frame size: NOT SET  <- a decoder that reports no size is unusable");
            dump_media_type("current type", *type);
        }
        log_hr("  result", hr);
    }
    return hr;
}

/*
 * The five slots not covered above, wrapped uniformly.
 *
 * Guessing one method per launch has cost several already. IMFSourceReader
 * has thirteen entries and the interesting ones are now all watched, so the
 * last line in the log before the retry is the call the game gives up on --
 * whichever it turns out to be.
 *
 * Every wrapper forwards all seven register arguments. ReadSample is the
 * widest at seven including `this`, so nothing is truncated.
 */
typedef ULONG_PTR (WINAPI *reader_fn)(void *, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                      ULONG_PTR, ULONG_PTR, ULONG_PTR);
static reader_fn real_reader[13];
static LONG reader_calls[13];

static void log_reader_call(const char *name, unsigned slot, ULONG_PTR a, ULONG_PTR b,
                            HRESULT hr)
{
    if (InterlockedIncrement(&reader_calls[slot]) > 4 && SUCCEEDED(hr)) return;
    logf_("IMFSourceReader::%s(0x%llx, 0x%llx)", name,
          (unsigned long long)a, (unsigned long long)b);
    log_hr("  result", hr);
}

#define READER_WRAP(slot, name)                                                          static ULONG_PTR WINAPI rdr_##slot(void *self, ULONG_PTR a, ULONG_PTR b,                                                ULONG_PTR c, ULONG_PTR d, ULONG_PTR e,                                               ULONG_PTR f)                                      {                                                                                        ULONG_PTR r = real_reader[slot](self, a, b, c, d, e, f);                              log_reader_call(name, slot, a, b, (HRESULT)r);                                        return r;                                                                        }

READER_WRAP(3,  "GetStreamSelection")
READER_WRAP(8,  "SetCurrentPosition")
READER_WRAP(10, "Flush")
READER_WRAP(11, "GetServiceForStream")

/*
 * GetPresentationAttribute is the last call the game makes before tearing
 * everything down, and it succeeds. So it is not the call that fails -- it is
 * the answer.
 *
 * The attribute is MF_PD_DURATION. A player handed a duration of zero has a
 * video of no length, and skipping it is the reasonable thing to do; it would
 * look exactly like this. So log the value, and when it is zero substitute a
 * plausible one.
 *
 * The substitution is a test, not a fix. If the game starts reading frames,
 * the real repair belongs in winegstreamer, which should report the duration
 * the container already carries -- ffprobe reads 10.01s out of TITLE.webm
 * without difficulty.
 */
static const GUID mf_pd_duration = { 0x6c990d33, 0xbb8e, 0x477a,
                                     { 0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a } };
#define SUBSTITUTE_DURATION  (10ull * 60 * 10000000)   /* ten minutes in 100ns units */

static ULONG_PTR WINAPI rdr_12(void *self, ULONG_PTR stream, ULONG_PTR guid,
                               ULONG_PTR value, ULONG_PTR d, ULONG_PTR e, ULONG_PTR f)
{
    ULONG_PTR r = real_reader[12](self, stream, guid, value, d, e, f);
    HRESULT hr = (HRESULT)r;
    PROPVARIANT *pv = (PROPVARIANT *)value;
    BOOL duration = guid && IsEqualGUID((const GUID *)guid, &mf_pd_duration);

    if (InterlockedIncrement(&reader_calls[12]) <= 4 || FAILED(hr))
    {
        logf_("IMFSourceReader::GetPresentationAttribute(stream=0x%llx, %s)",
              (unsigned long long)stream, duration ? "MF_PD_DURATION" : "other");
        /* The last call before the player is torn down. Whoever makes it is
         * the function that then decides to give up, so name it. */
        if (reader_calls[12] == 1) log_stack("the last call before the retry");
        log_hr("  result", hr);
        if (SUCCEEDED(hr) && pv)
            logf_("  value: vt=%u  %llu", pv->vt, (unsigned long long)pv->uhVal.QuadPart);
    }

    if (duration && SUCCEEDED(hr) && pv && pv->vt == VT_UI8 && pv->uhVal.QuadPart == 0)
    {
        pv->uhVal.QuadPart = SUBSTITUTE_DURATION;
        stub_called("MF_PD_DURATION was 0 -> substituted ten minutes");
    }
    return r;
}

static void hook_source_reader(void *reader)
{
    if (!reader || real_read_sample) return;

    real_reader[3]  = patch_vtable_slot(reader, 3,  rdr_3);
    real_reader[8]  = patch_vtable_slot(reader, 8,  rdr_8);
    real_reader[10] = patch_vtable_slot(reader, 10, rdr_10);
    real_reader[11] = patch_vtable_slot(reader, 11, rdr_11);
    real_reader[12] = patch_vtable_slot(reader, 12, rdr_12);

    real_set_stream_selection = patch_vtable_slot(reader, 4, reader_set_stream_selection);
    real_get_native_type      = patch_vtable_slot(reader, 5, reader_get_native_type);
    real_get_current_type     = patch_vtable_slot(reader, 6, reader_get_current_type);
    real_set_media_type       = patch_vtable_slot(reader, 7, reader_set_media_type);
    real_read_sample          = patch_vtable_slot(reader, 9, reader_read_sample);
    logf_("  source reader hooks: %s", real_read_sample ? "installed" : "COULD NOT PATCH");
}

static LONG open_calls;

static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(IMFByteStream *stream,
                                                            IMFAttributes *attrs,
                                                            IMFSourceReader **reader)
{
    HRESULT hr;
    UINT32 count = 0;
    BOOL first = InterlockedIncrement(&open_calls) == 1;

    logf_("MFCreateSourceReaderFromByteStream");
    if (first) log_stack("the video player");
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

/*
 * The rest of the MFPlat surface, wrapped uniformly.
 *
 * Filling in the aspect ratio and interlace mode did not change anything, and
 * that is eight guesses now. The game imports fifteen MFPlat functions and six
 * were watched; the last line before a retry has to be one of the other nine,
 * so stop choosing between them and take all of them.
 *
 * Each wrapper forwards four register arguments, which covers every one of
 * these, and logs the result.
 */
static void *real_mf[9];
static LONG mf_calls[9];
static const char *const mf_names[9] =
{
    "MFCreateMFVideoFormatFromMFMediaType",
    "MFCreateMediaType",
    "MFCreateSample",
    "MFCreateMemoryBuffer",
    "MFCreateAlignedMemoryBuffer",
    "MFCreateWaveFormatExFromMFMediaType",
    "MFPutWorkItem2",
    "MFCreateAsyncResult",
    "MFInvokeCallback",
};

#define MF_WRAP(idx)                                                                  static ULONG_PTR WINAPI mfw_##idx(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c,                                            ULONG_PTR d)                                    {                                                                                     ULONG_PTR (WINAPI *fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR)                    = real_mf[idx];                                                               ULONG_PTR r = fn(a, b, c, d);                                                     HRESULT hr = (HRESULT)r;                                                          if (FAILED(hr) || InterlockedIncrement(&mf_calls[idx]) <= 3)                      {                                                                                     logf_("%s", mf_names[idx]);                                                       log_hr("  result", hr);                                                       }                                                                                 return r;                                                                     }

MF_WRAP(0) MF_WRAP(1) MF_WRAP(2) MF_WRAP(3) MF_WRAP(4)
MF_WRAP(5) MF_WRAP(6) MF_WRAP(7) MF_WRAP(8)

static void *const mf_wrappers[9] =
{
    mfw_0, mfw_1, mfw_2, mfw_3, mfw_4, mfw_5, mfw_6, mfw_7, mfw_8,
};

static void hook_remaining_mfplat(void)
{
    unsigned i;
    for (i = 0; i < 9; ++i)
    {
        real_mf[i] = hook_import("MFPlat.DLL", mf_names[i], mf_wrappers[i]);
        logf_("  %-38s %s", mf_names[i], real_mf[i] ? "hooked" : "not imported");
    }
}

/*
 * Force the video player past its own abort branches.
 *
 * Every call it makes succeeds -- Media Foundation, the textures, the shared
 * one -- and it still gives up, so the check that stops it is one this probe
 * cannot see the inside of. Rather than keep guessing which, neutralise all
 * four branches at once and find out whether frames start.
 *
 *   +0x27930fc  je   after the open call
 *   +0x2793121  js   after MFCreateMFVideoFormatFromMFMediaType
 *   +0x279312e  js   after the texture allocation
 *   +0x279313b  js   after the third call
 *
 * All four are two-byte short jumps to the same failure block, so a two-byte
 * nop covers each exactly. If ReadSample starts being called, the blocker is
 * one of these and they can be re-enabled one at a time to say which. If
 * nothing changes, it is somewhere else entirely and this experiment is over
 * in one run rather than four.
 *
 * Offsets are from this build. Each is verified against the bytes expected
 * there and skipped if they differ, so a different build patches nothing
 * rather than something arbitrary.
 */
static const struct { DWORD rva; BYTE opcode, rel; const char *what; } abort_branches[] =
{
    { 0x27930fc, 0x74, 0x7e, "je  after the open" },
    { 0x2793121, 0x78, 0x59, "js  after MFCreateMFVideoFormatFromMFMediaType" },
    { 0x279312e, 0x78, 0x4c, "js  after the textures" },
    { 0x279313b, 0x78, 0x3f, "js  after the third call" },
};

static void defeat_abort_branches(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    unsigned i;

    logf_("");
    logf_("=== forcing the player past its abort branches ===");
    for (i = 0; i < ARRAY_COUNT(abort_branches); ++i)
    {
        BYTE *at = base + abort_branches[i].rva;
        DWORD old;

        if (at[0] != abort_branches[i].opcode || at[1] != abort_branches[i].rel)
        {
            logf_("  +0x%lx: expected %02x %02x, found %02x %02x -- left alone",
                  (unsigned long)abort_branches[i].rva,
                  abort_branches[i].opcode, abort_branches[i].rel, at[0], at[1]);
            continue;
        }
        if (!VirtualProtect(at, 2, PAGE_EXECUTE_READWRITE, &old))
        {
            logf_("  +0x%lx: could not make writable", (unsigned long)abort_branches[i].rva);
            continue;
        }
        at[0] = 0x66; at[1] = 0x90;            /* the canonical two-byte nop */
        VirtualProtect(at, 2, old, &old);
        FlushInstructionCache(GetCurrentProcess(), at, 2);
        logf_("  +0x%lx: %s -- neutralised",
              (unsigned long)abort_branches[i].rva, abort_branches[i].what);
    }
}

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
    hook_remaining_mfplat();
    defeat_abort_branches();
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
        InitializeCriticalSection(&frame_lock);
        DisableThreadLibraryCalls(inst);
        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH && !reserved)
        report_totals();
    return TRUE;
}
