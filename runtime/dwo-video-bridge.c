/*
 * dwo-video-bridge — makes DYNASTY WARRIORS: ORIGINS show its cutscenes under
 * CrossOver on Apple Silicon.
 *
 * The problem
 * -----------
 * The game decodes VP9 with Media Foundation on a D3D11 device kept only for
 * video, and draws with a D3D12 renderer. Five things stop that under Apple's
 * D3DMetal, and each one hides the next:
 *
 *   1. ID3D11VideoDevice and ID3D11VideoContext are not implemented, and the
 *      player refuses to start without both.
 *   2. It asks the source reader to decode into D3D video textures, which
 *      D3DMetal cannot produce.
 *   3. It requires a video processor whose rate conversion caps advertise
 *      DEINTERLACE_BOB, and gives up with E_FAIL when none does.
 *   4. It can only consume samples backed by a D3D texture -- it queries the
 *      buffer for IMFDXGIBuffer and has no path for anything else.
 *   5. It hands that texture to its D3D12 renderer by shared handle, and
 *      IDXGIResource::GetSharedHandle returns E_NOTIMPL. Nothing on the D3D11
 *      side can be seen by D3D12, so the video quad samples a texture nobody
 *      ever wrote.
 *
 * What this does
 * --------------
 * Supplies the interfaces, moves decoding to software, and carries the frame
 * across the gap itself: hand the game a handle of ours, receive it back at
 * ID3D12Device::OpenSharedHandle, and return a texture created on the game's
 * own D3D12 device that we fill each frame.
 *
 * It decodes nothing. Getting VP9 out of a .webm at all needs CrossOver
 * patched with winevideo -- libgstvpx, libgstmatroska, and the byte-stream
 * handler registration. This starts work where Media Foundation hands over a
 * decoded NV12 sample.
 *
 * The game's own code is never modified.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <d3d11.h>
#include <d3d12.h>
#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>

#define LOGFILE "C:\\dwo-video-bridge.log"

/* A game under CrossOver cannot be attached to, so leave enough of a trail to
 * tell a working install from a broken one. Not a trace: a handful of lines. */
static CRITICAL_SECTION log_lock;

/*
 * Which game wrote the line.
 *
 * One bottle, one log, and now more than one title on this bridge: DYNASTY
 * WARRIORS and Nioh 3 both append here. Reading a shared runtime log as if it
 * belonged to whichever game was just launched is a mistake this project has
 * already made once, and the fix is to stop making it possible.
 */
