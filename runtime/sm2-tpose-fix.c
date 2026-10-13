/*
 * sm2-tpose-fix.c -- Marvel's Spider-Man 2: characters stuck in a T-pose.
 *
 * THIS IS MACPROTIPS' FIX, PLUS ONE IMPROVEMENT OF OURS. The diagnosis, the raw
 * views and the four rewritten shaders are theirs: github.com/macprotips/
 * SpiderMan2Fix, public domain. What MacGameVideoFix adds is where it hooks, so
 * no DLL override is needed, and a barrier that removed the brief black blocks
 * in the sandstorm stage. Both are described below.
 *
 * WHAT IT FIXES. On M1-M4 Macs, D3DMetal returns 0 for a typed buffer read past
 * element 2^28, and drops a typed write past it. The game keeps every character's
 * mesh in one 2 GB pool and reads it through typed R32 views of 536,346,624
 * elements, so the meshes stored in its upper half are never skinned and stay in
 * a T-pose; and it writes the symbiote's triangles into that pool every frame
 * through a typed UAV of the same size, so they are lost (spikes, a flat black
 * sheet, or no symbiote). M5 Macs do not have the limit.
 *
 * Found and fixed by macprotips (github.com/macprotips/SpiderMan2Fix, public
 * domain). Everything below the DllMain glue is their d3d12.dll shim, unchanged
 * in what it does (their 1.2): typed 32-bit buffer SRVs and UAVs wider than 2^28
 * elements are created raw, and the four shaders that read or write them
 * (CS_ModelSkinning, a vertex morph, and the symbiote's two spline-mesh index
 * passes) are replaced by copies whose accesses are raw, sm2fix-pool-<hash>.dxil, built on
 * the user's Mac from their own Spider-Man2.exe. It acts only when sm2fix.ini
 * names this exact game build, and logs to sm2-tpose-fix.log beside itself.
 *
 * WHERE IT HOOKS -- the one difference. Their shim is d3d12.dll itself, so Wine
 * needs a per-game override to load it. This rides on amd_ags_x64.dll, which the
 * game imports and no runner ships, so it loads with no override under CrossOver,
 * RaccoonBot or NotProton alike. The game loads d3d12.dll later, at run time; a
 * loader notification catches that and points the export of D3D12CreateDevice at
 * this module, so whoever asks for it -- the game, or NVIDIA Streamline beneath
 * it -- creates D3DMetal's device through here, and the device class is hooked
 * as theirs hooks it.
 *
 * MGVF'S OWN ADDITION: a UAV barrier after CS_VolumetricFogCopy. vkd3d-proton
 * forces one there for this game ("Add missing barrier in Spider Man 2"); without
 * it the pass that reads the copied fog can see it half written. Measured once,
 * on 2026-10-12 in the opening sandstorm stage: without the barrier, brief black
 * blocks came and went; with it, the copy ran 8030 times, each followed by the
 * barrier, and none were seen in that session. The blocks were sporadic, so one
 * clean session is evidence, not proof. On when sm2fix.ini says
 * mgvf_fog_barrier = 1 (the installer's default), and counted in the log.
 *
 * Carried on amd_ags_x64.dll; every export is forwarded to the renamed original
 * (runtime/build-proxy.sh).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later (this file's glue; their code is public domain)
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sm2fix_core.h"
#include "sm2fix_core.c"

#define FIX_VERSION "1.2"
/* A plain string, so runtime/check-builds.sh can tie this DLL to this source. */
#define LOG_NAME "sm2-tpose-fix.log"
#define WIDE_VIEW (1u << 28)          /* D3DMetal's typed buffer limit on M1-M4 */
#define MAX_SHADERS 8
#define DEV_IDX(m) (offsetof(ID3D12Device2Vtbl, m) / sizeof(void *))

static HMODULE g_self;
static FILE *g_log;
static SRWLOCK g_lock = SRWLOCK_INIT;
static INIT_ONCE g_init = INIT_ONCE_STATIC_INIT;
static BOOL g_active;
static struct { uint64_t hash; void *data; size_t size; } g_shaders[MAX_SHADERS];
static unsigned int g_shader_count;
static volatile LONG g_views, g_uavs, g_pipelines;

