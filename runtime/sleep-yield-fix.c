/*
 * Give a spin loop a real yield, now and then.
 *
 * METAL GEAR SOLID 4 loads its audio banks behind a busy-wait: measured on this
 * machine, four and a half million Sleep calls before its menu, about fifteen
 * thousand a second, while file I/O sits idle and nothing advances. On Windows
 * that costs little. Under Wine each of those crosses more machinery, and
 * twenty threads doing it at once starve whichever one actually holds the work.
 *
 * How this fix was found, which is worth writing down because it was an
 * accident: a diagnostic probe that wrapped Sleep, WaitForSingleObject and
 * ReadFile made the title load. The reading at the time was that the probe's
 * GST_REGISTRY change was doing it. It was not -- built as a fix that only set
 * that variable, with the wrappers gone, and the title stopped loading again
 * while the log confirmed the variable had been set. What the probe was
 * contributing was its own overhead: one indirect call on each of four and a
 * half million Sleeps. It was not observing the fault, it was hiding it.
 *
 * So this does deliberately, and in one place, what that probe did by mistake:
 * every SLEEP_EVERY'th Sleep(0) becomes a Sleep(1), which yields the processor
 * for real instead of spinning against itself. The rest are passed through
 * untouched, and any Sleep with a duration is never touched at all.
 *
 * At fifteen thousand calls a second and a divisor of 64 that is roughly two
 * hundred real yields a second -- enough to let another thread run, far short
 * of pacing the game. The divisor is read from C:\mgvf-sleep.txt if it is
 * there, because the right number is a property of the machine and not
 * something to guess once and compile in.
 *
 * Inert where the fault is absent: a title that does not spin on Sleep(0)
 * never reaches the branch, and this file does nothing else.
 *
 * Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <stdio.h>

#define LOGFILE "C:\\sleep-yield-fix.log"

static void (WINAPI *real_Sleep)(DWORD);
static LONG spun, yielded;
static LONG sleep_every = 64;

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

static void WINAPI my_Sleep(DWORD ms)
{
    if (ms == 0)
    {
        LONG n = InterlockedIncrement(&spun);
        if (sleep_every > 0 && (n % sleep_every) == 0)
        {
            InterlockedIncrement(&yielded);
            real_Sleep(1);
            return;
        }
    }
    real_Sleep(ms);
}

/* Patch one imported function in the main executable's import table. */
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

static void read_divisor(void)
{
    char buf[33];
    DWORD got = 0;
    HANDLE f = CreateFileA("C:\\mgvf-sleep.txt", GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (ReadFile(f, buf, sizeof(buf) - 1, &got, NULL) && got) buf[got] = 0; else buf[0] = 0;
    CloseHandle(f);
    {
        int n = 0, i;
        for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++) n = n * 10 + (buf[i] - '0');
        /* 0 disables the fix, which is the point of being able to say it. */
        if (i > 0 && n >= 0 && n < 100000) sleep_every = n;
    }
}

static DWORD WINAPI arm(LPVOID unused)
{
    void *was;
    (void)unused;
    read_divisor();
    was = hook_import("kernel32.dll", "Sleep", (void *)my_Sleep);
    if (was)
    {
        real_Sleep = (void (WINAPI *)(DWORD))was;
        logf_("Sleep hooked -- every %ld'th Sleep(0) becomes a real yield%s",
              (long)sleep_every, sleep_every ? "" : " (disabled by C:\\mgvf-sleep.txt)");
    }
    else
        logf_("Sleep is not in this game's import table -- nothing to do");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        /* Off the loader lock: hook_import takes VirtualProtect, and the
         * import table is not fully bound while DllMain runs. */
        CreateThread(NULL, 0, arm, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH && real_Sleep)
        logf_("%ld spins, %ld of them yielded for real", (long)spun, (long)yielded);
    return TRUE;
}
