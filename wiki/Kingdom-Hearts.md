# KINGDOM HEARTS

Seven titles across two packages, one fault and one fix.  <!-- count-ok: titles inside these two packages, not entries in the table --> The first of them was
the first game here that wanted the decoder's own surface rather than a
picture, and the test that had settled the previous title's fault gave the
opposite answer, which is what made the route clear.

| Package | Fixed | Needs nothing |
| --- | --- | --- |
| HD 2.8 Final Chapter Prologue | Dream Drop Distance, χ Back Cover | 0.2 Birth by Sleep |
| HD 1.5+2.5 ReMIX | FINAL MIX, Re_Chain of Memories, II FINAL MIX, Birth by Sleep FINAL MIX, the Theater | — |

KINGDOM HEARTS III is not here either: it plays unaided. Both exceptions are
the same case and are explained below.

Everything measured below is from Dream Drop Distance, which is where the work
happened. HD 1.5+2.5 ReMIX needed **no new code at all** — its six
video-playing executables import the same five entry points and take the same
route, and the fix reached them by widening one gate from two names to the
family prefix.

| | |
| --- | --- |
| Video | H.264 High, 1920×1080, 30 fps, AAC-LC audio, in MP4 |
| Played by | `IMFSourceReader` from a URL; the frame is fetched as an `IMFDXGIBuffer`, shared into D3D12, and its two planes copied into textures the game converts in its own shader |
| Symptom | Cutscene runs with sound, picture solid green |
| Fix | Software decode, and the luma and chroma planes written straight into the game's own plane textures |
| Backend | **D3DMetal**, D3D11 **and** D3D12 |
| CrossOver | `crossover-preview-arm64-20260821`. Not tried on 26.3 |

## The two that need nothing, and why

KINGDOM HEARTS HD 2.8 Final Chapter Prologue ships two games behind a launcher,
and only Dream Drop Distance needed fixing.

**0.2 Birth by Sleep needs no fix: its cutscenes already play.** That is worth
stating plainly, because the same package failing in one half and working in
the other looks like an oversight otherwise.

The reason it works is the reason it cannot break the same way. Its 49
cutscenes are CriWare `.usm`, and the video inside them is **MPEG-1** —
`mpeg_codec = 1` in every `VIDEO_HDRINFO`, every stream opening with a
`00 00 01 B3` sequence header decoding to 1920×1080, and `ffprobe`
independently reporting `mpeg1video`. CriWare decodes that in its own software
decoder, statically linked into the executable. It never asks Media Foundation
for anything and never asks D3D for a video surface, so there is nothing for
D3DMetal to refuse. The payload is encrypted past the first 0x40 bytes of each
chunk by a scheme that was not determined, and that does not matter either —
the game decrypts it itself.

A game that decodes its own video in software is the case that was never going
to be a problem.

## Six more, for the price of a prefix

HD 1.5+2.5 ReMIX ships four games, a Theater and a launcher, and **all six
executables that play video** import exactly the same five entry points as
Dream Drop Distance: `dinput8`'s `DirectInput8Create`,
`MFCreateSourceReaderFromURL`, `MFCreateDXGIDeviceManager`,
`D3D11CreateDevice`, and `d3d12.dll` **ordinal 101**. Measured from the import
tables before anything was installed.

They take the same route too, and this was run rather than argued. Every
executable below was watched opening a clip, arming the plane pair and putting
a picture on screen:

| | Opens | Plane pair |
| --- | --- | --- |
| FINAL MIX | `dt/KH1Movie/OPN.mp4` | 1920×1080 and 960×540 |
| Re_Chain of Memories | `dt/KHCReSource/BIN/movie/RLP.mp4`, then three more | 1920×1080, then rebuilt at 1280×720 |
| II FINAL MIX | `juefigs/KH2ReSource/zmovie/en/opn.mp4` | 1280×720 and 640×360 |
| Birth by Sleep FINAL MIX | `juefigs/BBSReSource/movie/EN/LOP.mp4` | 1280×720 and 640×360 |
| the launcher | `Mare/MOVIE/ReCoded/dt/hd501.mp4`, then `Mare/MOVIE/Days/dt/DOP.mp4` | 1280×720, then rebuilt at 1920×1080 |

