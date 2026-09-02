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
static LONG CALLBACK note_exception(EXCEPTION_POINTERS *info);
static HRESULT (WINAPI *real_D3D12CreateDevice)(void *, UINT, const GUID *, void **);
static HRESULT WINAPI my_D3D12CreateDevice(void *, UINT, const GUID *, void **);
static HRESULT WINAPI my_CreateDXGIFactory(const GUID *, void **);
static HRESULT WINAPI my_CreateDXGIFactory1(const GUID *, void **);
static HRESULT WINAPI my_CreateDXGIFactory2(UINT, const GUID *, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory)(const GUID *, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory1)(const GUID *, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory2)(UINT, const GUID *, void **);


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
/*
 * Everything below changes what the game sees, and every one of them was at
 * some point left forced on -- which is how four runs measured a configuration
 * no title ever asked for. The file says it changes nothing while watching; it
 * now does that by default, and each lever has to be asked for.
 */
static BOOL force_cpu_decompression;   /* NG4_CPU_DECOMP */
static BOOL fake_options17;            /* NG4_FAKE_OPTIONS17 */

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
        /* Say what the page actually is, instead of only that it was refused.
         *
         * "VirtualProtect refused (err 87)" was recorded for months as a fact
         * about 26.3 and left there, and it is the reason no D3D instrumentation
         * can be installed on this title in the configuration that ships -- so
         * the one measurement that would explain it is worth more than another
         * observation of it happening. ERROR_INVALID_PARAMETER from
         * VirtualProtect means the range is not what the caller assumed: freed,
         * spanning two allocations, or not a private commit at all. */
        DWORD err = GetLastError();
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T got = VirtualQuery(&(*vt)[slot], &mbi, sizeof(mbi));

        logf_("  patch %s: VirtualProtect refused (err %lu) at vtable %p slot %d",
              what, err, (void *)*vt, slot);
        if (got)
            logf_("    the page: base %p, alloc base %p, size %llu, "
                  "state 0x%lx, protect 0x%lx (alloc 0x%lx), type 0x%lx",
                  mbi.BaseAddress, mbi.AllocationBase,
                  (unsigned long long)mbi.RegionSize, mbi.State,
                  mbi.Protect, mbi.AllocationProtect, mbi.Type);
        else
            logf_("    VirtualQuery could not describe the page either -- "
                  "the address is not mapped in this process");

        /* Opt-in only: a second attempt with execute rights, for when the vtable
         * shares a page with code. Off by default because making a patch land
         * that has never landed on this engine changes what is being measured. */
        {
            char v[8];
            if (GetEnvironmentVariableA("NG4_FORCE_PATCH", v, sizeof(v)) && v[0] == '1'
                && VirtualProtect(&(*vt)[slot], sizeof(void *),
                                  PAGE_EXECUTE_READWRITE, &old))
                logf_("    NG4_FORCE_PATCH: execute+write was accepted where "
                      "read+write was not -- continuing");
            else
                return FALSE;
        }
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
static BOOL refuse_d3d_manager;

/* Whether to patch the D3D12 device and its resources at all. Off, because on
 * the one build where the title works these patches are refused anyway. */
static BOOL patch_d3d12;

/* NG4_CAPS_LIKE_3=1: answer CheckFeatureSupport the way D3DMetal 3.0 does.
 *
 * Measured 2026-09-01 with a standalone PE against both toolkits, each
 * identified by lsof and by D3DM_DEVICE_DESCRIPTION echoing back: every
 * capability structure is identical between 3.0 and 4.0b2 except four words,
 * all of which 4.0b2 turns on -- OPTIONS2.DepthBoundsTestSupported,
 * OPTIONS12.EnhancedBarriersSupported,
 * OPTIONS13.UnrestrictedBufferTextureCopyPitchSupported and
 * OPTIONS13.UnrestrictedVertexElementAlignmentSupported. The movie is decoded
 * identically on both, the game reads the pixels on both, and only 3.0 draws
 * them. A capability that flips at device creation is exactly the kind of thing
 * a title branches its upload path on; a 1080p luma plane has a 1920-byte
 * pitch, which is not a multiple of 256, so the copy-pitch bit in particular
 * selects between padding rows and copying them as they are.
 *
 * This mode patches ONE vtable slot, CheckFeatureSupport, and nothing else.
 * The full NG4_PATCH_D3D12 set is what this title does not survive; whether a
 * single cold-path slot is tolerated is itself part of what the run measures,
 * and the log reports the patch landing or being refused either way. */
static BOOL caps_like_3;

/* NG4_WATCH_D3D12_RESOURCES=1: log what the game creates through
 * CreateCommittedResource -- one more cold slot, patched alone. The single
 * CheckFeatureSupport slot was tolerated on 2026-09-01 where the full set was
 * not, so the D3D12 side can be read one slot at a time. Observational only:
 * the call is forwarded untouched and only textures large enough to hold a
 * frame, or buffers large enough to carry one, are printed. */
static BOOL watch_d3d12_resources;

/* NG4_WATCH_CAPS=1: patch the CheckFeatureSupport slot to LOG the game's
 * capability queries, without changing any answer. NG4_CAPS_LIKE_3 does the
 * masking; this only listens, so the full list of what the title asks can be
 * recorded on a run that alters nothing. */
static BOOL watch_caps;

/* NG4_WATCH_MOVIE_COPY=1: follow the two objects the movie is built from.
 *
 * On 4.0b2 the game creates, as the movie opens, a 1920x1080 RGBA8_SRGB
 * texture and a buffer of exactly 1920x1080x4 bytes (2026-09-01). This mode
 * remembers both when they are created and then watches what touches them:
 * Map/Unmap on the buffer (and samples what the game wrote into it), and
 * CopyTextureRegion / CopyResource on any command list where either is the
 * source or destination. Everything else on those hot vtables is compared by
 * pointer and forwarded. 3.0 cannot be watched this way at all -- its D3D12
 * objects live in native code that wine reports as MEM_FREE -- so this is a
 * 4.0b2 instrument by construction. */
static BOOL watch_movie_copy;

/* NG4_WATCH_PRESENT=1: the swap chain.
 *
 * Nothing has ever been seen on 4.0b2 -- not the logos, not the menu, not the
 * movie -- and the Metal HUD, which is drawn on whatever the process presents,
 * never appears, while on 3.0 it shows up in the first small window and stays
 * through fullscreen. The one run of 2026-09-01 in which logos were seen turns
 * out to have been on 3.0 front-ends (its patches were refused with error 87).
 * So the question is not what the game draws but whether anything is ever
 * presented: creation of the swap chain and every Present, with its result,
 * watched and forwarded untouched. */
static BOOL watch_present;

/* NG4_NO_TEARING=1 and NG4_NO_WAITABLE=1: two experiments on the swap chain.
 *
 * Measured 2026-09-01 on 4.0b2: the game creates its swap chain, switches to
 * windowed, resizes with flags 0x842 (ALLOW_MODE_SWITCH |
 * FRAME_LATENCY_WAITABLE_OBJECT | ALLOW_TEARING) and then presents with
 * interval 0 and DXGI_PRESENT_ALLOW_TEARING -- and every Present returns S_OK
 * while nothing reaches the screen, not even the Metal HUD, which is drawn on
 * whatever is presented. A Present that succeeds and shows nothing is the
 * signature of a path D3DMetal accepts but does not perform. These strip the
 * tearing flag from Present and from the swap chain, and the waitable-object
 * flag from the swap chain, so the title takes the ordinary vsynced path. Opt
 * in, off by default, and each says in the log what it changed. */
static BOOL no_tearing, no_waitable;

/* NG4_FLIP_MODEL=1: rewrite the swap effect from the bitblt model (DISCARD 0 /
 * SEQUENTIAL 1) to FLIP_DISCARD (4). The title asks for SEQUENTIAL, which on
 * Windows a D3D12 device would refuse outright; D3DMetal 4.0b2 accepts it and
 * shows nothing, and the bitblt model is the kind of legacy path a new major
 * version drops first. NG4_FORCE_WINDOWED=1: ask for a windowed swap chain
 * even when the title asks for exclusive fullscreen, since the game keeps
 * re-creating swap chains and switching to windowed as if the mode switch
 * never settles. Both opt-in and logged. */
static BOOL flip_model, force_windowed;
static void *movie_tex_, *movie_buf_;
static void watch_movie_resource_vtable(void *res);   /* defined with the copy hooks below */


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

/* Whether this engine registers a VP9 decoder of its own. Recorded, and
 * deliberately not acted on -- see the note where it is set. */
static BOOL engine_offers_decoder = FALSE;

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
 * Off by default, like every other lever here. A run that substitutes an answer
 * cannot also tell you what the system would have answered -- and the first
 * thing this probe is now used for is watching a configuration that works,
 * where our answer would sit on top of a real one and hide it.
 *
 * Set NG4_ANSWER_MFT=1 to put it back. */
static BOOL answer_mft_gate;

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

    /* The engine's own answer, captured before the substitution below can
     * overwrite it. */
    BOOL engine_answered_vp9 = SUCCEEDED(hr) && in && count && *count > 0
                               && IsEqualGUID(&in->guidSubtype, &guid_VP90);

    if (answer_mft_gate && in && count && *count == 0
        && IsEqualGUID(&in->guidSubtype, &guid_VP90))
    {
        REG_TYPE_INFO substitute = *in;
        static LONG said;
        const char *how = "H264";

        substitute.guidSubtype = guid_H264;
        hr = real_MFTEnumEx(category, flags, &substitute, out, mfts, count);

        /* H.264 is the first choice and not the only one.
         *
         * Borrowing one named format's list assumed that format is registered,
         * and on a CrossOver that offers no H.264 decoder at all the fallback
         * had nothing to hand back: the gate still saw zero and the game still
         * quit, on an engine where everything else about the fix was working.
         * Measured on a patched Preview 27, where MFTEnumEx offers zero H.264
         * MFTs while 26.3 offers some -- so the answer was engine-dependent
         * through no property of this title.
         *
         * Asking with no input type at all means "every video decoder you
         * have", whatever this engine happens to register. It keeps the reason
         * the substitution is done this way -- these are real MFTs with real
         * lifetimes, not an array we fabricated for the game to free -- while
         * depending on nothing being present by name. */
        if (SUCCEEDED(hr) && *count == 0)
        {
            hr = real_MFTEnumEx(category, flags, NULL, out, mfts, count);
            how = "any video decoder";
        }
        if (InterlockedIncrement(&said) == 1)
            logf_("VP90 had no decoder; asked again for %s and got %u. On 26.3 "
                  "the game only counts these and never activates them -- if "
                  "an IMFActivate::ActivateObject line follows on this engine, "
                  "that assumption does not hold here and this answer is "
                  "hiding a legible error behind a later one",
                  how, count ? *count : 0);
        if (count && *count == 0)
            logf_("  and this engine registers NO video decoder of any kind, so "
                  "the gate cannot be answered from what is here");
    }
    logf_("MFTEnumEx flags=0x%lx -> 0x%08lx, %u decoder(s) offered",
          flags, hr, count ? *count : 0);
    /* Whether this engine registers a VP9 decoder -- measured, not obeyed.
     *
     * This fix pushes decoding to software by refusing the game a D3D device
     * manager. 5f781eb stood that down when the engine offers a decoder of its
     * own, reasoning that an engine which can decode VP9 needs no push. Both
     * halves of that were wrong, and each was wrong in its own way:
     *
     *   - It read *count after the substitution block below had run. That
     *     block is the workaround -- when the VP9 ask comes back empty we hand
     *     the game an H264 list so its gate passes -- so the count was ours.
     *     Fixed by capturing the game's typed ask first.
     *   - The idea itself does not hold. Measured 27 Aug on an engine that
     *     does register VP9 (winevideo's vpx plugin under our winegstreamer):
     *     the enumeration answers 1 on the first ask, the stand-down fires
     *     exactly as designed, and the title dies on its first video anyway --
     *     six access violations, the same ones it dies with everywhere else.
     *     On stock 26.3, where the ask returns 0 and the refusal stands, the
     *     videos play through to the menu.
     *
     * So a decoder existing does not make the hardware path work here. What
     * the title needs is the software path, on every engine, which is what
     * shipped on 25 Aug. The flag stays because the log is more legible for
     * having it -- it says which kind of engine this is -- and nothing reads
     * it.
     */
    if (engine_answered_vp9)
        engine_offers_decoder = TRUE;
    (void)engine_offers_decoder;
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
#define SLOT_GET_STREAM_SEL   3
#define SLOT_SET_STREAM_SEL   4
#define SLOT_GET_NATIVE_TYPE  5
#define SLOT_SET_CURRENT_TYPE 7
#define SLOT_READ_SAMPLE      9