/* The fog barrier (MGVF): the shader it follows, the pipelines built with it, and
 * per recording thread, which pipeline that thread's command list has bound. */
#define FOG_COPY_HASH 0x324071d329f05cccull
#define MAX_FOG_PSOS 16
#define LIST_IDX(m) (offsetof(ID3D12GraphicsCommandListVtbl, m) / sizeof(void *))
static BOOL g_fog_barrier;
static void *g_fog_psos[MAX_FOG_PSOS];
static volatile LONG g_fog_pso_count, g_fog_barriers;
static _Thread_local struct { void *list; BOOL fog; } t_bound;
static struct { void **vtbl; unsigned int index; void *orig; } g_hooks[32];
static unsigned int g_hook_count;

static void log_msg(const char *fmt, ...)
{
    va_list args;

    if (!g_log)
        return;
    AcquireSRWLockExclusive(&g_lock);
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
    ReleaseSRWLockExclusive(&g_lock);
}

static void path_next_to_self(WCHAR *out, size_t len, const WCHAR *file)
{
    WCHAR *slash;

    GetModuleFileNameW(g_self, out, (DWORD)len);
    slash = wcsrchr(out, L'\\');
    if (!slash)
        slash = wcsrchr(out, L'/');
    if (slash)
        slash[1] = 0;
    else
        out[0] = 0;
    wcsncat(out, file, len - wcslen(out) - 1);
}

static void *read_file(const WCHAR *path, size_t *size)
{
    void *data = NULL;
    FILE *f = _wfopen(path, L"rb");
    long n;

    *size = 0;
    if (!f)
        return NULL;
    if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)))
    {
        if (fread(data, 1, (size_t)n, f) == (size_t)n)
            *size = (size_t)n;
        else
        {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}

/* The running game's build: PE timestamp and image size of Spider-Man2.exe. The app
 * reads the same two fields from the file and writes them to sm2fix.ini. */
static void game_build(char *out, size_t len)
{
    const BYTE *base = (const BYTE *)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);

    snprintf(out, len, "%08lx-%08lx", (unsigned long)nt->FileHeader.TimeDateStamp,
            (unsigned long)nt->OptionalHeader.SizeOfImage);
}

/* sm2fix.ini: "game_build = <build>" and "shaders = <hash> <hash> ..." */
static int read_settings(char *build, size_t build_len, uint64_t *hashes, unsigned int *count)
{
    WCHAR path[MAX_PATH];
    char line[512];
    FILE *f;

    *count = 0;
    build[0] = 0;
    path_next_to_self(path, MAX_PATH, L"sm2fix.ini");
    if (!(f = _wfopen(path, L"r")))
        return 0;
    while (fgets(line, sizeof(line), f))
    {
        char *value = strchr(line, '='), *end;

        if (line[0] == '#' || !value)
            continue;
        *value++ = 0;
        while (*value == ' ' || *value == '\t')
            value++;
        value[strcspn(value, "\r\n")] = 0;
        if (!strncmp(line, "game_build", 10))
            snprintf(build, build_len, "%s", value);
        else if (!strncmp(line, "mgvf_fog_barrier", 16))
            g_fog_barrier = atoi(value) == 1;
        else if (!strncmp(line, "shaders", 7))
        {
            while (*value && *count < MAX_SHADERS)
            {
                uint64_t h = strtoull(value, &end, 16);
                if (end == value)
                    break;
                hashes[(*count)++] = h;
                value = end + strspn(end, " ,\t");
            }
        }
    }
    fclose(f);
    return 1;
}

