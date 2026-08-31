# Tormented Souls 2

The first entry here whose cause is not a gap in the graphics translation. The
GPU is fine, the driver is fine, the modes it reports are fine. The game has no
branch for a display that is not widescreen, and a MacBook display is not
widescreen.

| | |
| --- | --- |
| Symptom | `Fatal error! Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0xfffffffffffffff8` before the first frame |
| Cause | The game keeps only 16:9 resolutions and has no fallback when none exist |
| Fix | Put 16:9 modes in the list the game reads |
| Backend | **D3DMetal**, D3D12 (and D3D11 — it fails identically either way) |
| CrossOver | Stable 26.3 with Game Porting Toolkit 4.0b2 (2026-08-31). The measurements below were made on `crossover-preview-arm64-20260821`, which is no longer a supported engine |

## The address is the whole clue

`0xfffffffffffffff8` is not a random pointer. It is −8. An access violation at
−8 means something computed `base + stride * index` with a null base and an
index of −1, and −1 in Unreal is `INDEX_NONE`, the value a search returns when
it finds nothing.

So the question was never "what is broken in D3D12". It was "what did the game
look for and not find".

## Reading the game instead of guessing

Four runs went into DXGI on a hunch before anything was measured. That was
wasted work: the import table said which enumeration API the executable pulls
in, and it could have been read without launching anything. The lesson is
written down in the project's internal notes and it is the same one every time
— the binary answers questions that guessing only postpones.

What the disassembly says, following the crashing address backwards through the
exception directory (`.pdata` gives exact function bounds, so each function can
be disassembled alone):

    the crashing function
      call  <find the current mode>     ; returns the index
      mov   [rdi+0x28], eax             ; store it, no test
      mov   rdx, [rdi+0x98]             ; the array base
      movsxd rax, [rdi+0x28]            ; sign-extend the index
      mov   rdx, [rdx + 12*rax + 4]     ; base is null, index is -1

    <find the current mode>
      empties the array at this+0x98, fills it, returns -1 when empty

    <fill it>
      asks the RHI for the available resolutions   -> succeeds, 13 of them
      then, for each one:
        comisd  against 1.76
        comisd  against 1.79
        keep it only if it falls strictly between

The stride of 12 matches Unreal's `FScreenResolutionRHI` exactly: width,
height, refresh rate, three 32-bit fields. And the two doubles are constants in
the game's own `.rdata`. Strictly between 1.76 and 1.79 is 16:9 and nothing
else.

## Why that empties the array here

This screen is 2056×1329. That is an aspect ratio of 1.547. Every mode the
system offers for it is either 1.6 or 1.547, because they are all modes for
*this* panel.

Thirteen resolutions come back. Not one of them passes the filter. The array
stays empty, the search returns `INDEX_NONE`, and the very next instruction
indexes the empty array with it.

Nothing here is a translation defect. Ask Windows for the modes of a 3:2
monitor and you get 3:2 modes, and this code would read −8 on Windows too. It
is a game that assumes every display is widescreen, which on a desktop monitor
is nearly always true and on a laptop is nearly always false.

## The fix

`IDXGIOutput::GetDisplayModeList` is asked twice: once with a null array to
count, once to fill. The guard reserves a few slots in the count, lets the real
call fill the rest, and then looks at what came back. If any mode falls in the
game's window it returns having changed nothing. If none does, it appends the
16:9 ladder that fits inside the desktop — 1920×1080, 1600×900, 1366×768 and
1280×720 on a screen this size — inheriting format and scaling from the modes
already there.

    mode list for format 28: 26 modes, largest 2056x1329; the screen is 2056x1329
    not one of the 26 modes on offer is 16:9, <!-- count-ok --> and this game keeps nothing
    else -- added 4 of them, largest 1920x1080 @ 120 Hz

The game then finds resolutions it is willing to keep, the search returns a
real index, and it starts.

## Why it only crashes once

The filtered array is not rebuilt every launch. The game writes it into
`Saved/SaveGames/Settings.sav` under the name `AspectRatioResolutions` — the
same name the code uses — and reads it back on later runs instead of asking
again.

That has a consequence worth knowing before anyone tries to reproduce this. Once
the game has started successfully a single time, the list is on disk and the
enumeration path is not taken again, so the guard never fires and the game keeps
working whether or not it is installed. The crash belongs to a profile that has
never had a good launch, which is to say a new installation.

It also made this fix hard to verify honestly. Two rounds of "it works" were
recorded here against runs where the guard was demonstrably never called: the
log showed it installed and never entered. What settled it was deleting
`Settings.sav`, which is the only way back to a first launch:

    mode list for format 28: 26 modes, largest 2056x1329; the screen is 2056x1329
    not one of the 26 modes on offer is 16:9, <!-- count-ok --> and this game keeps nothing
    else -- added 4 of them, largest 1920x1080 @ 120 Hz

`Settings.sav` grew from 2720 to 3152 bytes on that run — the added modes,
written back.

**This is not specific to one screen size.** What triggers it is that nothing
in the list passes the filter, which is true of every display that is not
16:9 — 16:10, 3:2, 4:3, ultrawide, and this one. On a 16:9 display the guard
walks the list, finds a mode, and returns without touching anything.

The one number in it that belongs to this game is the 1.76/1.79 window, read
out of its own `.rdata`. A game filtering for some other aspect would need its
own; the shape of the repair would not change.

Rendering is unaffected either way. The game draws into a swap chain of
whatever size it asks for, exactly as it did before — the mode list is
consulted for the menu and for this filter, not to size the back buffer.

## What is installed

The game's own `OpenColorIO_2_3.dll` becomes `OpenColorIO_2_3_real.dll` and a
proxy takes its name, forwarding every export. Colour management is loaded
early and by the game itself, which is what a carrier has to be. Nothing is
redistributed, no registry key is written, and CrossOver is not modified.

The same proxy carries the other three guards in
[`d3d12-guards.c`](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/runtime/d3d12-guards.c).
Each is inert where its fault is absent. On this game the compute-device one
does fire — the log shows two requests for `1_0_CORE` refused and satisfied at
`11_0` instead — and it is not what fixed the crash; the game went on dying at
the same address while those retries were succeeding. The mode list is the one
that matters here.

The mode-list guard runs for named executables only, this one among them. It is
the single guard in the file that does not make an answer more truthful: it
offers resolutions the display cannot really do. That is the right trade for a
game whose alternative is dying on an empty array, and the wrong one for a game
that would have been fine, so it does not run in front of games that have not
been measured to need it.

## Do not use this on a game with anti-cheat

It patches a running process.
