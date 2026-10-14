/*
 * asf-reader.c -- an IMFSourceReader for ASF files, decoded by FFmpeg.
 *
 * Built into mgvf-asf-reader.dll, which sits beside a game and is loaded by
 * that game's bridge ONLY when Media Foundation itself refuses a stream. On an
 * engine that can open the file -- stable CrossOver 26.3 with the VC-1 codec
 * staged -- this code never runs.
 *
 * WHY IT EXISTS. Persona 5 Strikers' cutscenes are VC-1 (WVC1) with WMA Pro
 * audio, in ASF. No CrossOver ships a decoder for either: not 26.3 and not
 * Preview 27, whose GStreamer decodes only what VideoToolbox can, and
 * VideoToolbox has no Windows Media codecs. Without one, wine's ASF media
 * source cannot be created at all, and MFCreateSourceReaderFromByteStream
 * answers MF_E_UNSUPPORTED_BYTESTREAM_TYPE (measured on Preview 27, 271 times
 * in one session). Staging libav into the engine fixes it (measured on Preview
 * 27, 2026-10-14) but that is an engine change. This is the same reader, in the
 * game folder: FFmpeg's ASF demuxer and its VC-1 / WMV3 / WMV2 / WMV1 /
 * WMA Pro / WMA v2 decoders, statically linked, and nothing else of FFmpeg.
 * WMV2 with WMA v2 is NieR Replicant's.
 *
 * WebM with VP9 came later (Nioh 3): Preview 27's own decoder, VideoToolbox
 * through GStreamer, draws magenta blocks into this title's cutscenes, which a
 * software decode on the same file does not. That title asks for this reader
 * FIRST, by a marker file its installer writes; nothing else does.
 *
 * WHAT IT IMPLEMENTS, and what it does not. Synchronous mode only: ReadSample
 * returns the sample. A caller that asks for an async callback is refused, and
 * the bridge then hands the game Media Foundation's own refusal. Video comes
 * out as NV12, I420 or YV12 in system memory -- or, when the caller passes a
 * Direct3D 9 device manager, as NV12 in a D3D9 surface, which is what this
 * game draws from: it asks the buffer for its surface and StretchRects it, and
 * with a buffer in system memory it draws nothing at all (measured: a black
 * screen, every sample delivered). Audio comes out as 16-bit PCM or float, at
 * the stream's own rate and channel count -- there is no resampler, so a type
 * asking for another rate is accepted and logged, and gets the native one.
 * Stream order follows what wine's reader gave this game: video first, then
 * audio.
 *
 * FFmpeg 9.0.2, LGPL 2.1 or later, configured by build-asf-reader.sh.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <stdarg.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

#ifndef MAKEFOURCC
#define MAKEFOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
#endif

static const GUID guid_null_;

#define LOGFILE "C:\\mgvf-asf-reader.log"
#define LOG_MAX 400

static LONG log_lines;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    HANDLE h;
    DWORD w;

    if (InterlockedIncrement(&log_lines) > LOG_MAX) return;
    n = snprintf(buf, sizeof(buf), "[asf-reader] ");
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

/* Media Foundation's stream selectors. */
#define SEL_FIRST_VIDEO  0xFFFFFFFC
#define SEL_FIRST_AUDIO  0xFFFFFFFD
#define SEL_ANY          0xFFFFFFFE   /* also "all streams" for selection */
#define SEL_MEDIASOURCE  0xFFFFFFFF

/* What waits is the compressed packet, not the decoded frame. This game reads
 * audio well ahead of video, and decoded 1080p frames queued for that long ran
 * into a bound of 48 and were dropped -- a third of a six-second clip went
 * missing that way. Packets are a few kilobytes; a stream that is selected and
 * never read still has a bound, on packets, and the oldest goes. */
#define MAX_QUEUED_PACKETS 8192
#define MAX_QUEUED_VIDEO 64
#define MAX_QUEUED_AUDIO 1024

struct qnode { struct qnode *next; IMFSample *sample; LONGLONG ts; };
struct pnode { struct pnode *next; AVPacket *pkt; };

struct strm
{
    int av_index;
    BOOL video;
    BOOL selected;
    BOOL eos;
    AVCodecContext *dec;
    IMFMediaType *native[3];
    int n_native;
    IMFMediaType *current;
    GUID out;                   /* subtype delivered */
    UINT32 width, height;       /* video */
    UINT32 fps_n, fps_d;
    UINT32 rate, channels;      /* audio */
    UINT32 bits;                /* 16 PCM or 32 float */
    LONGLONG next_audio_ts;
    struct qnode *head, *tail;
    int count;
    struct pnode *phead, *ptail;
    int pcount;
    BOOL drained;               /* the decoder has been sent its end */
    LONG dropped;
};

struct reader
{
    IMFSourceReader iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IMFByteStream *bs;
    QWORD pos, len;
    AVFormatContext *fmt;
    AVIOContext *avio;
    AVPacket *pkt;
    AVFrame *frame;
    struct strm s[2];
    int n;
    BOOL demux_eof;
    LONGLONG duration;          /* 100 ns */
    LONG samples_out;
    IDirect3DDeviceManager9 *dm9;   /* the caller's, when it passed one */
    HANDLE dev_handle;
};

static HRESULT (WINAPI *pMFCreateDXSurfaceBuffer)(REFIID, IUnknown *, BOOL, IMFMediaBuffer **);

static struct reader *impl(IMFSourceReader *iface)
{
    return CONTAINING_RECORD(iface, struct reader, iface);
}

/* ---------------------------------------------------------------- byte I/O */

