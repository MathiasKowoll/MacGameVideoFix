# Games

Titles we have deliberately taken on, and what was measured on each. This is
not an inventory of anyone's library — a game gets a row when there was a
reason to work on it.

Tested on an M4 Max, macOS 27, against stable CrossOver 26.3 — copies of it
patched by this project. The CrossOver column has one answer on every row, the
GPTK column which toolkit, and the Motor column what the engine itself had to
carry — a cell reading **Stock** means a CrossOver as CodeWeavers shipped it,
and the rows that name [winevideo](https://github.com/Jfishin/winevideo) or an
engine of ours are the ones where that was not enough.

<!-- games:begin -->

| Game | Engine | Symptom | Fix | Backend | DX | GPTK | Motor | CrossOver | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [METAL GEAR SOLID: Peace Walker](Metal-Gear-Solid-Peace-Walker.md) | Konami, Master Collection | Dies the moment a pre-rendered cutscene starts | Engine patch mgvf-0001: 2D-capable buffers from the media source | D3DMetal | 11 | not measured -- the engine was the variable | **Ours** | 26.3, our winegstreamer | Fixed -- crash cured; a green band in some cutscenes is unexplained |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | D3DMetal | 12 | not measured | Stock | 26.3, our winegstreamer | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | Stock | 26.3, our winegstreamer | Runaway node walk stopped; no freeze-free session recorded |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | Stock | 26.3, our winegstreamer | Guard installs; the freeze was never reproduced or cured |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | D3DMetal | 12 | 4.0b2 | Stock&dagger; | 26.3, our winegstreamer | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | Console variable, and two IsSoftware call sites patched by address; **needs winevideo** | D3DMetal | 12 | not measured | winevideo | 26.3, our winegstreamer | Fixed |
| [Persona 5 Strikers](Persona-5-Strikers.md) | Koei Tecmo, in-house | Video never starts; sound only | Staged VC-1 codec, and a D3D9 to D3D11 bridge | **DXMT** | 11 | not measured | Stock&dagger; | 26.3, our winegstreamer | Fixed |
| [NINJA GAIDEN 3: Razor's Edge](Ninja-Gaiden-3-Razors-Edge.md) | Koei Tecmo, in-house | Will not start: "Insufficient VRAM" | d9vk, and winevideo's DirectShow filters | **DXVK** | 9 | not measured | Stock | 26.3, our winegstreamer | Starts, 60 fps, in-game cutscenes; the boot movie freezes, one click skips it |
| [Nioh](Nioh.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Staged WMV3 codec, and the same D3D9 to D3D11 bridge | **DXMT** | 11 | 4.0b2 | Stock&dagger; | 26.3, our winegstreamer | Fixed |
| [Nioh 2](Nioh-2.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Same codec and same bridge as Nioh, unchanged | **DXMT** | 11 | 4.0b2 | Stock&dagger; | 26.3, our winegstreamer | Fixed |
| [Nioh 3](Nioh-3.md) | Koei Tecmo, in-house | Failed to play movie | The DYNASTY WARRIORS bridge, with ordinal hooking added for this title | D3DMetal | 12 | 4.0b2 | Stock&dagger; | 26.3, our winegstreamer | Fixed |
| [Wo Long: Fallen Dynasty](Wo-Long-Fallen-Dynasty.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | The DYNASTY WARRIORS bridge, with ordinal hooking | D3DMetal | 12 | 4.0b2 | Stock&dagger; | 26.3, our winegstreamer | Bridge installs; picture measured only on a patched engine |
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | Toylogic, in-house | Crashes when the first video starts | Software decode, and the frame written into the game's target | D3DMetal | 11 | 4.0b2 | Stock | 26.3, our winegstreamer | Fixed |
| [KINGDOM HEARTS Dream Drop Distance](Kingdom-Hearts.md) | Square Enix, in-house | Cutscene runs with sound, picture solid green; a crash dialog on leaving | Software decode with the planes written into the game's own textures, and the shutdown fault swallowed | D3DMetal | 11 + 12 | not measured | Stock&dagger; | 26.3, our winegstreamer | Picture restored; the exit dialog was measured on 1.5+2.5 |
| [KINGDOM HEARTS 0.2 Birth by Sleep](Kingdom-Hearts.md) | Unreal Engine 4 | Would not start from a launcher; ran fine launched by hand | **None from us.** The launcher had to declare microphone use -- see the page | D3DMetal | 11 | not measured | None | 26.3, our winegstreamer | Works with nothing of ours -- fixed in the launcher |
| [KINGDOM HEARTS HD 1.5+2.5 ReMIX](Kingdom-Hearts.md) | Square Enix, in-house | Cutscene runs with sound, picture solid green; a crash dialog on leaving | The Dream Drop Distance fix, unchanged -- six executables, same route | D3DMetal | 11 + 12 | not measured | Stock | 26.3, our winegstreamer | Fixed |
| [TMNT: Splintered Fate](TMNT-Splintered-Fate.md) | Rebirth, in-house | Opens a window, then closes silently | A guard on the D3D12 call that ends the process instead of failing | D3DMetal | 12 | 4.0b2 | Stock | 26.3, our winegstreamer | Fixed |
| [Tormented Souls 2](Tormented-Souls-2.md) | Unreal Engine 5 | Fatal error before the first frame | 16:9 modes added to a list that offered none | D3DMetal | 12 | 4.0b2 | Stock | 26.3, our winegstreamer | Fixed |
| [Devil May Cry 5](RE-Engine-VC1.md) | RE Engine | Crashes when a skill preview video plays | Staged VC-1 codec. Nothing installed beside the game | D3DMetal | 12 | not measured | Stock | 26.3, our winegstreamer | Fixed |
| [RESIDENT EVIL 2](RE-Engine-VC1.md) | RE Engine | Crashes when a video plays | The same staged VC-1 codec, unchanged | D3DMetal | 12 | not measured | Stock | 26.3, our winegstreamer | Fixed |
| [RESIDENT EVIL 3](RE-Engine-VC1.md) | RE Engine | Crashes when a video plays | The same staged VC-1 codec, unchanged | D3DMetal | 12 | not measured | Stock | 26.3, our winegstreamer | Fixed |
| [NINJA GAIDEN 4](Ninja-Gaiden-4.md) | Koei Tecmo, in-house | Says the VP9 codec is missing, then exits | Staged Matroska demuxer, and the MFT gate answered | D3DMetal | 12 | **3.0 only** -- on 4.0b2 the video has sound and no picture | Stock | 26.3, our winegstreamer | Fixed |
| [RESONANCE: A PLAGUE TALE LEGACY](Resonance-A-Plague-Tale-Legacy.md) | Asobo, in-house | Fatal error: Shader Model 6.7 is not supported | Shader model floor lowered in memory; needs a 16:9 display | D3DMetal | 12 | not measured | Stock | 26.3, our winegstreamer | Starts on a 16:9 display -- its cutscenes have never been visible |

**One engine, and update its toolkit.** The supported engine is stable
CrossOver 26.3.0.39832 and nothing else: the winegstreamer pair this project
ships was built against that engine, and `install-engine-media.sh` refuses any
other version rather than installing onto a wine it was not compiled for. Every
fix here was written against Apple's Game Porting Toolkit 4.0b2, which 26.3 does
**not** ship -- it carries D3DMetal 3.0, and on 3.0 these patches do not find
what they were written to find. So 26.3 is a perfectly good engine for all of
this once its toolkit is replaced, and a poor one until then. The app does the
replacing in a copy of the CrossOver you point it at, and keeps both halves of
the original inside that copy, so on that route the CrossOver you installed is
not touched. With the copy turned off in Set up, a toolkit you ask for goes into
the CrossOver you installed instead, and the app says so where that choice is
made.

The exception is NINJA GAIDEN 4, which is the other way round: it is measured
working on 3.0, and on 4.0b2 its cutscene plays its audio and no picture
appears. It does not stall there and it does not exit; where the frames stop
has not been established.

**The GPTK column is the one that decides.** Apple's Game Porting Toolkit is
what actually draws these games, and CrossOver ships it inside the bundle
rather than as something you pick: 26.3 carries D3DMetal 3.0. A launcher can
put another generation in front of it -- RaccoonBot carries d3dMetal3 and
d3dMetal4 side by side and injects one at launch -- and that is how the rows
needing 4.0b2 run on this engine. So "this only works on the Preview build"
meant, for three of the rows here, "this needs the newer toolkit" and nothing
about Wine at all -- which is why dropping Preview costs the table nothing.

Those three rows in bold are where that stops being a footnote, and they fall
into two camps pointing opposite ways. **NINJA GAIDEN 4 is measured working on
3.0, and on 4.0b2 its video has sound and no picture. Life is Strange -- both
packages -- runs on 4.0b2 and crashes on 3.0.** Opposite requirements, same
machine, so there is no single toolkit that serves the whole table. Both were
measured by moving the toolkit under a fixed CrossOver, which is the only way
to separate the two.

**Everywhere else the column says "not measured", and that is deliberate.**
4.0b2 is what these titles need as a general rule, with NINJA GAIDEN 4 the
exception at 3.0 -- but a rule is not a run, and a row whose toolkit nobody
varied has no measurement of its own to report. The cells that used to hold one
were derived from the CrossOver build rather than run, and that derivation is
gone: a launcher chooses the generation, so the build implies nothing about it.

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
rest run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and one answer for every row.** Every measurement here was
taken on stable CrossOver 26.3, on an engine carrying this project's
winegstreamer, and the column says exactly that on every row because it is the
same answer on every row. Both halves of it are load-bearing: the version is
stable 26.3, and the engine carries this project's winegstreamer rather than
the one CodeWeavers ships. What a fix needs an engine to carry is a different
question, and the Motor column answers it per row.

**CrossOver Preview is no longer a supported engine.** It was measured against
here, and on 2026-08-31 it was dropped rather than half-supported: only titles
patched without winegstreamer could have worked on it, and the rest were out of
the equation. The findings below that mention it are kept, because they record
what was measured and a deleted measurement is not a correction. They are
history. None of them is guidance about where to run a game.

**The KINGDOM HEARTS 2.8 package holds three entries, not two** -- Dream Drop
Distance, 0.2 Birth by Sleep and the Back Cover film -- and they do not share a
fault. Dream Drop Distance needs the bridge and has it. 0.2 needs nothing at
all: no installer here has ever covered it, its executable lives in a subfolder
this project does not reach, and launched directly it runs and plays its video,
four times out of four.

**0.2 would not start from a launcher because the launcher declared no
microphone use.** Steam initialises voice detection at startup; without
`NSMicrophoneUsageDescription` macOS can neither prompt nor grant, the request
never resolves, and Steam's own main loop wedges -- its assertion says so. A
stalled Steam never answers the second request this package makes of it, which
is why no ordinary title showed the same fault. Declaring the permission fixed
it, and fixes any title that touches the microphone.

Its status says Fixed because this column answers "does the title work", and it
does. Its **Fix** column says "none needed", and that is the half to read before
concluding anything was shipped for it: nothing was. Every other Fixed row in
this table names something this project installs; this one names nothing.

**History: NINJA GAIDEN 4 and Beast of Reincarnation were both measured
stalling on Preview**, and both were recorded working on 26.3, which is the
opposite direction to the one this project expected. NINJA GAIDEN 4 stalled
there before any video call, with no thread in it touching D3D12, DXGI, Media
Foundation or winegstreamer, and what held it was never established. Beast of
Reincarnation stalled there as well, and carries a separate requirement of its
own: it needs winevideo since the game update of 2026-08-24. The shared lesson
is that "runs on Preview" was never the safe assumption this project began
with.

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

**The Motor column says what the engine itself has to carry, and for most rows
it is nothing.** Those ask nothing of ours from the engine: the fix sits beside
the game and, where the video needs one, a plugin goes in front of it. That was
not true when this project started, and it is the single biggest thing that
changed. The exceptions are the rows that name something instead: Beast of
Reincarnation needs a winegstreamer carrying winevideo's patches, and METAL GEAR
SOLID: Peace Walker is the first title here whose whole fix is an engine patch
of ours, with nothing installed beside the game at all -- it was tried on a
stable 26.3 as CodeWeavers ships it and could not play its cutscenes there,
which is the measurement that puts **Ours** in its cell.

**A dagger on "Stock" means inferred rather than run.** No run on those rows
isolated the engine's own `winegstreamer` as the thing that carried the video,
so Stock there is read off the mechanism rather than established. That is not a
smaller claim than Stock; it is an untested one, and the column says which rows
it applies to. It is not a statement about the CrossOver cell beside it. That
cell records what the runs were made on -- stable 26.3 with this project's
winegstreamer in the engine, on every row -- and Motor records what a fix needs
an engine to carry. A row reading Stock in one and our winegstreamer in the
other is not two claims in contradiction: the runs were made on the engine this
project supports, and Stock says the fix does not depend on it.

**History, and the finding that closed a gap.** Both builds decoded VP9 the
same way, and for a long time what only Preview could do was **open** a WebM --
which was the whole of the difference. DYNASTY WARRIORS ships 355 `.webm`
cutscenes and could not get as far as decoding on stable, while Mortal Shell 2
ships the same codec in `.mp4`, which both builds handled. The plugin-by-plugin
comparison that conclusion rested on is in [Findings](Findings.md), under *The
container, not the codec*.

**That gap is closed, and it was a missing plugin rather than a missing
engine.** Stable 26.3 ships `libgstmatroska` for neither architecture and the
Preview build shipped it for both -- so the difference between them on a WebM
was one plugin the whole time. Staging it beside the decoder gives 26.3 one
too, and NINJA GAIDEN 4 is where that was measured: it plays on stock 26.3,
video and all, with nothing patched into CrossOver.

Several titles need a codec no CrossOver ships -- VC-1, WMV3, WMV2 or WMA --
and where it comes from depends on the engine. On a stock CrossOver the plugin
is staged from a GStreamer runtime you installed, one staging per engine, with
the bottle pointed at it; on an engine copy this project makes, the same plugins
are already inside the engine's own `lib64/gstreamer-1.0` and nothing is staged
at all. Neither route patches a decoder into a CrossOver you installed. Which
titles, and which plugin each one needs, is the Codec column of
[what each title actually loads](Games.md#what-each-title-actually-loads); the
count is derived there rather than repeated here, because the number written
here was three for as long as it took two more titles to join the list. Nioh 3
needs none: its video is already NV12 by the time Media Foundation is asked for
it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.

<!-- games:end -->

## What each title actually loads

The table above says whether a title works. This one says what it is made of:
which DLL the fix rides on, which bridge it was built from, which plugin has to
be in front of CrossOver before the video plays, and whether Wine has to be told
to prefer our file at all.

<!-- stack:begin -->

| Game | Backend | DX | GPTK | Carrier | Kept as | Bridge | Codec | Env levers | Registry |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [METAL GEAR SOLID: Peace Walker](Metal-Gear-Solid-Peace-Walker.md) | D3DMetal | 11 | not measured -- the engine was the variable | — | — | — | **in the engine** | — | — |
| [Mortal Shell 2](Mortal-Shell-2.md) | D3DMetal | 12 | not measured | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | D3DMetal | 12 | 4.0b2 | `libxess.dll` | `libxess_real.dll` | `dwo-video-bridge.c` | `libgstmatroska` | — | — |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | D3DMetal | 12 | not measured | `libogg_64.dll` | `libogg_64_real.dll` | `ue5-media-fix.c` | — | — | — |
| [Persona 5 Strikers](Persona-5-Strikers.md) | **DXMT** | 11 | not measured | `amd_ags_x64.dll` | `amd_ags_x64_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [NINJA GAIDEN 3: Razor's Edge](Ninja-Gaiden-3-Razors-Edge.md) | **DXVK** | 9 | not measured | — | — | — | not measured | — | — |
| [Nioh](Nioh.md) | **DXMT** | 11 | 4.0b2 | `GfeSDK.dll` | `GfeSDK_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [Nioh 2](Nioh-2.md) | **DXMT** | 11 | 4.0b2 | `GfeSDK.dll` | `GfeSDK_real.dll` | `p5s-video-bridge.c` | `libgstlibav` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER` | — |
| [Nioh 3](Nioh-3.md) | D3DMetal | 12 | 4.0b2 | `amd_ags_x64.dll` | `amd_ags_x64_real.dll` | `dwo-video-bridge.c` | — | — | — |
| [Wo Long: Fallen Dynasty](Wo-Long-Fallen-Dynasty.md) | D3DMetal | 12 | 4.0b2 | `libxess.dll` | `libxess_real.dll` | `dwo-video-bridge.c` | — | — | — |
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | D3DMetal | 11 | 4.0b2 | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | `libgstlibav` | — | yes |
| [KINGDOM HEARTS Dream Drop Distance](Kingdom-Hearts.md) | D3DMetal | 11 + 12 | not measured | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | — | — | yes |
| [KINGDOM HEARTS 0.2 Birth by Sleep](Kingdom-Hearts.md) | D3DMetal | 11 | not measured | — | — | — | not measured | — | — |
| [KINGDOM HEARTS HD 1.5+2.5 ReMIX](Kingdom-Hearts.md) | D3DMetal | 11 + 12 | not measured | `dinput8.dll` | `dinput8_real.dll` | `dwo-video-bridge.c` | — | — | yes |
| [TMNT: Splintered Fate](TMNT-Splintered-Fate.md) | D3DMetal | 12 | 4.0b2 | `fmod.dll` | `fmod_real.dll` | `d3d12-guards.c` | — | — | — |
| [Tormented Souls 2](Tormented-Souls-2.md) | D3DMetal | 12 | 4.0b2 | `OpenColorIO_2_3.dll` | `OpenColorIO_2_3_real.dll` | `d3d12-guards.c` | — | — | — |
| [Devil May Cry 5](RE-Engine-VC1.md) | D3DMetal | 12 | not measured | — | — | — | `libgstlibav` | — | — |
| [RESIDENT EVIL 2](RE-Engine-VC1.md) | D3DMetal | 12 | not measured | — | — | — | `libgstlibav` | — | — |
| [RESIDENT EVIL 3](RE-Engine-VC1.md) | D3DMetal | 12 | not measured | — | — | — | `libgstlibav` | — | — |
| [NINJA GAIDEN 4](Ninja-Gaiden-4.md) | D3DMetal | 12 | **3.0 only** -- on 4.0b2 the video has sound and no picture | `dstorage.dll` | `dstorage_real.dll` | `ng4-observe.c` | `libgstmatroska` | `BEAST_FORCE_NV12`, `BEAST_REFUSE_D3D_MANAGER`, `NG4_ANSWER_MFT`, `NG4_CPU_DECOMP`, `NG4_FAKE_OPTIONS17`, `NG4_NO_D3D11_PATCH`, `NG4_PATCH_D3D12`, `NG4_REFUSE_DSTORAGE`, `NG4_SMALL_STAGING` | — |
| [RESONANCE: A PLAGUE TALE LEGACY](Resonance-A-Plague-Tale-Legacy.md) | D3DMetal | 12 | not measured | `NvCloth_x64.dll` | `NvCloth_x64_real.dll` | `shader-floor-fix.c` | — | — | — |

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

**Codec** is the plugin that has to be in front of CrossOver before the title
can play. 7 titles need a decoder no CrossOver ships, 2 need a
demuxer, and telling those two cases apart is most of the work -- see
[How the codec staging works](How-the-codec-staging-works.md). Stable 26.3
ships no `matroska` plugin at all, so the demuxer rows say what has to be put
in front of it.

Where it comes from depends on the engine. On a stock CrossOver
`stage-codecs.sh` puts it there; on one of this project's engine copies the
plugins are already inside the engine, and that script finds them and stands
down, because a second copy on the search path means two GStreamer cores in one
process. The row marked **in the engine** is METAL GEAR SOLID: Peace Walker,
where that is the whole fix: nothing beside the game, and nothing staged.

<!-- stack:end -->

## One bottle cannot hold all of these

`CX_GRAPHICS_BACKEND` divides them, and the Backend column above says which is
which. It now holds three values rather than two: the rows marked **DXMT**, the
rows marked **DXVK**, and the rest on `d3dmetal`. The DXMT group is there
because it needs a shared D3D9 surface handle, which DXMT implements and
D3DMetal has none to give. The **DXVK** row is Direct3D 9 only: CrossOver's own
DXVK cannot create a device for it, and the fix supplies a different D3D9
implementation for that one executable rather than changing what the rest of the
bottle uses — see [its own page](Ninja-Gaiden-3-Razors-Edge.md).

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
  not one. **This one depends on what the engine carries**: stable 26.3 ships no
  `matroska` plugin for either architecture, so it has to be supplied — staged
  beside the bottle, or already inside an engine this project patched. The VP9
  inside is decoded the same either way.
- **A decoder is a codec problem.** No CrossOver decodes VC-1, WMV3, WMV2 or
  WMA, and the titles marked `libgstlibav` in the Codec column play video in one
  of those — the RE Engine three, the two Nioh, Persona 5 Strikers, and NieR
  Replicant, which joined the list by measurement after years of being filed as
  needing nothing. `libgstlibav` is never patched into a CrossOver you
  installed: on a stock engine it is staged in front of it, and on an engine
  copy this project makes it is already inside. Either way those fixes depend on
  nothing CrossOver decodes and behave the same on both builds.
- **Neither, for the rest.** Mortal Shell 2 decodes in-process with Electra's
  own libvpx; Beast of Reincarnation goes through an H.264 decoder both builds
  have; both Life is Strange freezes are inside DXGI and never touch video. One
  qualification on the last of those: the DLL those two install carries all
  three repairs, and its policy table arms only the node guard for them -- but
  the Media Foundation hooks it installs go in for every title regardless, and
  that unconditional instrumentation is the standing suspicion for why those
  two crash on 26.3. The fault has nothing to do with video; the DLL touches
  video anyway.

**On winevideo.** On a stable build it
ships a WebM demuxer, which is the thing DYNASTY WARRIORS has no other way to
get — stable 26.3 carries no `matroska` plugin, read out of the two installs.
That is a conclusion from what each build contains rather than a measurement of
winevideo: the title crashes on a stock 26.3 — that much was run — and plays on
26.3 once the engine is patched, but whether winevideo in particular would serve
it was never tried. One unbuilt alternative, and the
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