static const char *exe_tag_(void)
{
    static char tag[64];
    if (!tag[0])
    {
        char path[MAX_PATH];
        const char *base = path;
        DWORD len = GetModuleFileNameA(NULL, path, sizeof(path) - 1);
        const char *p;
        if (!len) return "?";
        path[len] = 0;
        for (p = path; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
        lstrcpynA(tag, base, sizeof(tag));
    }
    return tag;
}

static void logf_(const char *fmt, ...)
{
    char buf[512];
    char line[640];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = 0;
    n = _snprintf(line, sizeof(line) - 2, "[%s] %s", exe_tag_(), buf);
    if (n < 0 || n >= (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    lstrcpynA(buf, line, sizeof(buf) - 1);
    n = lstrlenA(buf);
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

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------ hooking --- */

/*
 * Replace one entry in the main module's import address table. The thunk
 * arrays come in pairs: OriginalFirstThunk still holds the names the linker
 * recorded, FirstThunk holds the addresses the loader filled in.
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

/* Replace one entry of an object's vtable, returning what was there. One slot
 * rather than a proxy: routing a whole D3D interface through this module is
 * what broke rendering when it was tried. */
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

/* ------------------------------------------------------------- state --- */

static const GUID iid_video_device  = { 0x10ec4d5b, 0x975a, 0x4689,
                                        { 0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33 } };
static const GUID iid_video_context = { 0x61f21c45, 0x3c0e, 0x4a74,
                                        { 0x9c, 0xea, 0x67, 0x10, 0x0d, 0x9a, 0xd5, 0xe4 } };
static const GUID iid_dxgi_resource = { 0x035f3ab4, 0x482e, 0x4e50,
                                        { 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b } };
static const GUID iid_dxgi_buffer   = { 0xe7174cfa, 0x1c9e, 0x48b1,
                                        { 0x88, 0x66, 0x62, 0x62, 0x26, 0xbf, 0xc2, 0x58 } };

/* Handed to the game in place of the one D3DMetal will not make. Recognised
 * again when its D3D12 renderer brings it back. */
#define BRIDGE_HANDLE ((HANDLE)(ULONG_PTR)0xD3D12B21D)

/* Set once OpenSharedHandle is watched, so nothing hands out a handle only
 * that hook can make sense of. */
static BOOL d3d12_bridge_armed;
/* The D3D11 texture the game asked to share: its shape, not the clip's. */
static UINT share_width, share_height, share_format;

/*
 * Paint the bridge texture a flat colour instead of the frame.
 *
 * A diagnostic, not a feature, and compiled in rather than read from the
 * environment because a game started from a running Steam never sees a
 * variable set afterwards. It answers one question that nothing else here can:
 * whether what this bridge writes reaches the screen at all. If the screen
 * turns magenta the delivery works and the frame's contents or layout are
 * wrong; if it does not, nothing this bridge writes is ever displayed.
 */
#define BRIDGE_TEST_MAGENTA 0

/*
 * Let the IMFDXGIBuffer query fail, as it would on a real software sample.
 *
 * Answering it is right for a game that wants a texture and will share it on
 * to D3D12 -- DYNASTY WARRIORS and Nioh 3 both do. Kingdom Hearts does not:
 * it creates its own NV12 plane pair in D3D12, R8 for luma at full size and
 * R8G8 for chroma at half, and fills them from the sample's bytes. Handing it
 * a BGRA texture where it expects an NV12 one sends it down a branch whose
 * planes nothing fills, which is a green screen and no crash -- the same shape
 * of mistake as offering NieR Replicant a video device it could not use.
 */
#define BRIDGE_TEST_REFUSE_DXGI_BUFFER 0

struct stub_object { void **vtbl; LONG refcount; };

static struct stub_object stub_video_device, stub_video_context;

/* Offer the video device even with no D3D12 behind it. Diagnostic only: it is
 * how a title is asked what it would do with one. */
static struct stub_object stub_vp_enumerator, stub_vp_processor;
static struct stub_object stub_vp_input_view, stub_vp_output_view;
static struct stub_object stub_dxgi_buffer;

static CRITICAL_SECTION frame_lock;
static ID3D11Device *video_device;
static ID3D11DeviceContext *video_context;
static ID3D11Texture2D *frame_texture;
static UINT frame_width, frame_height, frame_stride;
static UINT texture_width, texture_height;

static void *vd_vtbl[], *vc_vtbl[], *vpe_vtbl[], *vp_vtbl[], *vpiv_vtbl[], *vpov_vtbl[];
static void *dxgibuf_vtbl[];
static HRESULT (WINAPI *real_create_texture2d)(void *, const void *, const void *, void **);
static void *input_view_resource, *output_view_resource;
/* The converted frame waiting to be uploaded, and what it cost. */
static BYTE *pending_bgra;
static size_t pending_size;
static UINT pending_w, pending_h;
static LONGLONG convert_ticks, upload_ticks;
static LONG converted, uploaded;
static D3D11_VIDEO_PROCESSOR_CONTENT_DESC vp_content_desc;

/* The generated vtables report which method was reached; a shipping build has
 * no use for that, and the file is generated so it is not edited to suit. */
/*
 * Which stub methods a game actually drives.
 *
 * Compiled out until now, which made the bridge silent about the one thing
 * only it can see: whether a title uses the D3D11 video processor to present
 * its frames, and which calls it makes. Bounded, so a per-frame call cannot
 * fill the log.
 */
/*
 * An interface id as text.
 *
 * The bridge only recognises the handful of GUIDs it acts on, so every other
 * query is invisible -- and on a title that takes an unknown route, the
 * unrecognised ones are exactly the interesting ones. Four rotating buffers so
 * a single log line can carry more than one.
 */
static const char *guid_text_(REFIID iid)
{
    static char buf[4][40];
    static LONG turn;
    char *out = buf[InterlockedIncrement(&turn) & 3];
    const GUID *g = (const GUID *)iid;

    if (!g) return "(null)";
    _snprintf(out, sizeof(buf[0]), "%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x",
              (unsigned long)g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
              g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return out;
}

/*
 * Has this resource shape been seen before?
 *
 * Bounding by call count is the wrong bound here: a renderer creates thousands
 * of textures and reuses a handful of shapes, so the first two dozen calls are
 * all interface art and the one that matters -- the surface the size of the
 * clip -- is created later and never reaches the log. Keyed on width, height
 * and format instead, every distinct shape is reported exactly once and the
 * total stays bounded by the number of shapes.
 */
static BOOL shape_is_new(UINT tag, UINT w, UINT h, UINT fmt)
{
    static struct { UINT tag, w, h, fmt; } seen[96];
    static LONG count;
    LONG i, n = count;

    for (i = 0; i < n && i < (LONG)(sizeof(seen) / sizeof(seen[0])); ++i)
        if (seen[i].tag == tag && seen[i].w == w && seen[i].h == h && seen[i].fmt == fmt)
            return FALSE;
    if (n >= (LONG)(sizeof(seen) / sizeof(seen[0]))) return FALSE;
    seen[n].tag = tag; seen[n].w = w; seen[n].h = h; seen[n].fmt = fmt;
    count = n + 1;
    return TRUE;
}

static void stub_called(const char *what)
{
    static LONG told;
    if (InterlockedIncrement(&told) <= 32) logf_("STUB  %s", what);
}

static const char *dxgi_format_name(UINT f) { (void)f; return ""; }

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
    return n < 0 ? 0 : n;          /* static objects: never actually freed */
}

/* --------------------------------------------------- video processor --- */

static HRESULT WINAPI vd_CreateVideoProcessorEnumerator(void *self, const void *desc, void **out)
{
    (void)self;
    if (desc) vp_content_desc = *(const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *)desc;
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_enumerator.refcount);
    *out = &stub_vp_enumerator;
    return S_OK;
}

static HRESULT WINAPI vd_CreateVideoProcessor(void *self, void *enumerator, UINT rate, void **out)
{
    (void)self; (void)enumerator; (void)rate;
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
    input_view_resource = resource;
    stub_called("ID3D11VideoDevice::CreateVideoProcessorInputView");
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
    output_view_resource = resource;
    if (!out) return E_INVALIDARG;
    InterlockedIncrement(&stub_vp_output_view.refcount);
    *out = &stub_vp_output_view;
    return S_OK;
}

/* The game asks about one format at a time and insists on
 * D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT, which is bit 1. */
static HRESULT WINAPI vpe_CheckVideoProcessorFormat(void *self, UINT format, UINT *flags)
{
    (void)self; (void)format;
    if (!flags) return E_INVALIDARG;
    *flags = 0x1 | 0x2;
    return S_OK;
}

static HRESULT WINAPI vpe_GetVideoProcessorCaps(void *self, D3D11_VIDEO_PROCESSOR_CAPS *caps)
{
    (void)self;
    if (!caps) return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));
    /* A processor advertising no rate conversions and no input streams is one
     * no caller can use, so claim the single one it needs. */
    caps->RateConversionCapsCount = 1;
    caps->MaxInputStreams = 1;
    caps->MaxStreamStates = 1;
    return S_OK;
}

static HRESULT WINAPI vpe_GetVideoProcessorContentDesc(void *self,
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC *desc)
{
    (void)self;
    if (!desc) return E_INVALIDARG;
    *desc = vp_content_desc;
    return S_OK;
}

/*
 * The one bit the whole thing turned on. The player walks these looking for
 * ProcessorCaps bit 1 and returns E_FAIL when no entry has it. Claiming
 * DEINTERLACE_BOB is honest enough: bob deinterlacing a progressive frame is
 * a copy, which is all this content needs.
 */
static HRESULT WINAPI vpe_GetVideoProcessorRateConversionCaps(void *self, UINT index,
        D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS *caps)
{
    (void)self; (void)index;
    if (!caps) return E_INVALIDARG;
    memset(caps, 0, sizeof(*caps));
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

/*
 * Nothing to blit. The frame reaches the renderer over the D3D12 bridge, so
 * the copies that would belong here would move megabytes a frame between
 * textures nothing reads. The call still has to succeed.
 */
/*
 * What a texture actually is, read from its own GetDesc.
 *
 * ID3D11Texture2D::GetDesc is slot 10: three IUnknown, four ID3D11DeviceChild,
 * three ID3D11Resource, then GetDesc.
 */
static void describe_texture(const char *label, void *tex)
{
    UINT d[11] = {0};
    void (WINAPI *get_desc)(void *, void *);
    if (!tex) { logf_("  %s: (none)", label); return; }
    get_desc = (void (WINAPI *)(void *, void *))(*(void ***)tex)[10];
    get_desc(tex, d);
    logf_("  %s: %ux%u format=%u usage=%u bind=0x%x cpu=0x%x misc=0x%x",
          label, d[0], d[1], d[4], d[7], d[8], d[9], d[10]);
}

static HRESULT WINAPI vc_VideoProcessorBlt(void *self, void *processor, void *output_view,
                                           UINT frame, UINT stream_count, const void *streams)
{
    (void)self; (void)processor; (void)output_view;
    (void)frame; (void)stream_count; (void)streams;

    /*
     * Do the blit, which for this shape is a copy.
     *
     * A game presenting through the video processor expects this call to put
     * the decoded frame into its own target. Returning S_OK without doing so
     * is a black screen on a title whose frames are all correct -- NieR
     * Replicant decodes over three hundred samples, none of them empty, and
     * shows nothing.
     *
     * No colour conversion is needed here and that is worth stating, because
     * the obvious assumption is wrong: both surfaces arrive as
     * B8G8R8A8_UNORM at the same size. Media Foundation delivered NV12 and the
     * game had already uploaded it as BGRA, so what the processor was being
     * asked for was the copy and the rectangle handling, not the colour
     * space. A conversion here would corrupt already-correct pixels.
     *
     * The destination rectangle is ignored: the two textures match exactly, so
     * every scaling case this could face is the identity. A title that
     * presents into a differently sized target needs CopySubresourceRegion and
     * a real scale, and would have to be measured before it is written.
     *
     * ID3D11DeviceContext slot 47 is CopyResource -- 46 is
     * CopySubresourceRegion, 48 UpdateSubresource.
     */
    /*
     * Geometry, taken from the target rather than from the media type.
     *
     * The type the game set carried no frame size -- it logged 0x0 -- so
     * nothing downstream had dimensions to work with. The output texture is
     * unambiguous and is right there.
     */
    if (!d3d12_bridge_armed && !frame_width && output_view_resource)
    {
        UINT d[11] = {0};
        void (WINAPI *get_desc)(void *, void *) =
            (void (WINAPI *)(void *, void *))(*(void ***)output_view_resource)[10];
        get_desc(output_view_resource, d);
        if (d[0] && d[1])
        {
            frame_width = d[0]; frame_height = d[1];
            logf_("  geometry taken from the target: %ux%u", d[0], d[1]);
        }
    }

    /*
     * Only the D3D11-only titles are served here.
     *
     * Where the D3D12 bridge is armed this call did nothing before and must go
     * on doing nothing: those titles have their frames presented by the bridge
     * itself, and an extra copy into a surface it also writes would be at best
     * redundant and at worst a race. Scoping this to the absence of a D3D12
     * device keeps DYNASTY WARRIORS and Nioh 3 on exactly the path they were
     * measured on.
     */
    if (!d3d12_bridge_armed && video_context && output_view_resource)
    {
        EnterCriticalSection(&frame_lock);
        /*
         * Upload on every blit, not only when the frame changed.
         *
         * Uploading only on new frames looked like the obvious saving and was
         * measured wrong: the upload costs 0.27 ms where the conversion costs
         * 4.51, so what it saved was nothing, and it left the target holding
         * whatever the game put there between video frames -- a 25 fps clip on
         * a 54 Hz display, alternating with anything the game drew. The
         * conversion still runs once per decoded frame, which is the cost that
         * actually matters.
         */
        if (pending_bgra && pending_w && pending_h)
        {
            void (WINAPI *update)(void *, void *, UINT, const void *,
                                  const void *, UINT, UINT) =
                (void (WINAPI *)(void *, void *, UINT, const void *,
                                 const void *, UINT, UINT))
                (*(void ***)video_context)[48];   /* UpdateSubresource */
            LARGE_INTEGER a_, b_;
            QueryPerformanceCounter(&a_);
            update(video_context, output_view_resource, 0, NULL, pending_bgra,
                   pending_w * 4, 0);
            QueryPerformanceCounter(&b_);
            upload_ticks += b_.QuadPart - a_.QuadPart;
            ++uploaded;
            {
                /* Each cost against its own count. Dividing both by the blit
                 * count once made the conversion look cheaper than it is:
                 * blits run at display rate, conversions once per decoded
                 * frame, and those are not the same number. */
                if (uploaded == 200)
                {
                    LARGE_INTEGER f;
                    QueryPerformanceFrequency(&f);
                    logf_("%ld frames in %ld blits: convert %.2f ms each, upload %.2f ms each",
                          converted, uploaded,
                          converted ? (double)convert_ticks * 1000.0 / f.QuadPart / converted : 0.0,
                          (double)upload_ticks * 1000.0 / f.QuadPart / uploaded);
                }
            }
        }
        LeaveCriticalSection(&frame_lock);
    }

    /*
     * The one call that would put the picture on screen, and today it returns
     * success having done nothing -- which is the whole of the black screen on
     * a title that decodes its frames correctly. Report what it was given, so
     * the conversion can be written against real formats rather than assumed
     * ones. Once, because this is per-frame.
     */
    {
        /*
         * The first Blt carried no input, so logging only it said nothing: the
         * input surface travels inside the stream description rather than
         * arriving through the create call, and the opening frame appears to be
         * a clear. Report the first few, and stop once one has actually shown
         * an input -- that is the frame the conversion has to be written for.
         *
         * D3D11_VIDEO_PROCESSOR_STREAM: BOOL Enable, then four UINTs, then
         * ppPastSurfaces and pInputSurface -- pointer-aligned, so +24 and +32.
         */
        static LONG told; static BOOL seen_input;
        if (!seen_input && InterlockedIncrement(&told) <= 6)
        {
            const BYTE *st = streams;
            void *input_surface = st ? *(void **)(st + 32) : NULL;
            BOOL enabled = st ? *(const BOOL *)st : FALSE;
            logf_("STUB  VideoProcessorBlt [#%ld] streams=%u enable=%d surface=%p%s",
                  told, stream_count, enabled, input_surface,
                  input_surface == (void *)&stub_vp_input_view ? " (ours)" : "");
            describe_texture("input  (from the decoder)", input_view_resource);
            if (told == 1) describe_texture("output (the game's target)", output_view_resource);
            if (input_view_resource) seen_input = TRUE;
        }
    }
    return S_OK;
}

#include "video-stubs.h"

/* -------------------------------------------------- NV12 conversion --- */

/* Saturation without branches. */
#define CLAMP_BIAS 512
static BYTE clamp8[1024 + 512];

static void build_clamp_table(void)
{
    int i;
    for (i = 0; i < (int)ARRAY_COUNT(clamp8); ++i)
    {
        int v = i - CLAMP_BIAS;
        clamp8[i] = (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

/*
 * NV12 to BGRA, BT.709 limited range, writing at the destination's own row
 * pitch so no second pass is needed to re-align it.
 *
 * Two pixels at a time: NV12 chroma is subsampled 2:1 across and both read
 * the same pair, so the three chroma terms are computed once rather than six
 * times. At 3.7 million pixels a frame that arithmetic is the whole cost.
 */
static void nv12_to_bgra(const BYTE *nv12, UINT stride, BYTE *bgra, UINT dst_pitch,
                         UINT width, UINT height)
{
    const BYTE *chroma = nv12 + (size_t)stride * height;
    UINT x, y;

    for (y = 0; y < height; ++y)
    {
        const BYTE *luma_row = nv12 + (size_t)stride * y;
        const BYTE *chroma_row = chroma + (size_t)stride * (y / 2);
        BYTE *out = bgra + (size_t)dst_pitch * y;

        for (x = 0; x + 1 < width; x += 2)
        {
            int d = chroma_row[x] - 128;
            int e = chroma_row[x + 1] - 128;
            int r_add = 459 * e + 128;
            int g_add = -55 * d - 136 * e + 128;
            int b_add = 541 * d + 128;
            int c0 = 298 * (luma_row[x] - 16);
            int c1 = 298 * (luma_row[x + 1] - 16);

            out[0] = clamp8[((c0 + b_add) >> 8) + CLAMP_BIAS];
            out[1] = clamp8[((c0 + g_add) >> 8) + CLAMP_BIAS];
            out[2] = clamp8[((c0 + r_add) >> 8) + CLAMP_BIAS];
            out[3] = 0xff;
            out[4] = clamp8[((c1 + b_add) >> 8) + CLAMP_BIAS];
            out[5] = clamp8[((c1 + g_add) >> 8) + CLAMP_BIAS];
            out[6] = clamp8[((c1 + r_add) >> 8) + CLAMP_BIAS];
            out[7] = 0xff;
            out += 8;
        }
        if (x < width)                        /* odd width, one left over */
        {
            int d = chroma_row[x & ~1u] - 128;
            int e = chroma_row[(x & ~1u) + 1] - 128;
            int c = 298 * (luma_row[x] - 16);
            out[0] = clamp8[((c + 541 * d + 128) >> 8) + CLAMP_BIAS];
            out[1] = clamp8[((c - 55 * d - 136 * e + 128) >> 8) + CLAMP_BIAS];
            out[2] = clamp8[((c + 459 * e + 128) >> 8) + CLAMP_BIAS];
            out[3] = 0xff;
        }
    }
}

/* ------------------------------------------------------- the bridge --- */

/*
 * A copy queue rather than a direct one, deliberately: resources sit in
 * COMMON, are promoted implicitly for the copy and decay back afterwards, so
 * no barriers are needed and nothing is assumed about the state the game's
 * renderer expects to find. It also keeps this work off the queue it draws
 * with.
 */
static ID3D12Resource *bridge_texture, *bridge_upload;
static ID3D12CommandQueue *bridge_queue;
static ID3D12CommandAllocator *bridge_alloc;
static ID3D12GraphicsCommandList *bridge_list;
static ID3D12Fence *bridge_fence;
static HANDLE bridge_event;
static UINT64 bridge_fence_value;
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT bridge_footprint;
static UINT bridge_rows;
static UINT64 bridge_row_bytes, bridge_total_bytes;

/*
 * Carry the frame as NV12, the way the decoder would have handed it over.
 *
 * The bridge has always published a B8G8R8A8 texture and converted into it,
 * which is what a game wanting a picture needs. Kingdom Hearts wants the
 * decoder's own surface instead: it opens the shared texture in D3D12 and
 * copies plane 0 and plane 1 out of it into an R8 at full size and an R8G8 at
 * half -- measured, 1920x1080 and 960x540 -- then converts in its own shader.
 * Those copies read nothing from a BGRA texture, so the planes stay at zero,
 * and zero luma with zero chroma is the green screen.
 *
 * Two subresources, two footprints, two copies. NV12 needs even dimensions.
 */
/*
 * The plane textures a game creates for itself.
 *
 * Kingdom Hearts Dream Drop Distance expects the decoder's NV12 surface and
 * pulls its two planes into resources of its own: R8_UNORM at the clip's size
 * for luma, R8G8_UNORM at half for chroma. Those copies read nothing from the
 * B8G8R8A8 texture this bridge publishes, so both planes stay at zero, and
 * zero luma with zero chroma is exactly the green screen.
 *
 * D3DMetal cannot be handed an NV12 texture to copy from -- the request is
 * fatal, see bridge_create -- so the frame goes in the other direction: this
 * writes the two planes itself, into the game's own resources, and the game's
 * shader converts them as it always did. Nothing here engages unless a game
 * creates that exact pair at the clip's dimensions.
 */
static ID3D12Resource *plane_y, *plane_uv, *plane_upload;
static D3D12_RESOURCE_DESC plane_y_desc, plane_uv_desc;
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT plane_y_fp, plane_uv_fp;
static UINT64 plane_upload_size;
static BOOL plane_ready;
static ID3D12Device *bridge_device;
static void plane_prepare(ID3D12Device *device);
static void plane_write(const BYTE *nv12, UINT stride, UINT width, UINT height);

/* A new plane needs new footprints and a correctly sized upload buffer. */
static void plane_rearm(void)
{
    plane_ready = FALSE;
    if (plane_upload) { ID3D12Resource_Release(plane_upload); plane_upload = NULL; }
}

static D3D12_PLACED_SUBRESOURCE_FOOTPRINT bridge_plane[2];
static UINT bridge_plane_rows[2];
static UINT64 bridge_plane_bytes[2];
static BOOL bridge_is_nv12;

#define BRIDGE_CHECK(call, what)                                    \
    do {                                                            \
        HRESULT _hr = (call);                                       \
        if (FAILED(_hr)) {                                          \
            logf_("bridge: %s failed, hr %#lx", what, (unsigned long)_hr); \
            return _hr;                                             \
        }                                                           \
    } while (0)

static HRESULT bridge_create(ID3D12Device *device, UINT width, UINT height)
{
    D3D12_HEAP_PROPERTIES heap = { 0 };
    D3D12_RESOURCE_DESC desc = { 0 };
    D3D12_COMMAND_QUEUE_DESC queue = { 0 };

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width & ~1u;
    desc.Height = height & ~1u;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    /*
     * Not NV12, and not by choice.
     *
     * A shared NV12 texture is what the decoder would have published and what
     * a game reading plane 0 and plane 1 out of it needs. D3DMetal does not
     * refuse the request -- CreateCommittedResource with DXGI_FORMAT_NV12 does
     * not return an error, it takes the process down, with no exception this
     * bridge could catch and nothing in the log after the call. Measured on
     * Kingdom Hearts Dream Drop Distance: the last line written is the shared
     * handle being opened.
     *
     * So the frame reaches such a game the other way round, by writing into
     * the plane textures it creates for itself. See plane_watch.
     */
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    bridge_is_nv12 = FALSE;
    BRIDGE_CHECK(ID3D12Device_CreateCommittedResource(device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&bridge_texture), "texture");

    ID3D12Device_GetCopyableFootprints(device, &desc, 0, bridge_is_nv12 ? 2 : 1, 0,
            bridge_plane, bridge_plane_rows, bridge_plane_bytes, &bridge_total_bytes);
    bridge_footprint = bridge_plane[0];
    bridge_rows = bridge_plane_rows[0];
    bridge_row_bytes = bridge_plane_bytes[0];

    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bridge_total_bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    BRIDGE_CHECK(ID3D12Device_CreateCommittedResource(device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&bridge_upload), "upload buffer");

    queue.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    BRIDGE_CHECK(ID3D12Device_CreateCommandQueue(device, &queue,
            &IID_ID3D12CommandQueue, (void **)&bridge_queue), "copy queue");
    BRIDGE_CHECK(ID3D12Device_CreateCommandAllocator(device,
            D3D12_COMMAND_LIST_TYPE_COPY, &IID_ID3D12CommandAllocator,
            (void **)&bridge_alloc), "allocator");
    BRIDGE_CHECK(ID3D12Device_CreateCommandList(device, 0,
            D3D12_COMMAND_LIST_TYPE_COPY, bridge_alloc, NULL,
            &IID_ID3D12GraphicsCommandList, (void **)&bridge_list), "command list");
    ID3D12GraphicsCommandList_Close(bridge_list);
    BRIDGE_CHECK(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&bridge_fence), "fence");

    bridge_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    texture_width = width;
    texture_height = height;
    logf_("bridge ready: %ux%u, upload pitch %u", width, height,
          bridge_footprint.Footprint.RowPitch);
    return S_OK;
}

static void bridge_destroy(void)
{
    plane_rearm();
    if (plane_y)  { ID3D12Resource_Release(plane_y);  plane_y = NULL; }
    if (plane_uv) { ID3D12Resource_Release(plane_uv); plane_uv = NULL; }
    if (bridge_event) { CloseHandle(bridge_event); bridge_event = NULL; }
    if (bridge_fence)   { ID3D12Fence_Release(bridge_fence); bridge_fence = NULL; }
    if (bridge_list)    { ID3D12GraphicsCommandList_Release(bridge_list); bridge_list = NULL; }
    if (bridge_alloc)   { ID3D12CommandAllocator_Release(bridge_alloc); bridge_alloc = NULL; }
    if (bridge_queue)   { ID3D12CommandQueue_Release(bridge_queue); bridge_queue = NULL; }
    if (bridge_upload)  { ID3D12Resource_Release(bridge_upload); bridge_upload = NULL; }
    if (bridge_texture) { ID3D12Resource_Release(bridge_texture); bridge_texture = NULL; }
    texture_width = texture_height = 0;
}

/* Convert one frame straight into the upload buffer and copy it across. */
static void bridge_upload_frame(const BYTE *nv12, UINT stride, UINT width, UINT height)
{
    D3D12_TEXTURE_COPY_LOCATION dst = { 0 }, src = { 0 };
    BYTE *mapped = NULL;

    if (!bridge_texture || !bridge_upload || !bridge_list) return;

    /* A game that pulls the planes out for itself is served by writing them,
     * not by the picture below. Both can run: they touch different resources. */
    if (bridge_device)
    {
        plane_prepare(bridge_device);
        plane_write(nv12, stride, width, height);
    }
    if (width > texture_width || height > texture_height) return;
    if (FAILED(ID3D12Resource_Map(bridge_upload, 0, NULL, (void **)&mapped)) || !mapped)
        return;
    if (bridge_is_nv12)
    {
        /*
         * Straight through, plane by plane. No conversion: the game does that
         * in its own shader, which is the whole reason it wanted the decoder's
         * surface rather than a picture.
         */
        const BYTE *uv = nv12 + (size_t)stride * height;
        UINT y;
        for (y = 0; y < height; ++y)
            memcpy(mapped + bridge_plane[0].Offset
                   + (size_t)y * bridge_plane[0].Footprint.RowPitch,
                   nv12 + (size_t)y * stride, width);
        for (y = 0; y < height / 2; ++y)
            memcpy(mapped + bridge_plane[1].Offset
                   + (size_t)y * bridge_plane[1].Footprint.RowPitch,
                   uv + (size_t)y * stride, width);
    }
    else
        nv12_to_bgra(nv12, stride, mapped, bridge_footprint.Footprint.RowPitch, width, height);
#if BRIDGE_TEST_MAGENTA
    if (!bridge_is_nv12)
    {
        static LONG told;
        UINT pitch = bridge_footprint.Footprint.RowPitch, y, x;
        for (y = 0; y < height; ++y)
        {
            UINT32 *row = (UINT32 *)(mapped + (size_t)y * pitch);
            for (x = 0; x < width; ++x) row[x] = 0xFFFF00FF;   /* BGRA magenta */
        }
        if (InterlockedIncrement(&told) == 1)
            logf_("TEST: filling the bridge texture with magenta, %ux%u pitch %u",
                  width, height, pitch);
    }
#endif
    ID3D12Resource_Unmap(bridge_upload, 0, NULL);

    ID3D12CommandAllocator_Reset(bridge_alloc);
    ID3D12GraphicsCommandList_Reset(bridge_list, bridge_alloc, NULL);
    dst.pResource = bridge_texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = bridge_upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    {
        UINT plane, planes = bridge_is_nv12 ? 2u : 1u;
        for (plane = 0; plane < planes; ++plane)
        {
            dst.SubresourceIndex = plane;
            src.PlacedFootprint = bridge_plane[plane];
            ID3D12GraphicsCommandList_CopyTextureRegion(bridge_list, &dst, 0, 0, 0,
                                                        &src, NULL);
        }
    }
    ID3D12GraphicsCommandList_Close(bridge_list);
    ID3D12CommandQueue_ExecuteCommandLists(bridge_queue, 1,
            (ID3D12CommandList *const *)&bridge_list);

    /* Wait for it. Letting the frame go before the copy lands shows the
     * previous one, or nothing -- the same lesson winevideo's D3D9 bridge
     * records, where a bare Flush was not enough either. */
    ++bridge_fence_value;
    if (SUCCEEDED(ID3D12CommandQueue_Signal(bridge_queue, bridge_fence, bridge_fence_value))
        && ID3D12Fence_GetCompletedValue(bridge_fence) < bridge_fence_value
        && bridge_event)
    {
        ID3D12Fence_SetEventOnCompletion(bridge_fence, bridge_fence_value, bridge_event);
        WaitForSingleObject(bridge_event, 1000);
    }
}

/* ------------------------------------------- the sample the game sees --- */

/*
 * The game can only present a sample backed by a D3D texture: it queries the
 * media buffer for IMFDXGIBuffer and has no path for anything else. Since
 * decoding was moved to software to get it working at all, the texture has to
 * come from here.
 */
static HRESULT WINAPI dxgibuf_GetResource(void *self, REFIID iid, void **out)
{
    HRESULT hr;
    (void)self;
    if (!out) return E_INVALIDARG;
    EnterCriticalSection(&frame_lock);
    hr = frame_texture ? ID3D11Texture2D_QueryInterface(frame_texture, iid, out) : E_FAIL;
    LeaveCriticalSection(&frame_lock);
    return hr;
}

static HRESULT WINAPI dxgibuf_GetSubresourceIndex(void *self, UINT *index)
{
    (void)self;
    if (!index) return E_INVALIDARG;
    *index = 0;
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
    dxgibuf_GetResource, dxgibuf_GetSubresourceIndex,
    dxgibuf_GetUnknown, dxgibuf_SetUnknown,
};

static HRESULT (WINAPI *real_buffer_qi)(void *, REFIID, void **);

static HRESULT WINAPI buffer_qi(void *self, REFIID iid, void **out)
{
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 6)
            logf_("sample buffer QI %s%s", guid_text_(iid),
                  IsEqualGUID(iid, &iid_dxgi_buffer) ? "   << IMFDXGIBuffer" : "");
    }
#if BRIDGE_TEST_REFUSE_DXGI_BUFFER
    if (IsEqualGUID(iid, &iid_dxgi_buffer))
    {
        static LONG told;
        if (InterlockedIncrement(&told) == 1)
            logf_("TEST: refusing IMFDXGIBuffer -- the game must fill its own planes");
        return real_buffer_qi(self, iid, out);
    }
#endif
    if (IsEqualGUID(iid, &iid_dxgi_buffer))
    {
        InterlockedIncrement(&stub_dxgi_buffer.refcount);
        *out = &stub_dxgi_buffer;
        return S_OK;
    }
    return real_buffer_qi(self, iid, out);
}

/* Take one decoded frame across. The D3D11 texture is only ever handed over,
 * never read -- the picture reaches the renderer through the bridge. */
static void upload_frame(IMFSample *sample)
{
    IMFMediaBuffer *buffer = NULL;
    BYTE *data = NULL;
    DWORD length = 0;
    UINT stride;

    if (!sample || !video_device) return;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)) || !buffer) return;

    if (!real_buffer_qi)
        real_buffer_qi = patch_vtable_slot(buffer, 0, buffer_qi);

    /*
     * ConvertToContiguousBuffer may hand back a different object from the one
     * the sample holds, and the game queries the one it was given. Patching a
     * vtable covers every instance of that class -- but only that class. Say
     * which case this is, once, rather than assume.
     */
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 2)
        {
            IMFMediaBuffer *own = NULL;
            DWORD count = 0;
            IMFSample_GetBufferCount(sample, &count);
            if (SUCCEEDED(IMFSample_GetBufferByIndex(sample, 0, &own)) && own)
            {
                logf_("sample: %lu buffer(s), own=%p contiguous=%p -- %s",
                      (unsigned long)count, (void *)own, (void *)buffer,
                      own == buffer ? "same object" : "DIFFERENT objects");
                IMFMediaBuffer_Release(own);
            }
        }
    }

    if (FAILED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length)) || !data)
    {
        IMFMediaBuffer_Release(buffer);
        return;
    }

    EnterCriticalSection(&frame_lock);
    stride = frame_stride ? frame_stride : frame_width;
    if (frame_width && frame_height && length >= (DWORD)stride * frame_height * 3 / 2)
    {
        /* A clip of a different size needs its own texture: this game ships
         * 2560x1440 cutscenes and 960x540 interface clips side by side. */
        if (frame_texture && (texture_width != frame_width || texture_height != frame_height))
        {
            ID3D11Texture2D_Release(frame_texture);
            frame_texture = NULL;
            bridge_destroy();
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
            if (FAILED(ID3D11Device_CreateTexture2D(video_device, &desc, NULL, &frame_texture)))
                frame_texture = NULL;
        }
        /*
         * Is there a picture in here at all?
         *
         * "A sample was delivered" only says the pointer was not null. NieR
         * Replicant delivered three hundred samples whose luma measured flat
         * at zero, and the two cases are indistinguishable from outside. A
         * sparse sweep of the luma plane separates a decode that produced
         * nothing from a frame that was produced and then lost on its way to
         * the screen.
         */
        {
            static LONG told;
            LONG which = InterlockedIncrement(&told);
            if (which == 1 || which == 30 || which == 120)
            {
                UINT x, y, lo = 255, hi = 0;
                UINT64 sum = 0; UINT count = 0;
                for (y = 0; y < frame_height; y += 8)
                    for (x = 0; x < frame_width; x += 8)
                    {
                        BYTE v = data[(size_t)y * stride + x];
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                        sum += v; ++count;
                    }
                logf_("frame [#%ld] luma: average %lu, range %u..%u over %u samples",
                      which, (unsigned long)(count ? sum / count : 0), lo, hi, count);
            }
        }
        bridge_upload_frame(data, stride, frame_width, frame_height);
        /*
         * The D3D11-only path.
         *
         * bridge_upload_frame carries the frame to D3D12, which a title
         * without a D3D12 device never reaches. NieR Replicant presents
         * through the D3D11 video processor instead, and the surface it hands
         * that processor is never filled -- measured flat, three times across
         * a scene -- because the sample now arrives in system memory rather
         * than as a DXGI buffer. So convert here and write the result into a
         * texture of our own, which VideoProcessorBlt then copies into the
         * game's target.
         */
        if (!d3d12_bridge_armed)
        {
            /*
             * Convert here and upload later, once.
             *
             * The first working version went NV12 -> our texture -> the game's
             * texture, and did the second copy inside every VideoProcessorBlt.
             * The game blits at display rate and the video is 25 fps, so most
             * of those eight-megabyte copies moved a frame that had not
             * changed. Converting into a buffer and marking it dirty leaves
             * exactly one upload per decoded frame, straight into the target,
             * with no intermediate texture at all.
             */
            size_t need = (size_t)frame_width * frame_height * 4;
            if (pending_size < need)
            {
                if (pending_bgra) HeapFree(GetProcessHeap(), 0, pending_bgra);
                pending_bgra = HeapAlloc(GetProcessHeap(), 0, need);
                pending_size = pending_bgra ? need : 0;
            }
            if (pending_bgra)
            {
                LARGE_INTEGER a_, b_;
                QueryPerformanceCounter(&a_);
                nv12_to_bgra(data, stride, pending_bgra, frame_width * 4,
                             frame_width, frame_height);
                QueryPerformanceCounter(&b_);
                convert_ticks += b_.QuadPart - a_.QuadPart;
                ++converted;
                pending_w = frame_width; pending_h = frame_height;
            }
        }
    }
    LeaveCriticalSection(&frame_lock);

    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
}