static int io_read(void *opaque, uint8_t *buf, int size)
{
    struct reader *r = opaque;
    ULONG got = 0;
    if (FAILED(IMFByteStream_SetCurrentPosition(r->bs, r->pos))) return AVERROR(EIO);
    if (FAILED(IMFByteStream_Read(r->bs, buf, (ULONG)size, &got))) return AVERROR(EIO);
    r->pos += got;
    return got ? (int)got : AVERROR_EOF;
}

static int64_t io_seek(void *opaque, int64_t offset, int whence)
{
    struct reader *r = opaque;
    switch (whence & ~AVSEEK_FORCE)
    {
    case AVSEEK_SIZE: return (int64_t)r->len;
    case SEEK_SET: r->pos = (QWORD)offset; break;
    case SEEK_CUR: r->pos = (QWORD)((int64_t)r->pos + offset); break;
    case SEEK_END: r->pos = (QWORD)((int64_t)r->len + offset); break;
    default: return -1;
    }
    return (int64_t)r->pos;
}

/* ------------------------------------------------------------ media types */

static void set_size(IMFMediaType *t, REFGUID key, UINT32 hi, UINT32 lo)
{
    IMFMediaType_SetUINT64(t, key, ((UINT64)hi << 32) | lo);
}

static IMFMediaType *video_type(struct strm *s, REFGUID sub)
{
    IMFMediaType *t = NULL;
    if (FAILED(MFCreateMediaType(&t))) return NULL;
    IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, sub);
    set_size(t, &MF_MT_FRAME_SIZE, s->width, s->height);
    set_size(t, &MF_MT_FRAME_RATE, s->fps_n, s->fps_d);
    set_size(t, &MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    IMFMediaType_SetUINT32(t, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    IMFMediaType_SetUINT32(t, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    IMFMediaType_SetUINT32(t, &MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    IMFMediaType_SetUINT32(t, &MF_MT_DEFAULT_STRIDE, s->width);
    IMFMediaType_SetUINT32(t, &MF_MT_SAMPLE_SIZE, s->width * s->height * 3 / 2);
    return t;
}

static IMFMediaType *audio_type(struct strm *s, REFGUID sub, UINT32 bits)
{
    IMFMediaType *t = NULL;
    UINT32 align = s->channels * bits / 8;
    if (FAILED(MFCreateMediaType(&t))) return NULL;
    IMFMediaType_SetGUID(t, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(t, &MF_MT_SUBTYPE, sub);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_NUM_CHANNELS, s->channels);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_SAMPLES_PER_SECOND, s->rate);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_BLOCK_ALIGNMENT, align);
    IMFMediaType_SetUINT32(t, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, align * s->rate);
    IMFMediaType_SetUINT32(t, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    return t;
}

/* ---------------------------------------------------------------- queues */

static void q_push(struct strm *s, IMFSample *sample, LONGLONG ts)
{
    struct qnode *n = malloc(sizeof(*n));
    if (!n) { IMFSample_Release(sample); return; }
    n->next = NULL; n->sample = sample; n->ts = ts;
    if (s->tail) s->tail->next = n; else s->head = n;
    s->tail = n;
    s->count++;
    if (s->count > (s->video ? MAX_QUEUED_VIDEO : MAX_QUEUED_AUDIO))
    {
        struct qnode *old = s->head;
        s->head = old->next;
        if (!s->head) s->tail = NULL;
        s->count--;
        IMFSample_Release(old->sample);
        free(old);
        if (InterlockedIncrement(&s->dropped) == 1)
            logf_("%s queue full -- the caller is not reading it; dropping the oldest",
                  s->video ? "video" : "audio");
    }
}

static IMFSample *q_pop(struct strm *s, LONGLONG *ts)
{
    struct qnode *n = s->head;
    IMFSample *sample;
    if (!n) return NULL;
    s->head = n->next;
    if (!s->head) s->tail = NULL;
    s->count--;
    sample = n->sample;
    if (ts) *ts = n->ts;
    free(n);
    return sample;
}

static void q_clear(struct strm *s)
{
    IMFSample *x;
    while ((x = q_pop(s, NULL))) IMFSample_Release(x);
}

static void p_push(struct strm *s, AVPacket *pkt)
{
    struct pnode *n = malloc(sizeof(*n));
    if (!n) { av_packet_free(&pkt); return; }
    n->next = NULL; n->pkt = pkt;
    if (s->ptail) s->ptail->next = n; else s->phead = n;
    s->ptail = n;
    if (++s->pcount > MAX_QUEUED_PACKETS)
    {
        struct pnode *old = s->phead;
        s->phead = old->next;
        if (!s->phead) s->ptail = NULL;
        s->pcount--;
        av_packet_free(&old->pkt);
        free(old);
        if (InterlockedIncrement(&s->dropped) == 1)
            logf_("%s packets piling up -- the caller is not reading it; dropping the oldest",
                  s->video ? "video" : "audio");
    }
}

static AVPacket *p_pop(struct strm *s)
{
    struct pnode *n = s->phead;
    AVPacket *pkt;
    if (!n) return NULL;
    s->phead = n->next;
    if (!s->phead) s->ptail = NULL;
    s->pcount--;
    pkt = n->pkt;
    free(n);
    return pkt;
}

static void p_clear(struct strm *s)
{
    AVPacket *pkt;
    while ((pkt = p_pop(s))) av_packet_free(&pkt);
}

/* ------------------------------------------------------------ conversion */

static LONGLONG to_hns(struct reader *r, struct strm *s, int64_t v)
{
    AVRational tb = r->fmt->streams[s->av_index]->time_base;
    if (v == AV_NOPTS_VALUE) return -1;
    return av_rescale_q(v, tb, (AVRational){ 1, 10000000 });
}

static IMFSample *wrap(IMFMediaBuffer *buf, LONGLONG ts, LONGLONG dur)
{
    IMFSample *sample = NULL;
    if (FAILED(MFCreateSample(&sample))) { IMFMediaBuffer_Release(buf); return NULL; }
    IMFSample_AddBuffer(sample, buf);
    IMFMediaBuffer_Release(buf);
    IMFSample_SetSampleTime(sample, ts < 0 ? 0 : ts);
    if (dur > 0) IMFSample_SetSampleDuration(sample, dur);
    return sample;
}

static void copy_plane(BYTE *dst, LONG dpitch, const uint8_t *src, int spitch, int w, int h)
{
    int y;
    for (y = 0; y < h; y++) memcpy(dst + (LONG_PTR)y * dpitch, src + (ptrdiff_t)y * spitch, w);
}

static LONGLONG video_ts(struct reader *r, struct strm *s, AVFrame *f, LONGLONG *dur)
{
    *dur = f->duration > 0 ? to_hns(r, s, f->duration)
         : (LONGLONG)10000000 * s->fps_d / (s->fps_n ? s->fps_n : 30);
    return to_hns(r, s, f->best_effort_timestamp);
}

/* NV12 into a D3D9 surface on the caller's own device, wrapped the way Media
 * Foundation wraps one: MFCreateDXSurfaceBuffer, so MFGetService on the buffer
 * hands back the IDirect3DSurface9. A default-pool offscreen plain surface,
 * which is what wine's reader gave this game (logged: pool DEFAULT, usage 0). */
static IMFSample *surface_sample(struct reader *r, struct strm *s, AVFrame *f, int w, int h)
{
    IDirect3DDevice9 *dev = NULL;
    IDirect3DSurface9 *surf = NULL;
    IMFMediaBuffer *buf = NULL;
    D3DLOCKED_RECT lr;
    HRESULT hr;
    int x, y, cw = (w + 1) / 2, ch = (h + 1) / 2;
    LONGLONG ts, dur;
    static LONG said;

    if (!pMFCreateDXSurfaceBuffer)
    {
        HMODULE evr = LoadLibraryA("evr.dll");
        if (evr) *(FARPROC *)&pMFCreateDXSurfaceBuffer = GetProcAddress(evr, "MFCreateDXSurfaceBuffer");
        if (!pMFCreateDXSurfaceBuffer)
        {
            if (!InterlockedExchange(&said, 1)) logf_("no MFCreateDXSurfaceBuffer in evr.dll; system memory instead");
            return NULL;
        }
    }
    if (!r->dev_handle && FAILED(IDirect3DDeviceManager9_OpenDeviceHandle(r->dm9, &r->dev_handle)))
        return NULL;
    hr = IDirect3DDeviceManager9_LockDevice(r->dm9, r->dev_handle, &dev, TRUE);
    if (hr == DXVA2_E_NEW_VIDEO_DEVICE)
    {
        IDirect3DDeviceManager9_CloseDeviceHandle(r->dm9, r->dev_handle);
        r->dev_handle = NULL;
        if (FAILED(IDirect3DDeviceManager9_OpenDeviceHandle(r->dm9, &r->dev_handle))) return NULL;
        hr = IDirect3DDeviceManager9_LockDevice(r->dm9, r->dev_handle, &dev, TRUE);
    }
    if (FAILED(hr) || !dev)
    {
        if (InterlockedIncrement(&said) <= 3) logf_("LockDevice -> 0x%08lx", hr);
        return NULL;
    }
    hr = IDirect3DDevice9_CreateOffscreenPlainSurface(dev, s->width, s->height,
             (D3DFORMAT)MAKEFOURCC('N','V','1','2'), D3DPOOL_DEFAULT, &surf, NULL);
    if (SUCCEEDED(hr)) hr = IDirect3DSurface9_LockRect(surf, &lr, NULL, 0);
    if (SUCCEEDED(hr))
    {
        BYTE *base = lr.pBits, *chroma = base + (LONG_PTR)lr.Pitch * s->height;
        copy_plane(base, lr.Pitch, f->data[0], f->linesize[0], w, h);
        for (y = 0; y < ch; y++)
        {
            BYTE *d = chroma + (LONG_PTR)y * lr.Pitch;
            const uint8_t *u = f->data[1] + (ptrdiff_t)y * f->linesize[1];
            const uint8_t *v = f->data[2] + (ptrdiff_t)y * f->linesize[2];
            for (x = 0; x < cw; x++) { d[2 * x] = u[x]; d[2 * x + 1] = v[x]; }
        }
        IDirect3DSurface9_UnlockRect(surf);
    }
    IDirect3DDeviceManager9_UnlockDevice(r->dm9, r->dev_handle, FALSE);
    IDirect3DDevice9_Release(dev);
    if (FAILED(hr))
    {
        if (InterlockedIncrement(&said) <= 3) logf_("NV12 surface -> 0x%08lx; system memory instead", hr);
        if (surf) IDirect3DSurface9_Release(surf);
        return NULL;
    }
    hr = pMFCreateDXSurfaceBuffer(&IID_IDirect3DSurface9, (IUnknown *)surf, FALSE, &buf);
    IDirect3DSurface9_Release(surf);
    if (FAILED(hr)) return NULL;
    {
        static LONG first;
        if (!InterlockedExchange(&first, 1)) logf_("video goes out in D3D9 NV12 surfaces, on the caller's device");
    }
    ts = video_ts(r, s, f, &dur);
    return wrap(buf, ts, dur);
}

static IMFSample *video_sample(struct reader *r, struct strm *s, AVFrame *f)
{
    IMFMediaBuffer *buf = NULL;
    IMF2DBuffer *b2 = NULL;
    BYTE *base = NULL;
    LONG pitch = 0;
    DWORD fourcc;
    int w = s->width, h = s->height, cw = (w + 1) / 2, ch = (h + 1) / 2, x, y;
    LONGLONG ts, dur;
    const uint8_t *U = f->data[1], *V = f->data[2];

    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P)
    {
        static LONG once;
        if (!InterlockedExchange(&once, 1))
            logf_("decoder gave pixel format %d, not planar 4:2:0; frame skipped", f->format);
        return NULL;
    }
    if (f->width < w) w = f->width;
    if (f->height < h) h = f->height;

    if (r->dm9 && IsEqualGUID(&s->out, &MFVideoFormat_NV12))
    {
        IMFSample *x = surface_sample(r, s, f, w, h);
        if (x) return x;
    }

    fourcc = IsEqualGUID(&s->out, &MFVideoFormat_NV12) ? MAKEFOURCC('N','V','1','2')
           : IsEqualGUID(&s->out, &MFVideoFormat_YV12) ? MAKEFOURCC('Y','V','1','2')
           : MAKEFOURCC('I','4','2','0');
    if (FAILED(MFCreate2DMediaBuffer(s->width, s->height, fourcc, FALSE, &buf))) return NULL;
    if (FAILED(IMFMediaBuffer_QueryInterface(buf, &IID_IMF2DBuffer, (void **)&b2)) ||
        FAILED(IMF2DBuffer_Lock2D(b2, &base, &pitch)))
    {
        if (b2) IMF2DBuffer_Release(b2);
        IMFMediaBuffer_Release(buf);
        return NULL;
    }

    copy_plane(base, pitch, f->data[0], f->linesize[0], w, h);
    {
        BYTE *chroma = base + (LONG_PTR)pitch * s->height;
        if (fourcc == MAKEFOURCC('N','V','1','2'))
        {
            for (y = 0; y < ch; y++)
            {
                BYTE *d = chroma + (LONG_PTR)y * pitch;
                const uint8_t *u = U + (ptrdiff_t)y * f->linesize[1];
                const uint8_t *v = V + (ptrdiff_t)y * f->linesize[2];
                for (x = 0; x < cw; x++) { d[2 * x] = u[x]; d[2 * x + 1] = v[x]; }
            }
        }
        else
        {
            LONG cp = pitch / 2;
            BYTE *p1 = chroma, *p2 = chroma + (LONG_PTR)cp * ((s->height + 1) / 2);
            const uint8_t *first = fourcc == MAKEFOURCC('Y','V','1','2') ? V : U;
            const uint8_t *second = fourcc == MAKEFOURCC('Y','V','1','2') ? U : V;
            int l1 = fourcc == MAKEFOURCC('Y','V','1','2') ? f->linesize[2] : f->linesize[1];
            int l2 = fourcc == MAKEFOURCC('Y','V','1','2') ? f->linesize[1] : f->linesize[2];
            copy_plane(p1, cp, first, l1, cw, ch);
            copy_plane(p2, cp, second, l2, cw, ch);
        }
    }
    IMF2DBuffer_Unlock2D(b2);
    IMF2DBuffer_Release(b2);
    IMFMediaBuffer_SetCurrentLength(buf, s->width * s->height * 3 / 2);

    ts = video_ts(r, s, f, &dur);
    return wrap(buf, ts, dur);
}

static IMFSample *audio_sample(struct reader *r, struct strm *s, AVFrame *f)
{
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    int ch = f->ch_layout.nb_channels, n = f->nb_samples, i, c;
    DWORD bytes;
    LONGLONG ts, dur;
    BOOL planar = av_sample_fmt_is_planar(f->format);
    enum AVSampleFormat packed = av_get_packed_sample_fmt(f->format);

    if (ch <= 0 || n <= 0) return NULL;
    if (packed != AV_SAMPLE_FMT_FLT && packed != AV_SAMPLE_FMT_S16)
    {
        static LONG once;
        if (!InterlockedExchange(&once, 1))
            logf_("decoder gave sample format %d; frame skipped", f->format);
        return NULL;
    }
    if ((UINT32)ch != s->channels)
    {
        static LONG once;
        if (!InterlockedExchange(&once, 1))
            logf_("decoder gave %d channels, the type says %u; frame skipped", ch, s->channels);
        return NULL;
    }

    bytes = (DWORD)n * ch * (s->bits / 8);
    if (FAILED(MFCreateMemoryBuffer(bytes, &buf))) return NULL;
    if (FAILED(IMFMediaBuffer_Lock(buf, &p, NULL, NULL))) { IMFMediaBuffer_Release(buf); return NULL; }

    for (i = 0; i < n; i++)
        for (c = 0; c < ch; c++)
        {
            float v;
            if (packed == AV_SAMPLE_FMT_FLT)
                v = planar ? ((const float *)f->extended_data[c])[i]
                           : ((const float *)f->extended_data[0])[i * ch + c];
            else
                v = (planar ? ((const int16_t *)f->extended_data[c])[i]
                            : ((const int16_t *)f->extended_data[0])[i * ch + c]) / 32768.0f;
            if (s->bits == 32)
                ((float *)p)[i * ch + c] = v;
            else
            {
                int x = (int)(v * 32767.0f + (v >= 0 ? 0.5f : -0.5f));
                ((int16_t *)p)[i * ch + c] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
        }
    IMFMediaBuffer_Unlock(buf);
    IMFMediaBuffer_SetCurrentLength(buf, bytes);

    ts = to_hns(r, s, f->best_effort_timestamp);
    if (ts < 0) ts = s->next_audio_ts;
    dur = (LONGLONG)n * 10000000 / (s->rate ? s->rate : 48000);
    s->next_audio_ts = ts + dur;
    return wrap(buf, ts, dur);
}

static void receive_all(struct reader *r, struct strm *s)
{
    for (;;)
    {
        IMFSample *sample;
        LONGLONG ts = 0;
        int e = avcodec_receive_frame(s->dec, r->frame);
        if (e < 0) break;
        sample = s->video ? video_sample(r, s, r->frame) : audio_sample(r, s, r->frame);
        av_frame_unref(r->frame);
        if (!sample) continue;
        IMFSample_GetSampleTime(sample, &ts);
        q_push(s, sample, ts);
    }
}

/* Demux one packet into its stream's queue. FALSE at the end of the file. */
static BOOL pump(struct reader *r)
{
    int i;
    if (r->demux_eof) return FALSE;
    if (av_read_frame(r->fmt, r->pkt) < 0)
    {
        r->demux_eof = TRUE;
        return FALSE;
    }
    for (i = 0; i < r->n; i++)
    {
        struct strm *s = &r->s[i];
        AVPacket *mine;
        if (s->av_index != r->pkt->stream_index) continue;
        if (!s->selected) break;
        mine = av_packet_alloc();
        if (!mine) break;
        av_packet_move_ref(mine, r->pkt);
        p_push(s, mine);
        break;
    }
    av_packet_unref(r->pkt);
    return TRUE;
}

/* Decode until this stream has a sample ready or has ended. Only this stream's
 * packets are decoded; the other stream's wait, compressed, for their turn. */
static void fill(struct reader *r, struct strm *s)
{
    while (!s->head && !s->eos)
    {
        AVPacket *pkt = p_pop(s);
        if (pkt)
        {
            if (avcodec_send_packet(s->dec, pkt) == AVERROR(EAGAIN))
            {
                receive_all(r, s);
                avcodec_send_packet(s->dec, pkt);
            }
            av_packet_free(&pkt);
            receive_all(r, s);
        }
        else if (pump(r))
            continue;
        else if (!s->drained)
        {
            avcodec_send_packet(s->dec, NULL);
            receive_all(r, s);
            s->drained = TRUE;
        }
        else
            s->eos = TRUE;
    }
}

/* ---------------------------------------------------------- the interface */

static struct strm *resolve(struct reader *r, DWORD index)
{
    int i;
    if (index == SEL_FIRST_VIDEO || index == SEL_FIRST_AUDIO)
    {
        for (i = 0; i < r->n; i++)
            if (r->s[i].video == (index == SEL_FIRST_VIDEO)) return &r->s[i];
        return NULL;
    }
    return index < (DWORD)r->n ? &r->s[index] : NULL;
}

static HRESULT WINAPI rd_QueryInterface(IMFSourceReader *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSourceReader))
    {
        *out = iface;
        IMFSourceReader_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI rd_AddRef(IMFSourceReader *iface)
{
    return InterlockedIncrement(&impl(iface)->ref);
}

static void destroy(struct reader *r)
{
    int i, k;
    for (i = 0; i < r->n; i++)
    {
        q_clear(&r->s[i]);
        p_clear(&r->s[i]);
        if (r->s[i].dec) avcodec_free_context(&r->s[i].dec);
        for (k = 0; k < r->s[i].n_native; k++) IMFMediaType_Release(r->s[i].native[k]);
        if (r->s[i].current) IMFMediaType_Release(r->s[i].current);
    }
    if (r->fmt) avformat_close_input(&r->fmt);
    if (r->avio) { av_freep(&r->avio->buffer); avio_context_free(&r->avio); }
    if (r->pkt) av_packet_free(&r->pkt);
    if (r->frame) av_frame_free(&r->frame);
    if (r->bs) IMFByteStream_Release(r->bs);
    if (r->dm9)
    {
        if (r->dev_handle) IDirect3DDeviceManager9_CloseDeviceHandle(r->dm9, r->dev_handle);
        IDirect3DDeviceManager9_Release(r->dm9);
    }
    DeleteCriticalSection(&r->cs);
    free(r);
}

static ULONG WINAPI rd_Release(IMFSourceReader *iface)
{
    struct reader *r = impl(iface);
    ULONG ref = InterlockedDecrement(&r->ref);
    if (!ref) destroy(r);
    return ref;
}

static HRESULT WINAPI rd_GetStreamSelection(IMFSourceReader *iface, DWORD index, BOOL *selected)
{
    struct reader *r = impl(iface);
    struct strm *s;
    if (!selected) return E_POINTER;
    EnterCriticalSection(&r->cs);
    s = resolve(r, index);
    if (s) *selected = s->selected;
    LeaveCriticalSection(&r->cs);
    return s ? S_OK : MF_E_INVALIDSTREAMNUMBER;
}

static HRESULT WINAPI rd_SetStreamSelection(IMFSourceReader *iface, DWORD index, BOOL selected)
{
    struct reader *r = impl(iface);
    HRESULT hr = S_OK;
    int i;
    EnterCriticalSection(&r->cs);
    if (index == SEL_ANY)
        for (i = 0; i < r->n; i++)
        { r->s[i].selected = selected; if (!selected) { q_clear(&r->s[i]); p_clear(&r->s[i]); } }
    else
    {
        struct strm *s = resolve(r, index);
        if (s) { s->selected = selected; if (!selected) { q_clear(s); p_clear(s); } }
        else hr = MF_E_INVALIDSTREAMNUMBER;
    }
    LeaveCriticalSection(&r->cs);
    return hr;
}

static HRESULT copy_type(IMFMediaType *from, IMFMediaType **out)
{
    IMFMediaType *t = NULL;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    hr = IMFMediaType_CopyAllItems(from, (IMFAttributes *)t);
    if (FAILED(hr)) { IMFMediaType_Release(t); return hr; }
    *out = t;
    return S_OK;
}

static HRESULT WINAPI rd_GetNativeMediaType(IMFSourceReader *iface, DWORD index, DWORD type_index,
                                            IMFMediaType **type)
{
    struct reader *r = impl(iface);
    struct strm *s;
    HRESULT hr;
    if (!type) return E_POINTER;
    EnterCriticalSection(&r->cs);
    s = resolve(r, index);
    if (!s) hr = MF_E_INVALIDSTREAMNUMBER;
    else if (type_index >= (DWORD)s->n_native) hr = MF_E_NO_MORE_TYPES;
    else hr = copy_type(s->native[type_index], type);
    LeaveCriticalSection(&r->cs);
    return hr;
}

static HRESULT WINAPI rd_GetCurrentMediaType(IMFSourceReader *iface, DWORD index, IMFMediaType **type)
{
    struct reader *r = impl(iface);
    struct strm *s;
    HRESULT hr;
    if (!type) return E_POINTER;
    EnterCriticalSection(&r->cs);
    s = resolve(r, index);
    hr = s ? copy_type(s->current, type) : MF_E_INVALIDSTREAMNUMBER;
    LeaveCriticalSection(&r->cs);
    return hr;
}

static HRESULT WINAPI rd_SetCurrentMediaType(IMFSourceReader *iface, DWORD index, DWORD *reserved,
                                             IMFMediaType *type)
{
    struct reader *r = impl(iface);
    struct strm *s;
    GUID major, sub;
    IMFMediaType *t = NULL;
    HRESULT hr = S_OK;

    if (!type) return E_POINTER;
    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major)) ||
        FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &sub)))
        return MF_E_INVALIDMEDIATYPE;

    EnterCriticalSection(&r->cs);
    s = resolve(r, index);
    if (!s) hr = MF_E_INVALIDSTREAMNUMBER;
    else if (s->video)
    {
        if (!IsEqualGUID(&major, &MFMediaType_Video) ||
            !(IsEqualGUID(&sub, &MFVideoFormat_NV12) || IsEqualGUID(&sub, &MFVideoFormat_I420) ||
              IsEqualGUID(&sub, &MFVideoFormat_IYUV) || IsEqualGUID(&sub, &MFVideoFormat_YV12)))
            hr = MF_E_INVALIDMEDIATYPE;
        else
        {
            s->out = IsEqualGUID(&sub, &MFVideoFormat_IYUV) ? MFVideoFormat_I420 : sub;
            t = video_type(s, &sub);
        }
    }
    else
    {
        UINT32 bits = 0, rate = 0, chans = 0;
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, &chans);
        if (!IsEqualGUID(&major, &MFMediaType_Audio)) hr = MF_E_INVALIDMEDIATYPE;
        else if (IsEqualGUID(&sub, &MFAudioFormat_Float)) s->bits = 32;
        else if (IsEqualGUID(&sub, &MFAudioFormat_PCM) && (!bits || bits == 16)) s->bits = 16;
        else hr = MF_E_INVALIDMEDIATYPE;
        if (SUCCEEDED(hr))
        {
            if ((rate && rate != s->rate) || (chans && chans != s->channels))
                logf_("audio asked for %u Hz %u ch; this reader has no resampler and gives %u Hz %u ch",
                      rate, chans, s->rate, s->channels);
            s->out = sub;
            t = audio_type(s, &sub, s->bits);
        }
    }
    if (t)
    {
        if (s->current) IMFMediaType_Release(s->current);
        s->current = t;
        q_clear(s);         /* anything queued was converted for the old type */
    }
    else if (SUCCEEDED(hr)) hr = E_OUTOFMEMORY;
    LeaveCriticalSection(&r->cs);
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 8)
            logf_("SetCurrentMediaType(stream %lu) -> 0x%08lx", index, hr);
    }
    return hr;
}

