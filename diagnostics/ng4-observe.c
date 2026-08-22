/* ng4-observe -- watch Ninja Gaiden 4 decide it cannot play, and change
 * nothing while watching.
 *
 * Measured from outside already: mfplat and mfreadwrite load, not one GStreamer
 * plugin ever does, and the game exits after about forty-five seconds having
 * shown a black screen and left no crash report. A stock bottle registers zero
 * video decoder MFTs, so MFTEnumEx asking for VP90 should be answering none --
 * and the game gives up there, before it ever tries to open a file.
 *
 * "Should be" is the part this is here to remove. Every intervention is off.
 *
 * It rides on dstorage.dll, which the game imports and which forwards four
 * exports. Deliberately not a Steam-named DLL and it re-exports no Steamworks
 * entry point: the game enumerates modules once at startup looking for a Steam
 * emulator, and while its worst outcome is a dialog, there is no reason to walk
 * into it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define LOGFILE "C:\\ng4-observe.log"

static const char *process_name(void)
{
    static char who[64];
    if (!who[0])
    {
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
        const char *base = path;
        while (len-- > 0)
            if (path[len] == '\\') { base = path + len + 1; break; }
        lstrcpynA(who, base, sizeof(who));
        if (!who[0]) lstrcpynA(who, "?", sizeof(who));
    }
    return who;
}

static LONG log_lines;
static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, const GUID *, void **);
static HRESULT WINAPI my_D3D12CreateDevice(void *, UINT, const GUID *, void **);


static void logf_(const char *fmt, ...)
{
    char buf[1024];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n, m;

    n = snprintf(buf, sizeof(buf) - 2, "[%s] ", process_name());
    if (n < 0) n = 0;
    va_start(ap, fmt);
    m = vsnprintf(buf + n, sizeof(buf) - 2 - n, fmt, ap);
    va_end(ap);
    if (m < 0) return;
    n += m;
    buf[n] = '\n';

    if (InterlockedIncrement(&log_lines) > 300) return;

    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n + 1, &written, NULL);
    CloseHandle(h);
}

/* A media subtype GUID's first four bytes are the FourCC for every codec that
 * has one, so printing both the FourCC and the GUID names the format whether
 * or not it is one we already know. */
