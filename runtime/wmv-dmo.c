/*
 * wmv-dmo.c -- a DirectShow video decoder (DMO) for WMV1/2/3 and VC-1, on FFmpeg.
 *
 * Linked into mgvf-asf-reader.dll beside asf-reader.c. Nioh and Nioh 2 play
 * their cutscenes as WMV3 through DirectShow: wine's WM ASF Reader hands the
 * compressed stream to the DMO wrapper, which creates the WMVideo Decoder DMO,
 * CLSID {82D353DF-90BD-4382-8BC2-3F6192B76E34}. Wine's is a shim over a
 * GStreamer decoder, and where GStreamer has none -- every CrossOver without a
 * staged codec -- creating it fails and the graph never builds. The bridge
 * beside the game asks for this one only after that failure, so an engine
 * that can decode never reaches it.
 *
 * It is created aggregated, as the DMO wrapper creates its DMO, and speaks
 * IMediaObject: one input stream (WMV1/2/3 or WVC1 in a vih_t or
 * vih2_t), one output stream (NV12, YV12, IYUV/I420, YUY2, UYVY or
 * RGB32), frames in system memory.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <mediaobj.h>

#ifndef DMO_E_INVALIDSTREAMINDEX
#define DMO_E_INVALIDSTREAMINDEX ((HRESULT)0x80040201)
#define DMO_E_INVALIDTYPE        ((HRESULT)0x80040202)
#define DMO_E_TYPE_NOT_SET       ((HRESULT)0x80040203)
#define DMO_E_NOTACCEPTING       ((HRESULT)0x80040204)
#define DMO_E_TYPE_NOT_ACCEPTED  ((HRESULT)0x80040205)
#define DMO_E_NO_MORE_ITEMS      ((HRESULT)0x80040206)
#endif
#ifndef DMO_INPUT_STREAMF_WHOLE_SAMPLES
#define DMO_INPUT_STREAMF_WHOLE_SAMPLES              0x1
#define DMO_OUTPUT_STREAMF_WHOLE_SAMPLES             0x1
#define DMO_OUTPUT_STREAMF_SINGLE_SAMPLE_PER_BUFFER  0x2
#define DMO_OUTPUT_STREAMF_FIXED_SAMPLE_SIZE         0x4
#endif
#include <stdio.h>
#include <stdarg.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

#define LOGFILE "C:\\mgvf-asf-reader.log"

static LONG log_lines;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    HANDLE h;
    DWORD w;

    if (InterlockedIncrement(&log_lines) > 200) return;
    n = snprintf(buf, sizeof(buf), "[wmv-dmo] ");
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

/* DirectShow's two video format blocks, laid out here: the headers that carry
 * them are not the same across toolchains. */
typedef struct
{
    RECT rcSource, rcTarget;
    DWORD dwBitRate, dwBitErrorRate;
    REFERENCE_TIME AvgTimePerFrame;
    BITMAPINFOHEADER bmiHeader;
} vih_t;
typedef struct
{
    RECT rcSource, rcTarget;
    DWORD dwBitRate, dwBitErrorRate;
    REFERENCE_TIME AvgTimePerFrame;
    DWORD dwInterlaceFlags, dwCopyProtectFlags, dwPictAspectRatioX, dwPictAspectRatioY;
    DWORD dwControlFlags, dwReserved2;
    BITMAPINFOHEADER bmiHeader;
} vih2_t;

#define FCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

/* The few GUIDs this needs, spelled out so nothing depends on which import
 * library carries them. */
#define FOURCC_GUID(a, b, c, d) \
    { (DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24), \
      0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } }
static const GUID mt_video     = FOURCC_GUID('v', 'i', 'd', 's');
static const GUID fmt_vih      = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
static const GUID fmt_vih2     = { 0xf72a76a0, 0xeb0a, 0x11d0, { 0xac, 0xe4, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba } };
static const GUID iid_unknown  = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID iid_mediaobj = { 0xd8ad0f58, 0x5494, 0x4102, { 0x97, 0xc5, 0xec, 0x79, 0x8e, 0x59, 0xbc, 0xf4 } };

static const struct { GUID sub; enum AVCodecID id; } inputs[] =
{
    { FOURCC_GUID('W', 'M', 'V', '3'), AV_CODEC_ID_WMV3 },
    { FOURCC_GUID('W', 'M', 'V', '2'), AV_CODEC_ID_WMV2 },
    { FOURCC_GUID('W', 'M', 'V', '1'), AV_CODEC_ID_WMV1 },
    { FOURCC_GUID('W', 'V', 'C', '1'), AV_CODEC_ID_VC1 },
};

