/* electra-h264-fix -- make UE5's Electra play H.264 under D3DMetal.
 *
 * Three faults in a row, each hiding the next. Found by instrumenting the game
 * rather than reasoning about it -- four earlier attempts, all argued from
 * resemblance to another title, changed nothing because they acted downstream
 * of a decoder that never started. The probe this grew from is kept at
 * diagnostics/electra-probe.c and its logs are what every claim here rests on.
 *
 * 1. CrossOver's winegstreamer censors NV12 from
 *    transform_GetOutputAvailableType whenever it detects macOS; the strings
 *    sit adjacent in the shipping DLL and is_macos() is the only guard. The
 *    decoder then offers YV12, YV12, IYUV, I420, YUY2 and nothing else, while
 *    Electra's H.264 decoder accepts NV12 and nothing else -- so it walks that
 *    list, finds nothing, and destroys the decoder before a frame exists.
 *    Handing NV12 back by name is enough: the censoring is only in the getter,
 *    and SetOutputType validates against an array that still holds it.
 *
 * 2. Electra decides whether decoding is in software by asking its OWN
 *    platform handle, never the decoder -- so withholding the D3D manager from
 *    the MFT, which seemed obvious, could never have worked. Because
 *    winegstreamer still advertises MF_SA_D3D_AWARE, Electra builds itself a
 *    D3D11 device, answers "not software", and takes a branch demanding
 *    IMFDXGIBuffer on the output buffer, which no system-memory buffer can
 *    satisfy. Every frame is dropped in silence.
 *
 * 3. The same gate decides the frame height. Patch one call site and not the
 *    other and the renderer is handed a luma-only picture, so both go.
 *
 * winevideo reaches the same place by patching winegstreamer itself (its patch
 * 0005). This is that effect from inside the process: one game, reversible,
 * nothing outside the game folder touched.
 *
 * Offsets are checked against the bytes that should be there before anything
 * is written. A game update moves them, and then this does nothing and says so
 * rather than corrupting whatever it landed in.
 *
 * NOT for a game with anti-cheat or anti-tamper: it patches a running process.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>

#define ELECTRA_D3D12_VERSION   0x2EE0        /* 12000 */
#define UNREACHABLE_VERSION     0x7FFFFFFF
#include <stdio.h>

#define LOGFILE "C:\\ue5-media-fix.log"

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
static BOOL h264_wanted;   /* gates the Media Foundation half */

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

    /* The diagnostic version logs every call, which reached 284 KB in one
     * cutscene. What matters after the fact is the opening handful of lines,
     * so the rest is dropped rather than the call sites being edited -- this
     * code works, and rewriting it for tidiness is how working code stops
     * working. */
    if (InterlockedIncrement(&log_lines) > 200) return;

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
/* The height the picture actually is, as opposed to the height H.264 codes it
 * at. SetOutputType is called twice here: first with 1920x1080, the real frame,
 * then with 1920x1088, which is 1080 rounded up to the multiple of 16 the codec
 * works in. The buffer is laid out for the coded height, and telling a consumer
 * that a contiguous copy is 1088 rows long when its destination holds 1080 is
 * 23040 bytes past the end. The first height seen is the honest one. */
