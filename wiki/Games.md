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
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
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
other nine run on
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
engine.** Neither build ships a Matroska demuxer; Preview reached WebM by another
route. Staging `libgstmatroska` beside the decoder gives stable one too, and
NINJA GAIDEN 4 is where it was measured -- it plays on stock 26.3, video and all,
with nothing patched into CrossOver. What this means for the titles above has not
been re-measured: their rows still say what each was measured on, and DYNASTY
WARRIORS in particular deserves a fresh run on 26.3 before its row changes.

Three titles need a codec no CrossOver ships -- VC-1 for Persona 5 Strikers,
WMV3 for Nioh and Nioh 2 -- and it is staged beside the game rather than patched
into it. Nioh 3 needs none: its video is already NV12 by the time Media
Foundation is asked for it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.

<!-- games:end -->

## One bottle cannot hold all of these

`CX_GRAPHICS_BACKEND` divides them. Five run on `d3dmetal`; Persona 5 Strikers
runs only on `dxmt`, because it needs a shared D3D9 surface handle and
D3DMetal has none to give.

Steam libraries are shared between bottles, so giving that one a bottle of its
own costs no disk and no re-download. Switching the backend by hand also works
and means remembering to switch it — and a forgotten backend looks exactly like
a fix that stopped working.

## What each fix needs from CrossOver

The older form of this question was "does a fix need winevideo?", which framed
it as a codec problem. It is not one, and asking it that way produced the wrong
answer for the single title it existed to describe. What separates these six is
what each fix needs CrossOver to have already done before it can start: open a
container, decode a codec, or neither.

- **DYNASTY WARRIORS: ORIGINS — a WebM demuxer.** Its 355 cutscenes are
  `.webm`, and the bridge only presents frames that Media Foundation has
  already produced. Preview ships the `matroska` plugin and stable 26.3 does
  not, so on stable `MFCreateSourceReaderFromByteStream` fails at the open and
  nothing reaches a decoder at all. Both builds decode the VP9 inside a WebM
  identically, through `applemedia` and VideoToolbox. The evidence and its
  limits are on
  [that title's page](Dynasty-Warriors-Origins.md).
- **Persona 5 Strikers — a VC-1 decoder, which it brings with it.** No
  CrossOver ships one. It is staged beside the game rather than patched into
  the engine, so this fix depends on nothing CrossOver decodes.
- **Everything else — nothing.** Mortal Shell 2 decodes in-process with
  Electra's own libvpx; Beast of Reincarnation goes through an H.264 decoder
  both builds have; both Life is Strange freezes are inside DXGI and never
  touch video. One qualification on the last of those: the DLL those two
  install carries all three repairs, and its policy table arms only the node
  guard for them — but the Media Foundation hooks it installs go in for every
  title regardless, and that unconditional instrumentation is the standing
  suspicion for why those two crash on 26.3. The fault has nothing to do with
  video; the DLL touches video anyway.

**On winevideo.** On a current Preview none of these fixes needs it, and the
VP9 and WebM plugins it installs are redundant there. On a stable build it
ships a WebM demuxer, which is the thing DYNASTY WARRIORS has no other way to
get — stable 26.3 carries no `matroska` plugin, read out of the two installs.
That is a conclusion from what each build contains, not a measurement: the
title has never been launched on stable, with winevideo or without it, so no
dependency on it has been measured here. One unbuilt alternative, and the
reasoning behind it, is in [Findings](Findings.md), under *The container, not
the codec*.

## The mechanism these six share

Why each hook exists, which vtable slot each one takes, how a carrier DLL is
picked and what was tried and did not work is in [Findings](Findings.md). The
division is deliberate: these pages carry the per-title findings and the wrong
turns that came first, that one carries what is common to all of them.

## Adding a row

Run the survey on that game's folder, and if it misbehaves, the probe. Both
are in `diagnostics/` and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md).

Paste what they print, and say which CrossOver build printed it. Measurements
are the point of these pages, and a row without one is worse than no row.