enum out_kind { OUT_NV12, OUT_YV12, OUT_IYUV, OUT_I420, OUT_YUY2, OUT_UYVY, OUT_RGB32 };
static const struct { GUID sub; DWORD fourcc; WORD bits; } outputs[] =
{
    { FOURCC_GUID('N', 'V', '1', '2'), FCC('N', 'V', '1', '2'), 12 },
    { FOURCC_GUID('Y', 'V', '1', '2'), FCC('Y', 'V', '1', '2'), 12 },
    { FOURCC_GUID('I', 'Y', 'U', 'V'), FCC('I', 'Y', 'U', 'V'), 12 },
    { FOURCC_GUID('I', '4', '2', '0'), FCC('I', '4', '2', '0'), 12 },
    { FOURCC_GUID('Y', 'U', 'Y', '2'), FCC('Y', 'U', 'Y', '2'), 16 },
    { FOURCC_GUID('U', 'Y', 'V', 'Y'), FCC('U', 'Y', 'V', 'Y'), 16 },
    { { 0xe436eb7e, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } }, BI_RGB, 32 },
};
#define N_OUTPUTS (sizeof(outputs) / sizeof(outputs[0]))

struct dmo
{
    IUnknown inner;
    IMediaObject iface;
    IUnknown *outer;
    LONG ref;
    CRITICAL_SECTION cs;
    DMO_MEDIA_TYPE in_mt, out_mt;
    BOOL in_set, out_set;
    enum AVCodecID codec;
    LONG width, height;
    REFERENCE_TIME avgtime;
    int out;                    /* index into outputs[] */
    LONG stride;                /* bytes for packed, luma bytes for planar */
    BOOL bottom_up;             /* RGB with a positive height */
    DWORD out_size;
    AVCodecContext *dec;
    AVPacket *pkt;
    AVFrame *frame;
    BOOL have_frame;
    LONG frames;
};

static struct dmo *from_inner(IUnknown *i) { return CONTAINING_RECORD(i, struct dmo, inner); }
static struct dmo *from_iface(IMediaObject *i) { return CONTAINING_RECORD(i, struct dmo, iface); }

/* ------------------------------------------------------------ media types */

static const char *guid_str(const GUID *g, char *buf)
{
    sprintf(buf, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned long)g->Data1,
            g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
            g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return buf;
}

/* What a caller offered, in one line: the negotiation fails silently otherwise,
 * and "Failure to play movie" is all the game says about it. */
static void log_type(const char *what, const DMO_MEDIA_TYPE *mt, DWORD flags, HRESULT hr, const char *why)
{
    static LONG said;
    char a[40], b[40], c[40];
    if (InterlockedIncrement(&said) > 40 || !mt) return;
    logf_("%s flags %lu: major %s sub %s format %s (%lu bytes) -> 0x%08lx%s%s", what,
          (unsigned long)flags, guid_str(&mt->majortype, a), guid_str(&mt->subtype, b),
          guid_str(&mt->formattype, c), (unsigned long)mt->cbFormat, (unsigned long)hr,
          why ? " -- " : "", why ? why : "");
}

static void free_mt(DMO_MEDIA_TYPE *mt)
{
    if (mt->pbFormat) CoTaskMemFree(mt->pbFormat);
    if (mt->pUnk) IUnknown_Release(mt->pUnk);
    memset(mt, 0, sizeof(*mt));
}

static HRESULT copy_mt(DMO_MEDIA_TYPE *dst, const DMO_MEDIA_TYPE *src)
{
    *dst = *src;
    dst->pbFormat = NULL;
    if (src->cbFormat)
    {
        if (!(dst->pbFormat = CoTaskMemAlloc(src->cbFormat))) return E_OUTOFMEMORY;
        memcpy(dst->pbFormat, src->pbFormat, src->cbFormat);
    }
    if (dst->pUnk) IUnknown_AddRef(dst->pUnk);
    return S_OK;
}