static UINT32 display_h;
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
        /* Once. Electra asks per frame, and 200 identical lines is not a log,
         * it is a wall -- the first one carries every bit of the information. */
        {
            static LONG once;
            if (InterlockedIncrement(&once) != 1) return hr;
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

/* Every attribute on a media type, by enumeration rather than by name.
 *
 * Looking for one attribute means knowing its GUID from memory, and a wrong
 * GUID reports an absence that is really a typo. IMFAttributes answers
 * GetCount (slot 30) and GetItemByIndex (slot 31), so the type can simply be
 * asked what it carries. Types: 0=UINT32 1=UINT64 2=double 3=GUID 4=string
 * 5=blob 6=IUnknown.
 *
 * What this is looking for: an H.264 decoder that produces the first keyframe
 * and nothing after is usually one that was never given the sequence header,
 * and this decoder declared no input types at all until this fix wrote one in
 * -- so what Electra ended up setting is worth seeing rather than assuming. */
static void dump_media_type(void *type, const char *what)
{
    void **vt;
    HRESULT (WINAPI *get_count)(void *, UINT32 *);
    HRESULT (WINAPI *get_by_index)(void *, UINT32, GUID *, PROPVARIANT *);
    UINT32 count = 0, i;

    if (!type) return;
    vt = *(void ***)type;
    get_count    = (HRESULT (WINAPI *)(void *, UINT32 *))vt[30];
    get_by_index = (HRESULT (WINAPI *)(void *, UINT32, GUID *, PROPVARIANT *))vt[31];
    if (FAILED(get_count(type, &count))) return;

    logf_("  %s carries %u attribute(s):", what, count);
    for (i = 0; i < count && i < 24; i++)
    {
        GUID key; PROPVARIANT v;
        PropVariantInit(&v);
        if (FAILED(get_by_index(type, i, &key, &v))) continue;
        if (v.vt == VT_UI4)
            logf_("    {%08lX-%04X} = %lu", key.Data1, key.Data2, (unsigned long)v.ulVal);
        else if (v.vt == VT_UI8)
            logf_("    {%08lX-%04X} = %lu x %lu (packed)", key.Data1, key.Data2,
                  (unsigned long)(v.uhVal.QuadPart >> 32),
                  (unsigned long)(v.uhVal.QuadPart & 0xffffffff));
        else if (v.vt == VT_CLSID)
            logf_("    {%08lX-%04X} = GUID {%08lX-...}", key.Data1, key.Data2,
                  v.puuid ? v.puuid->Data1 : 0);
        else if (v.vt == (VT_VECTOR | VT_UI1))
            logf_("    {%08lX-%04X} = blob, %lu bytes  << a header of some kind",
                  key.Data1, key.Data2, (unsigned long)v.caub.cElems);
        else
            logf_("    {%08lX-%04X} = type %u", key.Data1, key.Data2, v.vt);
        PropVariantClear(&v);
    }
}

static HRESULT WINAPI my_SetInputType(void *self, DWORD stream, void *type, DWORD flags)
{
    HRESULT hr = real_SetInputType(self, stream, type, flags);
    logf_("SetInputType(flags=0x%lx) -> 0x%08lx%s", flags, hr,
          FAILED(hr) ? "   << the decoder refuses the stream it was chosen for" : "");
    {
        static LONG once;
        if (SUCCEEDED(hr) && type && InterlockedIncrement(&once) == 1)
            dump_media_type(type, "the input type Electra set");
    }
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
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_Lock2D"); }
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
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_Unlock2D"); }
    struct two_d *td = (struct two_d *)self;
    HRESULT (WINAPI *unlock)(void *) =
        (HRESULT (WINAPI *)(void *))(*(void ***)td->inner)[4];
    td->locked = NULL;
    return unlock(td->inner);
}

static HRESULT WINAPI td_GetScanline0AndPitch(void *self, BYTE **scanline0, LONG *pitch)
{
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_GetScanline0AndPitch"); }
    struct two_d *td = (struct two_d *)self;
    if (!td->locked) return 0xC00D36B2L;  /* MF_E_INVALIDREQUEST */
    if (scanline0) *scanline0 = td->locked;
    if (pitch) *pitch = (LONG)frame_w;
    return S_OK;
}

static HRESULT WINAPI td_IsContiguousFormat(void *self, BOOL *contiguous)
{
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_IsContiguousFormat"); }
    (void)self;
    if (contiguous) *contiguous = TRUE;
    return S_OK;
}

static HRESULT WINAPI td_GetContiguousLength(void *self, DWORD *len)
{
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_GetContiguousLength"); }
    (void)self;
    /* Reported against the DISPLAY height, not the coded one. See display_h. */
    if (len) *len = frame_w * (display_h ? display_h : frame_h) * 3 / 2;
    return S_OK;
}

static HRESULT WINAPI td_ContiguousCopyTo(void *self, BYTE *dest, DWORD size)
{
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_ContiguousCopyTo"); }
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
    { static LONG once; if (InterlockedIncrement(&once) == 1) logf_("    Electra called td_ContiguousCopyFrom"); }
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
            {
                /* How big is the buffer REALLY?
                 *
                 * GetOutputStreamInfo said the caller allocates, so this memory
                 * is Electra's and sized to Electra's idea of the frame. The
                 * decoder negotiated 1920x1088 -- H.264 rounds height up to a
                 * multiple of 16 -- while the video is 1080. If Electra
                 * allocated for 1080 and the decoder fills 1088 rows, it writes
                 * eight rows past the end, which is a crash rather than a bad
                 * picture. IMFMediaBuffer::GetMaxLength is slot 4. */
                /* IMFMediaBuffer: Lock 3, Unlock 4, GetCurrentLength 5,
                 * SetCurrentLength 6, GetMaxLength 7. An earlier version of
                 * this read slot 4 as GetMaxLength, which is Unlock -- so it
                 * reported zero and, far worse, unlocked somebody else's buffer
                 * in the middle of the handover. Measuring must not disturb. */
                DWORD maxlen = 0, curlen = 0;
                HRESULT (WINAPI *get_cur)(void *, DWORD *) =
                    (HRESULT (WINAPI *)(void *, DWORD *))(*(void ***)self)[5];
                HRESULT (WINAPI *get_max)(void *, DWORD *) =
                    (HRESULT (WINAPI *)(void *, DWORD *))(*(void ***)self)[7];
                get_cur(self, &curlen); get_max(self, &maxlen);
                logf_("gave Electra an IMF2DBuffer over its own buffer "
                      "(%ux%u, pitch %u) -- nothing taken away this time",
                      frame_w, frame_h, frame_w);
                logf_("  its buffer holds %lu bytes (current %lu); NV12 at "
                      "%ux%u needs %lu, at %ux%u needs %lu  << %s",
                      (unsigned long)maxlen, (unsigned long)curlen,
                      frame_w, frame_h, (unsigned long)(frame_w * frame_h * 3 / 2),
                      frame_w, 1080u, (unsigned long)(frame_w * 1080u * 3 / 2),
                      (maxlen && maxlen < frame_w * frame_h * 3 / 2)
                        ? "TOO SMALL for the height the decoder negotiated"
                        : "big enough");
            }
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

/* Find the two call sites instead of remembering where they were.
 *
 * The RVAs below came from the disassembly of one build, and a game update
 * moved everything after it: both sites shifted by exactly 0x1E930, which is
 * what an insertion earlier in .text does. Fixed addresses then verify their
 * bytes, find something else and -- correctly -- do nothing, which leaves the
 * title broken until somebody disassembles it again.
 *
 * They do not need remembering. The two sites are `call qword ptr [rax+0x28]`
 * separated by exactly 0x1D3 bytes, and that pair is UNIQUE: in the 176 MB
 * shipping executable there are 3230 occurrences of the call and exactly one
 * pair at that distance. So the pair is the signature. If it is not unique in
 * some future build, this does nothing and says so rather than guessing.
 *
 * The original RVAs stay as a comment, because they are what the distance was
 * measured from. */
/* The two IsSoftware call sites, per build.
 *
 * HOW THESE WERE FOUND, because they move with every game update and the method
 * matters more than the numbers:
 *
 *   1. Let it crash and read the game's own report in Saved/Crashes. The
 *      PCallStack named +636cb43 among others.
 *   2. Disassemble there. The instruction before it is `callq 0x14636b8b0` and
 *      the one at it is `testb %al, %al` -- a function returning a bool, and a
 *      caller branching on it.
 *   3. Find every direct call to that same function followed by `testb %al,%al`.
 *      There are exactly two, which is the number this fix has always needed.
 *
 * The old build called it indirectly through a vtable (`call [rax+0x28]`, three
 * bytes); this one calls it directly (`e8 rel32`, five). Searching for the old
 * byte pattern therefore found nothing that was it -- and a search by the
 * DISTANCE between the old pair found a different pair entirely, which is what
 * a distance gets you: it is not a name.
 *
 * Verification below is on the call target, not on the bytes: both sites must
 * call the same address. That is what makes them the pair rather than two calls
 * that happen to look alike. */
#define RVA_ISSW_A     0x0636CB3E   /* callq IsSoftware ; testb al,al */
#define RVA_ISSW_B     0x06370B44   /* the second site, which is not optional */
#define RVA_ISSW_FUNC  0x0636B8B0   /* what both of them call */
/* Where this build stores the console variable, found from the single
 * reference to its name: the instruction after the registration call is
 * `movq %rax, 0x94c0d25(%rip)`. The old build had it at 0x0AA29110. */
#define RVA_CVAR_PTR_NEW 0x0AA78428
/* From vtable slot 12: lea rax,[rcx+0x50] ; ret */
#define CVAR_VALUES_OFFSET 0x50

/* Set once the console variable has been located and understood. Until then no
 * decoder is handed back: making one appear without putting Electra on the path
 * that can consume it is what turned a silently skipped video into a crash at
 * the menu -- measured, by reverting to the morning build and reaching the menu
 * cleanly. A fix that trades a missing picture for a crash is worse than none. */
static BOOL cvar_seen;
/* Flipped only when the variable has actually been set. Not yet. */
static BOOL cvar_ready;

static BOOL text_range(BYTE *base, BYTE **start, SIZE_T *size)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        if (memcmp(sec->Name, ".text", 5) == 0)
        {
            *start = base + sec->VirtualAddress;
            *size  = sec->Misc.VirtualSize;
            return TRUE;
        }
    return FALSE;
}

/* Both sites must call the same function, or this is not the pair. */
static BOOL issw_site_ok(BYTE *base, DWORD rva)
{
    BYTE *at = base + rva;
    LONG rel;
    if (at[0] != 0xE8) return FALSE;
    memcpy(&rel, at + 1, sizeof(rel));
    if ((DWORD)((at + 5 + rel) - base) != RVA_ISSW_FUNC) return FALSE;
    return at[5] == 0x84 && at[6] == 0xC0;      /* testb %al,%al */
}

/* call rel32 -> mov al,1 ; nop ; nop ; nop.  Same length, same meaning as the
 * three-byte form the old build needed. */
static const BYTE make_true5[5] = { 0xB0, 0x01, 0x90, 0x90, 0x90 };

static BOOL poke5(BYTE *at, const char *what)
{
    DWORD old;
    if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(at, make_true5, 5);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
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
    if (!issw_site_ok(base, RVA_ISSW_A) || !issw_site_ok(base, RVA_ISSW_B))
    {
        logf_("  the two call sites are not what this build has -- doing nothing. "
              "To find them again: crash it, read Saved/Crashes for the call "
              "stack, disassemble there for a bool-returning call followed by "
              "testb al,al, then find every direct call to that same function.");
        return;
    }

    if (poke5(base + RVA_ISSW_A, "IsSoftware (outer gate)")) done++;
    if (poke5(base + RVA_ISSW_B, "IsSoftware (sw value)")) done++;

    /* The console variable is a pointer read from a fixed address and written
     * through -- a wild write on any build but the one it was read from, and
     * that build is gone. Left alone until it is found the same way the calls
     * were. */
    /* The values live at object + 0x50, and that was read off the vtable
     * rather than guessed.
     *
     * What the fixed address holds in this build is an IConsoleVariable, not
     * the two plain ints the old code wrote -- its first qword is a pointer
     * into .rdata, so writing 1 at offset zero would destroy the vtable and
     * produce exactly the null dereference this afternoon was spent chasing.
     *
     * Its vtable says where the data is. Slot 12 is `lea rax,[rcx+0x50] ; ret`
     * -- AsVariableInt returning the embedded TConsoleVariableData<int32> --
     * and slot 8 is `mov al,1 ; ret`, which is the "this is an int variable"
     * answer. So the two int32s the old build had at offset zero are at 0x50
     * here, and the old write is right about what to set and wrong only about
     * where.
     *
     * Read before writing anyway: the default is 0, so anything else means
     * this is not what it is thought to be, and then nothing is touched. */
    {
        BYTE *slot = base + RVA_CVAR_PTR_NEW;
        void *obj = *(void **)slot;
        if (!obj)
        {
            logf_("  console variable not registered yet");
        }
        else
        {
            int *values = (int *)((BYTE *)obj + CVAR_VALUES_OFFSET);
            logf_("  console variable at %p, values at +0x%X currently [%d, %d]",
                  obj, (unsigned)CVAR_VALUES_OFFSET, values[0], values[1]);
            if (values[0] == 0 && values[1] == 0)
            {
                values[0] = 1;
                values[1] = 1;
                cvar_ready = TRUE;
                logf_("    set to 1 -- Electra takes the old CPU buffer path, "
                      "which stores the sample instead of querying for a D3D "
                      "resource that is not there");
            }
            else
                logf_("    left alone: the default for this variable is 0, so "
                      "this is not the field it is thought to be");
        }
    }

    if (!done) logf_("nothing was patched");
}

static HRESULT WINAPI my_ProcessOutput(void *self, DWORD flags, DWORD count,
                                       void *samples, DWORD *status)
{
    { static LONG calls; LONG k = InterlockedIncrement(&calls);
      if (k <= 12) logf_("  ProcessOutput call #%ld", (long)k); }
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
            {
                static LONG n;
                LONG k = InterlockedIncrement(&n);
                /* Every frame used to be silent after the first, so a run that
                 * decoded one frame and a run that decoded a thousand left the
                 * same log. Progress is the thing being measured here. */
                if (k == 1 || k % 60 == 0)
                    logf_("ProcessOutput: %ld frames decoded so far", (long)k);
            }
    }
    else if (n == 1 || (n % 200) == 0)
    {
        logf_("ProcessOutput -> 0x%08lx after %ld calls, %ld frames so far",
              hr, n, frames_out);
        if (hr == 0xC00D6D72L) logf_("  (MF_E_TRANSFORM_NEED_MORE_INPUT -- normal)");
    }
    { static LONG seen; LONG k = InterlockedIncrement(&seen);
      if (k <= 12) logf_("    ProcessOutput #%ld -> 0x%08lx%s", (long)k, hr,
          (hr == 0xC00D6D72L) ? "  (needs more input)" : ""); }
    return hr;
}