/* Why these two were added, since the vtable comment above has named them all
 * along and nobody hooked them.
 *
 * ReadSample returns no sample, 200 calls running, flags 0 -- no error and no
 * end of stream. Meanwhile winegstreamer's reader thread sits in
 * wg_parser_get_next_read_offset, waiting for GStreamer to ask for bytes it
 * never asks for. So the pipeline read enough to typefind the container and
 * negotiate a format -- which happens during preroll, from the first bytes --
 * and then stopped pulling.
 *
 * A pipeline that negotiates and does not pull is one that never left PAUSED.
 * In Media Foundation terms the reader starts the source on the first ReadSample
 * only if a stream is SELECTED; with none selected there is nothing to start,
 * nobody asks for bytes, and ReadSample returns "no sample" for ever -- without
 * an error, which is exactly what is measured.
 *
 * That is a hypothesis, and these two slots are how it is tested rather than
 * argued. Three outcomes, each naming a different culprit: the game never calls
 * SetStreamSelection at all; it calls it and we refuse; or it selects, we agree,
 * and nothing flows -- which kills the hypothesis and moves the search
 * downstream. */
#define MF_FIRST_VIDEO_STREAM 0xFFFFFFFCu

static HRESULT (WINAPI *real_GetStreamSelection)(void *, DWORD, BOOL *);
static HRESULT (WINAPI *real_SetStreamSelection)(void *, DWORD, BOOL);
static HRESULT (WINAPI *real_GetNativeMediaType)(void *, DWORD, DWORD, void **);
static HRESULT (WINAPI *real_SetCurrentMediaType)(void *, DWORD, DWORD *, void *);
static HRESULT (WINAPI *real_ReadSample)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                         LONGLONG *, void **);

static const char *stream_name(DWORD s)
{
    switch (s)
    {
    case 0xFFFFFFFEu: return "FIRST_AUDIO";
    case 0xFFFFFFFCu: return "FIRST_VIDEO";
    case 0xFFFFFFFFu: return "ALL";
    default:          return "index";
    }
}

static HRESULT WINAPI my_GetStreamSelection(void *self, DWORD stream, BOOL *selected)
{
    HRESULT hr = real_GetStreamSelection(self, stream, selected);
    logf_("GetStreamSelection(%s %lu) -> 0x%08lx, selected=%s",
          stream_name(stream), stream, hr,
          (SUCCEEDED(hr) && selected) ? (*selected ? "TRUE" : "FALSE") : "?");
    return hr;
}

static HRESULT WINAPI my_SetStreamSelection(void *self, DWORD stream, BOOL selected)
{
    HRESULT hr = real_SetStreamSelection(self, stream, selected);
    logf_("SetStreamSelection(%s %lu, %s) -> 0x%08lx  << the game asked",
          stream_name(stream), stream, selected ? "TRUE" : "FALSE", hr);
    return hr;
}

static HRESULT WINAPI my_GetNativeMediaType(void *self, DWORD stream, DWORD index, void **type)
{
    HRESULT hr;
    if (index == 0)
        logf_("GetNativeMediaType(stream %lu) -- calling", stream);
    hr = real_GetNativeMediaType(self, stream, index, type);
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
    HRESULT hr;
    /* Logged before the call as well as after. With a DXGI device manager bound
     * to the reader the process aborts from the unix side (SIGABRT, surfaced as
     * EXCEPTION_WINE_ASSERTION) somewhere after the reader is created, and both
     * media-type hooks only spoke after returning -- so a crash inside either of
     * them was indistinguishable from the game never calling it. */
    logf_("SetCurrentMediaType(stream %lu) -- calling", stream);
    hr = real_SetCurrentMediaType(self, stream, reserved, type);
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

    /* NG4_SELECT_STREAM=1 turns the reading above from a hypothesis into a test.
     *
     * Selecting the video stream ourselves, once, before the first read. If the
     * game never selected it, this is the missing call and frames should follow.
     * If frames still do not follow, the hypothesis is wrong and we have spent
     * one run to know it. Off by default: a probe that changes what it measures
     * is an instrument that has become the fix, and this project has been caught
     * by that before. */
    if (InterlockedCompareExchange(&calls, 0, 0) == 0 && real_SetStreamSelection)
    {
        char v[8];
        if (GetEnvironmentVariableA("NG4_SELECT_STREAM", v, sizeof(v)) && v[0] == '1')
        {
            BOOL was = FALSE;
            HRESULT q = real_GetStreamSelection
                      ? real_GetStreamSelection(self, MF_FIRST_VIDEO_STREAM, &was) : 0x80004005L;
            HRESULT s2 = real_SetStreamSelection(self, MF_FIRST_VIDEO_STREAM, TRUE);
            logf_("NG4_SELECT_STREAM: was %s (query 0x%08lx), selecting -> 0x%08lx",
                  SUCCEEDED(q) ? (was ? "TRUE" : "FALSE") : "unknown", q, s2);
        }
    }

    HRESULT hr;
    LONG n;
    if (InterlockedCompareExchange(&calls, 0, 0) == 0)
        logf_("ReadSample -- first call");
    hr = real_ReadSample(self, stream, flags, actual, sflags, ts, sample);
    n = InterlockedIncrement(&calls);
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
        /* Say which out parameters the caller passed, because without that this
         * line cannot be read.
         *
         * Media Foundation requires a caller in asynchronous mode to pass NULL
         * for the stream index, the flags, the timestamp and the sample: the
         * frames are delivered to IMFSourceReaderCallback::OnReadSample instead,
         * on a worker thread. So in async mode `sample` is NULL, `*sample` is
         * never written, the `got` counter above cannot increment, and `sflags`
         * is NULL so the flags print as zero -- all of which is exactly what a
         * working reader looks like from here.
         *
         * This printed "no sample, flags 0x0" for four NG4 runs and was read each
         * time as the reader producing nothing. It is not evidence of that until
         * the mode is known. If sample is NULL below, this counter is measuring a
         * channel that is supposed to be empty and the frames have to be looked
         * for in the callback. */
        logf_("ReadSample: no sample, flags 0x%lx (call %ld, %ld so far)"
              " -- caller passed sample=%s flags=%s%s",
              sflags ? *sflags : 0, n, got,
              sample ? "buffer" : "NULL", sflags ? "buffer" : "NULL",
              sample ? "" : "  << NULL out params: the reader is ASYNCHRONOUS and"
                            " the frames go to OnReadSample, which we do not watch."
                            " This counter cannot see them.");
    return hr;
}

/* Counted separately from what is printed.
 *
 * The first build of this hook logged only textures of 640x360 or more, and the
 * run produced none at all -- which reads as "the game creates no texture for
 * the video" but is equally consistent with "it creates them smaller" or "this
 * device is never used". Those need different next steps, so the total is
 * reported alongside the frame rather than inferred from a silence. */
static LONG tex2d_calls;

/* The frame size, taken from the buffer itself rather than from the caller.
 *
 * Lock's length parameters are optional and NG4 passes NULL for them on most
 * calls -- only the first asked for a length. A paint test conditioned on the
 * caller supplying one therefore painted exactly one frame out of three
 * hundred, and the black screen that followed was read as "the game does not
 * draw what we hand it" when nothing had actually been handed to it. The size
 * is a property of the buffer, so it is read once from GetCurrentLength and
 * kept. */
static DWORD known_buffer_len;

/* Whether the game ever reads the pixels it is handed.
 *
 * With the DXGI device manager refused -- which is what this title needs, since
 * allowing it crashes the game on a null write -- the frames arrive in system
 * memory. 300 of them were measured arriving on 2026-09-01 with correct
 * timestamps, and nothing was drawn. That leaves exactly two possibilities and
 * they need different repairs: the game locks the buffer and reads it, in which
 * case what is wrong is the content or the format and the magenta picture this
 * title once showed was this same state half solved; or it never locks at all,
 * in which case it is rejecting the sample for a reason that has to be found in
 * the sample itself.
 *
 * Patching Lock reaches every buffer in the process, because the vtable is
 * shared by the class rather than owned by the instance. That is acceptable for
 * a probe that only logs, and is the same technique used everywhere else in
 * this file -- but it is the reason this must never grow a side effect. */
#define SLOT_SAMPLE_BUFCOUNT    39
#define SLOT_SAMPLE_GET_BUFFER  40
#define SLOT_BUF_LOCK            3
#define SLOT_BUF_GETLENGTH       5
#define SLOT_2D_LOCK2D           3

static HRESULT (WINAPI *real_px_Lock)(void *, BYTE **, DWORD *, DWORD *);
static HRESULT (WINAPI *real_px_Lock2D)(void *, BYTE **, LONG *);