static BOOL input_geometry(const DMO_MEDIA_TYPE *mt, LONG *w, LONG *h, REFERENCE_TIME *avg,
                           const BYTE **extra, int *extra_len)
{
    const BITMAPINFOHEADER *bih;
    size_t at;
    if (IsEqualGUID(&mt->formattype, &fmt_vih) && mt->cbFormat >= sizeof(vih_t))
    {
        const vih_t *v = (const vih_t *)mt->pbFormat;
        bih = &v->bmiHeader; *avg = v->AvgTimePerFrame; at = offsetof(vih_t, bmiHeader);
    }
    else if (IsEqualGUID(&mt->formattype, &fmt_vih2) && mt->cbFormat >= sizeof(vih2_t))
    {
        const vih2_t *v = (const vih2_t *)mt->pbFormat;
        bih = &v->bmiHeader; *avg = v->AvgTimePerFrame; at = offsetof(vih2_t, bmiHeader);
    }
    else return FALSE;
    *w = bih->biWidth;
    *h = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight;
    /* The codec's private data follows the BITMAPINFOHEADER: WMV3's four-byte
     * sequence header, which the decoder cannot start without. */
    *extra = NULL; *extra_len = 0;
    if (bih->biSize > sizeof(BITMAPINFOHEADER) && at + bih->biSize <= mt->cbFormat)
    {
        *extra = (const BYTE *)bih + sizeof(BITMAPINFOHEADER);
        *extra_len = (int)(bih->biSize - sizeof(BITMAPINFOHEADER));
    }
    else if (at + sizeof(BITMAPINFOHEADER) < mt->cbFormat)
    {
        *extra = mt->pbFormat + at + sizeof(BITMAPINFOHEADER);
        *extra_len = (int)(mt->cbFormat - at - sizeof(BITMAPINFOHEADER));
    }
    return *w > 0 && *h > 0;
}

static DWORD image_size(int out, LONG w, LONG h)
{
    if (outputs[out].bits == 12) return (DWORD)(w * h + 2 * ((w + 1) / 2) * ((h + 1) / 2));
    return (DWORD)(w * (outputs[out].bits / 8) * h);
}

static HRESULT make_output_type(struct dmo *d, int out, DMO_MEDIA_TYPE *mt)
{
    vih_t *v;
    memset(mt, 0, sizeof(*mt));
    if (!(v = CoTaskMemAlloc(sizeof(*v)))) return E_OUTOFMEMORY;
    memset(v, 0, sizeof(*v));
    v->AvgTimePerFrame = d->avgtime;
    v->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    v->bmiHeader.biWidth = d->width;
    v->bmiHeader.biHeight = d->height;
    v->bmiHeader.biPlanes = 1;
    v->bmiHeader.biBitCount = outputs[out].bits;
    v->bmiHeader.biCompression = outputs[out].fourcc;
    v->bmiHeader.biSizeImage = image_size(out, d->width, d->height);
    mt->majortype = mt_video;
    mt->subtype = outputs[out].sub;
    mt->bFixedSizeSamples = TRUE;
    mt->lSampleSize = v->bmiHeader.biSizeImage;
    mt->formattype = fmt_vih;
    mt->cbFormat = sizeof(*v);
    mt->pbFormat = (BYTE *)v;
    return S_OK;
}

/* ------------------------------------------------------------ conversion */

/* Planar 4:2:0 into one of the layouts above. Shared with wm-reader.c, which
 * hands the same kinds to DirectShow's video renderer. `stride` is the luma
 * row in bytes for the planar kinds and the row in bytes for packed ones;
 * `height` is the picture's, which places the chroma planes. */
struct conv { int out; LONG stride; LONG width, height; BOOL bottom_up; };

void mgvf_write_420(int out, LONG stride, LONG width, LONG height, BOOL bottom_up,
                    const AVFrame *f, BYTE *dst);

static void write_frame(struct dmo *d, const AVFrame *f, BYTE *dst)
{
    mgvf_write_420(d->out, d->stride, d->width, d->height, d->bottom_up, f, dst);
}

