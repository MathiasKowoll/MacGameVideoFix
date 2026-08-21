/*
 * ue5-vpx-cpupath — makes Unreal's Electra VPx decoder take its CPU output
 * path, so VP9 cutscenes play under CrossOver instead of crashing.
 *
 * The problem
 * -----------
 * Apple's D3DMetal does not implement ID3DDestructionNotifier. Electra's D3D12
 * output buffer pool asks every resource for it and uses the answer without
 * checking the HRESULT, so the first VP9 frame dereferences a null vtable:
 *
 *     Res = Resource->QueryInterface(__uuidof(ID3DDestructionNotifier), &N);
 *     check(SUCCEEDED(Res));            // compiled out in Shipping builds
 *     N->RegisterDestructionCallback(); // null deref
 *
 * H.264 and H.265 can avoid that pool with Electra.Win.H264UseOldOutputPath.
 * VPx has no such CVar -- the shipped binaries contain only the H264 and H265
 * ones -- so VP9 on D3D12 has no way out through configuration.
 *
 * The fix
 * -------
 * Electra decides whether to use the pool by comparing the D3D version against
 * 12000, in code the compiler emits as:
 *
 *     cmp dword [rbp+disp], 12000
 *     jl  <cpu path>
 *
 * Raising that threshold to INT_MAX makes the comparison always take the CPU
 * branch -- the same path every D3D11 machine already uses, and the same one
 * H.264 reaches through its CVar. Nothing else changes: the decoder still
 * decodes VP9 with its own libvpx, and Unreal presents the frames normally.
 *
 * We patch the immediate rather than the jump so the edit is a plain four-byte
 * write with no relative displacement to recompute.
 *
 * This is engine code, not game code, so it is not specific to any one title.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define ELECTRA_D3D12_VERSION   0x2EE0        /* 12000 */
#define UNREACHABLE_VERSION     0x7FFFFFFF

static void logf_(const char *fmt, ...)
{
    char buf[512];
    HANDLE h;
    DWORD written;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = '\n';

    /* A game under CrossOver cannot be attached to, so leave a trail. */
    h = CreateFileA("C:\\ue5-vpx-cpupath.log", FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, buf, n + 1, &written, NULL);
    CloseHandle(h);
}

/* Is `at` a jl? Both encodings appear: rel8 near the flag assignment, rel32
 * where the skipped block is large. We only need to recognise them -- the jump
 * itself is left alone. */
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

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    apply();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    HANDLE thread;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        /* Off the loader lock: DllMain must not do real work. */
        thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
