# Games

Titles we have deliberately taken on, and what was measured on each. This is
not an inventory of anyone's library — a game gets a row when there was a
reason to work on it.

Tested on an M4 Max, macOS 27, GPTK 4.0b2, against CrossOver 26.3 and
`crossover-preview-arm64-20260821`. The CrossOver column says which builds each
title was measured on. No bottle carried
[winevideo](https://github.com/Jfishin/winevideo) except where a page below says
otherwise.

<!-- games:begin -->

| Game | Engine | Symptom | Fix | Backend | DX | GPTK | CrossOver | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | 26.3 and Preview | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | 26.3 and Preview | Fixed |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | Console variable puts Electra on its CPU path; **needs winevideo** | D3DMetal | 12 | 3.0 | 26.3 with winevideo -- Preview stalls | Fixed |
| [Persona 5 Strikers](Persona-5-Strikers.md) | Koei Tecmo, in-house | Video never starts; sound only | Staged VC-1 codec, and a D3D9 to D3D11 bridge | **DXMT** | 11 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [Nioh](Nioh.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Staged WMV3 codec, and the same D3D9 to D3D11 bridge | **DXMT** | 11 | 4.0b2 | 26.3 and Preview | Fixed |
| [Nioh 2](Nioh-2.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Same codec and same bridge as Nioh, unchanged | **DXMT** | 11 | 4.0b2 | 26.3 and Preview | Fixed |
| [Nioh 3](Nioh-3.md) | Koei Tecmo, in-house | Failed to play movie | The DYNASTY WARRIORS bridge, unchanged | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [Wo Long: Fallen Dynasty](Wo-Long-Fallen-Dynasty.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | The DYNASTY WARRIORS bridge, unchanged | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | Toylogic, in-house | Crashes when the first video starts | Software decode, and the frame written into the game's target | D3DMetal | 11 | 4.0b2 | 26.3 and Preview | Fixed |
| [KINGDOM HEARTS Dream Drop Distance](Kingdom-Hearts.md) | Square Enix, in-house | Cutscene runs with sound, picture solid green | Software decode, and the luma and chroma planes written into the game's own textures | D3DMetal | 11 + 12 | 4.0b2 | Preview -- not tried on 26.3 | Fixed |
| [KINGDOM HEARTS HD 1.5+2.5 ReMIX](Kingdom-Hearts.md) | Square Enix, in-house | Cutscene runs with sound, picture solid green | The Dream Drop Distance fix, unchanged -- six executables, same route | D3DMetal | 11 + 12 | 4.0b2 | Preview -- not tried on 26.3 | Fixed |
| [TMNT: Splintered Fate](TMNT-Splintered-Fate.md) | Rebirth, in-house | Opens a window, then closes silently | A guard on the D3D12 call that ends the process instead of failing | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [Tormented Souls 2](Tormented-Souls-2.md) | Unreal Engine 5 | Fatal error before the first frame | 16:9 modes added to a list that offered none | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [Devil May Cry 5](RE-Engine-VC1.md) | RE Engine | Crashes when a skill preview video plays | Staged VC-1 codec. Nothing installed beside the game | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [RESIDENT EVIL 2](RE-Engine-VC1.md) | RE Engine | Crashes when a video plays | The same staged VC-1 codec, unchanged | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [RESIDENT EVIL 3](RE-Engine-VC1.md) | RE Engine | Crashes when a video plays | The same staged VC-1 codec, unchanged | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [NINJA GAIDEN 4](Ninja-Gaiden-4.md) | Koei Tecmo, in-house | Says the VP9 codec is missing, then exits | Staged Matroska demuxer, and the MFT gate answered | D3DMetal | 12 | **3.0 only** -- 4.0b2 stalls it | 26.3 only -- Preview stalls before video | Fixed |
| [RESONANCE: A PLAGUE TALE LEGACY](Resonance-A-Plague-Tale-Legacy.md) | Asobo, in-house | Fatal error: Shader Model 6.7 is not supported | Shader model floor lowered in memory; needs a 16:9 display | D3DMetal | 12 | 3.0 | 26.3 | Fixed |

**Update the toolkit, then pick a CrossOver.** Every fix here was written
against Apple's Game Porting Toolkit 4.0b2, which is what CrossOver Preview
ships and what CrossOver 26.3 does **not** -- 26.3 carries D3DMetal 3.0, and on
3.0 these patches do not find what they were written to find. So 26.3 is a
perfectly good engine for all of this once its toolkit is replaced, and a poor
one until then. The app does the replacing, and keeps the original beside it.

The exception is NINJA GAIDEN 4, which is the other way round: it runs on 3.0
and stalls on 4.0b2, before its first frame and for reasons inside the toolkit
that nothing here can reach.

**The GPTK column is the one that decides, and it is newer than this table.**
Apple's Game Porting Toolkit is what actually draws these games, and CrossOver
ships it inside the bundle rather than as something you pick: 26.3 carries
D3DMetal 3.0, Preview 27.0 carries 4.0b2 and uses it unless
`CX_GRAPHICS_BACKEND_VERSION` says otherwise. So "this only works on Preview"
has, for at least two titles here, meant "this needs the newer toolkit" and
nothing about Wine at all.

The two rows in bold are where that stops being a footnote. **NINJA GAIDEN 4
runs on 3.0 and stalls on 4.0b2. Life is Strange runs on 4.0b2 and crashes on
3.0.** Opposite requirements, same machine, so there is no single toolkit that
serves the whole table and no version of CrossOver that is simply "better".
Both were measured by moving the toolkit under a fixed CrossOver, which is the
only way to separate the two.

Everything else in the column is derived rather than freshly run: a title
measured on 26.3 was measured on 3.0, and one measured on Preview was measured
on 4.0b2. A cell naming one generation means the other was never tried, not that
it fails.

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
rest run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and what "Preview" means.** Every measurement here was taken
against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the CrossOver
column says which of the two a title was measured on rather than which it might
work on. **Seventeen of the nineteen run on stable 26.3**, which inverts where
this project started: stable was the exception and is now the rule, and the
toolkit -- not the engine -- turned out to be the axis that decides most of
these titles.

The two exceptions point in opposite directions. The Kingdom Hearts pair has
only ever been launched on Preview, so its rows record an absence rather than a
result and nothing is claimed either way. NINJA GAIDEN 4 is the reverse: it runs
on stock 26.3 and stalls on Preview, and what stalls it is the toolkit, which
executes command lists concurrently with no lever to turn that off.

Two lessons paid for by rows that were wrong for a while, and are worth more
than the statuses they corrected:

- **A staged codec is built against one CrossOver and is not usable under
  another.** Persona 5 Strikers was recorded as not working on 26.3 after a
  first attempt failed there; the codec simply had not been built for 26.3 yet.
- **A fix that reports itself installed is not necessarily loaded.** NieR
  Replicant was recorded as Preview-only because its 26.3 runs died at the first
  video. The bridge was never executing in those runs: the registry override it
  depends on had gone missing, and the installer answered `installed` from the
  files alone.

**None of these games needs CrossOver patched, wherever the container can be
opened.** That was not true when this project started, and it is the single
biggest thing that changed. The qualifier is the whole of what remains, and it
is a container question rather than a codec one.

Both builds decode VP9 the same way, and for a long time what only Preview could
do was **open** a WebM -- which was the whole of the difference. DYNASTY WARRIORS
ships 355 `.webm` cutscenes and could not get as far as decoding on stable, while
Mortal Shell 2 ships the same codec in `.mp4`, which both builds handle. The
plugin-by-plugin comparison that conclusion rested on is in
[Findings](Findings.md), under *The container, not the codec*.

**That gap is now closed, and it was a missing plugin rather than a missing
engine.** Preview ships `libgstmatroska`, for both architectures, and stable
26.3 ships it for neither -- so the difference between the two builds on a WebM
was one plugin the whole time. Staging it beside the decoder gives stable one
too, and NINJA GAIDEN 4 is where that was measured: it plays on stock 26.3,
video and all, with nothing patched into CrossOver.

Several titles need a codec no CrossOver ships -- VC-1, WMV3, WMV2 or WMA -- and
it is staged beside the game rather than patched into it. Which titles, and
which plugin each one needs, is the Codec column of
[what each title actually loads](Games.md#what-each-title-actually-loads); the
count is derived there rather than repeated here, because the number written
here was three for as long as it took two more titles to join the list.
Nioh 3 needs none: its video is already NV12 by the time Media Foundation is
asked for it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.

<!-- games:end -->

## What each title actually loads

The table above says whether a title works. This one says what it is made of:
which DLL the fix rides on, which bridge it was built from, which plugin has to
be staged in front of CrossOver, and whether Wine has to be told to prefer our
file at all.

<!-- stack:begin -->

| Game | Backend | DX | GPTK | Carrier | Kept as | Bridge | Codec | Env levers | Registry |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | D3DMetal | 12 | 3.0 and 4.0b2 | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | D3DMetal | 12 | 4.0b2 | `libxess.dll` | `libxess_real.dll` | `dwo-video-bridge.c` | `libgstmatroska` | — | — |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | D3DMetal | 12 | 3.0 | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Persona 5 Strikers](Persona-5-Strikers.md) | **DXMT** | 11 | 3.0 and 4.0b2 | `amd_ags_x64.dll` | `amd_ags_x64_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [Nioh](Nioh.md) | **DXMT** | 11 | 4.0b2 | `GfeSDK.dll` | `GfeSDK_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [Nioh 2](Nioh-2.md) | **DXMT** | 11 | 4.0b2 | `GfeSDK.dll` | `GfeSDK_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [Nioh 3](Nioh-3.md) | D3DMetal | 12 | 4.0b2 | `amd_ags_x64.dll` | `amd_ags_x64_real.dll` | `dwo-video-bridge.c` | — | — | — |
| [Wo Long: Fallen Dynasty](Wo-Long-Fallen-Dynasty.md) | D3DMetal | 12 | 4.0b2 | `libxess.dll` | `libxess_real.dll` | `dwo-video-bridge.c` | — | — | — |
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | D3DMetal | 11 | 4.0b2 | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | `libgstlibav` | — | yes |
| [KINGDOM HEARTS Dream Drop Distance](Kingdom-Hearts.md) | D3DMetal | 11 + 12 | 4.0b2 | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | — | — | yes |
| [KINGDOM HEARTS HD 1.5+2.5 ReMIX](Kingdom-Hearts.md) | D3DMetal | 11 + 12 | 4.0b2 | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | — | — | yes |
| [TMNT: Splintered Fate](TMNT-Splintered-Fate.md) | D3DMetal | 12 | 4.0b2 | `fmod.dll` | `fmod_real.dll` | `d3d12-guards.c` | — | — | — |
| [Tormented Souls 2](Tormented-Souls-2.md) | D3DMetal | 12 | 4.0b2 | `OpenColorIO_2_3.dll` | `OpenColorIO_2_3_real.dll` | `d3d12-guards.c` | — | — | — |
| [Devil May Cry 5](RE-Engine-VC1.md) | D3DMetal | 12 | 3.0 and 4.0b2 | — | — | — | `libgstlibav` | — | — |
| [RESIDENT EVIL 2](RE-Engine-VC1.md) | D3DMetal | 12 | 3.0 and 4.0b2 | — | — | — | `libgstlibav` | — | — |
| [RESIDENT EVIL 3](RE-Engine-VC1.md) | D3DMetal | 12 | 3.0 and 4.0b2 | — | — | — | `libgstlibav` | — | — |
| [NINJA GAIDEN 4](Ninja-Gaiden-4.md) | D3DMetal | 12 | **3.0 only** -- 4.0b2 stalls it | `dstorage.dll` | `dstorage_real.dll` | `ng4-observe.c` | `libgstmatroska` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER`, `NG4_ANSWER_MFT`, `NG4_CPU_DECOMP`, `NG4_FAKE_OPTIONS17`, `NG4_NO_D3D11_PATCH`, `NG4_PATCH_D3D12`, `NG4_REFUSE_DSTORAGE`, `NG4_SMALL_STAGING` | — |
| [RESONANCE: A PLAGUE TALE LEGACY](Resonance-A-Plague-Tale-Legacy.md) | D3DMetal | 12 | 3.0 | `NvCloth_x64.dll` | `NvCloth_x64_real.dll` | `shader-floor-fix.c` | — | — | — |

**Carrier** is the DLL the fix rides on -- one the game already loads, chosen
because it has nothing to do with video. **Kept as** is what the original is
renamed to; where the carrier is a DLL Wine implements itself, that original is
a copy taken from your own CrossOver and nothing is redistributed. **Bridge** is
the source the shipped proxy was built from: 6 sources serve every title
here, so a fault found in one is often already fixed in the others.

**Registry** says whether Wine has to be told to prefer our DLL. 2 fixes
need it, and for the same reason: their carrier is `dinput8`, which Wine
implements, so without the override the file beside the game is never opened.
That key is the part that goes missing on its own -- a bottle reset, a bottle
made after a CrossOver upgrade -- and a fix whose files are present is not
therefore a fix that is running. Both installers check the registry now, and
answer `broken` rather than `installed` when it is gone.

**Env levers** are the environment variables the shipped DLL reads. They are
levers, not requirements: each one defaults to the setting the fix was measured
with, and they exist so a failing title can be bisected without a rebuild.

**Codec** is the plugin `stage-codecs.sh` must put in front of CrossOver.
7 titles need a decoder no CrossOver ships, 2 need a demuxer,
and telling those two cases apart is most of the work -- see
[How the codec staging works](How-the-codec-staging-works.md). The decoder
column is the same on every build; the demuxer one is not, because Preview
ships `matroska` and stable 26.3 does not, so those rows describe what stable
needs and what Preview already has.

<!-- stack:end -->

## One bottle cannot hold all of these

`CX_GRAPHICS_BACKEND` divides them, and the Backend column above says which is
which. The `dxmt` group because it needs a shared D3D9 surface handle and
D3DMetal has none to give.

Steam libraries are shared between bottles, so giving that one a bottle of its
own costs no disk and no re-download. Switching the backend by hand also works
and means remembering to switch it — and a forgotten backend looks exactly like
a fix that stopped working.

## What each fix needs from CrossOver

The older form of this question was "does a fix need winevideo?", which framed
it as a codec problem. It is not one, and asking it that way produced the wrong
answer for the single title it existed to describe. What separates these titles
is what each fix needs CrossOver to have already done before it can start: open
a container, decode a codec, or neither. The Codec column above answers it per
title. The distinction behind it is the part worth carrying:

- **A demuxer is a container problem.** DYNASTY WARRIORS: ORIGINS and NINJA
  GAIDEN 4 both play WebM, and Media Foundation cannot open one without a
  Matroska demuxer -- `MFCreateSourceReaderFromByteStream` fails at the open
  and nothing reaches a decoder at all. It reports that as
  `MF_E_UNSUPPORTED_BYTESTREAM_TYPE`, which reads like a missing codec and is
  not one. **This one depends on the engine**: Preview ships `matroska` for
  both architectures and stable 26.3 ships it for neither, so on stable the
  plugin has to be staged and on Preview it is already there. The VP9 inside is
  decoded identically by both builds.
- **A decoder is a codec problem.** No CrossOver decodes VC-1, WMV3, WMV2 or
  WMA, and seven titles play video in one of those -- the RE Engine three, the
  two Nioh, Persona 5 Strikers, and NieR Replicant, which joined the list by
  measurement after years of being filed as needing nothing. `libgstlibav` is
  staged beside the game rather than patched into the engine, so those fixes
  depend on nothing CrossOver decodes and behave the same on both builds.
- **Neither, for the rest.** Mortal Shell 2 decodes in-process with Electra's
  own libvpx; Beast of Reincarnation goes through an H.264 decoder both builds
  have; both Life is Strange freezes are inside DXGI and never touch video. One
  qualification on the last of those: the DLL those two install carries all
  three repairs, and its policy table arms only the node guard for them -- but
  the Media Foundation hooks it installs go in for every title regardless, and
  that unconditional instrumentation is the standing suspicion for why those
  two crash on 26.3. The fault has nothing to do with video; the DLL touches
  video anyway.

**On winevideo.** On a current Preview none of these fixes needs it, and the
VP9 and WebM plugins it installs are redundant there. On a stable build it
ships a WebM demuxer, which is the thing DYNASTY WARRIORS has no other way to
get — stable 26.3 carries no `matroska` plugin, read out of the two installs.
That is a conclusion from what each build contains, not a measurement: the
title has never been launched on stable, with winevideo or without it, so no
dependency on it has been measured here. One unbuilt alternative, and the
reasoning behind it, is in [Findings](Findings.md), under *The container, not
the codec*.

## The mechanism they share

Why each hook exists, which vtable slot each one takes, how a carrier DLL is
picked and what was tried and did not work is in [Findings](Findings.md). The
division is deliberate: these pages carry the per-title findings and the wrong
turns that came first, that one carries what is common to all of them.

## Not a video fix: RISE OF THE RONIN

Deliberately absent from the table above, and for a different reason from METAL
GEAR SOLID 4: this title has a video fault, and it is not one we can repair.

**What was fixed, and it is worth more than the title.** The game ran at 7.30
frames a second with 83.50 ms of GPU time per frame, on a scene of 36 draw
calls — a cost that cannot be the scene. The cause is its own setting:

    Documents/KoeiTecmo/Ronin/config.xml
    <RES_SCALING>XESS</RES_SCALING>   ->   OFF

| | XESS | OFF |
|---|---|---|
| FPS | 7.30 | 58.05 |
| GPU per frame | 83.50 ms | 3.51 ms |
| Dispatches | 19 | 2 |

The seventeen missing dispatches are XeSS's compute passes. Intel's upscaler has
no Intel GPU to run on under D3DMetal and falls back to a generic compute path
the translation layer makes ruinous. **It is XeSS in particular, not upscaling:**
DLSS in the same slot costs nothing, because it finds no NVIDIA hardware and
D3DMetal substitutes MetalFX. Any Koei Tecmo title shipping `libxess.dll` is
worth checking this way before the engine is blamed. No DLL, no code, no
install — one value in the game's own config.

**What is not fixed.** The opening cutscene plays its audio over a black screen
for twenty to a hundred seconds, input ignored, and then the menu appears. This
is not specific to macOS. GloriousEggroll/proton-ge-custom issue 165, filed for
this title, reports it word for word — *"wait for shaders to compile. It will
then play the intro video with audio and a black screen"* — and guesses the
format as WebM. It varies by Proton build and **has no identified cause and no
released fix upstream**.

Measured here, with every hook verified to install:

- Five Media Foundation entry points hooked — `MFTEnumEx`, both source readers,
  `IMFMediaEngine` through `CoCreateInstance`, and `MFCreateFile` — and **none
  is ever called**. `MFStartup` runs three times and nothing follows.
- `ID3D12VideoDevice` is never requested; the game asks its D3D12 device for
  `ID3D12Device1`, `ID3D12Device5` and one vendor interface, all granted.
- No NV12 or P010 among 67 distinct texture shapes. There is a 1920x1080
  `B8G8R8A8_TYPELESS` created 2.6 s in, in a game drawing at 2048x1152.
- `libgstmatroska` and `libgstvpx` are staged, the GStreamer registry lists both,
  and the bottle registers byte-stream handlers for `.mkv` and `.webm` — all
  legacy of the NINJA GAIDEN 4 work. Nothing that would normally be missing is.
- During the black the game streams ~6 MB per 15 s from a 691 MB `.file` whose
  header is `IDRK`/`TSRS`/`KTSR` — Koei Tecmo Sound Resource, the soundtrack.
  All fifteen loose resource files are audio; there is no loose video in 149 GB.

So the game does not attempt playback at all here. That matches the state
GE-Proton calls *skipped* rather than the one it calls *black*, and whatever it
probes before deciding sits upstream of every door listed above.

**A negative worth banking:** `sleep-yield-fix` at divisor 64 cuts this title's
`Sleep(0)` storm from eight million per fifteen seconds to a quarter of a
million — a thirtyfold reduction, measured twice — and moves neither the frame
rate nor the length of the black screen. Real waste, no benefit. It is not a fix
for this game and is not shipped as one.

## Not yet an installer: NINJA GAIDEN 3: Razor's Edge

Absent from the table because the fix is not ours to ship yet — it is four
DLL substitutions, three of them other people's work. Written down here because
it is measured, complete, and reproducible.

**Why it is the odd one of the Master Collection.** SIGMA and SIGMA 2 import
`d3d11.dll` and start fine. Razor's Edge is **Direct3D 9 only** — `d3d9.dll`
and `d3dx9_43.dll`, no d3d11 anywhere — and that is the whole reason the other
two work under DXVK and this one would not start at all.

**The message it shows is a red herring.** Under CrossOver's own DXVK it says
*"Insufficient VRAM. Please close all running applications."* Its own log says
what really happened, eleven times: `DxvkAdapter: Failed to create device`,
after reporting `transformFeedback: 0` and `timelineSemaphore: 0`. MoltenVK does
not offer those and that DXVK requires them. Nothing to do with memory — DXVK
reports the machine's full 49152 MiB two lines earlier.

**Its videos are not Media Foundation.** 24 files in `databin/movie`, all ASF
containers, **WMV3** video and **WMAv2** audio at 1280x720, played through
**DirectShow** — `quartz` and `qasf`. An afternoon went into instrumenting
`MFTEnumEx`, both source readers and `MFCreateFile` before the winevideo
developer said where to look. The codec was measured with ffprobe; the path
was not, and the difference cost hours.

**The recipe.** Four overrides, all per-executable under
`HKCU\Software\Wine\AppDefaults\NINJA GAIDEN 3 Razor's Edge.exe\DllOverrides`,
so no other title is affected, with the DLLs placed in
`drive_c/windows/system32`:

| value | `native,builtin` | where it comes from |
| --- | --- | --- |
| `*d3d9` | d9vk | `Sikarugir-App/d9vk`, `d9vk-macOS-async-v1.10.3-20250511` |
| `*qasf` | patched | winevideo 0.5 payload |
| `*quartz` | patched | winevideo 0.5 payload |
| `*winegstreamer` | patched | winevideo 0.5 payload |

That d9vk is the **same DXVK version** CrossOver ships, 1.10.3, built
differently for macOS. "DXVK 1.10.3 does not work on Apple silicon" was written
here first and was wrong: CrossOver's build does not, this one does.

**Result.** The game starts, renders at 60 fps, reaches its menu, and **its
in-game cutscenes play**. The boot movie decodes its first frame and freezes;
one click skips it and the game carries on. Nioh was re-run afterwards and is
unaffected, which matters because these DLLs sit in a shared `system32` and only
the per-app override keeps them out of everything else.

**What is left, and why it may stay left.** Adding winevideo's `winegstreamer`
changed nothing observable. That fits: it is split into a PE half and a Unix
`.so`, and only the PE half can be overridden per application, so the two halves
no longer match. Chasing the boot movie means transplanting more of another
engine into this one. **winevideo 0.5 runs this title properly**, because there
all of these are one coherent build — that is the honest answer for anyone who
wants the intro as well.

**Before this ships**, someone has to decide how the third-party binaries reach
a user: downloaded from their own source with the checksum the winevideo
contract already publishes, or copied out of a winevideo install the user
already has. Redistributing them inside this project's bundle is a different
question from either.

## Withdrawn: METAL GEAR SOLID 4

This title shipped a fix here between 2026-08-27 and 2026-08-28. It has been
removed from the app, the installers and the release, and the page is kept as a
record of how it got here.

**The reading was wrong.** The title showed a black window, a click of audio and
an exit, and the measured explanation was a busy-wait: four and a half million
`Sleep(0)` calls before the menu, Steam's client pipe timing out, and a fatal
assert. Turning every sixty-fourth `Sleep(0)` into a real `Sleep(1)` cut that to
36,160 spins and the title loaded. A control was even run — the fix built to set
only the environment variable, with the wrappers gone, and the title stopped
loading again — which seemed to isolate the yield as the cause.

**What was actually happening.** RaccoonBot was killing games that hand off from
their own launcher: the launcher exits once the game is running, that was read as
the title having finished, and the game went down with it. MGS4 is exactly that
shape — `Launcher/launcher.exe` starts `MGS4/mgs4.exe`. With the launcher fixed
on 2026-08-28, MGS4 reaches its menu **with nothing of ours installed at all**.

The busy-wait is real and the measurement stands. It was never the fault. The
control did not catch it because anything that changes the timing — including a
probe's own overhead — also changes whether a kill lands.

**What this costs elsewhere.** `sleep-yield-fix.c` stays in the tree with no
title to its name. It was measured again on RISE OF THE RONIN, where it cuts a
`Sleep(0)` storm thirtyfold and moves neither the frame rate nor anything the
player sees. Two titles, no benefit either time.

**The rule that came out of it:** a launcher that kills the game underneath it
manufactures a defect that looks exactly like the game's own, and every reading
of "it starts and then dies" taken before 2026-08-28 is suspect until re-run.

## Adding a row

Run the survey on that game's folder, and if it misbehaves, the probe. Both
are in `diagnostics/` and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md).

Paste what they print, and say which CrossOver build printed it. Measurements
are the point of these pages, and a row without one is worse than no row.
