# KINGDOM HEARTS

Seven titles across two packages, one fault and one fix. The first of them was
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

They take the same route too, confirmed by running three of them — one per
engine in the package:

| | Opens | Plane pair |
| --- | --- | --- |
| FINAL MIX | `dt/KH1Movie/OPN.mp4` | 1920×1080 and 960×540 |
| Re_Chain of Memories | `dt/KHCReSource/BIN/movie/RLP.mp4`, then a 1280×720 clip | both sizes, rebuilt between them |
| II FINAL MIX | `juefigs/KH2ReSource/zmovie/en/opn.mp4` | 1280×720 and 640×360 |

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

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on Preview and D3DMetal only. 26.3 has not been tried.

0.2 Birth by Sleep and KINGDOM HEARTS III are untouched by this and need to be.
Both decode their own video in software.

Five of the eight executables have been run and watched: Dream Drop Distance,
2.8's launcher playing Back Cover, FINAL MIX, Re_Chain of Memories and II FINAL
MIX — which between them are every distinct engine in the two packages. The
remaining three — Birth by Sleep FINAL MIX, the Theater and 1.5+2.5's launcher
— are covered by the same installer and carry the same measured import
signature, which is a strong argument and not a run.

The launcher crashes on exit on this machine, inside `libobjc` by way of
`winemac`, in a thread belonging to the launcher rather than the game. It is
unrelated to the bridge and the game keeps running.

## What it cost

Eight runs. Three hypotheses died by measurement: that the game never asks for
a D3D texture (it does, and refusing turns green into black), that the shared
texture's size was the mismatch (matching it changed nothing), and that
publishing NV12 would work (it ends the process).