/* ------------------------------------------------- the source reader --- */

static HRESULT (WINAPI *real_set_media_type)(void *, DWORD, DWORD *, IMFMediaType *);
static HRESULT (WINAPI *real_get_current_type)(void *, DWORD, IMFMediaType **);
static HRESULT (WINAPI *real_read_sample)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                          LONGLONG *, IMFSample **);

static HRESULT WINAPI reader_set_media_type(void *self, DWORD stream, DWORD *reserved,
                                            IMFMediaType *type)
{
    UINT64 size = 0;
    if (type && SUCCEEDED(IMFAttributes_GetUINT64((IMFAttributes *)type, &MF_MT_FRAME_SIZE, &size)))
    {
        frame_width = (UINT)(size >> 32);
        frame_height = (UINT)(size & 0xffffffff);
    }
    {
        /* The one answer that separates "no frames were produced" from
         * "frames were produced and went nowhere". A reader that refuses the
         * output format the game asked for leaves it with audio and no video
         * stream, and from outside that looks the same as a broken upload. */
        GUID sub; static LONG told;
        HRESULT hr = real_set_media_type(self, stream, reserved, type);
        if (InterlockedIncrement(&told) <= 8)
        {
            if (type && SUCCEEDED(IMFAttributes_GetGUID((IMFAttributes *)type,
                                                        &MF_MT_SUBTYPE, &sub)))
                logf_("SetCurrentMediaType asked '%c%c%c%c' %ux%u -> 0x%08lx",
                      (char)(sub.Data1 & 0xff), (char)((sub.Data1 >> 8) & 0xff),
                      (char)((sub.Data1 >> 16) & 0xff), (char)((sub.Data1 >> 24) & 0xff),
                      frame_width, frame_height, (unsigned long)hr);
            else
                logf_("SetCurrentMediaType -> 0x%08lx", (unsigned long)hr);
        }
        return hr;
    }
}

