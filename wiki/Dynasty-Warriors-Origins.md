# DYNASTY WARRIORS: ORIGINS

Koei Tecmo, in-house engine. Cutscenes played with sound and no picture.


| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader` on a **separate D3D11 device**, presented by a **D3D12** renderer |
| Was | Cutscene ran with sound and subtitles, picture black |
| Now | Plays |
| Needs | winevideo **and** an injected DLL. Neither alone is enough |

### Who does what

The two halves are not interchangeable, and it is worth being exact about the
line between them.

**winevideo decodes.** `libgstvpx` and `libgstmatroska` for VP9 in a WebM
container, a patched `winegstreamer` to advertise it and back the decoder MFT,
a patched `mfplat`, the `.webm` byte-stream handler registered in the bottle,
and the VP9 decoder MFT registered so the game can see one exists. Without
that, `MFCreateSourceReaderFromByteStream` on a `.webm` fails and there is no
frame to present.

**The DLL presents.** It starts work at the moment Media Foundation hands over
a decoded NV12 sample, and it decodes nothing itself.

The bridge has to live inside the game's process rather than in Wine, and not
for convenience: the call that has to be intercepted is
`ID3D12Device::OpenSharedHandle`, and that D3D12 is D3DMetal's, not Wine's.

### Five things were wrong, in sequence

Each one hid the next, so each looked like the answer when it was reached.

**1. `ID3D11VideoDevice` and `ID3D11VideoContext` return `E_NOINTERFACE`.**
The player refuses to start without both. With `CX_GRAPHICS_BACKEND=d3dmetal`,
CrossOver puts Apple's GPTK ahead of Wine, and GPTK's 136 KB `d3d11.dll` is a
shim over D3DMetal carrying neither — while Wine's own 425 KB one implements
`ID3D11VideoDevice1` with 24 real methods the game never sees. Answering the
two queries with stub objects is enough: of the eighty-odd methods on those
interfaces, the game calls three.

**2. `MF_SOURCE_READER_D3D_MANAGER`.** The game asks the reader to decode into
D3D video textures, which D3DMetal cannot produce. Dropping that attribute
from a *copy* of the request gives software decoding, which winegstreamer
handles without complaint. RGB32 output was tried and refused with
`MF_E_TOPO_CODEC_NOT_FOUND`, so the samples stay NV12.

**3. The video processor.** The player walks the rate conversion caps looking
for one bit — `ProcessorCaps` bit 1, `DEINTERLACE_BOB` — and returns `E_FAIL`
when no entry has it. Claiming it is honest enough: bob deinterlacing a
progressive frame is a copy.

**4. Samples must be D3D-backed.** The executable references `IMFDXGIBuffer`
and `ID3D11Texture2D` and neither `IMF2DBuffer` nor `IMFMediaBuffer`, so step
2 removed the very thing its presentation path needs. The media buffer's
`QueryInterface` is answered with an `IMFDXGIBuffer` of ours that hands over a
texture.

**5. `IDXGIResource::GetSharedHandle` returns `E_NOTIMPL`.** The game shares
its video texture with its D3D12 renderer by handle, and D3DMetal has no way
to make one: the legacy call refuses and `IDXGIResource1`, whose
`CreateSharedHandle` replaces it, is absent entirely.

### The bridge

Step 5 was called blocked upstream here, wrongly, and the correction matters
for anyone reading this as a recipe.

winevideo's D3D9 bridge does **not** invent a handle.
`0008-d3d9-dxmt-video-bridge-handle.patch` creates a texture with
`D3D11_RESOURCE_MISC_SHARED`, calls the real
`IDXGIResource::GetSharedHandle`, and fails with `E_FAIL` if it does not work.
What it substitutes is Wine's *unimplemented* D3D9 sharing with D3D11 sharing
that DXMT *does* implement — `dxmt/src/d3d11/d3d11_texture_device.cpp:288`.
The one call their design rests on is exactly the one that is `E_NOTIMPL`
here, so this is a new invention rather than a port, and the parts with no
reference implementation are the ones most likely to be fragile.

What did transfer is the upload recipe in `0009`: `UpdateSubresource`, then a
`D3D11_QUERY_EVENT` waited on with a deadline rather than a bare `Flush`.
That lesson is why the fence wait below exists.

The shape that works:

1. `GetSharedHandle` hands back a sentinel handle instead of the refusal.
2. The game carries it to `ID3D12Device::OpenSharedHandle` and asks for an
   `ID3D12Resource`.
3. That call returns a BGRA texture created on the game's **own** D3D12
   device, with an upload buffer, a copy queue, an allocator, a list and a
   fence behind it.
4. Each decoded frame is converted from NV12 straight into the upload buffer
   at that buffer's row pitch, copied with `CopyTextureRegion`, and waited on.

A copy queue rather than a direct one, deliberately: resources sit in
`COMMON`, are promoted implicitly for the copy and decay back afterwards, so
no barriers are needed and nothing is assumed about the state the renderer
expects to find. It also keeps the work off the queue the game draws with.

### Reaching the D3D12 device took one more step

The game ships **NVIDIA Streamline** and asks `sl.interposer.dll` for
`D3D12CreateDevice` by name. It also imports the function from `d3d12.dll` by
ordinal 101 — so an import-table hook there installs correctly, reports
itself hooked, and is never called. Returning a wrapper from `GetProcAddress`
catches it whichever module it is asked of.

That hook has to go in from `DllMain`. Everything else can wait for a worker
thread, because the game does not touch Media Foundation until a cutscene
starts; its D3D12 device is built during startup and already exists by then.

### Cost, and what was done about it

Three costs a frame, two of which were for nobody once the bridge worked:

- The D3D11 texture is still handed to the game, because it asks for one and
  drops the frame without it, but nothing reads its contents any more.
  Filling it and copying it twice in the blit moved **42 MB a frame** between
  textures nobody looks at. Removed.
- The conversion wrote into a packed scratch that then had to be re-pitched
  into the upload buffer. It now writes straight into the buffer at its own
  row pitch: another **14 MB pass** removed.
- The conversion does two pixels at a time, since NV12 chroma is subsampled
  2:1 across and both read the same pair — three chroma terms computed once
  rather than six — with saturation by table lookup rather than two branches
  per channel.

Still on the table if it is not enough: converting on the GPU, by uploading
the two NV12 planes and doing the colour conversion in a compute shader.

### Caveats

- **Do not use this on a game with anti-cheat.** It patches a running
  process.
- The four abort branches in the player were nopped while the bridge did not
  exist. Whether they are still needed is being checked; if not, the shipping
  version never modifies the game's code at all, and the crash on skipping a
  cutscene — the player running on half-built state — goes with them.
- Steam's *verify integrity of game files* undoes the install.
- DXMT implements `GetSharedHandle` and D3DMetal does not, so a game in this
  position may simply work under a different backend. Untested here.

### How it was found, since most of it was wrong turns

Eight hypotheses were tested and discarded before the disassembler was used
properly: a missing codec registration, the absent audio track, the container
duration, a missing pixel aspect ratio and interlace mode — setting those
actively broke the negotiation and had to be reverted — NV12 texture
creation, and the shared texture itself.

Three conclusions were also wrong and had to be withdrawn: that a crash at the
menu was D3D's fault rather than a raw pointer's, that the whole thing was
blocked upstream and could not be fixed from here, and that winevideo had
already solved the handle problem.

What worked was reading the code rather than guessing at it.
`find-callsites.py` located the player statically by resolving
`call qword ptr [rip+disp]` against the import table, which found in one
second what four launches of guesswork had not — two `MFStartup` call sites,
only one of them the video player. From there the branches were followed in a
disassembler, and interface queries were answered with stubs that log, so the
game named what it wanted next instead of being guessed at.

Two measurements settled more than any amount of reasoning: averaging the
luma plane, which separated "the frame never arrives" from "the frame is
black", and writing solid magenta into the texture, which proved in one run
that the bridge carried what was put in it.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md)
