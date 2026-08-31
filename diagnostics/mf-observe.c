/* mf-observe -- watch a game's Media Foundation use and change nothing.
 *
 * Every intervention this file grew for Beast of Reincarnation is off. A probe
 * that alters behaviour cannot be told apart from the fault it is looking for,
 * and on a game whose path is not yet understood the alterations would be
 * guesses about a mechanism nobody has established.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define LOGFILE "C:\\mf-observe.log"

static LONG CALLBACK note_exception(EXCEPTION_POINTERS *info);

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
static BOOL logf_had_enough;

/* Milliseconds since the probe loaded, in front of every line.
 *
 * Added late, and it cost: this file's log could not tell a title that died in
 * two seconds from one that sat for ninety, and both happen here from launch
 * to launch. A log without time is a list of things that happened in an order
 * -- useful, but it cannot answer "how long did that take", which was the
 * question. */
static DWORD logf_t0;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n, m;

    if (!logf_t0) logf_t0 = GetTickCount();
    n = snprintf(buf, sizeof(buf) - 2, "[%6lu ms] [%s] ",
                 (unsigned long)(GetTickCount() - logf_t0), process_name());
    if (n < 0) n = 0;
    va_start(ap, fmt);
    m = vsnprintf(buf + n, sizeof(buf) - 2 - n, fmt, ap);
    va_end(ap);
    if (m < 0) return;
    n += m;
    buf[n] = '\n';

    /* The cap keeps a runaway hook from filling the disk. It used to be 300,
     * which a title that actually reaches its videos exhausts in minutes --
     * and it stops without a word, so a truncated log and a dead process read
     * exactly alike. Higher, and it says when it stops. */
    {
        LONG line = InterlockedIncrement(&log_lines);
        if (line == 3000)
            logf_had_enough = TRUE;
        if (line > 3000)
        {
            if (line == 3001)
            {
                char note[] = "[log full: 3000 lines, nothing further is recorded]\n";
                HANDLE hh = CreateFileA(LOGFILE, FILE_APPEND_DATA,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hh != INVALID_HANDLE_VALUE)
                {
                    DWORD w;
                    SetFilePointer(hh, 0, NULL, FILE_END);
                    WriteFile(hh, note, sizeof(note) - 1, &w, NULL);
                    CloseHandle(hh);
                }
            }
            return;
        }
    }

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
static HRESULT (WINAPI *real_MFCreateSourceReaderFromMediaSource)(void *, void *, void **);
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

/* The geometry of one decoded frame, read rather than guessed.
 *
 * A green band along the bottom of Peace Walker's cutscenes survived filling
 * the buffer's unwritten tail, so the tail was not the cause. What settles it
 * is the buffer's own numbers: whether it is a 2D buffer at all, what it says
 * its lengths are, and what pitch it reports. Raw vtable calls, like the rest
 * of this file -- no Media Foundation headers are included here.
 *
 *   IMFSample:      GetBufferByIndex 40 (IUnknown 0-2, IMFAttributes 3-32)
 *   IMFMediaBuffer: QueryInterface 0, GetCurrentLength 5, GetMaxLength 7
 *   IMF2DBuffer2:   GetScanline0AndPitch 5, GetContiguousLength 7
 */
static const GUID guid_IID_IMF2DBuffer2 =
    { 0x33ae5ea6, 0x4316, 0x436f, { 0x8d, 0xdd, 0xd7, 0x3d, 0x22, 0xf8, 0x29, 0xec } };

static void log_frame_geometry(void *sample)
{
    void **svt, *buffer = NULL, *two = NULL;
    DWORD cur = 0, max = 0, contig = 0;
    BYTE *scan0 = NULL;
    LONG pitch = 0;

    if (!sample) return;
    svt = *(void ***)sample;
    if (FAILED(((HRESULT (WINAPI *)(void *, DWORD, void **))svt[40])(sample, 0, &buffer)) || !buffer)
        return;

    {
        void **bvt = *(void ***)buffer;
        ((HRESULT (WINAPI *)(void *, DWORD *))bvt[5])(buffer, &cur);
        ((HRESULT (WINAPI *)(void *, DWORD *))bvt[7])(buffer, &max);

        if (SUCCEEDED(((HRESULT (WINAPI *)(void *, const GUID *, void **))bvt[0])
                          (buffer, &guid_IID_IMF2DBuffer2, &two)) && two)
        {
            void **tvt = *(void ***)two;
            ((HRESULT (WINAPI *)(void *, DWORD *))tvt[7])(two, &contig);
            ((HRESULT (WINAPI *)(void *, BYTE **, LONG *))tvt[5])(two, &scan0, &pitch);
            logf_("frame geometry: 2D yes | current %lu | max %lu | contiguous %lu | pitch %ld",
                  (unsigned long)cur, (unsigned long)max, (unsigned long)contig, (long)pitch);
            ((ULONG (WINAPI *)(void *))tvt[2])(two);
        }
        else
            logf_("frame geometry: 2D NO | current %lu | max %lu", (unsigned long)cur, (unsigned long)max);

        ((ULONG (WINAPI *)(void *))bvt[2])(buffer);
    }
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
        if (g == 1) log_frame_geometry(*sample);
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

/* The third way in, and the one this file did not watch.
 *
 * Metal Gear Solid Peace Walker decrypts its video in memory and builds its own
 * media source, so it reaches the reader through neither a URL nor a byte
 * stream. The reader was therefore never wrapped, GetNativeMediaType never
 * hooked, and the log had nothing to say about the format -- which is what was
 * needed to explain a green band along the bottom of its frames. */
static HRESULT WINAPI my_MFCreateSourceReaderFromMediaSource(void *source, void *attrs, void **reader)
{
    static LONG made;
    HRESULT hr = real_MFCreateSourceReaderFromMediaSource(source, attrs, reader);
    LONG n = InterlockedIncrement(&made);

    if (n == 1 || FAILED(hr))
        logf_("MFCreateSourceReaderFromMediaSource -> 0x%08lx (reader %ld)", hr, n);
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

static HRESULT WINAPI my_MFCreateSourceReaderFromURL(LPCWSTR url, void *attrs, void **reader)
{
    HRESULT hr = real_MFCreateSourceReaderFromURL(url, attrs, reader);
    logf_("MFCreateSourceReaderFromURL(%ls) -> 0x%08lx", url ? url : L"(null)", hr);
    return hr;
}

/* Watch what the game asks the D3D12 device for.
 *
 * Media Foundation has three doors -- MFTEnumEx, the source readers, and
 * IMFMediaEngine through CoCreateInstance -- and this probe watches all three.
 * RISE OF THE RONIN opens none of them and plays a cutscene anyway: audio
 * fine, picture black, eight megabytes a second streaming out of a packed
 * archive the whole time. There is a fourth door. A D3D12 title can decode
 * video with ID3D12VideoDevice, obtained by QueryInterface on the device it
 * already has, and nothing about that touches Media Foundation. Three zeroes
 * were read here as "this game has no video" when they only meant "not through
 * those three".
 *
 * So: patch QueryInterface on the device and name every interface asked for.
 * If ID3D12VideoDevice is requested and refused, that is the whole fault.
 *
 * Behind a switch because NINJA GAIDEN 4 stalls when this probe patches D3D12
 * vtables -- see ng4-observe, where every such patch is deliberately left out
 * for that reason.
 *
 * It was briefly written up here that the same patch cost RISE OF THE RONIN its
 * two intro videos. That was wrong, and the error is worth keeping: the switch
 * armed three things at once -- this patch, a hook on GetProcAddress inside
 * sl.interposer.dll, and one in sl.common.dll -- so turning it off moved three
 * variables and proved nothing about any of them. With the switch reduced to
 * this patch alone the videos play and the patch still reports everything. What
 * cost the videos was hooking inside Streamline, which is an interposer already
 * proxying the graphics API. A control that moves three things measures none.
 *
 * With that settled the reading means what it says: Ronin asks the device for
 * ID3D12Device1, ID3D12Device5 and one vendor interface, all granted, and never
 * for a video device -- measured in a run where its video works.
 *
 * Asked for by name: put 'd3d12' in C:\mgvf-trace.txt. */
static BOOL watch_d3d12;
static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, const GUID *, void **);
static HRESULT WINAPI my_D3D12CreateDevice(void *, UINT, const GUID *, void **);

/* The interfaces worth recognising by name. Everything else is printed raw. */
static const GUID IID_ID3D12VideoDevice_  =
    { 0x1f052807, 0x0b46, 0x4acc, { 0x8a, 0x89, 0x36, 0x4f, 0x79, 0x37, 0x18, 0xa4 } };
static const GUID IID_ID3D12VideoDevice1_ =
    { 0x981611ad, 0xa144, 0x4c83, { 0x98, 0x90, 0xf3, 0x0e, 0x26, 0xd6, 0x58, 0xab } };
static const GUID IID_ID3D12VideoDevice2_ =
    { 0xf019ac49, 0xf838, 0x4a95, { 0x9b, 0x17, 0x57, 0x94, 0x37, 0xc8, 0xf5, 0x13 } };
static const GUID IID_ID3D12VideoDevice3_ =
    { 0x4243adb4, 0x3a32, 0x4666, { 0x97, 0x3c, 0x0c, 0xcc, 0x56, 0x25, 0xdc, 0x44 } };

static const char *name_of_iid(const GUID *g)
{
    if (IsEqualGUID(g, &IID_ID3D12VideoDevice_))  return "ID3D12VideoDevice";
    if (IsEqualGUID(g, &IID_ID3D12VideoDevice1_)) return "ID3D12VideoDevice1";
    if (IsEqualGUID(g, &IID_ID3D12VideoDevice2_)) return "ID3D12VideoDevice2";
    if (IsEqualGUID(g, &IID_ID3D12VideoDevice3_)) return "ID3D12VideoDevice3";
    return NULL;
}

static HRESULT (WINAPI *real_dev12_QueryInterface)(void *, const GUID *, void **);

static HRESULT WINAPI my_dev12_QueryInterface(void *self, const GUID *iid, void **out)
{
    HRESULT hr = real_dev12_QueryInterface(self, iid, out);
    if (iid)
    {
        const char *known = name_of_iid(iid);
        if (known)
        {
            /* Not capped. This is the answer the whole probe was rebuilt for,
             * and it is asked once or twice in a session. */
            logf_("  D3D12 QueryInterface(%s) -> 0x%08lX  %s", known, (unsigned long)hr,
                  SUCCEEDED(hr) ? "<< GRANTED: the game can decode video on the GPU"
                                : "<< REFUSED: this is where the cutscene goes black");
        }
        else
        {
            static LONG said;
            if (InterlockedIncrement(&said) <= 40)
                logf_("  D3D12 QueryInterface {%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X} -> 0x%08lX",
                      (unsigned long)iid->Data1, iid->Data2, iid->Data3,
                      iid->Data4[0], iid->Data4[1], iid->Data4[2], iid->Data4[3],
                      iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7],
                      (unsigned long)hr);
        }
    }
    return hr;
}