/* The type the reader settles on is the authority on geometry and stride,
 * more than the one the game asked for. A negative stride means bottom-up. */
static HRESULT WINAPI reader_get_current_type(void *self, DWORD stream, IMFMediaType **type)
{
    HRESULT hr = real_get_current_type(self, stream, type);
    if (SUCCEEDED(hr) && type && *type)
    {
        UINT64 size = 0;
        UINT32 stride = 0;
        if (SUCCEEDED(IMFAttributes_GetUINT64((IMFAttributes *)*type, &MF_MT_FRAME_SIZE, &size)))
        {
            frame_width = (UINT)(size >> 32);
            frame_height = (UINT)(size & 0xffffffff);
        }
        if (SUCCEEDED(IMFAttributes_GetUINT32((IMFAttributes *)*type,
                                              &MF_MT_DEFAULT_STRIDE, &stride)))
            frame_stride = stride > 0x7fffffff ? (UINT)(-(INT32)stride) : stride;
    }
    return hr;
}

static HRESULT WINAPI reader_read_sample(void *self, DWORD stream, DWORD flags,
                                         DWORD *actual, DWORD *sample_flags,
                                         LONGLONG *timestamp, IMFSample **sample)
{
    HRESULT hr = real_read_sample(self, stream, flags, actual, sample_flags,
                                  timestamp, sample);
    {
        static LONG reads, empty;
        LONG n = InterlockedIncrement(&reads);
        if (!(SUCCEEDED(hr) && sample && *sample)) InterlockedIncrement(&empty);
        /* First few and then a milestone: enough to say whether samples are
         * arriving at all without writing a line per frame. */
        if (n <= 3 || n == 50 || n == 300)
            logf_("ReadSample [#%ld] -> 0x%08lx, sample %s (%ld empty so far)",
                  n, (unsigned long)hr,
                  (sample && *sample) ? "delivered" : "NONE", empty);
    }
    if (SUCCEEDED(hr) && sample && *sample) upload_frame(*sample);
    return hr;
}