static HRESULT WINAPI my_px_Lock(void *self, BYTE **data, DWORD *maxlen, DWORD *curlen)
{
    static LONG n;
    HRESULT hr = real_px_Lock ? real_px_Lock(self, data, maxlen, curlen) : 0x80004005L;
    LONG i = InterlockedIncrement(&n);

    /* NG4_PAINT_TEST=1 -- overwrite the frame with flat white.
     *
     * This is the one place in this file where changing the data IS the
     * measurement, so it is opt-in and says so in the log every time it fires.
     *
     * Everything upstream is now known good: the game is handed a correct
     * 1920x1080 NV12 frame with a real picture in it and reads the pixels, and
     * the screen stays black. What cannot be seen from here is whether those
     * pixels are ever drawn, because the game is D3D12 and patching its D3D
     * vtables stops it dead. Writing a value we choose answers it without
     * touching D3D at all: if the screen turns white the game draws what we
     * hand it and the fault is in the colour conversion, which is where the
     * magenta this title once showed points too. If it stays black, nothing we
     * put in this buffer reaches the screen and the search moves past the
     * upload entirely.
     *
     * NV12: the luma plane is the first two thirds, 235 is white in video
     * range; the interleaved chroma that follows is neutral at 128. */
    {
        DWORD blen = (curlen && *curlen) ? *curlen : known_buffer_len;
        char v[8];
        if (SUCCEEDED(hr) && data && *data && blen >= 4096
            && GetEnvironmentVariableA("NG4_PAINT_TEST", v, sizeof(v)) && v[0] == '1')
        {
            DWORD luma = (blen * 2) / 3;
            memset(*data, 235, luma);
            memset(*data + luma, 128, blen - luma);
            if (i == 1 || i == 50)
                logf_("  NG4_PAINT_TEST: frame %ld overwritten with flat white, "
                      "%lu bytes (length %s) -- a white screen now means the game "
                      "draws what we hand it", i, blen,
                      (curlen && *curlen) ? "from the caller" : "from the buffer");
        }
    }

    if (i == 1 || i == 50)
    {
        /* Say whether the pixels are a picture or are black.
         *
         * A frame of the right size, delivered on time, with the game reading
         * it, is indistinguishable from a working video until someone looks at
         * the bytes -- and an all-zero NV12 frame satisfies every check this
         * probe made before this line existed. The luma plane is the first
         * width*height bytes; sampling it across the frame separates "the
         * decoder produced black" from "the game was given a picture and did
         * not draw it", which need repairs at opposite ends of the pipeline. */
        unsigned lo = 255, hi = 0;
        unsigned long long sum = 0;
        int taken = 0;
        DWORD blen2 = (curlen && *curlen) ? *curlen : known_buffer_len;
        if (SUCCEEDED(hr) && data && *data && blen2 >= 4096)
        {
            const BYTE *p8 = *data;
            DWORD luma = (blen2 * 2) / 3;   /* NV12: luma is two thirds */
            DWORD step = luma / 512;
            DWORD off;
            if (!step) step = 1;
            for (off = 0; off < luma; off += step)
            {
                BYTE v = p8[off];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                sum += v;
                taken++;
            }
        }
        if (taken)
            logf_("  Lock #%ld -> 0x%08lx, %lu bytes | luma min %u max %u mean %u over %d samples%s",
                  i, hr, blen2, lo, hi, (unsigned)(sum / taken), taken,
                  hi == 0 ? "  << THE FRAME IS ENTIRELY BLACK"
                          : "  << there is a real picture in this buffer");
        else
            logf_("  Lock #%ld -> 0x%08lx, %lu bytes  << the game IS reading the pixels",
                  i, hr, (curlen && SUCCEEDED(hr)) ? *curlen : 0);
    }
    return hr;
}

static HRESULT WINAPI my_px_Lock2D(void *self, BYTE **scan0, LONG *pitch)
{
    static LONG n;
    HRESULT hr = real_px_Lock2D ? real_px_Lock2D(self, scan0, pitch) : 0x80004005L;
    LONG i = InterlockedIncrement(&n);
    if (i == 1 || i == 50)
        logf_("  Lock2D #%ld -> 0x%08lx, pitch %ld  << the game IS reading the pixels (2D)",
              i, hr, (pitch && SUCCEEDED(hr)) ? *pitch : 0);
    return hr;
}

/* Look inside the first sample and arm the pixel hooks. Read-only: the buffer is
 * fetched by index rather than converted, so the sample is left as the game will
 * find it. */
static void watch_sample_pixels(void *sample)
{
    void ***svt, ***bvt;
    HRESULT (WINAPI *get_count)(void *, DWORD *);
    HRESULT (WINAPI *get_buffer)(void *, DWORD, void **);
    HRESULT (WINAPI *qi)(void *, const GUID *, void **);
    ULONG (WINAPI *rel)(void *);
    HRESULT (WINAPI *get_len)(void *, DWORD *);
    static void *l1, *l2;
    void *buf = NULL, *two = NULL;
    DWORD count = 0, len = 0;

    if (!sample || real_px_Lock || real_px_Lock2D)
        return;

    svt = (void ***)sample;
    get_count  = (HRESULT (WINAPI *)(void *, DWORD *))(*svt)[SLOT_SAMPLE_BUFCOUNT];
    get_buffer = (HRESULT (WINAPI *)(void *, DWORD, void **))(*svt)[SLOT_SAMPLE_GET_BUFFER];

    if (FAILED(get_count(sample, &count)) || !count)
    {
        logf_("  the sample carries no buffer at all (count %lu)", count);
        return;
    }
    if (FAILED(get_buffer(sample, 0, &buf)) || !buf)
    {
        logf_("  GetBufferByIndex(0) gave nothing");
        return;
    }

    bvt = (void ***)buf;
    get_len = (HRESULT (WINAPI *)(void *, DWORD *))(*bvt)[SLOT_BUF_GETLENGTH];
    if (FAILED(get_len(buf, &len))) len = 0;
    logf_("  sample carries %lu buffer(s), first is %lu bytes", count, len);
    known_buffer_len = len;

    if (patch_slot("buffer Lock", buf, SLOT_BUF_LOCK, (void *)my_px_Lock, &l1))
        real_px_Lock = (HRESULT (WINAPI *)(void *, BYTE **, DWORD *, DWORD *))l1;

    qi = (HRESULT (WINAPI *)(void *, const GUID *, void **))(*bvt)[0];
    if (SUCCEEDED(qi(buf, &IID_IMF2DBuffer_, &two)) && two)
    {
        void ***tvt = (void ***)two;
        if (patch_slot("buffer Lock2D", two, SLOT_2D_LOCK2D, (void *)my_px_Lock2D, &l2))
            real_px_Lock2D = (HRESULT (WINAPI *)(void *, BYTE **, LONG *))l2;
        rel = (ULONG (WINAPI *)(void *))(*tvt)[2];
        rel(two);
    }
    else
        logf_("  the buffer is not an IMF2DBuffer -- flat only");

    rel = (ULONG (WINAPI *)(void *))(*bvt)[2];
    rel(buf);
}

/* The frames of an asynchronous reader, which is where this title's actually are.
 *
 * Measured 2026-09-01: NG4 passes NULL for the sample and flags out parameters
 * of ReadSample, which is what Media Foundation requires of a caller in
 * asynchronous mode. The samples are delivered here instead, on a worker
 * thread. Until this hook existed the probe watched only ReadSample, whose
 * emptiness is not a symptom but the defined behaviour of that mode -- five NG4
 * runs were read as a dead pipeline on the strength of it.
 *
 * The callback is not reachable from the reader; it is handed to Media
 * Foundation in the creation attributes, so it is fetched from there. Both
 * creation paths carry attributes and both are wired, because this title was
 * already once found taking the path that had not been instrumented. */
#define SLOT_ON_READ_SAMPLE   3
#define SLOT_ATTR_GET_UNKNOWN 17

static const GUID guid_MF_SOURCE_READER_ASYNC_CALLBACK =
    { 0x1e3dbeac, 0xbb43, 0x4c35, { 0xb5, 0x07, 0xcd, 0x64, 0x44, 0x64, 0xc9, 0x65 } };
static const GUID IID_IMFSourceReaderCallback_ =
    { 0xdeec8d99, 0xfa1d, 0x4d82, { 0x84, 0xc2, 0x2c, 0x89, 0x69, 0x94, 0x48, 0x67 } };

static HRESULT (WINAPI *real_OnReadSample)(void *, HRESULT, DWORD, DWORD,
                                           LONGLONG, void *);

static HRESULT WINAPI my_OnReadSample(void *self, HRESULT status, DWORD stream,
                                      DWORD sflags, LONGLONG ts, void *sample)
{
    static LONG calls, frames;
    LONG n = InterlockedIncrement(&calls);

    if (sample)
    {
        LONG f = InterlockedIncrement(&frames);
        if (f == 1)
            watch_sample_pixels(sample);
        if (f == 1 || f == 50 || f == 300)
            logf_("OnReadSample: frame %ld arrived, pts %lld  << the reader IS producing",
                  f, (long long)ts);
    }
    else if (n == 1 || n == 20 || n == 200)
        logf_("OnReadSample: no sample (call %ld, %ld frames so far), "
              "status 0x%08lx, stream %lu, flags 0x%lx",
              n, frames, status, stream, sflags);

    if (FAILED(status))
    {
        static LONG bad;
        if (InterlockedIncrement(&bad) == 1)
            logf_("OnReadSample: status 0x%08lx on call %ld  << the reader is reporting failure",
                  status, n);
    }

    return real_OnReadSample ? real_OnReadSample(self, status, stream, sflags, ts, sample)
                             : 0;
}

/* Pull the callback out of the creation attributes and patch it. */
static void watch_async_callback(void *attrs)
{
    HRESULT (WINAPI *get_unknown)(void *, const GUID *, const GUID *, void **);
    void ***vt;
    void *cb = NULL;
    static void *saved;

    if (!attrs || real_OnReadSample)
        return;

    vt = (void ***)attrs;
    get_unknown = (HRESULT (WINAPI *)(void *, const GUID *, const GUID *, void **))
                  (*vt)[SLOT_ATTR_GET_UNKNOWN];
    if (FAILED(get_unknown(attrs, &guid_MF_SOURCE_READER_ASYNC_CALLBACK,
                           &IID_IMFSourceReaderCallback_, &cb)) || !cb)
    {
        logf_("  no async callback in the reader attributes -- this reader is synchronous");
        return;
    }

    if (patch_slot("OnReadSample", cb, SLOT_ON_READ_SAMPLE,
                   (void *)my_OnReadSample, &saved))
        real_OnReadSample = (HRESULT (WINAPI *)(void *, HRESULT, DWORD, DWORD,
                                                LONGLONG, void *))saved;

    /* Give back the reference GetUnknown took; the reader holds its own. */
    {
        void ***cvt = (void ***)cb;
        ULONG (WINAPI *rel)(void *) = (ULONG (WINAPI *)(void *))(*cvt)[2];
        rel(cb);
    }
}

static volatile LONG reader_exists_;   /* set when MFCreateSourceReaderFromURL succeeds */