void mgvf_write_420(int out, LONG stride, LONG width, LONG height, BOOL bottom_up,
                    const AVFrame *f, BYTE *dst)
{
    struct conv cv = { out, stride, width, height, bottom_up }, *d = &cv;
    LONG w = d->width < f->width ? d->width : f->width;
    LONG h = d->height < f->height ? d->height : f->height;
    LONG cw = (w + 1) / 2, ch = (h + 1) / 2, x, y;
    LONG ys = d->stride, cs = d->stride / 2;
    const uint8_t *Y = f->data[0], *U = f->data[1], *V = f->data[2];
    int ly = f->linesize[0], lu = f->linesize[1], lv = f->linesize[2];
    BYTE *p;

    switch (d->out)
    {
    case OUT_NV12:
        for (y = 0; y < h; y++) memcpy(dst + y * ys, Y + y * ly, w);
        p = dst + ys * d->height;
        for (y = 0; y < ch; y++)
            for (x = 0; x < cw; x++) { p[y * ys + 2 * x] = U[y * lu + x]; p[y * ys + 2 * x + 1] = V[y * lv + x]; }
        break;
    case OUT_YV12: case OUT_IYUV: case OUT_I420:
    {
        const uint8_t *first = d->out == OUT_YV12 ? V : U, *second = d->out == OUT_YV12 ? U : V;
        int l1 = d->out == OUT_YV12 ? lv : lu, l2 = d->out == OUT_YV12 ? lu : lv;
        BYTE *p1 = dst + ys * d->height, *p2 = p1 + cs * ((d->height + 1) / 2);
        for (y = 0; y < h; y++) memcpy(dst + y * ys, Y + y * ly, w);
        for (y = 0; y < ch; y++) { memcpy(p1 + y * cs, first + y * l1, cw); memcpy(p2 + y * cs, second + y * l2, cw); }
        break;
    }
    case OUT_YUY2: case OUT_UYVY:
        for (y = 0; y < h; y++)
        {
            BYTE *row = dst + y * ys;
            const uint8_t *yy = Y + y * ly, *uu = U + (y / 2) * lu, *vv = V + (y / 2) * lv;
            for (x = 0; x < w / 2; x++)
            {
                if (d->out == OUT_YUY2)
                { row[4 * x] = yy[2 * x]; row[4 * x + 1] = uu[x]; row[4 * x + 2] = yy[2 * x + 1]; row[4 * x + 3] = vv[x]; }
                else
                { row[4 * x] = uu[x]; row[4 * x + 1] = yy[2 * x]; row[4 * x + 2] = vv[x]; row[4 * x + 3] = yy[2 * x + 1]; }
            }
        }
        break;
    case OUT_RGB32:
        /* BT.601, limited range, as WMV is. */
        for (y = 0; y < h; y++)
        {
            BYTE *row = dst + (d->bottom_up ? (d->height - 1 - y) : y) * ys;
            const uint8_t *yy = Y + y * ly, *uu = U + (y / 2) * lu, *vv = V + (y / 2) * lv;
            for (x = 0; x < w; x++)
            {
                int c = 298 * (yy[x] - 16), u = uu[x / 2] - 128, v = vv[x / 2] - 128;
                int r = (c + 409 * v + 128) >> 8, g = (c - 100 * u - 208 * v + 128) >> 8, b = (c + 516 * u + 128) >> 8;
                row[4 * x]     = (BYTE)(b < 0 ? 0 : b > 255 ? 255 : b);
                row[4 * x + 1] = (BYTE)(g < 0 ? 0 : g > 255 ? 255 : g);
                row[4 * x + 2] = (BYTE)(r < 0 ? 0 : r > 255 ? 255 : r);
                row[4 * x + 3] = 0xff;
            }
        }
        break;
    }
}

/* ---------------------------------------------------------------- IUnknown */

static HRESULT WINAPI inner_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct dmo *d = from_inner(iface);
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &iid_unknown)) *out = &d->inner;
    else if (IsEqualGUID(iid, &iid_mediaobj)) *out = &d->iface;
    else
    {
        static LONG said;
        char g[40];
        if (InterlockedIncrement(&said) <= 10) logf_("asked for interface %s -- not here", guid_str(iid, g));
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI inner_AddRef(IUnknown *iface)
{
    return InterlockedIncrement(&from_inner(iface)->ref);
}

static ULONG WINAPI inner_Release(IUnknown *iface)
{
    struct dmo *d = from_inner(iface);
    ULONG ref = InterlockedDecrement(&d->ref);
    if (!ref)
    {
        if (d->dec) avcodec_free_context(&d->dec);
        if (d->pkt) av_packet_free(&d->pkt);
        if (d->frame) av_frame_free(&d->frame);
        free_mt(&d->in_mt);
        free_mt(&d->out_mt);
        DeleteCriticalSection(&d->cs);
        free(d);
    }
    return ref;
}

static IUnknownVtbl inner_vtbl = { inner_QueryInterface, inner_AddRef, inner_Release };

/* ------------------------------------------------------------ IMediaObject */

static HRESULT WINAPI mo_QueryInterface(IMediaObject *iface, REFIID iid, void **out)
{
    return IUnknown_QueryInterface(from_iface(iface)->outer, iid, out);
}
static ULONG WINAPI mo_AddRef(IMediaObject *iface) { return IUnknown_AddRef(from_iface(iface)->outer); }
static ULONG WINAPI mo_Release(IMediaObject *iface) { return IUnknown_Release(from_iface(iface)->outer); }

static HRESULT WINAPI mo_GetStreamCount(IMediaObject *iface, DWORD *in, DWORD *out)
{
    if (!in || !out) return E_POINTER;
    *in = 1; *out = 1;
    return S_OK;
}

static HRESULT WINAPI mo_GetInputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (!flags) return E_POINTER;
    *flags = DMO_INPUT_STREAMF_WHOLE_SAMPLES;
    return S_OK;
}