static HRESULT (WINAPI *real_MFCreateAttributes)(IMFAttributes **, UINT32);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromByteStream)(IMFByteStream *,
        IMFAttributes *, IMFSourceReader **);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromURL)(const WCHAR *,
        IMFAttributes *, IMFSourceReader **);

/*
 * A copy of the game's attributes without the two that demand D3D decoding.
 * NULL means there was nothing to copy or the copy failed, and the caller must
 * fall back to the game's own store.
 */
static IMFAttributes *strip_d3d_attributes(IMFAttributes *attrs)
{
    IMFAttributes *plain = NULL;
    UINT32 count = 0;

    if (!attrs || !real_MFCreateAttributes) return NULL;
    IMFAttributes_GetCount(attrs, &count);
    if (SUCCEEDED(real_MFCreateAttributes(&plain, count + 2)) && plain
        && SUCCEEDED(IMFAttributes_CopyAllItems(attrs, plain)))
    {
        IMFAttributes_DeleteItem(plain, &MF_SOURCE_READER_D3D_MANAGER);
        IMFAttributes_DeleteItem(plain, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS);
        return plain;
    }
    if (plain) IMFAttributes_Release(plain);
    return NULL;
}

/* IMFSourceReader: 6 GetCurrentMediaType, 7 SetCurrentMediaType, 9 ReadSample. */
static void arm_source_reader(IMFSourceReader *reader)
{
    if (!reader || real_read_sample) return;
    real_get_current_type = patch_vtable_slot(reader, 6, reader_get_current_type);
    real_set_media_type   = patch_vtable_slot(reader, 7, reader_set_media_type);
    real_read_sample      = patch_vtable_slot(reader, 9, reader_read_sample);
}