/* Every distinct texture shape the renderer asks D3D12 for, reported once.
 *
 * Lifted from dwo-video-bridge, whose comment explains why shapes and not
 * calls: a renderer creates thousands of textures and reuses a handful of
 * shapes, so the first two dozen calls are all interface art and the one that
 * matters -- the surface the size of the clip -- is created later and never
 * reaches a log capped by call count.
 *
 * This is the evidence that does not require knowing anything about the
 * decoder. RONIN decodes its cutscene inside its own packed executable: all
 * four system video paths are measured shut, and the code cannot be read off
 * disk. But whatever decodes it still has to put the picture somewhere D3D12
 * can draw from. A video-shaped resource appearing when the black screen
 * starts says a frame was produced and lost on the way to the screen. None
 * appearing says the decoder never produced one. Those are the two remaining
 * explanations and they are indistinguishable from the sofa. */
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

/* The formats a decoded frame actually arrives in, named. Everything else is
 * printed as a number, because guessing at a name is worse than a number. */
static const char *dxgi_format_note(UINT f)
{
    switch (f)
    {
    case 103: return "  << NV12 -- a decoded video frame";
    case 104: return "  << P010 -- a decoded 10-bit video frame";
    case 87:  return "  (BGRA8)";
    case 28:  return "  (RGBA8)";
    default:  return "";
    }
}

static HRESULT (WINAPI *real_create_committed)(void *, const void *, UINT,
        const void *, UINT, const void *, const GUID *, void **);
static HRESULT (WINAPI *real_create_placed)(void *, void *, UINT64,
        const void *, UINT, const void *, const GUID *, void **);

/* The shared body: both entry points carry a D3D12_RESOURCE_DESC and differ
 * only in where it sits in the argument list. */
static void note_resource_shape(const char *how, const void *desc, HRESULT hr)
{
    const char *d = (const char *)desc;
    UINT w, h, fmt;
    if (!desc) return;
    w   = (UINT)(*(const UINT64 *)(d + 16));
    h   = *(const UINT *)(d + 24);
    fmt = *(const UINT *)(d + 32);
    if (w >= 640 && h >= 360 && shape_is_new(12, w, h, fmt))
        logf_("  D3D12 texture %ux%u format=%u via %s -> 0x%08lX%s",
              w, h, fmt, how, (unsigned long)hr, dxgi_format_note(fmt));
}

static HRESULT WINAPI my_create_committed(void *self, const void *heap, UINT heap_flags,
        const void *desc, UINT state, const void *clear, const GUID *iid, void **out)
{
    /* D3D12_RESOURCE_DESC: Width is a UINT64 at +16, Height a UINT at +24,
     * Format a UINT at +32. Offsets taken from the header, as this project's
     * own rule requires, not guessed from a struct that looks right. */
    HRESULT hr = real_create_committed(self, heap, heap_flags, desc, state, clear, iid, out);
    note_resource_shape("committed", desc, hr);
    return hr;
}

/* Slot 29. A modern renderer reserves a few large heaps and places resources
 * inside them, so almost nothing goes through CreateCommittedResource and a
 * census that watches only slot 27 reports no textures at all in a game
 * drawing at 2048x1152 -- which is what happened. */
static HRESULT WINAPI my_create_placed(void *self, void *heap, UINT64 offset,
        const void *desc, UINT state, const void *clear, const GUID *iid, void **out)
{
    HRESULT hr = real_create_placed(self, heap, offset, desc, state, clear, iid, out);
    note_resource_shape("placed", desc, hr);
    return hr;
}

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT level,
                                           const GUID *iid, void **device)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, level, iid, device);
    logf_("D3D12CreateDevice(featureLevel=0x%lX) -> 0x%08lX", (unsigned long)level,
          (unsigned long)hr);
    if (SUCCEEDED(hr) && device && *device && watch_d3d12)
    {
        /* Arm every device, keyed on the vtable rather than the device.
         *
         * The once-only guard this replaces would have watched device one and
         * silently ignored a second -- and dwo-video-bridge and ng4-observe
         * both still carry that shape. The four negatives banked for RONIN all
         * rest on having watched the right device, and ng4 records exactly how
         * that fails: a hook that stopped being installed produced "the game
         * does not activate" three runs running, when what had happened is
         * that nobody was watching. An absence in a log is only evidence if
         * the thing that writes it was running.
         *
         * The vtable is the key because it is shared between devices: patching
         * one twice makes our own function the original it saves, and the next
         * call recurses until the stack is gone. */
        static void *armed[8];
        static LONG armed_n;
        void *vt = *(void **)*device;
        LONG i, n = armed_n;
        BOOL already = FALSE;

        for (i = 0; i < n && i < 8; i++) if (armed[i] == vt) already = TRUE;
        if (!already && n < 8)
        {
            /* One saved pointer per slot, and never one shared between two.
             *
             * patch_slot opens with `if (*saved) return TRUE;` so that a shared
             * vtable is patched once. Passing the same variable to two slots
             * therefore made the second call report success without patching
             * anything -- and hand back the FIRST slot's original as though it
             * were the second's. The census silently never installed, and
             * real_create_committed pointed at QueryInterface; had anything
             * called it the game would have died on the spot. */
            static void *was_qi, *was_cc, *was_cp;
            armed[n] = vt; armed_n = n + 1;
            if (patch_slot("d3d12 QueryInterface", *device, 0,
                           (void *)my_dev12_QueryInterface, &was_qi))
                real_dev12_QueryInterface =
                    (HRESULT (WINAPI *)(void *, const GUID *, void **))was_qi;
            if (patch_slot("d3d12 CreateCommittedResource", *device, 27,
                           (void *)my_create_committed, &was_cc))
                real_create_committed =
                    (HRESULT (WINAPI *)(void *, const void *, UINT, const void *,
                                        UINT, const void *, const GUID *, void **))was_cc;
            if (patch_slot("d3d12 CreatePlacedResource", *device, 29,
                           (void *)my_create_placed, &was_cp))
                real_create_placed =
                    (HRESULT (WINAPI *)(void *, void *, UINT64, const void *,
                                        UINT, const void *, const GUID *, void **))was_cp;
            logf_("watching D3D12 device %ld", (long)(n + 1));
        }
    }
    return hr;
}

/* The door before all the others.
 *
 * Four Media Foundation entry points were watched here and all four stayed at
 * zero for RISE OF THE RONIN, which was read as "this game has no Media
 * Foundation video". It has. GloriousEggroll's proton-ge-custom issue 165,
 * filed for this exact title, reports the symptom word for word -- "wait for
 * shaders to compile. It will then play the intro video with audio and a black
 * screen" -- and names the format: WebM.
 *
 * MFCreateFile is what opens the byte stream a source reader is then built on,
 * and it sits ahead of every entry point this probe was watching. A failure
 * here is silent downstream: no source, so no topology, so no MFTEnumEx, so
 * four zeroes that are all true and none of which is the answer. It is in this
 * game's import table and was never hooked.
 *
 * Not capped: a title opens a handful of these per cutscene, and this is the
 * line the whole search is for. */
