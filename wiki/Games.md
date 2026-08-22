# Games

Titles we have deliberately taken on, and what was measured on each. This is
not an inventory of anyone's library — a game gets a row when there was a
reason to work on it.

Tested on an M4 Max, macOS 27, GPTK 4.0b2, against CrossOver 26.3 and Preview
build 20260821. The CrossOver column says which builds each title was measured
on. No bottle carried [winevideo](https://github.com/Jfishin/winevideo) except
where a page below says otherwise.

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
project started, and it is the single biggest thing that changed.

What each build gives them is narrower than "decodes VP9", and the distinction
took a while to arrive at. Both CrossOver 26.3 and Preview decode VP9 the same
way, through `applemedia` and VideoToolbox; neither ships `libgstvpx` or
`libgstlibav`. Comparing the two installs plugin by plugin, stable carries 17
GStreamer plugins and Preview 19, and the two Preview has to itself are
`matroska` and `osxaudio`. So the engine dependency that remains is a
*container* one: only Preview can open a WebM. DYNASTY WARRIORS ships 355
`.webm` cutscenes and cannot get as far as decoding on stable, while Mortal
Shell 2 ships the same codec in `.mp4`, which `isomp4` handles on both.

Persona 5 Strikers is the one title needing a codec no CrossOver ships -- VC-1 --
and that is staged beside it rather than patched into it.

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
dependency on it has been measured here. Staging `libgstmatroska.dylib` from
the official GStreamer.framework would supply the same demuxer without patching
CrossOver — 756 KB, the move `runtime/stage-codecs.sh` already makes for
Persona 5 Strikers' VC-1 — but that is not built either.

## The mechanism these six share

Why each hook exists, which vtable slot each one takes, how a carrier DLL is
picked and what was tried and did not work is in
[How the fixes work](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/how-it-works.md),
in the repository rather than on the wiki. The division is deliberate: these
pages carry the per-title findings and the wrong turns that came first, that
one carries what is common to all of them.

## Adding a row

Run the survey on that game's folder, and if it misbehaves, the probe. Both
are in `diagnostics/` and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md).

Paste what they print, and say which CrossOver build printed it. Measurements
are the point of these pages, and a row without one is worse than no row.
