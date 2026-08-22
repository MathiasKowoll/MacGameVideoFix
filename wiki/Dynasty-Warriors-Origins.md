# DYNASTY WARRIORS: ORIGINS

Koei Tecmo, in-house engine. Cutscenes played with sound and no picture.


| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader` on a **separate D3D11 device**, presented by a **D3D12** renderer |
| Was | Cutscene ran with sound and subtitles, picture black |
| Now | Plays on `crossover-preview-arm64-20260821`. Crashes on 26.3 — that much was run. Why is inferred rather than measured: 26.3 ships no WebM demuxer, read from the plugin sets; see below |
| Needs | A build whose Media Foundation can **open a WebM**, and an injected DLL. Neither alone is enough: Preview demuxes WebM and decodes the VP9 inside it; stable 26.3 decodes the same VP9 and ships no `matroska` plugin, so nothing opens the container. The measured run also had the `.webm` byte-stream handler registered — see below for whether that is needed |

## Who does what

The two halves are not interchangeable, and it is worth being exact about the
line between them.

**Something upstream opens the container and decodes.** On a build with no
WebM demuxer that is winevideo: `libgstvpx` and `libgstmatroska` for VP9 in a
WebM container, a patched `winegstreamer` to advertise it and back the decoder
MFT, a patched `mfplat`, the `.webm` byte-stream handler registered in the
bottle, and the VP9 decoder MFT registered so the game can see one exists. On
Preview it is CrossOver's own `matroska` plugin, with `applemedia` and
VideoToolbox decoding the VP9. Which decoder actually runs on a winevideo build
was not determined here — that build installs `libgstvpx` and registers a
decoder MFT of its own, so it is a ranking question and the plugin comparison
below covers the two stock builds, not that one. Either way the open has to
succeed first: without a demuxer,
`MFCreateSourceReaderFromByteStream` on a `.webm` fails and there is never a
frame to present.

**The DLL presents.** It starts work at the moment Media Foundation hands over
a decoded NV12 sample, and it decodes nothing itself.

**The byte-stream handler, and an honest gap.** The one successful run was made
on Preview with `diagnostics/registry/apply-webm-handler.sh` applied, so that
mapping was present in the measured configuration. It is believed unnecessary
there: this title opens its cutscenes with `MFCreateSourceReaderFromByteStream`,
which resolves by content rather than by extension. No run without the mapping
has been made, so believed is as far as that goes.

The bridge has to live inside the game's process rather than in Wine, and not
for convenience: the call that has to be intercepted is
`ID3D12Device::OpenSharedHandle`, and that D3D12 is D3DMetal's, not Wine's.

## On stable CrossOver: one missing demuxer

Stable 26.3 ships no `matroska` plugin and nothing else it carries opens a WebM,
so this title's 355 `.webm` cutscenes never get as far as a decoder there. Both
builds decode the VP9 inside them identically. The plugin-by-plugin comparison
the claim rests on, and why [Mortal Shell 2](Mortal-Shell-2.md) is not the
control it looks like, are in [Findings](Findings.md), under *The container, not
the codec*.

**The reason is inferred, not instrumented.** The title was launched on stable
and crashed; what was not done is watch it fail. Everything claimed about
stable here is read from the plugin sets, not from a run.

So the honest requirement is narrower than "needs winevideo": it needs something
that can demux WebM.

## Five things were wrong, in sequence

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

## The bridge

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

## Reaching the D3D12 device took one more step

The game ships **NVIDIA Streamline** and asks `sl.interposer.dll` for
`D3D12CreateDevice` by name. It also imports the function from `d3d12.dll` by
ordinal 101 — so an import-table hook there installs correctly, reports
itself hooked, and is never called. Returning a wrapper from `GetProcAddress`
catches it whichever module it is asked of.

That hook has to go in from `DllMain`. Everything else can wait for a worker
thread, because the game does not touch Media Foundation until a cutscene
starts; its D3D12 device is built during startup and already exists by then.

## Cost, and what was done about it

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

One idea that has not been built, if this is not enough: converting on the GPU,
by uploading the two NV12 planes and doing the colour conversion in a compute
shader.

## Caveats

- **Do not use this on a game with anti-cheat.** It patches a running
  process.
- The four abort branches in the player were nopped while the bridge did not
  exist. They are gone: the shipping bridge modifies none of the game's code,
  which `runtime/dwo-video-bridge.c` states and its source bears out. What has
  not been re-measured since they were removed is the crash on skipping a
  cutscene — the player running on half-built state — which was tied to them.
- Steam's *verify integrity of game files* undoes the install.
- DXMT implements `GetSharedHandle` and D3DMetal does not, so a game in this
  position may work under a different backend instead. Untested here — though
  [Persona 5 Strikers](Persona-5-Strikers.md) is the case where that backend
  difference decided the whole fix.

## How it was found, since most of it was wrong turns

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

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [Findings](Findings.md), what the six have in common

### Staging the demuxer was tried, and it was not enough

The obvious move, once Persona 5 Strikers was fixed by staging a codec CrossOver
does not ship, was to stage `libgstmatroska` the same way. It was built, and it
resolved cleanly: every library it needs is present in 26.3 -- `libgstriff`,
`libgsttag`, `libgstpbutils` and the rest -- so only the plugin was missing, and
the staging carried it with its core symlinked to CrossOver's own.

The video still never starts. The bridge arms and stops one line short of where a
working session reaches: `D3D12 device reached, bridge armed`, and never `bridge
ready`, which is the line that reports the frame size. Nothing is ever decoded
for it to present.

Neither engine's `winegstreamer` mentions Matroska, WebM or even ASF anywhere in
its strings, so whatever decides which containers Media Foundation will open is
not reached by putting a plugin on GStreamer's path. That is as far as this was
taken: the change was reverted rather than left in, because it fixed nothing and
added a plugin to the environment of a title that does work there.
