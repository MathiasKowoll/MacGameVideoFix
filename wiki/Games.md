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

| Game | Engine | Symptom | Fix | Backend | DX | CrossOver | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | D3DMetal | 12 | 26.3 · Preview | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | D3DMetal | 12 | Preview -- 26.3 crashes | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | D3DMetal | 12 | Preview -- 26.3 crashes | Fixed |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | D3DMetal | 12 | Preview -- crashes on 26.3 | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | D3DMetal | 12 | 26.3 · Preview | Fixed |
| [Persona 5 Strikers](Persona-5-Strikers.md) | Koei Tecmo, in-house | Video never starts; sound only | Staged VC-1 codec, and a D3D9 to D3D11 bridge | **DXMT** | 11 | 26.3 and Preview | Fixed |
| [Nioh](Nioh.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Staged WMV3 codec, and the same D3D9 to D3D11 bridge | **DXMT** | 11 | Preview -- not tried on 26.3 | Fixed |
| [Nioh 2](Nioh-2.md) | Koei Tecmo, in-house | Cutscene refuses to play, then crashes | Same codec and same bridge as Nioh, unchanged | **DXMT** | 11 | Preview -- not tried on 26.3 | Fixed |
| [Nioh 3](Nioh-3.md) | Koei Tecmo, in-house | Failed to play movie | The DYNASTY WARRIORS bridge, unchanged | D3DMetal | 12 | Preview -- not tried on 26.3 | Fixed |
| [Wo Long: Fallen Dynasty](Wo-Long-Fallen-Dynasty.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | The DYNASTY WARRIORS bridge, unchanged | D3DMetal | 12 | Preview -- not tried on 26.3 | Fixed |
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | Toylogic, in-house | Crashes when the first video starts | Software decode, and the frame written into the game's target | D3DMetal | 11 | Preview -- not tried on 26.3 | Fixed |
| [KINGDOM HEARTS Dream Drop Distance](Kingdom-Hearts-HD-2.8.md) | Square Enix, in-house | Cutscene runs with sound, picture solid green | Software decode, and the luma and chroma planes written into the game's own textures | D3DMetal | 11 + 12 | Preview -- not tried on 26.3 | Fixed |

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
other eight run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and what "Preview" means.** Every measurement here was taken
against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the CrossOver
column says which of the two a title was measured on rather than which it might
work on. Everything runs on that Preview. Three are confirmed on 26.3 as well.
This project targets that Preview build, and it is what every title is measured
and supported on. Three also run on stable 26.3 and the column says so, but as a
bonus rather than a promise -- stable is not what gets tested before a release.

What stops the other three is in the engine. Both Life is Strange titles freeze
on 26.3 with the fix removed exactly as they do with it, so nothing installed
beside the game is involved; what differs is D3DMetal, 3.0 against 4.0b2. And
DYNASTY WARRIORS needs a WebM demuxer that 26.3 has no way to reach -- staging
the plugin was tried and the video still never starts. Persona 5 Strikers plays on both, which is what its
fix predicted: it stages its own decoder, so what CrossOver ships stops
mattering. A first attempt on 26.3 failed and was recorded as the title not
working there -- wrongly. The staged codec is built against one CrossOver and is
not usable under another, and none had been built for 26.3 yet.
DYNASTY WARRIORS crashes there too; that much was run, while the reason given for
it is read from the two installs' plugin sets rather than from watching it fail.

Four rows record an absence rather than a result: the three Nioh titles and
NieR Replicant were fixed on Preview and none was launched on 26.3, so nothing
is claimed either way.
Their codec is staged the same way Strikers' is, which is the half that made
Strikers portable, but the bridge half has only ever run against the Preview
build's DXMT.

**None of these games needs CrossOver patched, wherever the container can be
opened.** That was not true when this project started, and it is the single
biggest thing that changed. The qualifier is the whole of what remains, and it
is a container question rather than a codec one.

Both builds decode VP9 the same way; what only Preview can do is open a WebM,
which is the whole of the difference. DYNASTY WARRIORS ships 355 `.webm`
cutscenes and cannot get as far as decoding on stable, while Mortal Shell 2
ships the same codec in `.mp4`, which both builds handle. The plugin-by-plugin
comparison the conclusion rests on is in [Findings](Findings.md), under *The
container, not the codec*.

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
