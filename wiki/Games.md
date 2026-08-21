Titles we have taken on, and what was measured on each. Every row comes from an
installed copy rather than from memory; where a claim comes from a static scan
rather than from playing the game, it says so.

Tested on an M4 Max, macOS 27, CrossOver 26.2 patched with
[winevideo](https://github.com/Jfishin/winevideo), GPTK 4.0b2.

## Fixed

### Mortal Shell 2 — Unreal Engine 5.6.1

| | |
| --- | --- |
| Cutscenes | 61 × VP9 in `.mp4`, inside `pakchunk0-Windows.pak` and loose |
| Played by | Electra, decoding VP9 with its own libvpx |
| Symptom | `EXCEPTION_ACCESS_VIOLATION` reading `0x0` in `FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer` |
| Fix | Runtime patch — **4 patch sites**, all four confirmed at runtime |

Both modes work. The runtime patch leaves the original VP9 files untouched;
re-encoding is the fallback.

## Diagnosed, not yet fixed

### DYNASTY WARRIORS: ORIGINS — Koei Tecmo, in-house engine

| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader`, on a **separate D3D11 device** created only for video |
| Symptom | Cutscene plays with sound, picture stays black. No crash |

**Where it stands.** The player now runs: it opens the file, builds a reader,
negotiates NV12 at 2560x1440, reads samples, seeks, and the audio plays. What
it never does is put a frame on screen.

**The chain, in the order it was found.** Each of these was a real blocker;
none of them was the last one.

1. **`ID3D11VideoDevice` and `ID3D11VideoContext` return `E_NOINTERFACE`.**
   The player queries the D3D11 device for both and gives up if either fails.
   With `CX_GRAPHICS_BACKEND=d3dmetal`, CrossOver puts Apple's GPTK ahead of
   Wine, and GPTK's 136 KB `d3d11.dll` is a shim over D3DMetal carrying
   neither. Wine's own 425 KB `d3d11.dll` implements `ID3D11VideoDevice1` with
   24 real methods, but the game never sees it.

2. **`MF_SOURCE_READER_D3D_MANAGER`.** The game asks the source reader to
   decode into D3D video textures, which nothing under D3DMetal can produce.
   Dropping that attribute makes it decode in software, and winegstreamer's
   VP9 support handles it without complaint.

3. **The video processor.** Converting the decoded NV12 into the BGRA texture
   the game draws is `ID3D11VideoProcessor`'s job. The player walks the rate
   conversion caps looking for one specific bit — `ProcessorCaps` bit 1,
   `DEINTERLACE_BOB` — and returns `E_FAIL` when no entry has it.

4. **And then the tension that has no easy way out.** The executable
   references `IMFDXGIBuffer`, `ID3D11Texture2D` and `IDXGIResource`, but
   neither `IMF2DBuffer` nor `IMFMediaBuffer`. It only knows how to consume
   samples backed by D3D textures: query the buffer for `IMFDXGIBuffer`, take
   the texture, wrap it in a `VideoProcessorInputView`, blit.

   So the attribute dropped in step 2 to make decoding work is exactly the one
   its presentation path depends on. Give it back and decoding fails; leave it
   off and there is no texture to present.

**What a fix would take.** Software decoding that still hands back D3D-backed
samples: intercept `ReadSample`, upload each frame into a texture, and present
it through an `IMFDXGIBuffer` the game can consume. That is what Windows does
when the decoder is software and a device manager is set, and it is what
neither D3DMetal nor Wine's source reader does here.

The alternative is upstream: D3DMetal implementing `ID3D11VideoDevice`, at
which point steps 1 through 4 all disappear at once.

**How it was found.** Worth recording, because most of it was wrong turns.
Eight hypotheses were tested and discarded before the disassembler was used
properly: a missing codec registration, the absent audio track, the container
duration, a missing pixel aspect ratio and interlace mode (setting those
actively broke the negotiation), NV12 texture creation, and the shared
texture. What worked was reading the code — `find-callsites.py` locating the
player statically, then following the branches — and answering interface
queries with logging stubs so the game itself said what it wanted next.

## Adding a row

This page covers titles we have deliberately taken on, not everything that
happens to be installed on someone's machine. A game gets a row when there is a
reason to work on it.

To add one: run the survey on that game's folder, and if it misbehaves, the
probe. Both are in `diagnostics/`, and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md). Paste what they print —
measurements are the point of this page, and a row without one is worse than no
row.
