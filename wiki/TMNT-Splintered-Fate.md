# TEENAGE MUTANT NINJA TURTLES: Splintered Fate

The first fix here that has nothing to do with video, and the smallest one in
the project: twelve lines that turn a silent death into a game.

| | |
| --- | --- |
| Symptom | Opens a window, then closes about three seconds later. No dialog, no error, nothing in any log |
| Cause | `D3D12CreateRootSignatureDeserializer` ends the process instead of returning an error |
| Fix | Look in the container first, and answer for it when there is nothing to find |
| Backend | **D3DMetal**, D3D12 |
| CrossOver | Stable 26.3, on a copy of it carrying this project's `winegstreamer` (2026-08-31) |

## A crash with nothing to go on

A silent exit is the worst kind to work on. There is no stack, no message and
no failing call to point at, so every hypothesis costs a run and none of them
can be ruled out cheaply.

What made it tractable was a probe that logs refusals rather than calls, plus a
vectored exception handler that writes down where a process dies. Between them:

    D3D12CreateDevice -> S_OK
    CreateSwapChainForHwnd(1000x461 format=28 buffers=3 effect=4) -> S_OK
    48 committed resources, 2 queues, 3 descriptor heaps, 31 barriers
    CRASH  ACCESS_VIOLATION reading 0x4
           in D3DMetal.framework/.../libmetalirconverter.dylib +0xa27ef6

Reading address 4 is a null pointer with a field offset added. And the module
is not the game's — it is Apple's DXIL-to-Metal converter. So the game was not
doing anything wrong; something it called was.

## Which call, exactly

The game imports two free functions from `d3d12.dll` that no device-level hook
can see: `D3D12SerializeRootSignature` and
`D3D12CreateRootSignatureDeserializer`. Hooking those, and writing the line
*before* making the call so it survives the process:

    RS [#1] CreateRootSignatureDeserializer, 3224 bytes, first dword 0x43425844

`0x43425844` is `DXBC`. That call never returned.

## What was in the container

Dumped to disk before the call that consumed it, then parsed at leisure:

    DXBC, version 1, 3224 bytes, 7 parts
      SFI0  ISG1  OSG1  PSV0  STAT  HASH  DXIL

Seven parts and **no `RTS0`**. `RTS0` is where a root signature lives, and this
container has none — because the shader was not compiled with one, which is the
normal case.

So the game is asking *"does this shader carry a root signature?"*. On Windows
the answer is `E_INVALIDARG` and the engine carries on. Under D3DMetal the
question is fatal.

## The fix

Look before letting it look:

    walk the container's part table
    if there is no RTS0 -> return E_INVALIDARG, do not call through
    if there is        -> call through, untouched

Walking a DXBC container reads a count and an array of offsets and touches
nothing but the caller's own bytes. Every read is bounded by the length the
caller passed, because a guard that can run off the end is not a guard.

Measured in one session: eight containers, none with an `RTS0`, all eight
answered. Any one of them would have been the crash.

## The carrier, which is the game's own

`fmod.dll` — audio, imported directly, nothing to do with rendering. Its 1109
exports are forwarded straight back to the renamed original. Nothing is
redistributed, no registry key is written and no CrossOver file is copied,
which makes this the least invasive fix in the project.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on D3DMetal only: on 2026-08-31 on stable 26.3 — on a copy of it this
project patched, carrying our `winegstreamer`, not a stock one. It was measured
on `crossover-preview-arm64-20260821` before that, which stands as a record
rather than a route, since Preview is no longer a supported engine here.

Verified by ten minutes of play, not by a single boot.

The fix is scoped to one call and refuses only containers that genuinely have
no root signature in them. A container that has one is passed through
unexamined beyond the check.

## What it cost

Six runs. The first three established what was *not* wrong — nothing refused,
no capability missing, the swap chain and device both fine — which is what made
the fourth, with an exception handler in it, worth doing.