static HRESULT WINAPI my_MFCreateSourceReaderFromByteStream(void *stream, void *attrs, void **reader)
{
    static LONG made;
    HRESULT hr = real_MFCreateSourceReaderFromByteStream(stream, attrs, reader);
    LONG n = InterlockedIncrement(&made);
    if (n == 1 || FAILED(hr))
        logf_("MFCreateSourceReaderFromByteStream -> 0x%08lx (reader %ld)", hr, n);
    if (SUCCEEDED(hr) && reader && *reader)
    {
        static void *gn, *sc, *rs, *gs, *ss;
        patch_slot("GetStreamSelection",  *reader, SLOT_GET_STREAM_SEL,
                   (void *)my_GetStreamSelection,  &gs);
        patch_slot("SetStreamSelection",  *reader, SLOT_SET_STREAM_SEL,
                   (void *)my_SetStreamSelection,  &ss);
        patch_slot("GetNativeMediaType",  *reader, SLOT_GET_NATIVE_TYPE,
                   (void *)my_GetNativeMediaType,  &gn);
        patch_slot("SetCurrentMediaType", *reader, SLOT_SET_CURRENT_TYPE,
                   (void *)my_SetCurrentMediaType, &sc);
        patch_slot("ReadSample",          *reader, SLOT_READ_SAMPLE,
                   (void *)my_ReadSample,          &rs);
        real_GetStreamSelection  = (HRESULT (WINAPI *)(void *, DWORD, BOOL *))gs;
        real_SetStreamSelection  = (HRESULT (WINAPI *)(void *, DWORD, BOOL))ss;
        real_GetNativeMediaType  = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void **))gn;
        real_SetCurrentMediaType = (HRESULT (WINAPI *)(void *, DWORD, DWORD *, void *))sc;
        real_ReadSample = (HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                              LONGLONG *, void **))rs;
        watch_async_callback(attrs);
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
    if (SUCCEEDED(hr)) InterlockedExchange(&reader_exists_, 1);

    /* Watch this reader too.
     *
     * The three slots below were patched only on the byte-stream path, and this
     * title takes the URL one: its reader was created, succeeded, and then
     * nothing was ever logged again. That read as the game dying immediately
     * after -- it is where our own view ended. The reader is the same interface
     * either way, so the same three slots apply. */
    /* Every reader, not the first.
     *
     * The first version of this guarded on real_ReadSample being unset, so only
     * one reader was ever watched. A title that opens a reader per movie would
     * then show its first and nothing else -- and the log said exactly that:
     * the reader created, the slots patched, and not one call arriving. */
    if (SUCCEEDED(hr) && reader && *reader)
    {
        static void *gn, *sc, *rs, *gs, *ss;
        patch_slot("GetStreamSelection",  *reader, SLOT_GET_STREAM_SEL,
                   (void *)my_GetStreamSelection,  &gs);
        patch_slot("SetStreamSelection",  *reader, SLOT_SET_STREAM_SEL,
                   (void *)my_SetStreamSelection,  &ss);
        patch_slot("GetNativeMediaType",  *reader, SLOT_GET_NATIVE_TYPE,
                   (void *)my_GetNativeMediaType,  &gn);
        patch_slot("SetCurrentMediaType", *reader, SLOT_SET_CURRENT_TYPE,
                   (void *)my_SetCurrentMediaType, &sc);
        patch_slot("ReadSample",          *reader, SLOT_READ_SAMPLE,
                   (void *)my_ReadSample,          &rs);
        real_GetStreamSelection  = (HRESULT (WINAPI *)(void *, DWORD, BOOL *))gs;
        real_SetStreamSelection  = (HRESULT (WINAPI *)(void *, DWORD, BOOL))ss;
        real_GetNativeMediaType  = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void **))gn;
        real_SetCurrentMediaType = (HRESULT (WINAPI *)(void *, DWORD, DWORD *, void *))sc;
        real_ReadSample = (HRESULT (WINAPI *)(void *, DWORD, DWORD, DWORD *, DWORD *,
                                              LONGLONG *, void **))rs;
        watch_async_callback(attrs);
    }

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
            {   /* Release through slot 2 rather than the IUnknown macros:
                 * this file does not pull in the headers that declare them. */
                ULONG (WINAPI *release)(void *) =
                    (ULONG (WINAPI *)(void *))(*(void ***)stream)[2];
                release(stream);
            }
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
    {
        static LONG decided;
        if (!decided && InterlockedExchange(&decided, 1) == 0)
        {
            char v[8] = { 0 };
            if (GetEnvironmentVariableA("NG4_WATCH_PRESENT", v, sizeof(v)) && v[0] == '1')
                watch_present = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_NO_TEARING", v, sizeof(v)) && v[0] == '1')
            no_tearing = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_NO_WAITABLE", v, sizeof(v)) && v[0] == '1')
            no_waitable = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_FLIP_MODEL", v, sizeof(v)) && v[0] == '1')
            flip_model = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_FORCE_WINDOWED", v, sizeof(v)) && v[0] == '1')
            force_windowed = TRUE;
        }
    }
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
    if (watch_present)
    {
        SWAP(CreateDXGIFactory)
        SWAP(CreateDXGIFactory1)
        SWAP(CreateDXGIFactory2)
    }
#undef SWAP

    /* Name every media entry point the game asks for, resolved or not. The
     * list of what it looks for is itself evidence about which player it uses. */
    if ((name[0] == 'M' && name[1] == 'F') || strncmp(name, "CreateDXGI", 10) == 0)
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

/* Patching the game's own OpenSharedResource is only useful when the write path
 * is on. It is applied unconditionally, and on 26.3 that is harmless because
 * VirtualProtect refuses the page and the patch never lands. On CrossOver
 * Preview the same patch DOES land, and Preview is where this title stops
 * dead immediately afterwards -- so the one behaviour that differs between the
 * two engines needs to be switchable before it can be ruled in or out.
 * NG4_NO_D3D11_PATCH=1 leaves the vtable alone. */
static BOOL no_d3d11_patch;

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

/* What the game creates to put the video in.
 *
 * The picture reaches the game intact -- 1920x1080 NV12, 3110400 bytes, luma
 * measured min 25 max 222 mean 61 -- and it locks the buffer and reads it. So
 * the remaining question is what it copies those bytes into. A destination that
 * is not NV12, or whose dimensions do not match, produces exactly what has been
 * seen: a game that draws its frame and shows nothing useful, and once showed
 * magenta.
 *
 * Observational only. It forwards every call unchanged and logs the first few
 * descriptions, because this title has twice been broken by a probe that did
 * more than watch. */
#define SLOT_DEV11_CREATE_TEX2D_ 5

static HRESULT (WINAPI *real_CreateTexture2D)(void *, const void *, const void *, void **);


static const char *dxgi_name(UINT f)
{
    switch (f)
    {
        case 0:   return "UNKNOWN";
        case 28:  return "R8G8B8A8_UNORM";
        case 87:  return "B8G8R8A8_UNORM";
        case 61:  return "R8_UNORM";
        case 49:  return "R8G8_UNORM";
        case 103: return "NV12";
        case 104: return "P010";
        default:  return "other";
    }
}