/* Decides once whether to apply the fix, and loads the real d3d12.dll. */
static BOOL CALLBACK init_once(INIT_ONCE *once, void *param, void **context)
{
    WCHAR path[MAX_PATH], name[64];
    char running[32], expected[64] = "";
    uint64_t hashes[MAX_SHADERS];
    unsigned int count = 0, i;

    swprintf(name, ARRAYSIZE(name), L"%hs", LOG_NAME);
    path_next_to_self(path, MAX_PATH, name);
    g_log = _wfopen(path, L"w");
    log_msg("Spider-Man 2 T-Pose Fix " FIX_VERSION " (MGVF, on amd_ags_x64.dll)");

    game_build(running, sizeof(running));
    if (!read_settings(expected, sizeof(expected), hashes, &count))
        log_msg("No sm2fix.ini: not applying the fix.");
    else if (strcmp(running, expected))
        log_msg("This game build is %s, the fix was set up for %s (the game was updated). Not applying the fix;"
                " open the Spider-Man 2 T-Pose Fix app and click Update Fix.", running, expected[0] ? expected : "none");
    else if (!count)
        log_msg("sm2fix.ini lists no shaders: not applying the fix.");
    else
    {
        g_active = TRUE;
        for (i = 0; i < count; i++)
        {
            size_t size;
            void *data;

            swprintf(name, ARRAYSIZE(name), L"sm2fix-pool-%016llx.dxil", (unsigned long long)hashes[i]);
            path_next_to_self(path, MAX_PATH, name);
            data = read_file(path, &size);
            if (!data || size < 32 || memcmp(data, "DXBC", 4))
            {
                log_msg("Missing or damaged %ls: not applying the fix.", name);
                free(data);
                g_active = FALSE;
                break;
            }
            g_shaders[g_shader_count].hash = hashes[i];
            g_shaders[g_shader_count].data = data;
            g_shaders[g_shader_count].size = size;
            g_shader_count++;
        }
        if (g_active)
            log_msg("Game build %s matches. Applying the fix (%u shaders)%s.", running, g_shader_count,
                    g_fog_barrier ? ", with the fog barrier" : "");
    }
    return TRUE;
}

static void ensure_init(void)
{
    InitOnceExecuteOnce(&g_init, init_once, NULL, NULL);
}

/* ---------------------------------------------------------------- vtable hooks */

static void *orig_fn(void *obj, unsigned int index)
{
    void **vtbl = *(void ***)obj, *fn = NULL;
    unsigned int i;

    AcquireSRWLockShared(&g_lock);
    for (i = 0; i < g_hook_count; i++)
        if (g_hooks[i].vtbl == vtbl && g_hooks[i].index == index)
            fn = g_hooks[i].orig;
    ReleaseSRWLockShared(&g_lock);
    return fn;
}

static void hook_slot(void *obj, unsigned int index, void *hook)
{
    void **vtbl = *(void ***)obj;
    DWORD old;
    unsigned int i;

    AcquireSRWLockExclusive(&g_lock);
    for (i = 0; i < g_hook_count; i++)
        if (g_hooks[i].vtbl == vtbl && g_hooks[i].index == index)
            goto done;
    if (g_hook_count == ARRAYSIZE(g_hooks) || !vtbl[index] || vtbl[index] == hook
            || !VirtualProtect(&vtbl[index], sizeof(void *), PAGE_READWRITE, &old))
        goto done;
    g_hooks[g_hook_count].vtbl = vtbl;
    g_hooks[g_hook_count].index = index;
    g_hooks[g_hook_count].orig = vtbl[index];
    g_hook_count++;
    vtbl[index] = hook;
    VirtualProtect(&vtbl[index], sizeof(void *), old, &old);
done:
    ReleaseSRWLockExclusive(&g_lock);
}

/* ------------------------------------------------------------ fog barrier (MGVF) */