/*
 * Ask for software decoding.
 *
 * The game sets MF_SOURCE_READER_D3D_MANAGER and
 * MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, which tell Media Foundation to
 * decode into D3D video textures -- the thing D3DMetal cannot do. A copy of
 * the attributes without them decodes in software instead. A copy, never the
 * game's own store: writing into that breaks the negotiation outright.
 */
static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(IMFByteStream *stream,
        IMFAttributes *attrs, IMFSourceReader **reader)
{
    IMFAttributes *plain = strip_d3d_attributes(attrs);
    HRESULT hr = E_FAIL;

    if (plain)
    {
        hr = real_MFCreateSourceReaderFromByteStream(stream, plain, reader);
        IMFAttributes_Release(plain);
    }
    if (FAILED(hr))
        hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);

    if (SUCCEEDED(hr) && reader) arm_source_reader(*reader);
    return hr;
}

/*
 * The same door, opened by name instead of by stream. Kingdom Hearts HD 2.8
 * hands Media Foundation a path; NieR hands it a stream it has already
 * decrypted. Both need the D3D attributes gone.
 */
static HRESULT WINAPI my_MFCreateSourceReaderFromURL(const WCHAR *url,
        IMFAttributes *attrs, IMFSourceReader **reader)
{
    IMFAttributes *plain = strip_d3d_attributes(attrs);
    BOOL software = FALSE;
    HRESULT hr = E_FAIL;
    char name[260];
    size_t i = 0;

    if (url)
        while (i < sizeof(name) - 1 && url[i])
        {
            name[i] = (url[i] > 31 && url[i] < 127) ? (char)url[i] : '?';
            i++;
        }
    name[i] = 0;

    if (plain)
    {
        hr = real_MFCreateSourceReaderFromURL(url, plain, reader);
        IMFAttributes_Release(plain);
        software = SUCCEEDED(hr);
    }
    if (FAILED(hr))
        hr = real_MFCreateSourceReaderFromURL(url, attrs, reader);

    logf_("MFCreateSourceReaderFromURL(%s) -> 0x%08lx%s", name, (unsigned long)hr,
          software ? " [software decode]" : "");
    if (SUCCEEDED(hr) && reader) arm_source_reader(*reader);
    return hr;
}

/*
 * Hooking an import that has no name.
 *
 * An import table entry is either a name or an ordinal, and a hook that walks
 * names skips the ordinals entirely -- installing nothing, reporting nothing,
 * and never being called. d3d12.dll's D3D12CreateDevice is ordinal 101, and
 * two of the titles here import it that way.
 *
 * DYNASTY WARRIORS hid this: it has the ordinal import but reaches the
 * function through NVIDIA Streamline at runtime, so the GetProcAddress
 * substitution caught it and the missing half never showed. Wo Long calls it
 * straight through its own table, so nothing caught it -- and with the D3D12
 * side unarmed the bridge correctly refuses to hand out a share handle only
 * that side can read, the game learns it cannot share, and it tears the player
 * down and starts over. Seven times, in the log that led here.
 */
static void *hook_import_ordinal(const char *dll, WORD ordinal, void *replacement)
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
            void *previous;
            DWORD old;

            if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (IMAGE_ORDINAL(names->u1.Ordinal) != ordinal) continue;

            previous = (void *)addrs->u1.Function;
            if (!VirtualProtect(addrs, sizeof(*addrs), PAGE_READWRITE, &old)) return NULL;
            addrs->u1.Function = (ULONGLONG)(ULONG_PTR)replacement;
            VirtualProtect(addrs, sizeof(*addrs), old, &old);
            return previous;
        }
    }
    return NULL;
}

/* ------------------------------------------------------ the devices --- */

static HRESULT (WINAPI *real_device_qi)(void *, REFIID, void **);
static HRESULT (WINAPI *real_context_qi)(void *, REFIID, void **);

/*
 * Offer the ID3D11VideoDevice. Both halves are backed now.
 *
 * The stub is not a courtesy: it exists so a game will drive the video
 * processor whose output this bridge presents. Where a D3D12 device is armed
 * -- DYNASTY WARRIORS, Nioh 3, Wo Long, Kingdom Hearts -- the frame goes out
 * that way. Where there is none, VideoProcessorBlt writes the converted frame
 * into the game's own target with UpdateSubresource, which is how NieR
 * Replicant is served.
 *
 * This used to be gated on the D3D12 side being armed, and the reason was
 * sound when it was written: the D3D11 delivery did not exist yet, so
 * answering yes with nothing behind it was a black screen with sound. The
 * delivery was then written and the gate was not removed. What kept NieR
 * working after that was MGVF_VIDEO_DEVICE, an environment variable meant for
 * one experiment -- and nothing in the installer, the app or the bottle ever
 * set it. So the shipped fix answered "no" on every machine but the one it was
 * measured on, and NieR played its video to a black screen for everyone else.
 *
 * A game with no video path of its own is no worse off: it asked for a video
 * device, and it gets one that delivers.
 */
static HRESULT WINAPI device_qi(void *self, REFIID iid, void **out)
{
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 6)
            logf_("D3D11 device QI %s%s", guid_text_(iid),
                  IsEqualGUID(iid, &iid_video_device) ? "   << ID3D11VideoDevice" : "");
    }
    if (IsEqualGUID(iid, &iid_video_device))
    {
        InterlockedIncrement(&stub_video_device.refcount);
        *out = &stub_video_device;
        return S_OK;
    }
    return real_device_qi(self, iid, out);
}

static HRESULT WINAPI context_qi(void *self, REFIID iid, void **out)
{
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 6)
            logf_("D3D11 context QI %s%s", guid_text_(iid),
                  IsEqualGUID(iid, &iid_video_context) ? "   << ID3D11VideoContext" : "");
    }
    if (IsEqualGUID(iid, &iid_video_context))
    {
        InterlockedIncrement(&stub_video_context.refcount);
        *out = &stub_video_context;
        return S_OK;
    }
    return real_context_qi(self, iid, out);
}

/* The handle the game passes to its renderer. D3DMetal answers E_NOTIMPL, so
 * answer for it and recognise the value when it comes back. */
static HRESULT (WINAPI *real_res_get_shared_handle)(void *, HANDLE *);
static HRESULT (WINAPI *real_texture_qi)(void *, REFIID, void **);

static HRESULT WINAPI res_get_shared_handle(void *self, HANDLE *handle)
{
    HRESULT hr = real_res_get_shared_handle(self, handle);
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 12)
            logf_("GetSharedHandle -> 0x%08lx%s", (unsigned long)hr,
                  (FAILED(hr) && d3d12_bridge_armed) ? "   (answering BRIDGE_HANDLE)" : "");
    }
    /*
     * Only invent a handle if the side that can recognise it is armed.
     *
     * BRIDGE_HANDLE is meaningful to d3d12_open_shared_handle and to nothing
     * else. Handing it out while that hook is missing turns a clean failure --
     * the game learns it cannot share, and says so -- into a real D3D12 device
     * dereferencing 0xD3D12B21D. Nioh 3 crashed exactly that way, because its
     * D3D12 arrives through Streamline and the hook had not been placed there
     * yet. Reporting the original failure is the safe answer.
     */
    if (FAILED(hr) && handle && d3d12_bridge_armed) { *handle = BRIDGE_HANDLE; return S_OK; }
    return hr;
}

