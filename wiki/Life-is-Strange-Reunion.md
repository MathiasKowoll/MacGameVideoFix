# Life is Strange: Reunion

Unreal Engine 5. Runs, then freezes.

| | |
| --- | --- |
| Symptom | Runs fine, then freezes after a while, anywhere in the game |
| Cause | `IDXGIAdapter3::QueryVideoMemoryInfo` succeeds for node indices the adapter does not have |
| Fix | Refuse them, which is what Windows does |
| winevideo | Not required — the fault is in DXGI, nowhere near video. No paired with-and-without run was made on this title; the controlled comparison is [Mortal Shell 2](Mortal-Shell-2.md)'s |
| CrossOver | `crossover-preview-arm64-20260821`. **Freezes on 26.3**, with or without the fix — see below |

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

## On stable CrossOver: it freezes, and not because of us

Measured on 26.3: the game freezes. So does the same game on the same build with
the fix taken out entirely — restored to stock, relaunched, and it freezes again.
That control is what settles it. The fix is not what breaks this on stable; it
simply does not repair it there.

The runtime log says the same thing from the other side. On Preview the guard
arms and, in three sessions out of nine, reports refusing a node that does not
exist. On 26.3 it arms and then nothing: five lines against twenty-nine, and no
node is ever walked. The freeze arrives before the code that was written for it
gets a chance to act.

What differs between the two is D3DMetal. CrossOver 26.3 carries version 3.0;
that Preview carries 4.0b2, and ships 3.0 beside it unused. The two copies of 3.0
are byte-identical, so "stable" and "GPTK 3" are interchangeable in any account
of this.

That is where it stands: reproduced, controlled, and not ours. What has not been
established is why GPTK 3 freezes here, and the guard's own evidence is no help
because the guard never runs.

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

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [Findings](Findings.md), what the six have in common