static void note_fog_pso(const void *code, size_t size, void *pso)
{
    LONG n;

    if (!g_fog_barrier || !pso || !code || size < 32 || sm2_shader_hash(code, size) != FOG_COPY_HASH)
        return;
    AcquireSRWLockExclusive(&g_lock);
    n = g_fog_pso_count;
    if (n < MAX_FOG_PSOS)
    {
        g_fog_psos[n] = pso;
        g_fog_pso_count = n + 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    log_msg("Fog copy pipeline %p seen (CS_VolumetricFogCopy)", pso);
}

static BOOL is_fog_pso(const void *pso)
{
    LONG i, n = g_fog_pso_count;

    for (i = 0; pso && i < n; i++)
        if (g_fog_psos[i] == pso)
            return TRUE;
    return FALSE;
}

static void STDMETHODCALLTYPE hk_SetPipelineState(ID3D12GraphicsCommandList *list, ID3D12PipelineState *pso)
{
    void (STDMETHODCALLTYPE *orig)(ID3D12GraphicsCommandList *, ID3D12PipelineState *)
            = orig_fn(list, LIST_IDX(SetPipelineState));

    t_bound.list = list;
    t_bound.fog = is_fog_pso(pso);
    orig(list, pso);
}

static HRESULT STDMETHODCALLTYPE hk_Reset(ID3D12GraphicsCommandList *list, ID3D12CommandAllocator *allocator,
        ID3D12PipelineState *pso)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D12GraphicsCommandList *, ID3D12CommandAllocator *, ID3D12PipelineState *)
            = orig_fn(list, LIST_IDX(Reset));

    t_bound.list = list;
    t_bound.fog = is_fog_pso(pso);
    return orig(list, allocator, pso);
}

static void STDMETHODCALLTYPE hk_Dispatch(ID3D12GraphicsCommandList *list, UINT x, UINT y, UINT z)
{
    void (STDMETHODCALLTYPE *orig)(ID3D12GraphicsCommandList *, UINT, UINT, UINT) = orig_fn(list, LIST_IDX(Dispatch));

    orig(list, x, y, z);
    if (t_bound.list == list && t_bound.fog)
    {
        D3D12_RESOURCE_BARRIER barrier;

        memset(&barrier, 0, sizeof(barrier));
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = NULL;   /* every UAV access before, against every one after */
        list->lpVtbl->ResourceBarrier(list, 1, &barrier);
        InterlockedIncrement(&g_fog_barriers);
    }
}

static void hook_list(void *list)
{
    hook_slot(list, LIST_IDX(SetPipelineState), (void *)hk_SetPipelineState);
    hook_slot(list, LIST_IDX(Reset), (void *)hk_Reset);
    hook_slot(list, LIST_IDX(Dispatch), (void *)hk_Dispatch);
}

static HRESULT STDMETHODCALLTYPE hk_CreateCommandList(ID3D12Device *device, UINT mask, D3D12_COMMAND_LIST_TYPE type,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *pso, REFIID riid, void **list)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D12Device *, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *,
            ID3D12PipelineState *, REFIID, void **) = orig_fn(device, DEV_IDX(CreateCommandList));
    HRESULT hr = orig(device, mask, type, allocator, pso, riid, list);

    if (SUCCEEDED(hr) && list && *list)
    {
        hook_list(*list);
        t_bound.list = *list;
        t_bound.fog = is_fog_pso(pso);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_CreateCommandList1(ID3D12Device4 *device, UINT mask, D3D12_COMMAND_LIST_TYPE type,
        D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void **list)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D12Device4 *, UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS,
            REFIID, void **) = orig_fn(device, offsetof(ID3D12Device4Vtbl, CreateCommandList1) / sizeof(void *));
    HRESULT hr = orig(device, mask, type, flags, riid, list);

    if (SUCCEEDED(hr) && list && *list)
        hook_list(*list);
    return hr;
}

/* The rewritten copy of a shader, if it is one of the fix's. */
static const void *replacement(const void *code, size_t size, size_t *out_size)
{
    uint64_t hash;
    unsigned int i;

    if (!code || size < 32)
        return NULL;
    hash = sm2_shader_hash(code, size);
    for (i = 0; i < g_shader_count; i++)
    {
        if (g_shaders[i].hash == hash)
        {
            *out_size = g_shaders[i].size;
            return g_shaders[i].data;
        }
    }
    return NULL;
}