static HRESULT WINAPI mo_GetOutputStreamInfo(IMediaObject *iface, DWORD index, DWORD *flags)
{
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (!flags) return E_POINTER;
    *flags = DMO_OUTPUT_STREAMF_WHOLE_SAMPLES | DMO_OUTPUT_STREAMF_SINGLE_SAMPLE_PER_BUFFER |
             DMO_OUTPUT_STREAMF_FIXED_SAMPLE_SIZE;
    return S_OK;
}

static HRESULT WINAPI mo_GetInputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *mt)
{
    static LONG said;
    if (InterlockedIncrement(&said) <= 8) logf_("GetInputType(%lu)", (unsigned long)type_index);
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (type_index >= sizeof(inputs) / sizeof(inputs[0])) return DMO_E_NO_MORE_ITEMS;
    if (!mt) return S_OK;
    memset(mt, 0, sizeof(*mt));
    mt->majortype = mt_video;
    mt->subtype = inputs[type_index].sub;
    return S_OK;
}

static HRESULT WINAPI mo_GetOutputType(IMediaObject *iface, DWORD index, DWORD type_index, DMO_MEDIA_TYPE *mt)
{
    struct dmo *d = from_iface(iface);
    HRESULT hr;
    static LONG said;
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    EnterCriticalSection(&d->cs);
    if (!d->in_set) hr = DMO_E_TYPE_NOT_SET;
    else if (type_index >= N_OUTPUTS) hr = DMO_E_NO_MORE_ITEMS;
    else hr = mt ? make_output_type(d, (int)type_index, mt) : S_OK;
    LeaveCriticalSection(&d->cs);
    if (InterlockedIncrement(&said) <= 12) logf_("GetOutputType(%lu) -> 0x%08lx", (unsigned long)type_index, (unsigned long)hr);
    return hr;
}

static HRESULT WINAPI mo_SetInputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *mt, DWORD flags)
{
    struct dmo *d = from_iface(iface);
    LONG w, h;
    REFERENCE_TIME avg = 0;
    const BYTE *extra;
    int extra_len, i, found = -1;
    const AVCodec *codec;

    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (flags & DMO_SET_TYPEF_CLEAR)
    {
        EnterCriticalSection(&d->cs);
        free_mt(&d->in_mt); d->in_set = FALSE;
        LeaveCriticalSection(&d->cs);
        return S_OK;
    }
    if (!mt) return E_POINTER;
    if (!IsEqualGUID(&mt->majortype, &mt_video))
    { log_type("SetInputType", mt, flags, DMO_E_TYPE_NOT_ACCEPTED, "not video"); return DMO_E_TYPE_NOT_ACCEPTED; }
    for (i = 0; i < (int)(sizeof(inputs) / sizeof(inputs[0])); i++)
        if (IsEqualGUID(&mt->subtype, &inputs[i].sub)) found = i;
    if (found < 0)
    { log_type("SetInputType", mt, flags, DMO_E_TYPE_NOT_ACCEPTED, "subtype not WMV1/2/3 or WVC1"); return DMO_E_TYPE_NOT_ACCEPTED; }
    if (!input_geometry(mt, &w, &h, &avg, &extra, &extra_len))
    { log_type("SetInputType", mt, flags, DMO_E_TYPE_NOT_ACCEPTED, "no usable video format block"); return DMO_E_TYPE_NOT_ACCEPTED; }
    if (flags & DMO_SET_TYPEF_TEST_ONLY) { log_type("SetInputType", mt, flags, S_OK, "test only"); return S_OK; }

    EnterCriticalSection(&d->cs);
    if (d->dec) avcodec_free_context(&d->dec);
    codec = avcodec_find_decoder(inputs[found].id);
    d->dec = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!d->dec) { LeaveCriticalSection(&d->cs); return E_OUTOFMEMORY; }
    d->dec->width = w;
    d->dec->height = h;
    d->dec->pkt_timebase = (AVRational){ 1, 10000000 };
    d->dec->thread_count = 1;
    if (extra_len > 0 && (d->dec->extradata = av_mallocz(extra_len + AV_INPUT_BUFFER_PADDING_SIZE)))
    {
        memcpy(d->dec->extradata, extra, extra_len);
        d->dec->extradata_size = extra_len;
    }
    if (avcodec_open2(d->dec, codec, NULL) < 0)
    {
        avcodec_free_context(&d->dec);
        LeaveCriticalSection(&d->cs);
        logf_("the %s decoder would not open on this type", codec->name);
        return DMO_E_TYPE_NOT_ACCEPTED;
    }
    free_mt(&d->in_mt);
    copy_mt(&d->in_mt, mt);
    d->in_set = TRUE;
    d->codec = inputs[found].id;
    d->width = w; d->height = h;
    d->avgtime = avg > 0 ? avg : 333667;
    d->have_frame = FALSE;
    LeaveCriticalSection(&d->cs);
    logf_("input: %s %ldx%ld, %d bytes of codec data", codec->name, w, h, extra_len);
    return S_OK;
}