static HRESULT WINAPI my_ProcessInput(void *self, DWORD stream, void *sample, DWORD flags)
{
    { static LONG fed; LONG k = InterlockedIncrement(&fed);
      if (k <= 12) logf_("  ProcessInput #%ld -- Electra is still feeding it", (long)k); }
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
        /* The first height offered is the picture; anything later is the codec
         * rounding it up. Keep the smaller of the two. */
        if (!display_h || frame_h < display_h) display_h = frame_h;
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
        /* Tell it not to hoard.
         *
         * Measured: twelve ProcessOutput calls in a row answered
         * MF_E_TRANSFORM_NEED_MORE_INPUT while Electra fed nine samples, and
         * exactly one frame came out before Electra gave up and shut Media
         * Foundation down. That is not a broken decoder -- it is an H.264 decoder
         * doing what they all do, buffering several frames before emitting the
         * first, against a consumer that will not wait that long.
         *
         * MF_LOW_LATENCY is the switch for exactly that. IMFTransform GetAttributes
         * is slot 8 (GetStreamCount is 4, which this file already patches), and
         * IMFAttributes SetUINT32 is slot 21. */
        {
            static const GUID MF_LOW_LATENCY_ =
                { 0x9c27891a, 0xed7a, 0x40e1,
                  { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };
            HRESULT (WINAPI *get_attrs)(void *, void **) =
                (HRESULT (WINAPI *)(void *, void **))(*(void ***)*out)[8];
            void *attrs = NULL;
            if (SUCCEEDED(get_attrs(*out, &attrs)) && attrs)
            {
                void **avt = *(void ***)attrs;
                HRESULT (WINAPI *set_u32)(void *, REFGUID, UINT32) =
                    (HRESULT (WINAPI *)(void *, REFGUID, UINT32))avt[21];
                HRESULT hr2 = set_u32(attrs, &MF_LOW_LATENCY_, 1);
                logf_("  asked it for low latency -> 0x%08lx%s", hr2,
                      SUCCEEDED(hr2) ? "  (should emit without hoarding now)"
                                     : "  (refused; it will keep buffering)");
                ((ULONG (WINAPI *)(void *))avt[2])(attrs);
            }
            else logf_("  no attribute store on the transform");
        }
        if (h264_wanted) force_electra_software();
    }
    return hr;
}

