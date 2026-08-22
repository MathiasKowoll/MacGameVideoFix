# How the fixes work

The mechanism behind each of the six fixes: what is actually broken, why the
repair is shaped the way it is, and what was tried and did not work. It assumes
the fix is already installed, or that you are deciding whether to write one of
your own. To get a game playing, start at the
[README](../README.md#quick-start).

Per-title findings, with the wrong turns that came first, are in the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki). This document is
the account of the mechanism the fixes share.

## One DLL, three repairs

The Unreal titles are served by one proxy DLL carrying three unrelated repairs,
and each is asked for by executable name rather than applied wherever its
pattern happens to match:

- the Electra buffer path that crashes Mortal Shell 2,
- the adapter-node walk that freezes both Life is Strange titles,
- the H.264 output negotiation that leaves Beast of Reincarnation
  silent-but-blank.

They share one file because they share one carrier DLL. Separate files would
mean a title could only ever have one of the three.

Which of the three a title gets is a table in `runtime/ue5-media-fix.c`, keyed
by the shipping executable's name:

| Executable | Title | Armed |
| --- | --- | --- |
| `MortalShell2-Win64-Shipping.exe` | Mortal Shell 2 | Electra VPx |
| `Iris-Win64-Shipping.exe` | Life is Strange: Reunion | node guard |
| `Chronos-Win64-Shipping.exe` | Life is Strange: Double Exposure | node guard |
| `BeastOfReincarnation-Win64-Shipping.exe` | Beast of Reincarnation | H.264 / NV12 |
| anything else | an untried Unreal title | all three |

Two things follow from that table, and both matter further down. Neither Life
is Strange title runs the H.264 half: it is present in the file they install
and inert in their process. And an executable the build does not recognise arms
all three rather than a chosen subset, which the log says on startup, so an
unexpected result is traceable to the table rather than mistaken for a
measurement.

The Media Foundation hooks are the exception to the gating. They are installed
for every title, armed or not, because they are how a new one is surveyed: what
the gates decide is whether those hooks change anything, not whether they are
there.

## Mortal Shell 2: the cutscene crash

### The crash

A few seconds after launch, the game dies with:

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000000
...!FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer()
   [Engine\Plugins\Media\ElectraCodecs\...\WindowsElectraDecoderGPUBufferHelpers.h:276]
...!FVideoDecoderVPxElectra::ConvertDecodedImageToNV12orP010()
Crash in runnable thread ElectraPlayer::Video decoder
```

If your crash names a different function, this particular patch is not the one
you want. The same DLL carries two other repairs whose symptom is not a crash at
all — a freeze with no crash log, and a picture that never appears — and both are
described below.

### Root cause

Apple's D3DMetal does not implement `ID3DDestructionNotifier`. Unreal asks for
it and uses the result without checking the HRESULT — line 276 of that header is:

```cpp
TRefCountPtr<ID3DDestructionNotifier> Notifier;
Res = Resource->QueryInterface(__uuidof(ID3DDestructionNotifier), ...);
check(SUCCEEDED(Res));                              // compiled out in Shipping
Res = Notifier->RegisterDestructionCallback(...);   // null vtable deref
```

`QueryInterface` returns `E_NOINTERFACE`, `Notifier` stays null, the `check()`
does not exist in a shipping build, and the next line reads address 0.

### Why only VP9, and only D3D12

Every Electra decoder gates the D3D12 output buffer pool on the same condition.
The difference is who guards it:

| Decoder | Guard | On D3D12 |
|---|---|---|
| H.264 / H.265 | `if (… && CVarElectraWindowsH264UseOldOutputPath == 0)` | set the CVar to `1` and it never touches the pool |
| VPx (VP8/VP9) | none — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)` | always enters it → crash |

So VP9 always hits it on D3D12. Titles that run on D3D11 (Persona 5 Strikers,
for one) never do: the same bug is there and unreachable.

**There is no VPx CVar.** Extracting every string from the shipped executable
turns up only `Electra.Win.H264UseOldOutputPath` and
`Electra.Win.H265UseOldOutputPath`. VP9 on D3D12 has no configuration escape.

### The fix

A small proxy DLL patches Electra in memory as the game starts, so its VPx
decoder takes the same CPU output path that every D3D11 machine already uses.
Your original VP9 cutscenes play, untouched: nothing the game ships is edited,
it takes a second to apply, and D3D12 stays active so you keep PSO
precompilation. (`-dx11` also dodges the crash, but Unreal does not precompile
PSOs on the D3D11 RHI, which means permanent shader-compilation stutter.)