static void describe_subtype(const char *what, const GUID *g)
{
    char cc[5];
    DWORD fourcc = g->Data1;
    cc[0] = (char)(fourcc & 0xff);
    cc[1] = (char)((fourcc >> 8) & 0xff);
    cc[2] = (char)((fourcc >> 16) & 0xff);
    cc[3] = (char)((fourcc >> 24) & 0xff);
    cc[4] = 0;
    for (int i = 0; i < 4; i++)
        if (cc[i] < 32 || cc[i] > 126) cc[i] = '.';
    logf_("  %s: '%s'  {%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X}",
          what, cc, g->Data1, g->Data2, g->Data3,
          g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
          g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

typedef struct { GUID guidMajorType; GUID guidSubtype; } REG_TYPE_INFO;

/* Follow the decoder itself, not the registry entry that promises one.
 *
 * MFTEnumEx is a registry read: it loads no DLL, so a stale or bogus entry
 * makes it answer "1 decoder" and only fails later, when the object is
 * actually created. A player that ignores that HRESULT and keeps presenting
 * its render target shows a black picture and says nothing -- which is what we
 * are looking at.
 *
 * So the interesting calls are further in: does the object instantiate, does
 * SetOutputType agree a format, and does ProcessOutput ever hand back a frame.
 * A frame that never arrives and a frame that arrives and is not drawn are
 * both "black with sound", and only these tell them apart.
 *
 * Vtable slots, patched one at a time rather than proxying the whole object --
 * full proxying broke rendering when it was tried on DYNASTY WARRIORS.
 *   IMFActivate:  IUnknown(3) + IMFAttributes(30) -> ActivateObject = 33
 *   IMFTransform: IUnknown(3) + SetOutputType 16, ProcessMessage 23,
 *                 ProcessInput 24, ProcessOutput 25 */
/* A canary. Practically every caller asks a transform how many streams it has
 * before doing anything else, so if this never fires the patch is not taking
 * effect and the silence below means nothing. Distinguishing "the game does
 * not feed the decoder" from "I am not seeing it feed the decoder" is the
 * whole reason it is here. */
/* Between SetInputType and SetOutputType the caller asks what the decoder can
 * produce and picks one. The game stops in that gap, so this is where the
 * answer is: either the decoder offers nothing, or it offers formats the game
 * will not take. Both look identical from the sofa. */
#define SLOT_GET_OUTPUT_AVAIL 14
#define SLOT_GET_OUTPUT_INFO   7
#define SLOT_GET_STREAM_COUNT  4
#define SLOT_SET_INPUT_TYPE   15
#define SLOT_ACTIVATE_OBJECT  33
#define SLOT_SET_OUTPUT_TYPE  16
#define SLOT_PROCESS_MESSAGE  23
#define SLOT_PROCESS_INPUT    24
#define SLOT_PROCESS_OUTPUT   25

/* Reports what it did rather than failing quietly.
 *
 * The previous build patched nothing and said nothing, and the silence read
 * exactly like "the game never calls the decoder" -- a far more interesting
 * conclusion than "VirtualProtect refused", and the wrong one. An instrument
 * that can fail silently is worse than no instrument. */
/* Set while DirectStorage is inside CreateQueue, so the capability questions it
   asks can be told apart from the game's own. CheckFeatureSupport is called
   constantly by everything; only this window is interesting. */
static LONG in_create_queue_;
static void *hooked_device_;

/* Defined with the crash reporter below; needed up here by the D3D12 hooks. */
static BOOL readable_(const void *p, SIZE_T n);

static BOOL patch_slot(const char *what, void *obj, int slot,
                       void *replacement, void **saved)
{
    void ***vt = (void ***)obj;
    DWORD old;
    void *before;

    if (!obj) { logf_("  patch %s: no object", what); return FALSE; }
    if (!*vt) { logf_("  patch %s: no vtable", what); return FALSE; }
    if (*saved) return TRUE;                  /* one vtable, patch it once */

    before = (*vt)[slot];
    if (!before)
    {
        logf_("  patch %s: slot %d is NULL -- wrong layout", what, slot);
        return FALSE;
    }
    if (!VirtualProtect(&(*vt)[slot], sizeof(void *), PAGE_READWRITE, &old))
    {
        logf_("  patch %s: VirtualProtect refused (err %lu) at vtable %p slot %d",
              what, GetLastError(), (void *)*vt, slot);
        return FALSE;
    }
    (*vt)[slot] = replacement;
    VirtualProtect(&(*vt)[slot], sizeof(void *), old, &old);

    if ((*vt)[slot] != replacement)
    {
        logf_("  patch %s: write did not stick (still %p)", what, (*vt)[slot]);
        return FALSE;
    }
    *saved = before;
    logf_("  patch %s: slot %d  %p -> ours", what, slot, before);
    return TRUE;
}

static HRESULT (WINAPI *real_ActivateObject)(void *, REFIID, void **);
static HRESULT (WINAPI *real_SetOutputType)(void *, DWORD, void *, DWORD);
static HRESULT (WINAPI *real_ProcessMessage)(void *, DWORD, ULONG_PTR);
static HRESULT (WINAPI *real_ProcessInput)(void *, DWORD, void *, DWORD);
static HRESULT (WINAPI *real_ProcessOutput)(void *, DWORD, DWORD, void *, DWORD *);

static HRESULT (WINAPI *real_GetStreamCount)(void *, DWORD *, DWORD *);
static HRESULT (WINAPI *real_SetInputType)(void *, DWORD, void *, DWORD);
/* Give Electra a frame it will accept.
 *
 * The decoder now produces frames, and they are thrown away one by one: the
 * caller allocates a plain memory buffer, a plain memory buffer does not
 * implement IMF2DBuffer, and Electra rejects every video frame that is not 2D.
 * That is winevideo's patch 0007, whose fix is to make the decoder provide the
 * samples so its allocator's 2D buffers are used instead.
 *
 * That flag lives inside the MFT and cannot be set from out here. But the same
 * end is reached from the other side: tell the caller the decoder provides
 * samples, so it stops allocating and passes nothing -- then hand the real
 * ProcessOutput a sample of ours built on MFCreate2DMediaBuffer, which
 * implements IMF2DBuffer natively, and give that sample back.
 *
 * The MFT is unchanged and still fills a buffer it was handed. Only the buffer
 * is different, and it is different in exactly the way Electra requires. */
#define MFT_OUTPUT_STREAM_PROVIDES_SAMPLES 0x100

typedef struct
{
    DWORD dwStreamID;
    void *pSample;      /* IMFSample */
    DWORD dwStatus;
    void *pEvents;      /* IMFCollection */
} OUT_DATA_BUFFER;

static HRESULT (WINAPI *pMFCreateSample)(void **);
static HRESULT (WINAPI *pMFCreate2DMediaBuffer)(DWORD, DWORD, DWORD, BOOL, void **);
static UINT32 frame_w, frame_h;
/* The swap is off.
 *
 * Removing Electra's buffer from its own sample crashed the game outright:
 * RemoveAllBuffers releases it, and if Electra still holds the pointer it is
 * reading freed memory a moment later. Taking something away from a caller
 * that is still using it was never going to work.
 *
 * The buffer has to be left where it is and taught to answer IMF2DBuffer
 * instead -- which is what the DYNASTY WARRIORS bridge did for IMFDXGIBuffer,
 * and is the next thing to build. */
static BOOL provide_samples = FALSE;

static void load_mfplat(void)
{
    HMODULE mf;
    if (pMFCreateSample) return;
    mf = LoadLibraryA("mfplat.dll");
    if (!mf) { logf_("cannot load mfplat.dll"); return; }
    *(FARPROC *)&pMFCreateSample = GetProcAddress(mf, "MFCreateSample");
    *(FARPROC *)&pMFCreate2DMediaBuffer = GetProcAddress(mf, "MFCreate2DMediaBuffer");
    if (!pMFCreate2DMediaBuffer)
        logf_("mfplat has no MFCreate2DMediaBuffer -- cannot build a 2D frame");
}

static void release_obj(void *p)
{
    ULONG (WINAPI *rel)(void *) = (ULONG (WINAPI *)(void *))(*(void ***)p)[2];
    rel(p);
}

/* IMFSample sits on IMFAttributes, so its own methods start at 3 + 30 = 33:
 * GetSampleFlags 33, SetSampleFlags 34, GetSampleTime 35, SetSampleTime 36,
 * GetSampleDuration 37, SetSampleDuration 38, GetBufferCount 39,
 * GetBufferByIndex 40, ConvertToContiguousBuffer 41, AddBuffer 42,
 * RemoveBufferByIndex 43, RemoveAllBuffers 44.
 *
 * An earlier version of this file used 36 for AddBuffer, which is SetSampleTime
 * -- it would have called it with a buffer pointer as a timestamp. The branch
 * never ran, so it never did any harm, but the arithmetic was wrong. */
#define SLOT_SAMPLE_ADD_BUFFER     42
#define SLOT_SAMPLE_REMOVE_ALL     44

static HRESULT sample_add_buffer(void *sample, void *buffer)
{
    HRESULT (WINAPI *add)(void *, void *) =
        (HRESULT (WINAPI *)(void *, void *))(*(void ***)sample)[SLOT_SAMPLE_ADD_BUFFER];
    return add(sample, buffer);
}

static HRESULT sample_remove_all(void *sample)
{
    HRESULT (WINAPI *rm)(void *) =
        (HRESULT (WINAPI *)(void *))(*(void ***)sample)[SLOT_SAMPLE_REMOVE_ALL];
    return rm(sample);
}

/* Swap the caller's flat buffer for a 2D one, in the caller's own sample.
 *
 * Claiming PROVIDES_SAMPLES was not enough: Electra reads the flag and
 * allocates anyway. It keeps its sample either way, and it is the buffer inside
 * that it rejects -- so the buffer is what gets replaced, and the sample object
 * it tracks stays exactly the one it made. */
static BOOL give_sample_a_2d_buffer(void *sample)
{
    void *buffer = NULL;
    load_mfplat();
    if (!pMFCreate2DMediaBuffer || !frame_w || !frame_h) return FALSE;
    if (FAILED(pMFCreate2DMediaBuffer(frame_w, frame_h, 0x3231564e /* NV12 */,
                                      FALSE, &buffer)))
        return FALSE;
    sample_remove_all(sample);
    if (FAILED(sample_add_buffer(sample, buffer)))
    {
        release_obj(buffer);
        return FALSE;
    }
    release_obj(buffer);          /* the sample holds its own reference now */
    return TRUE;
}

static HRESULT (WINAPI *real_GetOutputAvailableType)(void *, DWORD, DWORD, void **);
static HRESULT (WINAPI *real_GetOutputStreamInfo)(void *, DWORD, void *);
static LONG frames_out, output_calls, input_calls;

/* Put NV12 back on the menu.
 *
 * CrossOver's winegstreamer censors NV12 from transform_GetOutputAvailableType
 * whenever it detects macOS -- the strings sit adjacent in the binary:
 *
 *     transform_GetOutputAvailableType / Skipping NV12 output format / Darwin
 *
 * Electra's H.264 decoder accepts NV12 and nothing else. It walks the offered
 * types looking for it, never finds it, reports "Failed to set video decoder
 * output type to NV12" and destroys the decoder. That is why SetInputType
 * succeeds here and SetOutputType is never reached, and why every earlier
 * lever did nothing: they all act downstream of a decoder that never starts.
 *
 * The censoring is only in the getter. SetOutputType validates against the
 * decoder's own output_types array, which still contains NV12 and carries no
 * macOS check -- so asking for NV12 is honoured. Re-labelling the offered type
 * hands Electra the name it is looking for and the format the MFT can really
 * produce.
 *
 * Set BEAST_NO_NV12=1 to watch without intervening. */
/* Required, as it turns out. Withholding the D3D manager does not make NV12
 * reappear: measured, the decoder then offers YV12, YV12, IYUV, I420, YUY2 and
 * no NV12 at all. So in an unpatched CrossOver the censoring really is
 * conditioned on is_macos() alone, exactly as the disassembly said -- the
 * have_d3d_manager condition is something winevideo ADDS in patch 0005, not
 * something already there.
 *
 * Relabelling is therefore not a shortcut around the patch, it is the patch,
 * done from outside. */
static BOOL restore_nv12 = FALSE;

/* Withhold the D3D manager from the decoder, without denying it to the game.
 *
 * Refusing MFCreateDXGIDeviceManager outright was too blunt: the game gave up
 * on video entirely and never touched the decoder. The manager it wants is for
 * its own renderer as much as for decoding, so it gets to have one -- the
 * decoder simply never hears about it, which is the state in which NV12 is not
 * censored and system-memory output is the honest answer. */
static BOOL withhold_d3d_from_mft = FALSE;

static const GUID guid_MFVideoFormat_NV12 =
    { 0x3231564e, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID guid_MF_MT_SUBTYPE =
    { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };

/* IMFAttributes vtable: GetGUID 10, SetGUID 24. */
static BOOL type_set_subtype(void *type, const GUID *sub)
{
    HRESULT (WINAPI *set_guid)(void *, const GUID *, const GUID *);
    void **vt;
    if (!type) return FALSE;
    vt = *(void ***)type;
    set_guid = (HRESULT (WINAPI *)(void *, const GUID *, const GUID *))vt[24];
    return SUCCEEDED(set_guid(type, &guid_MF_MT_SUBTYPE, sub));
}

/* IMFMediaType is an IMFAttributes: GetGUID is slot 3 + 7 = 10. */
static BOOL type_subtype(void *type, GUID *out)
{
    static const GUID mf_subtype =
        { 0xf7e34c9a, 0x42e8, 0x4714, { 0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5 } };
    HRESULT (WINAPI *get_guid)(void *, const GUID *, GUID *);
    void **vt;
    if (!type) return FALSE;
    vt = *(void ***)type;
    get_guid = (HRESULT (WINAPI *)(void *, const GUID *, GUID *))vt[10];
    return SUCCEEDED(get_guid(type, &mf_subtype, out));
}

static HRESULT WINAPI my_GetOutputAvailableType(void *self, DWORD stream,
                                                DWORD index, void **type)
{
    HRESULT hr = real_GetOutputAvailableType(self, stream, index, type);
    if (SUCCEEDED(hr) && type && *type)
    {
        GUID sub;
        if (type_subtype(*type, &sub))
        {
            char label[32];
            snprintf(label, sizeof(label), "offers type %lu", index);
            describe_subtype(label, &sub);

            if (restore_nv12 && !IsEqualGUID(&sub, &guid_MFVideoFormat_NV12))
            {
                static LONG said;
                if (type_set_subtype(*type, &guid_MFVideoFormat_NV12))
                {
                    if (InterlockedIncrement(&said) == 1)
                        logf_("  -> relabelled as NV12, which is what Electra "
                              "requires and what winegstreamer censored");
                }
                else if (InterlockedIncrement(&said) == 1)
                    logf_("  -> could NOT relabel to NV12");
            }
        }
        else
            logf_("  offers type %lu (subtype unreadable)", index);
    }
    else if (hr == 0xC00D36B9L)   /* MF_E_NO_MORE_TYPES */
        logf_("  ...that is all it offers (%lu total). The game picks from "
              "this list, and stops here if none will do.", index);
    else
        logf_("GetOutputAvailableType(%lu) -> 0x%08lx", index, hr);
    return hr;
}

static HRESULT WINAPI my_GetOutputStreamInfo(void *self, DWORD stream, void *info)
{
    HRESULT hr = real_GetOutputStreamInfo(self, stream, info);
    if (SUCCEEDED(hr) && info)
    {
        /* MFT_OUTPUT_STREAM_INFO: dwFlags at offset 4. Bit 0x100 =
         * PROVIDES_SAMPLES, which decides who allocates the frame. */
        DWORD flags = *(DWORD *)((BYTE *)info + 4);
        if (provide_samples && !(flags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
        {
            static LONG said;
            flags |= MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
            *(DWORD *)((BYTE *)info + 4) = flags;
            if (InterlockedIncrement(&said) == 1)
                logf_("GetOutputStreamInfo: claiming PROVIDES_SAMPLES so the "
                      "caller stops allocating flat buffers");
        }
        logf_("GetOutputStreamInfo: flags=0x%lx -- %s allocates the frame%s",
              flags, (flags & 0x100) ? "the DECODER" : "the CALLER",
              (flags & 0x100) ? "" : "   << a caller buffer is not IMF2DBuffer, "
              "and Electra rejects every frame that is not");
    }
    return hr;
}

static HRESULT WINAPI my_GetStreamCount(void *self, DWORD *in, DWORD *out)
{
    static LONG once;
    HRESULT hr = real_GetStreamCount(self, in, out);
    if (InterlockedIncrement(&once) == 1)
        logf_("GetStreamCount -> 0x%08lx  << CANARY: the vtable patch works, so "
              "anything not logged below genuinely is not being called", hr);
    return hr;
}

static HRESULT WINAPI my_SetInputType(void *self, DWORD stream, void *type, DWORD flags)
{
    HRESULT hr = real_SetInputType(self, stream, type, flags);
    logf_("SetInputType(flags=0x%lx) -> 0x%08lx%s", flags, hr,
          FAILED(hr) ? "   << the decoder refuses the stream it was chosen for" : "");
    return hr;
}


/* Teach the caller's own buffer to answer IMF2DBuffer.
 *
 * Electra's path, read from UE's VideoDecoderH264_DX.cpp rather than guessed:
 *
 *     GetBufferCount()                     must be exactly 1   (:1162)
 *     GetBufferByIndex(0, &Buffer)                             (:1167)
 *     Buffer->QueryInterface(IMF2DBuffer)  <- fails here       (:1192)
 *     Buffer2D->Lock2D(&Data, &Pitch)                          (:988)
 *     ... copies DecodedHeight * 3 / 2 rows ...
 *     Buffer2D->Unlock2D()
 *
 * So only Lock2D and Unlock2D are load-bearing, and nothing needs to be taken
 * away from the caller -- which is what crashed the game last time. The buffer
 * stays exactly where Electra put it; asking it for IMF2DBuffer now yields a
 * small object of ours that locks the very same memory and reports a pitch.
 *
 * NV12 in a flat buffer is contiguous, so the pitch is the frame width. */

static const GUID IID_IMF2DBuffer_ =
    { 0x7dc9d5f9, 0x9ed9, 0x44ec, { 0x9b, 0xbf, 0x06, 0x00, 0xbb, 0x58, 0x9f, 0xbb } };
static const GUID IID_IUnknown_ =
    { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

/* IMFMediaBuffer: Lock 3, Unlock 4, GetCurrentLength 5. */
struct two_d
{
    void **vtbl;
    LONG refs;
    void *inner;          /* the real IMFMediaBuffer, AddRef'd */
    BYTE *locked;
};

static HRESULT WINAPI td_QueryInterface(void *self, const GUID *iid, void **out)
{
    struct two_d *td = (struct two_d *)self;
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IMF2DBuffer_) || IsEqualGUID(iid, &IID_IUnknown_))
    {
        InterlockedIncrement(&td->refs);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI td_AddRef(void *self)
{
    return InterlockedIncrement(&((struct two_d *)self)->refs);
}

static ULONG WINAPI td_Release(void *self)
{
    struct two_d *td = (struct two_d *)self;
    LONG n = InterlockedDecrement(&td->refs);
    if (n == 0)
    {
        if (td->inner) release_obj(td->inner);
        HeapFree(GetProcessHeap(), 0, td);
    }
    return n;
}

static HRESULT WINAPI td_Lock2D(void *self, BYTE **scanline0, LONG *pitch)
{
    struct two_d *td = (struct two_d *)self;
    HRESULT (WINAPI *lock)(void *, BYTE **, DWORD *, DWORD *) =
        (HRESULT (WINAPI *)(void *, BYTE **, DWORD *, DWORD *))(*(void ***)td->inner)[3];
    BYTE *data = NULL;
    DWORD max = 0, cur = 0;
    HRESULT hr = lock(td->inner, &data, &max, &cur);
    if (FAILED(hr)) return hr;
    td->locked = data;
    if (scanline0) *scanline0 = data;
    if (pitch) *pitch = (LONG)frame_w;
    return S_OK;
}

static HRESULT WINAPI td_Unlock2D(void *self)
{
    struct two_d *td = (struct two_d *)self;
    HRESULT (WINAPI *unlock)(void *) =
        (HRESULT (WINAPI *)(void *))(*(void ***)td->inner)[4];
    td->locked = NULL;
    return unlock(td->inner);
}

static HRESULT WINAPI td_GetScanline0AndPitch(void *self, BYTE **scanline0, LONG *pitch)
{
    struct two_d *td = (struct two_d *)self;
    if (!td->locked) return 0xC00D36B2L;  /* MF_E_INVALIDREQUEST */
    if (scanline0) *scanline0 = td->locked;
    if (pitch) *pitch = (LONG)frame_w;
    return S_OK;
}

static HRESULT WINAPI td_IsContiguousFormat(void *self, BOOL *contiguous)
{
    (void)self;
    if (contiguous) *contiguous = TRUE;
    return S_OK;
}

static HRESULT WINAPI td_GetContiguousLength(void *self, DWORD *len)
{
    (void)self;
    if (len) *len = frame_w * frame_h * 3 / 2;
    return S_OK;
}

static HRESULT WINAPI td_ContiguousCopyTo(void *self, BYTE *dest, DWORD size)
{
    BYTE *src = NULL;
    LONG pitch = 0;
    HRESULT hr = td_Lock2D(self, &src, &pitch);
    if (FAILED(hr)) return hr;
    CopyMemory(dest, src, size);
    td_Unlock2D(self);
    return S_OK;
}

static HRESULT WINAPI td_ContiguousCopyFrom(void *self, const BYTE *src, DWORD size)
{
    BYTE *dst = NULL;
    LONG pitch = 0;
    HRESULT hr = td_Lock2D(self, &dst, &pitch);
    if (FAILED(hr)) return hr;
    CopyMemory(dst, src, size);
    td_Unlock2D(self);
    return S_OK;
}

static void *two_d_vtbl[10] =
{
    (void *)td_QueryInterface, (void *)td_AddRef, (void *)td_Release,
    (void *)td_Lock2D, (void *)td_Unlock2D, (void *)td_GetScanline0AndPitch,
    (void *)td_IsContiguousFormat, (void *)td_GetContiguousLength,
    (void *)td_ContiguousCopyTo, (void *)td_ContiguousCopyFrom
};

/* The buffer's own QueryInterface, patched once on its shared vtable. */
static HRESULT (WINAPI *real_buffer_QI)(void *, const GUID *, void **);

static HRESULT WINAPI my_buffer_QueryInterface(void *self, const GUID *iid, void **out)
{
    /* This sits on the shared vtable of every media buffer of its class, so it
     * is entered constantly and from every thread. If the original pointer is
     * not stored yet there is nothing safe to do but decline -- calling
     * through a null pointer is what took the process down. */
    if (!real_buffer_QI)
    {
        if (out) *out = NULL;
        return E_NOINTERFACE;
    }
    if (iid && IsEqualGUID(iid, &IID_IMF2DBuffer_))
    {
        HRESULT hr = real_buffer_QI(self, iid, out);
        if (SUCCEEDED(hr)) return hr;          /* already 2D: leave it alone */
        {
            struct two_d *td = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*td));
            static LONG said;
            if (!td) return E_OUTOFMEMORY;
            td->vtbl = two_d_vtbl;
            td->refs = 1;
            td->inner = self;
            ((ULONG (WINAPI *)(void *))(*(void ***)self)[1])(self);   /* AddRef inner */
            *out = td;
            if (InterlockedIncrement(&said) == 1)
                logf_("gave Electra an IMF2DBuffer over its own buffer "
                      "(%ux%u, pitch %u) -- nothing taken away this time",
                      frame_w, frame_h, frame_w);
            return S_OK;
        }
    }
    return real_buffer_QI(self, iid, out);
}


/* Make Electra take its software path, by changing its mind rather than the
 * decoder's.
 *
 * Electra does not ask the MFT whether decoding is in software -- withholding
 * SET_D3D_MANAGER was therefore useless. It asks its OWN platform handle,
 * IsSoftware(), and because CrossOver's winegstreamer still advertises
 * MF_SA_D3D_AWARE it has already built itself a D3D11 device and answers no.
 * It then takes the branch that requires IMFDXGIBuffer on the output buffer,
 * which a system-memory buffer can never satisfy, and drops every frame
 * without ever reaching the IMF2DBuffer query we hooked.
 *
 * winevideo reaches the same end by patching winegstreamer to report no D3D
 * awareness on macOS (patch 0005, "UE ElectraPlayer takes its software decode
 * path on macOS"). From inside the process the equivalent is to answer the two
 * questions Electra actually asks:
 *
 *   1. the console variable that selects the upload-heap path -> 1, so the
 *      decoder stores the sample and queries nothing
 *   2. IsSoftware() -> true at both call sites, so the consumer takes the
 *      branch that accepts a plain buffer
 *
 * The second site is not optional. If the renderer reports D3D11 the outer
 * gate is false, DecodedHeight stays at the frame height instead of one and a
 * half times it, and the renderer is handed a luma-only frame.
 *
 * Offsets are from the disassembly of this exact executable, so every one is
 * verified against the bytes that should be there before anything is written.
 * A game update moves them, and then this must do nothing rather than corrupt
 * something. */

#define RVA_CVAR_PTR      0x0AA29110   /* TConsoleVariableData<int32> ** */
#define RVA_ISSW_EXTRA    0x0634DAAA   /* call *0x28 feeding the "sw" value */
#define RVA_ISSW_GATE     0x0634D8D7   /* call *0x28 feeding the outer gate  */

static BOOL electra_sw_forced;

/* mov al,1 ; nop  -- returns true and leaves the following code untouched. */
static const BYTE want_call[3] = { 0xFF, 0x50, 0x28 };
static const BYTE make_true[3] = { 0xB0, 0x01, 0x90 };

static BOOL poke(BYTE *at, const BYTE *expect, const BYTE *with, const char *what)
{
    DWORD old;
    if (memcmp(at, expect, 3) != 0)
    {
        logf_("  %s: bytes are %02X %02X %02X, expected %02X %02X %02X -- this is "
              "a different build, leaving it alone", what,
              at[0], at[1], at[2], expect[0], expect[1], expect[2]);
        return FALSE;
    }
    if (!VirtualProtect(at, 3, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(at, with, 3);
    VirtualProtect(at, 3, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 3);
    logf_("  %s: patched", what);
    return TRUE;
}

static void force_electra_software(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    int done = 0;
    if (electra_sw_forced || !base) return;
    electra_sw_forced = TRUE;

    logf_("forcing Electra onto its software path");
    if (poke(base + RVA_ISSW_EXTRA, want_call, make_true, "IsSoftware (sw value)")) done++;
    if (poke(base + RVA_ISSW_GATE,  want_call, make_true, "IsSoftware (outer gate)")) done++;

    {
        int *cvar = *(int **)(base + RVA_CVAR_PTR);
        if (cvar)
        {
            cvar[0] = 1;
            cvar[1] = 1;
            logf_("  UseOldOutputPath console variable: set to 1");
            done++;
        }
        else
            logf_("  console variable not resolved yet");
    }
    if (!done)
        logf_("nothing was patched -- this build does not match the offsets");
}

static HRESULT WINAPI my_ProcessOutput(void *self, DWORD flags, DWORD count,
                                       void *samples, DWORD *status)
{
    OUT_DATA_BUFFER *out = (OUT_DATA_BUFFER *)samples;
    void *ours = NULL, *buffer = NULL;
    HRESULT hr;

    /* The caller believed us and passed nothing, so build the frame it would
     * have built -- on a 2D buffer, which is the whole point. */
    if (provide_samples && out && count >= 1 && out[0].pSample)
    {
        static LONG told, failed;
        if (give_sample_a_2d_buffer(out[0].pSample))
        {
            if (InterlockedIncrement(&told) == 1)
                logf_("the caller allocates anyway, so its flat buffer is swapped "
                      "for a 2D NV12 one of %ux%u -- same sample, different buffer",
                      frame_w, frame_h);
        }
        else if (InterlockedIncrement(&failed) == 1)
            logf_("could not swap in a 2D buffer (%ux%u)", frame_w, frame_h);
    }
    if (provide_samples && out && count >= 1 && !out[0].pSample && frame_w && frame_h)
    {
        load_mfplat();
        if (pMFCreateSample && pMFCreate2DMediaBuffer
            && SUCCEEDED(pMFCreateSample(&ours))
            && SUCCEEDED(pMFCreate2DMediaBuffer(frame_w, frame_h, 0x3231564e /* NV12 */,
                                                FALSE, &buffer))
            && SUCCEEDED(sample_add_buffer(ours, buffer)))
        {
            static LONG said;
            out[0].pSample = ours;
            if (InterlockedIncrement(&said) == 1)
                logf_("supplying a 2D NV12 sample %ux%u of our own", frame_w, frame_h);
        }
        else
        {
            if (buffer) release_obj(buffer);
            if (ours) { release_obj(ours); ours = NULL; }
            logf_("could not build a 2D sample");
        }
        if (buffer) release_obj(buffer);   /* the sample holds its own reference */
    }

    hr = real_ProcessOutput(self, flags, count, samples, status);

    /* Patch the buffer's QueryInterface on its shared vtable, once, using a
     * real buffer as the way in -- there is no other handle on that class. */
    if (SUCCEEDED(hr) && out && count >= 1 && out[0].pSample && !real_buffer_QI)
    {
        void *buffer = NULL;
        HRESULT (WINAPI *by_index)(void *, DWORD, void **) =
            (HRESULT (WINAPI *)(void *, DWORD, void **))(*(void ***)out[0].pSample)[40];
        if (SUCCEEDED(by_index(out[0].pSample, 0, &buffer)) && buffer)
        {
            static void *saved;
            /* Order matters and cost a crash. patch_slot arms the vtable and
             * only then returns, so assigning real_buffer_QI afterwards leaves
             * a window in which every QueryInterface in the process arrives
             * here with nothing to call through. Publish the original first. */
            void **vt = *(void ***)buffer;
            real_buffer_QI =
                (HRESULT (WINAPI *)(void *, const GUID *, void **))vt[0];
            if (!patch_slot("buffer QueryInterface", buffer, 0,
                            (void *)my_buffer_QueryInterface, &saved))
                real_buffer_QI = NULL;
            release_obj(buffer);
        }
    }
    if (FAILED(hr) && ours && out)
    {
        release_obj(ours);
        out[0].pSample = NULL;
    }
    LONG n = InterlockedIncrement(&output_calls);
    if (SUCCEEDED(hr))
    {
        LONG f = InterlockedIncrement(&frames_out);
        if (f == 1 || f == 10 || f == 100)
            logf_("ProcessOutput: frame %ld decoded OK  << the decoder works; "
                  "if the screen is black the frame is being lost after this", f);
    }
    else if (n == 1 || (n % 200) == 0)
    {
        logf_("ProcessOutput -> 0x%08lx after %ld calls, %ld frames so far",
              hr, n, frames_out);
        if (hr == 0xC00D6D72L) logf_("  (MF_E_TRANSFORM_NEED_MORE_INPUT -- normal)");
    }
    return hr;
}

static HRESULT WINAPI my_ProcessInput(void *self, DWORD stream, void *sample, DWORD flags)
{
    HRESULT hr = real_ProcessInput(self, stream, sample, flags);
    LONG n = InterlockedIncrement(&input_calls);
    if (FAILED(hr) && (n == 1 || (n % 200) == 0))
        logf_("ProcessInput -> 0x%08lx (call %ld)", hr, n);
    return hr;
}

/* MF_MT_FRAME_SIZE packs width in the high half and height in the low half. */
static void capture_frame_size(void *type)
{
    static const GUID mf_frame_size =
        { 0x1652c33d, 0xd6b2, 0x4012, { 0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d } };
    HRESULT (WINAPI *get_u64)(void *, const GUID *, UINT64 *);
    UINT64 packed = 0;
    if (!type) return;
    get_u64 = (HRESULT (WINAPI *)(void *, const GUID *, UINT64 *))(*(void ***)type)[8];
    if (SUCCEEDED(get_u64(type, &mf_frame_size, &packed)))
    {
        frame_w = (UINT32)(packed >> 32);
        frame_h = (UINT32)packed;
        logf_("  frame size %ux%u", frame_w, frame_h);
    }
}

static HRESULT WINAPI my_SetOutputType(void *self, DWORD stream, void *type, DWORD flags)
{
    HRESULT hr = real_SetOutputType(self, stream, type, flags);
    if (SUCCEEDED(hr)) capture_frame_size(type);
    logf_("SetOutputType(flags=0x%lx) -> 0x%08lx%s", flags, hr,
          FAILED(hr) ? "   << no agreed output format means no picture, ever" : "");
    return hr;
}

static HRESULT WINAPI my_ProcessMessage(void *self, DWORD message, ULONG_PTR param)
{
    HRESULT hr;
    if (message == 0x00000002 && withhold_d3d_from_mft) /* MFT_MESSAGE_SET_D3D_MANAGER */
    {
        logf_("ProcessMessage(SET_D3D_MANAGER, %p) -- WITHHELD from the decoder; "
              "the game keeps its manager, the decoder stays on system memory",
              (void *)param);
        return S_OK;
    }
    hr = real_ProcessMessage(self, message, param);
    if (message == 0x00000002)
        logf_("ProcessMessage(SET_D3D_MANAGER, %p) -> 0x%08lx", (void *)param, hr);
    return hr;
}

static HRESULT WINAPI my_ActivateObject(void *self, REFIID iid, void **out)
{
    HRESULT hr = real_ActivateObject(self, iid, out);
    if (iid)
        logf_("IMFActivate::ActivateObject asked for "
              "{%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X}",
              iid->Data1, iid->Data2, iid->Data3,
              iid->Data4[0], iid->Data4[1], iid->Data4[2], iid->Data4[3],
              iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7]);
    logf_("IMFActivate::ActivateObject -> 0x%08lx%s", hr,
          FAILED(hr) ? "   << the decoder MFTEnumEx promised does NOT exist" : "");
    if (SUCCEEDED(hr) && out && *out)
    {
        static void *st, *pm, *pi, *po, *gc, *si;
        {
            static void *ga, *gi;
            patch_slot("GetOutputAvailableType", *out, SLOT_GET_OUTPUT_AVAIL,
                       (void *)my_GetOutputAvailableType, &ga);
            patch_slot("GetOutputStreamInfo", *out, SLOT_GET_OUTPUT_INFO,
                       (void *)my_GetOutputStreamInfo, &gi);
            real_GetOutputAvailableType =
                (HRESULT (WINAPI *)(void *, DWORD, DWORD, void **))ga;
            real_GetOutputStreamInfo =
                (HRESULT (WINAPI *)(void *, DWORD, void *))gi;
        }
        patch_slot("GetStreamCount", *out, SLOT_GET_STREAM_COUNT, (void *)my_GetStreamCount, &gc);
        patch_slot("SetInputType", *out, SLOT_SET_INPUT_TYPE,  (void *)my_SetInputType,  &si);
        real_GetStreamCount = (HRESULT (WINAPI *)(void *, DWORD *, DWORD *))gc;
        real_SetInputType   = (HRESULT (WINAPI *)(void *, DWORD, void *, DWORD))si;
        patch_slot("SetOutputType", *out, SLOT_SET_OUTPUT_TYPE, (void *)my_SetOutputType, &st);
        patch_slot("ProcessMessage", *out, SLOT_PROCESS_MESSAGE, (void *)my_ProcessMessage, &pm);
        patch_slot("ProcessInput", *out, SLOT_PROCESS_INPUT,   (void *)my_ProcessInput,   &pi);
        patch_slot("ProcessOutput", *out, SLOT_PROCESS_OUTPUT,  (void *)my_ProcessOutput,  &po);
        real_SetOutputType  = (HRESULT (WINAPI *)(void *, DWORD, void *, DWORD))st;
        real_ProcessMessage = (HRESULT (WINAPI *)(void *, DWORD, ULONG_PTR))pm;
        real_ProcessInput   = (HRESULT (WINAPI *)(void *, DWORD, void *, DWORD))pi;
        real_ProcessOutput  = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void *, DWORD *))po;
        {
            void **vt = *(void ***)*out;
            logf_("  transform vtable %p: [3]=%p [4]=%p [15]=%p [16]=%p [24]=%p [25]=%p",
                  (void *)vt, vt[3], vt[4], vt[15], vt[16], vt[24], vt[25]);
        }
        logf_("  decoder instantiated and now watched");

    }
    return hr;
}

static HRESULT (WINAPI *real_MFStartup)(ULONG, DWORD);
static HRESULT (WINAPI *real_MFTEnumEx)(GUID, UINT32, const REG_TYPE_INFO *,
                                        const REG_TYPE_INFO *, void ***, UINT32 *);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromByteStream)(void *, void *, void **);
static HRESULT (WINAPI *real_MFCreateSourceReaderFromURL)(LPCWSTR, void *, void **);
static HRESULT (WINAPI *real_MFCreateDXGIDeviceManager)(UINT *, void **);

/* Refuse the DXGI device manager.
 *
 * The game asks Media Foundation to decode straight into D3D textures. Under
 * D3DMetal that path produces no picture -- the sound, which never goes near
 * it, is unaffected, which is exactly the symptom.
 *
 * Failing this is not a lie: a machine with no D3D video support is a state
 * Windows itself can be in, and a player that asks for hardware decoding is
 * expected to cope with not getting it. If the game falls back to software the
 * frame arrives in system memory, and this is the whole fix. If it refuses to
 * play at all, that is worth knowing too, and it is one file to put back.
 *
 * Set BEAST_ALLOW_D3D_MANAGER=1 in the bottle to watch without interfering. */
/* Default OFF now. Driving this from the bottle environment did not work --
 * a live wineserver kept the old config and the run silently repeated the
 * previous condition while looking like the new one. A build flag cannot do
 * that: the log line states which build is running, and it comes from the same
 * variable the code branches on. */
static BOOL refuse_d3d_manager = FALSE;


/* Follow the DXGI device manager, which is where this stops.
 *
 * The game resolves exactly three Media Foundation functions -- MFStartup,
 * MFTEnumEx and MFCreateDXGIDeviceManager -- and never asks for
 * MFCreateSourceReaderFromURL, which is in its binary. So it gives up between
 * creating the manager and opening a file.
 *
 * What sits in that gap is ResetDevice: a manager is useless until a D3D11
 * device is bound to it. If that fails under D3DMetal the player has nowhere
 * to decode into and stops without ever reaching the video.
 *
 * IMFDXGIDeviceManager: CloseDeviceHandle 3, GetVideoService 4, LockDevice 5,
 * OpenDeviceHandle 6, ResetDevice 7, TestDevice 8, UnlockDevice 9.
 */
#define SLOT_DXGIMGR_RESETDEVICE 7
#define SLOT_DXGIMGR_TESTDEVICE  8

static HRESULT (WINAPI *real_ResetDevice)(void *, void *, UINT);
static HRESULT (WINAPI *real_TestDevice)(void *, HANDLE);

static HRESULT WINAPI my_ResetDevice(void *self, void *device, UINT token)
{
    HRESULT hr = real_ResetDevice(self, device, token);
    logf_("IMFDXGIDeviceManager::ResetDevice(device=%p) -> 0x%08lx%s",
          device, hr,
          FAILED(hr) ? "   << the manager has no device, so nothing can decode"
                     : "   << a device is bound");
    return hr;
}

static HRESULT WINAPI my_TestDevice(void *self, HANDLE h)
{
    static LONG said;
    HRESULT hr = real_TestDevice(self, h);
    if (InterlockedIncrement(&said) <= 2)
        logf_("IMFDXGIDeviceManager::TestDevice -> 0x%08lx", hr);
    return hr;
}

static HRESULT WINAPI my_MFCreateDXGIDeviceManager(UINT *token, void **manager)
{
    if (refuse_d3d_manager)
    {
        logf_("MFCreateDXGIDeviceManager -- REFUSED, so decoding has to go to "
              "software; the frame then arrives in system memory");
        if (token) *token = 0;
        if (manager) *manager = NULL;
        return E_NOTIMPL;
    }
    {
        HRESULT hr = real_MFCreateDXGIDeviceManager(token, manager);
        logf_("MFCreateDXGIDeviceManager -> 0x%08lx", hr);
        if (SUCCEEDED(hr) && manager && *manager)
        {
            static void *rd, *td;
            void **vt = *(void ***)*manager;
            real_ResetDevice = (HRESULT (WINAPI *)(void *, void *, UINT))vt[SLOT_DXGIMGR_RESETDEVICE];
            real_TestDevice  = (HRESULT (WINAPI *)(void *, HANDLE))vt[SLOT_DXGIMGR_TESTDEVICE];
            if (!patch_slot("dxgi manager ResetDevice", *manager,
                            SLOT_DXGIMGR_RESETDEVICE, (void *)my_ResetDevice, &rd))
                real_ResetDevice = NULL;
            if (!patch_slot("dxgi manager TestDevice", *manager,
                            SLOT_DXGIMGR_TESTDEVICE, (void *)my_TestDevice, &td))
                real_TestDevice = NULL;
        }
        return hr;
    }
}

static HRESULT WINAPI my_MFStartup(ULONG version, DWORD flags)
{
    HRESULT hr = real_MFStartup ? real_MFStartup(version, flags) : S_OK;
    logf_("MFStartup(version=0x%lx, flags=0x%lx) -> 0x%08lx  "
          "<< Media Foundation IS in play", version, flags, hr);
    return hr;
}

/* Answer the capability question honestly enough.
 *
 * The game asks whether a VP9 decoder exists before it will try to open
 * anything, and a stock CrossOver bottle registers no video decoder MFTs at
 * all, so the answer is no and it exits without ever calling
 * MFCreateSourceReaderFromURL.
 *
 * What it does with a yes is set one bit -- the disassembly shows count > 0
 * leading to a single `mov byte [...], 1`. It never activates those objects.
 * The decoding happens later, through the source reader, which reaches
 * GStreamer by way of the .webm byte-stream handler and CrossOver's own VP9
 * support, which does exist: Preview decodes profile 0 and 2 through
 * vp9parse -> vtdec_hw.
 *
 * So the missing thing is the declaration, not the decoder. Rather than
 * fabricating an array the game would have to release, the query is repeated
 * for H.264 -- a format that really is registered -- so what comes back is a
 * genuine list of real objects with a real lifetime. The game counts them and
 * carries on.
 *
 * Set NG4_NO_MFT_ANSWER=1 to watch without this. */
static BOOL answer_mft_gate = TRUE;

static const GUID guid_VP90 =
    { 0x30395056, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID guid_H264 =
    { 0x34363248, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static HRESULT WINAPI my_MFTEnumEx(GUID category, UINT32 flags,
                                   const REG_TYPE_INFO *in,
                                   const REG_TYPE_INFO *out,
                                   void ***mfts, UINT32 *count)
{
    HRESULT hr = real_MFTEnumEx(category, flags, in, out, mfts, count);

    if (answer_mft_gate && in && count && *count == 0
        && IsEqualGUID(&in->guidSubtype, &guid_VP90))
    {
        REG_TYPE_INFO substitute = *in;
        static LONG said;
        substitute.guidSubtype = guid_H264;
        hr = real_MFTEnumEx(category, flags, &substitute, out, mfts, count);
        if (InterlockedIncrement(&said) == 1)
            logf_("VP90 had no decoder; asked again for H264 and got %u -- the "
                  "game only counts these, it never activates them",
                  count ? *count : 0);
    }
    logf_("MFTEnumEx flags=0x%lx -> 0x%08lx, %u decoder(s) offered",
          flags, hr, count ? *count : 0);
    if (in)  describe_subtype("wants to decode", &in->guidSubtype);
    /* Watch the promised decoder actually be created. */
    if (SUCCEEDED(hr) && mfts && *mfts && count && *count > 0)
    {
        static void *ao;
        if (patch_slot("ActivateObject", (*mfts)[0], SLOT_ACTIVATE_OBJECT, (void *)my_ActivateObject, &ao))
            real_ActivateObject = (HRESULT (WINAPI *)(void *, REFIID, void **))ao;
    }
    if (out) describe_subtype("wants out as  ", &out->guidSubtype);
    if (count && *count == 0)
        logf_("  NOTHING can decode that here -- this is the failure, if the "
              "picture is missing and the sound is not");
    return hr;
}


/* Follow the source reader itself.
 *
 * The reader is created and the file opens -- so the container is fine, and
 * everything about codecs and registration is settled. What is not settled is
 * what happens next: which streams the reader offers, whether the game and the
 * reader agree a media type, and whether ReadSample ever returns a sample.
 *
 * IMFSourceReader vtable, after IUnknown's three:
 *   GetStreamSelection 3, SetStreamSelection 4, GetNativeMediaType 5,
 *   GetCurrentMediaType 6, SetCurrentMediaType 7, SetCurrentPosition 8,
 *   ReadSample 9, Flush 10, GetServiceForStream 11, GetPresentationAttribute 12
 */
#define SLOT_GET_NATIVE_TYPE  5
#define SLOT_SET_CURRENT_TYPE 7
#define SLOT_READ_SAMPLE      9

static HRESULT (WINAPI *real_GetNativeMediaType)(void *, DWORD, DWORD, void **);
static HRESULT (WINAPI *real_SetCurrentMediaType)(void *, DWORD, DWORD *, void *);
static HRESULT (WINAPI *real_ReadSample)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                         LONGLONG *, void **);

static HRESULT WINAPI my_GetNativeMediaType(void *self, DWORD stream, DWORD index, void **type)
{
    HRESULT hr = real_GetNativeMediaType(self, stream, index, type);
    if (SUCCEEDED(hr) && type && *type)
    {
        GUID sub;
        if (type_subtype(*type, &sub))
        {
            char label[48];
            snprintf(label, sizeof(label), "stream %lu offers", stream);
            describe_subtype(label, &sub);
        }
    }
    else if (hr != 0xC00D36B3L /* MF_E_NO_MORE_TYPES */ && hr != 0xC00D36BAL)
        logf_("GetNativeMediaType(stream %lu, %lu) -> 0x%08lx", stream, index, hr);
    return hr;
}

static HRESULT WINAPI my_SetCurrentMediaType(void *self, DWORD stream, DWORD *reserved, void *type)
{
    HRESULT hr = real_SetCurrentMediaType(self, stream, reserved, type);
    GUID sub;
    if (type && type_subtype(type, &sub))
    {
        char label[64];
        snprintf(label, sizeof(label), "stream %lu asked for", stream);
        describe_subtype(label, &sub);
    }
    logf_("SetCurrentMediaType(stream %lu) -> 0x%08lx%s", stream, hr,
          FAILED(hr) ? "   << no agreed format on this stream" : "");
    return hr;
}

static HRESULT WINAPI my_ReadSample(void *self, DWORD stream, DWORD flags,
                                    DWORD *actual, DWORD *sflags,
                                    LONGLONG *ts, void **sample)
{
    static LONG calls, got, failed;
    HRESULT hr = real_ReadSample(self, stream, flags, actual, sflags, ts, sample);
    LONG n = InterlockedIncrement(&calls);
    if (SUCCEEDED(hr) && sample && *sample)
    {
        LONG g = InterlockedIncrement(&got);
        if (g == 1 || g == 50)
            logf_("ReadSample: sample %ld arrived  << the reader is producing", g);
    }
    else if (FAILED(hr))
    {
        if (InterlockedIncrement(&failed) == 1)
            logf_("ReadSample -> 0x%08lx on call %ld  << nothing comes out", hr, n);
    }
    else if (n == 1 || n == 200)
        logf_("ReadSample: no sample, flags 0x%lx (call %ld, %ld so far)",
              sflags ? *sflags : 0, n, got);
    return hr;
}

static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(void *stream, void *attrs, void **reader)
{
    static LONG made;
    HRESULT hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
    LONG n = InterlockedIncrement(&made);
    if (n == 1 || FAILED(hr))
        logf_("MFCreateSourceReaderFromByteStream -> 0x%08lx (reader %ld)", hr, n);
    if (SUCCEEDED(hr) && reader && *reader)
    {
        static void *gn, *sc, *rs;
        patch_slot("GetNativeMediaType",  *reader, SLOT_GET_NATIVE_TYPE,
                   (void *)my_GetNativeMediaType,  &gn);
        patch_slot("SetCurrentMediaType", *reader, SLOT_SET_CURRENT_TYPE,
                   (void *)my_SetCurrentMediaType, &sc);
        patch_slot("ReadSample",          *reader, SLOT_READ_SAMPLE,
                   (void *)my_ReadSample,          &rs);
        real_GetNativeMediaType  = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void **))gn;
        real_SetCurrentMediaType = (HRESULT (WINAPI *)(void *, DWORD, DWORD *, void *))sc;
        real_ReadSample = (HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                              LONGLONG *, void **))rs;
    }
    return hr;
}