static HRESULT WINAPI texture_qi(void *self, REFIID iid, void **out)
{
    HRESULT hr = real_texture_qi(self, iid, out);
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 6)
            logf_("texture QI %s -> 0x%08lx", guid_text_(iid), (unsigned long)hr);
    }
    if (IsEqualGUID(iid, &iid_dxgi_resource) && SUCCEEDED(hr) && out && *out
        && !real_res_get_shared_handle)
        real_res_get_shared_handle = patch_vtable_slot(*out, 8, res_get_shared_handle);
    return hr;
}


/* The game's shared texture is the one whose handle has to work. */
static HRESULT WINAPI device_create_texture2d(void *self, const void *desc,
                                              const void *initial, void **texture)
{
    HRESULT hr = real_create_texture2d(self, desc, initial, texture);
    const UINT *d = desc;

    /*
     * Report the two cases that decide whether a video frame has anywhere to
     * land: a refused texture, and any request for NV12 -- the format
     * D3DMetal is documented elsewhere as unable to create. A player whose
     * upload surface is refused runs, plays its audio and shows nothing, and
     * from outside that is indistinguishable from a frame path that never ran
     * at all. Bounded, because a renderer creates thousands of textures.
     */
    if (d)
    {
        static LONG told;
        BOOL nv12 = (d[4] == 103);   /* DXGI_FORMAT_NV12 */
        /* Video-sized textures too: on a title that takes an unknown route,
         * where it asks for a surface the size of the clip is the evidence
         * that says where the frame was meant to land. */
        BOOL big  = (d[0] >= 640 && d[1] >= 360);
        (void)told;
        if ((FAILED(hr) || nv12 || big) && shape_is_new(11, d[0], d[1], d[4]))
            logf_("CreateTexture2D %ux%u format=%u bind=0x%x misc=0x%x -> 0x%08lx%s",
                  d[0], d[1], d[4], d[8], d[10], (unsigned long)hr,
                  nv12 ? "   << NV12" : "");
    }
    if (SUCCEEDED(hr) && d && texture && *texture && (d[10] & 2) && !real_texture_qi)
    {
        /*
         * This is the surface the game means to share, so this -- not the
         * decoded frame's size -- is the shape the D3D12 side must match.
         * Kingdom Hearts asks for 2048x1080 to carry a 1920x1080 clip.
         */
        share_width = d[0]; share_height = d[1]; share_format = d[4];
        logf_("share target: %ux%u format=%u (clip is %ux%u)",
              d[0], d[1], d[4], frame_width, frame_height);
        real_texture_qi = patch_vtable_slot(*texture, 0, texture_qi);
    }
    return hr;
}

static HRESULT (WINAPI *real_D3D11CreateDevice)(void *, UINT, HMODULE, UINT, const UINT *,
                                                UINT, UINT, void **, UINT *, void **);

static HRESULT WINAPI my_D3D11CreateDevice(void *adapter, UINT driver_type, HMODULE software,
                                           UINT flags, const UINT *levels, UINT num_levels,
                                           UINT sdk, void **device, UINT *level, void **context)
{
    HRESULT hr = real_D3D11CreateDevice(adapter, driver_type, software, flags, levels,
                                        num_levels, sdk, device, level, context);
    if (SUCCEEDED(hr))
    {
        if (device && *device && !real_device_qi)
        {
            video_device = *(ID3D11Device **)device;
            real_device_qi = patch_vtable_slot(*device, 0, device_qi);
            /* ID3D11Device slot 5 is CreateTexture2D. */
            real_create_texture2d = patch_vtable_slot(*device, 5, device_create_texture2d);
        }
        if (context && *context && !real_context_qi)
        {
            video_context = *(ID3D11DeviceContext **)context;
            real_context_qi = patch_vtable_slot(*context, 0, context_qi);
        }
    }
    return hr;
}

static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, REFIID, void **);
static HRESULT (WINAPI *real_open_shared_handle)(void *, HANDLE, REFIID, void **);

/*
 * D3D12 resources large enough to hold a frame.
 *
 * A title that renders in D3D12 may never touch the D3D11 video processor and
 * never ask for a shared handle: it can create its own committed resource and
 * expect to fill it. Nothing else in this bridge would see that, and from
 * outside it looks the same as a frame path that never ran. Log-only, bounded.
 *
 * D3D12_RESOURCE_DESC: Width is a UINT64 at offset 16, Height a UINT at 24,
 * Format a UINT at 32.
 */
static HRESULT (WINAPI *real_create_committed)(void *, const void *, UINT,
        const void *, UINT, const void *, REFIID, void **);

static HRESULT WINAPI d3d12_create_committed(void *self, const void *heap, UINT heap_flags,
        const void *desc, UINT state, const void *clear, REFIID iid, void **out)
{
    HRESULT hr = real_create_committed(self, heap, heap_flags, desc, state, clear,
                                       iid, out);
    if (desc)
    {
        static LONG told;
        const char *d = (const char *)desc;
        UINT w = (UINT)(*(const UINT64 *)(d + 16));
        UINT h = *(const UINT *)(d + 24);
        UINT fmt = *(const UINT *)(d + 32);
        (void)told;
        (void)told;
        if (w >= 640 && h >= 360 && shape_is_new(12, w, h, fmt))
            logf_("D3D12 CreateCommittedResource %ux%u format=%u -> 0x%08lx%s",
                  w, h, fmt, (unsigned long)hr, fmt == 103 ? "   << NV12" : "");

        if (SUCCEEDED(hr) && out && *out && frame_width && frame_height)
        {
            /*
             * Hold a reference, and take the newest pair.
             *
             * Keeping the pointer without a reference is a use-after-free the
             * moment the game drops the texture between clips, and keeping the
             * first pair for ever means the second cutscene is written into
             * resources nothing draws. Both are silent. AddRef fixes the
             * first; replacing on sight fixes the second.
             */
            if (fmt == DXGI_FORMAT_R8_UNORM && w == frame_width && h == frame_height
                && (ID3D12Resource *)*out != plane_y)
            {
                if (plane_y) ID3D12Resource_Release(plane_y);
                plane_y = (ID3D12Resource *)*out;
                ID3D12Resource_AddRef(plane_y);
                memcpy(&plane_y_desc, desc, sizeof(plane_y_desc));
                plane_rearm();
                logf_("plane: luma %ux%u", w, h);
            }
            else if (fmt == DXGI_FORMAT_R8G8_UNORM && w == frame_width / 2
                     && h == frame_height / 2 && (ID3D12Resource *)*out != plane_uv)
            {
                if (plane_uv) ID3D12Resource_Release(plane_uv);
                plane_uv = (ID3D12Resource *)*out;
                ID3D12Resource_AddRef(plane_uv);
                memcpy(&plane_uv_desc, desc, sizeof(plane_uv_desc));
                plane_rearm();
                logf_("plane: chroma %ux%u", w, h);
            }
        }
    }
    return hr;
}

static void plane_prepare(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap = { 0 };
    D3D12_RESOURCE_DESC desc = { 0 };
    UINT rows = 0;
    UINT64 row_bytes = 0, y_size = 0, uv_size = 0, uv_offset;

    if (plane_ready || !plane_y || !plane_uv) return;

    ID3D12Device_GetCopyableFootprints(device, &plane_y_desc, 0, 1, 0,
            &plane_y_fp, &rows, &row_bytes, &y_size);
    uv_offset = (y_size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)
                & ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    ID3D12Device_GetCopyableFootprints(device, &plane_uv_desc, 0, 1, uv_offset,
            &plane_uv_fp, &rows, &row_bytes, &uv_size);
    plane_upload_size = uv_offset + uv_size;

    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = plane_upload_size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(ID3D12Device_CreateCommittedResource(device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            &IID_ID3D12Resource, (void **)&plane_upload)))
    {
        logf_("plane: upload buffer refused, the planes stay empty");
        if (plane_y)  { ID3D12Resource_Release(plane_y);  plane_y = NULL; }
        if (plane_uv) { ID3D12Resource_Release(plane_uv); plane_uv = NULL; }
        return;
    }
    plane_ready = TRUE;
    logf_("plane: writing both planes directly, luma pitch %u chroma pitch %u",
          plane_y_fp.Footprint.RowPitch, plane_uv_fp.Footprint.RowPitch);
}

