Cutscenes that crash, or stay black, in Windows games running under CrossOver
on Apple Silicon — what causes it, which games are affected, and how to find
out about a game that is not listed yet.

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

The thing that matters is what the cutscenes are encoded as and which API plays
them. `survey-games.sh` reports both for a game folder:

```
diagnostics/survey-games.sh "/path/to/steamapps/common/<Game>"
```

Reading the output:

- **VP9 + Unreal** — the crash. Run the fix.
- **VP9 + anything else** — possible, but a different mechanism each time.
- **Bink (`.bik` / `.bk2`)** — Bink ships its own decoder and never touches
  Media Foundation or D3D video. Not affected by any of this.
- **H.264** — normally fine, provided CrossOver is patched with
  [winevideo](https://github.com/Jfishin/winevideo).

Two caveats on the survey. It reads Unreal `.pak` indexes but only version 11
unencrypted ones, so a title using anything else reports zero videos when it
may have hundreds. And a game that packs its movies in a proprietary archive is
invisible to it — `0` means "none found loose or in a readable pak", never "no
videos".

## Games

<!-- games:begin -->

| Game | Engine | Symptom | Fix | winevideo | Status |
| --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | No <sup>1</sup> | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | No <sup>1</sup> | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | No <sup>2</sup> | Fixed |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | No <sup>3</sup> | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | No <sup>1</sup> | Fixed |

<sup>1</sup> Measured, not assumed. Mortal Shell 2 and Life is Strange:
Reunion were played on a CrossOver carrying no winevideo and again on one that
did, same version, differing only in the GStreamer plugins. Beast of
Reincarnation was measured differently and more strictly: every run of it was in
a bottle winevideo has never touched.

<sup>2</sup> Inferred from Reunion rather than measured: identical fault,
identical DLL.

<sup>3</sup> Measured on CrossOver Preview 20260821, in a bottle winevideo had
never touched and with no `.webm` byte-stream handler registered. It was
expected to fail there and did not. **How the WebM is opened at all under those
conditions is not yet explained** -- Preview's own mfplat contains neither the
handler's CLSID nor the string "webm" -- so this is recorded as a measurement
with an open question behind it, not as an understood result.

**No game here needs CrossOver patched with winevideo.** That was not true when
this project started, and it is the single biggest change: Preview decodes VP9
profile 0 and 2, H.264 and AAC on its own. What is still needed is everything
in the Fix column, because none of it is decoding.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy. Where something was
established by reading the executable rather than by playing to the failure,
the page says so.

<!-- games:end -->

Each row links to a page with the findings and the fix for that title.

## Pages

- [Games](Games.md) — the table above, plus how a row gets added
- [Diagnosing a new game](Diagnosing-a-new-game.md) — the tools, and what each one answers