/*
 * Resolve by content when resolving by extension fails.
 *
 * MFCreateSourceReaderFromURL picks a handler from the registry by file
 * extension. MFCreateSourceReaderFromByteStream picks one by looking at the
 * bytes. That difference is the whole reason DYNASTY WARRIORS plays in a
 * bottle where this title does not: one asks what the file is called, the
 * other asks what it is.
 *
 * A failure here is fatal to this game -- it calls exit(-1) and leaves a black
 * screen with no crash report -- so the retry is worth having even where the
 * registry mapping is present. It also covers the one file out of four hundred
 * that is not a WebM at all: an .msd holding H.264 in MP4, whose extension no
 * handler claims either, and which could otherwise turn a working fix into a
 * hard exit on one cutscene.
 *
 * Written as a fallback rather than a replacement: the real call goes first
 * and its result is kept whenever it succeeds, so this can only add outcomes.
 */
static HRESULT (WINAPI *real_MFCreateFile)(DWORD, DWORD, DWORD, LPCWSTR, void **);

static HRESULT WINAPI my_MFCreateSourceReaderFromURL(LPCWSTR url, void *attrs, void **reader)
{
    HRESULT hr = real_MFCreateSourceReaderFromURL(url, attrs, reader);
    logf_("MFCreateSourceReaderFromURL(%ls) -> 0x%08lx", url ? url : L"(null)", hr);

    if (FAILED(hr) && url && reader
        && real_MFCreateFile && real_MFCreateSourceReaderFromByteStream)
    {
        void *stream = NULL;
        /* MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE */
        HRESULT open = real_MFCreateFile(1, 0, 0, url, &stream);
        logf_("  by extension refused; opening it ourselves -> 0x%08lx", open);
        if (SUCCEEDED(open) && stream)
        {
            HRESULT again = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
            logf_("  resolved by content -> 0x%08lx", again);
            IUnknown_Release((IUnknown *)stream);
            if (SUCCEEDED(again)) return again;
        }
    }
    return hr;
}

