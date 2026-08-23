# Findings

What the nine titles have in common: the results that are not about any one of
them, the mechanism the fixes share, and what was tried and did not work.

**This page is addressed to whoever maintains Wine, D3DMetal, DXMT or
CrossOver's packaging.** Every result is stated as a defect or a gap in the
translation stack rather than as a property of a game, and each one carries what
was observed, how to reproduce it, and what would close it. Start at
[Defects, by the component that would fix them](#defects-by-the-component-that-would-fix-them);
the sections after it are the mechanism and the background.

What this project got wrong on the way to these results is deliberately not
here. It is in
[docs/what-we-got-wrong.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/what-we-got-wrong.md),
because it is of no use to anyone implementing a fix.

The per-title pages carry the findings for a title. This page carries what is common to all of them, and it is where a claim
lives when it belongs to an engine, to CrossOver, or to the toolkit rather than
to a game. Where a title page already tells a story in full, this page links to
it rather than telling it again.

To get a game playing, start at the
[README](https://github.com/MathiasKowoll/MacGameVideoFix#readme).

Everything here was measured on an M4 Max, macOS 27, GPTK 4.0b2, against
CrossOver 26.3 and `crossover-preview-arm64-20260821`. "Preview" on this page
means that build and no other.

## Defects, by the component that would fix them

Everything below is a gap or a defect in the translation stack, not in a game.
Each one carries what was observed, how to reproduce it, and what would close
it. The games are only where it surfaced.

One repair in this project does not belong in this list, because the fault it
works around is the game's own. It is written up separately in
[the fault that was not in the stack](#the-fault-that-was-not-in-the-stack),
and the distinction is worth keeping: a defect reported to the wrong project
does not get fixed.

Four words are used precisely: **measured** (observed directly here),
**controlled** (observed with and without the thing under test), **inferred**
(reasoned from something measured, not itself observed), and **not measured**
(recorded so it is not mistaken for a result).

### Wine

**`d3d9` reports success and returns a null share handle.**

- *Observed.* `IDirect3DDevice9::CreateRenderTarget` with a share handle
  requested returns `S_OK` and writes zero into the handle. Three titles, same
  result: `CreateRenderTarget(1920x1080, SHARED requested) -> S_OK, handle 0`.
- *Consequence.* A caller that believes the return value carries the null
  forward. Nioh reaches `mov rdx, [rdx+0x10]` with `rdx` at zero on its video
  thread and the process dies. No crash report names the cause.
- *Reproduce.* Nioh, Nioh 2 or Persona 5 Strikers under DXMT; patch slot 28 of
  `IDirect3DDevice9` and log the handle written.
- *What would fix it.* Either implement D3D9 resource sharing, or fail the
  creation when a share handle is asked for and cannot be produced. The second
  is much smaller and removes the crash: a caller told *no* can fall back, and
  every one of these titles has a non-shared path. Success-with-null is the
  harmful answer, not the missing feature.
- *Standing.* **Measured**, three titles.

**No D3D11 resource sharing in `d3d11`.**

- *Observed.* `GetSharedHandle` appears 17 times in DXMT's `d3d11.dll` and not
  once in Wine's, by symbol scan of both.
- *Consequence.* The bridges here work only on DXMT. On Wine's own D3D11 there
  is nothing to build a shared surface on.
- *Standing.* **Measured.**

**No VP9 decoder MFT is registered.**

- *Observed.* `MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, 0x3F, {Video, VP90})`
  returns zero entries, and the registry contains no mention of VP9 anywhere.
- *Consequence.* A title that asks before decoding never proceeds. Ninja Gaiden
  4 stops here and calls `exit(-1)`, leaving a black screen and no report.
- *Note.* The capability exists — CrossOver Preview decodes VP9 profile 0 and 2
  through `vp9parse → vtdec_hw`. What is missing is the declaration, not the
  decoder.
- *What would fix it.* Registering a VP9 decoder MFT. Proton carries one and
  winevideo ports it under Microsoft's public VP9 Video Extensions CLSID.
- *Standing.* **Measured.**

**No `.webm` byte-stream handler is registered.**

- *Observed.* `MFCreateSourceReaderFromURL` resolves by file extension through
  the registry and fails on `.webm`. `MFCreateSourceReaderFromByteStream`
  resolves by content and succeeds on the same data.
- *Consequence.* Two titles with the same container behave differently
  depending only on which entry point their player uses.
- *What would fix it.* Registering the handler. Content-based resolution is
  already working, so this is a registration gap rather than a capability one.
- *Standing.* **Measured.**

**The WMV3 path fails to construct without an ffmpeg plugin present.**

- *Observed.* `qasf` → `wmvdecod.dll` → winegstreamer's `wg_wmv_decoder`, which
  probes a fixed WMV3-to-I420 1920x1080 transform before constructing anything.
  With nothing able to serve it, `IDMOWrapperFilter::Init` returns
  `0xD0000001` — `HRESULT_FROM_NT(0xC0000001)` — and the DirectShow graph never
  builds. With `libgstlibav` reachable, the same call returns `S_OK`.
- *Standing.* **Measured**, on Nioh, either side of the change.

### D3DMetal

**`ID3DDestructionNotifier` is not implemented.**

- *Observed.* `QueryInterface` returns `E_NOINTERFACE`.
- *Consequence.* Unreal Engine 5 does not check the result —
  `WindowsElectraDecoderGPUBufferHelpers.h:276`, where the `check()` is compiled
  out of shipping builds — and the next line dereferences a null vtable. Every
  UE5 title with VP9 cutscenes on the D3D12 RHI crashes on its first cutscene,
  with the same stack at different offsets.
- *Reproduce.* Any such title; the stack names
  `FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer`.
- *What would fix it.* Implementing the interface. It is small, and winevideo
  ships a standalone shim DLL that does nothing else — evidence that the surface
  needed is narrow.
- *Standing.* **Measured.**

**`IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for every node index.**

- *Observed.* The call returns `S_OK` for indices past the number of memory
  nodes. On Windows it fails there.
- *Consequence.* Unreal's D3D12 RHI walks nodes accumulating totals and ends the
  walk when the call fails. Against an answer that never fails, the walk does
  not end. Two titles freeze.
- *Reproduce.* Patch slot 14 of `IDXGIAdapter3` and refuse index 1 with
  `DXGI_ERROR_INVALID_CALL`; the freeze stops.
- *What would fix it.* Returning `DXGI_ERROR_INVALID_CALL` past the node count.
  This is the smallest fix on this page and it removes a hard freeze.
- *Standing.* **Measured.**

**No `ID3D11VideoDevice` or `ID3D11VideoContext`.**

- *Observed.* `QueryInterface` returns `0x80004002` for both, on a device that
  was created successfully.
- *Consequence.* Two distinct failures. A title that sets
  `MF_SOURCE_READER_D3D_MANAGER` and `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`
  gets no D3D-backed decode; and a title that drives the video processor itself
  — `CreateVideoProcessorEnumerator`, `CreateVideoProcessor`, the input and
  output views, and eleven `VideoProcessorSet*` calls — has nothing to drive.
- *What would fix it.* Implementing the video device and processor. Short of
  that, the software path works: stripping those two attributes from the source
  reader's attribute store is sufficient for playback, which suggests a
  fallback inside Media Foundation would serve titles that ask for acceleration
  and can live without it.
- *Standing.* **Measured.**

**Removing the D3D manager moves the frames somewhere the game does not look.**

- *Observed.* Stripping `MF_SOURCE_READER_D3D_MANAGER` is what makes the
  software path available and stops a title crashing, and it changes where the
  sample data lives: system memory rather than a DXGI buffer. A player whose
  upload step expects a DXGI buffer then never fills the surface it hands the
  video processor. That surface was read directly and measured flat -- average
  0, range 0..0 -- while the reader was delivering three hundred good samples.
- *Why this belongs here.* The workaround is only complete for a title that can
  take its frames from system memory. Anything relying on the D3D-backed buffer
  needs the frame delivered for it by hand, which is what this project ends up
  doing. A fallback inside Media Foundation would not need to.
- *Reproduce.* NieR Replicant with the two attributes stripped; read back the
  texture passed to `VideoProcessorBlt`.
- *Standing.* **Measured.**

**`D3D12CreateRootSignatureDeserializer` ends the process when the container
holds no root signature.**

- *Observed.* Given a valid DXBC container with no `RTS0` part, the call does
  not return an error. The process dies reading a field at offset 4 of a null
  pointer, inside
  `D3DMetal.framework/Versions/A/Resources/libmetalirconverter.dylib`, and
  there is no dialog, no Wine backtrace and nothing in any log. On Windows the
  same call returns `E_INVALIDARG`.
- *Consequence.* Asking whether a compiled shader carries an embedded root
  signature is an ordinary thing for an engine to do, and the answer for most
  shaders is "no". A title that asks about the first shader it loads does not
  start at all, and leaves nothing behind that says why. TEENAGE MUTANT NINJA
  TURTLES: SPLINTERED FATE opens a window and closes about three seconds later.
- *Reproduce.* Compile any shader without `[RootSignature(...)]` and pass the
  whole container to `D3D12CreateRootSignatureDeserializer`. The one measured
  here is 3224 bytes and holds `SFI0`, `ISG1`, `OSG1`, `PSV0`, `STAT`, `HASH`
  and `DXIL` — seven parts, none of them `RTS0`.
- *What would fix it.* Checking whether the part is present before reading it,
  and returning `E_INVALIDARG` when it is not. The whole workaround this
  project ships for it is that check: walk the container's part table, look for
  `RTS0`, and answer without calling through when it is absent. Twelve lines,
  and it turns a silent death into a game that plays.
- *Standing.* **Measured**, with the container captured to disk before the
  call that consumed it.

**`ID3D12Device::CreateCommittedResource` with `DXGI_FORMAT_NV12` ends the
process.**

- *Observed.* The call does not return and does not fail. The process
  terminates, with no exception a DLL loaded into it can catch, and nothing is
  written after the call — the last line recorded is the shared handle being
  opened, one call earlier. Requested with `D3D12_RESOURCE_DIMENSION_TEXTURE2D`,
  even width and height, one mip, one array slice,
  `D3D12_HEAP_TYPE_DEFAULT`, `D3D12_RESOURCE_STATE_COMMON`, no clear value.
- *Consequence.* Worse than a refusal, because a refusal can be handled. A
  caller cannot probe for support by trying and checking the result, and cannot
  guard the attempt: there is no result. Any title that publishes or consumes
  NV12 through D3D12 is unreachable, and so is any workaround that would supply
  one on its behalf.
- *Reproduce.* Create a D3D12 device and make that one call. KINGDOM HEARTS
  Dream Drop Distance reaches it by way of a shared texture; nothing about the
  game is needed to reproduce it.
- *What would fix it.* Returning an `HRESULT` for a format that cannot be
  created. Support would be better, but an error is what makes the situation
  survivable — a caller that is told no can fall back, and one that is killed
  cannot.
- *Standing.* **Measured**, on one title, once.

**A game may want the decoder's surface rather than a picture.**

- *Observed.* KINGDOM HEARTS Dream Drop Distance takes the NV12 texture Media
  Foundation would have produced, opens it in D3D12 through a shared handle,
  and copies **plane 0 and plane 1** into resources of its own — `R8_UNORM` at
  the clip's size for luma, `R8G8_UNORM` at half for chroma — then converts
  them in its own shader.
- *Consequence.* Handing such a title a converted `B8G8R8A8` frame is not a
  partial fix, it is invisible: the plane copies read nothing, both planes stay
  at zero, and zero luma with zero chroma displays as solid green. The symptom
  looks like a missing frame and is not one. Any Media Foundation fallback that
  substitutes a picture for the decoder's own surface will produce this.
- *Reproduce.* The plane pair is created immediately after the shared handle is
  opened, and is visible from `ID3D12Device::CreateCommittedResource`.
- *Standing.* **Measured.**

**`IDXGIResource::GetSharedHandle` is `E_NOTIMPL`.**

- *Consequence.* Decoding video on a D3D11 device and presenting it with a
  D3D12 renderer cannot work at all, which is a common shape.
- *Standing.* **Measured.**

**D3DMetal 3.0 freezes two titles that 4.0b2 runs.**

- *Observed.* CrossOver 26.3 carries 3.0; the Preview build carries 4.0b2 and
  ships 3.0 beside it unused. The two copies of 3.0 are byte-identical.
  Everything else is held constant: same game files, same bottle, same backend.
- *Standing.* **Measured** that this is the only difference. **Not measured**
  why. This is the shape a bug report would take rather than a diagnosis.

**DirectStorage cannot create a queue, whatever it is offered.**

- *Observed.* `IDStorageFactory::CreateQueue` returns `0x887a0004`,
  `DXGI_ERROR_UNSUPPORTED`, and writes a null queue pointer. Every capability
  it asks about first is granted -- `OPTIONS17` answered yes, feature 46, 7, 8
  and 23 all succeeding, a 32 MB committed resource created, a copy command
  queue created -- and it still refuses.
- *Not a configuration.* `DSTORAGE_CONFIGURATION1` was set with
  `DisableGpuDecompressionMetacommand` and `DisableGpuDecompression` both on,
  so no metacommand path is involved, and the result is byte-identical. The
  staging buffer was reduced from 256 MB to 32 MB, and `ForceMappingLayer` was
  tried and made the failure earlier and different.
- *Consequence.* Ninja Gaiden 4 stores the null without checking it and calls
  through it -- `mov rdx, [rax]` with `rax` at zero. The same shape as
  `ID3DDestructionNotifier`: a `check()` compiled out of a shipping build.
- *And the fallback does not work either.* Denying the factory outright, which
  is the same lever as removing `dstoragecore.dll`, makes the title choose its
  other I/O backend. It then stalls: every one of its 76 threads waiting, none
  of them inside DirectStorage, Media Foundation, D3D12 or DXGI, and the main
  one blocked in a C++ condition variable.
- *Standing.* **Measured.** Why the queue is refused is **not measured** -- the
  decision happens inside Microsoft's `dstoragecore.dll` running under Wine,
  and the Agility SDK this title ships is never loaded.

**DirectStorage fails on both CrossOver lines, and a patched CrossOver does not
change it.**

- *Observed.* `CreateQueue` returns `DXGI_ERROR_UNSUPPORTED` on
  `crossover-preview-arm64-20260821` and on stable 26.3 alike, including on a
  26.3 patched with winevideo 0.5. Same result, same null queue, same
  dereference in the calling title.
- *Consequence.* Any title that uses DirectStorage and does not check
  `CreateQueue`'s result dies. The only working configuration found is one where
  `dstoragecore.dll` is removed so that no factory exists at all and the title
  takes another path.
- *Standing.* **Measured** on two CrossOver versions and with a third-party
  patch installed. Why the queue is refused is **not measured**.

**A per-title override table decides feature availability by executable name.**

- *Observed.* `__ZL20ApplicationOverrides`, 49 records of `0x48` bytes, matched
  against the executable's basename. Byte `+0x24` gates `EnumerateMetaCommands`.
- *Consequence.* A title's DirectStorage support depends on whether its name is
  in that table, which is invisible from outside and cannot be configured.
- *Standing.* **Measured**, by binary analysis.

**`d3d12.dll` refuses to initialise under another module name.**

- *Observed.* Renamed, it returns `ERROR_DLL_INIT_FAILED`. `dxgi.dll` tolerates
  the same rename.
- *Consequence.* A CrossOver-level proxy is possible for DXGI and not for D3D12,
  which is what keeps most fixes here per-game.
- *Standing.* **Measured** for both. Whether a proxy that exports
  `D3D12CreateDevice` itself is also refused is **not measured**.

### CrossOver packaging

**Stable 26.3 ships no WebM demuxer.**

- *Observed.* 26.3 carries 17 GStreamer plugins and
  `crossover-preview-arm64-20260821` carries 19; the two Preview has to itself
  are `matroska` and `osxaudio`.
- *Consequence.* A `.webm` cutscene cannot be opened on stable, so nothing gets
  as far as a decoder. Both builds decode VP9 identically through
  `applemedia`, so this is a container gap, not a codec one.
- *What would fix it.* Shipping `libgstmatroska` in the stable line.
- *Standing.* **Measured** for the plugin sets. **Inferred** for the effect on
  stable — the affected title has never been launched there.

**No VC-1 or WMV3 decoder is shipped in either line.**

- *Observed.* Neither build carries `libgstlibav` or `libgstvpx`.
- *Consequence.* Three titles here need a decoder staged from the user's own
  GStreamer install to play at all.
- *Standing.* **Measured.**

**The launcher leaves `GST_PLUGIN_PATH` free.**

- *Observed.* CrossOver's launcher sets only `GST_PLUGIN_SYSTEM_PATH`, around
  line 690 of `bin/wine`, and the bottle's own environment is applied first.
- *Consequence.* A plugin can be supplied per bottle without modifying the
  installation. This is the property the staging here depends on; it is recorded
  so that a change to it is known to break something.
- *Standing.* **Measured** on Preview. **Inferred** for other builds — not
  checked on any.

### A note on method

**A black screen with correct audio has two causes, and they are separable.**
A frame that never arrives and a frame written somewhere that is never
displayed look identical from outside. Painting the target a colour the game
could not produce tells them apart in a single run: if it appears, the write
path is right and the fault is upstream of it. That test is what turned NieR
Replicant from a guess into an implementation, and it had settled the same
question for the DYNASTY WARRIORS bridge earlier.

The corollary is the reason frames are read back rather than trusted: a bridge
that hands over a valid but empty surface produces a game that runs and a
screen that is black, and nothing distinguishes that from success without
looking at the pixels.

### Open, and attributable to nobody yet

**Why D3DMetal 3.0 freezes those two titles.** The fault arrives before any
guard can act: on 3.0 the adapter node walk never happens, so whatever stops
them is upstream of the walk that explains the same freeze on other builds.

**What decodes Persona 5 Strikers' VC-1.** The title plays on a system where the
staged ffmpeg plugin is demonstrably not loading, so something else is serving
it. Not chased down.

## The container, not the codec

The most cross-cutting result here, and the one that took longest to arrive at.
This is the canonical account; [Games](Games.md) and
[DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) state the conclusion
and point here.

Established by comparing the two installs plugin by plugin on this machine
rather than by running anything: CrossOver 26.3 carries 17 GStreamer plugins and
`crossover-preview-arm64-20260821` carries 19, and the two Preview has to itself
are `matroska` and `osxaudio`.

`matroska` is a demuxer, not a decoder, and it is the whole difference. Both
builds carry `applemedia` and decode VP9 through VideoToolbox; neither ships
`libgstvpx` or `libgstlibav`. What only Preview can do is open a WebM container.
DYNASTY WARRIORS' 355 cutscenes are `.webm`, so on stable nothing gets as far as
a decoder.

Mortal Shell 2 is not the control it looks like, and it is worth saying why. Its
61 cutscenes are the same codec in a different box, and it plays on stable — but
its VP9 never reaches CrossOver's media stack at all. Electra opens the `.mp4`
and decodes it in-process with its own libvpx, which is the finding recorded
under [why only VP9, and only D3D12](#why-only-vp9-and-only-d3d12) below. So it
is evidence that VP9 is not what stops DYNASTY WARRIORS, and no evidence at all
about which containers CrossOver can open. The plugin-set comparison is the
whole of that.

It also closes a question this project carried open for a while. The cutscene
was measured playing on Preview in a bottle winevideo had never touched, with
the `.webm` byte-stream handler registered — a run that was expected to fail and
did not, with no account of how the WebM was being opened. The `matroska` plugin
is the account. Whether that handler mapping was needed in that run is a
separate question and still open;
[the title's page](Dynasty-Warriors-Origins.md) says what was and was not tried.

It narrows what the fix requires. The bridge presents frames and decodes
nothing, so the open has to succeed first: on a build with no WebM demuxer,
`MFCreateSourceReaderFromByteStream` on a `.webm` fails outright and there is
never a frame to carry.

**The limit on all of this.** DYNASTY WARRIORS has never been launched on stable
26.3, with or without anything added. Everything said here about stable is read
from the plugin sets rather than from a run.

**What would close the gap, and has not been built.**
[winevideo](https://github.com/Jfishin/winevideo) supplies a WebM demuxer today,
by patching the CrossOver installation. Staging `libgstmatroska.dylib` would
supply one without patching anything — 756 KB, in the official
GStreamer.framework, the same re-homing move that stages VC-1 for Persona 5
Strikers. Neither has been measured with this title, and the staging has not
been written. It is recorded here as a plugin to stage rather than an engine to
patch, not as something that works.

## The freeze on 26.3, and what a control run settled

Both Life is Strange titles freeze on stable CrossOver 26.3 and run on
`crossover-preview-arm64-20260821`. This is the canonical account; both title
pages state the observation and point here.

**Nothing installed beside the game is involved.** Established by a control
run: the fix removed entirely, the game restored to what Steam delivers,
relaunched on 26.3 — and it freezes exactly as before.

**The guard never runs.** The policy table [below](#one-dll-three-repairs) arms
the DXGI node guard for `Iris` and `Chronos` and nothing else. On Preview it arms
and, in three sessions out of nine, reports refusing a node that does not exist.
On 26.3 it arms and then reports nothing at all: five lines of log against
twenty-nine, and no adapter node is ever walked. Whatever stops the game arrives
before the code written for it can act, which is also why the guard's own
evidence cannot explain this.

**What differs is D3DMetal.** CrossOver 26.3 carries 3.0; that Preview carries
4.0b2 and ships 3.0 beside it, unused and unreferenced. The two copies of 3.0 are
byte-identical, so "stable" and "GPTK 3" are interchangeable in any account of
this. Nothing else about the two installs distinguishes these titles: the proxy
DLL is the same file, the bottle the same bottle, the backend `d3dmetal` in both.

**Still unknown** is why GPTK 3 freezes here at all. That is a question for the
engine rather than for this project, and it is the shape a report upstream would
take: a title that runs on 4.0b2 and freezes on 3.0, with nothing of ours in the
process.

One thing this leaves worth asking. The guard fires in a minority of sessions
even where it works, and not at all where the freeze happens. How much of the
improvement on Preview is actually its doing has never been controlled, and the
same experiment would answer it.

## One DLL, three repairs

The Unreal titles are served by one proxy DLL carrying three unrelated repairs,
and each is asked for by executable name rather than applied wherever its
pattern happens to match:

- the Electra buffer path that crashes Mortal Shell 2,
- the adapter-node walk that freezes both Life is Strange titles,
- the H.264 output negotiation that leaves Beast of Reincarnation
  silent-but-blank.

They share one file because they share one carrier DLL. Separate files would
mean a title could only ever have one of the three. The merged DLL logs to
`C:\ue5-media-fix.log`; releases from before the merge wrote
`C:\ue5-runtime-fix.log`, which is the name to look for in an old log.

Which of the three a title gets is a table in `runtime/ue5-media-fix.c`, keyed
by the shipping executable's name:

| Executable | Title | Armed |
| --- | --- | --- |
| `MortalShell2-Win64-Shipping.exe` | Mortal Shell 2 | Electra VPx |
| `Iris-Win64-Shipping.exe` | Life is Strange: Reunion | node guard |
| `Chronos-Win64-Shipping.exe` | Life is Strange: Double Exposure | node guard |
| `BeastOfReincarnation-Win64-Shipping.exe` | Beast of Reincarnation | H.264 / NV12 |
| anything else | an untried Unreal title | all three |

Two things follow from that table. Neither Life is Strange title runs the H.264
half: it is present in the file they install and inert in their process. And an
executable the build does not recognise arms all three rather than a chosen
subset, which the log says on startup, so an unexpected result is traceable to
the table rather than mistaken for a measurement. That last row is what the
app's **Another Unreal Engine 5 title** entry reaches, and it is why an untried
title is best tried on Preview: the 26.3 defect is open and unexplained, and a
title the table does not recognise arms more of the DLL than any title that has
been measured, so Preview is where it carries the least unknown.

The Media Foundation hooks are the exception to the gating. They are installed
for every title, armed or not, because they are how a new one is surveyed: what
the gates decide is whether those hooks change anything, not whether they are
there. That unconditional instrumentation is the standing suspicion in
[the freeze on 26.3](#the-freeze-on-263-and-what-a-control-run-settled) above.

## Why only VP9, and only D3D12

The Mortal Shell 2 crash is engine code rather than title code, which is why it
is recorded here as well as on [that title's page](Mortal-Shell-2.md).

Apple's D3DMetal does not implement `ID3DDestructionNotifier`. Unreal asks for
it and uses the result without checking the HRESULT —
`WindowsElectraDecoderGPUBufferHelpers.h:276` is:

```cpp
TRefCountPtr<ID3DDestructionNotifier> Notifier;
Res = Resource->QueryInterface(__uuidof(ID3DDestructionNotifier), ...);
check(SUCCEEDED(Res));                              // compiled out in Shipping
Res = Notifier->RegisterDestructionCallback(...);   // null vtable deref
```

`QueryInterface` returns `E_NOINTERFACE`, `Notifier` stays null, the `check()`
does not exist in a shipping build, and the next line reads address 0. The
stack, for recognising it in a crash log of your own:

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000000
...!FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer()
   [Engine\Plugins\Media\ElectraCodecs\...\WindowsElectraDecoderGPUBufferHelpers.h:276]
...!FVideoDecoderVPxElectra::ConvertDecodedImageToNV12orP010()
Crash in runnable thread ElectraPlayer::Video decoder
```

Every Electra decoder gates the D3D12 output buffer pool on the same condition.
The difference is who guards it:

| Decoder | Guard | On D3D12 |
|---|---|---|
| H.264 / H.265 | `if (… && CVarElectraWindowsH264UseOldOutputPath == 0)` | set the CVar to `1` and it never touches the pool |
| VPx (VP8/VP9) | none — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)` | always enters it → crash |

So VP9 always hits it on D3D12. Titles that run on D3D11 — Persona 5 Strikers,
for one — never do: the same bug is there and unreachable.

**There is no VPx CVar.** Extracting every string from the shipped executable
turns up only `Electra.Win.H264UseOldOutputPath` and
`Electra.Win.H265UseOldOutputPath`. VP9 on D3D12 has no configuration escape.

`-dx11` also dodges the crash, but Unreal does not precompile PSOs on the D3D11
RHI, which means permanent shader-compilation stutter. That is the same trade
that makes `-dx11` a poor answer to the Life is Strange freeze.

**VP9 here never goes through Media Foundation,** which is why this fault needs
nothing from CrossOver's media stack. It was measured rather than argued: the
game was played through on CrossOver 26.3 carrying no winevideo, and again on
CrossOver-winevideo 26.3, the same version differing only in the GStreamer
plugins. The fix worked either way. That paired run is the only controlled
comparison in the project, and several claims elsewhere lean on it — it was made
on [Mortal Shell 2](Mortal-Shell-2.md) and no other title.
`diagnostics/launch-with.sh` is what makes that comparison cheap: bottles with a
build present or absent, without reinstalling anything.

The Life is Strange fix is independent of winevideo for a reason rather than by
measurement: the node walk is in DXGI and has nothing to do with video decoding,
so the guard depends on no media plugin at all.

## The adapter-node walk, and where the guard can live

Unreal walks the adapter's memory nodes through
`IDXGIAdapter3::QueryVideoMemoryInfo`, accumulating across them, and ends the
walk when the call fails. On Windows it fails once the index passes the number
of nodes. D3DMetal answers `S_OK` for every index, so the counter climbs
forever. Refusing node 1 with `DXGI_ERROR_INVALID_CALL` ends it, once per
session — so the guard writes one line the first time it refuses, and that line
in the log is what says the fix took effect. A log carrying the Electra lines
but not that one says the game never made the walk, which rules this fault out
rather than the fix. The disassembly and the spindump are on
[Reunion's page](Life-is-Strange-Reunion.md).

**This one is not about these games.** The walk is in Unreal's D3D12 RHI rather
than in anything either title added, so the guard is worth trying on any Unreal
game that freezes this way. Two executables have been scanned and both carry the
loop; that is the whole sample.

Because the fault is not in either game, the guard can live in two places, and
the choice is a trade rather than a conclusion.

The **per-game** fix is the default, because patching one game's process has a
much smaller blast radius than replacing a DLL every bottle loads.

A **CrossOver-wide** override also exists: `crossover/install-node-guard.sh`
replaces Apple's `dxgi.dll` inside a CrossOver build with a proxy handling all
seven exports — three implemented here, which is how the adapter gets wrapped at
all, and four PE forwarders to the renamed original. It reaches every game in
every bottle using that CrossOver without anybody selecting a folder, at the
cost of an invalidated code signature and a much larger blast radius.

That this guard is possible at all rests on Apple's `dxgi.dll` tolerating being
renamed to `dxgi_real.dll`. `d3d12.dll` is not known to tolerate the same, which
is what limits the move — see
[things that do not work](#things-that-do-not-work).

### The 100 ms cache, which is not in the fix

A separate result came out of the same work: serving repeat queries from a
100 ms cache was reported as feeling smoother. No frame time was captured before
or after, so that is a report rather than a measurement, and the cache was not
retained. The shipping guard holds nothing between calls.

The reasoning is written down anyway, because it is not title-specific:
thousands of crossings a second into Wine's unix side cost more than their wall
time, which is contention rather than cycles. Anyone bringing it back should
capture a frame time first, since that is exactly what the original report
lacked. The fuller note is on [Reunion's page](Life-is-Strange-Reunion.md).

## Decoding on D3D11 and presenting with D3D12

The DYNASTY WARRIORS fault is what happens to any game that decodes video on a
D3D11 device and presents it with a D3D12 renderer, because
`IDXGIResource::GetSharedHandle` is `E_NOTIMPL` under D3DMetal for all of them.
The five faults in sequence, the bridge design and the copy-queue reasoning are
on [that title's page](Dynasty-Warriors-Origins.md).

Three things from it generalise.

**A handle of ours is a new invention, not a port.** winevideo's D3D9 bridge
does not invent a handle: it creates a texture with
`D3D11_RESOURCE_MISC_SHARED`, calls the real `GetSharedHandle`, and fails when
that does not work. What it substitutes is Wine's unimplemented D3D9 sharing
with D3D11 sharing that DXMT does implement. The one call their design rests on
is exactly the one that is `E_NOTIMPL` under D3DMetal, so the parts of this with
no reference implementation are the ones most likely to be fragile.

**The frame converter is not title-specific.** The NV12-to-BGRA converter came
across from the DYNASTY WARRIORS bridge to the
[Persona 5 Strikers](Persona-5-Strikers.md) bridge unchanged. That is the
clearest evidence so far that the logic in these fixes is general and only the
carrier is not.

**Backend is a requirement, not a preference.** DXMT implements sharing where
D3DMetal has none to build on: `GetSharedHandle` appears 17 times in DXMT's
`d3d11.dll` and not once in Wine's. That is why Persona 5 Strikers is fixable on
DXMT and on nothing else, and why a game in DYNASTY WARRIORS' position may work
under a different backend instead — untested here.

## Two bridges, and which title needs which

By the ninth title the pattern is not one bridge but two, and the split does
not follow the names on the boxes.

The **D3D9 bridge** serves Persona 5 Strikers, Nioh and Nioh 2. They ask D3D9
for a shared surface, Wine returns `S_OK` with a handle of zero, and the bridge
supplies a real one from the D3D11 side. It needs DXMT, because
`GetSharedHandle` is what the whole design rests on and D3DMetal has none.

The **D3D11-to-D3D12 bridge** serves DYNASTY WARRIORS and Nioh 3. They ask
Media Foundation to decode into D3D video textures, which D3DMetal cannot
provide -- `QueryInterface(ID3D11VideoDevice)` is `E_NOINTERFACE` -- so the
bridge strips the request down to software decode and stubs the entire video
processor the game then drives by hand. It runs on D3DMetal.

Nioh 3 belongs to the second group despite sharing a name with the first two,
which is worth stating plainly: the series a game is in predicts nothing. What
predicts the fix is which API it reaches its frames through.

## The same bridge, three games

Nioh and Nioh 2 were both fixed by the Persona 5 Strikers bridge with no change
to what the bridge does. That is the strongest evidence so far that these
repairs are general and only the carrier is not.

The three do not even reach the video the same way. Strikers and Nioh 2 go
through Media Foundation; Nioh builds a DirectShow graph. What they share is
one line further down, where a shared D3D9 surface is asked for and the handle
comes back null -- and that is the line the bridge watches, which is why the
player above it can differ without mattering.

**The gap, stated exactly.** The game asks D3D9 for a shared render target.
Wine's `d3d9` creates it, returns `S_OK`, and hands back a share handle of zero.
Nothing obliges a caller to survive that, and Nioh does not: it carries the null
through to `mov rdx, [rdx+0x10]` on a worker thread and dies there. The repair
is not to make video work. It is to hand back a handle that exists.

    CreateRenderTarget(1920x1080 fmt=21, SHARED requested) -> S_OK, handle 0
    sidecar: 1920x1080 texture, GetSharedHandle -> S_OK, handle 40000082
    OpenSharedResource: our handle -- made a texture on the game's own device
    StretchRect INTO it
    source luma [420]: average 11, range 0..166   << has picture

The luma line is why this is recorded as working rather than as reported
working: a bridge that hands over a valid but empty surface produces a game that
runs and a screen that is black, and the two are indistinguishable without
reading the pixels.

**The Ex variants are a second door.** Persona 5 Strikers creates its device
through `IDirect3D9::CreateDevice`, slot 16. Nioh reaches the same object
through `Direct3DCreate9Ex` and `CreateDeviceEx`, slot 20, and never calls slot
16 at all. Anything watching one of those interfaces has to watch both, and slot
20 may only be written on an object that came from `Direct3DCreate9Ex` — on a
plain `IDirect3D9` that index is past the end of the vtable.

**And it held for a third.** Nioh 2 needed no code change at all -- same
carrier, exporting the identical 16 symbols, so even the built proxy was reused.
It exercised more of the bridge than Nioh had: four of six import hooks landed
rather than two, `MFCreateSourceReaderFromByteStream` accepted the ASF, and the
frames arrived as NV12 with a 1920 pitch where Nioh's had been a four-byte
format at 7680. The NV12-to-BGRA converter that came across from DYNASTY
WARRIORS absorbed that difference without being told.

**What is not measured.** Neither Nioh title has run anywhere but Preview and
DXMT. Whether
the bridge works under D3DMetal is not open in the same way -- the sidecar needs
`GetSharedHandle`, which is `E_NOTIMPL` there -- but it has not been tried, and
26.3 has not been tried at all.

## The module a function lives in is not the module it is asked from

Three of these fixes hook a function by naming the DLL it belongs to. That
assumption held for eight titles and broke on the ninth.

Nioh 3 ships NVIDIA Streamline. It imports `D3D11CreateDevice` and
`D3D12CreateDevice` from `sl.interposer.dll`, a drop-in replacement exporting
the same names, and never mentions `d3d11.dll` or `d3d12.dll` anywhere in its
import table. A hook placed against the real module's name finds nothing, says
`not imported`, and the game proceeds unwatched.

There are two ways a game reaches these entry points and both need covering: a
static import from whatever module it names, and a runtime `GetProcAddress`.
DYNASTY WARRIORS uses the second, Nioh 3 the first, and the probe written for
Ninja Gaiden 4 had only ever needed the second.

**A substituted handle needs both ends.** A bridge that answers a failing
`GetSharedHandle` with a handle of its own invention is only correct if the
place that handle comes back to — `ID3D12Device::OpenSharedHandle` — is also
intercepted. Reaching one and not the other sends an invented value into a real
device, and the result is an access violation rather than a clean failure. Both
ends have to be gated on each other.

This is worth stating for the same reason as the defect it works around: a
component that answers *no* to sharing lets a caller fall back, and a caller
handed something that is not a handle has nowhere to go.

## Living outside CrossOver

winevideo repairs several of these titles by replacing binaries inside the
CrossOver installation: its payload carries a patched Wine `d3d9.dll`,
`mfplat.dll` and `winegstreamer`, and its two D3D9 patches add the bridge handle
and the sidecar upload to `d3d9` itself. This project reaches several of the
same places from inside the game process. That is a trade, not an improvement,
and anyone who finds both will ask about it.

**What being outside buys.**

*The fixes survive a CrossOver update.* A patched binary has to be rebuilt for
every build it is applied to. winevideo declares stable 26.2 or 26.3 and states
that Preview is not supported; that follows from what it ships rather than from
a preference. The patches here survived a CrossOver version change without
recompiling, which is the property that makes targeting a Preview build possible
at all.

*Nothing is redistributed.* No modified Wine binaries leave this repository, and
a fix is removed by deleting one file from the game folder.

*The findings stay reportable.* Every conclusion here is stated at the level of
an interface or an instruction -- D3DMetal's per-title override table, the
`Disable*` fields in `DSTORAGE_CONFIGURATION1`, the share handle of zero. That
form is directly actionable for whoever maintains the runtime. A diff against a
Wine tree is not the same offer.

*Old games hold still.* These titles will not ship another patch, so teaching the
bridge which entry point one of them uses is a cost paid once rather than
maintenance.

**A decoder has to live in the process, and it can still be ours.** A title that
needs a VP9 decoder MFT needs one inside the process that owns Media
Foundation. It does not follow that it has to come from a patched
winegstreamer, and this page briefly said it did. Both calls that decide which
transform gets used -- `MFTEnumEx` and `IMFActivate::ActivateObject` -- are
interceptable from a proxy, so a transform of our own can be handed over without
registering anything. What it would need inside it is a decoder, and Mortal
Shell 2 is the existing proof that one can be carried in-process: its Electra
decodes VP9 with its own libvpx.

Not built, and not needed by any title fixed here. Recorded because the limit
was stated more strongly than the evidence supports.

**What being outside costs.** A hook in the game process reaches only what the
game calls. A repair living in `d3d9` sees every surface presented no matter
which entry point created it; this one saw nothing at all until it was told
about slot 20, and that is the general shape of the weakness rather than a
single oversight. The tamper-protection limit noted under *The staged codecs*
applies here too.

## Getting a fix into the process

How a repair reaches the code it has to change: the patch itself, the DLL it
rides in on, and the two ways a call is intercepted once it is there.

### The runtime patch

Electra decides whether to use the D3D12 buffer pool by comparing the D3D
version against 12000 — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)`.
The compiler turns that into:

```asm
cmp dword [rbp+disp], 12000     ; 81 7D xx E0 2E 00 00
jl  <cpu path>                  ; 7C xx   or   0F 8C xx xx xx xx
```

The 12000 is raised to `INT_MAX`, so the comparison always takes the CPU branch.
Four bytes per site, four sites in the Mortal Shell 2 build, each confirmed to
be reached at runtime rather than assumed from the disassembly. Nothing else is
touched: the decoder still decodes VP9 with its own libvpx, and Unreal presents
the frames the way it does on any D3D11 machine.

The compare has to be against a **stack slot** (`81 7D` / `81 BD`). The H.264
and H.265 decoders compare a register instead, and they already have a way out
through `Electra.Win.H264UseOldOutputPath` — leaving them alone keeps this
change to the one decoder that has no other option.

It is a pattern scan, not a table of offsets, because a game update moves
everything: between two builds of Mortal Shell 2 the crash site alone shifted by
`0x2C70`. If the pattern does not match, nothing is written and the log says so.

No static scanner for that pattern ships here. The code that knows it is the
runtime patch itself, so the count of sites arrives once the fix is installed
rather than before.

### The carrier DLL

The game has no plugin hook, so the patch rides in on a DLL the engine already
loads. `libogg_64.dll` is a good carrier: every Unreal title ships it, it loads
before any cutscene, its ABI has been frozen for years, and it has nothing to do
with rendering — so a proxy in front of it cannot disturb the renderer.

```
libogg_64.dll    <- our proxy
libogg_real.dll  <- the game's original, renamed, untouched
```

The proxy is 90,624 bytes as it ships today. That figure moves whenever another
repair is merged into the one carrier, so read it as a scale rather than a
constant.

The proxy re-exports all 64 symbols as PE **forwarders** straight to
`libogg_real`, so the Windows loader resolves them on demand and no thunk code
of ours ever runs. The only thing we get is `DllMain`, which starts a thread and
applies the patch.

The installer refuses to run if the game's `libogg` exports anything the shipped
proxy does not forward — a missing entry point would stop the game from starting
at all, so it is better to fail early and ask for a rebuild.

The same shape carries the other two fixes on different carriers: `libxess.dll`
for DYNASTY WARRIORS, 27 exports, with the game's own renamed to
`libxess_real.dll`; `amd_ags_x64.dll` for Persona 5 Strikers, 38. Both are
vendor libraries the game imports directly and barely uses under CrossOver —
`libxess` is Intel's XeSS upscaler, which is why a proxy in front of it cannot
disturb video.

### Import tables, and vtable slots

Two ways in, used for different things.

**Import-table hooks** replace the address the game calls for a named function
in a named DLL. The IAT is a documented structure, so nothing depends on which
compiler built the game or on code moving between updates. Two traps come with
it, and both have cost runs here:

- **Delay-loaded imports** are resolved through `GetProcAddress`, so a hook on
  the import table alone never fires. `GetProcAddress` is hooked as well, which
  also catches a call made through some other module — DYNASTY WARRIORS asks
  NVIDIA Streamline's `sl.interposer.dll` for `D3D12CreateDevice` by name.
- **Imports by ordinal** carry no name, and a hook that walks names skips them.
  The hook then installs, reports itself installed, and is never called.
  DYNASTY WARRIORS imports `D3D12CreateDevice` from `d3d12.dll` by ordinal 101.

Both traps also explain an empty probe log, which is why
[Diagnosing a new game](Diagnosing-a-new-game.md) sets them out again in that
context.

**Vtable-slot patches** replace one entry of one COM object's function table,
which is how a call is intercepted on an interface nobody exports a function
for. The slot is counted from the start of the interface's inherited layout, so
the number has to be derived rather than guessed:

| Interface | Slot | Method |
| --- | --- | --- |
| `IDXGIAdapter3` | 14 | `QueryVideoMemoryInfo` — three `IUnknown`, four `IDXGIObject`, three `IDXGIAdapter`, `GetDesc1`, `GetDesc2`, two content-protection entries |
| `IDXGIResource` | 8 | `GetSharedHandle` |
| `IDXGIFactory1` | 7 / 12 | `EnumAdapters` / `EnumAdapters1` |
| `ID3D11Device` | 5 | `CreateTexture2D` |
| `ID3D11DeviceContext` | 48 | `UpdateSubresource` |
| `ID3D12Device` | 32 | `OpenSharedHandle` |
| `IMFSourceReader` | 6 / 7 / 9 | `GetCurrentMediaType` / `SetCurrentMediaType` / `ReadSample` |
| `IMFMediaType` | 10 | `GetGUID` — an `IMFAttributes`, so 3 + 7 |

Two things about these are worth knowing before writing one. A vtable is shared
by every instance of a class and read by every thread, so the moment the slot is
written the replacement is live everywhere at once — the original pointer has to
be stored before the slot goes live, not after. And a `NULL` slot means the
object is not the class it was taken for; the patch refuses rather than writing
into whatever is there. `IDXGIFactory`'s vtable ends at 11, which is what makes
the `IDXGIFactory1` query before slot 12 a correctness requirement rather than
tidiness.

## The staged codecs, and where they come from

Persona 5 Strikers needs a VC-1 decoder **no CrossOver ships** — that is a
property of CrossOver rather than a difference between the two lines, and it is
what makes this title independent of which build it runs on. Nothing is
distributed here: `runtime/stage-codecs.sh` borrows the decoder from the user's
own official GStreamer install. The staging, the dyld crash that made re-homing
necessary, and the layout trap that cost a first attempt are on
[that title's page](Persona-5-Strikers.md).

Four things about the staging are not title-specific.

**What else is in that plugin.** `libgstlibav` carries ffmpeg. VC-1 is what this
title needs and what was measured, by the game playing. WMV3 and WMA are in the
same plugin and are expected to work; neither has been exercised here, because
ffmpeg ships no encoder for either and no test bitstream could be made.

**Why 1.24.14.** It is the version this was verified with. Others may well be
fine — winevideo names 1.24.13 for the same titles — and the requirement is
likely to be the 1.24 series rather than that exact release, since the plugin
only has to be ABI-compatible with the CrossOver GStreamer core it is re-homed
onto. No other release has been tried, so that last part is an inference from
ABI policy and not a measurement. `stage-codecs.sh` prints the version it finds
and says so when it is outside 1.24, reporting rather than refusing: turning
away something that might work is as unhelpful as staying quiet about something
that might not.

**Re-homing has to follow the whole `@rpath` chain, not one level.** A staged
plugin names its own dependencies, but those name siblings of their own —
`libgstpbutils` names `libgsttag` — and dyld resolves those against the staging
directory rather than against CrossOver's. Linking only what the plugin itself
names leaves the second level unresolved, and the plugin then fails to load
outright and silently: no error reaches the application, and the only trace is a
GStreamer warning on a channel nobody is reading. A staging that appears to do
nothing should be checked for this before anything else is concluded from it.

Following the chain is what takes Nioh's DMO `Init` from `0xD0000001` to `S_OK`
and lets the DirectShow graph build.

**Per-engine staging.** Every installed CrossOver gets its own staging
directory, keyed by the engine's `CFBundleVersion` — the same string a bottle
records as its Version, so a bottle can be matched to the staging it needs
without guessing, and an engine whose `.app` filename does not say what it is
still resolves. The bottle setting survives because the launcher sets only
`GST_PLUGIN_SYSTEM_PATH` and never touches `GST_PLUGIN_PATH`, and the bottle's
environment is applied first. That was read out of Preview's `bin/wine` and has
not been checked on another build, so the staging assumes it holds across
CrossOver releases rather than knowing it does.

**The idea is not ours.** [winevideo](https://github.com/Jfishin/winevideo) does
this too — its README describes importing WMV/VC-1 codecs from the user's
official GStreamer install, and it requires GStreamer 1.24.13 for exactly the
titles that need them. What differs is only the mechanism: winevideo patches the
CrossOver installation to make those plugins loadable, and this reaches the same
end with one staged folder and one line of bottle configuration.

That difference runs through the whole comparison, and it is a trade rather than
an improvement. This project reaches several of the same places from inside the
game process instead of by patching Wine, which is only possible because
winevideo had already worked out what was wrong. Where the two differ most:
winevideo works outside the game, so it reaches titles protected against
tampering, which nothing here can.

## Titles that need nothing, and why that is worth recording

A page of defects makes every failure look inevitable. These were tested and
play correctly untouched, and the reason each one does is the same reason it
was never at risk.

- **KINGDOM HEARTS 0.2 Birth by Sleep.** 49 CriWare `.usm` cutscenes carrying
  MPEG-1 (`mpeg_codec = 1`, `00 00 01 B3` sequence headers at 1920×1080,
  `ffprobe` reporting `mpeg1video`), decoded by CriWare's own software decoder,
  statically linked. It never asks Media Foundation for anything and never asks
  D3D for a video surface.
- **KINGDOM HEARTS III.** 180 CriWare `.usm`, this time carrying H.264, on the
  D3D11 RHI. Plays at full rate with the bridge not loaded at all — verified by
  an empty log, not by absence of complaint.

The pattern is worth stating: **a title that decodes its own video in software
and uploads the result itself has nothing here to break.** Every fault on this
page arrives through a game asking the platform for hardware decode, a D3D
video device, or a D3D-backed surface. A game that asks for none of those is
unaffected by all of them, whatever its container or codec.

**The adapter describes itself as two different vendors at once.**

- *Observed.* `IDXGIAdapter::GetDesc` returns a description that does not agree
  with itself:

      adapter 0 from GetDesc: "AMD Compatibility Mode"
        vendor 0x10de   device 0x66af
        dedicated video 38338 MB, dedicated system 38338 MB, shared 38338 MB

  `0x66af` is AMD's Radeon VII. `0x10de` is NVIDIA's vendor id. The name says
  AMD. Two of the three say AMD, and the one that disagrees is the field
  software actually branches on.
- *Consequence.* DXMT reads the vendor id, concludes NVIDIA, and says so:
  `info:  Vendor extension enabled: NVEXT`. It then has `nvapi64.dll` and
  `nvngx.dll` in play on a machine with no NVIDIA hardware in it. Engines branch
  on this constantly -- 0x1002 AMD, 0x10de NVIDIA, 0x8086 Intel -- and a game
  taking an NVIDIA path on an adapter that answers like an AMD card in every
  other respect is a real hazard, whatever any individual game does with it.
- *Also.* The three memory figures are equal, which no real adapter reports:
  Windows gives dedicated system memory as zero for a discrete card, and a
  shared figure unrelated to the dedicated one.
- *Reproduce.* Any title under D3DMetal; log `GetDesc` and `GetDesc1`. For the
  DXMT half, run any D3D11 title with `CX_GRAPHICS_BACKEND=dxmt` and read its
  log.
- *What would fix it.* Make the three agree. Either report AMD throughout
  (0x1002, matching the name and the device id already in use), or report
  something that is not any of the three vendors software special-cases.
  Success-with-a-contradiction is the harmful answer, as it is elsewhere in this
  list.
- *Standing.* **Measured**, on both backends. Rewriting the vendor id to 0x1002
  under D3DMetal did not change the outcome for the title it was found on --
  which says it is not what breaks that title, not that the inconsistency is
  harmless.

## The fault that was not in the stack

Every other repair here works around something the translation layer does
wrong. This one does not, and saying so plainly matters — a bug filed against
D3DMetal for this would be closed, correctly.

**A game that keeps only 16:9 resolutions, on a display that has none.**

- *Observed.* Tormented Souls 2 dies before the first frame with
  `EXCEPTION_ACCESS_VIOLATION reading address 0xfffffffffffffff8`. That address
  is −8: a null array base indexed with −1, and −1 in Unreal is `INDEX_NONE`.
- *Why the array is empty.* The game asks its RHI for the available
  resolutions — the call succeeds, thirteen come back — and then filters them
  with two `comisd` instructions against doubles in its own `.rdata`, keeping
  only aspect ratios strictly between 1.76 and 1.79. That is 16:9 and nothing
  else. This screen is 2056×1329, an aspect of 1.547; every mode offered for it
  is 1.6 or 1.547, because they are modes for this panel. Nothing survives the
  filter, the array stays empty, the search for a current mode returns
  `INDEX_NONE`, and the next instruction indexes the empty array with it. There
  is no branch after the filter for the empty case.
- *Whose defect it is.* The game's. Ask Windows for the modes of a 3:2 monitor
  and you get 3:2 modes; this code reads −8 there too. It is an assumption that
  every display is widescreen — nearly always true on a desktop monitor, nearly
  always false on a laptop.
- *Reproduce.* Any non-16:9 display. The trigger is not a size, it is the
  absence of a 16:9 mode, which holds for 16:10, 3:2, 4:3 and ultrawide alike.
- *The workaround.* Append 16:9 modes to what `IDXGIOutput::GetDisplayModeList`
  returns when it offers none, in
  [`d3d12-guards.c`](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/runtime/d3d12-guards.c).
  Rendering is unaffected: the game draws into a swap chain of whatever size it
  asks for, and the mode list feeds the menu and this filter, not the back
  buffer.
- *Standing.* **Measured.** The two constants were read out of the game's
  `.rdata`; the filter and the missing branch were read in the disassembly, not
  inferred from behaviour.

Two things about `DXGI_MODE_DESC` are worth writing down, because getting them
wrong cost a whole hypothesis here. It is **28 bytes**, not 20: Width 0,
Height 4, RefreshRate numerator 8 and denominator 12, Format 16,
ScanlineOrdering 20, Scaling 24. And a mode list read at the wrong stride
reports resolutions nobody offered — it produced a confident claim that the
largest available mode was 730 pixels tall, when the real list held 26 modes
topping out at exactly the panel size.

## Other games

None of these faults is specific to the title it was found on.

The Unreal crash is in `ElectraMediaVPxDecoder`, which is engine code, so any
UE5 title with VP9 cutscenes on D3D12 hits it — same stack, same address,
different offsets. The DXGI node walk is in Unreal's D3D12 RHI, so any UE5 title
on that RHI makes it. The DYNASTY WARRIORS fault is what happens to any game
that decodes video on a D3D11 device and presents it with a D3D12 renderer. The
H.264/NV12 negotiation is CrossOver's behaviour on macOS and not any game's.
Persona 5 Strikers' D3D9-to-D3D11 bridge is the narrowest of the five, and even
there the frame converter came from another title's fix unchanged.

What is specific is the **carrier** — the DLL the fix rides in on:

- `libogg_64.dll` for Unreal titles
- `libxess.dll` for DYNASTY WARRIORS: ORIGINS
- `amd_ags_x64.dll` for Persona 5 Strikers

Adding a game means finding a DLL it loads directly that has nothing to do with
rendering, and building a proxy for it:

```bash
runtime/build-proxy.sh "/path/to/game/<carrier>.dll" dwo-video-bridge.c
```

An Unreal title that ships no `libogg` cannot take the runtime patch at all,
since that is what it rides in on. Another carrier may exist for such a title —
the two non-Unreal games here are reached through vendor libraries — but none
has been found for an Unreal title without one, and
[Diagnosing a new game](Diagnosing-a-new-game.md) describes how a carrier is
chosen.

`diagnostics/survey-games.sh` reports what a game ships and which media API it
uses. Read both halves of that row: the codec says whether anything can decode
it, and the container says whether anything can open it — which, on stable
CrossOver, is where WebM stops.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop.

## Things that do not work

Documented so nobody spends an evening rediscovering them.

- **Registry keys or environment variables for D3DMetal.** Read from the
  D3DMetal shipped with GPTK 4.0 beta 2: no registry keys at all, and 27
  `D3DM_*` environment variables, none relevant. The count will change with the
  next GPTK; the conclusion does not depend on it. The
  `IID_ID3DDestructionNotifier` GUID does not appear in the framework binary at
  all — control GUIDs such as `IID_ID3D12Device` do, so the test is sound. There
  is no switch because the code is not there.

- **GPTK 4.0 beta 2.** Does not fix it. Its `d3d12.dll` does contain the string
  `IID_ID3DDestructionNotifier`, which looks promising — but that DLL carries
  **698** `IID_*` strings (`IID_IAdviseSink`, `IID_IBindCtx`, …). It is a
  generic COM name table used for diagnostic logging, not an implementation.
  Tested in-game: identical crash, same address.

- **A proxy `d3d12.dll`.** On GPTK 3 it is structurally impossible: every export
  is a trampoline (`mov gGFXTDispatch+N, rax ; jmp rax`) into a table the
  D3DMetal core fills at runtime, and only in the module it recognises as
  `d3d12.dll`. GPTK 4 turned those into real functions (`.text` grew from 854 to
  7238 bytes) so a proxy becomes possible in principle — except D3DMetal refuses
  to initialise under any other module name, giving `ERROR_DLL_INIT_FAILED`. The
  same refusal is expected to block moving the D3D11 and D3D12 hooks to the
  CrossOver level the way the DXGI node guard already is: that guard works
  because Apple's `dxgi.dll` tolerates being renamed to `dxgi_real.dll`, and
  `d3d12.dll` is not known to tolerate the same. Expected rather than tried —
  nobody has built that proxy and watched it fail.
  [docs/upstreaming.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/upstreaming.md)
  proposes the move and carries the same caveat.

- **WebMMedia.** Unreal ships a second VP8/VP9 player, wholly independent of
  Electra, and it is compiled into the game. Its factory accepts only the
  `.webm` extension while Unreal asks for `.mp4`, so it never gets a look.
  Remuxing every video to WebM losslessly: game boots, no crash, no video.

- **Patching vkd3d.** vkd3d-proton added `ID3DDestructionNotifier` in v2.14
  precisely for games that expect it, and backporting it onto CrossOver's
  vkd3d 1.18 builds and loads cleanly. It is not on the path — D3D12 is served
  by D3DMetal here. Useful only if your D3D12 goes through vkd3d.

- **DXMT for D3D12.** Its D3D12 exists (`-Denable_d3d12=true`, off by default)
  but is early: 8787 lines against 18621 for its mature D3D11. Built against
  CrossOver, the game does not launch. That is a measured failure at the version
  tested, not a judgement about the project.

### For anyone building DLLs for CrossOver

Wine distinguishes builtin from native DLLs by a 32-byte signature at offset
`0x40` of the DOS stub — the string `Wine builtin DLL`, checked in
`dlls/ntdll/loader.c`. `winebuild` writes it; **llvm-mingw does not**. Without
it Wine will not load your DLL as a builtin, and you will chase phantom errors.
DXMT's meson handles it for you.

## The re-encode mode that was removed

Earlier releases could transcode the cutscenes to H.264 and drop the VP9
originals from the `.pak` index. The runtime patch replaces it completely and is
better on every axis, so that mode has been removed rather than left as a trap:
it took twenty minutes, needed ffmpeg and a gigabyte, softened the picture, and
edited files the game shipped.

If you applied it with an older release, the app still detects it and offers to
undo it, because a patched pak index and a `Movies_VP9_backup` folder cannot be
unwound any other way short of letting Steam re-download the game. Undo it, then
apply the runtime patch. The commands are in the
[README](https://github.com/MathiasKowoll/MacGameVideoFix#undoing-an-older-releases-re-encode).

`pak-hide-videos.py` is pure Python 3 with no dependencies and runs on the
interpreter macOS already ships. The forward direction of both scripts is no
longer offered by the app.

### What the pak patch did

Kept because the undo path still relies on it, and because the format notes are
the only public write-up of this that we know of.

It never touches file data. It rewrites the `FullDirectoryIndex` without the
`.mp4` entries, sets `bReaderHasPathHashIndex = 0` so the engine consults only
the directory index — which avoids having to reimplement UE's path hash —
recomputes the SHA-1s, and **appends** the new index at the end of the file with
an updated footer. Undoing it is a plain truncate back to the original size,
recorded in a small JSON file beside the pak.

It validates the index hash before writing anything, refuses to run twice, and
rejects encrypted indexes and pak versions other than 11.

Once an entry is gone the engine falls through to disk, because
`FPakPlatformFile::IsNonPakFilenameAllowed` does not exclude `.mp4`.

## Further reading

In the repository rather than on the wiki:

- [docs/winevideo-on-preview.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/winevideo-on-preview.md)
  — what a current CrossOver decodes on its own, measured codec by codec, and
  what winevideo is still for.
- [docs/what-we-got-wrong.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/what-we-got-wrong.md)
  — the mistakes made producing these results: claims that reached the wiki and
  were withdrawn, the reading errors that caused them, and the hypotheses that
  did not survive.
- [docs/upstreaming.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/upstreaming.md)
  — what of this could stop being per-game, and what should not be attempted.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md)
