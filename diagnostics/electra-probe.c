/* electra-probe -- ask the game what it is doing with its video, and change
 * nothing while asking.
 *
 * Beast of Reincarnation plays a startup video whose audio is audible and
 * whose picture never appears. Four separate levers made no difference:
 * forcing Electra's VPx output to the CPU path, and the H264/H265
 * UseOldOutputPath CVars. Each of those covers one decode path, so the fact
 * that none of them moved anything says the problem is not where they act --
 * but it does not say where it is.
 *
 * The point of this file is to stop deducing. It answers three questions:
 *
 *   1. Does the startup movie go through Media Foundation at all? If MFStartup
 *      is never called, every hypothesis about MF, winevideo, registry
 *      mappings and D3D-aware MFTs is irrelevant by construction.
 *   2. If it does, what codec is it asking for? MFTEnumEx carries the subtype
 *      GUID of the format the caller wants decoded, which is the codec by
 *      FourCC, and is the fact four investigations have been guessing at.
 *   3. Which module asks, and does it get an answer?
 *
 * It hooks by import table and by GetProcAddress, because this game
 * delay-loads MFPlat, MFReadWrite and MF -- an import-table hook alone
 * installs correctly, reports itself hooked, and is never called.
 *
 * Purely observational. Every wrapper calls the real function and returns its
 * real result. A probe that changes behaviour cannot distinguish the fault
 * from itself.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define LOGFILE "C:\\electra-probe.log"

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
static BOOL restore_nv12 = TRUE;

/* Withhold the D3D manager from the decoder, without denying it to the game.
 *
 * Refusing MFCreateDXGIDeviceManager outright was too blunt: the game gave up
 * on video entirely and never touched the decoder. The manager it wants is for
 * its own renderer as much as for decoding, so it gets to have one -- the
 * decoder simply never hears about it, which is the state in which NV12 is not
 * censored and system-memory output is the honest answer. */
static BOOL withhold_d3d_from_mft = TRUE;

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
        force_electra_software();
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
        logf_("MFCreateDXGIDeviceManager -> 0x%08lx (allowed through)", hr);
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

static HRESULT WINAPI my_MFTEnumEx(GUID category, UINT32 flags,
                                   const REG_TYPE_INFO *in,
                                   const REG_TYPE_INFO *out,
                                   void ***mfts, UINT32 *count)
{
    HRESULT hr = real_MFTEnumEx(category, flags, in, out, mfts, count);
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

static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(void *stream, void *attrs, void **reader)
{
    HRESULT hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
    logf_("MFCreateSourceReaderFromByteStream -> 0x%08lx", hr);
    return hr;
}

static HRESULT WINAPI my_MFCreateSourceReaderFromURL(LPCWSTR url, void *attrs, void **reader)
{
    HRESULT hr = real_MFCreateSourceReaderFromURL(url, attrs, reader);
    logf_("MFCreateSourceReaderFromURL(%ls) -> 0x%08lx", url ? url : L"(null)", hr);
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

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    {
        char v[8] = {0};
        if (GetEnvironmentVariableA("BEAST_REFUSE_D3D_MANAGER", v, sizeof(v)) && v[0] == '1')
            refuse_d3d_manager = TRUE;
        v[0] = 0;
        /* On by default, so the lever is the one that turns it OFF. Asking for
         * '1' set TRUE a flag that was already TRUE: the relabel could not be
         * disabled, and the log below could only ever print "on" while
         * advertising a state that had two values. This is the idiom the rest
         * of the tree uses for a flag that ships armed. */
        if (GetEnvironmentVariableA("BEAST_FORCE_NV12", v, sizeof(v)) && v[0] == '0')
            restore_nv12 = FALSE;
    }
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