/* Delay-loaded imports are resolved through GetProcAddress, so this is where
 * the hooks actually land for this game. */
static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI my_GetProcAddress(HMODULE module, LPCSTR name)
{
    FARPROC proc = real_GetProcAddress(module, name);
    if (!proc || !name || ((ULONG_PTR)name >> 16) == 0)
        return proc;

#define SWAP(fn)                                                          \
    if (lstrcmpiA(name, #fn) == 0) {                                      \
        if (!real_##fn) { *(FARPROC *)&real_##fn = proc; }                \
        logf_("GetProcAddress(\"%s\") -- hooked", name);                  \
        return (FARPROC)my_##fn;                                          \
    }
    SWAP(MFStartup)
    SWAP(MFTEnumEx)
    SWAP(MFCreateSourceReaderFromByteStream)
    SWAP(MFCreateSourceReaderFromURL)
    SWAP(MFCreateDXGIDeviceManager)
    SWAP(D3D12CreateDevice)
#undef SWAP

    /* Name every media entry point the game asks for, resolved or not. The
     * list of what it looks for is itself evidence about which player it uses. */
    if (name[0] == 'M' && name[1] == 'F')
        logf_("GetProcAddress(\"%s\") -> %s", name, proc ? "ok" : "NOT FOUND");
    return proc;
}

static void *hook_import(const char *dll, const char *func, void *replacement)
{
    HMODULE base = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    void *original = NULL;

    if (!dir->VirtualAddress) return NULL;
    imp = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)base + dir->VirtualAddress);

    for (; imp->Name; imp++)
    {
        const char *name = (const char *)((BYTE *)base + imp->Name);
        IMAGE_THUNK_DATA *orig, *iat;
        if (lstrcmpiA(name, dll) != 0) continue;

        orig = (IMAGE_THUNK_DATA *)((BYTE *)base + imp->OriginalFirstThunk);
        iat  = (IMAGE_THUNK_DATA *)((BYTE *)base + imp->FirstThunk);
        for (; orig->u1.AddressOfData; orig++, iat++)
        {
            IMAGE_IMPORT_BY_NAME *by;
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            by = (IMAGE_IMPORT_BY_NAME *)((BYTE *)base + orig->u1.AddressOfData);
            if (lstrcmpiA((const char *)by->Name, func) != 0) continue;

            original = (void *)iat->u1.Function;
            if (VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old))
            {
                iat->u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            }
            return original;
        }
    }
    return original;
}


/* Watch the D3D9 side, where this game is understood to stop.
 *
 * The reader negotiates video and audio and then never reads a sample, so the
 * player gives up before asking for data. Persona 5 Strikers decodes with
 * Media Foundation on D3D11 and presents through D3D9, sharing a surface
 * between the two, and Wine's d3d9 answers "Resource sharing not implemented"
 * -- the string is still in Preview's copy.
 *
 * That is the shape of the explanation, and it is exactly the kind of tidy
 * story that has been wrong three times today, so it gets measured.
 *
 * IDirect3D9:      CreateDevice 16
 * IDirect3DDevice9: CreateTexture 23, CreateRenderTarget 28,
 *                   CreateOffscreenPlainSurface 36
 */
#define SLOT_D3D9_CREATE_DEVICE   16
#define SLOT_DEV_CREATE_TEXTURE   23
#define SLOT_DEV_CREATE_RT        28
#define SLOT_DEV_CREATE_OFFSCREEN 36

static void *(WINAPI *real_Direct3DCreate9)(UINT);
static HRESULT (WINAPI *real_Direct3DCreate9Ex)(UINT, void **);
static HRESULT (WINAPI *real_CreateDevice)(void *, UINT, DWORD, HWND, DWORD, void *, void **);
static HRESULT (WINAPI *real_CreateTexture)(void *, UINT, UINT, UINT, DWORD, DWORD, DWORD, void **, HANDLE *);
static HRESULT (WINAPI *real_CreateRenderTarget)(void *, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void **, HANDLE *);
static HRESULT (WINAPI *real_CreateOffscreen)(void *, UINT, UINT, DWORD, DWORD, void **, HANDLE *);

static void note_shared(const char *what, UINT w, UINT h, DWORD fmt,
                        HRESULT hr, HANDLE *shared)
{
    static LONG said[4];
    int i = what[0] & 3;
    if (!shared) return;                 /* not a sharing request; uninteresting */
    if (InterlockedIncrement(&said[i]) > 3) return;
    logf_("%s(%ux%u fmt=%lu, SHARED requested) -> 0x%08lx, handle %p%s",
          what, w, h, fmt, hr, *shared,
          (SUCCEEDED(hr) && !*shared)
              ? "   << succeeded but handed back no handle: nothing to share"
              : "");
}


/* Hand back a real shared handle, and see whether the game carries on.
 *
 * Wine's d3d9 answers CreateRenderTarget with S_OK and a null handle: it warns
 * and continues rather than failing, so the game believes it succeeded and
 * gives up quietly later, without ever calling ReadSample.
 *
 * DXMT's d3d11 does implement sharing -- GetSharedHandle and
 * CreateSharedHandle are both present in it, against none in Wine's -- so a
 * genuine handle can be made here. This creates a D3D11 texture of our own and
 * returns its handle.
 *
 * This alone cannot show the video: nothing yet copies from that texture to the
 * D3D9 surface the game actually draws. It is the step that produces evidence
 * before the expensive part is built. If the game starts reading samples, the
 * account is right and only the copy is missing. If it still does not, the
 * account is wrong, and better to learn that from one build than from three
 * days of bridge.
 */
#define SLOT_SURF_LOCKRECT     13
#define SLOT_SURF_UNLOCKRECT   14
static HRESULT (WINAPI *real_SurfUnlock)(void *);
static HRESULT WINAPI my_SurfUnlock(void *self);
/* The write-path hooks are off.
 *
 * With them in, the run degraded: sound stopped and it ended early. With them
 * in AND the copy disabled by a failing GetRenderTargetData, the same thing
 * happened -- zero frames copied, same symptom. So the copy was not the cause
 * and blaming it was wrong; the hooks themselves are.
 *
 * The likeliest of them is UnlockRect, which is patched on a vtable shared by
 * every D3D9 surface in the process, so every unlock the game makes anywhere
 * runs through it.
 *
 * This build keeps only what was measured to help: a real shared handle, which
 * makes the game read samples and reach playable state with a magenta picture.
 * That is worse than a picture and much better than nothing, and it is a state
 * to build forward from rather than guess away from. */
static BOOL watch_write_path = FALSE;

static void build_clamp_table(void);

static void *sidecar_device, *sidecar_context, *sidecar_texture;
static void *sidecar_staging, *shared_surface;
static UINT  sidecar_w, sidecar_h;
static HANDLE sidecar_handle;

static const GUID iid_dxgi_resource =
    { 0x035f3ab4, 0x482e, 0x4e50, { 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b } };

static BOOL make_sidecar(UINT width, UINT height)
{
    typedef HRESULT (WINAPI *create_dev_t)(void *, UINT, void *, UINT, const UINT *,
                                           UINT, UINT, void **, UINT *, void **);
    create_dev_t create_dev;
    HMODULE d3d11;
    /* D3D11_TEXTURE2D_DESC */
    struct { UINT w, h, mips, arr; DWORD fmt; struct { UINT c, q; } sample;
             UINT usage; UINT bind; UINT cpu; UINT misc; } desc;
    HRESULT hr;

    if (sidecar_handle) return TRUE;

    d3d11 = LoadLibraryA("d3d11.dll");
    if (!d3d11) { logf_("sidecar: no d3d11.dll"); return FALSE; }
    create_dev = (create_dev_t)GetProcAddress(d3d11, "D3D11CreateDevice");
    if (!create_dev) { logf_("sidecar: no D3D11CreateDevice"); return FALSE; }

    hr = create_dev(NULL, 1 /* HARDWARE */, NULL, 0, NULL, 0, 7 /* SDK */,
                    &sidecar_device, NULL, &sidecar_context);
    if (FAILED(hr) || !sidecar_device)
    {
        logf_("sidecar: D3D11CreateDevice -> 0x%08lx", hr);
        return FALSE;
    }

    ZeroMemory(&desc, sizeof(desc));
    desc.w = width; desc.h = height; desc.mips = 1; desc.arr = 1;
    desc.fmt = 87;              /* DXGI_FORMAT_B8G8R8A8_UNORM, matching D3DFMT_X8R8G8B8 */
    desc.sample.c = 1;
    desc.usage = 0;             /* DEFAULT */
    desc.bind = 8 | 32;         /* SHADER_RESOURCE | RENDER_TARGET */
    desc.misc = 2;              /* D3D11_RESOURCE_MISC_SHARED */
    {
        HRESULT (WINAPI *create_tex)(void *, const void *, const void *, void **) =
            (HRESULT (WINAPI *)(void *, const void *, const void *, void **))
            (*(void ***)sidecar_device)[5];   /* ID3D11Device::CreateTexture2D */
        hr = create_tex(sidecar_device, &desc, NULL, &sidecar_texture);
    }
    if (FAILED(hr) || !sidecar_texture)
    {
        logf_("sidecar: CreateTexture2D -> 0x%08lx", hr);
        return FALSE;
    }
    {
        void *res = NULL;
        HRESULT (WINAPI *qi)(void *, const GUID *, void **) =
            (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)sidecar_texture)[0];
        hr = qi(sidecar_texture, &iid_dxgi_resource, &res);
        if (SUCCEEDED(hr) && res)
        {
            /* IDXGIResource::GetSharedHandle is slot 3(IUnknown) + 4(IDXGIObject
             * and IDXGIDeviceSubObject) + 1 = 8. */
            HRESULT (WINAPI *get_shared)(void *, HANDLE *) =
                (HRESULT (WINAPI *)(void *, HANDLE *))(*(void ***)res)[8];
            hr = get_shared(res, &sidecar_handle);
            release_obj(res);
        }
    }
    logf_("sidecar: %ux%u texture, GetSharedHandle -> 0x%08lx, handle %p",
          width, height, hr, sidecar_handle);
    return sidecar_handle != NULL;
}

static HRESULT WINAPI my_CreateTexture(void *self, UINT w, UINT h, UINT levels,
                                       DWORD usage, DWORD fmt, DWORD pool,
                                       void **tex, HANDLE *shared)
{
    HRESULT hr = real_CreateTexture(self, w, h, levels, usage, fmt, pool, tex, shared);
    note_shared("CreateTexture", w, h, fmt, hr, shared);
    return hr;
}

static HRESULT WINAPI my_CreateRenderTarget(void *self, UINT w, UINT h, DWORD fmt,
                                            DWORD ms, DWORD msq, BOOL lockable,
                                            void **surface, HANDLE *shared)
{
    HRESULT hr = real_CreateRenderTarget(self, w, h, fmt, ms, msq, lockable, surface, shared);
    note_shared("CreateRenderTarget", w, h, fmt, hr, shared);
    /* Only where Wine left a hole: it succeeded and shared nothing. */
    if (SUCCEEDED(hr) && shared && !*shared && make_sidecar(w, h))
    {
        static LONG said;
        *shared = sidecar_handle;
        sidecar_w = w; sidecar_h = h;
        if (surface && *surface)
        {
            static void *su;
            void **vt;
            shared_surface = *surface;
            vt = *(void ***)shared_surface;
            real_SurfUnlock = (HRESULT (WINAPI *)(void *))vt[SLOT_SURF_UNLOCKRECT];
            if (!watch_write_path || !patch_slot("surface UnlockRect", shared_surface,
                    SLOT_SURF_UNLOCKRECT, (void *)my_SurfUnlock, &su))
                real_SurfUnlock = NULL;
        }
        if (InterlockedIncrement(&said) == 1)
            logf_("  handed the game a real shared handle %p instead of null -- "
                  "nothing copies into it yet; this only asks whether the game "
                  "then carries on", sidecar_handle);
    }
    return hr;
}

static HRESULT WINAPI my_CreateOffscreen(void *self, UINT w, UINT h, DWORD fmt,
                                         DWORD pool, void **surface, HANDLE *shared)
{
    HRESULT hr = real_CreateOffscreen(self, w, h, fmt, pool, surface, shared);
    note_shared("CreateOffscreenPlainSurface", w, h, fmt, hr, shared);
    return hr;
}


/* Carry the frame from the sidecar to the surface D3D9 draws.
 *
 * Everything else is now measured. The reader produces samples, the game opens
 * our shared handle on its own D3D11 device and writes the decoded frame into
 * it, and the D3D9 surface reaches the screen -- it shows magenta, which is an
 * uninitialised texture, and is therefore proof that the surface is being
 * presented and simply has nothing in it.
 *
 * So the copy is the last piece: sidecar texture -> D3D9 surface, once per
 * frame, triggered on Present because that is the one moment out here that is
 * guaranteed to happen after the game has written and before anyone looks.
 *
 * It goes through the CPU. A staging texture is the only way to read a D3D11
 * resource from out of process without owning the game's device, and D3D9's
 * LockRect is the only way into its surface. 1920x1080x4 is 8.3 MB a frame,
 * the same order as the DYNASTY WARRIORS bridge moved, and that ran fine.
 *
 * Slots, counted rather than guessed:
 *   ID3D11Device::CreateTexture2D 5
 *   ID3D11DeviceContext: Map 14, Unmap 15, CopyResource 47
 *   IDirect3DSurface9:  LockRect 13, UnlockRect 14
 *   IDirect3DDevice9::Present 17
 */
#define SLOT_CTX_MAP           14
#define SLOT_CTX_UNMAP         15
#define SLOT_CTX_COPYRESOURCE  47
#define SLOT_DEV9_PRESENT      17

static HRESULT (WINAPI *real_Present)(void *, const void *, const void *, HWND, const void *);

typedef struct { void *pData; UINT RowPitch; UINT DepthPitch; } MAPPED_SUBRESOURCE;
typedef struct { INT Pitch; void *pBits; } D3DLOCKED_RECT;

static BOOL make_staging(void)
{
    struct { UINT w, h, mips, arr; DWORD fmt; struct { UINT c, q; } sample;
             UINT usage; UINT bind; UINT cpu; UINT misc; } desc;
    HRESULT (WINAPI *create_tex)(void *, const void *, const void *, void **);
    HRESULT hr;

    if (sidecar_staging) return TRUE;
    if (!sidecar_device || !sidecar_w) return FALSE;

    ZeroMemory(&desc, sizeof(desc));
    desc.w = sidecar_w; desc.h = sidecar_h; desc.mips = 1; desc.arr = 1;
    desc.fmt = 87;              /* B8G8R8A8_UNORM */
    desc.sample.c = 1;
    desc.usage = 3;             /* D3D11_USAGE_STAGING */
    desc.cpu   = 0x20000;       /* D3D11_CPU_ACCESS_READ */
    create_tex = (HRESULT (WINAPI *)(void *, const void *, const void *, void **))
                 (*(void ***)sidecar_device)[5];
    hr = create_tex(sidecar_device, &desc, NULL, &sidecar_staging);
    if (FAILED(hr)) logf_("staging texture -> 0x%08lx", hr);
    return SUCCEEDED(hr);
}

