#!/usr/bin/env python3
#
# Which controller library each game can reach, read from its files.
#
#     diagnostics/scan-pad-libraries.py <library dir> [more dirs...] [-o report.txt]
#
# One line per game: the markers found, and the class they add up to. Nothing is
# executed and nothing needs to be installed twice; the scan reads the game's
# executables and its largest DLLs and looks for the names a controller library
# leaves behind -- import names, exported symbols, the strings a plugin carries.
#
# WHY THE CLASS MATTERS. A DualSense under wine on macOS is a different problem
# per class, and the per-title options this project's launcher writes are
# different per class too:
#
#   A   Sony's own SDK (libScePad)     -- drives the pad itself, needs nothing
#                                        from us, and XInput rumble should be
#                                        OFF for it: measured 2026-09-10, it
#                                        costs a title nothing and ours costs 2 fps
#   A*  Unreal's WinDualShock plugin   -- the same, through Unreal
#   B/C XInput                          -- wants mgvf-0010's rumble and, without
#                                        a glyph selector, wants the pad to
#                                        present as Sony (mgvf-0002)
#   D   Steam Input API only            -- Steam decides; leave it alone
#
# WHAT A SCAN CAN AND CANNOT SAY. Imports say what a title CAN use, not what it
# does: a game may import xinput and go through Steam Input at run time.
# So this proposes and a +hid trace (diagnostics/capture-hid-trace.sh) confirms
# -- that trace shows exactly which reports a title writes, and it is the ground
# truth this scan is calibrated against.
#
# THE REPORT IS NOT FOR THE REPOSITORY. It lists somebody's installed games.
# It goes where -o says, and by default beside the caller, never here.
#
# Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
import os, sys, re, collections

DLL_MARKERS = {
  "libscepad": "ships:sony-sdk",
  "xinput1_1": "ships:xinput", "xinput1_2": "ships:xinput", "xinput1_3": "ships:xinput",
  "xinput1_4": "ships:xinput", "xinput9_1_0": "ships:xinput",
  "steam_api": "ships:steam-api", "sdl2": "ships:sdl", "sdl3": "ships:sdl",
  "dinput8": "ships:dinput", "gameinput": "ships:gameinput",
  "unityplayer": "engine:unity", "gameassembly": "engine:unity",
}
STR_MARKERS = [
  (b"libScePad", "sony-sdk"), (b"scePadSetVibration", "sony-sdk"), (b"scePadInit", "sony-sdk"),
  (b"WinDualShock", "unreal-ps-plugin"), (b"DualSense", "mentions-dualsense"),
  (b"xinput1_4.dll", "xinput"), (b"xinput1_3.dll", "xinput"), (b"XINPUT1_4.dll", "xinput"),
  (b"XINPUT1_3.dll", "xinput"), (b"xinput9_1_0.dll", "xinput"), (b"XInputGetState", "xinput"),
  (b"XInputSetState", "xinput-rumble"),
  (b"HidD_SetOutputReport", "raw-hid-write"), (b"HidD_GetAttributes", "raw-hid"),
  (b"steam_api64.dll", "steam-api"), (b"SteamInput", "steam-input-api"), (b"ISteamInput", "steam-input-api"),
  (b"SDL2.dll", "sdl"), (b"SDL_GameController", "sdl"), (b"SDL_JoystickRumble", "sdl"),
  (b"dinput8.dll", "dinput"), (b"DirectInput8Create", "dinput"),
  (b"GameInput.dll", "gameinput"), (b"IGameInput", "gameinput"),
  (b"Windows.Gaming.Input", "wgi"),
]
# Not the game: installers, redistributables, crash handlers, anti-cheat.
SKIP = re.compile(r"(unins|redist|vcredist|dxsetup|crash|report|EasyAntiCheat|BattlEye|dotnet|python|node\.exe)", re.I)

def classify(t):
    if "sony-sdk" in t or "ships:sony-sdk" in t: return "A   sony-sdk"
    if "unreal-ps-plugin" in t: return "A*  unreal-windualshock"
    if "xinput-rumble" in t or "xinput" in t or "ships:xinput" in t: return "B/C xinput"
    if "steam-input-api" in t: return "D   steam-input-only"
    if "sdl" in t or "ships:sdl" in t: return "SDL"
    if "raw-hid-write" in t: return "raw-hid"
    if "dinput" in t or "ships:dinput" in t: return "dinput-only"
    if "engine:unity" in t: return "unity-unmarked"
    return "?   unmarked"

def scan_file(path, found):
    try:
        size = os.path.getsize(path)
        if size < 100_000 or size > 1_500_000_000: return
        with open(path, "rb") as f: data = f.read()
        for needle, tag in STR_MARKERS:
            if needle in data: found.add(tag)
    except Exception: pass

def scan_game(gdir):
    found, exes, dlls = set(), [], []
    for dp, dn, fn in os.walk(gdir):
        if dp.count(os.sep) - gdir.count(os.sep) > 6: dn[:] = []; continue
        for n in fn:
            low = n.lower(); p = os.path.join(dp, n)
            if low.endswith(".dll"):
                for m, tag in DLL_MARKERS.items():
                    if low.startswith(m): found.add(tag)
                if not SKIP.search(n): dlls.append(p)
            elif low.endswith(".exe") and not SKIP.search(n): exes.append(p)
    size = lambda p: os.path.getsize(p) if os.path.exists(p) else 0
    # The executables, and the largest DLLs beside them: a packed or thin .exe
    # says nothing, and the engine that actually reads the pad is usually a DLL.
    for p in sorted(exes, key=size, reverse=True)[:4]: scan_file(p, found)
    for p in sorted(dlls, key=size, reverse=True)[:8]: scan_file(p, found)
    return found

def main(argv):
    out = None
    if "-o" in argv:
        i = argv.index("-o"); out = argv[i+1]; argv = argv[:i] + argv[i+2:]
    roots = argv or []
    if not roots:
        print(__doc__ or "usage: scan-pad-libraries.py <library dir> [...] [-o report]"); return 2
    lines, tally = [], collections.Counter()
    for root in roots:
        if not os.path.isdir(root): print(f"no such directory: {root}", file=sys.stderr); continue
        for game in sorted(os.listdir(root)):
            gdir = os.path.join(root, game)
            if not os.path.isdir(gdir): continue
            found = scan_game(gdir)
            c = classify(found); tally[c] += 1
            line = f"{c:<24s} {game:<48s} {' '.join(sorted(found))}"
            lines.append(line); print(line, flush=True)
    summary = ["", f"{sum(tally.values())} games"] + [f"  {v:4d}  {k}" for k, v in sorted(tally.items(), key=lambda x: -x[1])]
    for s in summary: print(s)
    if out:
        with open(out, "w") as f: f.write("\n".join(lines + summary) + "\n")
        print(f"\nreport: {out}")
    return 0

if __name__ == "__main__": sys.exit(main(sys.argv[1:]))
