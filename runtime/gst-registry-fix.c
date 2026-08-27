/*
 * One environment variable, set early, because the launcher owns the rest.
 *
 * GStreamer caches its plugin scan in a registry file, and the file stores
 * absolute plugin paths. Two engines on this machine point at the same one:
 * CrossOver's default, ~/Library/Application Support/CrossOver/
 * gstreamer-1.0-registry.x86_64.bin. Their plugin sets differ -- stock carries
 * eighteen, an engine with winevideo's plugins inside carries twenty-one -- so
 * every alternation between them invalidates the cache and forces a full
 * rescan, inside the process, while the game waits.
 *
 * METAL GEAR SOLID 4 is where this became visible: it reaches winegstreamer
 * through xaudio2_9 -> mfplat without ever asking for a codec, and on the fork
 * it would sit on a black screen making millions of Sleep calls while Steam's
 * client pipe timed out and the title killed itself with a fatal assert.
 * Measured 27 Aug 2026: with a private registry it loads.
 *
 * Why from inside the process, which is not where an environment variable
 * belongs:
 *
 *   - cxbottle.conf is wiped. RaccoonBot and Procyon clear the environment and
 *     set their own on every launch, so a line there does not survive.
 *   - The launcher's own per-game "Env variables" field loses to the launcher.
 *     Asked for GST_REGISTRY there and the process still received the
 *     launcher's value -- measured, not assumed.
 *
 * That leaves here. This DLL is a static import of the game, so it loads before
 * mfplat does, and whoever reads GST_REGISTRY afterwards reads ours.
 *
 * The path is inside the bottle, so each bottle gets its own and two engines
 * stop sharing. It has no spaces, deliberately: a path with spaces did not
 * survive the launcher's field, and there is no reason to discover the hard way
 * what else it would not survive.
 *
 * Inert where the fault is absent: a bottle whose registry is already private
 * is left alone, and nothing else in this file touches the game.
 *
 * Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <stdio.h>

#define OWN_REGISTRY "C:\\mgvf-gst-registry.bin"
#define LOGFILE      "C:\\gst-registry-fix.log"

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

static void point_registry_somewhere_of_our_own(void)
{
    char had[512] = "";
    DWORD n = GetEnvironmentVariableA("GST_REGISTRY", had, sizeof(had) - 1);

    if (n && n < sizeof(had) && !lstrcmpiA(had, OWN_REGISTRY))
        return;                      /* already ours, nothing to say */

    if (SetEnvironmentVariableA("GST_REGISTRY", OWN_REGISTRY))
        logf_("GST_REGISTRY -> %s   (was %s)", OWN_REGISTRY, n ? had : "unset");
    else
        logf_("could not set GST_REGISTRY (err %lu) -- the shared cache stays",
              GetLastError());
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        point_registry_somewhere_of_our_own();
    }
    return TRUE;
}