static void carry_frame(void)
{
    HRESULT (WINAPI *copy)(void *, void *, void *);
    HRESULT (WINAPI *map)(void *, void *, UINT, UINT, UINT, MAPPED_SUBRESOURCE *);
    void (WINAPI *unmap)(void *, void *, UINT);
    HRESULT (WINAPI *lock)(void *, D3DLOCKED_RECT *, const void *, DWORD);
    HRESULT (WINAPI *unlock)(void *);
    MAPPED_SUBRESOURCE src;
    D3DLOCKED_RECT dst;
    static LONG frames, complained;

    if (!sidecar_texture || !shared_surface || !sidecar_context) return;
    if (!make_staging()) return;

    copy  = (HRESULT (WINAPI *)(void *, void *, void *))(*(void ***)sidecar_context)[SLOT_CTX_COPYRESOURCE];
    map   = (HRESULT (WINAPI *)(void *, void *, UINT, UINT, UINT, MAPPED_SUBRESOURCE *))
            (*(void ***)sidecar_context)[SLOT_CTX_MAP];
    unmap = (void (WINAPI *)(void *, void *, UINT))(*(void ***)sidecar_context)[SLOT_CTX_UNMAP];

    copy(sidecar_context, sidecar_staging, sidecar_texture);
    ZeroMemory(&src, sizeof(src));
    if (FAILED(map(sidecar_context, sidecar_staging, 0, 1 /* MAP_READ */, 0, &src)) || !src.pData)
    {
        if (InterlockedIncrement(&complained) == 1) logf_("carry: staging Map failed");
        return;
    }

    lock   = (HRESULT (WINAPI *)(void *, D3DLOCKED_RECT *, const void *, DWORD))
             (*(void ***)shared_surface)[SLOT_SURF_LOCKRECT];
    unlock = (HRESULT (WINAPI *)(void *))(*(void ***)shared_surface)[SLOT_SURF_UNLOCKRECT];

    ZeroMemory(&dst, sizeof(dst));
    if (SUCCEEDED(lock(shared_surface, &dst, NULL, 0)) && dst.pBits)
    {
        UINT row, bytes = sidecar_w * 4;
        const BYTE *s = (const BYTE *)src.pData;
        BYTE *d = (BYTE *)dst.pBits;
        for (row = 0; row < sidecar_h; row++)
            memcpy(d + (size_t)row * dst.Pitch, s + (size_t)row * src.RowPitch, bytes);
        unlock(shared_surface);
        {
            LONG f = InterlockedIncrement(&frames);
            if (f == 1 || f == 100)
                logf_("carry: frame %ld moved to the D3D9 surface "
                      "(%ux%u, src pitch %u, dst pitch %d)",
                      f, sidecar_w, sidecar_h, src.RowPitch, dst.Pitch);
        }
    }
    else if (InterlockedIncrement(&complained) == 1)
        logf_("carry: LockRect on the D3D9 surface failed");

    unmap(sidecar_context, sidecar_staging, 0);
}

static HRESULT WINAPI my_Present(void *self, const void *src, const void *dst,
                                 HWND wnd, const void *dirty)
{
    carry_frame();
    return real_Present(self, src, dst, wnd, dirty);
}


/* Which way does the frame travel?
 *
 * Present on the D3D9 device is never called, so that device is not drawing
 * anything -- it is a source, not a sink. The game decodes into the D3D9
 * surface and its D3D11 renderer opens the shared handle to display it, which
 * is the opposite of what the first carry assumed.
 *
 * That also explains the magenta exactly: the game is faithfully showing our
 * sidecar, and our sidecar is empty.
 *
 * So the copy has to run D3D9 surface -> sidecar, and it has to run when the
 * game has finished writing. This watches the calls that could be that moment,
 * rather than picking one and hoping.
 *
 * IDirect3DDevice9: UpdateSurface 30, GetRenderTargetData 32, StretchRect 34,
 *                   ColorFill 35, SetRenderTarget 37
 * IDirect3DSurface9: LockRect 13, UnlockRect 14
 */
#define SLOT_DEV9_UPDATESURFACE 30
#define SLOT_DEV9_STRETCHRECT   34
#define SLOT_DEV9_COLORFILL     35
#define SLOT_DEV9_SETRT         37
#define SLOT_DEV9_GETRTDATA     32
#define SLOT_DEV9_CREATE_OFFSCREEN 36

static HRESULT (WINAPI *real_UpdateSurface)(void *, void *, const void *, void *, const void *);
static HRESULT (WINAPI *real_StretchRect)(void *, void *, const void *, void *, const void *, DWORD);
static HRESULT (WINAPI *real_ColorFill)(void *, void *, const void *, DWORD);
static HRESULT (WINAPI *real_SetRenderTarget)(void *, DWORD, void *);
static HRESULT (WINAPI *real_SurfUnlock)(void *);


/* Fill the sidecar from the surface the game writes.
 *
 * Measured, not assumed: the game ColorFills our shared surface, StretchRects
 * the frame into it, and also locks and writes it directly. Those last two are
 * the moments it has just finished writing, so that is when the copy runs.
 *
 * The direction is D3D9 surface -> sidecar, which is the opposite of the first
 * attempt. Present on the D3D9 device is never called, and a device that
 * presents nothing is not drawing: it is the source. The magenta filling the
 * screen was the game faithfully displaying our empty sidecar.
 *
 * UpdateSubresource writes straight into the sidecar from the locked bits, so
 * no staging texture is needed on this side. D3DFMT_X8R8G8B8 and
 * DXGI_FORMAT_B8G8R8A8_UNORM have the same byte layout, so the rows go across
 * unchanged.
 *
 * ID3D11DeviceContext::UpdateSubresource is slot 48, right after CopyResource.
 */
#define SLOT_CTX_UPDATESUBRESOURCE 48

static void *sysmem_surface, *d3d9_device;
static HRESULT (WINAPI *real_GetRenderTargetData)(void *, void *, void *);

/* Read the render target the way D3D9 intends.
 *
 * Locking a render target directly, once per frame, is what broke a state that
 * had been working: sound played and the game was reachable, magenta and all,
 * and after the copy went in there was noise and then nothing. LockRect on a
 * render target forces a GPU sync in the middle of the game's own work.
 *
 * GetRenderTargetData copies it into a system-memory surface, which is then
 * safe to lock because it is not something the GPU is drawing into. It is one
 * more copy and the correct one.
 */
static BOOL make_sysmem(void)
{
    HRESULT (WINAPI *create_off)(void *, UINT, UINT, DWORD, DWORD, void **, HANDLE *);
    HRESULT hr;
    if (sysmem_surface) return TRUE;
    if (!d3d9_device || !sidecar_w) return FALSE;
    create_off = (HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, DWORD, void **, HANDLE *))
                 (*(void ***)d3d9_device)[SLOT_DEV9_CREATE_OFFSCREEN];
    hr = create_off(d3d9_device, sidecar_w, sidecar_h, 22 /* X8R8G8B8 */,
                    2 /* D3DPOOL_SYSTEMMEM */, &sysmem_surface, NULL);
    if (FAILED(hr)) logf_("fill: system-memory surface -> 0x%08lx", hr);
    return SUCCEEDED(hr);
}


/* Write where the game reads, on the device it reads with.
 *
 * The sidecar lives on a D3D11 device of ours; the game opens the shared handle
 * on its own. Writes on one are not visible on the other, which is why the
 * screen stayed magenta while a hundred converted frames went into a texture
 * nobody was looking at.
 *
 * So the texture is created on the GAME'S device instead, handed back from its
 * own OpenSharedResource, and written through its own immediate context. That
 * removes the cross-device question entirely rather than trying to answer it.
 *
 * DXMT exposes ID3D11Multithread, and turning it on makes those writes safe
 * from the video thread with ordinary D3D11 ordering guarantees -- program
 * order against the engine's later reads. It is off by default in DXMT, so we
 * turn it on.
 *
 * ID3D11Device:  CreateTexture2D 5, OpenSharedResource 28, GetImmediateContext 40
 * ID3D11Multithread: Enter 3, Leave 4, SetMultithreadProtected 5
 */
#define SLOT_DEV11_CREATE_TEX2D   5
#define SLOT_DEV11_OPENSHARED    28
#define SLOT_DEV11_IMMCONTEXT    40

static const GUID iid_multithread =
    { 0x9b7e4e00, 0x342c, 0x4106, { 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0 } };

static void *game_device, *game_context, *game_texture, *game_mt;
static HRESULT (WINAPI *real_OpenSharedResource)(void *, HANDLE, const GUID *, void **);
static HRESULT (WINAPI *real_D3D11CreateDevice)(void *, UINT, void *, UINT, const UINT *,
                                                UINT, UINT, void **, UINT *, void **);

static void mt_enter(void) { if (game_mt) ((void (WINAPI *)(void *))(*(void ***)game_mt)[3])(game_mt); }
static void mt_leave(void) { if (game_mt) ((void (WINAPI *)(void *))(*(void ***)game_mt)[4])(game_mt); }

/* OpenSharedResource returns whatever interface was asked for -- usually
 * ID3D11Texture2D, but a caller may ask for IDXGIResource or IDXGISurface.
 * Returning the texture regardless would hand back an object that does not
 * implement what the caller is about to call, which fails somewhere later and
 * looks like nothing to do with us. */
static HRESULT hand_back(const GUID *iid, void **out)
{
    HRESULT (WINAPI *qi)(void *, const GUID *, void **) =
        (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)game_texture)[0];
    HRESULT hr = qi(game_texture, iid, out);
    if (FAILED(hr))
    {
        static LONG told;
        if (InterlockedIncrement(&told) == 1)
            logf_("OpenSharedResource: the caller wanted "
                  "{%08lX-%04X-%04X-...} which our texture does not implement",
                  iid->Data1, iid->Data2, iid->Data3);
    }
    return hr;
}

static HRESULT WINAPI my_OpenSharedResource(void *self, HANDLE handle,
                                            const GUID *iid, void **out)
{
    if (handle && handle == sidecar_handle && out)
    {
        struct { UINT w, h, mips, arr; DWORD fmt; struct { UINT c, q; } sample;
                 UINT usage; UINT bind; UINT cpu; UINT misc; } desc;
        HRESULT (WINAPI *create_tex)(void *, const void *, const void *, void **);
        HRESULT hr;

        if (game_texture) return hand_back(iid, out);

        /* The saturation table the converter indexes has to be built before
         * anything is converted. It was copied across with the converter and
         * its call was not, so every pixel resolved to clamp8[...] = 0 and the
         * picture came out black from perfectly good input. */
        build_clamp_table();

        ZeroMemory(&desc, sizeof(desc));
        desc.w = sidecar_w; desc.h = sidecar_h; desc.mips = 1; desc.arr = 1;
        desc.fmt = 87;            /* B8G8R8A8_UNORM */
        desc.sample.c = 1;
        desc.usage = 1;           /* D3D11_USAGE_DEFAULT is 0; DYNAMIC is 2.
                                   * IMMUTABLE(1) would refuse writes, so DEFAULT: */
        desc.usage = 0;           /* DEFAULT */
        /* A texture opened from a shared handle would normally be bindable as
         * both a shader resource and a render target, and carry MISC_SHARED.
         * Handing back one that is only samplable is a guess about how the game
         * uses it, and a wrong guess here fails later rather than here. */
        desc.bind = 8 | 32;       /* SHADER_RESOURCE | RENDER_TARGET */
        desc.misc = 2;            /* MISC_SHARED */
        create_tex = (HRESULT (WINAPI *)(void *, const void *, const void *, void **))
                     (*(void ***)self)[SLOT_DEV11_CREATE_TEX2D];
        hr = create_tex(self, &desc, NULL, &game_texture);
        logf_("OpenSharedResource: our handle -- made a %ux%u texture on the "
              "game's own device instead, hr 0x%08lx", sidecar_w, sidecar_h, hr);
        if (FAILED(hr)) return real_OpenSharedResource(self, handle, iid, out);

        game_device = self;
        ((void (WINAPI *)(void *, void **))(*(void ***)self)[SLOT_DEV11_IMMCONTEXT])
            (self, &game_context);
        {
            HRESULT (WINAPI *qi)(void *, const GUID *, void **) =
                (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)self)[0];
            if (SUCCEEDED(qi(self, &iid_multithread, &game_mt)) && game_mt)
            {
                ((BOOL (WINAPI *)(void *, BOOL))(*(void ***)game_mt)[5])(game_mt, TRUE);
                logf_("  multithread protection enabled on the game's device");
            }
        }
        return hand_back(iid, out);
    }
    return real_OpenSharedResource(self, handle, iid, out);
}

static HRESULT WINAPI my_D3D11CreateDevice(void *adapter, UINT type, void *sw, UINT flags,
                                           const UINT *levels, UINT nlevels, UINT sdk,
                                           void **device, UINT *got, void **context)
{
    HRESULT hr = real_D3D11CreateDevice(adapter, type, sw, flags, levels, nlevels,
                                        sdk, device, got, context);
    if (SUCCEEDED(hr) && device && *device)
    {
        static void *os;
        void **vt = *(void ***)*device;
        real_OpenSharedResource = (HRESULT (WINAPI *)(void *, HANDLE, const GUID *, void **))
                                  vt[SLOT_DEV11_OPENSHARED];
        if (!patch_slot("d3d11 OpenSharedResource", *device, SLOT_DEV11_OPENSHARED,
                        (void *)my_OpenSharedResource, &os))
            real_OpenSharedResource = NULL;
    }
    return hr;
}

/* Magenta filled the screen, so the writes land and the texture is the one the
 * game shows. Whatever is wrong is in the content, which is the cheaper half.
 *
 * Back to real frames, and sampling the luma over time rather than three times
 * at the start: the first samples read 16,16 then 15..50, which is a fade in
 * from black. It is entirely possible the video is being carried correctly and
 * only its dark opening was ever measured. */
static BOOL paint_magenta = FALSE;
static void *owning_device;

static void fill_sidecar_from_surface(const char *why)
{
    HRESULT (WINAPI *lock)(void *, D3DLOCKED_RECT *, const void *, DWORD);
    HRESULT (WINAPI *unlock)(void *);
    void (WINAPI *update)(void *, void *, UINT, const void *, const void *, UINT, UINT);
    D3DLOCKED_RECT r;
    static LONG frames, failed;
    HRESULT hr;

    if (!shared_surface || !sidecar_texture || !sidecar_context) return;
    if (!make_sysmem()) return;

    hr = real_GetRenderTargetData
       ? real_GetRenderTargetData(owning_device, shared_surface, sysmem_surface)
       : E_FAIL;
    if (FAILED(hr))
    {
        if (InterlockedIncrement(&failed) == 1)
            logf_("fill: GetRenderTargetData -> 0x%08lx", hr);
        return;
    }

    lock   = (HRESULT (WINAPI *)(void *, D3DLOCKED_RECT *, const void *, DWORD))
             (*(void ***)sysmem_surface)[SLOT_SURF_LOCKRECT];
    unlock = (HRESULT (WINAPI *)(void *))(*(void ***)sysmem_surface)[SLOT_SURF_UNLOCKRECT];
    update = (void (WINAPI *)(void *, void *, UINT, const void *, const void *, UINT, UINT))
             (*(void ***)sidecar_context)[SLOT_CTX_UPDATESUBRESOURCE];

    ZeroMemory(&r, sizeof(r));
    if (SUCCEEDED(lock(sysmem_surface, &r, NULL, 0x10 /* READONLY */)) && r.pBits)
    {
        update(sidecar_context, sidecar_texture, 0, NULL, r.pBits, (UINT)r.Pitch, 0);
        unlock(sysmem_surface);
        {
            LONG f = InterlockedIncrement(&frames);
            if (f == 1 || f == 100)
                logf_("fill: frame %ld carried after %s (%ux%u, pitch %d)",
                      f, why, sidecar_w, sidecar_h, r.Pitch);
        }
    }
    else if (InterlockedIncrement(&failed) == 1)
        logf_("fill: could not lock the system-memory surface");
}

static void seen(const char *what, void *surface)
{
    static LONG said[6];
    int i = (int)((what[0] + what[1]) % 6);
    if (InterlockedIncrement(&said[i]) > 2) return;
    logf_("%s%s", what, surface == shared_surface ? "   << ON OUR SHARED SURFACE" : "");
}

static HRESULT WINAPI my_UpdateSurface(void *self, void *src, const void *srect,
                                       void *dst, const void *dpoint)
{
    HRESULT hr = real_UpdateSurface(self, src, srect, dst, dpoint);
    if (dst == shared_surface || src == shared_surface)
        seen("UpdateSurface", dst);
    return hr;
}

/* D3DSURFACE_DESC, in order: Format, Type, Usage, Pool, MultiSampleType,
 * MultiSampleQuality, Width, Height. IDirect3DSurface9::GetDesc is slot 12. */
typedef struct { DWORD fmt, type, usage, pool, ms, msq, w, h; } SURF_DESC;

static const char *pool_name(DWORD pool)
{
    switch (pool) {
    case 0: return "DEFAULT";
    case 1: return "MANAGED";
    case 2: return "SYSTEMMEM";
    case 3: return "SCRATCH";
    default: return "?";
    }
}

/* Take the frame from the surface the game is blitting FROM.
 *
 * GetRenderTargetData on the destination answers D3DERR_INVALIDCALL, so it
 * cannot be read back. The source is the surface holding the decoded frame,
 * and if it lives anywhere lockable it can be read directly -- which is one
 * copy instead of two and avoids touching a render target at all.
 *
 * This logs what the source actually is before trying, because guessing at a
 * surface's pool and then locking it is how a working state was lost earlier. */

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* The source surface is NV12, measured: its D3DFORMAT is 0x3231564E, and the
 * lock hands back a pitch of 1920 on a 1920-wide frame -- one byte per pixel,
 * the luma plane. Copying those bytes into a BGRA texture is what produced
 * noise: correct data read as the wrong thing.
 *
 * The converter below is lifted unchanged from the DYNASTY WARRIORS bridge,
 * where it has been carrying frames for weeks. Two pixels at a time, because
 * NV12 chroma is subsampled 2:1 across and both read the same pair, and
 * saturation by table rather than two branches per channel.
 */
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

