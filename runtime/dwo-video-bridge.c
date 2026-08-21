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

static void logf_(const char *fmt, ...)
{
    char buf[512];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
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

struct stub_object { void **vtbl; LONG refcount; };

static struct stub_object stub_video_device, stub_video_context;
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
static void *input_view_resource, *output_view_resource;
static D3D11_VIDEO_PROCESSOR_CONTENT_DESC vp_content_desc;

/* The generated vtables report which method was reached; a shipping build has
 * no use for that, and the file is generated so it is not edited to suit. */
#define stub_called(what) ((void)0)

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
static HRESULT WINAPI vc_VideoProcessorBlt(void *self, void *processor, void *output_view,
                                           UINT frame, UINT stream_count, const void *streams)
{
    (void)self; (void)processor; (void)output_view;
    (void)frame; (void)stream_count; (void)streams;
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
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    BRIDGE_CHECK(ID3D12Device_CreateCommittedResource(device, &heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            &IID_ID3D12Resource, (void **)&bridge_texture), "texture");

    ID3D12Device_GetCopyableFootprints(device, &desc, 0, 1, 0, &bridge_footprint,
            &bridge_rows, &bridge_row_bytes, &bridge_total_bytes);

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
    if (width != texture_width || height != texture_height) return;
    if (FAILED(ID3D12Resource_Map(bridge_upload, 0, NULL, (void **)&mapped)) || !mapped)
        return;
    nv12_to_bgra(nv12, stride, mapped, bridge_footprint.Footprint.RowPitch, width, height);
    ID3D12Resource_Unmap(bridge_upload, 0, NULL);

    ID3D12CommandAllocator_Reset(bridge_alloc);
    ID3D12GraphicsCommandList_Reset(bridge_list, bridge_alloc, NULL);
    dst.pResource = bridge_texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = bridge_upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = bridge_footprint;
    ID3D12GraphicsCommandList_CopyTextureRegion(bridge_list, &dst, 0, 0, 0, &src, NULL);
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
        bridge_upload_frame(data, stride, frame_width, frame_height);
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
    return real_set_media_type(self, stream, reserved, type);
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
    if (SUCCEEDED(hr) && sample && *sample) upload_frame(*sample);
    return hr;
}

static HRESULT (WINAPI *real_MFCreateAttributes)(IMFAttributes **, UINT32);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromByteStream)(IMFByteStream *,
        IMFAttributes *, IMFSourceReader **);

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
    HRESULT hr = E_FAIL;
    IMFAttributes *plain = NULL;
    UINT32 count = 0;

    if (attrs && real_MFCreateAttributes)
    {
        IMFAttributes_GetCount(attrs, &count);
        if (SUCCEEDED(real_MFCreateAttributes(&plain, count + 2)) && plain
            && SUCCEEDED(IMFAttributes_CopyAllItems(attrs, plain)))
        {
            IMFAttributes_DeleteItem(plain, &MF_SOURCE_READER_D3D_MANAGER);
            IMFAttributes_DeleteItem(plain, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS);
            hr = real_MFCreateSourceReaderFromByteStream(stream, plain, reader);
        }
        if (plain) IMFAttributes_Release(plain);
    }
    if (FAILED(hr))
        hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);

    if (SUCCEEDED(hr) && reader && *reader && !real_read_sample)
    {
        /* IMFSourceReader: 6 GetCurrentMediaType, 7 SetCurrentMediaType,
         * 9 ReadSample. */
        real_get_current_type = patch_vtable_slot(*reader, 6, reader_get_current_type);
        real_set_media_type   = patch_vtable_slot(*reader, 7, reader_set_media_type);
        real_read_sample      = patch_vtable_slot(*reader, 9, reader_read_sample);
    }
    return hr;
}

/* ------------------------------------------------------ the devices --- */

static HRESULT (WINAPI *real_device_qi)(void *, REFIID, void **);
static HRESULT (WINAPI *real_context_qi)(void *, REFIID, void **);

static HRESULT WINAPI device_qi(void *self, REFIID iid, void **out)
{
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
    if (FAILED(hr) && handle) { *handle = BRIDGE_HANDLE; return S_OK; }
    return hr;
}

static HRESULT WINAPI texture_qi(void *self, REFIID iid, void **out)
{
    HRESULT hr = real_texture_qi(self, iid, out);
    if (IsEqualGUID(iid, &iid_dxgi_resource) && SUCCEEDED(hr) && out && *out
        && !real_res_get_shared_handle)
        real_res_get_shared_handle = patch_vtable_slot(*out, 8, res_get_shared_handle);
    return hr;
}

static HRESULT (WINAPI *real_create_texture2d)(void *, const void *, const void *, void **);

/* The game's shared texture is the one whose handle has to work. */
static HRESULT WINAPI device_create_texture2d(void *self, const void *desc,
                                              const void *initial, void **texture)
{
    HRESULT hr = real_create_texture2d(self, desc, initial, texture);
    const UINT *d = desc;

    if (SUCCEEDED(hr) && d && texture && *texture && (d[10] & 2) && !real_texture_qi)
        real_texture_qi = patch_vtable_slot(*texture, 0, texture_qi);
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

static HRESULT WINAPI d3d12_open_shared_handle(void *self, HANDLE handle,
                                               REFIID iid, void **out)
{
    if (handle == BRIDGE_HANDLE)
    {
        HRESULT hr;
        if (!out) return E_INVALIDARG;
        *out = NULL;
        if (!IsEqualGUID(iid, &IID_ID3D12Resource)) return E_NOINTERFACE;

        EnterCriticalSection(&frame_lock);
        if (!bridge_texture && frame_width && frame_height)
            bridge_create((ID3D12Device *)self, frame_width, frame_height);
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
        /* ID3D12Device slot 32 is OpenSharedHandle. */
        real_open_shared_handle = patch_vtable_slot(*device, 32, d3d12_open_shared_handle);
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

    logf_("dwo-video-bridge: d3d11 %s, source reader %s",
          real_D3D11CreateDevice ? "hooked" : "not imported",
          real_MFCreateSourceReaderFromByteStream ? "hooked" : "not imported");
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