static HRESULT STDMETHODCALLTYPE hk_CreateComputePipelineState(ID3D12Device *device,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pso)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D12Device *, const D3D12_COMPUTE_PIPELINE_STATE_DESC *, REFIID, void **)
            = orig_fn(device, DEV_IDX(CreateComputePipelineState));
    const void *alt;
    size_t alt_size;

    if (desc && (alt = replacement(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, &alt_size)))
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC copy = *desc;
        HRESULT hr;

        copy.CS.pShaderBytecode = alt;
        copy.CS.BytecodeLength = alt_size;
        hr = orig(device, &copy, riid, pso);
        log_msg("Replaced shader %016llx: %s", (unsigned long long)sm2_shader_hash(desc->CS.pShaderBytecode,
                desc->CS.BytecodeLength), SUCCEEDED(hr) ? "ok" : "FAILED, using the game's own");
        if (SUCCEEDED(hr))
        {
            InterlockedIncrement(&g_pipelines);
            return hr;
        }
    }
    {
        HRESULT hr = orig(device, desc, riid, pso);

        if (SUCCEEDED(hr) && desc && pso)
            note_fog_pso(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, *pso);
        return hr;
    }
}

static HRESULT STDMETHODCALLTYPE hk_CreatePipelineState(ID3D12Device2 *device,
        const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pso)
{
    HRESULT (STDMETHODCALLTYPE *orig)(ID3D12Device2 *, const D3D12_PIPELINE_STATE_STREAM_DESC *, REFIID, void **)
            = orig_fn(device, DEV_IDX(CreatePipelineState));
    const void *code = NULL, *alt;
    size_t size = 0, alt_size;
    long at;

    if (desc && sm2_stream_find_cs(desc->pPipelineStateSubobjectStream, desc->SizeInBytes, &code, &size) == SM2_STREAM_COMPUTE
            && (alt = replacement(code, size, &alt_size))
            && (at = sm2_stream_cs_offset(desc->pPipelineStateSubobjectStream, desc->SizeInBytes)) >= 0)
    {
        BYTE *copy = malloc(desc->SizeInBytes);
        D3D12_PIPELINE_STATE_STREAM_DESC patched = *desc;
        UINT64 len = alt_size;
        HRESULT hr;

        if (copy)
        {
            memcpy(copy, desc->pPipelineStateSubobjectStream, desc->SizeInBytes);
            memcpy(copy + at + 8, &alt, sizeof(alt));
            memcpy(copy + at + 16, &len, sizeof(len));
            patched.pPipelineStateSubobjectStream = copy;
            hr = orig(device, &patched, riid, pso);
            free(copy);
            log_msg("Replaced shader %016llx: %s", (unsigned long long)sm2_shader_hash(code, size),
                    SUCCEEDED(hr) ? "ok" : "FAILED, using the game's own");
            if (SUCCEEDED(hr))
            {
                InterlockedIncrement(&g_pipelines);
                return hr;
            }
        }
    }
    {
        HRESULT hr = orig(device, desc, riid, pso);

        if (SUCCEEDED(hr) && pso && code)
            note_fog_pso(code, size, *pso);
        else if (SUCCEEDED(hr) && pso && desc && g_fog_barrier
                && sm2_stream_find_cs(desc->pPipelineStateSubobjectStream, desc->SizeInBytes, &code, &size) == SM2_STREAM_COMPUTE)
            note_fog_pso(code, size, *pso);
        return hr;
    }
}

static void STDMETHODCALLTYPE hk_CreateShaderResourceView(ID3D12Device *device, ID3D12Resource *resource,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    void (STDMETHODCALLTYPE *orig)(ID3D12Device *, ID3D12Resource *, const D3D12_SHADER_RESOURCE_VIEW_DESC *,
            D3D12_CPU_DESCRIPTOR_HANDLE) = orig_fn(device, DEV_IDX(CreateShaderResourceView));

    if (desc && desc->ViewDimension == D3D12_SRV_DIMENSION_BUFFER && desc->Buffer.NumElements > WIDE_VIEW
            && !(desc->Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW)
            && (desc->Format == DXGI_FORMAT_R32_UINT || desc->Format == DXGI_FORMAT_R32_SINT
                || desc->Format == DXGI_FORMAT_R32_FLOAT))
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC raw = *desc;

        raw.Format = DXGI_FORMAT_R32_TYPELESS;
        raw.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
        raw.Buffer.StructureByteStride = 0;
        if (InterlockedIncrement(&g_views) <= 4)
            log_msg("Wide typed view (%u elements) created as a raw view", desc->Buffer.NumElements);
        orig(device, resource, &raw, handle);
        return;
    }
    orig(device, resource, desc, handle);
}