static HRESULT (WINAPI *real_MFStartup)(ULONG, DWORD);
/* MFTEnumEx filters by kind, not only by format. ALL is every kind at once. */
#ifndef MFT_ENUM_FLAG_ALL
#define MFT_ENUM_FLAG_ALL 0x0000003F
#endif

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

/* Hand back the decoder this engine really has for the format asked about.
 *
 * MFTEnumEx asked for H.264 answers zero on this engine -- with either filter
 * widened, measured -- and yet the decoder is registered. Enumerating with no
 * format filter returns three, and their CLSIDs resolve in the bottle's own
 * registry to:
 *
 *   {62CE7E72-4C71-4D20-B15D-452831A87D9D}  CMSH264DecoderMFT      (msmpeg2vdec)
 *   {82D353DF-90BD-4382-8BC2-3F6192B76E34}  CWMVDecMediaObject     (wmvdecod)
 *   {E3AAF548-C9A4-4C6E-234D-5ADA374B0000}  WineGStreamer VP9 MFT  (winegstreamer)
 *
 * So the H.264 decoder is present and simply does not advertise H.264 as an
 * input type in a way the query matches. Handing back all three crashed this
 * title -- it activates what it is given, and two of those decode something
 * else. Handing back the one that matches the format is not a fabrication: it
 * is the decoder the caller was asking for, found by identity instead of by a
 * filter that lies.
 *
 * Anything not in this table is left out. A decoder for the wrong format is
 * worse than none, which is the lesson the crash reporter taught. */
