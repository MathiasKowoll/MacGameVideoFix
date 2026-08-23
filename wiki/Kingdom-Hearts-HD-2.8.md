# KINGDOM HEARTS Dream Drop Distance

The twelfth title, and the first whose game wanted the decoder's own surface
rather than a picture. The test that had settled the previous title's fault
gave the opposite answer here, and that was what made the route clear.

| | |
| --- | --- |
| Video | H.264 High, 1920×1080, 30 fps, AAC-LC audio, in MP4 |
| Played by | `IMFSourceReader` from a URL; the frame is fetched as an `IMFDXGIBuffer`, shared into D3D12, and its two planes copied into textures the game converts in its own shader |
| Symptom | Cutscene runs with sound, picture solid green |
| Fix | Software decode, and the luma and chroma planes written straight into the game's own plane textures |
| Backend | **D3DMetal**, D3D11 **and** D3D12 |
| CrossOver | `crossover-preview-arm64-20260821`. Not tried on 26.3 |

## One package, two games, one of them fixed

KINGDOM HEARTS HD 2.8 Final Chapter Prologue ships two executables behind a
launcher. Only **Dream Drop Distance** is fixed here.

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
creates a new pair — a second cutscene at a different size gets new textures,
and writing into the old ones would be silent.

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

Only Dream Drop Distance. Selecting the package folder finds it by name; 0.2
Birth by Sleep is a different executable and a different video path, and this
does nothing for it.

The launcher crashes on exit on this machine, inside `libobjc` by way of
`winemac`, in a thread belonging to the launcher rather than the game. It is
unrelated to the bridge and the game keeps running.

## What it cost

Eight runs. Three hypotheses died by measurement: that the game never asks for
a D3D texture (it does, and refusing turns green into black), that the shared
texture's size was the mismatch (matching it changed nothing), and that
publishing NV12 would work (it ends the process).