/* NOTE ON DEVICES, which cost a wrong conclusion.
 *
 * There are two D3D9 devices in this process: the engine's, created through
 * CreateDevice, and the movie player's, created through CreateDeviceEx. Wine
 * shares one vtable between them, so a hook installed from one fires for the
 * other -- which is why patching from the engine's device caught the movie
 * player's calls, and why global caching of "the" device is wrong.
 *
 * GetRenderTargetData answered D3DERR_INVALIDCALL because it was asked of the
 * engine's device about the movie player's surface. The owning device is the
 * `self` of the call being intercepted, and it is used from now on. */
static void take_from_source(void *self, void *src)
{
    HRESULT (WINAPI *get_desc)(void *, SURF_DESC *);
    HRESULT (WINAPI *lock)(void *, D3DLOCKED_RECT *, const void *, DWORD);
    HRESULT (WINAPI *unlock)(void *);
    void (WINAPI *update)(void *, void *, UINT, const void *, const void *, UINT, UINT);
    SURF_DESC d;
    D3DLOCKED_RECT r;
    static LONG told, frames, failed;
    HRESULT hr;

    if (!src || !sidecar_texture || !sidecar_context) return;

    get_desc = (HRESULT (WINAPI *)(void *, SURF_DESC *))(*(void ***)src)[12];
    ZeroMemory(&d, sizeof(d));
    if (FAILED(get_desc(src, &d))) return;

    if (InterlockedIncrement(&told) == 1)
        logf_("source surface: %ux%u fmt=%lu pool=%s usage=0x%lx",
              d.w, d.h, d.fmt, pool_name(d.pool), d.usage);

    lock   = (HRESULT (WINAPI *)(void *, D3DLOCKED_RECT *, const void *, DWORD))(*(void ***)src)[SLOT_SURF_LOCKRECT];
    unlock = (HRESULT (WINAPI *)(void *))(*(void ***)src)[SLOT_SURF_UNLOCKRECT];
    update = (void (WINAPI *)(void *, void *, UINT, const void *, const void *, UINT, UINT))
             (*(void ***)sidecar_context)[SLOT_CTX_UPDATESUBRESOURCE];

    ZeroMemory(&r, sizeof(r));
    hr = lock(src, &r, NULL, 0x10 /* READONLY */);
    if (FAILED(hr) || !r.pBits)
    {
        if (InterlockedIncrement(&failed) == 1)
            logf_("source surface: LockRect -> 0x%08lx -- trying the destination "
                  "through its own device instead", hr);
        {
            HRESULT (WINAPI *grt)(void *, void *, void *) =
                (HRESULT (WINAPI *)(void *, void *, void *))(*(void ***)self)[SLOT_DEV9_GETRTDATA];
            real_GetRenderTargetData = grt;
            owning_device = self;
            fill_sidecar_from_surface("StretchRect, via GetRenderTargetData");
        }
        return;
    }
    /* Only if it is the size and shape the sidecar expects; a mismatched blit
     * would write a frame-sized amount into the wrong place. */
    if (d.w == sidecar_w && d.h == sidecar_h)
    {
        static BYTE *scratch;
        static UINT scratch_size;
        UINT need = sidecar_w * 4 * sidecar_h;
        if (scratch_size < need)
        {
            BYTE *bigger = (BYTE *)HeapAlloc(GetProcessHeap(), 0, need);
            if (bigger) { if (scratch) HeapFree(GetProcessHeap(), 0, scratch);
                          scratch = bigger; scratch_size = need; }
        }
        if (scratch)
        {
            /* Is there a picture in there at all?
             *
             * Black and magenta look equally like failure from the sofa, and
             * they mean opposite things: magenta was an untouched texture,
             * black could be our writes not landing OR a source that is itself
             * empty. Averaging the luma plane separates them in one number,
             * which is what settled the same ambiguity on DYNASTY WARRIORS. */
            {
                static LONG sampled;
                LONG n = InterlockedIncrement(&sampled);
                if (n <= 3 || (n % 60) == 0)
                {
                    const BYTE *y = (const BYTE *)r.pBits;
                    unsigned long long sum = 0;
                    UINT px, py, lo = 255, hi = 0;
                    for (py = 0; py < sidecar_h; py += 8)
                        for (px = 0; px < sidecar_w; px += 8)
                        {
                            BYTE v = y[(size_t)r.Pitch * py + px];
                            sum += v;
                            if (v < lo) lo = v;
                            if (v > hi) hi = v;
                        }
                    logf_("source luma [%ld]: average %llu, range %u..%u  << %s", n,
                          sum / (((sidecar_h + 7) / 8) * ((sidecar_w + 7) / 8)),
                          lo, hi,
                          hi <= 16 ? "black" : "has picture");
                }
            }
            /* Write something that cannot be mistaken for anything else.
             *
             * The source has a picture -- luma range 15..50 on the third
             * sample, dark but real, after two genuinely black frames of a fade
             * in. So the frame exists and is lost after this point, and the two
             * remaining explanations are that our writes do not land or that
             * the game samples something else. On a black screen those look
             * identical.
             *
             * Solid magenta tells them apart in one run. Set P5S_REAL_FRAMES=1
             * to carry the video instead. This is the measurement that settled
             * the same question on DYNASTY WARRIORS. */
            if (paint_magenta)
            {
                UINT i, n = sidecar_w * sidecar_h;
                for (i = 0; i < n; i++)
                {
                    scratch[i * 4 + 0] = 0xFF;
                    scratch[i * 4 + 1] = 0x00;
                    scratch[i * 4 + 2] = 0xFF;
                    scratch[i * 4 + 3] = 0xFF;
                }
            }
            else if (d.fmt == 0x3231564E)      /* NV12 */
                nv12_to_bgra((const BYTE *)r.pBits, (UINT)r.Pitch, scratch,
                             sidecar_w * 4, sidecar_w, sidecar_h);
            else
                CopyMemory(scratch, r.pBits, need);
            if (game_context && game_texture)
            {
                void (WINAPI *gupdate)(void *, void *, UINT, const void *,
                                       const void *, UINT, UINT) =
                    (void (WINAPI *)(void *, void *, UINT, const void *,
                                     const void *, UINT, UINT))
                    (*(void ***)game_context)[SLOT_CTX_UPDATESUBRESOURCE];
                mt_enter();
                gupdate(game_context, game_texture, 0, NULL, scratch, sidecar_w * 4, 0);
                mt_leave();
            }
            else
                update(sidecar_context, sidecar_texture, 0, NULL, scratch, sidecar_w * 4, 0);
        }
    }
    else if (InterlockedIncrement(&failed) == 1)
        logf_("source surface is %ux%u but the sidecar is %ux%u -- not copying",
              d.w, d.h, sidecar_w, sidecar_h);
    unlock(src);

    {
        LONG f = InterlockedIncrement(&frames);
        if (f == 1 || f == 100)
            logf_("fill: frame %ld taken from the StretchRect source (pitch %d)", f, r.Pitch);
    }
}

static HRESULT WINAPI my_StretchRect(void *self, void *src, const void *srect,
                                     void *dst, const void *drect, DWORD filter)
{
    HRESULT hr = real_StretchRect(self, src, srect, dst, drect, filter);
    if (dst == shared_surface || src == shared_surface)
    {
        seen(dst == shared_surface ? "StretchRect INTO it" : "StretchRect OUT of it", dst);
        if (dst == shared_surface && SUCCEEDED(hr)) { owning_device = self; take_from_source(self, src); }
        /* Deliberately NOT copying here.
         *
         * StretchRect is GPU work that has not finished when it returns, so
         * locking the surface immediately after forces a sync in the middle of
         * a blit -- which is the likeliest cause of the noise that appeared and
         * the run ending. UnlockRect is the game saying it has finished
         * writing, which is a fact rather than a guess about timing. */
    }
    return hr;
}

static HRESULT WINAPI my_ColorFill(void *self, void *surface, const void *rect, DWORD colour)
{
    HRESULT hr = real_ColorFill(self, surface, rect, colour);
    if (surface == shared_surface) seen("ColorFill", surface);
    return hr;
}

static HRESULT WINAPI my_SetRenderTarget(void *self, DWORD index, void *surface)
{
    HRESULT hr = real_SetRenderTarget(self, index, surface);
    if (surface == shared_surface) seen("SetRenderTarget", surface);
    return hr;
}

static HRESULT WINAPI my_SurfUnlock(void *self)
{
    HRESULT hr = real_SurfUnlock(self);
    if (self == shared_surface)
    {
        static LONG inside;
        seen("UnlockRect -- the game wrote to it directly", self);
        /* fill_sidecar_from_surface locks and unlocks the same surface, so it
         * would re-enter here. One flag, not a lock: this is the same thread. */
        if (InterlockedCompareExchange(&inside, 1, 0) == 0)
        {
            fill_sidecar_from_surface("UnlockRect");
            InterlockedExchange(&inside, 0);
        }
    }
    return hr;
}

static HRESULT WINAPI my_CreateDevice(void *self, UINT adapter, DWORD type, HWND focus,
                                      DWORD flags, void *params, void **device)
{
    HRESULT hr = real_CreateDevice(self, adapter, type, focus, flags, params, device);
    logf_("IDirect3D9::CreateDevice -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && device && *device)
    {
        static void *ct, *rt, *op;
        patch_slot("d3d9 CreateTexture",      *device, SLOT_DEV_CREATE_TEXTURE,
                   (void *)my_CreateTexture,   &ct);
        patch_slot("d3d9 CreateRenderTarget", *device, SLOT_DEV_CREATE_RT,
                   (void *)my_CreateRenderTarget, &rt);
        patch_slot("d3d9 CreateOffscreen",    *device, SLOT_DEV_CREATE_OFFSCREEN,
                   (void *)my_CreateOffscreen, &op);
        real_CreateTexture = (HRESULT (WINAPI *)(void *, UINT, UINT, UINT, DWORD, DWORD, DWORD, void **, HANDLE *))ct;
        real_CreateRenderTarget = (HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void **, HANDLE *))rt;
        real_CreateOffscreen = (HRESULT (WINAPI *)(void *, UINT, UINT, DWORD, DWORD, void **, HANDLE *))op;
        {
            static void *us, *sr, *cf, *rt2;
            if (watch_write_path) patch_slot("d3d9 UpdateSurface",   *device, SLOT_DEV9_UPDATESURFACE,
                       (void *)my_UpdateSurface, &us);
            patch_slot("d3d9 StretchRect",     *device, SLOT_DEV9_STRETCHRECT,
                       (void *)my_StretchRect,   &sr);
            if (watch_write_path) patch_slot("d3d9 ColorFill",       *device, SLOT_DEV9_COLORFILL,
                       (void *)my_ColorFill,     &cf);
            if (watch_write_path) patch_slot("d3d9 SetRenderTarget", *device, SLOT_DEV9_SETRT,
                       (void *)my_SetRenderTarget, &rt2);
            real_UpdateSurface = (HRESULT (WINAPI *)(void *, void *, const void *, void *, const void *))us;
            real_StretchRect = (HRESULT (WINAPI *)(void *, void *, const void *, void *, const void *, DWORD))sr;
            real_ColorFill = (HRESULT (WINAPI *)(void *, void *, const void *, DWORD))cf;
            real_SetRenderTarget = (HRESULT (WINAPI *)(void *, DWORD, void *))rt2;
            d3d9_device = *device;
            real_GetRenderTargetData = (HRESULT (WINAPI *)(void *, void *, void *))
                (*(void ***)*device)[SLOT_DEV9_GETRTDATA];
        }
        {
            static void *pr;
            /* Publish the original before arming, which is the ordering that
             * cost a crash earlier today. */
            void **vt = *(void ***)*device;
            real_Present = (HRESULT (WINAPI *)(void *, const void *, const void *,
                                               HWND, const void *))vt[SLOT_DEV9_PRESENT];
            if (!watch_write_path || !patch_slot("d3d9 Present", *device,
                    SLOT_DEV9_PRESENT, (void *)my_Present, &pr))
                real_Present = NULL;
        }
    }
    return hr;
}

static void watch_d3d9_object(void *d3d9)
{
    static void *cd;
    if (!d3d9) return;
    if (patch_slot("d3d9 CreateDevice", d3d9, SLOT_D3D9_CREATE_DEVICE,
                   (void *)my_CreateDevice, &cd))
        real_CreateDevice = (HRESULT (WINAPI *)(void *, UINT, DWORD, HWND, DWORD, void *, void **))cd;
}

static void *WINAPI my_Direct3DCreate9(UINT sdk)
{
    void *d3d9 = real_Direct3DCreate9(sdk);
    logf_("Direct3DCreate9 -> %p", d3d9);
    watch_d3d9_object(d3d9);
    return d3d9;
}