static HRESULT WINAPI mo_SetOutputType(IMediaObject *iface, DWORD index, const DMO_MEDIA_TYPE *mt, DWORD flags)
{
    struct dmo *d = from_iface(iface);
    int i, found = -1;
    LONG stride;
    BOOL bottom_up = FALSE;

    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (flags & DMO_SET_TYPEF_CLEAR)
    {
        EnterCriticalSection(&d->cs);
        free_mt(&d->out_mt); d->out_set = FALSE;
        LeaveCriticalSection(&d->cs);
        return S_OK;
    }
    if (!mt) return E_POINTER;
    if (!d->in_set) { log_type("SetOutputType", mt, flags, DMO_E_TYPE_NOT_SET, "no input type yet"); return DMO_E_TYPE_NOT_SET; }
    if (!IsEqualGUID(&mt->majortype, &mt_video))
    { log_type("SetOutputType", mt, flags, DMO_E_TYPE_NOT_ACCEPTED, "not video"); return DMO_E_TYPE_NOT_ACCEPTED; }
    for (i = 0; i < (int)N_OUTPUTS; i++)
        if (IsEqualGUID(&mt->subtype, &outputs[i].sub)) { found = i; break; }
    if (found < 0)
    { log_type("SetOutputType", mt, flags, DMO_E_TYPE_NOT_ACCEPTED, "subtype not offered"); return DMO_E_TYPE_NOT_ACCEPTED; }

    /* A renderer may ask for a wider stride than the picture, by biWidth. */
    stride = d->width;
    if ((IsEqualGUID(&mt->formattype, &fmt_vih) && mt->cbFormat >= sizeof(vih_t)) ||
        (IsEqualGUID(&mt->formattype, &fmt_vih2) && mt->cbFormat >= sizeof(vih2_t)))
    {
        const BITMAPINFOHEADER *bih = IsEqualGUID(&mt->formattype, &fmt_vih)
            ? &((const vih_t *)mt->pbFormat)->bmiHeader
            : &((const vih2_t *)mt->pbFormat)->bmiHeader;
        if (bih->biWidth > stride) stride = bih->biWidth;
        if (bih->biHeight > 0 && found == OUT_RGB32) bottom_up = TRUE;
    }
    else if (found == OUT_RGB32) bottom_up = TRUE;
    if (flags & DMO_SET_TYPEF_TEST_ONLY) { log_type("SetOutputType", mt, flags, S_OK, "test only"); return S_OK; }

    EnterCriticalSection(&d->cs);
    free_mt(&d->out_mt);
    copy_mt(&d->out_mt, mt);
    d->out_set = TRUE;
    d->out = found;
    d->bottom_up = bottom_up;
    d->stride = outputs[found].bits == 12 ? stride : stride * (outputs[found].bits / 8);
    d->out_size = outputs[found].bits == 12
        ? (DWORD)(stride * d->height + stride * ((d->height + 1) / 2))
        : (DWORD)(d->stride * d->height);
    LeaveCriticalSection(&d->cs);
    logf_("output: subtype %d, stride %ld, %lu bytes a frame%s", found, d->stride,
          (unsigned long)d->out_size, bottom_up ? ", bottom-up" : "");
    return S_OK;
}

static HRESULT WINAPI mo_GetInputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *mt)
{
    struct dmo *d = from_iface(iface);
    HRESULT hr;
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    EnterCriticalSection(&d->cs);
    hr = d->in_set ? copy_mt(mt, &d->in_mt) : DMO_E_TYPE_NOT_SET;
    LeaveCriticalSection(&d->cs);
    return hr;
}