static const GUID guid_friendly_name =
    { 0x314ffbae, 0x5b41, 0x4c95, { 0x9c, 0x19, 0x4e, 0x7d, 0x58, 0x6f, 0xac, 0xe3 } };
static const GUID guid_transform_clsid =
    { 0x6821c42b, 0x65a4, 0x4e82, { 0x99, 0xbc, 0x9a, 0x88, 0x20, 0x5e, 0xcd, 0x0c } };
/* MFT_INPUT_TYPES_Attributes: a blob of MFT_REGISTER_TYPE_INFO pairs, which is
 * what both the enumeration filter and (the hypothesis) the caller look at when
 * deciding whether a decoder handles a format. IMFAttributes: GetBlobSize 14,
 * GetBlob 15. */
static const GUID guid_input_types =
    { 0x4276c9b1, 0x759d, 0x4bf3, { 0x9c, 0xd0, 0x0d, 0x72, 0x3d, 0x13, 0x8f, 0x96 } };

static void say_what_it_declares(void *activate)
{
    void **vtbl = *(void ***)activate;
    HRESULT (WINAPI *get_blob_size)(void *, REFGUID, UINT32 *) =
        (HRESULT (WINAPI *)(void *, REFGUID, UINT32 *))vtbl[14];
    HRESULT (WINAPI *get_blob)(void *, REFGUID, UINT8 *, UINT32, UINT32 *) =
        (HRESULT (WINAPI *)(void *, REFGUID, UINT8 *, UINT32, UINT32 *))vtbl[15];
    UINT32 size = 0, got = 0, i, n;
    BYTE buf[512];

    if (FAILED(get_blob_size(activate, &guid_input_types, &size)) || !size)
    {
        logf_("    it declares NO input types at all -- which is why the filter "
              "excludes it, and very likely why the caller refuses it");
        return;
    }
    if (size > sizeof(buf)) size = sizeof(buf);
    if (FAILED(get_blob(activate, &guid_input_types, buf, size, &got))) return;

    n = got / (sizeof(GUID) * 2);
    logf_("    it declares %u input type(s):", n);
    for (i = 0; i < n && i < 8; i++)
    {
        GUID *sub = (GUID *)(buf + i * sizeof(GUID) * 2 + sizeof(GUID));
        char tag[5];
        tag[0] = (char)(sub->Data1 & 0xff); tag[1] = (char)((sub->Data1 >> 8) & 0xff);
        tag[2] = (char)((sub->Data1 >> 16) & 0xff); tag[3] = (char)((sub->Data1 >> 24) & 0xff);
        tag[4] = 0;
        logf_("      [%u] %s {%08lX-...}", i, tag, sub->Data1);
    }
}