static void STDMETHODCALLTYPE hk_CreateUnorderedAccessView(ID3D12Device *device, ID3D12Resource *resource,
        ID3D12Resource *counter, const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    void (STDMETHODCALLTYPE *orig)(ID3D12Device *, ID3D12Resource *, ID3D12Resource *,
            const D3D12_UNORDERED_ACCESS_VIEW_DESC *, D3D12_CPU_DESCRIPTOR_HANDLE)
            = orig_fn(device, DEV_IDX(CreateUnorderedAccessView));

    if (desc && desc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER && desc->Buffer.NumElements > WIDE_VIEW
            && !counter && !(desc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW)
            && (desc->Format == DXGI_FORMAT_R32_UINT || desc->Format == DXGI_FORMAT_R32_SINT
                || desc->Format == DXGI_FORMAT_R32_FLOAT))
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC raw = *desc;

        raw.Format = DXGI_FORMAT_R32_TYPELESS;
        raw.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
        raw.Buffer.StructureByteStride = 0;
        raw.Buffer.CounterOffsetInBytes = 0;
        if (InterlockedIncrement(&g_uavs) <= 4)
            log_msg("Wide typed UAV (%u elements) created as a raw view", desc->Buffer.NumElements);
        orig(device, resource, counter, &raw, handle);
        return;
    }
    orig(device, resource, counter, desc, handle);
}

static void hook_device(IUnknown *device)
{
    static const struct { const IID *iid; int has_stream; } ifaces[] =
    {
        {&IID_ID3D12Device, 0}, {&IID_ID3D12Device1, 0}, {&IID_ID3D12Device2, 1}, {&IID_ID3D12Device3, 1},
        {&IID_ID3D12Device4, 1}, {&IID_ID3D12Device5, 1}, {&IID_ID3D12Device6, 1}, {&IID_ID3D12Device7, 1},
        {&IID_ID3D12Device8, 1}, {&IID_ID3D12Device9, 1}, {&IID_ID3D12Device10, 1},
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(ifaces); i++)
    {
        IUnknown *iface = NULL;

        if (FAILED(device->lpVtbl->QueryInterface(device, ifaces[i].iid, (void **)&iface)) || !iface)
            continue;
        hook_slot(iface, DEV_IDX(CreateComputePipelineState), (void *)hk_CreateComputePipelineState);
        hook_slot(iface, DEV_IDX(CreateShaderResourceView), (void *)hk_CreateShaderResourceView);
        hook_slot(iface, DEV_IDX(CreateUnorderedAccessView), (void *)hk_CreateUnorderedAccessView);
        if (ifaces[i].has_stream)
            hook_slot(iface, DEV_IDX(CreatePipelineState), (void *)hk_CreatePipelineState);
        if (g_fog_barrier)
        {
            hook_slot(iface, DEV_IDX(CreateCommandList), (void *)hk_CreateCommandList);
            if (ifaces[i].iid != &IID_ID3D12Device && ifaces[i].iid != &IID_ID3D12Device1
                    && ifaces[i].iid != &IID_ID3D12Device2 && ifaces[i].iid != &IID_ID3D12Device3)
                hook_slot(iface, offsetof(ID3D12Device4Vtbl, CreateCommandList1) / sizeof(void *),
                        (void *)hk_CreateCommandList1);
        }
        iface->lpVtbl->Release(iface);
    }
}


/* ------------------------------------------------- reaching D3D12CreateDevice */

typedef HRESULT (WINAPI *create_device_fn)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
static create_device_fn g_real_create;

