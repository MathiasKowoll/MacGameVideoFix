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
#define SLOT_ACTIVATE_OBJECT  33
#define SLOT_SET_OUTPUT_TYPE  16
#define SLOT_PROCESS_MESSAGE  23
#define SLOT_PROCESS_INPUT    24
#define SLOT_PROCESS_OUTPUT   25

static BOOL patch_slot(void *obj, int slot, void *replacement, void **saved)
{
    void ***vt = (void ***)obj;
    DWORD old;
    if (!obj || !vt[0]) return FALSE;
    if (*saved) return TRUE;                  /* one vtable, patch it once */
    *saved = (*vt)[slot];
    if (!VirtualProtect(&(*vt)[slot], sizeof(void *), PAGE_READWRITE, &old))
        return FALSE;
    (*vt)[slot] = replacement;
    VirtualProtect(&(*vt)[slot], sizeof(void *), old, &old);
    return TRUE;
}

static HRESULT (WINAPI *real_ActivateObject)(void *, REFIID, void **);
static HRESULT (WINAPI *real_SetOutputType)(void *, DWORD, void *, DWORD);
static HRESULT (WINAPI *real_ProcessMessage)(void *, DWORD, ULONG_PTR);
static HRESULT (WINAPI *real_ProcessInput)(void *, DWORD, void *, DWORD);
static HRESULT (WINAPI *real_ProcessOutput)(void *, DWORD, DWORD, void *, DWORD *);

static LONG frames_out, output_calls, input_calls;

static HRESULT WINAPI my_ProcessOutput(void *self, DWORD flags, DWORD count,
                                       void *samples, DWORD *status)
{
    HRESULT hr = real_ProcessOutput(self, flags, count, samples, status);
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

static HRESULT WINAPI my_SetOutputType(void *self, DWORD stream, void *type, DWORD flags)
{
    HRESULT hr = real_SetOutputType(self, stream, type, flags);
    logf_("SetOutputType(flags=0x%lx) -> 0x%08lx%s", flags, hr,
          FAILED(hr) ? "   << no agreed output format means no picture, ever" : "");
    return hr;
}

static HRESULT WINAPI my_ProcessMessage(void *self, DWORD message, ULONG_PTR param)
{
    HRESULT hr = real_ProcessMessage(self, message, param);
    if (message == 0x00000002) /* MFT_MESSAGE_SET_D3D_MANAGER */
        logf_("ProcessMessage(SET_D3D_MANAGER, %p) -> 0x%08lx", (void *)param, hr);
    return hr;
}

static HRESULT WINAPI my_ActivateObject(void *self, REFIID iid, void **out)
{
    HRESULT hr = real_ActivateObject(self, iid, out);
    logf_("IMFActivate::ActivateObject -> 0x%08lx%s", hr,
          FAILED(hr) ? "   << the decoder MFTEnumEx promised does NOT exist" : "");
    if (SUCCEEDED(hr) && out && *out)
    {
        static void *st, *pm, *pi, *po;
        patch_slot(*out, SLOT_SET_OUTPUT_TYPE, (void *)my_SetOutputType, &st);
        patch_slot(*out, SLOT_PROCESS_MESSAGE, (void *)my_ProcessMessage, &pm);
        patch_slot(*out, SLOT_PROCESS_INPUT,   (void *)my_ProcessInput,   &pi);
        patch_slot(*out, SLOT_PROCESS_OUTPUT,  (void *)my_ProcessOutput,  &po);
        real_SetOutputType  = (HRESULT (WINAPI *)(void *, DWORD, void *, DWORD))st;
        real_ProcessMessage = (HRESULT (WINAPI *)(void *, DWORD, ULONG_PTR))pm;
        real_ProcessInput   = (HRESULT (WINAPI *)(void *, DWORD, void *, DWORD))pi;
        real_ProcessOutput  = (HRESULT (WINAPI *)(void *, DWORD, DWORD, void *, DWORD *))po;
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
static BOOL refuse_d3d_manager = TRUE;

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
        if (patch_slot((*mfts)[0], SLOT_ACTIVATE_OBJECT, (void *)my_ActivateObject, &ao))
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
        if (GetEnvironmentVariableA("BEAST_ALLOW_D3D_MANAGER", v, sizeof(v)) && v[0] == '1')
            refuse_d3d_manager = FALSE;
    }
    logf_("---- armed: DXGI device manager %s ----",
          refuse_d3d_manager ? "REFUSED (forcing software decode)" : "allowed (watching only)");
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
