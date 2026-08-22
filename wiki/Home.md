Cutscenes that crash, or stay black, in Windows games running under CrossOver
on Apple Silicon — what causes it, which games are affected, and how to find
out about a game that is not listed yet.

> ### This is for CrossOver Preview
>
> Specifically **`crossover-preview-arm64-20260821`**. That is where every title
> here is measured, and it is the only configuration this project supports.
> Three of the nine also run on stable 26.3; treat that as a bonus rather than a
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
| [NieR Replicant ver.1.22474487139](NieR-Replicant.md) | Toylogic, in-house | Crashes when the first video starts | Software decode, and the frame written into the game's target | D3DMetal | 11 | Preview -- not tried on 26.3 | Fixed |

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
other seven run on
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

Each row links to a page with the findings and the fix for that title.

## Pages

- [Games](Games.md) — the table above, what each fix needs from CrossOver, and
  how a row gets added
- [Diagnosing a new game](Diagnosing-a-new-game.md) — the tools, and what each one answers
- [Findings](Findings.md) — what the nine have in common: root causes, the
  vtable slots each hook takes, the carrier DLLs, the container-versus-codec
  comparison, the open defect on 26.3, and what was tried and did not work.
  These pages hold the per-title findings; that one holds what is shared.
- [Running the scripts directly](Running-the-scripts.md) — for working from a
  clone: reproducing a fix by hand, or building a proxy for a title that has none.
  Nothing here is needed to use a release.