static HRESULT WINAPI my_Direct3DCreate9Ex(UINT sdk, void **out)
{
    HRESULT hr = real_Direct3DCreate9Ex(sdk, out);
    logf_("Direct3DCreate9Ex -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && out) watch_d3d9_object(*out);
    return hr;
}


/* Watch D3D12, which is the other lead and the untested one.
 *
 * Everything Media Foundation does here succeeds -- startup, the decoder gate
 * once answered, the DXGI manager, and binding a device to it -- and the game
 * still never asks to open a video. So the black screen is probably not about
 * video at all, and the one thing seen on the CrossOver console was
 *
 *     D3DMetal ID3DDestructionNot...
 *
 * D3DMetal reporting a query for ID3DDestructionNotifier, which it does not
 * implement. The game ships its own D3D12Core.dll -- the Agility SDK -- and
 * that binary does carry the interface's GUID and name. It is also the
 * interface behind Mortal Shell 2's crash.
 *
 * This only watches: who asks, and what they are told.
 *
 * ID3D12Device: CreateCommittedResource 27, CreateHeap 28.
 */
#define SLOT_D3D12_CREATECOMMITTED 27

static const GUID iid_destruction_notifier =
    { 0xa06eb39a, 0x50da, 0x425b, { 0x8c, 0x31, 0x4e, 0xec, 0xd6, 0xc2, 0x70, 0xf3 } };

static HRESULT (WINAPI *real_dev12_QI)(void *, const GUID *, void **);
static HRESULT (WINAPI *real_CreateCommitted)(void *, const void *, UINT, const void *,
                                              UINT, const void *, const GUID *, void **);
static HRESULT (WINAPI *real_res12_QI)(void *, const GUID *, void **);

static void note_query(const char *who, const GUID *iid, HRESULT hr)
{
    static LONG said;
    if (!iid) return;
    if (IsEqualGUID(iid, &iid_destruction_notifier))
    {
        if (InterlockedIncrement(&said) <= 4)
            logf_("%s asked for ID3DDestructionNotifier -> 0x%08lx%s", who, hr,
                  FAILED(hr) ? "   << D3DMetal does not implement it" : "");
    }
}

static HRESULT WINAPI my_res12_QI(void *self, const GUID *iid, void **out)
{
    HRESULT hr = real_res12_QI ? real_res12_QI(self, iid, out) : E_NOINTERFACE;
    note_query("a D3D12 resource", iid, hr);
    return hr;
}

/* ID3D12Device::CheckFeatureSupport is slot 13. DXGI_ERROR_UNSUPPORTED out of
   CreateQueue means DirectStorage asked this device what it could do and did not
   like an answer. Logging every question and answer inside that window makes the
   missing capability name itself. */
#define SLOT_D3D12_CHECKFEATURE 13

static HRESULT (WINAPI *real_CheckFeature)(void *, UINT, void *, UINT);

static HRESULT WINAPI my_CheckFeature(void *self, UINT feature, void *data, UINT size)
{
    static LONG outside;
    HRESULT hr = real_CheckFeature(self, feature, data, size);

    /* Answer yes to D3D12_OPTIONS17, but only while DirectStorage is asking.
     *
     * Everything else it wants is granted -- Shader Model 6.5, WaveOps, Int64,
     * Native16Bit, ExpandedComputeResourceStates -- and it builds no pipeline and
     * makes no further call before refusing, so the refusal is decided from these
     * numbers alone. OPTIONS17 is the one structure that comes back all zeros,
     * the first thing asked, and the newest of the set, which fits the "requires
     * Agility SDK >= 707" string this component carries.
     *
     * Saying yes does not make the capability exist. If it turns the refusal into
     * a queue, it names the requirement, which is what this run is for; whether
     * that queue then works is the next question, not this one. */
    if (in_create_queue_ && feature == 46 && SUCCEEDED(hr) &&
        data && size >= 8 && readable_(data, 8))
    {
        ((UINT32 *)data)[0] = 1;
        ((UINT32 *)data)[1] = 1;
        logf_("  [in CreateQueue] answering yes to OPTIONS17 (was all zeros)");
    }

    if (in_create_queue_ || InterlockedIncrement(&outside) <= 8)
    {
        char vals[16 * 9 + 1];
        UINT n = size / 4, i;
        vals[0] = 0;
        if (n > 16) n = 16;
        if (data && readable_(data, n * 4))
            for (i = 0; i < n; i++)
                sprintf(vals + i * 9, "%08x ", ((const UINT32 *)data)[i]);
        logf_("  %sCheckFeatureSupport(feature=%u, %u bytes) -> 0x%08lx  [ %s]",
              in_create_queue_ ? "[in CreateQueue] " : "",
              feature, size, hr, vals);
    }
    return hr;
}

static HRESULT WINAPI my_CreateCommitted(void *self, const void *heap, UINT hflags,
                                         const void *desc, UINT state,
                                         const void *clear, const GUID *iid, void **out)
{
    HRESULT hr = real_CreateCommitted(self, heap, hflags, desc, state, clear, iid, out);

    /* Every allocation DirectStorage makes while standing up its device path. Its
     * staging buffers are created here, and a refusal at this size is the most
     * ordinary explanation for a queue that will not be built. */
    if (in_create_queue_)
    {
        UINT64 width = 0;
        UINT32 dim = 0;
        if (desc && readable_(desc, 16))
        {
            memcpy(&dim, desc, 4);
            /* D3D12_RESOURCE_DESC: Dimension at 0, Alignment at 8, Width at 16. */
            if (readable_(desc, 24)) memcpy(&width, (const BYTE *)desc + 16, 8);
        }
        logf_("  [in CreateQueue] CreateCommittedResource(dim=%u, %llu bytes = %llu MB) -> 0x%08lx%s",
              dim, (unsigned long long)width, (unsigned long long)(width / (1024 * 1024)), hr,
              FAILED(hr) ? "   << refused" : "");
    }

    if (SUCCEEDED(hr) && out && *out && !real_res12_QI)
    {
        static void *saved;
        void **vt = *(void ***)*out;
        real_res12_QI = (HRESULT (WINAPI *)(void *, const GUID *, void **))vt[0];
        if (!patch_slot("d3d12 resource QueryInterface", *out, 0,
                        (void *)my_res12_QI, &saved))
            real_res12_QI = NULL;
    }
    return hr;
}

/* ID3D12Device::CreateCommandQueue is slot 8. DirectStorage stands up its own
   queue to serve the device it was handed; a refusal there would surface as the
   UNSUPPORTED we get and leave no other trace. */
#define SLOT_D3D12_CREATECMDQUEUE 8

static HRESULT (WINAPI *real_CreateCmdQueue)(void *, const void *, const GUID *, void **);

static HRESULT WINAPI my_CreateCmdQueue(void *self, const void *desc,
                                        const GUID *iid, void **out)
{
    HRESULT hr = real_CreateCmdQueue(self, desc, iid, out);
    const UINT32 *d = (const UINT32 *)desc;

    if (in_create_queue_)
        logf_("  [in CreateQueue] CreateCommandQueue(type=%u priority=%d flags=0x%x) -> 0x%08lx",
              (d && readable_(d, 16)) ? d[0] : 0xffffffff,
              (d && readable_(d, 16)) ? (int)d[1] : 0,
              (d && readable_(d, 16)) ? d[2] : 0, hr);
    return hr;
}

/* The GDeflate decompression pipeline. dstoragecore asks about Shader Model,
   WaveOps and Native16Bit and then, satisfied, builds a compute pipeline -- and
   it carries the string "pCreateComputeRootSignature failed" as a hard failure,
   with no fallback beside it. These are the last two things it can do before
   giving up, and the only ones still unwatched. */
#define SLOT_D3D12_CREATECOMPUTEPSO 11
#define SLOT_D3D12_CREATEROOTSIG    16

static HRESULT (WINAPI *real_CreateComputePSO)(void *, const void *, const GUID *, void **);
static HRESULT (WINAPI *real_CreateRootSig)(void *, UINT, const void *, SIZE_T, const GUID *, void **);

static HRESULT WINAPI my_CreateComputePSO(void *self, const void *desc,
                                          const GUID *iid, void **out)
{
    HRESULT hr = real_CreateComputePSO(self, desc, iid, out);
    if (in_create_queue_)
        logf_("  [in CreateQueue] CreateComputePipelineState -> 0x%08lx%s", hr,
              FAILED(hr) ? "   << refused" : "");
    return hr;
}

static HRESULT WINAPI my_CreateRootSig(void *self, UINT node, const void *blob,
                                       SIZE_T len, const GUID *iid, void **out)
{
    HRESULT hr = real_CreateRootSig(self, node, blob, len, iid, out);
    if (in_create_queue_)
        logf_("  [in CreateQueue] CreateRootSignature(%llu bytes) -> 0x%08lx%s",
              (unsigned long long)len, hr, FAILED(hr) ? "   << refused" : "");
    return hr;
}

static HRESULT WINAPI my_dev12_QI(void *self, const GUID *iid, void **out)
{
    HRESULT hr = real_dev12_QI ? real_dev12_QI(self, iid, out) : E_NOINTERFACE;

    if (in_create_queue_ && iid)
        logf_("  [in CreateQueue] QueryInterface {%08lx-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x} -> 0x%08lx%s",
              iid->Data1, iid->Data2, iid->Data3,
              iid->Data4[0], iid->Data4[1], iid->Data4[2], iid->Data4[3],
              iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7],
              hr, FAILED(hr) ? "   << refused" : "");
    else
        note_query("the D3D12 device", iid, hr);
    return hr;
}

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT level,
                                           const GUID *iid, void **device)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, level, iid, device);
    logf_("D3D12CreateDevice(featureLevel=0x%lx) -> 0x%08lx", level, hr);
    if (SUCCEEDED(hr) && device && *device)
    {
        static void *qi, *cc;
        void **vt = *(void ***)*device;
        real_dev12_QI = (HRESULT (WINAPI *)(void *, const GUID *, void **))vt[0];
        real_CreateCommitted = (HRESULT (WINAPI *)(void *, const void *, UINT, const void *,
                                                   UINT, const void *, const GUID *, void **))
                               vt[SLOT_D3D12_CREATECOMMITTED];
        if (!patch_slot("d3d12 device QueryInterface", *device, 0, (void *)my_dev12_QI, &qi))
            real_dev12_QI = NULL;
        if (!patch_slot("d3d12 CreateCommittedResource", *device,
                        SLOT_D3D12_CREATECOMMITTED, (void *)my_CreateCommitted, &cc))
            real_CreateCommitted = NULL;

        hooked_device_ = *device;
        {
            static void *cf;
            real_CheckFeature = (HRESULT (WINAPI *)(void *, UINT, void *, UINT))
                                vt[SLOT_D3D12_CHECKFEATURE];
            if (!patch_slot("d3d12 CheckFeatureSupport", *device,
                            SLOT_D3D12_CHECKFEATURE, (void *)my_CheckFeature, &cf))
                real_CheckFeature = NULL;
        }
        {
            static void *cq;
            real_CreateCmdQueue = (HRESULT (WINAPI *)(void *, const void *, const GUID *, void **))
                                  vt[SLOT_D3D12_CREATECMDQUEUE];
            if (!patch_slot("d3d12 CreateCommandQueue", *device,
                            SLOT_D3D12_CREATECMDQUEUE, (void *)my_CreateCmdQueue, &cq))
                real_CreateCmdQueue = NULL;
        }
        {
            static void *pso, *rs;
            real_CreateComputePSO = (HRESULT (WINAPI *)(void *, const void *, const GUID *, void **))
                                    vt[SLOT_D3D12_CREATECOMPUTEPSO];
            real_CreateRootSig = (HRESULT (WINAPI *)(void *, UINT, const void *, SIZE_T,
                                                     const GUID *, void **))
                                 vt[SLOT_D3D12_CREATEROOTSIG];
            if (!patch_slot("d3d12 CreateComputePipelineState", *device,
                            SLOT_D3D12_CREATECOMPUTEPSO, (void *)my_CreateComputePSO, &pso))
                real_CreateComputePSO = NULL;
            if (!patch_slot("d3d12 CreateRootSignature", *device,
                            SLOT_D3D12_CREATEROOTSIG, (void *)my_CreateRootSig, &rs))
                real_CreateRootSig = NULL;
        }
    }
    return hr;
}

/* By ordinal, because that is how this game imports it.
 *
 * The import table lists d3d12.dll #101 -- D3D12CreateDevice with no name --
 * and hook_import walks names only, skipping ordinal entries outright. So the
 * hook installed, reported itself installed, and was never called: the log
 * showed no D3D12 device being created because nothing was watching the door
 * it came through.
 *
 * DYNASTY WARRIORS did exactly this and the lesson is written down in this
 * repository. Walking into it a second time is what comes of deriving a probe
 * from the wrong sibling -- this one grew from the Media Foundation probe,
 * which never needed ordinals. */
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


/* Watch DirectStorage, which we already are.
 *
 * The game ships its own dstorage.dll and DirectStorage/dstoragecore*.dll and
 * streams 35 GB of assets through them. It is a Windows 11 API for NVMe
 * streaming, and if it does not work here the game would sit loading forever:
 * black screen, live process, no error anywhere -- which is exactly the
 * symptom, and which nothing measured so far explains.
 *
 * We are the proxy for that DLL, so the four exports pass through us already.
 * This says whether they are called and what they answer.
 *
 * The forwarding itself is unchanged: each of these calls the real function and
 * returns its result.
 */
/* ---------------------------------------------------------------- crash context

   This executable is Denuvo-protected. On disk, the .text and .rdata around the
   addresses this crash touches are encrypted -- dumping them reads high-entropy
   noise, and the file carries two .text sections, which is what a packer looks
   like. In memory they are plain, because the protection decrypts as it runs.

   So the only place this crash can be read is from inside the process at the
   moment it happens. A vectored handler gets first refusal on the exception,
   before Wine prints its own dump, and can read what a disassembler cannot.

   Denuvo also throws first-chance exceptions as part of its own obfuscation, so
   this reports only the one we are chasing -- a read of address zero -- and only
   the first few times. */

static BOOL readable_(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    const BYTE *a = (const BYTE *)p;

    if (!p) return FALSE;
    while (n)
    {
        SIZE_T left;
        if (!VirtualQuery(a, &mbi, sizeof(mbi)))        return FALSE;
        if (mbi.State != MEM_COMMIT)                    return FALSE;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return FALSE;
        left = (SIZE_T)((const BYTE *)mbi.BaseAddress + mbi.RegionSize - a);
        if (left >= n) return TRUE;
        a += left; n -= left;
    }
    return TRUE;
}

/* Print 32 bytes and say what they look like: a C string, a UTF-16 string, or a
   run of pointers into the image, which would make it a vtable. */
static void dump_at_(const char *what, ULONG_PTR addr, ULONG_PTR base, ULONG_PTR end)
{
    BYTE b[32];
    char hex[32 * 3 + 1], asc[33];
    int i, printable = 0, wide = 1;

    if (!readable_((const void *)addr, sizeof(b)))
    {
        logf_("  %-16s %p  <unreadable>", what, (void *)addr);
        return;
    }
    memcpy(b, (const void *)addr, sizeof(b));
    for (i = 0; i < 32; i++)
    {
        sprintf(hex + i * 3, "%02x ", b[i]);
        asc[i] = (b[i] >= 32 && b[i] < 127) ? (char)b[i] : '.';
        if (b[i] >= 32 && b[i] < 127) printable++;
        if ((i & 1) && b[i]) wide = 0;
    }
    asc[32] = 0;
    logf_("  %-16s %p", what, (void *)addr);
    logf_("      %s|%s|", hex, asc);

    if (b[0] && printable >= 8 && memchr(b, 0, sizeof(b)))
        logf_("      -> C string: \"%s\"", (const char *)b);
    else if (wide && b[0])
    {
        char narrow[17];
        for (i = 0; i < 16; i++) narrow[i] = b[i * 2] ? (char)b[i * 2] : ' ';
        narrow[16] = 0;
        logf_("      -> UTF-16: \"%s\"", narrow);
    }
    else
    {
        ULONG_PTR p0, p1;
        memcpy(&p0, b, 8); memcpy(&p1, b + 8, 8);
        if (p0 >= base && p0 < end && p1 >= base && p1 < end)
            logf_("      -> looks like a vtable: +0x%llx, +0x%llx",
                  (unsigned long long)(p0 - base), (unsigned long long)(p1 - base));
    }
}