static HRESULT (WINAPI *real_MFCreateFile)(DWORD, DWORD, DWORD, LPCWSTR, void **);

static HRESULT WINAPI my_MFCreateFile(DWORD access, DWORD open, DWORD flags,
                                      LPCWSTR path, void **stream)
{
    HRESULT hr = real_MFCreateFile(access, open, flags, path, stream);
    CHAR narrow[MAX_PATH];
    /* note_name is defined further down; this is the same two lines inline. */
    narrow[0] = 0;
    if (path) WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, MAX_PATH, NULL, NULL);
    narrow[MAX_PATH - 1] = 0;
    logf_("MFCreateFile(\"%s\") -> 0x%08lX  %s", narrow, (unsigned long)hr,
          SUCCEEDED(hr) ? "<< the byte stream the video is read from"
                        : "<< FAILED HERE, and everything downstream goes quiet");
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
    SWAP(MFCreateFile)
    SWAP(D3D12CreateDevice)
#undef SWAP

    /* Name every Media Foundation entry the title resolves, hooked or not.
     *
     * RESONANCE opens its LOGO.mp4 itself and calls MFStartup, and then the log
     * went quiet: no enumeration, no reader, no sample. Five swapped entries is
     * a guess about which door a game uses, and this one uses a different one.
     * Listing what it asks for costs one line each and turns "we saw nothing"
     * into "it asked for these six things", which is a different sentence. */
    if ((name[0] == 'M' || name[0] == 'm') && (name[1] == 'F' || name[1] == 'f'))
    {
        static LONG named;
        if (InterlockedIncrement(&named) <= 40)
            logf_("  asks for: %s (not hooked)", name);
    }

    /* Name every media entry point the game asks for, resolved or not. The
     * list of what it looks for is itself evidence about which player it uses. */
    if (name[0] == 'M' && name[1] == 'F')
        logf_("GetProcAddress(\"%s\") -> %s", name, proc ? "ok" : "NOT FOUND");
    return proc;
}

/* Patch an import in a named module, not only in the executable.
 *
 * The executable's table is the right place when the executable is the caller.
 * RISE OF THE RONIN never calls D3D12CreateDevice itself: sl.interposer.dll
 * does, and that DLL imports neither d3d12 nor anything else useful -- it
 * carries LoadLibrary and GetProcAddress and resolves the world by hand. A
 * hook on the executable's table watched a road nobody drives on, and reported
 * no traffic, which is the third time in this file that a zero has been read
 * as an answer when it was a blind spot. */