/* MFMediaType_Video, for the major type half of a registration entry. */
static const GUID guid_major_video =
    { 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

/* Tell the decoder to say what it decodes.
 *
 * CMSH264DecoderMFT is registered on this engine and declares NO input types at
 * all -- measured. That single omission causes both halves of the failure:
 * MFTEnumEx filters on that attribute, so asking for H.264 returns zero, and
 * the caller checks the same attribute, so handing the decoder over by identity
 * was refused just as quietly.
 *
 * So the missing attribute is written. This is not a claim about what the
 * decoder can do -- it IS the H.264 decoder, by CLSID, out of the bottle's own
 * registry -- it is filling in a registration that arrived empty. IMFAttributes
 * SetBlob is slot 26 (GetGUID 10 and SetGUID 24 are already relied on above).
 *
 * If the write fails, nothing is claimed and the caller sees exactly what it
 * saw before. */
static BOOL declare_input_type(void *activate, const GUID *subtype)
{
    void **vtbl = *(void ***)activate;
    HRESULT (WINAPI *set_blob)(void *, REFGUID, const UINT8 *, UINT32) =
        (HRESULT (WINAPI *)(void *, REFGUID, const UINT8 *, UINT32))vtbl[26];
    struct { GUID major; GUID sub; } entry;
    HRESULT hr;

    entry.major = guid_major_video;
    entry.sub   = *subtype;
    hr = set_blob(activate, &guid_input_types, (const UINT8 *)&entry, sizeof(entry));
    logf_("    writing the input type it never declared -> 0x%08lx%s", hr,
          SUCCEEDED(hr) ? "" : "  (left exactly as it was)");
    return SUCCEEDED(hr);
}

static const GUID clsid_h264_decoder =
    { 0x62ce7e72, 0x4c71, 0x4d20, { 0xb1, 0x5d, 0x45, 0x28, 0x31, 0xa8, 0x7d, 0x9d } };
static const GUID guid_H264 =
    { 0x34363248, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

/* The CLSID that decodes a given subtype, or NULL if we do not know of one. */
static const GUID *decoder_for(const GUID *subtype)
{
    if (subtype && IsEqualGUID(subtype, &guid_H264)) return &clsid_h264_decoder;
    return NULL;
}

static BOOL keep_only_matching(const GUID *wanted, void ***mfts, UINT32 *count)
{
    void **found = *mfts;
    UINT32 n = *count, i, kept = 0;

    for (i = 0; i < n; i++)
    {
        void **vtbl;
        HRESULT (WINAPI *get_guid)(void *, REFGUID, GUID *);
        ULONG (WINAPI *release)(void *);
        GUID clsid;
        BOOL match = FALSE;

        if (!found[i]) continue;
        vtbl = *(void ***)found[i];
        get_guid = (HRESULT (WINAPI *)(void *, REFGUID, GUID *))vtbl[10];
        release  = (ULONG (WINAPI *)(void *))vtbl[2];

        if (SUCCEEDED(get_guid(found[i], &guid_transform_clsid, &clsid))
            && IsEqualGUID(&clsid, wanted))
            match = TRUE;

        if (match)
            found[kept++] = found[i];
        else
            release(found[i]);          /* not ours to hand on */
    }
    *count = kept;
    return kept > 0;
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

    /* Asked narrowly, answered nothing -- so ask again without the narrowing.
     *
     * MFTEnumEx filters by KIND as well as by format: 0x1 is synchronous MFTs
     * alone. A decoder registered as asynchronous, local or hardware-backed is
     * simply not in that answer, and a game that asks for one kind and gets
     * zero concludes the format cannot be decoded at all.
     *
     * Beast of Reincarnation is where this was measured. It moved from VP9 to
     * H.264 in an update and started asking with flags=0x1, receiving zero --
     * on an engine whose registry holds CMSH264DecoderMFT and whose GStreamer
     * carries both applemedia and the staged libav. The decoder was there the
     * whole time; the question excluded it.
     *
     * This differs from the answer NINJA GAIDEN 4 needs, and in the way that
     * matters: what comes back here is the RIGHT decoder for the format asked
     * about, so the game can go on to activate it and actually decode. Nothing
     * is fabricated -- the retry is the same call with the kind filter dropped.
     *
     * No lever. It only widens, never narrows, and only when the narrow answer
     * was already empty -- a state in which the title is broken anyway -- so
     * there is nothing to turn off. The log prints both counts, so what the
     * engine said on its own is still visible without one. This file reads no
     * environment at all, and a flag nobody sets is the dead lever this tree
     * spent today removing three of. */
    if (count && *count == 0 && SUCCEEDED(hr)
        && (flags & MFT_ENUM_FLAG_ALL) != MFT_ENUM_FLAG_ALL)
    {
        /* Widen the KIND filter only, and never the format one.
         *
         * MFTEnumEx narrows by kind (sync, async, hardware) and by format. This
         * drops the kind filter, which is free: whatever comes back still
         * claims the format the caller asked about.
         *
         * Dropping the FORMAT filter as well was tried here and must not be:
         * NINJA GAIDEN 4 gets away with it because that title only counts what
         * it is handed and never activates it. Beast of Reincarnation does
         * activate -- measured, IMFActivate::ActivateObject -> 0x00000000
         * followed by our own output-type hooks arming -- and then dies inside
         * a decoder that was never claiming to decode H.264. A crash reporter
         * is a worse answer than a missing video, and both are worse than the
         * engine's honest zero.
         *
         * So the zero stands, and it is a true statement about this engine:
         * winegstreamer exposes no MFT claiming H.264 as input, even though its
         * own applemedia decodes H.264 perfectly well through the source
         * reader. What this title needs is that path, not this one. */
        UINT32 narrow = flags;
        hr = real_MFTEnumEx(category, MFT_ENUM_FLAG_ALL, in, out, mfts, count);
        logf_("  asked again without the kind filter (0x%lx -> 0x%lx): %u "
              "decoder(s)%s", narrow, (unsigned long)MFT_ENUM_FLAG_ALL,
              count ? *count : 0,
              (count && *count == 0)
                ? ". The format filter is what excludes everything, and dropping "
                  "it hands this title a decoder for another format, which it "
                  "activates and crashes on. Left alone deliberately."
                : ".");
        if (*count == 0 && in)
        {
            const GUID *want_clsid = cvar_ready ? decoder_for(&in->guidSubtype) : NULL;
            if (!cvar_ready)
                logf_("  a decoder could be handed back here, and is not: without "
                      "the console variable Electra takes a path that dereferences "
                      "null on the first frame, which is worse than the skip");
            if (want_clsid
                && SUCCEEDED(real_MFTEnumEx(category, MFT_ENUM_FLAG_ALL, NULL,
                                            out, mfts, count))
                && *mfts)
            {
                if (keep_only_matching(want_clsid, mfts, count))
                {
                    logf_("  the decoder for that format IS registered here and "
                          "simply does not advertise the format: handing back "
                          "%u, by identity rather than by the filter", *count);
                    say_what_it_declares((*mfts)[0]);
                    declare_input_type((*mfts)[0], &in->guidSubtype);
                }
                else
                    logf_("  none of what this engine offers decodes that "
                          "format, so the zero stands");
            }
        }
    }
    /* Watch the promised decoder actually be created.
     *
     * This is installed on whatever is being handed back, AFTER the work above,
     * because that work is what puts something there to hook. An earlier edit
     * moved this block away and the hook stopped being installed at all -- so
     * three runs in a row reported "the game does not activate" when what had
     * actually happened is that nobody was watching. An absence in a log is
     * only evidence if the thing that writes it was running. */
    if (SUCCEEDED(hr) && mfts && *mfts && count && *count > 0)
    {
        static void *ao;
        if (patch_slot("ActivateObject", (*mfts)[0], SLOT_ACTIVATE_OBJECT,
                       (void *)my_ActivateObject, &ao))
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

/* ============================ the Electra VPx crash (Mortal Shell 2) === */

static BOOL is_jl(const BYTE *at)
{
    return at[0] == 0x7C || (at[0] == 0x0F && at[1] == 0x8C);
}

/* Raise a `cmp dword [rbp+disp], 12000` to INT_MAX. `imm` points at the
 * four-byte immediate. */
static BOOL raise_threshold(BYTE *imm)
{
    DWORD old;

    if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    *(DWORD *)imm = UNREACHABLE_VERSION;
    VirtualProtect(imm, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), imm, 4);
    return TRUE;
}

/*
 * Scan the executable's own code for Electra's VPx version checks.
 *
 * We match on the compare being against a *stack slot*: the VPx decoder keeps
 * the version in a local, while the H.264 and H.265 decoders compare a register
 * (3d / 81 f8). That distinction matters -- H.264 already has its CVar and must
 * be left alone.
 *
 * A pattern scan rather than fixed offsets, because a game update moves
 * everything: between two builds of the same title the crash site alone shifted
 * by 0x2C70.
 */
static int apply(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    int found = 0, patched = 0;
    unsigned i;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        BYTE *p, *end;

        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        p = base + sec->VirtualAddress;
        end = p + sec->Misc.VirtualSize - 16;

        for (; p < end; ++p)
        {
            BYTE *imm, *jump;

            if (p[0] != 0x81) continue;

            if (p[1] == 0x7D)          /* cmp dword [rbp+disp8],  imm32 */
            {
                imm = p + 3;
                jump = p + 7;
            }
            else if (p[1] == 0xBD)     /* cmp dword [rbp+disp32], imm32 */
            {
                imm = p + 6;
                jump = p + 10;
            }
            else continue;

            if (*(DWORD *)imm != ELECTRA_D3D12_VERSION) continue;
            if (!is_jl(jump)) continue;      /* not the shape we mean */

            ++found;
            if (raise_threshold(imm))
            {
                ++patched;
                logf_("  raised threshold at +0x%llx",
                      (unsigned long long)(imm - base));
            }
            else
            {
                logf_("  could not write at +0x%llx",
                      (unsigned long long)(imm - base));
            }
            p = jump;
        }
    }

    logf_("VPx version checks: %d found, %d patched", found, patched);
    if (!found)
        logf_("nothing matched -- this build may not be affected, or Unreal's "
              "code generation changed. Nothing was modified.");
    return patched;
}

/* ---------------------------------------------------- the node walk --- */

/*
 * Part two: stop D3DMetal answering for adapter nodes that do not exist.
 *
 * Unreal's D3D12 renderer walks the adapter's memory nodes, accumulating
 * across them, and ends the walk when the call fails:
 *
 *     callq *0x70(%rax)     ; IDXGIAdapter3::QueryVideoMemoryInfo, slot 14
 *     testl %eax, %eax
 *     jns   <backwards>     ; keep going while it succeeds
 *
 * On Windows that call returns an error once the index passes the number of
 * nodes, and that is what stops the loop. D3DMetal answers S_OK for every
 * index, so the counter climbs forever -- two hundred million iterations a
 * second, measured -- one thread pinned and everything else starving behind
 * it. The game runs, then freezes after a while, wherever it happens to be.
 *
 * Refusing an index the adapter does not have ends it. The refusal fires once
 * per session: the caller takes the node count from that answer and stops
 * asking.
 *
 * Not every Unreal title emits that loop. Of eight checked, two did, both from
 * the same studio and engine build; diagnostics/find-node-walk.py says which.
 * The guard is inert on the rest.
 */
static const GUID iid_adapter3 = { 0x645967a4, 0x1392, 0x4310,
                                   { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };

static HRESULT (WINAPI *real_query_vram)(void *, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *);
static LONG refusals;

static HRESULT WINAPI guarded_query_vram(void *self, UINT node,
                                         DXGI_MEMORY_SEGMENT_GROUP group,
                                         DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    /* One GPU means one node, so zero is the only valid index, and
     * DXGI_ERROR_INVALID_CALL is what Windows returns past the end. */
    if (node != 0)
    {
        if (InterlockedIncrement(&refusals) == 1)
            logf_("node %u does not exist -- refused, which ends the caller's walk", node);
        return DXGI_ERROR_INVALID_CALL;
    }
    return real_query_vram(self, node, group, info);
}

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

static void guard_adapter(void *adapter)
{
    IDXGIAdapter3 *a3 = NULL;

    if (!adapter || real_query_vram) return;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)adapter, &iid_adapter3, (void **)&a3)) || !a3)
        return;
    real_query_vram = patch_vtable_slot(a3, 14, guarded_query_vram);
    IDXGIAdapter3_Release(a3);
}