static HRESULT WINAPI rd_SetCurrentPosition(IMFSourceReader *iface, REFGUID format, REFPROPVARIANT pos)
{
    struct reader *r = impl(iface);
    LONGLONG hns;
    int i, e;
    if (!pos) return E_POINTER;
    if (format && !IsEqualGUID(format, &guid_null_)) return MF_E_UNSUPPORTED_TIME_FORMAT;
    if (pos->vt != VT_I8) return MF_E_UNSUPPORTED_TIME_FORMAT;
    hns = pos->hVal.QuadPart;
    EnterCriticalSection(&r->cs);
    e = av_seek_frame(r->fmt, -1, hns / 10, AVSEEK_FLAG_BACKWARD);
    if (e < 0 && hns == 0)
        e = avformat_seek_file(r->fmt, -1, INT64_MIN, 0, 0, 0);
    for (i = 0; i < r->n; i++)
    {
        q_clear(&r->s[i]);
        p_clear(&r->s[i]);
        avcodec_flush_buffers(r->s[i].dec);
        r->s[i].eos = FALSE;
        r->s[i].drained = FALSE;
        r->s[i].next_audio_ts = hns;
    }
    r->demux_eof = FALSE;
    LeaveCriticalSection(&r->cs);
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 8)
            logf_("SetCurrentPosition(%lld ms) -> seek %d", hns / 10000, e);
    }
    return e < 0 ? E_FAIL : S_OK;
}