/* Copy queue: every resource is in COMMON there, so no barriers are needed. */
static void plane_write(const BYTE *nv12, UINT stride, UINT width, UINT height)
{
    D3D12_TEXTURE_COPY_LOCATION dst = { 0 }, src = { 0 };
    const BYTE *uv = nv12 + (size_t)stride * height;
    BYTE *mapped = NULL;
    UINT y;

    if (!plane_ready || !bridge_list || !bridge_queue) return;
    if (FAILED(ID3D12Resource_Map(plane_upload, 0, NULL, (void **)&mapped)) || !mapped)
        return;
    for (y = 0; y < height; ++y)
        memcpy(mapped + plane_y_fp.Offset + (size_t)y * plane_y_fp.Footprint.RowPitch,
               nv12 + (size_t)y * stride, width);
    for (y = 0; y < height / 2; ++y)
        memcpy(mapped + plane_uv_fp.Offset + (size_t)y * plane_uv_fp.Footprint.RowPitch,
               uv + (size_t)y * stride, width);
    ID3D12Resource_Unmap(plane_upload, 0, NULL);

    ID3D12CommandAllocator_Reset(bridge_alloc);
    ID3D12GraphicsCommandList_Reset(bridge_list, bridge_alloc, NULL);
    src.pResource = plane_upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = plane_y;  src.PlacedFootprint = plane_y_fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(bridge_list, &dst, 0, 0, 0, &src, NULL);
    dst.pResource = plane_uv; src.PlacedFootprint = plane_uv_fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(bridge_list, &dst, 0, 0, 0, &src, NULL);
    ID3D12GraphicsCommandList_Close(bridge_list);
    ID3D12CommandQueue_ExecuteCommandLists(bridge_queue, 1,
            (ID3D12CommandList *const *)&bridge_list);

    ++bridge_fence_value;
    if (SUCCEEDED(ID3D12CommandQueue_Signal(bridge_queue, bridge_fence, bridge_fence_value))
        && ID3D12Fence_GetCompletedValue(bridge_fence) < bridge_fence_value
        && bridge_event)
    {
        ID3D12Fence_SetEventOnCompletion(bridge_fence, bridge_fence_value, bridge_event);
        WaitForSingleObject(bridge_event, 1000);
    }
}

static HRESULT WINAPI d3d12_open_shared_handle(void *self, HANDLE handle,
                                               REFIID iid, void **out)
{
    {
        static LONG told;
        if (InterlockedIncrement(&told) <= 12)
            logf_("D3D12 OpenSharedHandle %p %s%s", handle, guid_text_(iid),
                  handle == BRIDGE_HANDLE ? "   << ours" : "");
    }
    if (handle == BRIDGE_HANDLE)
    {
        HRESULT hr;
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!IsEqualGUID(iid, &IID_ID3D12Resource)) return E_NOINTERFACE;

        EnterCriticalSection(&frame_lock);
        if (!bridge_texture && frame_width && frame_height)
        {
            /*
             * The size the game asked to share, when it said one.
             *
             * The handle it passes belongs to a texture it created itself, and
             * what comes back has to describe that texture -- not the clip.
             * Kingdom Hearts shares a 2048x1080 surface to carry a 1920x1080
             * frame, so a texture built to the clip's size is the wrong
             * resource under the same handle, and what the game copies out of
             * it is not a picture.
             */
            bridge_create((ID3D12Device *)self, frame_width, frame_height);
        }
        hr = bridge_texture ? S_OK : E_FAIL;
        if (bridge_texture)
        {
            ID3D12Resource_AddRef(bridge_texture);
            *out = bridge_texture;
        }
        LeaveCriticalSection(&frame_lock);
        return hr;
    }
    return real_open_shared_handle(self, handle, iid, out);
}

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT feature_level,
                                           REFIID iid, void **device)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, feature_level, iid, device);
    if (SUCCEEDED(hr) && device && *device && !real_open_shared_handle)
    {
        /* ID3D12Device slot 32 is OpenSharedHandle, 27 CreateCommittedResource. */
        real_open_shared_handle = patch_vtable_slot(*device, 32, d3d12_open_shared_handle);
        real_create_committed   = patch_vtable_slot(*device, 27, d3d12_create_committed);
        bridge_device = (ID3D12Device *)*device;
        d3d12_bridge_armed = TRUE;
        logf_("D3D12 device reached, bridge armed");
    }
    return hr;
}

/*
 * The game ships NVIDIA Streamline and asks sl.interposer.dll for
 * D3D12CreateDevice by name, so its own d3d12 import -- which it also has, by
 * ordinal -- is never used. Substituting here catches the call whichever
 * module it is asked of.
 */
static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI my_GetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC proc = real_GetProcAddress(module, name);

    if (proc && (ULONG_PTR)name > 0xFFFF
        && !lstrcmpA(name, "D3D12CreateDevice")
        && proc != (FARPROC)my_D3D12CreateDevice)
    {
        real_D3D12CreateDevice = (void *)proc;
        return (FARPROC)my_D3D12CreateDevice;
    }
    return proc;
}

/* --------------------------------------------------------- startup --- */

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    /* MGVF_VIDEO_DEVICE is gone: the video device is offered unconditionally
     * now, so the lever it used to open has nothing left to open. Leaving it
     * readable would only let a stale variable look meaningful. */
    stub_video_device.vtbl   = vd_vtbl;
    stub_video_context.vtbl  = vc_vtbl;
    stub_vp_enumerator.vtbl  = vpe_vtbl;
    stub_vp_processor.vtbl   = vp_vtbl;
    stub_vp_input_view.vtbl  = vpiv_vtbl;
    stub_vp_output_view.vtbl = vpov_vtbl;
    stub_dxgi_buffer.vtbl    = dxgibuf_vtbl;
    build_clamp_table();

    real_D3D11CreateDevice = hook_import("d3d11.dll", "D3D11CreateDevice",
                                         my_D3D11CreateDevice);
    /*
     * And through Streamline, for a game that never names d3d11 itself.
     *
     * Nioh 3 ships NVIDIA Streamline and imports D3D11CreateDevice from
     * sl.interposer.dll, which is a drop-in replacement exporting the same
     * name. Against "d3d11.dll" the hook finds nothing and the bridge reports
     * "d3d11 not imported" while the game goes on to create its device through
     * the interposer, unwatched. DYNASTY WARRIORS imports d3d11 directly, so
     * this only ever mattered for the second game to need this bridge.
     */
    if (!real_D3D11CreateDevice)
        real_D3D11CreateDevice = hook_import("sl.interposer.dll", "D3D11CreateDevice",
                                             my_D3D11CreateDevice);
    /*
     * D3D12 through the interposer too, and this half is not optional.
     *
     * The D3D12 hook exists to recognise BRIDGE_HANDLE coming back through
     * OpenSharedHandle. Without it the bridge still hands the game that
     * invented handle from GetSharedHandle, and the game passes it to a real
     * D3D12 device that has never heard of it -- 0xC0000005, which is exactly
     * what Nioh 3 did when only the D3D11 half was hooked here. Either both
     * halves are in place or neither may be.
     */
    if (!real_D3D12CreateDevice)
    {
        void *was = hook_import("sl.interposer.dll", "D3D12CreateDevice",
                                (void *)my_D3D12CreateDevice);
        if (was) real_D3D12CreateDevice = (HRESULT (WINAPI *)(void *, UINT, REFIID, void **))was;
    }
    /* And by ordinal, which is how d3d12 exports it and how a game that names
     * no module reaches it. */
    if (!real_D3D12CreateDevice)
    {
        void *was = hook_import_ordinal("d3d12.dll", 101, (void *)my_D3D12CreateDevice);
        if (was) real_D3D12CreateDevice = (HRESULT (WINAPI *)(void *, UINT, REFIID, void **))was;
    }
    /* MFCreateAttributes is called, not intercepted, so take its address --
     * hooking it with NULL would have replaced the game's own import with a
     * null pointer. */
    {
        HMODULE mfplat = LoadLibraryA("mfplat.dll");
        if (mfplat)
            real_MFCreateAttributes = (void *)real_GetProcAddress(mfplat, "MFCreateAttributes");
    }
    real_MFCreateSourceReaderFromByteStream =
        hook_import("MFReadWrite.dll", "MFCreateSourceReaderFromByteStream",
                    my_MFCreateSourceReaderFromByteStream);
    real_MFCreateSourceReaderFromURL =
        hook_import("MFReadWrite.dll", "MFCreateSourceReaderFromURL",
                    my_MFCreateSourceReaderFromURL);

    logf_("dwo-video-bridge: d3d11 %s, d3d12 %s, source reader %s",
          real_D3D11CreateDevice ? "hooked" : "not imported",
          real_D3D12CreateDevice ? "hooked at startup" : "waiting for GetProcAddress",
          (real_MFCreateSourceReaderFromByteStream && real_MFCreateSourceReaderFromURL)
            ? "FromByteStream and FromURL hooked"
            : real_MFCreateSourceReaderFromByteStream ? "FromByteStream hooked"
            : real_MFCreateSourceReaderFromURL ? "FromURL hooked" : "not imported");
    return 0;
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

        /*
         * This one cannot wait for the worker thread. Everything else is
         * hooked before it is used, because the game does not touch Media
         * Foundation until a cutscene starts -- but it resolves
         * D3D12CreateDevice during startup, and by the time a thread runs the
         * device already exists.
         *
         * Patching an import table is a write to memory the loader has
         * already mapped, which is safe here in a way that loading a library
         * or taking someone else's lock would not be.
         */
        real_GetProcAddress = hook_import("KERNEL32.dll", "GetProcAddress",
                                          my_GetProcAddress);

        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
