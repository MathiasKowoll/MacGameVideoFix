# Life is Strange: Reunion

Unreal Engine 5. Runs, then freezes.

| | |
| --- | --- |
| Symptom | Runs fine, then freezes after a while, anywhere in the game |
| Cause | `IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for node indices the adapter does not have |
| Fix | Refuse them, which is what Windows does |
| winevideo | Not required — the fault is in DXGI, nowhere near video. No paired with-and-without run was made on this title; the controlled comparison is [Mortal Shell 2](Mortal-Shell-2.md)'s |
| CrossOver | Preview build 20260821. **Crashes on 26.3** — our defect, open; see below |

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
forever — two hundred million iterations a second on an M4 Max, counted during
the run the spindump above was taken from.

Refusing node 1 with `DXGI_ERROR_INVALID_CALL` ends it. The refusal fires
**once** per session: Unreal takes the node count from that answer and never
asks again. The polling rate settles at around 2,500 a second and stays there —
one machine, one session, from the same instrumented run.

**Corroboration.** The freeze does not happen with `-dx11`, because the D3D11
RHI has no node concept and never makes the walk — at the cost of PSO
precompilation, which is why that workaround performs badly.

**A second, separate win, and it is a report rather than a measurement — and it
is not in the shipping fix.** Serving repeat queries from a 100 ms cache felt
like noticeably better frame rates even before the freeze was fixed. No frame
time was captured before or after, so that is an impression and nothing more,
and the cache was not kept: the guard that ships refuses any node other than
zero and passes node zero straight through, holding nothing between calls. The
reasoning behind the cache stands on its own — thousands of crossings a second
into Wine's unix side cost more than their wall time, which is contention
rather than cycles — but it wants a frame-time capture before anyone brings it
back.

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

**This one is not about this game.** The walk is in Unreal's D3D12 RHI rather
than in anything either title added, so other UE5 titles on that RHI are likely
to make it. Two have been scanned and both have it; that is the whole sample.

## On stable CrossOver: this one crashes, and it is ours

Measured on CrossOver 26.3: the game crashes. The freeze fix itself is nowhere
near video — but the DLL that carries it carries two other repairs as well, and
it is worth being exact about which of them this title actually runs.

Its policy table arms the node guard for `Iris-Win64-Shipping.exe` and nothing
else. The H.264 half described on the
[Beast of Reincarnation](Beast-of-Reincarnation.md) page — the one that puts
NV12 back on the decoder's menu after CrossOver removes it on macOS — is in the
file and switched off in the process, so it cannot be crashing this title by
changing a format.

What is not switched off is the survey instrumentation. The Media Foundation
hooks are installed for every title the DLL runs in, armed or not: the entry
points are interposed and the decoder's vtable slots are patched when one is
created. That is the standing suspicion, and it is a suspicion — no measurement
names it yet, and the test that would, running these two on 26.3 with those
hooks compiled out, has not been made. The defect is ours rather than the
engine's, it is open, and until it is settled these two titles want Preview.

## Also affects

[Life is Strange: Double Exposure](Life-is-Strange-Double-Exposure.md) carries
the same loop, and the same DLL fixes it — including the 26.3 crash above.

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

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [How the fixes work](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/how-it-works.md), the shared mechanism behind all six
