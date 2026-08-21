# Life is Strange: Reunion

Unreal Engine 5. Runs, then freezes.

| | |
| --- | --- |
| Symptom | Runs fine, then freezes after a while, anywhere in the game |
| Cause | `IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for node indices the adapter does not have |
| Fix | Refuse them, which is what Windows does |
| winevideo | Not required — measured, and the fault is in DXGI, not in video |

**It is not a deadlock.** A spindump taken while it was stuck shows the
GameThread burning 1.27 seconds of CPU across 128 samples while RenderThread 0
used four milliseconds. One thread pinned, everything else starving behind it.

**The loop.** Unreal walks the adapter's memory nodes, accumulating across
them, and ends the walk when the call fails:

```asm
145325f60:  movl  $0x1, %r8d     ; group 1
145325f79:  movl  %esi, %edx     ; node N
145325f99:  callq *0x70(%rax)    ; QueryVideoMemoryInfo, slot 14
145325f9e:  js    <exit>         ; failed -> done
145325fae:  incl  %esi           ; ++N
145325fb5:  xorl  %r8d, %r8d     ; group 0
145325fd8:  callq *0x70(%rax)    ; node N+1
145325fdd:  jns   <loop>         ; succeeded -> keep going
```

Two queries a turn, one per memory segment group, and the only brake is the
call failing. On Windows it returns an error once the index passes the number
of nodes. **D3DMetal answers `S_OK` for every index**, so the counter climbs
forever — two hundred million iterations a second, measured.

Refusing node 1 with `DXGI_ERROR_INVALID_CALL` ends it. The refusal fires
**once** per session: Unreal takes the node count from that answer and never
asks again. The polling rate settles at around 2,500 a second and stays there.

**Corroboration.** The freeze does not happen with `-dx11`, because the D3D11
RHI has no node concept and never makes the walk — at the cost of PSO
precompilation, which is why that workaround performs badly.

**A second, separate win.** Serving repeat queries from a 100 ms cache was
reported as noticeably better frame rates even before the freeze was fixed.
Thousands of crossings a second into Wine's unix side cost more than their
wall time, which is contention rather than cycles.

**How it was found, and three wrong turns.** Each theory died to a measurement
rather than an argument: the budget is never full (75 GB against 751 MB in
use), the call is not expensive (under two microseconds), and the game never
reserves memory at all, so the reservation fields it was handed were never
being waited on. What found it was asking *where the loop lives* rather than
what it reads — and that only worked after the instrumentation built for the
job, a table under its own lock on a timer, was thrown away for a single log
line. It had produced nothing at all across two billion calls.

The call site it named, `+0x5325f9c`, is the same address a winedbg capture
had recorded as frame 1 weeks earlier.

**This one is not about this game.** Any UE5 title on the D3D12 RHI makes that
walk.

## Also affects

[Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) carries
the same loop, and the same DLL fixes it.


---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md)