The launcher is not a formality here: it is what plays **both** film
collections, Re:coded and 358/2 Days, the way 2.8's launcher plays Back Cover.
It also crosses a clip-size change between them, in the opposite direction from
Re_Chain of Memories — 1280×720 up to 1920×1080 — which exercises the same
re-arm the other way.

All in software, all getting the shared handle back through
`OpenSharedHandle`, all producing the plane pair the fix writes into.

The only change any of this needed was to the gate that decides which titles
take the plane route. It had been two executable names; eight is not a list any
more, so it is the family prefix instead. That is safe in a way a shape match
is not: this DLL exists only where an installer put it, and the capture also
needs a clip opened through the hooked reader, which is why KINGDOM HEARTS III
matches the prefix and can never arm.

## Back Cover plays too, and the launcher is what plays it

KINGDOM HEARTS χ Back Cover — the film in the package — is not played by either
game executable. **The launcher plays it**, out of its own `Launcher/MOVIE`
folder, through the same source reader and the same plane pair. That is
measured: the bridge engages inside `KINGDOM HEARTS HD 2.8 Launcher.exe` and
the log names the file it opens.

So the installer writes its registry override for both executables. The
launcher happens to load the DLL beside it without being told to on this
machine, but that is Wine's default load order rather than anything arranged,
and a fix that works by default ordering is a fix that stops working when the
ordering changes.

## The files decrypt themselves

The 27 videos ship with their first 256 bytes scrambled — no `ftyp` at offset
4, and every file byte-identical to every other across its first twenty bytes.
That is not something the fix has to deal with, because the game rewrites each
file **in place, in plaintext, the first time it plays it**. Measured: after
playing exactly one cutscene, 1 of 27 files had a readable `ftyp mp42` header
and the other 26 did not.

This is worth writing down because it makes the reader's job ordinary.
`MFCreateSourceReaderFromURL` is handed a path and opens a normal MP4, and the
scrambling never appears anywhere in the video path.

## Why the screen was green

The game asks for `MF_SOURCE_READER_D3D_MANAGER`, so on Windows Media
Foundation hands it an NV12 texture. It then does something none of the earlier
titles do: it opens that texture in D3D12 through a shared handle and copies
**plane 0 and plane 1** into resources of its own — `R8_UNORM` at 1920×1080 for
luma, `R8G8_UNORM` at 960×540 for chroma — and converts them in its own shader.

The bridge published a `B8G8R8A8` texture, because that is what a game wanting
a picture needs. Those plane copies read nothing from it, so both planes stayed
at zero. **Zero luma with zero chroma is green**, and that is the whole
symptom: not a missing frame, but a correct frame in a format the game was not
reading.

## What the magenta test settled, by failing

Painting the bridge's texture solid magenta is the test that split the previous
title's fault in two. Here it came out **green anyway** — the magenta never
appeared. That is a stronger result than a positive one would have been: it
proved the shared texture is not what the game draws, and pointed at the two
plane resources it had created a few lines earlier in the log.

The frames themselves were never in doubt. A sparse sweep of the luma plane
measured `average 16, range 14..20` on the first frame and `average 63, range
28..235` by the hundred-and-twentieth — a picture fading in, decoded correctly
all along.

## Asking D3DMetal for NV12 is fatal

The obvious repair is to publish the shared texture as NV12, which is what the
decoder would have handed over.

`ID3D12Device::CreateCommittedResource` with `DXGI_FORMAT_NV12` **does not
return an error under D3DMetal. It takes the process down.** No exception a
proxy DLL can catch, and nothing in the log after the call — the last line
written is the shared handle being opened. Measured on this title; reported in
[Findings](Findings).