static HRESULT WINAPI my_CreateTexture2D(void *self, const void *desc,
                                         const void *data, void **tex)
{
    HRESULT hr = real_CreateTexture2D ? real_CreateTexture2D(self, desc, data, tex)
                                      : 0x80004005L;
    if (desc)
    {
        const UINT *d = (const UINT *)desc;
        LONG i = InterlockedIncrement(&tex2d_calls);
        /* Only the ones that could hold a 1080p frame, and only a few of them:
         * this title creates hundreds of textures and the log is not the place
         * for all of them. */
        if (d[0] >= 640 && d[1] >= 360 && i < 4000)
        {
            static LONG shown;
            if (InterlockedIncrement(&shown) <= 12)
                logf_("  CreateTexture2D %ux%u fmt %u (%s) usage %u bind 0x%x "
                      "cpu 0x%x misc 0x%x -> 0x%08lx",
                      d[0], d[1], d[4], dxgi_name(d[4]), d[7], d[8], d[9], d[10], hr);
        }
    }
    return hr;
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
        if (no_d3d11_patch)
        {
            logf_("  d3d11 OpenSharedResource: left alone (NG4_NO_D3D11_PATCH)");
        }
        else if (!patch_slot("d3d11 OpenSharedResource", *device, SLOT_DEV11_OPENSHARED,
                             (void *)my_OpenSharedResource, &os))
            real_OpenSharedResource = NULL;

        /* CreateTexture2D is deliberately NOT patched.
         *
         * Measured 2026-09-01: with the hook installed the title stops dead
         * immediately after the patch lands -- the log ends at that line, no
         * device manager, no reader, no frames, and the audio that had been
         * playing stops. One device, so this is not the vtable being
         * self-assigned; the patch itself is what NG4 will not survive, which is
         * the same thing already recorded for its D3D12 vtables. The hook and
         * its formatting are kept because they cost nothing while unwired and
         * the next attempt should not have to write them again. */
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
             * Solid magenta told them apart in one run, and the screen came out
             * magenta: the write path was right and the fault was upstream of
             * it. The same measurement settled the same question on DYNASTY
             * WARRIORS. The scaffolding that painted it is gone -- it had been
             * wired to a P5S_REAL_FRAMES lever that only ever set its flag back
             * to the value it already had, so the compiler dropped the branch
             * and the lever moved nothing while still being advertised. */
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
    if (caps_like_3 && (feature == 18 || feature == 41 || feature == 42))
    {
        static LONG asked;
        if (InterlockedIncrement(&asked) <= 6)
            logf_("  caps-like-3.0: the game asked for %s (hr 0x%08lx)",
                  feature == 18 ? "OPTIONS2" : feature == 41 ? "OPTIONS12" : "OPTIONS13", hr);
    }
    if (caps_like_3 && SUCCEEDED(hr) && data && size >= 4 && readable_(data, size))
    {
        UINT32 *w = (UINT32 *)data;
        static LONG said2, said12, said13;
        if (feature == 18 && w[0])                         /* OPTIONS2 */
        {
            w[0] = 0;
            if (InterlockedIncrement(&said2) == 1)
                logf_("  caps-like-3.0: OPTIONS2.DepthBoundsTestSupported 1 -> 0");
        }
        else if (feature == 41 && size >= 8 && w[1])       /* OPTIONS12 */
        {
            w[1] = 0;
            if (InterlockedIncrement(&said12) == 1)
                logf_("  caps-like-3.0: OPTIONS12.EnhancedBarriersSupported 1 -> 0");
        }
        else if (feature == 42 && size >= 8 && (w[0] || w[1]))   /* OPTIONS13 */
        {
            w[0] = 0; w[1] = 0;
            if (InterlockedIncrement(&said13) == 1)
                logf_("  caps-like-3.0: OPTIONS13.UnrestrictedBufferTextureCopyPitch and "
                      "UnrestrictedVertexElementAlignment 1 -> 0");
        }
    }

    if (fake_options17 && in_create_queue_ && feature == 46 && SUCCEEDED(hr) &&
        data && size >= 8 && readable_(data, 8))
    {
        ((UINT32 *)data)[0] = 1;
        ((UINT32 *)data)[1] = 1;
        logf_("  [in CreateQueue] answering yes to OPTIONS17 (was all zeros)");
    }

    if (in_create_queue_ || InterlockedIncrement(&outside) <= 48)
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

/* ---- swap chain watch ---- */
static HRESULT (WINAPI *real_SC_Present)(void *, UINT, UINT);
static HRESULT (WINAPI *real_SC_Present1)(void *, UINT, UINT, const void *);
static HRESULT (WINAPI *real_SC_GetBuffer)(void *, UINT, const GUID *, void **);
static HRESULT (WINAPI *real_SC_SetFullscreen)(void *, BOOL, void *);
static HRESULT (WINAPI *real_SC_ResizeBuffers)(void *, UINT, UINT, UINT, UINT, UINT);
static HRESULT (WINAPI *real_F_CreateSwapChain)(void *, void *, const void *, void **);
static HRESULT (WINAPI *real_F_CreateSwapChainForHwnd)(void *, void *, HWND, const void *, const void *, void *, void **);
static HRESULT (WINAPI *real_F_CreateSwapChainForComposition)(void *, void *, const void *, void *, void **);
static const GUID IID_IDXGISwapChain1_ = { 0x790a45f7, 0x0d42, 0x4876, { 0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa } };

static const char *hr_name(HRESULT hr)
{
    switch ((ULONG)hr)
    {
        case 0: return "S_OK"; case 0x087A0001: return "DXGI_STATUS_OCCLUDED";
        case 0x887A0001: return "DXGI_ERROR_INVALID_CALL"; case 0x887A0005: return "DXGI_ERROR_DEVICE_REMOVED";
        case 0x887A0006: return "DXGI_ERROR_DEVICE_HUNG"; case 0x887A0007: return "DXGI_ERROR_DEVICE_RESET";
        case 0x887A0004: return "DXGI_ERROR_UNSUPPORTED"; case 0x887A000A: return "DXGI_ERROR_WAS_STILL_DRAWING";
        case 0x887A0020: return "DXGI_ERROR_ACCESS_DENIED"; case 0x80004001: return "E_NOTIMPL";
        case 0x80004005: return "E_FAIL"; case 0x80070057: return "E_INVALIDARG"; default: return "?";
    }
}

static HRESULT WINAPI my_SC_Present(void *self, UINT interval, UINT flags)
{
    HRESULT hr;
    if (no_tearing && (flags & 0x200))
    {
        static LONG said;
        flags &= ~0x200u;
        if (InterlockedIncrement(&said) == 1)
            logf_("  NG4_NO_TEARING: DXGI_PRESENT_ALLOW_TEARING stripped from Present (interval %u kept)", interval);
    }
    hr = real_SC_Present(self, interval, flags);
    static LONG n, bad;
    LONG i = InterlockedIncrement(&n);
    /* D3DMetal's own count of presentations, asked of the swap chain itself
     * (IDXGISwapChain::GetLastPresentCount, slot 17). A Present that returns
     * S_OK while this number stands still is a presentation accepted and
     * dropped inside the toolkit; one that advances is a frame that reached a
     * drawable, whatever the screen shows. */
    if (i == 5 || i == 300 || i == 900 || i == 1800 || i == 3600)
    {
        UINT count = 0xFFFFFFFFu;
        HRESULT (WINAPI *glpc)(void *, UINT *) = (HRESULT (WINAPI *)(void *, UINT *))(*(void ***)self)[17];
        HRESULT h2 = glpc(self, &count);
        logf_("  GetLastPresentCount after Present #%ld -> 0x%08lx, count %u%s", i, h2, count,
              (SUCCEEDED(h2) && count == 0) ? "  << D3DMetal has presented NOTHING" :
              (SUCCEEDED(h2) && count > 0) ? "  << D3DMetal counts presented frames" : "");
    }
    if (FAILED(hr) || hr == 0x087A0001) { if (InterlockedIncrement(&bad) <= 5 || (bad % 300) == 0)
        logf_("  Present #%ld -> 0x%08lx %s  (interval %u flags 0x%x)", i, hr, hr_name(hr), interval, flags); }
    else if (i <= 5 || (i % 300) == 0)
        logf_("  Present #%ld -> S_OK  (interval %u flags 0x%x)  << something IS being presented", i, interval, flags);
    return hr;
}
static HRESULT WINAPI my_SC_Present1(void *self, UINT interval, UINT flags, const void *params)
{
    HRESULT hr;
    if (no_tearing && (flags & 0x200)) flags &= ~0x200u;
    hr = real_SC_Present1(self, interval, flags, params);
    static LONG n, bad;
    LONG i = InterlockedIncrement(&n);
    if (FAILED(hr) || hr == 0x087A0001) { if (InterlockedIncrement(&bad) <= 5 || (bad % 300) == 0)
        logf_("  Present1 #%ld -> 0x%08lx %s  (interval %u flags 0x%x)", i, hr, hr_name(hr), interval, flags); }
    else if (i <= 5 || (i % 300) == 0)
        logf_("  Present1 #%ld -> S_OK  (interval %u flags 0x%x)  << something IS being presented", i, interval, flags);
    return hr;
}
static HRESULT WINAPI my_SC_GetBuffer(void *self, UINT idx, const GUID *iid, void **out)
{
    HRESULT hr = real_SC_GetBuffer(self, idx, iid, out);
    static LONG n; if (InterlockedIncrement(&n) <= 4 || FAILED(hr))
        logf_("  swapchain GetBuffer(%u) -> 0x%08lx %s", idx, hr, hr_name(hr));
    return hr;
}
static HRESULT WINAPI my_SC_SetFullscreen(void *self, BOOL fs, void *target)
{
    HRESULT hr;
    if (force_windowed && fs)
    {
        logf_("  NG4_FORCE_WINDOWED: SetFullscreenState(FULLSCREEN) answered S_OK without switching");
        return 0;
    }
    hr = real_SC_SetFullscreen(self, fs, target);
    logf_("  swapchain SetFullscreenState(%s) -> 0x%08lx %s", fs ? "FULLSCREEN" : "windowed", hr, hr_name(hr));
    return hr;
}
static HRESULT WINAPI my_SC_ResizeBuffers(void *self, UINT count, UINT w, UINT h, UINT fmt, UINT flags)
{
    HRESULT hr;
    UINT asked = flags;
    if (no_tearing)  flags &= ~0x800u;
    if (no_waitable) flags &= ~0x40u;
    if (flags != asked)
        logf_("  swap chain flags 0x%x -> 0x%x (%s%s)", asked, flags,
              (asked & ~flags & 0x800) ? "tearing off " : "", (asked & ~flags & 0x40) ? "waitable off" : "");
    hr = real_SC_ResizeBuffers(self, count, w, h, fmt, flags);
    logf_("  swapchain ResizeBuffers(%u x %ux%u fmt %u flags 0x%x) -> 0x%08lx %s", count, w, h, fmt, flags, hr, hr_name(hr));
    return hr;
}

static void watch_swapchain(void *sc)
{
    static void *p, *p1, *gb, *fs, *rb;
    void *sc1 = NULL;
    if (!sc || real_SC_Present) return;
    real_SC_Present       = (HRESULT (WINAPI *)(void *, UINT, UINT))(*(void ***)sc)[8];
    real_SC_GetBuffer     = (HRESULT (WINAPI *)(void *, UINT, const GUID *, void **))(*(void ***)sc)[9];
    real_SC_SetFullscreen = (HRESULT (WINAPI *)(void *, BOOL, void *))(*(void ***)sc)[10];
    real_SC_ResizeBuffers = (HRESULT (WINAPI *)(void *, UINT, UINT, UINT, UINT, UINT))(*(void ***)sc)[13];
    if (!patch_slot("swapchain Present", sc, 8, (void *)my_SC_Present, &p)) real_SC_Present = NULL;
    if (!patch_slot("swapchain GetBuffer", sc, 9, (void *)my_SC_GetBuffer, &gb)) real_SC_GetBuffer = NULL;
    if (!patch_slot("swapchain SetFullscreenState", sc, 10, (void *)my_SC_SetFullscreen, &fs)) real_SC_SetFullscreen = NULL;
    if (!patch_slot("swapchain ResizeBuffers", sc, 13, (void *)my_SC_ResizeBuffers, &rb)) real_SC_ResizeBuffers = NULL;
    /* Present1 lives on IDXGISwapChain1; only touch slot 22 on an object that says it is one. */
    {
        HRESULT (WINAPI *qi)(void *, const GUID *, void **) = (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)sc)[0];
        if (SUCCEEDED(qi(sc, &IID_IDXGISwapChain1_, &sc1)) && sc1)
        {
            real_SC_Present1 = (HRESULT (WINAPI *)(void *, UINT, UINT, const void *))(*(void ***)sc1)[22];
            if (!patch_slot("swapchain Present1", sc1, 22, (void *)my_SC_Present1, &p1)) real_SC_Present1 = NULL;
            ((ULONG (WINAPI *)(void *))(*(void ***)sc1)[2])(sc1);
        }
        else logf_("  swap chain is not IDXGISwapChain1 -- Present1 not watched");
    }
}

static void log_desc1(const char *what, const void *desc, HRESULT hr)
{
    const UINT *d = (const UINT *)desc;
    if (desc && readable_(desc, 44))
        logf_("  %s: %ux%u fmt %u buffers %u swapeffect %u flags 0x%x -> 0x%08lx %s",
              what, d[0], d[1], d[2], d[7], d[9], d[11], hr, hr_name(hr));
    else logf_("  %s -> 0x%08lx %s", what, hr, hr_name(hr));
}

/* Which API the swap chain belongs to. For D3D12 the "device" handed to
 * CreateSwapChain must be an ID3D12CommandQueue; for D3D11 it is the device.
 * Asked of the object itself rather than inferred from the swap effect. */
static const char *which_device_(void *dev)
{
    static const GUID IID_ID3D12CommandQueue_ = { 0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed } };
    static const GUID IID_ID3D11Device_      = { 0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };
    static const GUID IID_ID3D12Device_      = { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
    void *o = NULL;
    HRESULT (WINAPI *qi)(void *, const GUID *, void **);
    if (!dev || !readable_(dev, 8)) return "no device";
    qi = (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)dev)[0];
    if (SUCCEEDED(qi(dev, &IID_ID3D12CommandQueue_, &o)) && o) { ((ULONG (WINAPI *)(void *))(*(void ***)o)[2])(o); return "D3D12 command queue"; }
    if (SUCCEEDED(qi(dev, &IID_ID3D11Device_, &o)) && o)      { ((ULONG (WINAPI *)(void *))(*(void ***)o)[2])(o); return "D3D11 device"; }
    if (SUCCEEDED(qi(dev, &IID_ID3D12Device_, &o)) && o)      { ((ULONG (WINAPI *)(void *))(*(void ***)o)[2])(o); return "D3D12 device (not a queue!)"; }
    return "unknown object";
}
static const char *swapeffect_name(UINT e)
{ return e == 0 ? "DISCARD" : e == 1 ? "SEQUENTIAL" : e == 3 ? "FLIP_SEQUENTIAL" : e == 4 ? "FLIP_DISCARD" : "?"; }

static HRESULT WINAPI my_F_CreateSwapChain(void *self, void *dev, const void *desc, void **out)
{
    HRESULT hr;
    BYTE copy[72];
    if ((no_tearing || no_waitable || flip_model || force_windowed) && desc && readable_(desc, 72))
    {
        UINT *f, *effect, *windowed;
        memcpy(copy, desc, 72);
        f = (UINT *)(copy + 60);          /* Flags */
        effect = (UINT *)(copy + 56);     /* SwapEffect */
        windowed = (UINT *)(copy + 52);   /* Windowed */
        if (no_tearing)  *f &= ~0x800u;
        if (no_waitable) *f &= ~0x40u;
        if (flip_model && (*effect == 0 || *effect == 1))
        { logf_("  NG4_FLIP_MODEL: swap effect %u -> 4 (FLIP_DISCARD)", *effect); *effect = 4; }
        if (force_windowed && !*windowed)
        { logf_("  NG4_FORCE_WINDOWED: windowed 0 -> 1"); *windowed = 1; }
        desc = copy;
    }
    hr = real_F_CreateSwapChain(self, dev, desc, out);
    const UINT *d = (const UINT *)desc;   /* DXGI_SWAP_CHAIN_DESC: BufferDesc{w,h,rr{2},fmt,so,sc} sample{2} usage count hwnd windowed effect flags */
    if (desc && readable_(desc, 72))
        logf_("  CreateSwapChain on %s (%p): %ux%u fmt %u buffers %u usage 0x%x hwnd %p windowed %u swapeffect %u (%s) flags 0x%x -> 0x%08lx %s",
              which_device_(dev), dev, d[0], d[1], d[4], d[10], d[9], *(void **)(d + 11), d[13], d[14], swapeffect_name(d[14]), d[15], hr, hr_name(hr));
    else logf_("  CreateSwapChain -> 0x%08lx %s", hr, hr_name(hr));
    if (SUCCEEDED(hr) && out && *out) watch_swapchain(*out);
    return hr;
}
static HRESULT WINAPI my_F_CreateSwapChainForHwnd(void *self, void *dev, HWND hwnd, const void *desc,
                                                  const void *fsdesc, void *restrict_out, void **out)
{
    HRESULT hr = real_F_CreateSwapChainForHwnd(self, dev, hwnd, desc, fsdesc, restrict_out, out);
    logf_("  CreateSwapChainForHwnd on %s, hwnd %p", which_device_(dev), (void *)hwnd);
    log_desc1("CreateSwapChainForHwnd", desc, hr);
    if (SUCCEEDED(hr) && out && *out) watch_swapchain(*out);
    return hr;
}
static HRESULT WINAPI my_F_CreateSwapChainForComposition(void *self, void *dev, const void *desc, void *restrict_out, void **out)
{
    HRESULT hr = real_F_CreateSwapChainForComposition(self, dev, desc, restrict_out, out);
    log_desc1("CreateSwapChainForComposition", desc, hr);
    if (SUCCEEDED(hr) && out && *out) watch_swapchain(*out);
    return hr;
}

