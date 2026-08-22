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

| Game | Engine | Symptom | Fix | Backend | DX | Status |
| --- | --- | --- | --- | --- | --- | --- |
| [Mortal Shell 2](Mortal-Shell-2.md) | Unreal Engine 5.6.1 | Crash on the first cutscene | Runtime patch, 4 sites | D3DMetal | 12 | Fixed |
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | D3DMetal | 12 | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | D3DMetal | 12 | Fixed |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | D3DMetal | 12 | Fixed |
| [Beast of Reincarnation](Beast-of-Reincarnation.md) | Unreal Engine 5 | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | D3DMetal | 12 | Fixed |
| [Persona 5 Strikers](Persona-5-Strikers.md) | Koei Tecmo, in-house | Video never starts; sound only | Staged VC-1 codec, and a D3D9 to D3D11 bridge | **DXMT** | 11 | Fixed |

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers only works on DXMT: it needs a shared D3D9 surface handle, and DXMT
implements sharing where D3DMetal has none to build on. The other five run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**None of these games needs CrossOver patched.** That was not true when this
project started, and it is the single biggest thing that changed: CrossOver
Preview decodes VP9 profile 0 and 2, H.264 and AAC on its own. Persona 5
Strikers needs a VC-1 decoder CrossOver does not ship, and that is staged
beside it rather than patched into it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.

<!-- games:end -->

Each row links to a page with the findings and the fix for that title.

## Pages

- [Games](Games.md) — the table above, plus how a row gets added
- [Diagnosing a new game](Diagnosing-a-new-game.md) — the tools, and what each one answers
