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

## Diagnosed — blocked upstream

### DYNASTY WARRIORS: ORIGINS — Koei Tecmo, in-house engine

| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader` on a **separate D3D11 device**, presented by a **D3D12** renderer |
| Symptom | Cutscene runs with sound and subtitles, picture stays black |
| Cause | `IDXGIResource::GetSharedHandle` returns **`E_NOTIMPL`** under D3DMetal |

**The last link.** The game decodes into a D3D11 texture created with
`D3D11_RESOURCE_MISC_SHARED`, then asks it for `IDXGIResource` and for a
shared handle to give to its D3D12 renderer. That call is not implemented, so
D3D12 never opens the texture, and the video quad samples one nothing ever
wrote — flat grey, then black.

Everything on the D3D11 side is healthy, which is what made this hard to see.
Frames decode, convert and land in the texture correctly; the luma plane
averages 16 with a range to 132, a real if dark picture. Writing solid magenta
into both of the game's textures changes nothing on screen, because neither is
ever read.

**There is no second route.** D3DMetal carries the receiving half of resource
sharing — `ID3D11Device1::OpenSharedResource1` and
`ID3D12Device::OpenSharedHandle` are both present in the binary — but nothing
that produces a handle. `GetSharedHandle` refuses and `IDXGIResource1`, whose
`CreateSharedHandle` is the modern replacement, is absent entirely. A handle
cannot be manufactured from inside the process: faking success only moves the
failure to `OpenSharedHandle` on the other side.

So this is a gap in Apple's D3DMetal, and the fix belongs there. Any Windows
game that decodes video on a D3D11 device and presents it with a D3D12
renderer will hit it.

**Four blockers were real and were cleared before reaching it.** Each one
looked like the answer at the time:

1. **`ID3D11VideoDevice` / `ID3D11VideoContext` return `E_NOINTERFACE`.** The
   player refuses to start without both. With `CX_GRAPHICS_BACKEND=d3dmetal`,
   CrossOver puts Apple's GPTK ahead of Wine, and GPTK's 136 KB `d3d11.dll`
   is a shim carrying neither — while Wine's own 425 KB one implements
   `ID3D11VideoDevice1` with 24 real methods that the game never sees.
2. **`MF_SOURCE_READER_D3D_MANAGER`.** The game asks the reader to decode into
   D3D video textures, which D3DMetal cannot produce. Dropping it gives
   software decoding, which winegstreamer handles without complaint.
3. **The video processor.** The player walks the rate conversion caps for one
   specific bit — `ProcessorCaps` bit 1, `DEINTERLACE_BOB` — and returns
   `E_FAIL` when no entry has it.
4. **Samples must be D3D-backed.** The executable references `IMFDXGIBuffer`
   and `ID3D11Texture2D` and neither `IMF2DBuffer` nor `IMFMediaBuffer`, so
   dropping the device manager in step 2 removed the very thing its
   presentation path needs. Answering the `IMFDXGIBuffer` query with a texture
   filled here satisfies both at once.

With all four cleared the cutscene runs in full — audio, subtitles, timeline,
decoded frames in a texture — and stops at a handle that does not exist.

**Method, since most of it was wrong turns.** Eight hypotheses were tested and
discarded: a missing codec registration, the absent audio track, the container
duration, a missing pixel aspect ratio and interlace mode (setting those
actively broke the negotiation and had to be reverted), NV12 texture creation,
and the shared texture itself. What worked was reading the code —
`find-callsites.py` locating the player statically by resolving
`call qword ptr [rip+disp]` against the import table, then following its
branches — and answering interface queries with stubs that log, so the game
said what it wanted next instead of being guessed at.

## Adding a row

This page covers titles we have deliberately taken on, not everything that
happens to be installed on someone's machine. A game gets a row when there is a
reason to work on it.

To add one: run the survey on that game's folder, and if it misbehaves, the
probe. Both are in `diagnostics/`, and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md). Paste what they print —
measurements are the point of this page, and a row without one is worse than no
row.