It does not survive Steam's **verify integrity of game files** — that puts
every original back, including the DLL we moved aside. Re-apply afterwards.

### VP9 here never goes through Media Foundation

Which is why this title needs nothing from CrossOver's media stack. Electra
decodes the VP9 in-process with its own bundled libvpx, and only the *output*
conversion was broken — that is what the patch reroutes.

It was measured rather than argued: the game was played through on CrossOver
26.3 carrying no winevideo, and again on CrossOver-winevideo 26.3, the same
version differing only in the GStreamer plugins. The fix worked either way.
`diagnostics/launch-with.sh` is what makes that comparison cheap — bottles with
a build present or absent, without reinstalling anything. Preview was measured
separately.

That paired run was made on this title and no other, which is narrower than
this repository once claimed. The Life is Strange fix is independent of
winevideo for a reason rather than by measurement: the node walk is in DXGI and
has nothing to do with video decoding, so the guard depends on no media plugin
at all. `crossover/install-node-guard.sh` states the same thing in its own
header.

### The re-encode mode that was removed

Earlier releases could transcode the cutscenes to H.264 and drop the VP9
originals from the `.pak` index. The runtime patch replaces it completely and
is better on every axis, so that mode has been removed rather than left as a
trap: it took twenty minutes, needed ffmpeg and a gigabyte, softened the
picture, and edited files the game shipped.

**If you applied it with an older release, the app still detects it and offers
to undo it**, because a patched pak index and a `Movies_VP9_backup` folder
cannot be unwound any other way short of letting Steam re-download the game.
Undo it, then apply the runtime patch.

By hand, both steps are no-ops on a game that never had it applied:

```bash
scripts/pak-hide-videos.py ".../Content/Paks/pakchunk0-Windows.pak" --restore
scripts/transcode-movies.sh "/path/to/<Game>/Content" --restore
```

`pak-hide-videos.py` is pure Python 3 with no dependencies and runs on the
interpreter macOS already ships. The forward direction of both scripts is no
longer offered by the app.

### What the pak patch did

Kept because the undo path still relies on it, and because the format notes are
the only public write-up of this that we know of.

