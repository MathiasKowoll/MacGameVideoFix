/*
 * One byte, in memory, and nothing else.
 *
 * RESONANCE refuses to start on a device that reports Shader Model 6.6:
 *
 *     Fatal error
 *     Shader Model 6.7 is not supported by this device!
 *     Maximum Shader Model supported is 6.6
 *
 * The check is one comparison, and everything either side of it is correct --
 * D3DMetal has 6.6 because it has 6.6, and the game's own store page asks for
 * 6.6 anyway. What is wrong is the floor:
 *
 *     8B 85 B8 07 00 00     mov  eax, [rbp+7B8h]     ; highest model it has
 *     83 F8 67              cmp  eax, 67h            ; 6.7
 *     0F 8D 8C 00 00 00     jge  past_the_error
 *
 * 67h becomes 66h and the game accepts what it was given. Nothing on disk is
 * touched: Resonance.exe stays exactly as Steam installed it, so a verification
 * does not undo this, an update does not fight it, and removing the carrier
 * gives the game back byte for byte. That is the whole difference between this
 * and the hex edit going around.
 *
 * By pattern rather than by address, because the offset that circulates is not
 * every build: 0x15C73E6 holds FFh here, and the comparison is at 0x15CD606
 * instead. A single unambiguous match is required -- two means the pattern has
 * stopped being specific and choosing between them would be a guess.
 *
 * WHY THIS FILE EXISTS SEPARATELY, which is the part worth reading.
 *
 * This started as a fifth guard inside d3d12-guards.c, and the game then ran to
 * a black screen while rendering five hundred draws a frame at eighty frames a
 * second. That file hooks D3D12CreateDevice, D3D11CreateDevice,
 * EnumDisplayMonitors and GetProcAddress, watches monitors, and retries device
 * creation -- all of it earned by other titles, none of it wanted by this one.
 * Carrying four repairs a game does not need in order to deliver one it does is
 * how a fix acquires a fault of its own.
 *
 * So: no hooks, no vtables, no logging beyond a single line, and one write.
 *
 * Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <stdio.h>

#define LOGFILE "C:\\shader-floor-fix.log"

/* cmp eax, 67h ; jge rel32 -- the immediate is at [2]. */
static const BYTE SM_FLOOR[] = { 0x83, 0xF8, 0x67, 0x0F, 0x8D };

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

    h = CreateFileA(LOGFILE, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, (DWORD)(n + 1), &written, NULL);
    CloseHandle(h);
}

static void lower_the_floor(void)
{
    HMODULE base = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sec;
    BYTE *found = NULL;
    int hits = 0;
    unsigned i;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    nt = (IMAGE_NT_HEADERS *)((BYTE *)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    sec = IMAGE_FIRST_SECTION(nt);

    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        BYTE *p, *end;
        if (memcmp(sec->Name, ".text", 5) != 0) continue;
        p   = (BYTE *)base + sec->VirtualAddress;
        end = p + sec->Misc.VirtualSize - sizeof(SM_FLOOR);
        for (; p < end; p++)
        {
            if (p[0] != SM_FLOOR[0] || memcmp(p, SM_FLOOR, sizeof(SM_FLOOR)) != 0) continue;
            if (++hits == 1) found = p;
        }
    }

    if (hits != 1)
    {
        logf_("%d places match the pattern -- leaving all of them alone", hits);
        return;
    }
    {
        DWORD old;
        if (!VirtualProtect(found + 2, 1, PAGE_EXECUTE_READWRITE, &old))
        {
            logf_("VirtualProtect refused (err %lu)", GetLastError());
            return;
        }
        found[2] = 0x66;
        VirtualProtect(found + 2, 1, old, &old);
        logf_("shader model floor 6.7 -> 6.6 at +0x%lX, in memory only%s",
              (unsigned long)(found - (BYTE *)base),
              found[2] == 0x66 ? "" : " -- but the write did not stick");
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        lower_the_floor();
    }
    return TRUE;
}