static HRESULT (WINAPI *real_enum_adapters)(void *, UINT, void **);
static HRESULT (WINAPI *real_enum_adapters1)(void *, UINT, void **);

static HRESULT WINAPI guarded_enum_adapters(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) guard_adapter(*adapter);
    return hr;
}

static HRESULT WINAPI guarded_enum_adapters1(void *self, UINT index, void **adapter)
{
    HRESULT hr = real_enum_adapters1(self, index, adapter);
    if (SUCCEEDED(hr) && adapter) guard_adapter(*adapter);
    return hr;
}

static void guard_factory(void *factory)
{
    static LONG done;

    if (!factory || InterlockedExchange(&done, 1)) return;
    /* IDXGIFactory1: slot 7 EnumAdapters, slot 12 EnumAdapters1. */
    real_enum_adapters  = patch_vtable_slot(factory, 7,  guarded_enum_adapters);
    real_enum_adapters1 = patch_vtable_slot(factory, 12, guarded_enum_adapters1);
}

static HRESULT (WINAPI *real_CreateDXGIFactory)(REFIID, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory1)(REFIID, void **);
static HRESULT (WINAPI *real_CreateDXGIFactory2)(UINT, REFIID, void **);

static HRESULT WINAPI my_CreateDXGIFactory(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

static HRESULT WINAPI my_CreateDXGIFactory1(REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory1(iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

static HRESULT WINAPI my_CreateDXGIFactory2(UINT flags, REFIID iid, void **out)
{
    HRESULT hr = real_CreateDXGIFactory2(flags, iid, out);
    if (SUCCEEDED(hr) && out) guard_factory(*out);
    return hr;
}

/* Replace one entry in the main module's import address table. */

static void install_node_guard(void)
{
    real_CreateDXGIFactory  = hook_import("dxgi.dll", "CreateDXGIFactory",  my_CreateDXGIFactory);
    real_CreateDXGIFactory1 = hook_import("dxgi.dll", "CreateDXGIFactory1", my_CreateDXGIFactory1);
    real_CreateDXGIFactory2 = hook_import("dxgi.dll", "CreateDXGIFactory2", my_CreateDXGIFactory2);
    logf_("node guard: %s",
          (real_CreateDXGIFactory || real_CreateDXGIFactory1 || real_CreateDXGIFactory2)
          ? "armed" : "this game creates no DXGI factory by name");
}

/* Which halves each title actually needs.
 *
 * One DLL serves every game, and that is worth keeping: a single file to build,
 * ship and reason about. What is not worth keeping is every half acting
 * wherever its byte pattern happens to match, because matching is not the same
 * as belonging -- and a change made to help one title then silently changes
 * every other one that matches too.
 *
 * So each half is asked to act by name. Narrowing Electra's patch for one game
 * cannot alter what happens in another, and a title that only ever froze is
 * never patched for a crash it does not have.
 *
 * A title not listed here gets both halves, which is how a new game is tried
 * for the first time. The log says so, so an unexpected result is traceable to
 * this table rather than mistaken for a measurement. */
struct policy
{
    const char *exe;
    BOOL electra;     /* raise Electra's VPx GPU-buffer threshold */
    BOOL node_guard;  /* refuse adapter nodes that do not exist */
};

/* ================================================ what each title needs === */

/* Each half is asked for by name. Matching a byte pattern is not the same as
 * belonging in a game, and one shared binary with one shared behaviour means
 * every change lands everywhere at once.
 *
 * All three halves ride on the same carrier DLL, which is why they live in one
 * file rather than one per game: a title needing two of them could not be given
 * two files. */
struct policy3
{
    const char *exe;
    BOOL electra_vpx;    /* raise Electra's VPx GPU-buffer threshold */
    BOOL node_guard;     /* refuse adapter nodes that do not exist */
    BOOL electra_h264;   /* NV12 back on the menu, and Electra onto software */
};

static const struct policy3 wanted[] =
{
    { "MortalShell2-Win64-Shipping.exe",         TRUE,  FALSE, FALSE },
    { "Iris-Win64-Shipping.exe",                 FALSE, TRUE,  FALSE },
    { "Chronos-Win64-Shipping.exe",              FALSE, TRUE,  FALSE },
    { "BeastOfReincarnation-Win64-Shipping.exe", FALSE, FALSE, TRUE  },
};

static DWORD WINAPI worker(LPVOID unused)
{
    const char *me = process_name();
    struct policy3 want = { NULL, TRUE, TRUE, TRUE };
    BOOL known = FALSE;
    size_t i;

    (void)unused;

    for (i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++)
        if (lstrcmpiA(me, wanted[i].exe) == 0) { want = wanted[i]; known = TRUE; break; }

    if (!known)
        logf_("not a title this build knows -- arming everything; report what "
              "happens rather than trusting it");

    /* The Media Foundation half only acts where it is wanted. Its hooks are
     * installed regardless -- they are how a new title gets surveyed -- but
     * they change nothing unless these are set. */
    h264_wanted        = want.electra_h264;
    restore_nv12       = want.electra_h264;
    withhold_d3d_from_mft = want.electra_h264;
    refuse_d3d_manager = FALSE;

    logf_("halves for this title: electra-vpx %s | node-guard %s | electra-h264 %s",
          want.electra_vpx  ? "on" : "off",
          want.node_guard   ? "on" : "off",
          want.electra_h264 ? "on" : "off");

    if (want.electra_vpx) apply();
    if (want.node_guard)  install_node_guard();

    /* Electra's own gate, patched as soon as the half is armed.
     *
     * This used to happen only from inside ActivateObject, after a decoder was
     * instantiated -- which never runs on an engine where the enumeration
     * answers zero, so the one patch that does not need a decoder was the one
     * gated behind having one. Measured: three runs where the pair was never
     * even looked for. It answers a question Electra asks of ITSELF, so it
     * belongs here. */
    if (want.electra_h264) force_electra_software();
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