static void *hook_import_mod(HMODULE base, const char *dll, const char *func,
                             void *replacement)
{
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

/* The executable's own table, which is what almost every hook wants. */
static void *hook_import(const char *dll, const char *func, void *replacement)
{
    return hook_import_mod(GetModuleHandleA(NULL), dll, func, replacement);
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
            if (d.fmt == 0x3231564E)      /* NV12 */
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

/* Ported from the Nioh/P5S bridge, 27 Aug, for METAL GEAR SOLID 4.
 *
 * That title fails to a black screen with an audio click and writes nothing:
 * it imports no Media Foundation and no D3D at all, so every hook this probe
 * installs watches a road it never drives on. What it does import is
 * bink2w64 -- Bink carries both picture and sound here, through XAudio2 --
 * and dbghelp, so it has a crash handler of its own that runs before anything
 * we could see.
 *
 * These two together are what a silent death leaves us: where it faulted, and
 * whatever the title said on its way out. */
/* Where the title actually dies, when it dies past our instrumentation.
 *
 * Nioh reaches the six D3D9 patches and then goes, without calling one of
 * them -- so the bridge, which is the fix, never runs and the log ends at a
 * patch line. Measured 27 Aug: the same last line whether the two levers are
 * on or off, because neither fires this early.
 *
 * A log that stops is not a log that says nothing happened. This is the
 * instrument that broke the same deadlock on NINJA GAIDEN 4 after three
 * probes found nothing: catch the exception inside the process we are already
 * in, rather than hunting for a crash report the launcher never writes.
 *
 * First-chance and non-intrusive -- it always continues the search, so it
 * changes no behaviour and only writes what would have happened anyway. */
/* Which media libraries are in the process, asked at a chosen instant.
 *
 * The 5 s tick reports these as they appear, which answers "did it load" but
 * not "had it loaded when the fault happened" -- a tick says only that the
 * module was there by that tick. In two Peace Walker runs the first access
 * violation and the winegstreamer load fell inside the same tick, and the order
 * between them decides whether the faulting buffer could have come from the
 * media source at all. Reading an order out of the tick would be reading the
 * instrument, not the game, so the fault handler asks again at the fault. */
static const char *const media_modules[] = {
    "mfplat.dll", "mfreadwrite.dll", "mf.dll", "mfmediaengine.dll",
    "winegstreamer.dll", "quartz.dll", "msdmo.dll", "dxva2.dll",
    "evr.dll", "mfsrcsnk.dll", "mfmp4srcsnk.dll", "d3d11.dll", "d3d12.dll"
};

static void log_media_modules(const char *why)
{
    char line[600];
    unsigned m;

    line[0] = 0;
    for (m = 0; m < sizeof(media_modules) / sizeof(media_modules[0]); m++)
    {
        if (!GetModuleHandleA(media_modules[m])) continue;
        if (line[0]) lstrcatA(line, " ");
        lstrcatA(line, media_modules[m]);
    }
    logf_("    media modules %s: %s", why, line[0] ? line : "(none)");
}


static LONG CALLBACK note_exception(EXCEPTION_POINTERS *info)
{
    static LONG said;
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void *at = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : NULL;

    /* 0xe06d7363 is a C++ throw, 0x406d1388 a thread-name notification, and
     * both are ordinary traffic in a running game. */
    if (code == 0xE06D7363 || code == 0x406D1388 || code == EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

    /* OutputDebugString, which is the title talking.
     *
     * 0x40010006 and 0x4001000a are how OutputDebugStringA and W reach a
     * debugger: not faults, but the message itself, carried in the exception
     * record. The first version of this handler logged that an exception had
     * happened and threw the text away -- which is how six of these sat in the
     * log looking like noise while the game was saying, in words, what was
     * wrong with it. Nioh's documented symptom is a message box reading
     * "Failure to play movie. (RTM_ID_EV0001)"; if it says that, it says it
     * here.
     *
     * Counted apart from the fault budget below: these are the interesting
     * ones, and eight faults' worth of room is not enough for a title that
     * narrates. */
    if ((code == 0x40010006 || code == 0x4001000A)
        && info->ExceptionRecord->NumberParameters >= 2
        && info->ExceptionRecord->ExceptionInformation[1])
    {
        static LONG printed;
        if (InterlockedIncrement(&printed) <= 120)
        {
            const void *p = (const void *)info->ExceptionRecord->ExceptionInformation[1];
            char msg[512];
            msg[0] = 0;
            if (code == 0x40010006)
            {
                size_t n = 0;
                const char *a = (const char *)p;
                while (n < sizeof(msg) - 1 && a[n]) { msg[n] = a[n]; n++; }
                msg[n] = 0;
            }
            else
            {
                const WCHAR *w = (const WCHAR *)p;
                WideCharToMultiByte(CP_UTF8, 0, w, -1, msg, sizeof(msg) - 1, NULL, NULL);
            }
            /* Trim the trailing newline these usually carry. */
            {
                size_t n = strlen(msg);
                while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
            }
            if (msg[0]) logf_("game says: %s", msg);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (InterlockedIncrement(&said) <= 8)
    {
        char mod[MAX_PATH] = "";
        HMODULE h = NULL;
        if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                     | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)at, &h) && h)
        {
            GetModuleFileNameA(h, mod, sizeof(mod) - 1);
            logf_("EXCEPTION 0x%08lx at %p  in %s  (+0x%lx)", code, at,
                  mod, (unsigned long)((BYTE *)at - (BYTE *)h));
        }
        else
            logf_("EXCEPTION 0x%08lx at %p  in no known module", code, at);

        if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
            logf_("    access violation %s address %p",
                  info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                  (void *)info->ExceptionRecord->ExceptionInformation[1]);

        /* Asked here, not at a tick: this is the only moment that answers
         * whether a media source existed when the pointer was dereferenced. */
        log_media_modules("at this fault");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}


/* Startup trace, for a title that hangs before it reaches any media API.
 *
 * METAL GEAR SOLID 4 stops on a black screen after its launcher hands over,
 * with every thread parked, 3% of a core, and no .bk2 open -- so it never gets
 * near Bink, and every hook above watches a road it does not drive on. A hang
 * leaves no exception and no last log line to read backwards from: what it
 * leaves is whatever it touched last before it stopped touching anything.
 *
 * Off unless C:\mgvf-trace.txt exists, because this is noise in any title that
 * is not being chased, and this probe is shared.
 */
static BOOL trace_startup;
/* Refuse the vendor library this machine does not have.
 *
 * METAL GEAR SOLID 4 asks for amd_ags_x64, GFSDK_Aftermath and dxgidebug and
 * is told no -- correctly, none of them is here. Then it asks for nvapi64 and
 * the engine says yes, because it ships one to translate DLSS. So the title
 * learns it is running on an NVIDIA card that is not there, and what it does
 * with that is its own business until something in that path has no answer.
 *
 * Refusing the load is what a machine without the card would have done, and
 * it is one line rather than a bottle-wide DLL override -- which would tell
 * every other title the same thing, and several of them want DLSS. Asked for
 * by name: put 'nonvapi' in C:\mgvf-trace.txt. */
static BOOL refuse_nvapi;
/* Give this engine its own GStreamer registry cache.
 *
 * Two engines on this machine share one cache file, and the cache stores
 * absolute plugin paths. Stable carries 18 plugins and the fork 21, so every
 * alternation between them invalidates it and forces a full rescan -- which
 * happens inside winegstreamer's initialisation, reached here through
 * xaudio2_9 -> mfplat, and which is exactly where METAL GEAR SOLID 4 stops.
 *
 * Set from in here because the launcher overrides its own per-game field:
 * asked for GST_REGISTRY in RaccoonBot's Env variables and the process still
 * received RaccoonBot's value. We load before mfplat does, so we can win.
 *
 * Asked for by name: put 'ownreg' in C:\mgvf-trace.txt. */
static BOOL own_registry;
static CHAR last_file[MAX_PATH];
static CHAR last_lib[MAX_PATH];
static LONG waits_in, waits_out, files_opened, libs_loaded;
/* Entered-and-returned pairs, so a call that never comes back is visible.
 * A counter that only counts entries cannot tell 'the last thing it did'
 * from 'the thing it is still doing', and those are different bugs. */
static LONG opens_in, opens_out, reads_in, reads_out, reads_empty, read_bytes;
/* The other ways a thread can stop.
 *
 * Measured 27 Aug on METAL GEAR SOLID 4: all file I/O complete, and the count
 * of outstanding infinite WaitForSingleObject calls identical whether the
 * title hangs early or late -- so those twenty are its idle worker pool and
 * the stuck thread is blocked on something else entirely. These are what is
 * left: the Ex variants, condition variables, and WaitOnAddress. Critical
 * sections are deliberately not hooked -- they are far too hot to wrap, and a
 * probe that halves the frame rate changes the race it is trying to watch. */
static LONG waitex_in, waitex_out, cond_in, cond_out, addr_in, addr_out;
/* Waits with a deadline, and sleeps, counted separately from the infinite ones.
 *
 * A thread parked forever and a thread polling forever look the same from
 * outside -- both are "stopped" and both burn almost no CPU -- and only one of
 * them is a deadlock. Measured on METAL GEAR SOLID 4: no infinite wait, no
 * condition variable, no WaitOnAddress, no I/O in flight, and still nothing
 * moves. If these climb while the file counters do not, it is spinning on a
 * condition that never becomes true, which is a different bug with a different
 * fix. */
static LONG polls_wait, polls_sleep;
static void (WINAPI *real_Sleep)(DWORD);
static DWORD (WINAPI *real_WaitForSingleObjectEx)(HANDLE, DWORD, BOOL);
static BOOL (WINAPI *real_SleepConditionVariableSRW)(void *, void *, DWORD, ULONG);
static BOOL (WINAPI *real_SleepConditionVariableCS)(void *, void *, DWORD);
static BOOL (WINAPI *real_WaitOnAddress)(volatile void *, void *, SIZE_T, DWORD);
static LONG last_read_bytes;
static BOOL (WINAPI *real_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
static HRESULT (WINAPI *real_CoCreateInstance)(const GUID *, void *, DWORD, const GUID *, void **);

static HANDLE (WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
static HMODULE (WINAPI *real_LoadLibraryW)(LPCWSTR);
static HMODULE (WINAPI *real_LoadLibraryA)(LPCSTR);
static HMODULE (WINAPI *real_LoadLibraryExA)(LPCSTR, HANDLE, DWORD);
static HMODULE (WINAPI *real_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
static DWORD (WINAPI *real_WaitForSingleObject)(HANDLE, DWORD);
static DWORD (WINAPI *real_WaitForMultipleObjects)(DWORD, const HANDLE *, BOOL, DWORD);

static void note_name(CHAR *slot, LPCWSTR w)
{
    CHAR tmp[MAX_PATH];
    if (!w) return;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, tmp, sizeof(tmp) - 1, NULL, NULL)) return;
    tmp[MAX_PATH - 1] = 0;
    lstrcpynA(slot, tmp, MAX_PATH);
}

/* Which file the bytes are actually coming from.
 *
 * The read counter says how much is being read and never says from where, and
 * that gap cost a wrong conclusion on RISE OF THE RONIN: a steady 287 KB/s
 * during a black screen was written off as "the idle rate" because the menu
 * showed the same figure -- when two and a bit megabits a second is also
 * exactly what a video stream looks like, and the menu has an animated
 * background. A rate with no name attached cannot settle that.
 *
 * Handles are the key because a game that streams from a packed archive opens
 * it once and then reads for minutes without opening anything again, so the
 * open counter goes quiet while the interesting traffic continues. Slots are
 * overwritten when a handle value comes back from the OS, which is what makes
 * reuse safe without hooking CloseHandle. */
/* Big enough that a burst cannot flush it.
 *
 * 96 slots was sized for a game that opens a few dozen files. RONIN opens 805
 * in a single fifteen-second window and fourteen thousand over a session, so a
 * 96-slot table is emptied eight times over before the screen even goes black,
 * and anything opened beforehand is guaranteed to be gone by the time it
 * matters. No eviction rule fixes that -- three were tried and the first two
 * reported silence they had made. At 80 bytes a slot this costs 80 KB and the
 * scan stays linear over something a video stream keeps warm at the front. */
#define HOTFILES 1024
static struct { HANDLE h; CHAR name[64]; LONG bytes; LONG seen; } hotfile[HOTFILES];
static LONG hotfile_next;
/* Ticks up on every read, so "least recently read" is answerable. */
static LONG hotfile_clock;

static void note_open_handle(HANDLE h, const char *full)
{
    const char *tail;
    LONG i, slot;
    if (h == INVALID_HANDLE_VALUE || !full || !full[0]) return;

    /* The tail is what identifies it: these paths are long and the leaf is the
     * only part that differs. */
    tail = full;
    {
        const char *p;
        for (p = full; *p; p++)
            if (*p == '\\' || *p == '/') tail = p + 1;
    }

    /* Take the same handle's slot if it has one, then any idle slot, and only
     * then recycle.
     *
     * A plain round robin over 96 slots was worse than useless here: RONIN
     * opens fourteen thousand files, so a slot is recycled every few seconds
     * and the archive it streams from -- opened early, read for minutes -- was
     * evicted while still being read. The heartbeat then printed "reading
     * from: nothing" for three minutes straight and it was read as the game
     * having gone quiet. It had not. An instrument that recycles its own
     * memory faster than the thing it watches reports silence it manufactured.
     *
     * Idle means no bytes since the last report, and report_hot_files zeroes
     * as it reads, so a file being streamed is never idle when this runs. */
    /* Same handle, then an empty slot, then the one read longest ago.
     *
     * Two eviction rules have already been wrong here and both reported a
     * silence they created. Round robin recycled a slot every few seconds
     * against fourteen thousand opens, so the archive being streamed was
     * evicted mid-stream. Preferring slots with zero bytes looked like the fix
     * and was not: report_hot_files zeroes every counter as it reads, so a
     * moment after each report every slot is "idle" and the file being read is
     * as evictable as anything else. Only a timestamp survives the report,
     * which is why there is a separate clock. */
    for (i = 0; i < HOTFILES; i++)
        if (hotfile[i].h == h) { slot = i; goto fill; }
    for (i = 0; i < HOTFILES; i++)
        if (hotfile[i].h == NULL) { slot = i; goto fill; }
    {
        LONG oldest = 0;
        slot = 0;
        for (i = 0; i < HOTFILES; i++)
            if (i == 0 || hotfile[i].seen < oldest) { oldest = hotfile[i].seen; slot = i; }
    }
fill:
    if (hotfile[slot].h != h) hotfile[slot].bytes = 0;
    hotfile[slot].h = h;
    hotfile[slot].seen = InterlockedIncrement(&hotfile_clock);
    lstrcpynA(hotfile[slot].name, tail, sizeof(hotfile[0].name));
}

static void note_read_bytes(HANDLE h, DWORD n)
{
    LONG i;
    for (i = 0; i < HOTFILES; i++)
        if (hotfile[i].h == h)
        {
            InterlockedExchangeAdd(&hotfile[i].bytes, (LONG)n);
            hotfile[i].seen = InterlockedIncrement(&hotfile_clock);
            return;
        }
}

/* The three busiest files since the last call, and it resets as it reads so
 * every report covers only its own window. */
static void report_hot_files(void)
{
    LONG best[3] = {0, 0, 0};
    int  who[3]  = {-1, -1, -1};
    LONG i;
    int  k, j;

    for (i = 0; i < HOTFILES; i++)
    {
        LONG b = InterlockedExchange(&hotfile[i].bytes, 0);
        if (b <= 0) continue;
        for (k = 0; k < 3; k++)
            if (b > best[k])
            {
                for (j = 2; j > k; j--) { best[j] = best[j-1]; who[j] = who[j-1]; }
                best[k] = b; who[k] = (int)i;
                break;
            }
    }
    if (who[0] < 0)
    {
        logf_("      reading from: nothing");
        return;
    }
    logf_("      reading from: %s %ld KB%s%s%s%s%s",
          hotfile[who[0]].name, (long)(best[0] / 1024),
          who[1] >= 0 ? " | " : "", who[1] >= 0 ? hotfile[who[1]].name : "",
          who[1] >= 0 ? " " : "",
          who[2] >= 0 ? "| " : "", who[2] >= 0 ? hotfile[who[2]].name : "");
}

/* The ANSI half of the file opens, which is not a formality here.
 *
 * The handle-to-name table was wired into CreateFileW only, so any file the
 * game opens through the ANSI entry contributes reads that belong to nobody.
 * RONIN's black screen reads six megabytes every fifteen seconds -- a video
 * bitrate, and none of the reads empty -- while the heartbeat says "reading
 * from: nothing", and an unhooked open is the obvious way for both to be true
 * at once. This file already carries the same lesson about LoadLibrary, learned
 * on METAL GEAR SOLID 4: hooking only the wide entry watches half a road. */
static HANDLE (WINAPI *real_CreateFileA)(LPCSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);

static HANDLE WINAPI my_CreateFileA(LPCSTR name, DWORD access, DWORD share, void *sa,
                                    DWORD disp, DWORD flags, HANDLE tmpl)
{
    HANDLE h;
    if (name) lstrcpynA(last_file, name, MAX_PATH);
    InterlockedIncrement(&files_opened);
    InterlockedIncrement(&opens_in);
    h = real_CreateFileA(name, access, share, sa, disp, flags, tmpl);
    InterlockedIncrement(&opens_out);
    if (name) note_open_handle(h, name);
    return h;
}

static HANDLE WINAPI my_CreateFileW(LPCWSTR name, DWORD access, DWORD share, void *sa,
                                    DWORD disp, DWORD flags, HANDLE tmpl)
{
    note_name(last_file, name);
    InterlockedIncrement(&files_opened);
    /* Name the video files as they are opened.
     *
     * Cheap because there are eighteen of them and hundreds of everything
     * else, and it answers the one question the file counter cannot: whether
     * the title got as far as its first cutscene before it stopped. */
    {
        /* Any container a title might hand to a player, not just the one the
         * last game used. Written for .bk2 while chasing METAL GEAR SOLID 4 and
         * then carried, unchanged, to a title whose videos are .mp4 -- so the
         * log said no video was ever opened while two of them sat in
         * VIDEOS/NTSC waiting to be read. A list that only covers the last
         * game is a blind spot with a good alibi. */
        static const char *const exts[] =
            { ".bk2", ".bik", ".mp4", ".m4v", ".mov", ".webm", ".mkv", ".usm",
              ".wmv", ".avi", ".asf", ".msd", ".ogv" };
        size_t n = lstrlenA(last_file), i;
        for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        {
            size_t e = lstrlenA(exts[i]);
            if (n > e && !lstrcmpiA(last_file + n - e, exts[i]))
            {
                logf_("  opens video: %s", last_file);
                break;
            }
        }
    }
    {
        HANDLE h;
        InterlockedIncrement(&opens_in);
        h = real_CreateFileW(name, access, share, sa, disp, flags, tmpl);
        InterlockedIncrement(&opens_out);
        note_open_handle(h, last_file);
        return h;
    }
}

/* The ANSI half, which is not a formality.
 *
 * Measured on METAL GEAR SOLID 4: with only the wide entries hooked the log
 * reported no library loaded at all, in a process that was rendering at 56
 * frames a second -- so it loads its graphics stack through the ANSI names,
 * and the blind spot read as "it never loads anything". Every library is
 * named as it arrives now: which one comes last before a hang is worth more
 * than a count. */
/* Case-insensitive "does this name contain nvapi", without shlwapi.
 *
 * The first version compared p[0] through p[4] while only checking p[0] for
 * the terminator, so any name shorter than five characters was read past its
 * end -- and the title stopped launching at all. Written by me, found by it
 * refusing to start. Bounded properly now: the length is measured once and
 * the scan stops five characters before the end. */
static BOOL name_has_nvapi(LPCSTR s)
{
    int n = lstrlenA(s), i;
    for (i = 0; i + 5 <= n; i++)
    {
        if ((s[i] | 32) == 'n' && (s[i+1] | 32) == 'v' && (s[i+2] | 32) == 'a'
            && (s[i+3] | 32) == 'p' && (s[i+4] | 32) == 'i')
            return TRUE;
    }
    return FALSE;
}

/* Arm Streamline the moment it appears, not on the next watchdog tick.
 *
 * The retry loop found sl.interposer.dll at five seconds and the DLL had
 * loaded at 1.3 -- it resolves what it needs immediately, so by the time the
 * hook landed the D3D12 device already existed and every QueryInterface on it
 * had gone past unseen. A five-second poll cannot win a race decided in the
 * first second. LoadLibrary is already hooked here, so this rides in on the
 * one event that is exactly on time. */
static void arm_streamline_if_it_is(const char *libname)
{
    /* Disabled, and kept as a record of a wrong turn.
     *
     * Two rounds went into hooking inside Streamline, on the theory that it
     * resolved D3D12CreateDevice by hand. It does not need to: it EXPORTS the
     * whole D3D12 and DXGI surface, and the executable imports the function
     * from sl.interposer.dll by name. The exe's own table was right all along.
     *
     * Left off rather than deleted because it also confounded a control. The
     * 'd3d12' switch turned on three things at once -- this, a hook in
     * sl.common.dll, and the device vtable patch -- so when Ronin's intro
     * videos came back with the switch off, which of the three had been
     * costing them was not established. One switch, one act. */
    (void)libname;
}

static HMODULE WINAPI my_LoadLibraryA(LPCSTR name)
{
    HMODULE h;
    if (name) { lstrcpynA(last_lib, name, MAX_PATH); InterlockedIncrement(&libs_loaded); }
    if (refuse_nvapi && name && name_has_nvapi(name))
    {
        logf_("  REFUSED (asked for): %s -- as a machine with no NVIDIA card would", name);
        SetLastError(ERROR_MOD_NOT_FOUND);
        return NULL;
    }
    if (trace_startup && name) logf_("  loads: %s", name);
    h = real_LoadLibraryA(name);
    if (h) arm_streamline_if_it_is(name);
    if (!h) logf_("  LoadLibraryA FAILED: %s (err %lu)", name ? name : "(null)", GetLastError());
    return h;
}

static HMODULE WINAPI my_LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags)
{
    HMODULE h;
    if (name) { lstrcpynA(last_lib, name, MAX_PATH); InterlockedIncrement(&libs_loaded); }
    if (trace_startup && name) logf_("  loads: %s", name);
    h = real_LoadLibraryExA(name, f, flags);
    if (h) arm_streamline_if_it_is(name);
    if (!h) logf_("  LoadLibraryExA FAILED: %s (err %lu)", name ? name : "(null)", GetLastError());
    return h;
}

static HMODULE WINAPI my_LoadLibraryW(LPCWSTR name)
{
    HMODULE h;
    note_name(last_lib, name);
    InterlockedIncrement(&libs_loaded);
    if (trace_startup) logf_("  loads: %s", last_lib);
    h = real_LoadLibraryW(name);
    if (h) arm_streamline_if_it_is(last_lib);
    if (!h) logf_("  LoadLibrary FAILED: %s (err %lu)", last_lib, GetLastError());
    return h;
}

static HMODULE WINAPI my_LoadLibraryExW(LPCWSTR name, HANDLE f, DWORD flags)
{
    HMODULE h;
    note_name(last_lib, name);
    InterlockedIncrement(&libs_loaded);
    h = real_LoadLibraryExW(name, f, flags);
    if (h) arm_streamline_if_it_is(last_lib);
    if (!h) logf_("  LoadLibraryEx FAILED: %s (err %lu)", last_lib, GetLastError());
    return h;
}

static DWORD WINAPI my_WaitForSingleObject(HANDLE h, DWORD ms)
{
    DWORD r;
    if (ms == INFINITE) InterlockedIncrement(&waits_in);
    else InterlockedIncrement(&polls_wait);
    r = real_WaitForSingleObject(h, ms);
    if (ms == INFINITE) InterlockedIncrement(&waits_out);
    return r;
}

static DWORD WINAPI my_WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{
    DWORD r;
    if (ms == INFINITE) InterlockedIncrement(&waits_in);
    else InterlockedIncrement(&polls_wait);
    r = real_WaitForMultipleObjects(n, h, all, ms);
    if (ms == INFINITE) InterlockedIncrement(&waits_out);
    return r;
}

static void WINAPI my_Sleep(DWORD ms)
{
    InterlockedIncrement(&polls_sleep);
    real_Sleep(ms);
}

static DWORD WINAPI my_WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alert)
{
    DWORD r;
    if (ms == INFINITE) InterlockedIncrement(&waitex_in);
    r = real_WaitForSingleObjectEx(h, ms, alert);
    if (ms == INFINITE) InterlockedIncrement(&waitex_out);
    return r;
}

static BOOL WINAPI my_SleepConditionVariableSRW(void *cv, void *lock, DWORD ms, ULONG flags)
{
    BOOL r;
    if (ms == INFINITE) InterlockedIncrement(&cond_in);
    r = real_SleepConditionVariableSRW(cv, lock, ms, flags);
    if (ms == INFINITE) InterlockedIncrement(&cond_out);
    return r;
}

static BOOL WINAPI my_SleepConditionVariableCS(void *cv, void *cs, DWORD ms)
{
    BOOL r;
    if (ms == INFINITE) InterlockedIncrement(&cond_in);
    r = real_SleepConditionVariableCS(cv, cs, ms);
    if (ms == INFINITE) InterlockedIncrement(&cond_out);
    return r;
}

static BOOL WINAPI my_WaitOnAddress(volatile void *a, void *cmp, SIZE_T sz, DWORD ms)
{
    BOOL r;
    if (ms == INFINITE) InterlockedIncrement(&addr_in);
    r = real_WaitOnAddress(a, cmp, sz, ms);
    if (ms == INFINITE) InterlockedIncrement(&addr_out);
    return r;
}

static BOOL WINAPI my_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPVOID ov)
{
    BOOL r;
    InterlockedExchange(&last_read_bytes, (LONG)n);
    InterlockedIncrement(&reads_in);
    r = real_ReadFile(h, buf, n, got, ov);
    InterlockedIncrement(&reads_out);
    if (r)
    {
        DWORD n_got = got ? *got : n;
        /* A read that returns nothing is not the same as no read. During
         * RONIN's black screen the heartbeat showed two thousand reads a
         * window and no bytes attributed to any file, which read as an
         * accounting hole; counting the empty ones separately says instead
         * that the game is polling something that has no data for it. */
        if (n_got == 0) InterlockedIncrement(&reads_empty);
        else InterlockedExchangeAdd(&read_bytes, (LONG)(n_got / 1024));
        note_read_bytes(h, n_got);
    }
    return r;
}

/* Every COM object the title asks for, by CLSID.
 *
 * RESONANCE plays its MP4s through IMFMediaEngine, created with
 * CoCreateInstance -- so MFCreateSourceReader is never called, MFTEnumEx is
 * never called, and a probe watching those two saw a title with no video at
 * all while mfmediaengine, mfmp4srcsnk and winegstreamer sat loaded in its
 * address space. The door was open; we were watching a different one.
 *
 * Named rather than hooked: what is created says which path the video takes,
 * and that has to be known before anything is worth patching. */
/* The Media Engine, and the two calls that hand a frame to the game.
 *
 * RESONANCE plays its MP4s through IMFMediaEngine, confirmed by the bottle's
 * own registry: {B44392DA-499B-446B-A4CB-005FEAD0E6D5} is "Media Engine Class
 * Factory", served by mfmediaengine.dll, and the title creates it and gets
 * S_OK. Underneath sit mfmp4srcsnk for the container and winegstreamer for the
 * H.264 -- both measured present in the process.
 *
 * A title playing video this way does not read samples. It asks the engine, on
 * each frame, whether a new one is ready (OnVideoStreamTick) and then to copy
 * it into a texture of its own (TransferVideoFrame). Sound with no picture is
 * exactly what those two failing looks like from outside, and neither appears
 * in any log we had, because they are vtable calls on an object obtained
 * through COM.
 *
 * IMFMediaEngineClassFactory slot 3 is CreateInstance. On the engine it hands
 * back, slot 43 is TransferVideoFrame and 44 OnVideoStreamTick -- IUnknown's
 * three, then forty of IMFMediaEngine's own, in the order the interface
 * declares them.
 *
 * Watching only: what these return decides whether there is anything to fix
 * and where, and guessing that before measuring it is how this afternoon went. */
/* Paint the frame's surroundings a colour this game does not contain.
 *
 * The same trick as ue5-media-fix's magenta switch, adapted: there the frame
 * arrived in system memory and could be overwritten, here it lands in a GPU
 * texture we do not own. But TransferVideoFrame takes a destination rectangle
 * and a border colour, and those are enough to ask the question.
 *
 * With this on, the video is drawn into the middle sixty percent of the
 * destination and everything around it is filled opaque magenta. Then:
 *
 *   - magenta with a small picture inside  -> the surface reaches the screen,
 *     and whatever is wrong is in how the game composites it afterwards.
 *   - magenta and nothing else             -> the surface is presented but the
 *     frame is not landing in it.
 *   - nothing at all                       -> the surface never reaches the
 *     screen, and the video is a bystander to the same fault as the rest.
 *
 * Three answers, and the difference between them is not something the return
 * codes can tell us: TransferVideoFrame has returned S_OK a hundred and twenty
 * times while the screen stayed black.
 *
 * Asked for by name: 'magenta' in C:\\mgvf-trace.txt. */
static BOOL paint_the_border;

/* MFARGB is blue, green, red, alpha -- in that order. */
static const BYTE MAGENTA[4] = { 0xFF, 0x00, 0xFF, 0xFF };

static HRESULT (WINAPI *real_transfer)(void *, void *, RECT *, RECT *, void *);
static HRESULT (WINAPI *real_streamtick)(void *, LONGLONG *);
static HRESULT (WINAPI *real_factory_create)(void *, DWORD, void *, void **);

static HRESULT WINAPI my_transfer(void *self, void *surf, RECT *src, RECT *dst, void *clr)
{
    RECT inset;
    HRESULT hr;

    if (paint_the_border && dst)
    {
        LONG w = dst->right - dst->left, h = dst->bottom - dst->top;
        inset.left   = dst->left + w / 5;
        inset.top    = dst->top  + h / 5;
        inset.right  = dst->right  - w / 5;
        inset.bottom = dst->bottom - h / 5;
        dst = &inset;
        clr = (void *)MAGENTA;
        {
            static LONG once;
            if (InterlockedIncrement(&once) == 1)
                logf_("magenta: video into the middle of %ldx%ld, the rest filled",
                      (long)w, (long)h);
        }
    }
    hr = real_transfer(self, surf, src, dst, clr);
    static LONG said, failed;
    if (FAILED(hr))
    {
        if (InterlockedIncrement(&failed) <= 4)
            logf_("TransferVideoFrame -> 0x%08lX  << the frame never reaches the game",
                  (unsigned long)hr);
    }
    else if (InterlockedIncrement(&said) <= 3 || (said % 120) == 0)
        logf_("TransferVideoFrame -> ok (%ld so far)%s", (long)said,
              dst ? "" : " -- with no destination rectangle");
    return hr;
}

static HRESULT WINAPI my_streamtick(void *self, LONGLONG *pts)
{
    HRESULT hr = real_streamtick(self, pts);
    static LONG asked, ready;
    InterlockedIncrement(&asked);
    if (hr == S_OK) InterlockedIncrement(&ready);
    if (asked == 1 || (asked % 240) == 0)
        logf_("OnVideoStreamTick: asked %ld times, a frame was ready %ld of them "
              "(last 0x%08lX)", (long)asked, (long)ready, (unsigned long)hr);
    return hr;
}

static HRESULT WINAPI my_factory_create(void *self, DWORD flags, void *attrs, void **engine)
{
    HRESULT hr = real_factory_create(self, flags, attrs, engine);
    logf_("MediaEngineClassFactory::CreateInstance(flags 0x%lX) -> 0x%08lX",
          (unsigned long)flags, (unsigned long)hr);
    if (SUCCEEDED(hr) && engine && *engine)
    {
        void *was = NULL;
        if (!real_transfer && patch_slot("TransferVideoFrame", *engine, 43,
                                         (void *)my_transfer, &was) && was)
            real_transfer = (HRESULT (WINAPI *)(void *, void *, RECT *, RECT *, void *))was;
        was = NULL;
        if (!real_streamtick && patch_slot("OnVideoStreamTick", *engine, 44,
                                           (void *)my_streamtick, &was) && was)
            real_streamtick = (HRESULT (WINAPI *)(void *, LONGLONG *))was;
    }
    return hr;
}

static HRESULT WINAPI my_CoCreateInstance(const GUID *clsid, void *outer, DWORD ctx,
                                          const GUID *iid, void **out)
{
    HRESULT hr = real_CoCreateInstance(clsid, outer, ctx, iid, out);
    static LONG said;

    /* {B44392DA-499B-446B-A4CB-005FEAD0E6D5} -- Media Engine Class Factory. */
    if (SUCCEEDED(hr) && clsid && out && *out
        && clsid->Data1 == 0xB44392DA && clsid->Data2 == 0x499B
        && !real_factory_create)
    {
        void *was = NULL;
        if (patch_slot("MediaEngineClassFactory::CreateInstance", *out, 3,
                       (void *)my_factory_create, &was) && was)
            real_factory_create = (HRESULT (WINAPI *)(void *, DWORD, void *, void **))was;
    }
    if (clsid && InterlockedIncrement(&said) <= 30)
    {
        const GUID *g = clsid;
        logf_("  CoCreateInstance {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} -> 0x%08lX",
              (unsigned long)g->Data1, g->Data2, g->Data3,
              g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
              g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7],
              (unsigned long)hr);
    }
    return hr;
}