So the frame goes the other way round.

## Writing the planes

The bridge already watches `ID3D12Device::CreateCommittedResource` — that is
how the plane pair became visible. It now captures them:

    R8_UNORM   at the clip's size        -> luma
    R8G8_UNORM at half the clip's size   -> chroma

and every decoded frame is copied straight into them from a copy queue, with no
colour conversion at all. The game's shader does that, as it always did.

    ReadSample        -> NV12 in system memory
                      -> memcpy of the luma rows   -> the game's R8 texture
                      -> memcpy of the chroma rows -> the game's R8G8 texture

Nothing engages unless a game creates that exact pair at the clip's dimensions,
so the four titles that take the shared-texture route are untouched.

Both plane resources are referenced while held, and replaced when the game
creates a new pair. That path is not theoretical and was not left to argument:
Re_Chain of Memories plays a 1920×1080 clip and then a 1280×720 one in the same
session, and the log shows the bridge rebuilding around it —

    bridge ready: 1280x720, upload pitch 5120
    plane: luma 1280x720
    plane: chroma 640x360
    plane: writing both planes directly, luma pitch 1280 chroma pitch 1280

— with both clips visible on screen. Writing into the first pair after the
second was created would have been silent, which is the reason to hold a
reference and replace on sight rather than capture once.

## The carrier, which is not the game's

Only `steam_api64.dll` sits beside the executable, and nothing here rides on
Steam's API or re-exports a Steamworks entry point. So the bridge rides on
`dinput8.dll`, which the game imports, which has five exports and nothing to do
with rendering. The original is CrossOver's own and the installer copies it out
of the bottle; nothing is redistributed, but it is a copy, so refresh it after a
CrossOver upgrade.

Like NieR Replicant, this one writes a registry key so Wine prefers the copy
beside the game over its own `dinput8`. It is scoped to this executable alone.

## The launcher's crash dialog, and why it is in this DLL

Both packages' launchers start the game correctly and then fault while tearing
themselves down. Wine's AeDebug runs `winedbg`, and the user gets a **Program
Error** dialog for a program that did its job. Measured from the dump:

    Unhandled exception: page fault on write access to 0x0000000000000000
    ntdll+0x38d55: movq $0, (%rax)     rax = 0
    backtrace: five frames, every one of them ntdll

All five are in ntdll's shutdown chain, and the dump's own process list already
shows the game running. The launcher hands off and then dies clearing up.

**Why not the registry.** `AeDebug` lives in HKLM and is bottle-wide. Turning it
off would silence crash reporting for every other game in that bottle to spare
one dialog. This DLL is already loaded into that exact process — the dump shows
a `wine_dinput_worker` thread in it — so it is the narrowest place to act.

**Why a vectored handler and not a top-level filter.** A top-level filter was
tried first and never ran: the log said `filter armed` and never said it fired,
while the dialog still appeared — which settles it, because that filter called
`TerminateProcess`. Disassembly of this engine's
`kernelbase!UnhandledExceptionFilter` explains it: the filter is consulted at
`0x174066820` and the whole AeDebug path only begins at `0x17406682a`, so a
non-zero return **would** have stopped it. The function is never reached from
that depth of shutdown. That also rules out the other two gates in the same
function — the hardcoded exe-name hack and the `GetErrorMode` check — and is why
`SetErrorMode(SEM_NOGPFAULTERRORBOX)` is deliberately **not** added as a belt: it
would only fire where the handler declines on purpose.

**The gate is tight, and one condition is load-bearing.** One of the package's
executables, an access violation, a **write**, to address zero, **and the fault
coming from inside ntdll**. That last one is not pedantry: these launchers are
.NET run under Mono, and Mono implements `NullReferenceException` by faulting on
a write to zero and catching it. Those are normal and frequent in this process.
The module the fault came from is the only thing separating them, and without it
the handler would kill the launcher while it was working.

