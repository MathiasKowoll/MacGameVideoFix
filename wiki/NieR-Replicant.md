# NieR Replicant ver.1.22474487139

The tenth title, and the one that took the most measurement: three hypotheses
died before the working path was found, and the test that split the problem in
two was a magenta screen.

| | |
| --- | --- |
| Video | **WMV2** (Windows Media Video 8) 1920×1080 25 fps, audio **WMA v2** (`0x0161`) 48 kHz, in ASF |
| Played by | `IMFSourceReader` on Media Foundation, presented through the D3D11 video processor |
| Symptom | Crashes when the first video starts |
| Fix | Software decode, and the frame converted and written into the game's own target |
| Backend | **D3DMetal**, D3D11 |
| Needs | `libgstlibav` staged, and the `dinput8` override below — without the override nothing else matters |
| CrossOver | 26.3 and `crossover-preview-arm64-20260821` |

## What the source codec is

The video lives inside the game's own `.arc` archives, and an early scan for a
container signature found nothing — which is why this page said for a while that
the codec could not be established. The scan was looking one header too early.
Each `.arc` opens with a 16-byte `MARC` header, and the ASF object GUID
`3026B275-8E66-CF11-A6D9-00AA0062CE6C` begins immediately after it:

    00000000  4d 41 52 43 01 00 00 00  18 00 00 00 00 00 00 00  |MARC............|
    00000010  30 26 b2 75 8e 66 cf 11  a6 d9 00 aa 00 62 ce 6c  |0&.u.f.......b.l|

Parsing the ASF stream properties from there names both streams outright:

    VIDEO  1920x1080  biCompression = WMV2      Windows Media Video 8
    AUDIO  formatTag = 0x0161  2ch  48000 Hz    Windows Media Audio v2

That is more than a label. CrossOver ships `libgstasf`, so the container opens on
its own; it ships no decoder for either stream. Both come from the staged
`libgstlibav`, whose FFmpeg carries `wmv2` and `wmav2`. NieR belongs with the
titles that need the staged decoder, and was filed with the ones that need
nothing.

The reader still reports its stream 0 native type as `I420` with
`MF_MT_COMPRESSED` at 0, and that reading was correct: by the time the source
reader is visible, the decode has already happened. It is what made the stored
format look unknowable from inside the process. It was knowable from outside it.

## Two faults, and the second was made by the first

**The crash.** The game creates its source reader with
`MF_SOURCE_READER_D3D_MANAGER` set and
`MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS` at 1 — decode into D3D video
textures. Under D3DMetal there is nothing to decode into, and the process dies
inside `SetCurrentMediaType` asking for NV12 output. Handing the reader a copy
of the attributes without those two makes it decode in software, and the crash
is gone: over three hundred samples arrive, none of them empty.

**Then the screen stayed black.** With frames decoding correctly, nothing
appeared. The reason is a consequence of the repair above: the samples now
arrive in system memory rather than as DXGI buffers, and the game's own upload
step — the one that would put the frame into the surface it hands the video
processor — never runs. That surface was read directly and measured **flat,
average 0, range 0..0**, three times across a scene.

## What the magenta test settled

Two explanations of a black screen look identical from outside: a frame that
never arrives, and a frame written somewhere that is never displayed. Painting
the game's output texture solid magenta separates them in one run, and it came
out magenta — so the write path was right and the fault was upstream of it.

That turned the remaining work into something concrete: take the sample we
already receive, convert it ourselves, and write it where the magenta went.

    ReadSample          -> NV12 in system memory
                        -> nv12_to_bgra
                        -> UpdateSubresource into the game's target
    VideoProcessorBlt   -> uploads the current frame, every blit

The game's own surface is bypassed entirely. Its `VideoProcessorBlt` — which
under D3DMetal reaches a stub that returned `S_OK` having done nothing — now
carries the frame.

## Cost, measured

`convert 4.51 ms` per decoded frame, `upload 0.21 ms` per blit, against a 40 ms
budget at 25 fps. The conversion is scalar C over two million pixels and is the
only part worth optimising if a more demanding clip ever needs it; the upload
is not.

One optimisation was tried and withdrawn by its own measurement: uploading only
when the frame changed. It saved 0.21 ms, and left the game's target holding
whatever was drawn between video frames. The conversion runs once per decoded
frame; the upload runs every blit because it is cheap enough not to matter.

## The carrier, which is not the game's

NieR ships exactly one DLL of its own, `steam_api64.dll`. Nothing here rides on
Steam's API or re-exports a Steamworks entry point, so the bridge rides on
`dinput8.dll` instead — five exports, imported by the game, nothing to do with
rendering. The original is CrossOver's own and the installer copies it out of
the bottle; nothing is redistributed, but it is a copy, so it should be
refreshed after a CrossOver upgrade.

This is also the only fix here that writes a registry key. Wine implements
`dinput8` and prefers its own build, so a DLL beside the game would never load.
The override says otherwise and is scoped to this executable alone.

## Why it looked like a 26.3 failure, and was not

This title was recorded as Preview-only, with the note that 26.3 "crashes before
the fix loads". Half of that was right: the fix was not loading. The half that
was wrong is which side the fault was on.

The bridge needs two things, and the installer checks one. The proxy and the
copied original sit beside the game; the registry override below is what makes
Wine prefer them over its own `dinput8`. In the bottles these runs used, the
override was **absent** — and `--status` answered `installed` anyway, because it
looks at the two files and never at the registry. So every 26.3 attempt measured
the game running with no fix at all, and dying at the attract video is exactly
what an unfixed build should do.

The override had been there once: the magenta test and the luma readings on this
page could only have come from the bridge executing. It went missing at some
point between then and now, and nothing reported that it had.

With the key written, on stock 26.3 and D3DMetal 4.0b2, the bridge loads and
produces the same log every other title does:

    SetCurrentMediaType asked 'NV12' -> 0x00000000
    ReadSample [#300] -> 0x00000000, sample delivered (0 empty so far)
    frame [#120] luma: average 16, range 14..238 over 32400 samples
    200 frames in 200 blits: convert 2.06 ms each, upload 0.21 ms each

A luma range of 14..238 is a picture. The title is supported on 26.3.

The finding worth carrying past this game: a fix can report itself applied while
the one thing that makes it run is missing, and every measurement taken after
that is a measurement of the wrong subject. Kingdom Hearts rides the same
carrier and depends on the same key.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

The frame is copied at the target's own size with no scaling: both surfaces are
1920×1080 here. A clip presented into a differently sized target would need
`CopySubresourceRegion` and a real scale, and that has not been measured.

## What it cost

Nine runs. Three hypotheses were killed by measurement rather than by argument:
that the game asks for an NV12 texture D3DMetal cannot create (it never asks
for one), that the D3D manager was what kept the picture away (removing it
fixed the crash and not the picture), and that the surface the game hands the
processor holds the frame (it is empty).