static HRESULT WINAPI rd_ReadSample(IMFSourceReader *iface, DWORD index, DWORD flags, DWORD *actual,
                                    DWORD *sflags, LONGLONG *timestamp, IMFSample **sample)
{
    struct reader *r = impl(iface);
    struct strm *s = NULL;
    IMFSample *got = NULL;
    LONGLONG ts = 0;
    DWORD out_flags = 0, out_index = index;
    HRESULT hr = S_OK;

    if (!sample) return E_POINTER;
    *sample = NULL;
    EnterCriticalSection(&r->cs);

    if (index == SEL_ANY)
    {
        int i, best = -1, first = -1;
        for (i = 0; i < r->n; i++)
        {
            if (!r->s[i].selected) continue;
            if (first < 0) first = i;
            fill(r, &r->s[i]);
            if (r->s[i].head && (best < 0 || r->s[i].head->ts < r->s[best].head->ts)) best = i;
        }
        if (first < 0) hr = MF_E_INVALIDREQUEST;
        else if (best >= 0) { out_index = best; got = q_pop(&r->s[best], &ts); }
        else { out_index = first; out_flags |= MF_SOURCE_READERF_ENDOFSTREAM; }
    }
    else
    {
        s = resolve(r, index);
        if (!s) hr = MF_E_INVALIDSTREAMNUMBER;
        else if (!s->selected) hr = MF_E_INVALIDREQUEST;
        else
        {
            out_index = (DWORD)(s - r->s);
            fill(r, s);
            got = q_pop(s, &ts);
            if (!got) out_flags |= MF_SOURCE_READERF_ENDOFSTREAM;
        }
    }
    LeaveCriticalSection(&r->cs);

    if (actual) *actual = out_index;
    if (sflags) *sflags = out_flags;
    if (timestamp) *timestamp = got ? ts : 0;
    *sample = got;
    if (got)
    {
        LONG n = InterlockedIncrement(&r->samples_out);
        if (n == 1 || n == 100 || n == 1000)
            logf_("ReadSample: sample %ld out (stream %lu, %lld ms)", n, out_index, ts / 10000);
    }
    else if (out_flags & MF_SOURCE_READERF_ENDOFSTREAM)
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 8)
            logf_("ReadSample: end of stream %lu after %ld samples", out_index, r->samples_out);
    }
    (void)flags;
    return hr;
}

