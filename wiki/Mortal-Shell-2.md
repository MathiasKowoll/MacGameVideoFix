# Mortal Shell 2

Unreal Engine 5.6.1. Crashes on the first cutscene.

| | |
| --- | --- |
| Cutscenes | 61 x VP9 in `.mp4`, inside `pakchunk0-Windows.pak` and loose |
| Played by | Electra, decoding VP9 with its own bundled libvpx |
| Symptom | `EXCEPTION_ACCESS_VIOLATION` reading `0x0` in `FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer` |
| Fix | Runtime patch, **4 sites**, all four confirmed at runtime |
| CrossOver | 26.3 and `crossover-preview-arm64-20260821`, played through on both |
| winevideo | Not required — see below |

## The fault

Electra picks its output buffer path from the graphics device:

```c
bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000);
```

On D3D12 that is true, so it takes the GPU path — and that path asks every
D3D12 resource for `ID3DDestructionNotifier` and uses the answer without
checking it got one. Apple's D3DMetal does not implement that interface, so
the first VP9 frame dereferences a null vtable and the game dies.

H.264 and H.265 can avoid the buffer pool through a CVar. VPx has no
equivalent, so **VP9 on D3D12 has no way out through configuration**.

## The fix

Raise the `12000` immediate to `INT_MAX` in the running process. The
comparison then never holds, Electra takes the CPU buffer path, and the
interface it cannot get is never asked for.

The same immediate appears at four sites and all four are patched; each was
confirmed to be reached at runtime rather than assumed from the disassembly.

Nothing on disk changes. The original VP9 files are left exactly as they are.

## winevideo

**Not required — measured.** The game was played through on CrossOver 26.3
carrying no winevideo, and again on CrossOver-winevideo 26.3: the same version,
differing only in the GStreamer plugins. The fix worked either way. That
paired run is the controlled comparison the rest of these pages lean on, and it
was made on this title and no other. `crossover-preview-arm64-20260821` was
measured separately, and also plays.

That matches the mechanism. VP9 never goes through Media Foundation here —
Electra decodes it in-process with its own libvpx, and only the *output*
conversion was broken, which is what the patch reroutes. The executable does
import `MFPlat`, `MFReadWrite` and `MF`, but all three are **delay-loaded**, so
the import table only ever said the code *could* reach Media Foundation, never
that it did.

Install winevideo if something else needs it. This fix does not depend on it.

This title has been offered as the control for a finding that belongs to
another one, and it is not one. Its 61 cutscenes are VP9 in `.mp4` and it plays
on stable 26.3, while
[DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) ships the same codec in
`.webm` and does not — but as above, this title's VP9 never reaches CrossOver's
media stack at all, so nothing it does tests what stable can open. What it is
evidence for is that VP9 is not the obstacle. The container finding rests on the
plugin-set comparison instead, which is in [Findings](Findings.md), under *The
container, not the codec*.

## Caveats

- **Do not use this on a game with anti-cheat.** It patches a running process.
- Steam's *verify integrity of game files* undoes the install. No game file's
  contents are edited, but the game's own `libogg_64.dll` is moved aside and
  the proxy put in its place, and a verification puts the stock DLL back over
  it. Running `install-runtime-fix.sh` again restores the fix.


---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [Findings](Findings.md), what what they have in common