Measured 2026-08-29 on 1.5+2.5: armed seven times, fired six, every firing the
launcher on its way out — with the video fix decoding in the same session.

## KINGDOM HEARTS HD 2.8: three entries, and only one of them ours

The package holds **three**, not two: Dream Drop Distance, 0.2 Birth by Sleep,
and the Back Cover film. They do not share a fault and only one needs this
project.

**Dream Drop Distance** needs the bridge and has it. Measured on a 26.3-based
engine: four runs of four reached the menu, two of them to `ReadSample [#300]`
with a luma range of 28..235 — essentially the full dynamic range. And the
bridge is necessary, isolated with a single variable: same engine, same toolkit,
patch removed, and the game crashes.

**0.2 Birth by Sleep needs nothing.** No installer here has ever covered it —
its executable is in `KINGDOM HEARTS 0.2 Birth by Sleep/Binaries/Win64`, and
`install-kh-bridge.sh` looks only at the top of the package folder. Launched
directly it runs and plays its video, four times out of four, with nothing of
ours installed.

### What actually fails for 0.2, and what it is not

Launched **by a launcher**, its process is never created at all. The selector
sits at 0% CPU, `WaitTitleProject.exe` draws the loading heart at 16% and waits
in `mach_msg` for an event that does not come, and no `CreateProcessInternalW`
for the game ever appears in a `WINEDEBUG=+process` trace. Launched by hand from
the same engine, the same bottle and the same executable, the trace shows the
process created and the game runs.

So something about **how** it is launched decides it, and what that something is
remains unknown. Eliminated by measurement, one variable at a time: the game
itself, this project's patch (absent), the three `GST_*` variables (which come
from the engine's own wine, not from a launcher), MetalFX, the graphics backend,
the toolkit generation, which executable is launched, the process tree, Wine
tracing, the Metal HUD, and — tested last, at the launcher author's own
suggestion — invoking the executable by native path from a foreign working
directory instead of through `--cx-app`. That was the strongest remaining
hypothesis, and the game started anyway.

**This page deliberately does not name a culprit.** "A launcher's launch fails"
is measured; "the launcher does X wrong" is not, and every specific X proposed
so far has been tested and survived. Naming one would be inventing a cause to
finish a sentence.

**A warning about the instrument, paid for twice.** With a probe DLL beside it,
0.2 launched; without, it did not — which looked like a finding and was noise
from whatever the probe changed. And the probe's environment dump lists a fixed
set of variable names rather than the whole environment, so several hours were
spent testing against a list that was never complete. Both are the same mistake:
believing an instrument about a question it was not built to answer.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on Preview and D3DMetal only. 26.3 has not been tried.

0.2 Birth by Sleep and KINGDOM HEARTS III are untouched by this and need to be.
Both decode their own video in software.

Seven of the eight executables have been run and watched, covering every <!-- count-ok -->
distinct engine in both packages: Dream Drop Distance, 2.8's launcher playing
Back Cover, FINAL MIX, Re_Chain of Memories, II FINAL MIX, Birth by Sleep
FINAL MIX and 1.5+2.5's launcher.

`KINGDOM HEARTS Theater.exe` is the one not observed, and it now looks like it
never will be: **both** film collections were reached through the launcher, so
nothing in normal play appears to route through that executable at all. It
carries the same import signature and the installer covers it regardless, which
costs a registry key and removes the question.

The launcher crashes on exit on this machine, inside `libobjc` by way of
`winemac`, in a thread belonging to the launcher rather than the game. It is
unrelated to the bridge and the game keeps running.

## What it cost

Eight runs. Three hypotheses died by measurement: that the game never asks for
a D3D texture (it does, and refusing turns green into black), that the shared
texture's size was the mismatch (matching it changed nothing), and that
publishing NV12 would work (it ends the process).