static void watch_factory(void *fac, const char *how, HRESULT hr)
{
    static void *a, *b, *c;
    logf_("%s -> 0x%08lx %s", how, hr, hr_name(hr));
    if (FAILED(hr) || !fac || real_F_CreateSwapChain) return;
    real_F_CreateSwapChain = (HRESULT (WINAPI *)(void *, void *, const void *, void **))(*(void ***)fac)[10];
    if (!patch_slot("factory CreateSwapChain", fac, 10, (void *)my_F_CreateSwapChain, &a)) real_F_CreateSwapChain = NULL;
    /* slots 15 and 24 exist only on IDXGIFactory2: every factory D3DMetal hands out is one
     * (CreateDXGIFactory2 is what modern titles call), but check the size of the vtable
     * by asking for the interface rather than assuming. */
    {
        static const GUID IID_IDXGIFactory2_ = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
        void *f2 = NULL;
        HRESULT (WINAPI *qi)(void *, const GUID *, void **) = (HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)fac)[0];
        if (SUCCEEDED(qi(fac, &IID_IDXGIFactory2_, &f2)) && f2)
        {
            real_F_CreateSwapChainForHwnd = (HRESULT (WINAPI *)(void *, void *, HWND, const void *, const void *, void *, void **))(*(void ***)f2)[15];
            real_F_CreateSwapChainForComposition = (HRESULT (WINAPI *)(void *, void *, const void *, void *, void **))(*(void ***)f2)[24];
            if (!patch_slot("factory CreateSwapChainForHwnd", f2, 15, (void *)my_F_CreateSwapChainForHwnd, &b)) real_F_CreateSwapChainForHwnd = NULL;
            if (!patch_slot("factory CreateSwapChainForComposition", f2, 24, (void *)my_F_CreateSwapChainForComposition, &c)) real_F_CreateSwapChainForComposition = NULL;
            ((ULONG (WINAPI *)(void *))(*(void ***)f2)[2])(f2);
        }
        else logf_("  factory is not IDXGIFactory2");
    }
}
/* Patch the factory CLASS by making a factory of our own.
 *
 * The title's own CreateDXGIFactory2 never went through the import hook nor
 * GetProcAddress (Streamline's interposer sits in front of DXGI here, as it
 * did for Ronin). The vtable is per class, not per instance, so a factory we
 * create ourselves and patch is enough: whatever path the game takes to its
 * factory, its CreateSwapChain* calls land in the same slots. */
static void seed_dxgi_factory_watch(void)
{
    static const GUID IID_IDXGIFactory1_ = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HRESULT (WINAPI *mk)(const GUID *, void **) =
        dxgi ? (void *)(real_GetProcAddress ? real_GetProcAddress : GetProcAddress)(dxgi, "CreateDXGIFactory1") : NULL;
    void *fac = NULL;
    if (!mk) { logf_("  seed: no CreateDXGIFactory1 in dxgi.dll"); return; }
    watch_factory(NULL, "seed: about to make our own factory", 0);
    if (SUCCEEDED(mk(&IID_IDXGIFactory1_, &fac)) && fac)
    {
        watch_factory(fac, "seed: our own IDXGIFactory1", 0);
        ((ULONG (WINAPI *)(void *))(*(void ***)fac)[2])(fac);
    }
    else logf_("  seed: CreateDXGIFactory1 failed");
}

static HRESULT WINAPI my_CreateDXGIFactory(const GUID *iid, void **out)
{ HRESULT hr = real_CreateDXGIFactory(iid, out); watch_factory(out ? *out : NULL, "CreateDXGIFactory", hr); return hr; }
static HRESULT WINAPI my_CreateDXGIFactory1(const GUID *iid, void **out)
{ HRESULT hr = real_CreateDXGIFactory1(iid, out); watch_factory(out ? *out : NULL, "CreateDXGIFactory1", hr); return hr; }
static HRESULT WINAPI my_CreateDXGIFactory2(UINT flags, const GUID *iid, void **out)
{ HRESULT hr = real_CreateDXGIFactory2(flags, iid, out); watch_factory(out ? *out : NULL, "CreateDXGIFactory2", hr); return hr; }

static HRESULT (WINAPI *real_CreateCommittedWatch)(void *, const void *, UINT, const void *,
                                                     UINT, const void *, const GUID *, void **);

static const char *fmt_name(UINT f)
{
    switch (f)
    {
        case 0: return "UNKNOWN"; case 28: return "RGBA8"; case 87: return "BGRA8";
        case 61: return "R8"; case 49: return "R8G8"; case 103: return "NV12";
        case 104: return "P010"; case 56: return "R16"; case 35: return "R16G16";
        case 10: return "RGBA16F"; case 2: return "RGBA32F"; default: return "?";
    }
}

/* Placed resources and the heaps they live in. The committed hook saw nothing
 * frame-sized after the movie opened on 2026-09-01; engines that suballocate
 * create their textures with CreatePlacedResource on heaps of their own, which
 * that hook cannot see. Same rule: forwarded untouched, logged only. */
static HRESULT (WINAPI *real_CreatePlacedWatch)(void *, void *, UINT64, const void *, UINT,
                                                const void *, const GUID *, void **);
static HRESULT (WINAPI *real_CreateHeapWatch)(void *, const void *, const GUID *, void **);

static HRESULT WINAPI my_CreateHeapWatch(void *self, const void *desc, const GUID *iid, void **out)
{
    HRESULT hr = real_CreateHeapWatch(self, desc, iid, out);
    if (desc && readable_(desc, 24))
    {
        unsigned long long size = *(const unsigned long long *)desc;
        UINT htype = *(const UINT *)((const BYTE *)desc + 8);
        /* Uncapped: heaps are few, and the two the movie used on 2026-09-01
         * were created before a 20-line cap and so their types went unrecorded. */
        if (size >= 2097152)
            logf_("  CreateHeap: %llu MB heap %s -> 0x%08lx  (%p)", size >> 20,
                  htype == 1 ? "DEFAULT" : htype == 2 ? "UPLOAD" : htype == 3 ? "READBACK" : "CUSTOM",
                  hr, out ? *out : NULL);
    }
    return hr;
}

static HRESULT WINAPI my_CreatePlacedWatch(void *self, void *heap, UINT64 offset, const void *desc,
                                           UINT state, const void *clear, const GUID *iid, void **out)
{
    HRESULT hr = real_CreatePlacedWatch(self, heap, offset, desc, state, clear, iid, out);
    static LONG total, shown;
    InterlockedIncrement(&total);
    if (desc && readable_(desc, 52))
    {
        const BYTE *d = (const BYTE *)desc;
        UINT dim = *(const UINT *)d;
        unsigned long long width = *(const unsigned long long *)(d + 16);
        UINT height = *(const UINT *)(d + 24);
        UINT16 mips = *(const UINT16 *)(d + 30);
        UINT fmt = *(const UINT *)(d + 32), layout = *(const UINT *)(d + 44), flags = *(const UINT *)(d + 48);
        BOOL frame_sized = (dim == 3 && width >= 640 && height >= 360) || (dim == 1 && width >= 524288);
        if (watch_movie_copy && SUCCEEDED(hr) && out && *out)
        {
            if (dim == 3 && width == 1920 && height == 1080 && fmt == 29 && !movie_tex_)
            { movie_tex_ = *out; logf_("  MOVIE-TEX is %p", movie_tex_); }
            else if (dim == 1 && width == 8294400 && !movie_buf_)
            { movie_buf_ = *out; logf_("  MOVIE-BUF is %p", movie_buf_); watch_movie_resource_vtable(*out); }
        }
        if (frame_sized && (reader_exists_ || InterlockedIncrement(&shown) <= 40))
            logf_("  CreatePlacedResource #%ld: %s %llux%u mips %u fmt %u (%s) layout %u flags 0x%x "
                  "on heap %p @%llu -> 0x%08lx",
                  InterlockedCompareExchange(&total, 0, 0),
                  dim == 1 ? "buffer" : dim == 3 ? "tex2d" : dim == 4 ? "tex3d" : "tex1d",
                  width, height, mips, fmt, fmt_name(fmt), layout, flags, heap, offset, hr);
    }
    return hr;
}

/* --- the movie's buffer: what the game writes into it --- */
static HRESULT (WINAPI *real_ResMap)(void *, UINT, const void *, void **);
static void    (WINAPI *real_ResUnmap)(void *, UINT, const void *);
static void *movie_buf_data_;

static HRESULT WINAPI my_ResMap(void *self, UINT sub, const void *range, void **data)
{
    HRESULT hr = real_ResMap(self, sub, range, data);
    if (self == movie_buf_)
    {
        static LONG n;
        LONG i = InterlockedIncrement(&n);
        if (data && SUCCEEDED(hr)) movie_buf_data_ = *data;
        if (i <= 3 || i == 50)
            logf_("  movie buffer Map #%ld -> 0x%08lx, data %p", i, hr, data ? *data : NULL);
    }
    return hr;
}

static void WINAPI my_ResUnmap(void *self, UINT sub, const void *range)
{
    if (self == movie_buf_ && movie_buf_data_)
    {
        static LONG n;
        LONG i = InterlockedIncrement(&n);
        if (i <= 3 || i == 50)
        {
            /* RGBA, 8,294,400 bytes: sample 512 pixels across the frame. */
            const BYTE *p = (const BYTE *)movie_buf_data_;
            unsigned lo = 255, hi = 0; unsigned long long sum = 0; int k;
            if (readable_(p, 8294400))
            {
                for (k = 0; k < 512; k++)
                {
                    const BYTE *px = p + (unsigned long long)k * (8294400 / 512);
                    unsigned g = (px[0] + px[1] + px[2]) / 3;
                    if (g < lo) lo = g; if (g > hi) hi = g; sum += g;
                }
                logf_("  movie buffer Unmap #%ld: RGB min %u max %u mean %u%s", i, lo, hi,
                      (unsigned)(sum / 512), hi == 0 ? "  << the game wrote BLACK into it"
                                                       : "  << the game wrote a picture into it");
            }
            else logf_("  movie buffer Unmap #%ld: data not readable", i);
        }
    }
    real_ResUnmap(self, sub, range);
}

