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

### DYNASTY WARRIORS: ORIGINS — Koei Tecmo, in-house engine

| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader` on a **separate D3D11 device**, presented by a **D3D12** renderer |
| Was | Cutscene ran with sound and subtitles, picture black |
| Fix | An injected DLL that supplies the video interfaces, decodes in software, and bridges the frame into D3D12 itself |

Five things were wrong, in sequence, and each had to be cleared before the
next became visible.

**1. `ID3D11VideoDevice` and `ID3D11VideoContext` return `E_NOINTERFACE`.**
The player refuses to start without both. With `CX_GRAPHICS_BACKEND=d3dmetal`,
CrossOver puts Apple's GPTK ahead of Wine, and GPTK's 136 KB `d3d11.dll` is a
shim over D3DMetal carrying neither — while Wine's own 425 KB one implements
`ID3D11VideoDevice1` with 24 real methods the game never sees. Answering the
two queries with stub objects is enough; of the eighty-odd methods on those
interfaces the game calls almost none.

**2. `MF_SOURCE_READER_D3D_MANAGER`.** The game asks the reader to decode into
D3D video textures, which D3DMetal cannot produce. Dropping that attribute
gives software decoding, which winegstreamer handles without complaint.

**3. The video processor.** The player walks the rate conversion caps looking
for one bit — `ProcessorCaps` bit 1, `DEINTERLACE_BOB` — and returns `E_FAIL`
when no entry has it. Claiming it is honest: bob deinterlacing a progressive
frame is a copy.

**4. Samples must be D3D-backed.** The executable references `IMFDXGIBuffer`
and `ID3D11Texture2D` and neither `IMF2DBuffer` nor `IMFMediaBuffer`, so step
2 removed the very thing its presentation path needs. Each decoded frame is
converted from NV12 to BGRA and uploaded into a texture, and the buffer's
`QueryInterface` is answered with an `IMFDXGIBuffer` that hands it over.

**5. `IDXGIResource::GetSharedHandle` returns `E_NOTIMPL`.** The game shares
its video texture with its D3D12 renderer by handle, and D3DMetal has no way
to make one: the legacy call refuses and `IDXGIResource1`, whose
`CreateSharedHandle` replaces it, is absent.

That last one looked like the end of the road, and it was not. winevideo had
already met the same wall for D3D9 under DXMT and gone around it — see
`0008-d3d9-dxmt-video-bridge-handle.patch` — by manufacturing the handle
itself and owning the far side. The same shape works here:

- `GetSharedHandle` hands back a handle of ours instead of the refusal.
- The game carries it to `ID3D12Device::OpenSharedHandle` and asks for an
  `ID3D12Resource`.
- That call returns a BGRA texture created on the game's own D3D12 device.
- Each converted frame is re-pitched into an upload buffer, copied with
  `CopyTextureRegion` on a copy queue, and waited on with a fence.

Reaching the D3D12 device took one more step than expected: the game ships
NVIDIA Streamline and asks `sl.interposer.dll` for `D3D12CreateDevice` by
name, so an import-table hook on `d3d12.dll` — where it also imports the
function, by ordinal 101 — is installed correctly and never called. Returning
a wrapper from `GetProcAddress` catches it whichever module it is asked of.

A copy queue rather than a direct one: resources sit in `COMMON`, are promoted
implicitly for the copy and decay back afterwards, so no barriers are needed
and nothing is assumed about the state the renderer expects. The fence wait is
not caution — winevideo needed the same, and letting the frame go early shows
the previous one or nothing.

**Caveats.** The NV12 to BGRA conversion is scalar over 3.7 million pixels a
frame and the fence wait is synchronous, so frame pacing suffers; both are
addressable and neither is load-bearing. The player's four abort branches are
still patched out, which was necessary before the bridge existed and probably
is not now. And this is memory patching: never use it on a game with
anti-cheat.

**Method, since most of it was wrong turns.** Eight hypotheses were tested and
discarded before the disassembler was used properly: a missing codec
registration, the absent audio track, the container duration, a missing pixel
aspect ratio and interlace mode — setting those actively broke the negotiation
and had to be reverted — NV12 texture creation, and the shared texture itself.
Two conclusions were also wrong and had to be withdrawn, the second of them
"this is blocked upstream and cannot be fixed from here". What worked was
reading the code rather than guessing at it: `find-callsites.py` locating the
player statically by resolving `call qword ptr [rip+disp]` against the import
table, following its branches in a disassembler, and answering interface
queries with stubs that log — so the game said what it wanted next instead of
being guessed at. Writing solid magenta into the texture settled in one run
what several rounds of reasoning had not.

## Adding a row

This page covers titles we have deliberately taken on, not everything that
happens to be installed on someone's machine. A game gets a row when there is a
reason to work on it.

To add one: run the survey on that game's folder, and if it misbehaves, the
probe. Both are in `diagnostics/`, and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md). Paste what they print —
measurements are the point of this page, and a row without one is worse than no
row.