static HRESULT WINAPI mo_GetOutputCurrentType(IMediaObject *iface, DWORD index, DMO_MEDIA_TYPE *mt)
{
    struct dmo *d = from_iface(iface);
    HRESULT hr;
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    EnterCriticalSection(&d->cs);
    hr = d->out_set ? copy_mt(mt, &d->out_mt) : DMO_E_TYPE_NOT_SET;
    LeaveCriticalSection(&d->cs);
    return hr;
}

static HRESULT WINAPI mo_GetInputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size,
                                          DWORD *lookahead, DWORD *alignment)
{
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (size) *size = 0;
    if (lookahead) *lookahead = 0;
    if (alignment) *alignment = 1;
    return S_OK;
}

static HRESULT WINAPI mo_GetOutputSizeInfo(IMediaObject *iface, DWORD index, DWORD *size, DWORD *alignment)
{
    struct dmo *d = from_iface(iface);
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (!d->out_set) return DMO_E_TYPE_NOT_SET;
    if (size) *size = d->out_size;
    if (alignment) *alignment = 1;
    return S_OK;
}

static HRESULT WINAPI mo_GetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME *latency)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI mo_SetInputMaxLatency(IMediaObject *iface, DWORD index, REFERENCE_TIME latency)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI mo_Flush(IMediaObject *iface)
{
    struct dmo *d = from_iface(iface);
    EnterCriticalSection(&d->cs);
    if (d->dec) avcodec_flush_buffers(d->dec);
    if (d->have_frame) av_frame_unref(d->frame);
    d->have_frame = FALSE;
    LeaveCriticalSection(&d->cs);
    return S_OK;
}

static HRESULT WINAPI mo_Discontinuity(IMediaObject *iface, DWORD index)
{
    return index ? DMO_E_INVALIDSTREAMINDEX : S_OK;
}

static HRESULT WINAPI mo_AllocateStreamingResources(IMediaObject *iface) { return S_OK; }
static HRESULT WINAPI mo_FreeStreamingResources(IMediaObject *iface) { return S_OK; }

static HRESULT WINAPI mo_GetInputStatus(IMediaObject *iface, DWORD index, DWORD *flags)
{
    struct dmo *d = from_iface(iface);
    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (!flags) return E_POINTER;
    *flags = d->have_frame ? 0 : DMO_INPUT_STATUSF_ACCEPT_DATA;
    return S_OK;
}

/* Take the next decoded frame, if the decoder has one, into d->frame. */
static void pull(struct dmo *d)
{
    if (!d->have_frame && d->dec && avcodec_receive_frame(d->dec, d->frame) >= 0)
        d->have_frame = TRUE;
}

static HRESULT WINAPI mo_ProcessInput(IMediaObject *iface, DWORD index, IMediaBuffer *buffer,
                                      DWORD flags, REFERENCE_TIME time, REFERENCE_TIME length)
{
    struct dmo *d = from_iface(iface);
    BYTE *data = NULL;
    DWORD len = 0;
    int e;

    if (index) return DMO_E_INVALIDSTREAMINDEX;
    if (!buffer) return E_POINTER;
    EnterCriticalSection(&d->cs);
    if (!d->in_set || !d->out_set || !d->dec) { LeaveCriticalSection(&d->cs); return DMO_E_TYPE_NOT_SET; }
    if (d->have_frame) { LeaveCriticalSection(&d->cs); return DMO_E_NOTACCEPTING; }
    if (FAILED(IMediaBuffer_GetBufferAndLength(buffer, &data, &len)) || !len)
    { LeaveCriticalSection(&d->cs); return S_OK; }
    if (av_new_packet(d->pkt, (int)len) < 0) { LeaveCriticalSection(&d->cs); return E_OUTOFMEMORY; }
    memcpy(d->pkt->data, data, len);
    d->pkt->pts = (flags & DMO_INPUT_DATA_BUFFERF_TIME) ? time : AV_NOPTS_VALUE;
    d->pkt->duration = (flags & DMO_INPUT_DATA_BUFFERF_TIMELENGTH) ? length : 0;
    if (flags & DMO_INPUT_DATA_BUFFERF_SYNCPOINT) d->pkt->flags |= AV_PKT_FLAG_KEY;
    e = avcodec_send_packet(d->dec, d->pkt);
    av_packet_unref(d->pkt);
    pull(d);
    LeaveCriticalSection(&d->cs);
    if (e < 0 && e != AVERROR(EAGAIN))
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 3) logf_("the decoder refused a packet (%d)", e);
    }
    return S_OK;
}

