# Games

Titles we have deliberately taken on, and what was measured on each. This is
not an inventory of anyone's library — a game gets a row when there was a
reason to work on it.

Tested on an M4 Max, macOS 27, CrossOver 26.2 patched with
[winevideo](https://github.com/Jfishin/winevideo), GPTK 4.0b2.

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

**None of these games needs CrossOver patched, on a build that already decodes
VP9.** That was not true when this project started, and it is the single biggest
thing that changed: CrossOver Preview decodes VP9 profile 0 and 2, H.264 and AAC
on its own. The one thing that still depends on the engine is DYNASTY WARRIORS:
ORIGINS, which decodes VP9 through Media Foundation and has nothing to present
without it -- so on an older or stable build it still needs winevideo, and on a
current Preview it does not. Persona 5 Strikers needs a VC-1 decoder no CrossOver
ships, and that is staged beside it rather than patched into it -- which also
makes it the one title here that does not care what CrossOver decodes.

**Which engine, per title.** Measured: Mortal Shell 2, Beast of Reincarnation and
Persona 5 Strikers run on stable 26.3 and on Preview. The other three crash on
stable, for two unrelated reasons. DYNASTY WARRIORS was expected to -- it decodes
VP9 through Media Foundation and stable ships none, which is the winevideo
requirement on its own row, now measured rather than predicted. Both Life is
Strange titles are our own defect: they share the H.264 half, which restores NV12
after CrossOver removes it on macOS and was written against Preview's behaviour.
That one is open. Use Preview for those three.

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

## Does a fix need winevideo?

One question, and the table above no longer carries a column for it: **does the
fix need the engine to decode video before it can work?**

- **DYNASTY WARRIORS: ORIGINS — yes.** It decodes VP9 through Media Foundation,
  and the bridge only presents frames. Without a decoder there is no frame to
  present. A current CrossOver Preview supplies it; an older or stable build
  needs winevideo.
- **Everything else — no.** The fault has nothing to do with video decoding.
  Both Life is Strange titles freeze inside DXGI; the cutscenes were never the
  problem. Persona 5 Strikers is the separate case: it needs VC-1, which no
  CrossOver ships, staged beside it rather than patched into it.

winevideo is worth having regardless. This is about what each fix depends on,
not about whether to install it.

## Adding a row

Run the survey on that game's folder, and if it misbehaves, the probe. Both
are in `diagnostics/` and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md).

Paste what they print. Measurements are the point of these pages, and a row
without one is worse than no row.