static void report_crash_(EXCEPTION_POINTERS *ep, const char *how)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    const CONTEXT *c = ep->ContextRecord;
    ULONG_PTR base, end, sp;
    MEMORY_BASIC_INFORMATION mbi;
    int shown;

    base = (ULONG_PTR)GetModuleHandleW(NULL);
    end  = base + 0x3000000;

    logf_("============ %s: code 0x%08lx at +0x%llx ============",
          how, er->ExceptionCode,
          (unsigned long long)((ULONG_PTR)er->ExceptionAddress - base));
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        logf_("  %s address %p",
              er->ExceptionInformation[0] ? "writing" : "reading",
              (void *)er->ExceptionInformation[1]);
    logf_("  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx",
          (unsigned long long)c->Rax, (unsigned long long)c->Rbx,
          (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
    logf_("  rsi=%016llx rdi=%016llx r12=%016llx r13=%016llx",
          (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
          (unsigned long long)c->R12, (unsigned long long)c->R13);

    /* The decrypted instruction stream -- what the on-disk binary hides. */
    dump_at_("code at rip", (ULONG_PTR)er->ExceptionAddress, base, end);

    /* The objects in play. One of these holds the null. */
    dump_at_("*rbx", c->Rbx, base, end);
    dump_at_("*rsi", c->Rsi, base, end);
    dump_at_("*r13", c->R13, base, end);
    dump_at_("*r12", c->R12, base, end);

    /* Anything on the stack pointing into the image. The constant paired with 15
       on every dump so far lives in .rdata, and in memory it should be legible. */
    logf_("  -- stack pointers into the image --");
    shown = 0;
    for (sp = c->Rsp; sp < c->Rsp + 0x200 && shown < 12; sp += 8)
    {
        ULONG_PTR v;
        if (!readable_((const void *)sp, sizeof(v))) break;
        memcpy(&v, (const void *)sp, sizeof(v));
        if (v <= base || v >= end) continue;
        if (VirtualQuery((void *)v, &mbi, sizeof(mbi)) &&
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        {
            char tag[40];
            sprintf(tag, "data +0x%llx", (unsigned long long)(v - base));
            dump_at_(tag, v, base, end);
        }
        else
        {
            /* Print what runs immediately before the return: that is the call
             * itself, and the instructions that loaded its arguments. */
            char tag[48];
            sprintf(tag, "call site  +0x%llx", (unsigned long long)(v - base - 32));
            dump_at_(tag, v - 32, base, end);
        }
        shown++;
    }
    logf_("======================================================");
}

/* Two ways in, because they see different things.

   The vectored handler gets first refusal on every exception, which is how we
   caught the read of address zero -- but Denuvo throws first-chance exceptions
   as part of its own obfuscation, so this one stays narrowed to that fault.

   The unhandled filter runs only when nothing else claimed the exception: it is
   the last thing before Wine puts up its dialog. That makes it the honest report
   of whatever actually kills the process, whatever kind of fault it is. */
static LONG CALLBACK crash_context_(EXCEPTION_POINTERS *ep)
{
    static LONG reported;
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;

    /* OutputDebugString arrives as an exception under Wine. DirectStorage explains
     * its own refusals through it once debug flags are on, so this is where the
     * answer should appear -- in Microsoft's own words rather than our inference. */
    if (er->ExceptionCode == DBG_PRINTEXCEPTION_C && er->NumberParameters >= 2)
    {
        const char *msg = (const char *)er->ExceptionInformation[1];
        if (readable_(msg, 1)) logf_("[debug] %.300s", msg);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    /* And the wide variant, which is a different exception code entirely. Every
     * diagnostic string inside dstoragecore is UTF-16, so this is the one that
     * matters here -- the narrow handler above only ever caught Steam. */
    if (er->ExceptionCode == 0x4001000A && er->NumberParameters >= 2)
    {
        const WCHAR *w = (const WCHAR *)er->ExceptionInformation[1];
        if (readable_(w, 2))
        {
            char narrow[301];
            int i;
            for (i = 0; i < 300 && readable_(w + i, 2) && w[i]; i++)
                narrow[i] = (w[i] < 128) ? (char)w[i] : '?';
            narrow[i] = 0;
            logf_("[debug] %s", narrow);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    if (er->NumberParameters < 2 || er->ExceptionInformation[1] != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&reported) > 3)      return EXCEPTION_CONTINUE_SEARCH;

    report_crash_(ep, "read of address 0");
    return EXCEPTION_CONTINUE_SEARCH;             /* let Wine print its dump too */
}

static LONG (WINAPI *prev_filter_)(EXCEPTION_POINTERS *);

static LONG WINAPI fatal_filter_(EXCEPTION_POINTERS *ep)
{
    report_crash_(ep, "FATAL, nothing handled it");
    return prev_filter_ ? prev_filter_(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static HRESULT (WINAPI *real_DStorageGetFactory)(const GUID *, void **);
static HRESULT (WINAPI *real_DStorageSetConfiguration)(const void *);
static HRESULT (WINAPI *real_DStorageSetConfiguration1)(const void *);
static HRESULT (WINAPI *real_DStorageCreateCompressionCodec)(UINT, UINT, const GUID *, void **);

/* DirectStorage's configuration, version 1. Laid out here rather than pulled
 * from the SDK header so the probe keeps building without it; the raw dwords
 * are logged alongside the named fields so a run confirms the layout instead
 * of us trusting it.
 *
 * The last two fields are the whole point. GDeflate decompression on the GPU
 * runs as a D3D12 compute pass, and that pass is the one thing here D3DMetal
 * has never been able to survive -- which is presumably why someone disabled
 * dstoragecore.dll outright by renaming it. Turning these two off moves the
 * work to the CPU: slower loading, but loading that finishes. */
struct dstorage_config1
{
    UINT32 NumSubmitThreads;
    INT32  NumBuiltInCpuDecompressionThreads;
    BOOL   ForceMappingLayer;
    BOOL   DisableBypassIO;
    BOOL   DisableTelemetry;
    BOOL   DisableGpuDecompressionMetacommand;
    BOOL   DisableGpuDecompression;
};

static LONG config1_seen;

/* Hand DirectStorage a configuration with GPU decompression off. Used both for
 * the game's own call and, if the game never makes one, ahead of the factory. */
static HRESULT set_config1_cpu_only(const struct dstorage_config1 *from)
{
    struct dstorage_config1 cfg;
    HRESULT hr;

    if (!real_DStorageSetConfiguration1) return E_NOTIMPL;

    if (from)
    {
        const UINT32 *raw = (const UINT32 *)from;
        cfg = *from;
        logf_("  config in : submit=%u cpuThreads=%d mapping=%u bypassIO=%u telemetry=%u "
              "metacmd=%u gpuDecomp=%u",
              raw[0], (INT32)raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]);
    }
    else
    {
        memset(&cfg, 0, sizeof(cfg));
        logf_("  config in : (none -- the game never set one, so these are defaults)");
    }

    /* Nothing is forced any more. These fields are named Disable*, so the zeros
     * the game passes mean the features are ON, not off -- the opposite of how
     * they were read at first. Forcing them changed nothing about the crash, but
     * it did mean every run since was measuring a configuration the game never
     * asked for, immediately before the call that fails. Pass it through. */

    /* ForceMappingLayer was tried here and made things worse: the game died
     * earlier, before CreateQueue, and with a different kind of fault. The
     * compatibility layer is evidently not a drop-in for what this engine does,
     * so it stays off and the native path stays the thing to explain. */

    hr = real_DStorageSetConfiguration1(&cfg);
    logf_("  config out: passed through unchanged -> 0x%08lx", hr);
    return hr;
}

/* IDStorageFactory::CreateQueue is slot 3. The crash dump shows the game calling
   slot 9 -- IDStorageQueue::GetErrorEvent -- on a null object and storing the
   result, which is exactly what an unchecked CreateQueue failure looks like. So
   this reports the descriptor it asked for and what it got back. */
#define SLOT_DS_CREATE_QUEUE 3

struct dstorage_queue_desc
{
    UINT32       SourceType;
    UINT16       Capacity;
    INT16        Priority;
    const char  *Name;
    void        *Device;
};

static HRESULT (WINAPI *real_CreateQueue)(void *, const struct dstorage_queue_desc *,
                                          const GUID *, void **);

/* IDStorageFactory::SetStagingBufferSize is slot 7. The game asks for 256 MB,
   and dstoragecore turns that into three staging buffers -- upload, decompression
   input, decompression output -- allocated as D3D12 heaps the first time a device
   is attached, which is inside CreateQueue. Roughly three quarters of a gigabyte,
   requested from a translation layer on unified memory, at the exact moment the
   call starts failing.

   DirectStorage's own default is 32 MB, so this is a return to normal rather than
   an unusual value. If the refusal is an allocation, this is the whole fix; if it
   is not, the CreateCommittedResource logging below says so instead. */
#define SLOT_DS_SET_STAGING 7
#define STAGING_DEFAULT     (32u * 1024 * 1024)

static HRESULT (WINAPI *real_SetStaging)(void *, UINT32);

static HRESULT WINAPI my_SetStaging(void *self, UINT32 size)
{
    HRESULT hr;
    if (size > STAGING_DEFAULT)
    {
        logf_("IDStorageFactory::SetStagingBufferSize(%u MB) -> asking for %u MB instead",
              size / (1024 * 1024), STAGING_DEFAULT / (1024 * 1024));
        size = STAGING_DEFAULT;
    }
    hr = real_SetStaging(self, size);
    logf_("  staging buffer now %u MB -> 0x%08lx", size / (1024 * 1024), hr);
    return hr;
}

static HRESULT WINAPI my_CreateQueue(void *self, const struct dstorage_queue_desc *desc,
                                     const GUID *iid, void **out)
{
    HRESULT hr;

    /* Print the descriptor as raw dwords rather than trusting a layout. A first
     * attempt here dereferenced what it took to be the name and crashed inside
     * vsnprintf, which cost two runs and looked exactly like the game dying. The
     * config struct was read this way and the raw values confirmed its shape, so
     * the same discipline applies to anything else handed in by the game. */
    if (desc)
    {
        const UINT32 *raw = (const UINT32 *)desc;
        if (readable_(desc, 32))
            logf_("IDStorageFactory::CreateQueue desc: %08x %08x %08x %08x %08x %08x %08x %08x",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
        else
            logf_("IDStorageFactory::CreateQueue desc: %p <unreadable>", (const void *)desc);

        /* Only follow a name pointer once it is known to be mapped, and cap it. */
        {
            int off;
            for (off = 8; off <= 16; off += 8)
            {
                const char *name;
                memcpy(&name, (const BYTE *)desc + off, sizeof(name));
                if (readable_(name, 1))
                    logf_("  a name at +%d: \"%.31s\"", off, name);
            }
        }
    }

    if (desc && readable_(desc, 32))
    {
        void *dev;
        memcpy(&dev, (const BYTE *)desc + 24, sizeof(dev));
        logf_("  device=%p  (%s)", dev,
              dev == hooked_device_ ? "the one we hooked"
                                    : "NOT the device we hooked -- something wraps it");
    }

    InterlockedExchange(&in_create_queue_, 1);
    hr = real_CreateQueue(self, desc, iid, out);
    InterlockedExchange(&in_create_queue_, 0);

    logf_("  -> 0x%08lx, queue=%p%s", hr, out ? *out : NULL,
          (FAILED(hr) || !out || !*out)
              ? "   << no queue -- and the game stores this without checking it"
              : "");
    return hr;
}

static HRESULT WINAPI my_DStorageGetFactory(const GUID *iid, void **out)
{
    HRESULT hr;

    /* Configuration is only accepted before the factory exists. If the game is
     * about to create one without having configured anything, this is the last
     * moment we get. */
    if (!InterlockedCompareExchange(&config1_seen, 1, 0))
    {
        logf_("DStorageGetFactory: no configuration came first -- setting one now");
        set_config1_cpu_only(NULL);
    }

    hr = real_DStorageGetFactory ? real_DStorageGetFactory(iid, out) : E_NOTIMPL;
    logf_("DStorageGetFactory -> 0x%08lx%s", hr,
          FAILED(hr) ? "   << no factory: the game cannot stream its assets" : "");

    /* Refuse the factory, and let the game choose its other I/O backend.
     *
     * The queue cannot be had: every capability DirectStorage asks about is
     * granted, no pipeline is ever built, and it still refuses -- and the reason
     * is not reachable from here, because the Agility SDK this game ships is
     * never loaded under Wine at all. There is nothing left to concede.
     *
     * So aim at the game instead of at the queue. Its backend dispatcher picks
     * DirectStorage on nothing more than a null test of this pointer, which is
     * exactly why disabling dstoragecore.dll changed behaviour. Refusing here is
     * the same lever, but without disturbing the install and with every other
     * probe still reporting -- so this run says how far the other path gets. */
    if (SUCCEEDED(hr) && out && *out)
    {
        void **factory = (void **)*out;
        ((HRESULT (WINAPI *)(void *))((void **)*factory)[2])(factory);   /* Release */
        *out = NULL;
        logf_("  refused, so the game falls back to its other I/O backend");
        return E_NOINTERFACE;
    }

    /* Armed. The two runs that appeared to die from this patch were in fact
     * dying inside its own log call, which the crash dump named outright:
     * vsnprintf, formatting a name pointer that was never valid. The patch
     * itself was innocent. */
    if (SUCCEEDED(hr) && out && *out)
    {
        patch_slot("IDStorageFactory::CreateQueue", *out, SLOT_DS_CREATE_QUEUE,
                   (void *)my_CreateQueue, (void **)&real_CreateQueue);
        patch_slot("IDStorageFactory::SetStagingBufferSize", *out, SLOT_DS_SET_STAGING,
                   (void *)my_SetStaging, (void **)&real_SetStaging);

        /* SetDebugFlags is slot 6, and DSTORAGE_DEBUG_SHOW_ERRORS is 1. Every
         * capability DirectStorage asks about here is granted and it still
         * refuses the queue, so rather than instrument more of the device, ask
         * the component that is refusing to say why. */
        {
            void (WINAPI *set_flags)(void *, UINT32) =
                (void (WINAPI *)(void *, UINT32))(*(void ***)*out)[6];
            set_flags(*out, 1);
            logf_("  DirectStorage error reporting: on");
        }
    }
    return hr;
}

static HRESULT WINAPI my_DStorageSetConfiguration(const void *config)
{
    HRESULT hr = real_DStorageSetConfiguration ? real_DStorageSetConfiguration(config) : E_NOTIMPL;
    logf_("DStorageSetConfiguration -> 0x%08lx", hr);
    return hr;
}

static HRESULT WINAPI my_DStorageSetConfiguration1(const void *config)
{
    HRESULT hr;

    InterlockedExchange(&config1_seen, 1);
    hr = set_config1_cpu_only((const struct dstorage_config1 *)config);
    logf_("DStorageSetConfiguration1 -> 0x%08lx", hr);
    return hr;
}

static HRESULT WINAPI my_DStorageCreateCompressionCodec(UINT format, UINT threads,
                                                        const GUID *iid, void **out)
{
    static LONG said;
    HRESULT hr = real_DStorageCreateCompressionCodec
               ? real_DStorageCreateCompressionCodec(format, threads, iid, out) : E_NOTIMPL;
    if (InterlockedIncrement(&said) <= 2)
        logf_("DStorageCreateCompressionCodec(format=%u) -> 0x%08lx", format, hr);
    return hr;
}

static void watch_directstorage(void)
{
    HMODULE real = GetModuleHandleA("dstorage_real.dll");
    if (!real) real = LoadLibraryA("dstorage_real.dll");
    if (!real) { logf_("DirectStorage: cannot reach dstorage_real.dll"); return; }
#define GRAB(fn) *(FARPROC *)&real_##fn = GetProcAddress(real, #fn)
    GRAB(DStorageGetFactory);
    GRAB(DStorageSetConfiguration);
    GRAB(DStorageSetConfiguration1);
    GRAB(DStorageCreateCompressionCodec);
#undef GRAB
    {
        void *was;
        int n = 0;
        if ((was = hook_import("dstorage.dll", "DStorageGetFactory",
                               (void *)my_DStorageGetFactory))) n++;
        if ((was = hook_import("dstorage.dll", "DStorageSetConfiguration",
                               (void *)my_DStorageSetConfiguration)))  n++;
        if ((was = hook_import("dstorage.dll", "DStorageSetConfiguration1",
                               (void *)my_DStorageSetConfiguration1))) n++;
        if ((was = hook_import("dstorage.dll", "DStorageCreateCompressionCodec",
                               (void *)my_DStorageCreateCompressionCodec))) n++;
        (void)was;
        logf_("DirectStorage: %d of 4 entry points watched", n);
    }
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    {
        char v[8] = {0};
        if (GetEnvironmentVariableA("BEAST_REFUSE_D3D_MANAGER", v, sizeof(v)) && v[0] == '1')
            refuse_d3d_manager = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("BEAST_FORCE_NV12", v, sizeof(v)) && v[0] == '1')
            restore_nv12 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_NO_MFT_ANSWER", v, sizeof(v)) && v[0] == '1')
            answer_mft_gate = FALSE;
        v[0] = 0;
        if (GetEnvironmentVariableA("P5S_REAL_FRAMES", v, sizeof(v)) && v[0] == '1')
            paint_magenta = FALSE;
    }
    /*
     * MFCreateFile is called, never intercepted, so what matters is having its
     * address rather than having hooked it. Taken straight out of mfplat,
     * because this game resolves nearly everything through GetProcAddress and
     * an import-table lookup would leave it null -- which is how the same
     * pointer was silently missing in another title's bridge and cost a run.
     */
    {
        HMODULE mfplat_ = LoadLibraryA("mfplat.dll");
        if (mfplat_)
            *(void **)&real_MFCreateFile = (void *)GetProcAddress(mfplat_, "MFCreateFile");
        logf_("MFCreateFile (direct): %s",
              real_MFCreateFile ? "resolved" : "NOT FOUND -- the content retry cannot run");
    }

    /* Hook the import table as well as GetProcAddress.
     *
     * The version of this probe that found Beast of Reincarnation's faults only
     * hooked GetProcAddress, because that game delay-loads Media Foundation and
     * resolves every entry point through it. Persona 5 Strikers imports the
     * same functions normally, so the loader binds them and GetProcAddress is
     * never asked -- the probe watched an empty road and reported no traffic.
     *
     * Both are needed, and neither implies the other. */
    {
        struct { const char *dll, *fn; void *ours; void **real; } hooks[] = {
            { "mfplat.dll",      "MFStartup",
              (void *)my_MFStartup,      (void **)&real_MFStartup },
            { "mfplat.dll",      "MFTEnumEx",
              (void *)my_MFTEnumEx,      (void **)&real_MFTEnumEx },
            { "mfreadwrite.dll", "MFCreateSourceReaderFromByteStream",
              (void *)my_MFCreateSourceReaderFromByteStream,
              (void **)&real_MFCreateSourceReaderFromByteStream },
            { "mfreadwrite.dll", "MFCreateSourceReaderFromURL",
              (void *)my_MFCreateSourceReaderFromURL,
              (void **)&real_MFCreateSourceReaderFromURL },
        };
        size_t i;
        int got = 0;
        for (i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        {
            void *was = hook_import(hooks[i].dll, hooks[i].fn, hooks[i].ours);
            if (was) { *hooks[i].real = was; got++; }
        }
        {
            void *was;
            if ((was = hook_import("d3d9.dll", "Direct3DCreate9", (void *)my_Direct3DCreate9)))
                { *(void **)&real_Direct3DCreate9 = was; got++; }
            if ((was = hook_import("d3d9.dll", "Direct3DCreate9Ex", (void *)my_Direct3DCreate9Ex)))
                { *(void **)&real_Direct3DCreate9Ex = was; got++; }
            if ((was = hook_import("d3d11.dll", "D3D11CreateDevice", (void *)my_D3D11CreateDevice)))
                { *(void **)&real_D3D11CreateDevice = was; got++; }
            if ((was = hook_import("d3d12.dll", "D3D12CreateDevice", (void *)my_D3D12CreateDevice)))
                { *(void **)&real_D3D12CreateDevice = was; got++; }
            if (!real_D3D12CreateDevice
                && (was = hook_import_ordinal("d3d12.dll", 101, (void *)my_D3D12CreateDevice)))
                { *(void **)&real_D3D12CreateDevice = was; got++; }
            if ((was = hook_import("sl.interposer.dll", "D3D12CreateDevice",
                                   (void *)my_D3D12CreateDevice)))
                { if (!real_D3D12CreateDevice) *(void **)&real_D3D12CreateDevice = was;
                  got++; }
        }
        logf_("import table: %d of %d Media Foundation and D3D9 entries hooked "
              "(0 here means this game resolves them some other way)",
              got, (int)(sizeof(hooks) / sizeof(hooks[0])) + 2);
    }

    watch_directstorage();
    logf_("---- write-path hooks %s | painting %s ----",
          watch_write_path ? "ON" : "off",
          paint_magenta ? "SOLID MAGENTA" : "the real frames");
    logf_("---- armed: D3D manager %s from the MFT | NV12 relabel %s | "
          "MFCreateDXGIDeviceManager %s ----",
          withhold_d3d_from_mft ? "WITHHELD" : "passed",
          restore_nv12 ? "on" : "off",
          refuse_d3d_manager ? "refused" : "allowed");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        /* Armed before anything else: the crash we are chasing happens on a
         * worker thread during startup, and this is the only reader that can
         * see through the anti-tamper encryption. */
        AddVectoredExceptionHandler(1, crash_context_);
        prev_filter_ = SetUnhandledExceptionFilter(fatal_filter_);
        /* GetProcAddress has to be in place before the game resolves anything,
         * so it goes in here rather than on the worker thread. */
        *(void **)&real_GetProcAddress =
            hook_import("KERNEL32.dll", "GetProcAddress", (void *)my_GetProcAddress);
        if (!real_GetProcAddress)
            *(FARPROC *)&real_GetProcAddress =
                GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
        CreateThread(NULL, 0, worker, NULL, 0, NULL);
    }
    return TRUE;
}