static HRESULT WINAPI mo_ProcessOutput(IMediaObject *iface, DWORD flags, DWORD count,
                                       DMO_OUTPUT_DATA_BUFFER *buffers, DWORD *status)
{
    struct dmo *d = from_iface(iface);
    BYTE *dst = NULL;
    DWORD len = 0, max = 0;
    int64_t ts;

    if (status) *status = 0;
    if (count != 1 || !buffers) return E_INVALIDARG;
    buffers[0].dwStatus = 0;
    EnterCriticalSection(&d->cs);
    pull(d);
    if (!d->have_frame) { LeaveCriticalSection(&d->cs); return S_FALSE; }
    if (!buffers[0].pBuffer)
    {
        /* Asked to discard. */
        av_frame_unref(d->frame); d->have_frame = FALSE;
        LeaveCriticalSection(&d->cs);
        return S_OK;
    }
    if (FAILED(IMediaBuffer_GetBufferAndLength(buffers[0].pBuffer, &dst, &len)) ||
        FAILED(IMediaBuffer_GetMaxLength(buffers[0].pBuffer, &max)) || max < d->out_size)
    {
        LeaveCriticalSection(&d->cs);
        logf_("the output buffer holds %lu bytes, a frame needs %lu", (unsigned long)max,
              (unsigned long)d->out_size);
        return E_FAIL;
    }
    if (d->frame->format == AV_PIX_FMT_YUV420P || d->frame->format == AV_PIX_FMT_YUVJ420P)
        write_frame(d, d->frame, dst);
    IMediaBuffer_SetLength(buffers[0].pBuffer, d->out_size);
    ts = d->frame->best_effort_timestamp;
    buffers[0].dwStatus = DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT;
    if (ts != AV_NOPTS_VALUE)
    {
        buffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIME | DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH;
        buffers[0].rtTimestamp = ts;
        buffers[0].rtTimelength = d->frame->duration > 0 ? d->frame->duration : d->avgtime;
    }
    av_frame_unref(d->frame);
    d->have_frame = FALSE;
    pull(d);
    if (d->have_frame) buffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE;
    {
        LONG n = InterlockedIncrement(&d->frames);
        if (n == 1 || n == 100 || n == 1000)
            logf_("frame %ld out, %lld ms", n, ts == AV_NOPTS_VALUE ? -1LL : (long long)(ts / 10000));
    }
    LeaveCriticalSection(&d->cs);
    return S_OK;
}

static HRESULT WINAPI mo_Lock(IMediaObject *iface, LONG lock)
{
    struct dmo *d = from_iface(iface);
    if (lock) EnterCriticalSection(&d->cs); else LeaveCriticalSection(&d->cs);
    return S_OK;
}

static IMediaObjectVtbl mo_vtbl =
{
    mo_QueryInterface, mo_AddRef, mo_Release,
    mo_GetStreamCount, mo_GetInputStreamInfo, mo_GetOutputStreamInfo,
    mo_GetInputType, mo_GetOutputType, mo_SetInputType, mo_SetOutputType,
    mo_GetInputCurrentType, mo_GetOutputCurrentType,
    mo_GetInputSizeInfo, mo_GetOutputSizeInfo,
    mo_GetInputMaxLatency, mo_SetInputMaxLatency,
    mo_Flush, mo_Discontinuity,
    mo_AllocateStreamingResources, mo_FreeStreamingResources,
    mo_GetInputStatus, mo_ProcessInput, mo_ProcessOutput, mo_Lock,
};

/* Created the way CoCreateInstance would create it: aggregated when an outer
 * unknown is given, in which case only IUnknown may be asked for. */
__declspec(dllexport) HRESULT WINAPI MGVF_CreateWmvDmo(IUnknown *outer, REFIID iid, void **out)
{
    struct dmo *d;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (outer && !IsEqualGUID(iid, &iid_unknown)) return CLASS_E_NOAGGREGATION;
    if (!(d = calloc(1, sizeof(*d)))) return E_OUTOFMEMORY;
    d->inner.lpVtbl = &inner_vtbl;
    d->iface.lpVtbl = &mo_vtbl;
    d->outer = outer ? outer : &d->inner;
    d->ref = 1;
    InitializeCriticalSection(&d->cs);
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->pkt || !d->frame) { IUnknown_Release(&d->inner); return E_OUTOFMEMORY; }
    hr = IUnknown_QueryInterface(&d->inner, iid, out);
    IUnknown_Release(&d->inner);
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 3)
            logf_("created%s -> 0x%08lx", outer ? " (aggregated)" : "", (unsigned long)hr);
    }
    return hr;
}