static HRESULT WINAPI mgvf_D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL level, REFIID riid, void **device)
{
    HRESULT hr;

    ensure_init();
    hr = g_real_create(adapter, level, riid, device);
    if (SUCCEEDED(hr) && g_active && device && *device)
        hook_device(*device);
    return hr;
}

/* An export is a 32-bit offset from the module, so it can only point at code
 * within 4 GB above it: a jump to mgvf_D3D12CreateDevice is placed there. */
static void *jump_near(BYTE *base, SIZE_T image, void *target)
{
    BYTE *at;

    for (at = base + ((image + 0xFFFF) & ~(SIZE_T)0xFFFF); at < base + 0xF0000000u; at += 0x10000)
    {
        BYTE *page = VirtualAlloc(at, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!page)
            continue;
        page[0] = 0xFF; page[1] = 0x25; memset(page + 2, 0, 4);   /* jmp [rip+0] */
        memcpy(page + 6, &target, sizeof(target));
        FlushInstructionCache(GetCurrentProcess(), page, 14);
        return page;
    }
    return NULL;
}

static void redirect_create_device(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *exports;
    DWORD *names, *functions, old, i;
    WORD *ordinals;
    void *jump;

    if (g_real_create || !dir.VirtualAddress)
        return;
    exports = (IMAGE_EXPORT_DIRECTORY *)(base + dir.VirtualAddress);
    names = (DWORD *)(base + exports->AddressOfNames);
    ordinals = (WORD *)(base + exports->AddressOfNameOrdinals);
    functions = (DWORD *)(base + exports->AddressOfFunctions);
    for (i = 0; i < exports->NumberOfNames; i++)
    {
        DWORD *slot;
        if (strcmp((const char *)(base + names[i]), "D3D12CreateDevice"))
            continue;
        slot = &functions[ordinals[i]];
        if (!(jump = jump_near(base, nt->OptionalHeader.SizeOfImage, (void *)mgvf_D3D12CreateDevice)))
            return;
        g_real_create = (create_device_fn)(base + *slot);
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        {
            *slot = (DWORD)((BYTE *)jump - base);
            VirtualProtect(slot, sizeof(*slot), old, &old);
        }
        return;
    }
}

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } MGVF_UNICODE_STRING;
typedef struct
{
    ULONG Flags;
    const MGVF_UNICODE_STRING *FullDllName, *BaseDllName;
    void *DllBase;
    ULONG SizeOfImage;
} MGVF_DLL_LOADED;
typedef void (CALLBACK *dll_notify_fn)(ULONG reason, const MGVF_DLL_LOADED *data, void *context);
typedef LONG (NTAPI *register_notify_fn)(ULONG flags, dll_notify_fn callback, void *context, void **cookie);

/* Runs under the loader lock: it only edits the new module's export table. */
static void CALLBACK on_dll(ULONG reason, const MGVF_DLL_LOADED *data, void *context)
{
    if (reason == 1 && data && data->BaseDllName && data->BaseDllName->Buffer
            && data->BaseDllName->Length == 9 * sizeof(WCHAR)
            && !_wcsnicmp(data->BaseDllName->Buffer, L"d3d12.dll", 9))
        redirect_create_device((HMODULE)data->DllBase);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        register_notify_fn reg = (register_notify_fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                "LdrRegisterDllNotification");
        HMODULE loaded = GetModuleHandleW(L"d3d12.dll");
        void *cookie;

        g_self = instance;
        DisableThreadLibraryCalls(instance);
        if (loaded)
            redirect_create_device(loaded);
        else if (reg)
            reg(0, on_dll, NULL, &cookie);
    }
    else if (reason == DLL_PROCESS_DETACH && g_log)
    {
        if (g_active)
            fprintf(g_log, "Exit: %ld shaders replaced, %ld wide views and %ld wide UAVs made raw, "
                    "%ld fog copy pipelines, %ld fog barriers\n", (long)g_pipelines, (long)g_views, (long)g_uavs,
                    (long)g_fog_pso_count, (long)g_fog_barriers);
        fclose(g_log);
        g_log = NULL;
    }
    return TRUE;
}
