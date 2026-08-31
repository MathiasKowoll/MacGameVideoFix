Cutscenes that crash, or stay black, in Windows games running under CrossOver
on Apple Silicon — what causes it, which games are affected, and how to find
out about a game that is not listed yet.

> ### Which CrossOver, and whose
>
> By default the app does not modify the CrossOver you installed. It makes a
> copy, at your direction, and patches the copy — the media libraries, the
> codecs, and the Game Porting Toolkit if you ask for it. On that route the
> original stays as CodeWeavers shipped it, so a support question about an
> engine we changed is never a question about theirs. With the copy turned off
> in Set up, a toolkit you ask for goes into the CrossOver you installed
> instead.
>
> The supported engine is stable CrossOver 26.3.0.39832, and nothing else. That
> is what the `winegstreamer` pair in the app was built against, and
> `install-engine-media.sh` refuses any other version rather than installing
> onto a wine it was not compiled for. The refusal comes after the copy has been
> made, so it leaves an unfinished bundle in `~/Applications` rather than a
> working copy.
>
> **CrossOver Preview is not supported.** It was the engine everything here was
> measured on for a long time, and it was dropped rather than half-supported:
> only titles patched without `winegstreamer` could have worked on it. The notes
> below that say what it did are kept as history, because a deleted measurement
> is not a correction. None of them is guidance about where to run a game.
>
> The CrossOver column below says what each title was measured on, and it is the
> same answer on every row: stable 26.3, on an engine carrying this project's
> `winegstreamer`. The GPTK column says which toolkit, and the Motor column what
> the engine itself had to carry.

The tooling lives in [MacGameVideoFix](https://github.com/MathiasKowoll/MacGameVideoFix).

## The failure modes

They look different and have nothing in common except the symptom.

**The crash.** Unreal's Electra media player asks every D3D12 resource for
`ID3DDestructionNotifier` and uses the answer without checking whether it got
one. Apple's D3DMetal does not implement that interface, so the first VP9 frame
dereferences a null vtable and the game dies. H.264 and H.265 can avoid the
buffer pool through a CVar; VPx has no equivalent, so VP9 on D3D12 has no way
out through configuration.

→ Fixed. See [Mortal Shell 2](Mortal-Shell-2.md).

**The black screen.** The game reaches the cutscene, shows nothing, and does
not crash. Nothing returns an error, so there is no crash log and nothing to
grep for — the failure has to be traced through the code.

→ Fixed on [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md), where it
turned out to be five separate faults in a row, ending in a frame that decoded
correctly and had no way to reach the renderer that draws it.

**The buffer, rather than the codec.** The frame arrives, and the game asks it
for `IMF2DBuffer2` — a view with a stride, which is the shape it reads. Wine's
media source hands out a plain 1-D memory buffer, which answers `E_NOINTERFACE`;
the game does not look at that answer and dereferences the null it was given.
Nothing is wrong with the decoder, and nothing installed beside the game can
reach this: the buffer is made inside the engine, so that is where it is fixed.

→ Fixed. See [METAL GEAR SOLID: Peace Walker](Metal-Gear-Solid-Peace-Walker.md).

**A fourth mode has nothing to do with video at all.** Both Life is Strange
titles run fine and then freeze, anywhere, because Unreal walks the GPU's
memory nodes and D3DMetal never tells it to stop. It is listed here because it
is the same toolkit, not because it is the same problem.

## Is my game affected?

The thing that matters is what the cutscenes are encoded as, what box they are
in, and which API plays them. `survey-games.sh` reports all three for a game
folder:

```
diagnostics/survey-games.sh "/path/to/steamapps/common/<Game>"
```

Reading the output:

- **VP9 + Unreal** — likely the crash, but confirm it before patching. No
  static scanner for Electra's `12000` version check ships here: the code that
  knows the pattern is the runtime patch itself, which reports how many sites
  it found once it is installed. A count of zero means this bug is not present
  in that build, whatever else may be wrong. See
  [Diagnosing a new game](Diagnosing-a-new-game.md).
- **VP9 + anything else** — possible, but a different mechanism each time.
- **Bink (`.bik` / `.bk2`)** — Bink ships its own decoder and never touches
  Media Foundation or D3D video. Not affected by any of this.
- **H.264** — decoded by CrossOver on its own, with nothing patched. A working
  decoder is not the whole story: Beast of Reincarnation decodes H.264 and still
  showed nothing, because CrossOver withholds NV12 from the format list on macOS
  and Electra accepts nothing else. If the sound plays and the picture does not,
  start at [Beast of Reincarnation](Beast-of-Reincarnation.md).

The container is worth as much as the codec. The codec says whether anything
can decode the file; the container says whether anything can open it, and on a
CrossOver as CodeWeavers ships it that is where WebM stops: 26.3 carries no
Matroska demuxer, so one has to be supplied — see [Games](Games.md).

Two caveats on the survey. It reads Unreal `.pak` indexes but only version 11
unencrypted ones, so a title using anything else reports zero videos when it
may have hundreds. And a game that packs its movies in a proprietary archive is
invisible to it — `0` means "none found loose or in a readable pak", never "no
videos".

## Games

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

Each row links to a page with the findings and the fix for that title.

## Pages

- [Games](Games.md) — the table above, what each fix needs from CrossOver, and
  how a row gets added
- [Diagnosing a new game](Diagnosing-a-new-game.md) — the tools, and what each one answers
- [Findings](Findings.md) — what they have in common: root causes, the
  vtable slots each hook takes, the carrier DLLs, the container-versus-codec
  comparison, the open defect on 26.3, and what was tried and did not work.
  These pages hold the per-title findings; that one holds what is shared.
- [Running the scripts directly](Running-the-scripts.md) — for working from a
  clone: reproducing a fix by hand, or building a proxy for a title that has none.
  Nothing here is needed to use a release.
- [What ships, and where it goes](What-ships-and-where-it-goes.md) — every
  binary this project produces or redistributes, where each one belongs, and
  what a launcher has to copy to work on another machine.
