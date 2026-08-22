# NieR Replicant ver.1.22474487139

The tenth title, and the one that took the most measurement: three hypotheses
died before the working path was found, and the test that split the problem in
two was a magenta screen.

| | |
| --- | --- |
| Video | Delivered as I420, 1920×1080, 25 fps. The source codec is not known — see below |
| Played by | `IMFSourceReader` on Media Foundation, presented through the D3D11 video processor |
| Symptom | Crashes when the first video starts |
| Fix | Software decode, and the frame converted and written into the game's own target |
| Backend | **D3DMetal**, D3D11 |
| CrossOver | `crossover-preview-arm64-20260821`. Not tried on 26.3 |

## What the source codec is, which was never established

The video lives inside the game's own `.arc` archives — there are no loose
files, and a signature scan over three gigabytes of each turned up no ASF, MP4,
Matroska, USM or Bink header, so they are compressed or encrypted.

The reader reports its stream 0 **native** type as `I420` with
`MF_MT_COMPRESSED` at 0, which means whatever decoded it did so upstream of the
source reader and the compressed format never became visible. So this page says
what the frames arrive as, not what they are stored as. Nothing in the fix
depends on the difference, but it is a gap rather than a finding.

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

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on Preview and D3DMetal only. 26.3 has not been tried.

The frame is copied at the target's own size with no scaling: both surfaces are
1920×1080 here. A clip presented into a differently sized target would need
`CopySubresourceRegion` and a real scale, and that has not been measured.

## What it cost

Nine runs. Three hypotheses were killed by measurement rather than by argument:
that the game asks for an NV12 texture D3DMetal cannot create (it never asks
for one), that the D3D manager was what kept the picture away (removing it
fixed the crash and not the picture), and that the surface the game hands the
processor holds the frame (it is empty).
