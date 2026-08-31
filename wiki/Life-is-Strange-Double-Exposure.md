# Life is Strange: Double Exposure

Unreal Engine 5. The same freeze as [Reunion](Life-is-Strange-Reunion.md), and
the same fix.

| | |
| --- | --- |
| Symptom | Runs fine, then freezes after a while, anywhere in the game |
| Cause | `IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for node indices the adapter does not have |
| Fix | Refuse them — the same DLL Reunion uses |
| winevideo | Not required — the fault is in DXGI, not in video. Not separately measured here; the DLL and the finding are shared with [Reunion](Life-is-Strange-Reunion.md) |
| CrossOver | Stable 26.3. **Freezes on the D3DMetal 3.0 that build ships**, with or without the fix; this title needs Game Porting Toolkit 4.0b2 — see below |
| Status | Fixed — the guard was confirmed to arm in play. The freeze itself was never reproduced on this title, so what is confirmed is that the fix takes hold, not that a freeze was cured |

## What was actually checked

The node walk was confirmed **statically**, not by playing to a freeze. The
scanner that looks for the `call qword ptr [rax+0x70]` / backwards `jns` pair
found it at `+0x162e8d8` in this executable, the same shape it found in
Reunion. That is the loop described in full on
[Reunion's page](Life-is-Strange-Reunion.md), so it is not repeated here.

The fix is the same file, installed the same way, and it is one DLL for both
titles rather than a per-game build.

It has since been confirmed in play, on the merged DLL: the guard armed and
refused node 1 exactly once, which is the signature of the fix taking hold —
Unreal takes the node count from that answer and stops asking.

What that does not establish is that a freeze was cured. This title was never
played to a freeze before the fix, so there is no before-state to compare
against, and a session that never froze is indistinguishable from one that
could not have frozen yet without a duration to put beside it. None was
recorded. The static match and the armed guard are what there is.

## On stable CrossOver

`crossover-preview-arm64-20260821` is where the confirmation above was made,
which is a record of a measurement and not a route: Preview is no longer a
supported engine here. On 26.3 this title freezes, as Reunion does, and the two
share a DLL and a policy row.

It is not ours. The control was run on Reunion, which is the sibling this page
defers to throughout: the fix removed entirely, the game restored to stock, and
it freezes the same way on the same build. What differs between the engines is
D3DMetal -- 3.0 on 26.3 against 4.0b2 on that Preview. Reunion's page carries the
measurements; [Findings](Findings.md) carries the cross-cutting account.

This title was not separately controlled, and the claim here is inherited rather
than measured, which is the same footing as everything else on this page. What
that leaves is a toolkit requirement rather than an engine one: on stable 26.3
this title needs Game Porting Toolkit 4.0b2, and why GPTK 3 freezes it is still
not understood.

## Caveats

- **Do not use this on a game with anti-cheat.** It patches a running process.
- Steam's *verify integrity of game files* restores the game's own
  `libogg_64.dll`, which undoes the install. Running the installer again puts
  it back.
- The same guard can be installed once into a CrossOver build instead of once
  per game, with `crossover/install-node-guard.sh`. That reaches every title in
  every bottle using that build, and it invalidates the bundle's code
  signature, as any CrossOver patch does.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [Findings](Findings.md), what what they have in common