/* Says what the process last touched, every five seconds.
 *
 * A hang is legible from the outside only as "nothing changed since": the
 * counters stop moving and the last name stays put, and that name is the lead. */
static DWORD WINAPI watchdog(LPVOID unused)
{
    CHAR seen_file[MAX_PATH] = "", seen_lib[MAX_PATH] = "";
    LONG seen_files = -1, ticks = 0, quiet = 0;
    LONG prev_files = 0, prev_opens = 0, prev_reads = 0;
    LONG prev_sleep = 0, prev_pollw = 0;
    (void)unused;
    for (;;)
    {
        LONG d_files, d_opens, d_reads, d_sleep, d_pollw;

        Sleep(5000);
        ticks++;

        /* Streamline loads a second after we arm, and resolves D3D12 by hand.
         *
         * Retried from here because there is no load event we can wait on, and
         * the flag is set BEFORE the patch rather than after: patching an
         * import twice makes our own function the "original" it saves, and the
         * second call recurses until the stack is gone. Once attempted, never
         * again, whether it took or not. */

        /* The heartbeat.
         *
         * These counters used to print only inside the quiet branch below, so
         * they were legible only once the process had already stopped moving.
         * A game that is busy and wrong -- burning twelve cores at seven frames
         * a second -- never goes quiet, and nothing was ever reported about it.
         * Reading no STILL line then says only "not deadlocked"; it says
         * nothing about where the time goes.
         *
         * What separates real work from a spin storm is not the totals but
         * their rate, so every third tick says what moved since the last one.
         * Deltas over the window, not per second: a spin storm shows up in the
         * millions and would be legible either way, but file opens arrive a few
         * per minute and a per-second figure would round them to nothing. */
        d_files = files_opened - prev_files;
        d_opens = opens_in     - prev_opens;
        d_reads = reads_in     - prev_reads;
        d_sleep = polls_sleep  - prev_sleep;
        d_pollw = polls_wait   - prev_pollw;
        prev_files = files_opened;
        prev_opens = opens_in;
        prev_reads = reads_in;
        prev_sleep = polls_sleep;
        prev_pollw = polls_wait;

        if (ticks % 3 == 0)
        {
            logf_("PULSE at %ld s -- in the last 15 s: Sleep %ld, timed waits %ld, "
                  "opens %ld, reads %ld (%ld empty, %ld KB), files %ld",
                  (long)ticks * 5, (long)d_sleep, (long)d_pollw,
                  (long)d_opens, (long)d_reads,
                  (long)InterlockedExchange(&reads_empty, 0),
                  (long)InterlockedExchange(&read_bytes, 0), (long)d_files);
            report_hot_files();
            logf_("      in flight now -- infinite waits %ld, WaitEx %ld, condvar %ld, "
                  "WaitOnAddress %ld, reads %ld, opens %ld",
                  (long)(waits_in - waits_out), (long)(waitex_in - waitex_out),
                  (long)(cond_in - cond_out), (long)(addr_in - addr_out),
                  (long)(reads_in - reads_out), (long)(opens_in - opens_out));
        }

        if (files_opened == seen_files
            && !lstrcmpA(seen_file, last_file) && !lstrcmpA(seen_lib, last_lib))
        {
            quiet++;
            if (quiet == 2 || quiet == 6 || quiet == 18)
            {
                logf_("STILL: nothing new in %ld s. last file '%s'", (long)quiet * 5, last_file);
                logf_("      opens %ld in / %ld out (%ld in flight), reads %ld in / %ld out "
                      "(%ld in flight, last asked %ld bytes), infinite waits %ld/%ld",
                      (long)opens_in, (long)opens_out, (long)(opens_in - opens_out),
                      (long)reads_in, (long)reads_out, (long)(reads_in - reads_out),
                      (long)last_read_bytes, (long)waits_in, (long)waits_out);
                logf_("      other blocks -- WaitEx %ld/%ld (%ld), condvar %ld/%ld (%ld), "
                      "WaitOnAddress %ld/%ld (%ld)",
                      (long)waitex_in, (long)waitex_out, (long)(waitex_in - waitex_out),
                      (long)cond_in, (long)cond_out, (long)(cond_in - cond_out),
                      (long)addr_in, (long)addr_out, (long)(addr_in - addr_out));
                logf_("      polling -- timed waits %ld, Sleep %ld  "
                      "(if these climb between reports it is spinning, not deadlocked)",
                      (long)polls_wait, (long)polls_sleep);
            }
        }
        else
        {
            if (quiet >= 2)
                logf_("moving again after %ld s", (long)quiet * 5);
            quiet = 0;
            seen_files = files_opened;
            lstrcpynA(seen_file, last_file, MAX_PATH);
            lstrcpynA(seen_lib, last_lib, MAX_PATH);
        }
        /* Which media libraries are actually in the process.
         *
         * Hooking LoadLibrary only sees what the executable itself asks for.
         * COM loads its in-proc servers through LoadLibraryExW called from
         * ole32's own import table, not ours, and RESONANCE's log therefore
         * said no media library was ever loaded while it was reading two MP4s.
         * Asking whether a module is present answers that without caring how
         * it got there. */
        {
            static LONG modules_present;
            unsigned m;
            for (m = 0; m < sizeof(media_modules) / sizeof(media_modules[0]); m++)
            {
                if (!(modules_present & (1 << m)) && GetModuleHandleA(media_modules[m]))
                {
                    modules_present |= (1 << m);
                    logf_("  now loaded: %s", media_modules[m]);
                }
            }
        }
        if (ticks > 240) return 0;
    }
}