static HRESULT WINAPI rd_Flush(IMFSourceReader *iface, DWORD index)
{
    struct reader *r = impl(iface);
    int i;
    EnterCriticalSection(&r->cs);
    for (i = 0; i < r->n; i++)
        if (index == SEL_ANY || resolve(r, index) == &r->s[i]) { q_clear(&r->s[i]); p_clear(&r->s[i]); }
    LeaveCriticalSection(&r->cs);
    return S_OK;
}

static HRESULT WINAPI rd_GetServiceForStream(IMFSourceReader *iface, DWORD index, REFGUID service,
                                             REFIID riid, void **object)
{
    if (object) *object = NULL;
    return MF_E_UNSUPPORTED_SERVICE;
}

static HRESULT WINAPI rd_GetPresentationAttribute(IMFSourceReader *iface, DWORD index, REFGUID key,
                                                  PROPVARIANT *value)
{
    struct reader *r = impl(iface);
    if (!value) return E_POINTER;
    PropVariantInit(value);
    if (index != SEL_MEDIASOURCE)
        return resolve(r, index) ? MF_E_ATTRIBUTENOTFOUND : MF_E_INVALIDSTREAMNUMBER;
    if (IsEqualGUID(key, &MF_PD_DURATION))
    {
        value->vt = VT_UI8;
        value->uhVal.QuadPart = (ULONGLONG)r->duration;
        return S_OK;
    }
    if (IsEqualGUID(key, &MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS))
    {
        value->vt = VT_UI4;
        value->ulVal = MFMEDIASOURCE_CAN_SEEK | MFMEDIASOURCE_CAN_PAUSE;
        return S_OK;
    }
    return MF_E_ATTRIBUTENOTFOUND;
}

