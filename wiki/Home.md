Cutscenes that crash, or stay black, in Windows games running under CrossOver
on Apple Silicon — what causes it, which games are affected, and how to find
out about a game that is not listed yet.

> ### This is for CrossOver Preview
>
> Specifically **`crossover-preview-arm64-20260821`**. That is where every title
> here is measured, and it is the only configuration this project supports.
> Most of them also run on stable 26.3; treat that as a bonus rather than a
> promise.

The tooling lives in [MacGameVideoFix](https://github.com/MathiasKowoll/MacGameVideoFix).

## The two failure modes

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

**A third mode has nothing to do with video at all.** Both Life is Strange
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
- **H.264** — decoded by CrossOver on its own, on stable and on Preview, with
  nothing patched. A working decoder is not the whole story: Beast of
  Reincarnation decodes H.264 and still showed nothing, because CrossOver
  withholds NV12 from the format list on macOS and Electra accepts nothing
  else. If the sound plays and the picture does not, start at
  [Beast of Reincarnation](Beast-of-Reincarnation.md).

The container is worth as much as the codec. The codec says whether anything
can decode the file; the container says whether anything can open it, and on
stable CrossOver that is where WebM stops — see
[Games](Games.md).

Two caveats on the survey. It reads Unreal `.pak` indexes but only version 11
unencrypted ones, so a title using anything else reports zero videos when it
may have hundreds. And a game that packs its movies in a proprietary archive is
invisible to it — `0` means "none found loose or in a readable pak", never "no
videos".

## Games

<!-- games:begin -->

| Game | Engine | Symptom | Fix | Backend | DX | GPTK | CrossOver | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | D3DMetal | 12 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | 26.3 and Preview | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | D3DMetal | 12 | **4.0b2 only** -- 3.0 crashes it | 26.3 and Preview | Fixed |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | D3DMetal | 12 | 4.0b2 | 26.3 and Preview | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | Console variable puts Electra on its CPU path; **needs winevideo** | D3DMetal | 12 | 3.0 | 26.3 with winevideo -- Preview stalls | Fixed |
| [Persona 5 Strikers](Persona-5-Strikers.md) | Koei Tecmo, in-house | Video never starts; sound only | Staged VC-1 codec, and a D3D9 to D3D11 bridge | **DXMT** | 11 | 3.0 and 4.0b2 | 26.3 and Preview | Fixed |
| [NINJA GAIDEN 3: Razor's Edge](Ninja-Gaiden-3-Razors-Edge.md) | Koei Tecmo, in-house | Will not start: "Insufficient VRAM" | d9vk, and winevideo's DirectShow filters | **DXVK** | 9 | 3.0 | 26.3 | Fixed |
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
has, for three of the rows here, meant "this needs the newer toolkit" and
nothing about Wine at all.

Those three rows in bold are where that stops being a footnote, and they fall
into two camps pointing opposite ways. **NINJA GAIDEN 4 runs on 3.0 and stalls
on 4.0b2. Life is Strange -- both packages -- runs on 4.0b2 and crashes on
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

**Which CrossOver, and what "Preview" means.** Every measurement here was
taken against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the
CrossOver column says which of the two a title was measured on rather than
which it might work on. **Nineteen of the twenty-one run on stable 26.3**,
which inverts where this project started: stable was the exception and is now
the rule, and the toolkit -- not the engine -- turned out to be the axis that
decides most of these titles.

The two that are missing point one way and are not a result: the Kingdom
Hearts pair has only ever been launched on Preview, so its rows record an
absence rather than a finding and nothing is claimed either way.

Two run on stable and stall on Preview, which is the interesting direction:
Beast of Reincarnation and NINJA GAIDEN 4. What stalls NINJA GAIDEN 4 is the
toolkit, which executes command lists concurrently with no lever to turn that
off. Beast of Reincarnation is on that list for a different reason -- it needs
winevideo since the game update of 2026-08-24 -- and the shared lesson is that
"runs on Preview" was never the safe assumption this project began with.

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
