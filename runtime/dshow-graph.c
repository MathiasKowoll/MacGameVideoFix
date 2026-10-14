/*
 * dshow-graph.c -- give a .wmv the source filter Windows would give it.
 *
 * Linked into mgvf-asf-reader.dll. On Windows a .wmv or .asf opened through
 * DirectShow gets the WM ASF Reader as its source, by the Media Type\Extensions
 * key. Wine's prefixes carry no such key, so the file is opened by the generic
 * async file source instead, and every decoder is then offered the raw bytes
 * (measured with Nioh on CrossOver Preview 27: the WMV decoder was offered
 * MEDIATYPE_Stream and nothing else, and the game said "Failure to play
 * movie"). Where GStreamer can parse and decode the file that route works; where
 * it cannot, nothing does.
 *
 * So the filter graph's RenderFile and AddSourceFilter are watched. RenderFile
 * is tried as wine would do it first; only when it fails on a .wmv or .asf are
 * the filters it left behind removed and the graph built again from the WM ASF
 * Reader. AddSourceFilter on such a file hands out the WM ASF Reader when it
 * opens the file, as Windows does. The reader inside that filter is wine's when
 * wine can open the file, and wm-reader.c's when it cannot.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <strmif.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

#define LOGFILE "C:\\mgvf-asf-reader.log"

static LONG log_lines;

static void logf_(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    HANDLE h;
    DWORD w;

    if (InterlockedIncrement(&log_lines) > 100) return;
    n = snprintf(buf, sizeof(buf), "[graph] ");
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

static const GUID clsid_asf_reader = { 0x187463a0, 0x5bb7, 0x11d3, { 0xac, 0xbe, 0x00, 0x80, 0xc7, 0x5e, 0x24, 0x6e } };
static const GUID iid_graph    = { 0x56a868a9, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID iid_filter   = { 0x56a86895, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };
static const GUID iid_filesrc  = { 0x56a868a6, 0x0ad4, 0x11ce, { 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 } };

#define VFW_S_PARTIAL_RENDER_ ((HRESULT)0x00040242L)

/* IGraphBuilder: 3 AddFilter, 4 RemoveFilter, 5 EnumFilters, 12 Render,
 * 13 RenderFile, 14 AddSourceFilter. */
#define SLOT_RENDER_FILE   13
#define SLOT_ADD_SOURCE    14

static HRESULT (WINAPI *real_RenderFile)(IGraphBuilder *, LPCWSTR, LPCWSTR);
static HRESULT (WINAPI *real_AddSourceFilter)(IGraphBuilder *, LPCWSTR, LPCWSTR, IBaseFilter **);

static BOOL is_asf_file(LPCWSTR file)
{
    const WCHAR *dot = file ? wcsrchr(file, L'.') : NULL;
    return dot && (!lstrcmpiW(dot, L".wmv") || !lstrcmpiW(dot, L".asf"));
}

/* The WM ASF Reader with the file loaded, if it came up with output pins.
 * qasf's Load says S_OK even when its reader refused the file, so the pins are
 * what tells. */
static IBaseFilter *asf_source(LPCWSTR file, int *pins)
{
    IBaseFilter *f = NULL;
    IFileSourceFilter *src = NULL;
    IEnumPins *e = NULL;
    IPin *pin;
    HRESULT hr;

    *pins = 0;
    if (FAILED(hr = CoCreateInstance(&clsid_asf_reader, NULL, CLSCTX_INPROC_SERVER, &iid_filter, (void **)&f)))
    {
        logf_("WM ASF Reader not created (0x%08lx)", (unsigned long)hr);
        return NULL;
    }
    if (SUCCEEDED(IBaseFilter_QueryInterface(f, &iid_filesrc, (void **)&src)))
    {
        hr = IFileSourceFilter_Load(src, file, NULL);
        IFileSourceFilter_Release(src);
    }
    if (SUCCEEDED(IBaseFilter_EnumPins(f, &e)))
    {
        while (IEnumPins_Next(e, 1, &pin, NULL) == S_OK) { (*pins)++; IPin_Release(pin); }
        IEnumPins_Release(e);
    }
    if (!*pins) { IBaseFilter_Release(f); return NULL; }
    return f;
}

/* Render every output pin of an added source; how many took. */
static int render_pins(IGraphBuilder *g, IBaseFilter *f)
{
    IEnumPins *e = NULL;
    IPin *pin;
    int ok = 0;
    if (FAILED(IBaseFilter_EnumPins(f, &e))) return 0;
    while (IEnumPins_Next(e, 1, &pin, NULL) == S_OK)
    {
        PIN_DIRECTION dir;
        if (SUCCEEDED(IPin_QueryDirection(pin, &dir)) && dir == PINDIR_OUTPUT)
        {
            HRESULT hr = IGraphBuilder_Render(g, pin);
            if (SUCCEEDED(hr)) ok++;
            else logf_("rendering an output pin -> 0x%08lx", (unsigned long)hr);
        }
        IPin_Release(pin);
    }
    IEnumPins_Release(e);
    return ok;
}