static void watch_movie_resource_vtable(void *res)
{
    static void *m, *u;
    if (!res || real_ResMap) return;
    real_ResMap   = (HRESULT (WINAPI *)(void *, UINT, const void *, void **))(*(void ***)res)[8];
    real_ResUnmap = (void (WINAPI *)(void *, UINT, const void *))(*(void ***)res)[9];
    if (!patch_slot("ID3D12Resource Map (movie)", res, 8, (void *)my_ResMap, &m)) real_ResMap = NULL;
    if (!patch_slot("ID3D12Resource Unmap (movie)", res, 9, (void *)my_ResUnmap, &u)) real_ResUnmap = NULL;
}

/* --- command lists: copies that touch the movie's objects --- */
static void (WINAPI *real_CopyTextureRegion)(void *, const void *, UINT, UINT, UINT, const void *, const void *);
static void (WINAPI *real_CopyResource)(void *, void *, void *);
static HRESULT (WINAPI *real_CreateCommandListW)(void *, UINT, UINT, void *, void *, const GUID *, void **);

static const char *loc_str(const void *loc, char *buf, size_t n)
{
    const BYTE *l = (const BYTE *)loc;
    void *res = *(void **)l; UINT type = *(const UINT *)(l + 8);
    if (type == 1)
        snprintf(buf, n, "%s(footprint off %llu fmt %u %ux%u pitch %u)",
                 res == movie_buf_ ? "MOVIE-BUF" : res == movie_tex_ ? "MOVIE-TEX" : "other",
                 *(const unsigned long long *)(l + 16), *(const UINT *)(l + 24),
                 *(const UINT *)(l + 28), *(const UINT *)(l + 32), *(const UINT *)(l + 40));
    else
        snprintf(buf, n, "%s(subresource %u)",
                 res == movie_buf_ ? "MOVIE-BUF" : res == movie_tex_ ? "MOVIE-TEX" : "other",
                 *(const UINT *)(l + 16));
    return buf;
}

static void WINAPI my_CopyTextureRegion(void *self, const void *dst, UINT x, UINT y, UINT z,
                                        const void *src, const void *box)
{
    if (dst && src && readable_(dst, 48) && readable_(src, 48))
    {
        void *dr = *(void **)dst, *sr = *(void **)src;
        if (dr == movie_tex_ || dr == movie_buf_ || sr == movie_tex_ || sr == movie_buf_)
        {
            static LONG n; LONG i = InterlockedIncrement(&n);
            if (i <= 3 || i == 50 || i == 300)
            {
                char a[96], b[96];
                logf_("  CopyTextureRegion #%ld: dst %s at (%u,%u,%u)  src %s  box %s",
                      i, loc_str(dst, a, sizeof(a)), x, y, z, loc_str(src, b, sizeof(b)),
                      box ? "yes" : "none");
            }
        }
    }
    real_CopyTextureRegion(self, dst, x, y, z, src, box);
}

static void WINAPI my_CopyResource(void *self, void *dst, void *src)
{
    if (dst == movie_tex_ || dst == movie_buf_ || src == movie_tex_ || src == movie_buf_)
    {
        static LONG n; LONG i = InterlockedIncrement(&n);
        if (i <= 3 || i == 50)
            logf_("  CopyResource #%ld: dst %s src %s", i,
                  dst == movie_tex_ ? "MOVIE-TEX" : dst == movie_buf_ ? "MOVIE-BUF" : "other",
                  src == movie_tex_ ? "MOVIE-TEX" : src == movie_buf_ ? "MOVIE-BUF" : "other");
    }
    real_CopyResource(self, dst, src);
}

static HRESULT WINAPI my_CreateCommandListW(void *self, UINT node, UINT type, void *alloc,
                                            void *pso, const GUID *iid, void **out)
{
    HRESULT hr = real_CreateCommandListW(self, node, type, alloc, pso, iid, out);
    if (SUCCEEDED(hr) && out && *out && !real_CopyTextureRegion)
    {
        static void *c1, *c2;
        void **vt = *(void ***)*out;
        real_CopyTextureRegion = (void (WINAPI *)(void *, const void *, UINT, UINT, UINT, const void *, const void *))vt[16];
        real_CopyResource      = (void (WINAPI *)(void *, void *, void *))vt[17];
        if (!patch_slot("command list CopyTextureRegion (movie)", *out, 16, (void *)my_CopyTextureRegion, &c1))
            real_CopyTextureRegion = NULL;
        if (!patch_slot("command list CopyResource (movie)", *out, 17, (void *)my_CopyResource, &c2))
            real_CopyResource = NULL;
    }
    return hr;
}

static HRESULT WINAPI my_CreateCommittedWatch(void *self, const void *heap, UINT hflags,
                                              const void *desc, UINT state, const void *clear,
                                              const GUID *iid, void **out)
{
    HRESULT hr = real_CreateCommittedWatch(self, heap, hflags, desc, state, clear, iid, out);
    static LONG total, shown;
    InterlockedIncrement(&total);
    if (desc && readable_(desc, 52) && heap && readable_(heap, 4))
    {
        const BYTE *d = (const BYTE *)desc;
        UINT dim = *(const UINT *)d;
        unsigned long long width = *(const unsigned long long *)(d + 16);
        UINT height = *(const UINT *)(d + 24);
        UINT16 depth = *(const UINT16 *)(d + 28), mips = *(const UINT16 *)(d + 30);
        UINT fmt = *(const UINT *)(d + 32), layout = *(const UINT *)(d + 44), flags = *(const UINT *)(d + 48);
        UINT htype = *(const UINT *)heap;
        BOOL frame_sized = (dim == 3 && width >= 640 && height >= 360) || (dim == 1 && width >= 524288);
        /* Before the reader exists, forty lines of context; once the game has
         * opened the movie, every frame-sized resource, uncapped. The first
         * build spent 22 of its 40 lines on loading-screen render targets and
         * would have gone quiet exactly where the answer is. */
        if (frame_sized && (reader_exists_ || InterlockedIncrement(&shown) <= 40))
            logf_("  CreateCommittedResource #%ld: %s %llux%u x%u mips %u fmt %u (%s) layout %u flags 0x%x "
                  "heap %s state 0x%x -> 0x%08lx",
                  InterlockedCompareExchange(&total, 0, 0),
                  dim == 1 ? "buffer" : dim == 3 ? "tex2d" : dim == 4 ? "tex3d" : "tex1d",
                  width, height, depth, mips, fmt, fmt_name(fmt), layout, flags,
                  htype == 1 ? "DEFAULT" : htype == 2 ? "UPLOAD" : htype == 3 ? "READBACK" : "CUSTOM",
                  state, hr);
    }
    return hr;
}

