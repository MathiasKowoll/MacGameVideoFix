# Games

Titles we have deliberately taken on, and what was measured on each. This is
not an inventory of anyone's library — a game gets a row when there was a
reason to work on it.

Tested on an M4 Max, macOS 27, CrossOver 26.2 patched with
[winevideo](https://github.com/Jfishin/winevideo), GPTK 4.0b2.

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

## Reading the winevideo column

It answers one question: **does the game need CrossOver patched with winevideo
for the fix to get it working?**

- **Yes** means the fix is not enough on its own. DYNASTY WARRIORS: ORIGINS
  decodes VP9 through Media Foundation, and without winevideo there is nothing
  to decode it with — no frame ever exists for the bridge to present.
- **No** means the fault has nothing to do with video decoding. Both Life is
  Strange titles freeze inside DXGI; the cutscenes were never the problem.

winevideo is worth having regardless. The column is about what each fix
depends on, not about whether to install it.

## Adding a row

Run the survey on that game's folder, and if it misbehaves, the probe. Both
are in `diagnostics/` and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md).

Paste what they print. Measurements are the point of these pages, and a row
without one is worse than no row.