/* Filters in the graph now, so the ones a failed RenderFile added can go. */
#define MAX_KEEP 64
static int snapshot(IGraphBuilder *g, IBaseFilter **list)
{
    IEnumFilters *e = NULL;
    IBaseFilter *f;
    int n = 0;
    if (FAILED(IGraphBuilder_EnumFilters(g, &e))) return 0;
    while (n < MAX_KEEP && IEnumFilters_Next(e, 1, &f, NULL) == S_OK) list[n++] = f;
    IEnumFilters_Release(e);
    return n;
}

static void remove_new(IGraphBuilder *g, IBaseFilter **before, int n_before)
{
    IBaseFilter *now[MAX_KEEP];
    int n = snapshot(g, now), i, j;
    for (i = 0; i < n; i++)
    {
        BOOL old = FALSE;
        for (j = 0; j < n_before; j++) if (now[i] == before[j]) old = TRUE;
        if (!old) IGraphBuilder_RemoveFilter(g, now[i]);
        IBaseFilter_Release(now[i]);
    }
}

static HRESULT WINAPI my_RenderFile(IGraphBuilder *g, LPCWSTR file, LPCWSTR playlist)
{
    IBaseFilter *before[MAX_KEEP], *f;
    int n_before, pins, ok, i;
    HRESULT hr;

    if (!is_asf_file(file)) return real_RenderFile(g, file, playlist);
    n_before = snapshot(g, before);
    hr = real_RenderFile(g, file, playlist);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < n_before; i++) IBaseFilter_Release(before[i]);
        return hr;
    }
    remove_new(g, before, n_before);
    for (i = 0; i < n_before; i++) IBaseFilter_Release(before[i]);

    if (!(f = asf_source(file, &pins)))
    {
        logf_("RenderFile(%ls) -> 0x%08lx, and the WM ASF Reader has no pins for it either", file, (unsigned long)hr);
        return hr;
    }
    IGraphBuilder_AddFilter(g, f, L"WM ASF Reader");
    ok = render_pins(g, f);
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 5 || !ok)
            logf_("RenderFile(%ls) -> 0x%08lx; from the WM ASF Reader: %d of %d pins rendered",
                  file, (unsigned long)hr, ok, pins);
    }
    if (!ok)
    {
        IGraphBuilder_RemoveFilter(g, f);
        IBaseFilter_Release(f);
        return hr;
    }
    IBaseFilter_Release(f);
    return ok == pins ? S_OK : VFW_S_PARTIAL_RENDER_;
}

static HRESULT WINAPI my_AddSourceFilter(IGraphBuilder *g, LPCWSTR file, LPCWSTR name, IBaseFilter **out)
{
    IBaseFilter *f;
    int pins;
    HRESULT hr;

    if (!is_asf_file(file) || !out) return real_AddSourceFilter(g, file, name, out);
    if (!(f = asf_source(file, &pins))) return real_AddSourceFilter(g, file, name, out);
    if (FAILED(hr = IGraphBuilder_AddFilter(g, f, name ? name : L"WM ASF Reader")))
    {
        IBaseFilter_Release(f);
        return real_AddSourceFilter(g, file, name, out);
    }
    {
        static LONG said;
        if (InterlockedIncrement(&said) <= 5)
            logf_("AddSourceFilter(%ls): the WM ASF Reader, %d pin(s)", file, pins);
    }
    *out = f;
    return S_OK;
}

static BOOL patch(void **vt, int slot, void *mine, void **saved)
{
    DWORD old;
    if (vt[slot] == mine) return TRUE;
    if (!VirtualProtect(&vt[slot], sizeof(void *), PAGE_READWRITE, &old)) return FALSE;
    if (!*saved) *saved = vt[slot];
    vt[slot] = mine;
    VirtualProtect(&vt[slot], sizeof(void *), old, &old);
    return TRUE;
}

/* Called on any object the game creates as CLSID_FilterGraph. Every graph
 * shares one vtable, so this patches once. */
__declspec(dllexport) void WINAPI MGVF_PatchFilterGraph(IUnknown *obj)
{
    IGraphBuilder *g = NULL;
    static LONG done;
    void **vt;
    if (!obj || done) return;
    if (FAILED(IUnknown_QueryInterface(obj, &iid_graph, (void **)&g)) || !g) return;
    vt = *(void ***)g;
    if (patch(vt, SLOT_RENDER_FILE, (void *)my_RenderFile, (void **)&real_RenderFile)
        && patch(vt, SLOT_ADD_SOURCE, (void *)my_AddSourceFilter, (void **)&real_AddSourceFilter))
    {
        done = 1;
        logf_("filter graph watched: a .wmv the graph cannot render gets the WM ASF Reader");
    }
    IGraphBuilder_Release(g);
}
