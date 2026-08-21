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

→ Fixed. See [Games](Games.md) for which titles this applies to.

**The black screen.** The game reaches the cutscene, shows nothing, and does
not crash. Nothing returns an error, so there is no crash log and nothing to
grep for — the failure has to be traced through the code.

→ Fixed on DYNASTY WARRIORS: ORIGINS, where it turned out to be five separate
faults in a row, ending in a frame that decoded correctly and had no way to
reach the renderer that draws it. See [Games](Games.md).

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

## Pages

- [Games](Games.md) — what was measured, per title
- [Diagnosing a new game](Diagnosing-a-new-game.md) — the tools, and what each one answers