static HRESULT WINAPI my_D3D12CreateDevice(void *adapter, UINT level,
                                           const GUID *iid, void **device)
{
    HRESULT hr = real_D3D12CreateDevice(adapter, level, iid, device);
    logf_("D3D12CreateDevice(featureLevel=0x%lx) -> 0x%08lx", level, hr);
    /*
     * Leave D3D12 alone.
     *
     * On CrossOver 26.3 every one of the patches below is refused --
     * VirtualProtect returns error 87 against that vtable -- and the title
     * reaches its menu. On Preview they all take, and it stalls immediately
     * after the last of them. That is the cleanest remaining difference between
     * a run that works and a run that does not, and it is ours rather than the
     * system's, so it is the one to remove first.
     *
     * Set NG4_PATCH_D3D12=1 to put them back.
     */
    if (!caps_like_3)
    {
        /* Read here as well as in the worker. NG4 starts two processes and the
         * first one created its device before the worker thread had parsed the
         * switches, so the mode logged "left untouched" there and applied only
         * in the second -- whichever of the two plays the movie is a matter of
         * timing, and a switch that depends on timing is not a switch. */
        char v[8] = { 0 };
        if (GetEnvironmentVariableA("NG4_CAPS_LIKE_3", v, sizeof(v)) && v[0] == '1')
            caps_like_3 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_WATCH_D3D12_RESOURCES", v, sizeof(v)) && v[0] == '1')
            watch_d3d12_resources = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_WATCH_MOVIE_COPY", v, sizeof(v)) && v[0] == '1')
            watch_movie_copy = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_WATCH_PRESENT", v, sizeof(v)) && v[0] == '1')
            watch_present = TRUE;
    }
    if (!watch_d3d12_resources)
    {
        char v[8] = { 0 };
        if (GetEnvironmentVariableA("NG4_WATCH_D3D12_RESOURCES", v, sizeof(v)) && v[0] == '1')
            watch_d3d12_resources = TRUE;
    }
    if (!patch_d3d12 && watch_d3d12_resources && SUCCEEDED(hr) && device && *device)
    {
        static void *cw;
        void **vt2 = *(void ***)*device;
        real_CreateCommittedWatch = (HRESULT (WINAPI *)(void *, const void *, UINT, const void *, UINT,
                                                        const void *, const GUID *, void **))
                                    vt2[SLOT_D3D12_CREATECOMMITTED];
        if (!patch_slot("d3d12 CreateCommittedResource (watch only)", *device,
                        SLOT_D3D12_CREATECOMMITTED, (void *)my_CreateCommittedWatch, &cw))
            real_CreateCommittedWatch = NULL;
        {
            static void *ch, *cp;
            real_CreateHeapWatch = (HRESULT (WINAPI *)(void *, const void *, const GUID *, void **))vt2[28];
            if (!patch_slot("d3d12 CreateHeap (watch only)", *device, 28, (void *)my_CreateHeapWatch, &ch))
                real_CreateHeapWatch = NULL;
            real_CreatePlacedWatch = (HRESULT (WINAPI *)(void *, void *, UINT64, const void *, UINT,
                                                         const void *, const GUID *, void **))vt2[29];
            if (!patch_slot("d3d12 CreatePlacedResource (watch only)", *device, 29,
                            (void *)my_CreatePlacedWatch, &cp))
                real_CreatePlacedWatch = NULL;
        }
        {
            char v[8] = { 0 };
            if (!watch_movie_copy && GetEnvironmentVariableA("NG4_WATCH_MOVIE_COPY", v, sizeof(v)) && v[0] == '1')
                watch_movie_copy = TRUE;
        }
        if (watch_movie_copy)
        {
            static void *cl;
            real_CreateCommandListW = (HRESULT (WINAPI *)(void *, UINT, UINT, void *, void *, const GUID *, void **))vt2[12];
            if (!patch_slot("d3d12 CreateCommandList (movie copy watch)", *device, 12,
                            (void *)my_CreateCommandListW, &cl))
                real_CreateCommandListW = NULL;
        }
    }
    if (!watch_caps)
    {
        char v[8] = { 0 };
        if (GetEnvironmentVariableA("NG4_WATCH_CAPS", v, sizeof(v)) && v[0] == '1')
            watch_caps = TRUE;
    }
    if (!patch_d3d12 && (caps_like_3 || watch_caps))
    {
        if (SUCCEEDED(hr) && device && *device)
        {
            static void *cf1;
            void **vt1 = *(void ***)*device;
            real_CheckFeature = (HRESULT (WINAPI *)(void *, UINT, void *, UINT))
                                vt1[SLOT_D3D12_CHECKFEATURE];
            if (patch_slot(caps_like_3 ? "d3d12 CheckFeatureSupport (caps-like-3.0, the only slot)"
                                       : "d3d12 CheckFeatureSupport (watch only)",
                           *device, SLOT_D3D12_CHECKFEATURE, (void *)my_CheckFeature, &cf1))
            {
                if (caps_like_3)
                    logf_("  NG4_CAPS_LIKE_3: the four 4.0b2-only capabilities will be answered as 3.0 does");
            }
            else
                real_CheckFeature = NULL;
        }
        return hr;
    }
    if (!patch_d3d12) { logf_("  d3d12: left untouched"); return hr; }
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

    /*
     * Ask for CPU decompression, because the GPU kind cannot be had here.
     *
     * These fields are named Disable*, so the zeros the game passes mean the
     * features are ON. It is asking for GPU decompression through the
     * metacommand path -- and D3DMetal refuses EnumerateMetaCommands for this
     * executable by name, through its per-title override table. CreateQueue
     * then returns DXGI_ERROR_UNSUPPORTED, hands back a null queue, and the
     * game stores it without checking and calls through it.
     *
     * This was tried once before and recorded as changing nothing. The
     * conditions have moved since: OPTIONS17 is now answered yes, the staging
     * buffer is reduced, and the factory is no longer being refused outright.
     * So it is worth one measurement rather than an assumption inherited from a
     * different configuration.
     *
     * NG4_KEEP_GPU_DECOMP=1 passes the game's own values through instead.
     */
    if (force_cpu_decompression)
    {
        cfg.DisableGpuDecompressionMetacommand = TRUE;
        cfg.DisableGpuDecompression = TRUE;
        logf_("  forcing CPU decompression: metacmd and gpuDecomp both disabled");
    }

    /* ForceMappingLayer was tried here and made things worse: the game died
     * earlier, before CreateQueue, and with a different kind of fault. The
     * compatibility layer is evidently not a drop-in for what this engine does,
     * so it stays off and the native path stays the thing to explain. */

    hr = real_DStorageSetConfiguration1(&cfg);
    logf_("  config out -> 0x%08lx", hr);
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

static BOOL small_staging;
static HRESULT (WINAPI *real_SetStaging)(void *, UINT32);

static HRESULT WINAPI my_SetStaging(void *self, UINT32 size)
{
    HRESULT hr;
    /*
     * Pass the game's own size through.
     *
     * Capping this at 32 MB was an experiment and was left forced on, so every
     * run since has been asking DirectStorage to build a queue against a
     * staging buffer an eighth of what the title asked for -- immediately
     * before the call that refuses. NG4_SMALL_STAGING=1 brings the cap back.
     */
    if (small_staging && size > STAGING_DEFAULT)
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

/*
 * Refusing the factory was an experiment, and it stopped being one.
 *
 * It was left forced on, so every run since has measured the game's fallback
 * I/O path rather than DirectStorage -- including two runs made after the
 * MFTEnumEx gate was answered for the first time, which is exactly when the
 * DirectStorage path became worth seeing. Both stalled identically, which does
 * establish that the fallback path does not work either, but it was not the
 * question being asked.
 *
 * Opt in with NG4_REFUSE_DSTORAGE=1.
 */
static BOOL refuse_dstorage_factory;

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
    if (refuse_dstorage_factory && SUCCEEDED(hr) && out && *out)
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
        /* Catch the crash ourselves.
     *
     * Wine writes a backtrace when a process dies, and this launcher sets
     * WINEDEBUG=-all, which silences it. Rather than argue with that, note the
     * exception here: this DLL is already inside the process, and a vectored
     * handler sees every exception before anything else does.
     *
     * Logged: the code, the address, and which module that address belongs to.
     * The module is the part that matters -- it says whether the title dies in
     * its own code, in the engine's, or in ours.
     *
     * First-chance exceptions are normal and common, so this reports only the
     * ones that are not: a C++ throw or a breakpoint says nothing. */
    AddVectoredExceptionHandler(1, note_exception);

    /* What the launcher actually handed this process.
     *
     * Reproducing a crash outside the launcher needs the same environment, and
     * macOS will not let one process read another's. This one is inside the
     * game, so it can simply ask. Named variables only: dumping everything
     * would put a user's paths and tokens in a log that gets shared. */
    {
        static const char *want[] = {
            "CX_GRAPHICS_BACKEND", "CX_BOTTLE", "D3DM_ENABLE_METALFX",
            "D3DM_MTL4", "DXMT_ENABLE_NVEXT", "DXMT_CONFIG", "WINEMSYNC",
            "WINEESYNC", "WINEDEBUG", "GST_PLUGIN_PATH", "GST_PLUGIN_SYSTEM_PATH",
            "GST_PLUGIN_SCANNER", "GST_REGISTRY", "MVK_CONFIG_LOG_LEVEL",
            "MTL_HUD_ENABLED", "WINEDLLOVERRIDES", "WINEDLLPATH",
            /* The debug redirection and our own switches.
             *
             * A run was spent on 2026-09-01 setting CX_DEBUGMSG and CX_LOG and
             * then being unable to tell whether they had arrived, because this
             * list did not name them -- the same list that was once read as if
             * it were the whole environment. Anything we ask the user to set has
             * to be visible here, or its absence cannot be told from its
             * silence. These are all ours, so none of them carries a secret. */
            "CX_DEBUGMSG", "CX_LOG", "D3DM_NOT_IMPLEMENTED", "D3DM_SHOW_HUD_STATS",
            "BEAST_REFUSE_D3D_MANAGER", "NG4_NO_D3D11_PATCH", "NG4_SELECT_STREAM",
            "NG4_PATCH_D3D12", "NG4_FORCE_PATCH", "NG4_ANSWER_MFT",
            "NG4_WITHHOLD_D3D_FROM_MFT", "NG4_PAINT_TEST", "NG4_CAPS_LIKE_3", "NG4_WATCH_D3D12_RESOURCES", "NG4_WATCH_CAPS", "NG4_WATCH_MOVIE_COPY", "NG4_WATCH_PRESENT", "NG4_NO_TEARING", "NG4_NO_WAITABLE", "NG4_FLIP_MODEL", "NG4_FORCE_WINDOWED",
        };
        char buf[512];
        size_t i;
        logf_("environment this process was given:");
        for (i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        {
            DWORD n = GetEnvironmentVariableA(want[i], buf, sizeof(buf) - 1);
            if (n && n < sizeof(buf))
                logf_("    %s=%s", want[i], buf);
        }
    }
    logf_("DirectStorage: %d of 4 entry points watched", n);
    }
}

static LONG CALLBACK note_exception(EXCEPTION_POINTERS *info)
{
    static LONG said;
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void *at = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : NULL;
    if (code == 0x80000101)   /* EXCEPTION_WINE_ASSERTION: a unix-side abort() */
        logf_("EXCEPTION_WINE_ASSERTION on thread %lu -- something on the unix side "
              "called abort()", GetCurrentThreadId());

    /* 0xe06d7363 is a C++ throw, 0x406d1388 a thread-name notification, and
     * both are ordinary traffic in a running game. */
    if (code == 0xE06D7363 || code == 0x406D1388 || code == EXCEPTION_BREAKPOINT)
        return EXCEPTION_CONTINUE_SEARCH;

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
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    {
        char v[8] = {0};
#ifdef NG4_FIX
        /* Built as the shipped fix rather than as a probe.
         *
         * The two levers this title needs are on unless something turns them
         * off, which is the opposite of the diagnostic build and is the whole
         * difference between a repair and an experiment. A fix that only works
         * when somebody has set two environment variables is not a fix: it is a
         * configuration that a bottle rewrite silently undoes -- and CrossOver
         * rewrites bottle configuration often enough that this was observed
         * seven times in one afternoon, each time surfacing as the game's own
         * "the VP9 codec is not installed" dialog.
         *
         * Both remain overridable, with "0", so a future measurement can still
         * take either of them away without a rebuild. */
        refuse_d3d_manager = TRUE;
        answer_mft_gate = TRUE;
        if (GetEnvironmentVariableA("BEAST_REFUSE_D3D_MANAGER", v, sizeof(v)) && v[0] == '0')
            refuse_d3d_manager = FALSE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_ANSWER_MFT", v, sizeof(v)) && v[0] == '0')
            answer_mft_gate = FALSE;
        v[0] = 0;
#else
        if (GetEnvironmentVariableA("BEAST_REFUSE_D3D_MANAGER", v, sizeof(v)) && v[0] == '1')
            refuse_d3d_manager = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_ANSWER_MFT", v, sizeof(v)) && v[0] == '1')
            answer_mft_gate = TRUE;
        v[0] = 0;
#endif
        if (GetEnvironmentVariableA("BEAST_FORCE_NV12", v, sizeof(v)) && v[0] == '1')
            restore_nv12 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_PATCH_D3D12", v, sizeof(v)) && v[0] == '1')
            patch_d3d12 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_CAPS_LIKE_3", v, sizeof(v)) && v[0] == '1')
            caps_like_3 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_NO_D3D11_PATCH", v, sizeof(v)) && v[0] == '1')
            no_d3d11_patch = TRUE;
        v[0] = 0;
        /* NG4_WITHHOLD_D3D_FROM_MFT=1: the game keeps its DXGI device manager --
         * which its presentation path appears to need -- but the decoder is not
         * told about it, so samples stay in system memory. The interception in
         * my_ProcessMessage existed without any way to turn it on. Whether it
         * fires at all is itself the first thing to learn: the decoder the game
         * enumerates through MFTEnumEx has never logged SET_D3D_MANAGER, so the
         * reader may be using one of its own. */
        if (GetEnvironmentVariableA("NG4_WITHHOLD_D3D_FROM_MFT", v, sizeof(v)) && v[0] == '1')
            withhold_d3d_from_mft = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_REFUSE_DSTORAGE", v, sizeof(v)) && v[0] == '1')
            refuse_dstorage_factory = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_CPU_DECOMP", v, sizeof(v)) && v[0] == '1')
            force_cpu_decompression = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_FAKE_OPTIONS17", v, sizeof(v)) && v[0] == '1')
            fake_options17 = TRUE;
        v[0] = 0;
        if (GetEnvironmentVariableA("NG4_SMALL_STAGING", v, sizeof(v)) && v[0] == '1')
            small_staging = TRUE;
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
            if (watch_present)
            {
                if ((was = hook_import("dxgi.dll", "CreateDXGIFactory",  (void *)my_CreateDXGIFactory)))  { *(void **)&real_CreateDXGIFactory  = was; got++; }
                if ((was = hook_import("dxgi.dll", "CreateDXGIFactory1", (void *)my_CreateDXGIFactory1))) { *(void **)&real_CreateDXGIFactory1 = was; got++; }
                if ((was = hook_import("dxgi.dll", "CreateDXGIFactory2", (void *)my_CreateDXGIFactory2))) { *(void **)&real_CreateDXGIFactory2 = was; got++; }
            }
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
    {
        char v[8] = { 0 };
        if (GetEnvironmentVariableA("NG4_WATCH_PRESENT", v, sizeof(v)) && v[0] == '1')
        {
            watch_present = TRUE;
            seed_dxgi_factory_watch();
        }
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
