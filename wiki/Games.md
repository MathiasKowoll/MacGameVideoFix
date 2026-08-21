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
| [Life is Strange: Reunion](Life-is-Strange-Reunion.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard | No | Fixed |
| [Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) | Unreal Engine 5 | Freezes after a while, anywhere | DXGI node guard, same DLL | No | Installed, not yet confirmed in play |
| [DYNASTY WARRIORS: ORIGINS](Dynasty-Warriors-Origins.md) | Koei Tecmo, in-house | Cutscene runs with sound, picture black | Video bridge, D3D11 to D3D12 | **Yes** | Fixed |

<sup>1</sup> Expected rather than measured: VP9 never reaches Media Foundation
in that game, but it has not been run on a CrossOver without winevideo.

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