static void arm_startup_trace(void)
{
    void *was;
    HANDLE f = CreateFileA("C:\\mgvf-trace.txt", GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    {
        char buf[257];
        DWORD got = 0;
        if (ReadFile(f, buf, sizeof(buf) - 1, &got, NULL) && got) buf[got] = 0; else buf[0] = 0;
        if (strstr(buf, "nonvapi")) refuse_nvapi = TRUE;
        if (strstr(buf, "ownreg")) own_registry = TRUE;
        if (strstr(buf, "magenta")) paint_the_border = TRUE;
        if (strstr(buf, "d3d12")) watch_d3d12 = TRUE;
    }
    CloseHandle(f);
    trace_startup = TRUE;

    if (own_registry)
    {
        /* Space-free on purpose: a path with spaces did not survive the
         * launcher's own field, and there is no reason to find out whether it
         * survives everything else. */
        static const char *own = "C:\\mgvf-gst-registry.bin";
        char had[512] = "";
        GetEnvironmentVariableA("GST_REGISTRY", had, sizeof(had) - 1);
        if (SetEnvironmentVariableA("GST_REGISTRY", own))
            logf_("GST_REGISTRY set to %s by us (was %s)", own, had[0] ? had : "unset");
        else
            logf_("could not set GST_REGISTRY (err %lu)", GetLastError());
    }

    if ((was = hook_import("kernel32.dll", "CreateFileW", (void *)my_CreateFileW)))
        *(void **)&real_CreateFileW = was;
    if ((was = hook_import("kernel32.dll", "CreateFileA", (void *)my_CreateFileA)))
        real_CreateFileA = (HANDLE (WINAPI *)(LPCSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE))was;
    if ((was = hook_import("kernel32.dll", "LoadLibraryW", (void *)my_LoadLibraryW)))
        *(void **)&real_LoadLibraryW = was;
    if ((was = hook_import("kernel32.dll", "LoadLibraryExW", (void *)my_LoadLibraryExW)))
        *(void **)&real_LoadLibraryExW = was;
    if ((was = hook_import("kernel32.dll", "LoadLibraryA", (void *)my_LoadLibraryA)))
        *(void **)&real_LoadLibraryA = was;
    if ((was = hook_import("kernel32.dll", "LoadLibraryExA", (void *)my_LoadLibraryExA)))
        *(void **)&real_LoadLibraryExA = was;
    if ((was = hook_import("kernel32.dll", "ReadFile", (void *)my_ReadFile)))
        *(void **)&real_ReadFile = was;
    if ((was = hook_import("ole32.dll", "CoCreateInstance", (void *)my_CoCreateInstance)))
        *(void **)&real_CoCreateInstance = was;
    if ((was = hook_import("kernel32.dll", "WaitForSingleObjectEx", (void *)my_WaitForSingleObjectEx)))
        *(void **)&real_WaitForSingleObjectEx = was;
    if ((was = hook_import("kernel32.dll", "SleepConditionVariableSRW", (void *)my_SleepConditionVariableSRW)))
        *(void **)&real_SleepConditionVariableSRW = was;
    if ((was = hook_import("kernel32.dll", "SleepConditionVariableCS", (void *)my_SleepConditionVariableCS)))
        *(void **)&real_SleepConditionVariableCS = was;
    if ((was = hook_import("kernel32.dll", "WaitOnAddress", (void *)my_WaitOnAddress)))
        *(void **)&real_WaitOnAddress = was;
    if ((was = hook_import("kernel32.dll", "Sleep", (void *)my_Sleep)))
        *(void **)&real_Sleep = was;
    if ((was = hook_import("kernel32.dll", "WaitForSingleObject", (void *)my_WaitForSingleObject)))
        *(void **)&real_WaitForSingleObject = was;
    if ((was = hook_import("kernel32.dll", "WaitForMultipleObjects", (void *)my_WaitForMultipleObjects)))
        *(void **)&real_WaitForMultipleObjects = was;

    logf_("startup trace ON: CreateFileW %s, LoadLibraryW %s, LoadLibraryExW %s, "
          "WaitForSingleObject %s, WaitForMultipleObjects %s",
          real_CreateFileW ? "hooked" : "NOT IMPORTED",
          real_LoadLibraryW ? "hooked" : "NOT IMPORTED",
          real_LoadLibraryExW ? "hooked" : "NOT IMPORTED",
          real_WaitForSingleObject ? "hooked" : "NOT IMPORTED",
          real_WaitForMultipleObjects ? "hooked" : "NOT IMPORTED");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
}


/* The environment this process was actually given.
 *
 * Read from inside, because a launcher's settings screen says what it means to
 * pass and the process is what receives it, and those are different claims.
 * Learned the hard way on 27 Aug: a D3DM_MTL4=0 measured inside NINJA GAIDEN 4
 * was carried over to METAL GEAR SOLID 4 as though the two ran with the same
 * environment. They do not -- RaccoonBot sets these per game -- and reasoning
 * from one title's variables to another's is guessing with extra steps.
 *
 * Named variables only for VALUES: dumping the whole block would print the
 * user's paths and tokens into a log that gets pasted into bug reports.
 *
 * But every NAME is listed, and that is new. A named-only dump answers "what is
 * the value of X" and cannot answer "is there an X we never thought of" -- and
 * that second question cost an evening. Chasing why one title starts by hand and
 * not from a launcher, every comparison was made against this list, and the list
 * was not the environment. A name leaks nothing: it is a knob, not a secret. */
static void dump_environment(void)
{
    static const char *names[] = {
        "CX_GRAPHICS_BACKEND", "CX_BOTTLE", "CX_GRAPHICS_BACKEND_VERSION",
        "D3DM_ENABLE_METALFX", "D3DM_MTL4", "D3DM_SUPPORT_DXR", "D3DM_ENABLE_ASYNC_COMMIT",
        "DXMT_ENABLE_NVEXT", "DXMT_CONFIG",
        "WINEMSYNC", "WINEESYNC", "WINEFSYNC", "WINEDEBUG", "WINEDLLOVERRIDES",
        "MTL_HUD_ENABLED", "MVK_CONFIG_LOG_LEVEL",
        "GST_PLUGIN_PATH", "GST_PLUGIN_SYSTEM_PATH", "GST_PLUGIN_SCANNER", "GST_REGISTRY",
    };
    /* Families whose values are engine knobs rather than anything private.
     * Anything outside them is listed by name with its value withheld. */
    static const char *const open_[] = {
        "CX_", "D3DM_", "DXMT_", "DXVK_", "MTL_", "MVK_", "NAS_",
        "WINE", "GST_", "ROSETTA_",
    };
    char v[512];
    size_t i, j;

    logf_("environment this process was given:");
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        DWORD n = GetEnvironmentVariableA(names[i], v, sizeof(v) - 1);
        if (n && n < sizeof(v)) logf_("    %s=%s", names[i], v);
    }

    /* Every name present, so a variable nobody thought to ask about is still
     * visible. Values only for the open families above. */
    {
        LPCH block = GetEnvironmentStringsA();
        LPCH p = block;
        int shown = 0;
        if (!block) { logf_("    (could not read the environment block)"); return; }
        logf_("  every variable present, values only where they are engine knobs:");
        while (*p)
        {
            const char *eq = strchr(p, '=');
            size_t nlen = eq ? (size_t)(eq - p) : strlen(p);
            int open = 0;
            if (nlen && nlen < 200 && p[0] != '=')   /* skip cmd.exe's "=C:" entries */
            {
                for (j = 0; j < sizeof(open_) / sizeof(open_[0]); j++)
                    if (!strncmp(p, open_[j], strlen(open_[j]))) { open = 1; break; }
                if (open) logf_("    %s", p);
                else      logf_("    %.*s=<withheld>", (int)nlen, p);
                ++shown;
            }
            p += strlen(p) + 1;
        }
        logf_("  %d variable(s) in all", shown);
        FreeEnvironmentStringsA(block);
    }
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    dump_environment();
    arm_startup_trace();
    AddVectoredExceptionHandler(1, note_exception);
    {
        char v[8] = {0};
        if (GetEnvironmentVariableA("BEAST_REFUSE_D3D_MANAGER", v, sizeof(v)) && v[0] == '1')
            refuse_d3d_manager = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("BEAST_FORCE_NV12", v, sizeof(v)) && v[0] == '1')
            restore_nv12 = TRUE;
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
            { "mfplat.dll",      "MFCreateFile",
              (void *)my_MFCreateFile,   (void **)&real_MFCreateFile },
            { "mfreadwrite.dll", "MFCreateSourceReaderFromByteStream",
              (void *)my_MFCreateSourceReaderFromByteStream,
              (void **)&real_MFCreateSourceReaderFromByteStream },
            { "mfreadwrite.dll", "MFCreateSourceReaderFromURL",
              (void *)my_MFCreateSourceReaderFromURL,
              (void **)&real_MFCreateSourceReaderFromURL },
            { "mfreadwrite.dll", "MFCreateSourceReaderFromMediaSource",
              (void *)my_MFCreateSourceReaderFromMediaSource,
              (void **)&real_MFCreateSourceReaderFromMediaSource },
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

            /* D3D12 the way a title that ships Streamline actually asks for it.
             *
             * RISE OF THE RONIN imports D3D12CreateDevice from
             * sl.interposer.dll, not from d3d12.dll -- exporting the whole
             * D3D12 and DXGI surface under its own name is the entire point of
             * an interposer. Two rounds went into chasing this: first a hook on
             * the executable's d3d12.dll entry, which does not exist; then
             * hooks placed inside Streamline itself, on the theory that it
             * resolved the real thing by hand. It does not need to. The table
             * was right all along and the DLL name in it was wrong. Try both
             * names, since a title without Streamline uses the plain one. */
            if ((was = hook_import("sl.interposer.dll", "D3D12CreateDevice",
                                   (void *)my_D3D12CreateDevice)))
                { *(void **)&real_D3D12CreateDevice = was;
                  logf_("D3D12CreateDevice hooked -- the game asks Streamline for it"); }
            else if ((was = hook_import("d3d12.dll", "D3D12CreateDevice",
                                        (void *)my_D3D12CreateDevice)))
                { *(void **)&real_D3D12CreateDevice = was;
                  logf_("D3D12CreateDevice hooked -- straight from d3d12.dll"); }
        }
        logf_("import table: %d of %d Media Foundation and D3D9 entries hooked "
              "(0 here means this game resolves them some other way)",
              got, (int)(sizeof(hooks) / sizeof(hooks[0])) + 3);
    }

    logf_("---- write-path hooks %s | painting %s ----",
          watch_write_path ? "ON" : "off",
          "the real frames");
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