/* Not const: the bridge that loads this patches reader vtables for its own
 * logging, and a vtable in read-only memory would turn that into a fault. */
static IMFSourceReaderVtbl reader_vtbl =
{
    rd_QueryInterface, rd_AddRef, rd_Release,
    rd_GetStreamSelection, rd_SetStreamSelection,
    rd_GetNativeMediaType, rd_GetCurrentMediaType, rd_SetCurrentMediaType,
    rd_SetCurrentPosition, rd_ReadSample, rd_Flush,
    rd_GetServiceForStream, rd_GetPresentationAttribute,
};

/* ------------------------------------------------------------- creation */

static BOOL open_stream(struct reader *r, int av_index, BOOL video)
{
    AVStream *st = r->fmt->streams[av_index];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    struct strm *s = &r->s[r->n];
    if (!codec) return FALSE;
    s->dec = avcodec_alloc_context3(codec);
    if (!s->dec) return FALSE;
    if (avcodec_parameters_to_context(s->dec, st->codecpar) < 0) goto fail;
    s->dec->thread_count = video ? 0 : 1;
    if (avcodec_open2(s->dec, codec, NULL) < 0) goto fail;

    s->av_index = av_index;
    s->video = video;
    s->selected = TRUE;
    if (video)
    {
        /* Not r_frame_rate: on this title's files it reads 60000/1001 while the
         * frames are 33 ms apart, and avg_frame_rate is 0/0. A frame with no
         * duration of its own is timed from this, so the fallback is the
         * cadence measured on those files. */
        AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : (AVRational){ 30000, 1001 };
        s->width = st->codecpar->width;
        s->height = st->codecpar->height;
        s->fps_n = fr.num > 0 ? fr.num : 30000;
        s->fps_d = fr.num > 0 ? fr.den : 1001;
        s->out = MFVideoFormat_NV12;
        s->native[s->n_native++] = video_type(s, &MFVideoFormat_NV12);
        s->native[s->n_native++] = video_type(s, &MFVideoFormat_I420);
        s->native[s->n_native++] = video_type(s, &MFVideoFormat_YV12);
    }
    else
    {
        s->rate = st->codecpar->sample_rate;
        s->channels = st->codecpar->ch_layout.nb_channels;
        s->bits = 32;
        s->out = MFAudioFormat_Float;
        s->native[s->n_native++] = audio_type(s, &MFAudioFormat_Float, 32);
        s->native[s->n_native++] = audio_type(s, &MFAudioFormat_PCM, 16);
    }
    if (!s->native[0]) goto fail;
    copy_type(s->native[0], &s->current);
    logf_("stream %d: %s %s, %s", r->n, video ? "video" : "audio", codec->name,
          video ? "NV12/I420/YV12 out" : "float/PCM16 out");
    if (video) logf_("  %ux%u at %u/%u fps", s->width, s->height, s->fps_n, s->fps_d);
    else       logf_("  %u Hz, %u channels", s->rate, s->channels);
    r->n++;
    return TRUE;
fail:
    avcodec_free_context(&s->dec);
    return FALSE;
}