It never touches file data. It rewrites the `FullDirectoryIndex` without the
`.mp4` entries, sets `bReaderHasPathHashIndex = 0` so the engine consults only
the directory index (which avoids having to reimplement UE's path hash),
recomputes the SHA-1s, and **appends** the new index at the end of the file with
an updated footer. Undoing it is a plain truncate back to the original size,
recorded in a small JSON file beside the pak.

It validates the index hash before writing anything, refuses to run twice, and
rejects encrypted indexes and pak versions other than 11.

Once an entry is gone the engine falls through to disk, because
`FPakPlatformFile::IsNonPakFilenameAllowed` does not exclude `.mp4`.

## Beast of Reincarnation: sound, and no picture

Three faults in a row, each hiding the next. The full account, including the
four attempts that failed first, is on the
[wiki page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation).
The shape of it:

**CrossOver hides NV12, and Electra accepts nothing else.**
`transform_GetOutputAvailableType` in CrossOver's `winegstreamer` skips NV12
whenever it detects macOS, and Electra's H.264 decoder walks the offered list
looking for NV12, does not find it, and destroys the decoder. The censoring is
only in the getter: `SetOutputType` validates against an array that still
contains NV12 and carries no macOS check, so handing NV12 back by name is
honoured and the negotiation completes. That is the H.264 half of the runtime
patch.

**Electra asks itself, not the decoder, whether it is in software.** So
withholding the D3D manager from the decoder — the obvious move, and the one
tried first — could never have worked. Two calls to `IsSoftware()` are made to
return true.

**The same gate decides the frame height.** Patch one of those two call sites
and not the other and the renderer is handed a luma-only picture. Both go, or
neither.

## Life is Strange: Reunion and Double Exposure

A different fault entirely, in the same DLL. The game runs correctly and then
freezes — after a while, anywhere, with no crash and nothing in any log.

**It is not a deadlock.** A spindump taken while it was stuck shows the
GameThread burning 1.27 seconds of CPU across 128 samples while RenderThread 0
used four milliseconds. One thread pinned, everything else starving behind it.

Unreal walks the adapter's memory nodes through
`IDXGIAdapter3::QueryVideoMemoryInfo`, accumulating across them, and ends the
walk when the call fails. On Windows it fails once the index passes the number
of nodes. **D3DMetal answers `S_OK` for every index**, so the counter climbs
forever — on an M4 Max, in the run the spindump was taken during, at two hundred
million iterations a second.

Refusing node 1 with `DXGI_ERROR_INVALID_CALL` ends it. The refusal fires
**once** per session: Unreal takes the node count from that answer and never
asks again, and in that same run the polling rate settled at around 2,500 a
second.

The full write-up, including the disassembly of the loop and the three wrong
turns that came first, is on the wiki:
[Reunion](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) ·
[Double Exposure](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure).

**This one is not about these games.** The walk is in Unreal's D3D12 RHI rather
than in anything either title added, so the guard is worth trying on any Unreal
game that freezes this way. Two executables have been scanned and both carry the
loop; that is the whole sample.

### Where the guard can live

Both places, and the choice is a trade rather than a conclusion.

The **per-game** fix is the default, because patching one game's process has a
much smaller blast radius than replacing a DLL every bottle loads.

A **CrossOver-wide** override also exists: `crossover/install-node-guard.sh`
replaces Apple's `dxgi.dll` inside a CrossOver build with a proxy that forwards
all seven exports and corrects the one call. It reaches every game in every
bottle using that CrossOver without anybody selecting a folder, at the cost of
an invalidated code signature and a much larger blast radius. The install
commands are in the [README](../README.md#the-crossover-wide-node-guard).

### The 100 ms cache, which is not in the fix

A separate result came out of the same work: serving repeat queries from a
100 ms cache was reported as feeling smoother. No frame time was captured
before or after, so that is a report rather than a measurement — and the cache
was not retained. The shipping guard holds nothing between calls: it refuses
any node other than zero and passes node zero straight through to the original.

The reasoning is written down anyway, because it stands on its own — thousands
of crossings a second into Wine's unix side cost more than their wall time,
which is contention rather than cycles. Anyone bringing it back should capture
a frame time first, since that is exactly what the original report lacked.

### The open defect on 26.3

Both Life is Strange titles crash on stable CrossOver 26.3 and run on Preview.
The crash is ours, not the engine's.

What is known is what those two actually run. The policy table arms the node
guard for `Iris` and `Chronos` and nothing else, so the H.264 half — the NV12
restore described under Beast of Reincarnation above — sits in the DLL they
install and is switched off in their process. It cannot crash them by putting a
format back on a menu, because for them it puts nothing back.

What is not switched off is the instrumentation. `GetProcAddress` is interposed
from `DllMain`, the Media Foundation entry points are replaced through it,
`MFTEnumEx` patches the activate object it hands back, and the decoder's vtable
slots are patched the moment one is created. That happens for these two titles
on both builds, gates or no gates, and it is the candidate cause — a candidate,
not a finding: nothing measured yet names it.

The cheap test nobody has run is the obvious one: those two on 26.3 with the
Media Foundation hooks compiled out and the node guard left in.

Beast of Reincarnation is the title whose whole fix is the H.264 half, armed,
and it is measured working on 26.3 — its own page records the NV12 censoring as
present in the shipping binary of both builds. So the observation is firm and
the cause is open. Until it is settled, use Preview for those two.

## DYNASTY WARRIORS: ORIGINS

The game decodes VP9 with Media Foundation on a D3D11 device kept only for
video, and draws with a D3D12 renderer. Five things stop that under D3DMetal,
and each hides the next:

1. `ID3D11VideoDevice` and `ID3D11VideoContext` are not implemented, and the
   player refuses to start without both.
2. It asks the source reader to decode into D3D video textures, which
   D3DMetal cannot produce.
3. It requires a video processor advertising `DEINTERLACE_BOB`, and returns
   `E_FAIL` when none does.
4. It can only present a sample backed by a D3D texture — it queries the
   buffer for `IMFDXGIBuffer` and has no path for anything else.
5. It hands that texture to its D3D12 renderer by shared handle, and
   `IDXGIResource::GetSharedHandle` returns **`E_NOTIMPL`**. Nothing written
   on the D3D11 side can be seen by D3D12, so the video quad samples a texture
   nobody ever wrote.

The fix supplies the interfaces, moves decoding to software, and carries the
frame across the gap itself: a handle of ours where D3DMetal refuses,
recognised again at `ID3D12Device::OpenSharedHandle`, answered with a texture
created on the game's own D3D12 device and filled each frame over a copy queue
with a fence wait.

It never modifies the game's code. One DLL is renamed and one is added:

```
libxess.dll       <- the bridge, forwards all 27 exports to the original
libxess_real.dll  <- the game's own, untouched
```

`libxess` is Intel's XeSS upscaler. It carries the fix because the game loads
it directly and it has nothing to do with video, so a proxy in front of it
cannot disturb what it does.

The [wiki page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins)
has the full account, including the eight hypotheses that were wrong on the way
there and the three conclusions that had to be withdrawn.

### The container, not the codec

This is the one title here that still needs something from CrossOver's media
stack before its fix can do anything. What it needs is narrower than it looked
for a long time.

Established by comparing the two installs plugin by plugin on this machine,
rather than by running anything on stable: CrossOver 26.3 carries 17 GStreamer
plugins and Preview 19, and the two Preview has to itself are `matroska` and
`osxaudio`.

`matroska` is a demuxer, not a decoder, and it is the whole difference. Both
builds carry `applemedia` and decode VP9 through VideoToolbox; neither ships
`libgstvpx` or `libgstlibav`. What only Preview can do is open a WebM container.
This title's 355 cutscenes are `.webm`, so on stable nothing gets as far as a
decoder.

Mortal Shell 2 is not the control it looks like, and it is worth saying why.
Its 61 cutscenes are the same codec in a different box, and it plays on stable
— but its VP9 never reaches CrossOver's media stack at all. Electra opens the
`.mp4` and decodes it in-process with its own libvpx, which is the finding
recorded above. So it is evidence that VP9 is not what stops this title, and no
evidence at all about which containers CrossOver can open. The plugin-set
comparison is the whole of that.

It also closes a question this repository carried open for a while. The cutscene
was measured playing on Preview in a bottle winevideo had never touched, with
the `.webm` byte-stream handler registered — a run that was expected to fail and
did not, with no account of how the WebM was being opened. The `matroska` plugin
is the account. Whether that handler mapping was needed in that run is a
separate question and still open; the
[title's page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins)
says what was and was not tried.

That is also what reconciles the two statements the wiki has carried at
different times. "None of these games needs CrossOver patched" holds wherever
the container can be opened; "winevideo, not optional" was true of a build with
no WebM demuxer. Reconcile them on the container rather than on the codec, and
on the plugin sets rather than on any pair of titles.

It also narrows what the fix requires. The bridge presents frames and decodes
nothing, so the open has to succeed first: on a build with no WebM demuxer,
`MFCreateSourceReaderFromByteStream` on a `.webm` fails outright and there is
never a frame to carry. [winevideo](https://github.com/Jfishin/winevideo)
supplies a demuxer today. So would staging `libgstmatroska.dylib` — 756 KB, in
the official GStreamer.framework, the same re-homing move that stages VC-1 for
Persona 5 Strikers. That has not been built, and is written here as a plugin to
stage rather than an engine to patch, not as something that works.

## Persona 5 Strikers

The one title needing a codec CrossOver does not ship, and the one that runs on
a different backend.

Its cutscenes are **VC-1 in ASF**, measured by header scan of all five
`data/pd/movie*.bin` archives rather than assumed — the obvious guess was VP9
and it was wrong. Preview decodes VP9, H.264 and AAC on its own and has no VC-1
decoder at all — measured codec by codec on Preview, in
[docs/winevideo-on-preview.md](winevideo-on-preview.md) — so the decoder is
staged beside the game out of the official GStreamer framework. See
[the staged codecs](#the-staged-codecs-and-where-they-come-from).

With the codec in, the reader opened the ASF, agreed types on video and audio,
and never called `ReadSample`. Wine's D3D9 does not fail
`CreateRenderTarget(SHARED)`; it returns `S_OK` with a **null handle**, so the
game believed it had succeeded and gave up quietly later. Handing back a
genuine shared handle from a DXMT D3D11 texture is what makes it start reading —
`GetSharedHandle` appears 17 times in DXMT's `d3d11.dll` and not once in Wine's,
which is why this title is fixable on DXMT and on nothing else.

The bridge rides on `amd_ags_x64.dll`, 38 exports forwarded, which the game
imports and barely uses under CrossOver — the same role `libxess` plays for
DYNASTY WARRIORS. The NV12-to-BGRA converter came across from that bridge
unchanged, which is the clearest evidence so far that the logic here is not
title-specific and only the carrier is.

The five things that had to line up, each measured rather than reasoned, are on
the [wiki page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers).

## Getting a fix into the process

### The runtime patch

Electra decides whether to use the D3D12 buffer pool by comparing the D3D
version against 12000 — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)`.
The compiler turns that into:

```asm
cmp dword [rbp+disp], 12000     ; 81 7D xx E0 2E 00 00
jl  <cpu path>                  ; 7C xx   or   0F 8C xx xx xx xx
```

We raise the 12000 to `INT_MAX`, so the comparison always takes the CPU branch.
Four bytes per site, four sites in this build, each confirmed to be reached at
runtime rather than assumed from the disassembly. Nothing else is touched: the
decoder still decodes VP9 with its own libvpx, and Unreal presents the frames
the way it does on any D3D11 machine.

The compare has to be against a **stack slot** (`81 7D` / `81 BD`). The H.264
and H.265 decoders compare a register instead, and they already have a way out
through `Electra.Win.H264UseOldOutputPath` — leaving them alone keeps this
change to the one decoder that has no other option.

It is a pattern scan, not a table of offsets, because a game update moves
everything: between two builds of Mortal Shell 2 the crash site alone shifted by
`0x2C70`. If the pattern does not match, nothing is written and the log says so.

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

## Other games

None of these faults is specific to the title it was found on.

The Unreal crash is in `ElectraMediaVPxDecoder`, which is engine code, so any
UE5 title with VP9 cutscenes on D3D12 hits it — same stack, same address,
different offsets. The DXGI node walk is in Unreal's D3D12 RHI, so any UE5
title on that RHI makes it. The DYNASTY WARRIORS fault is what happens to *any*
game that decodes video on a D3D11 device and presents it with a D3D12
renderer, because `GetSharedHandle` is `E_NOTIMPL` under D3DMetal for all of
them. The H.264/NV12 negotiation is CrossOver's behaviour on macOS and not any
game's. Persona 5 Strikers' D3D9-to-D3D11 bridge is the narrowest of the five,
and even there the frame converter came from another title's fix unchanged.

What is specific is the **carrier** — the DLL the fix rides in on:

- `libogg_64.dll` for Unreal titles
- `libxess.dll` for DYNASTY WARRIORS: ORIGINS
- `amd_ags_x64.dll` for Persona 5 Strikers

Adding a game means finding a DLL it loads directly that has nothing to do with
rendering, and building a proxy for it:

```bash
runtime/build-proxy.sh "/path/to/game/<carrier>.dll" dwo-video-bridge.c
```

`diagnostics/survey-games.sh` reports what a game ships and which media API it
uses. Read both halves of that row: the codec says whether anything can decode
it, and the container says whether anything can open it — which, on stable
CrossOver, is where WebM stops.
[Diagnosing a new game](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
runs through the tools in order.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop.

## Things that do not work

Documented so nobody spends an evening rediscovering them.

- **Registry keys or environment variables for D3DMetal.** Read from the
  D3DMetal shipped with GPTK 4.0 beta 2: no registry keys at all, and 27
  `D3DM_*` environment variables, none relevant. The count will change with the
  next GPTK; the conclusion does not depend on it. The
  `IID_ID3DDestructionNotifier` GUID does not appear in the framework binary at
  all (control GUIDs such as `IID_ID3D12Device` do, so the test is sound).
  There is no switch because the code is not there.

- **GPTK 4.0 beta 2.** Does not fix it. Its `d3d12.dll` does contain the string
  `IID_ID3DDestructionNotifier`, which looks promising — but that DLL carries
  **698** `IID_*` strings (`IID_IAdviseSink`, `IID_IBindCtx`, …). It is a generic
  COM name table used for diagnostic logging, not an implementation. Tested
  in-game: identical crash, same address.

- **A proxy `d3d12.dll`.** On GPTK 3 it is structurally impossible: every export
  is a trampoline (`mov gGFXTDispatch+N, rax ; jmp rax`) into a table the
  D3DMetal core fills at runtime, and only in the module it recognises as
  `d3d12.dll`. GPTK 4 turned those into real functions (`.text` grew from 854 to
  7238 bytes) so a proxy becomes possible in principle — except D3DMetal refuses
  to initialise under any other module name, giving `ERROR_DLL_INIT_FAILED`.
  The same refusal is expected to block moving the D3D11 and D3D12 hooks to the
  CrossOver level the way the DXGI node guard already is: that guard works
  because Apple's `dxgi.dll` tolerates being renamed to `dxgi_real.dll`, and
  `d3d12.dll` is not known to tolerate the same. Expected rather than tried —
  nobody has built that proxy and watched it fail.
  [docs/upstreaming.md](upstreaming.md) proposes the move and carries the same
  caveat.

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
  CrossOver, the game does not launch. Worth revisiting as it matures — being
  open source, it is the one path where this could be fixed properly for
  everyone rather than per title.

### For anyone building DLLs for CrossOver

Wine distinguishes builtin from native DLLs by a 32-byte signature at offset
`0x40` of the DOS stub — the string `Wine builtin DLL`, checked in
`dlls/ntdll/loader.c`. `winebuild` writes it; **llvm-mingw does not**. Without
it Wine will not load your DLL as a builtin, and you will chase phantom errors.
DXMT's meson handles it for you.

## The staged codecs, and where they come from

Persona 5 Strikers needs a VC-1 decoder CrossOver does not ship. **We do not
distribute one.** `runtime/stage-codecs.sh` borrows it from the official
GStreamer install described under
[Requirements](../README.md#requirements).

`libgstlibav` in that package carries ffmpeg. VC-1 is what this title needs and
what was measured, by the game playing — on Preview, through the `x86_64`
staging a `WineArch=win64` bottle selects. WMV3 and WMA are in the same plugin
and are expected to work; neither has been exercised here, because ffmpeg ships
no encoder for either and no test bitstream could be made.

**1.24.14 is the version to install**, because it is the one this was verified
with. Others may well be fine — winevideo names 1.24.13 for the same titles —
and the requirement is likely to be the 1.24 series rather than that exact
release, since the plugin only has to be ABI-compatible with the CrossOver
GStreamer core it is re-homed onto. No other release has been tried, so that
last part is an inference from ABI policy and not a measurement.
`stage-codecs.sh` prints the version it finds and says so when it is outside
1.24, reporting rather than refusing: turning away something that might work is
as unhelpful as staying quiet about something that might not.

### Why it is re-homed rather than loaded in place

Loading the framework's plugin where it sits crashes. dyld ends up with two
copies of libgstreamer and two GObject type registries, and Preview ships no
`gst-plugin-scanner`, so there is no forked scanner to absorb it. Preview is
also the only build this staging has been exercised on, though the script
stages per engine and would run against a 26.3 install:

```
objc: Class GstCocoaApplicationDelegate is implemented in both
      <CrossOver>/libgstreamer-1.0.0.dylib and /Library/Frameworks/…
```

The script copies that one plugin and ffmpeg into a directory of its own,
symlinks the GStreamer core to **CrossOver's** copy, and points the bottle at it
with `GST_PLUGIN_PATH`. One core, one registry, and the decoders registered.
Layout matters and cost a first attempt: `GST_PLUGIN_PATH` names a directory
GStreamer scans and tries to load everything in as a plugin, so the support
libraries sit one level out, where the plugin's own `@loader_path/../lib` finds
them and the scanner never looks.

The bottle setting survives because the launcher sets only
`GST_PLUGIN_SYSTEM_PATH` and never touches `GST_PLUGIN_PATH`, and the bottle's
environment is applied first. That was read out of Preview's `bin/wine` and has
not been checked on another build, so the staging assumes it holds across
CrossOver releases rather than knowing it does. Every installed CrossOver gets its own staging
directory, keyed by the engine's `CFBundleVersion` — the same string a bottle
records as its Version, so a bottle can be matched to the staging it needs
without guessing.

Nothing is patched and nothing is redistributed.

**The idea is not ours.** [winevideo](https://github.com/Jfishin/winevideo)
does this too — its README describes importing WMV/VC-1 codecs from the user's
official GStreamer install, and it requires GStreamer 1.24.13 for exactly the
titles that need them. What differs is only the mechanism: winevideo patches
the CrossOver installation to make those plugins loadable, and this reaches the
same end with one staged folder and one line of bottle configuration.

## Further reading

- [docs/winevideo-on-preview.md](winevideo-on-preview.md) — what a current
  CrossOver decodes on its own, measured codec by codec, and what winevideo is
  still for.
- [docs/upstreaming.md](upstreaming.md) — what of this could stop being
  per-game, and what should not be attempted.
- [docs/ninja-gaiden-4.md](ninja-gaiden-4.md) — a title that is not fixed, and
  the precise point at which it stops. Out of scope, and recorded because the
  negative result is exact.
