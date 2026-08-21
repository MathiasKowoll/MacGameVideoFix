# Life is Strange: Double Exposure

Unreal Engine 5. The same freeze as [Reunion](Life-is-Strange-Reunion.md), and
the same fix.

| | |
| --- | --- |
| Symptom | Runs fine, then freezes after a while, anywhere in the game |
| Cause | `IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for node indices the adapter does not have |
| Fix | Refuse them — the same DLL Reunion uses |
| winevideo | Not required — the fault is in DXGI, not in video |
| Status | **Fix installed, not yet confirmed in play** |

## What was actually checked

The node walk was confirmed **statically**, not by playing to a freeze. The
scanner that looks for the `call qword ptr [rax+0x70]` / backwards `jns` pair
found it at `+0x162e8d8` in this executable, the same shape it found in
Reunion. That is the loop described in full on
[Reunion's page](Life-is-Strange-Reunion.md), so it is not repeated here.

The fix is the same file, installed the same way, and it is one DLL for both
titles rather than a per-game build.

**This row will say "fixed" once it has survived a session that would
previously have frozen, and not before.** A static match says the loop is
there; it does not say nothing else is also wrong.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md)