static BOOL is_supported(enum AVCodecID id, BOOL *video)
{
    switch (id)
    {
    case AV_CODEC_ID_VC1: case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_WMV2: case AV_CODEC_ID_WMV1:
    case AV_CODEC_ID_VP9: case AV_CODEC_ID_VP8: *video = TRUE;  return TRUE;
    case AV_CODEC_ID_WMAPRO: case AV_CODEC_ID_WMAV2:
    case AV_CODEC_ID_OPUS: case AV_CODEC_ID_VORBIS: *video = FALSE; return TRUE;
    default: return FALSE;
    }
}

__declspec(dllexport) HRESULT WINAPI MGVF_CreateAsfReader(IMFByteStream *bs, IMFAttributes *attrs,
                                                          IMFSourceReader **out)
{
    static const BYTE asf_guid[16] = { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                       0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C };
    struct reader *r;
    unsigned char *iobuf;
    BYTE head[16] = { 0 };
    ULONG got = 0;
    IUnknown *callback = NULL;
    const char *container;
    unsigned i;
    int pass;

    if (!bs || !out) return E_POINTER;
    *out = NULL;
    if (attrs && SUCCEEDED(IMFAttributes_GetUnknown(attrs, &MF_SOURCE_READER_ASYNC_CALLBACK,
                                                    &IID_IUnknown, (void **)&callback)))
    {
        IUnknown_Release(callback);
        logf_("asked for an asynchronous reader; this one is synchronous only -- refused");
        return E_NOTIMPL;
    }

    /* ASF by its header GUID, Matroska and WebM by the EBML magic: the two
     * containers this reader carries a demuxer for. */
    if (FAILED(IMFByteStream_SetCurrentPosition(bs, 0)) ||
        FAILED(IMFByteStream_Read(bs, head, sizeof(head), &got)) || got < 16)
        return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
    if (!memcmp(head, asf_guid, 16)) container = "asf";
    else if (head[0] == 0x1a && head[1] == 0x45 && head[2] == 0xdf && head[3] == 0xa3) container = "matroska";
    else return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;

    r = calloc(1, sizeof(*r));
    if (!r) return E_OUTOFMEMORY;
    if (attrs)
    {
        IUnknown *mgr = NULL;
        if (SUCCEEDED(IMFAttributes_GetUnknown(attrs, &MF_SOURCE_READER_D3D_MANAGER,
                                               &IID_IUnknown, (void **)&mgr)))
        {
            if (FAILED(IUnknown_QueryInterface(mgr, &IID_IDirect3DDeviceManager9, (void **)&r->dm9)))
                r->dm9 = NULL;
            {
                static LONG said;
                if (InterlockedIncrement(&said) <= 2)
                    logf_("the caller passed a device manager: %s", r->dm9 ? "Direct3D 9 -- frames go out as surfaces"
                          : "not Direct3D 9 -- frames go out in system memory");
            }
            IUnknown_Release(mgr);
        }
    }
    r->iface.lpVtbl = &reader_vtbl;
    r->ref = 1;
    InitializeCriticalSection(&r->cs);
    r->bs = bs;
    IMFByteStream_AddRef(bs);
    IMFByteStream_GetLength(bs, &r->len);

    iobuf = av_malloc(64 * 1024);
    r->avio = iobuf ? avio_alloc_context(iobuf, 64 * 1024, 0, r, io_read, NULL, io_seek) : NULL;
    r->fmt = avformat_alloc_context();
    r->pkt = av_packet_alloc();
    r->frame = av_frame_alloc();
    if (!r->avio || !r->fmt || !r->pkt || !r->frame) { if (!r->avio) av_free(iobuf); goto fail; }
    r->fmt->pb = r->avio;
    r->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&r->fmt, NULL, av_find_input_format(container), NULL) < 0)
    {
        logf_("FFmpeg's %s demuxer could not open the stream", container);
        goto fail;
    }
    avformat_find_stream_info(r->fmt, NULL);

    /* Video first, then audio: the order wine's reader gave this game. */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < r->fmt->nb_streams && r->n < 2; i++)
        {
            BOOL video;
            if (!is_supported(r->fmt->streams[i]->codecpar->codec_id, &video)) continue;
            if (video != (pass == 0)) continue;
            if (r->n && r->s[0].video == video) continue;   /* one of each */
            open_stream(r, (int)i, video);
        }
    if (!r->n)
    {
        logf_("no stream this reader decodes in this file");
        goto fail;
    }
    r->duration = r->fmt->duration > 0 ? r->fmt->duration * 10 : 0;
    logf_("opened: %d stream(s), %lld ms", r->n, r->duration / 10000);
    *out = &r->iface;
    return S_OK;

fail:
    destroy(r);
    return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(inst);
    return TRUE;
}
