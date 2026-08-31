# Persona 4 Golden — seventeen runs, and where they stop

Internal notes. No wiki entry and no fix: what stops this game is not a defect
in the translation layer, and the one thing that would "fix" it is out of scope.
Written down so the next attempt starts from here.

**Preview is no longer supported.** As of 2026-08-31 the only supported engine
is stable CrossOver 26.3, and CrossOver Preview was dropped. The Preview
measurements below are kept as a record of what was measured, not as guidance
about where to run anything.

**Symptom.** Opens, shows a window with an FPS counter in the title bar for a
second or two, and closes.

## What the engine decides

The single sharpest result is that the CrossOver build decides whether the game
kills itself.

| Engine + backend | What happens |
| --- | --- |
| Preview 20260821, D3DMetal | Self-terminates, deterministically |
| Preview 20260821, DXMT | Dies immediately |
| **Stable (2026-07-15), D3DMetal** | **Does not self-terminate.** Window, FPS, then hangs |

Same executable, same bottle, same machine.

## The self-termination, measured

Under Preview the fault is identical every time -- not merely the same address,
but the same sixteen register values, with and without any instrumentation:

    ACCESS_VIOLATION at 00000001669D745B   writing to FFFFFFF22D819090
    rax=0000000080000002 rbx=0000000000000000 rcx=000000015e4aface ...

The instruction at that address, read from inside the process because the
executable is packed and cannot be disassembled from disk:

    a2 90 90 81 2d f2 ff ff ff      mov byte ptr [0xFFFFFFF22D819090], al

The impossible address is not computed. It is written into the instruction.
Nothing arithmetic went wrong, because there is no arithmetic -- an afternoon
was spent looking for a mispredicted pointer that does not exist.

The 32 bytes before decode as a function epilogue (`add rsp,0x28 ; ret`) and
alignment padding, so this is real code, not data being executed. The faulting
instruction begins one byte inside what a linear disassembler reads as `cpuid`
-- overlapping instructions, which is deliberate obfuscation. Every stack frame
that resolves to a module is `P4G.exe`: no `d3d11`, no `dxgi`, nothing from the
probe. `rax` holds `0x80000002`, `E_OUTOFMEMORY` in its old OLE form.

A store to a fixed impossible address, reached only from the game's own code, is
what anti-tamper protection does when a check fails. **Defeating that check is
circumvention and this project does not do it.** That is the reason work stopped
here, not a lack of leads.

## Ruled out, each by measurement

- **Our own watching.** A control build with every hook disabled -- no vtable
  patched, no import redirected -- faults at the same address with the same
  sixteen registers. Nine earlier runs stand.
- **The graphics layer.** Across a thousand logged calls D3DMetal refuses
  exactly two things: `EnumAdapters(1)` and `EnumOutputs(1)`, which mean "there
  is only one" and are correct. Nothing else is refused. There is no bad answer
  here to answer better, which is what this project's whole method depends on.
- **Adapter memory figures.** Rewritten from 38338/38338/38338 MB to a
  believable 4096/0/8192; crash unmoved, resource counts identical.
- **The vendor id, on D3DMetal.** Rewritten from 0x10de to 0x1002; crash
  unmoved. (On DXMT it does change behaviour -- see below.)
- **`Map` returning a bad pointer.** 961 calls, all successful, every returned
  pointer a normal user-space address. The theory that a mapped pointer produced
  the fault was good and wrong.
- **Missing dependencies.** Every DLL the game imports resolves, including
  `d3dx11_43`, `vcomp140` and the 64-bit `MSVCP110`/`MSVCR110`.
- **Wine's `d3dx11_43`.** The hang under stable puts frames in it, so the native
  Microsoft build (276 KB, present in the bottle) was overridden in for
  `P4G.exe` in place of Wine's (102 KB). Confirmed loaded from `system32`; the
  hang is unchanged.

## It is not a hang, and it does load its archives

Two things written above were wrong, and both were wrong the same way: a probe
was pointed at one door, saw nothing, and the silence was written down as a fact
about the game.

**It is not frozen.** The window renders the game's own loading indicator at
24-29 fps with the Metal HUD confirming GPU work, and its frame limiter -- read
in the disassembly at +0x609c29: QueryPerformanceCounter, elapsed microseconds,
Sleep -- runs normally. That only became visible when the Metal HUD was turned
on, at the user's suggestion, after an afternoon of measuring CPU from outside.
Removing the cross-engine GStreamer contamination described below took it from
24 fps to 61.

**It does open its data.** A file probe reported "167 files opened, and not one
archive", which was repeated here as the headline finding. The arithmetic kills
it: P4G.ini is 1374 bytes and was opened 163 times, which is 218 KB, while the
same log counted 192 reads totalling 20 MB. Roughly 19.8 MB came from handles
the probe never saw -- because it hooked `CreateFileW` and the game imports
`CreateFileA` as well. The archives were being opened the whole time.

The same error, twice more: a wait probe hooked only `WaitForSingleObjectEx` and
recorded one wait while forty-six threads sat in waits, which was written up as
"the protected binary bypasses kernel32". It does not; most code calls the plain
form, which was not hooked. And a control build meant to prove the probes were
innocent had its early return placed above `AddVectoredExceptionHandler`, so it
ran with no fault handler at all and its empty log was very nearly read as "no
crash happened".

## Cross-engine GStreamer, which was ours

Every run made with the stable engine had two copies of libgstreamer in the
process, and the terminal said so from the first one:

    Class GstCocoaApplicationDelegate is implemented in both
      /Applications/CrossOver.app/.../libgstreamer-1.0.0.dylib
      /Applications/CrossOver Preview.app/.../libgstreamer-1.0.0.dylib

The bottle was wired for Preview, correctly, and the runs were launched with
stable. A staged codec reaches its libraries through symlinks into one
CrossOver's bundle, so loading it under another drags that engine's core in
beside the running one -- the exact crash `stage-codecs.sh` was written to avoid,
arrived at from the one direction it did not cover.

It is not the cause: removing it changed the frame rate and nothing else, and on
Preview -- where there is no duplicate, because the bottle points at Preview's
own staging -- the game fails identically with `GST_PLUGIN_PATH` emptied. But it
was underneath every stable-engine measurement taken before it was noticed, so
those are worth less than they were credited with at the time.

Two repairs came out of it, in `diagnostics/launch-with.sh` and
`stage-codecs.sh`, and they are written up in
[what we got wrong](what-we-got-wrong.md).

## What was eliminated on the loading screen

- **A controller.** A DualSense Edge was connected and `dinput8` frames jumped
  from 2 to 24 between two captures. With it fully powered off, verified at zero
  HID entries, the stall is identical.
- **Disk.** No failed open, no failed read, no short read, no read outstanding.
- **Audio.** `mmdevapi`, `winecoreaudio` and CoreAudio all load. The empty
  `SoundDeviceID` in P4G.ini is the game's own shipped default.
- **Steam.** `steam.exe` runs with `steam://run/1113000`; the process holds
  `steam_api64.dll` and `steamclient64.dll`.
- **Wine's partial `d3dx11_43`.** The native Microsoft build was overridden in
  and confirmed loaded from `system32`; unchanged.
- **The staged codec's version.** The plugin is gstreamer-libav 1.24.14 with
  FFmpeg 6.0; Preview's core is 1.28.5. That mismatch is real and worth closing
  on its own account, but emptying `GST_PLUGIN_PATH` on Preview changes nothing
  here.

## Why it was not chased further

The game reaches a window and stops with 46 of 48 threads parked in `ntdll`,
memory flat, roughly a tenth of one core busy. Nobody is working and nobody is
being woken.

Two tools failed on it, both for the same reason:

- `winedbg` cannot unwind stacks under Rosetta. It returns one or two frames per
  thread, which is not enough to name a wait.
- Import-table hooks do not see this binary. A probe watching every untimed wait
  recorded **one** call while 46 threads sat in waits; the same probe watching
  `VirtualAlloc` recorded no refusals at all. The protected executable reaches
  `ntdll` directly rather than through `kernel32`, so the usual hooking sees
  almost nothing.

Hooking `ntdll` itself inside a protected process is fragile and is
indistinguishable from tampering with it. That is the wall.

## What is worth reporting upstream

**The adapter describes itself three ways at once.** This is a real defect in
the translation stack, independent of this game, and it is written up in
[Findings](../wiki/Findings.md).

    adapter 0 from GetDesc: "AMD Compatibility Mode"
      vendor 0x10de   device 0x66af

`0x66af` is AMD's Radeon VII. `0x10de` is NVIDIA. The name says AMD. Two of the
three say AMD and the vendor id says otherwise -- and it is the vendor id that
software branches on. DXMT demonstrably does: it prints

    info:  Vendor extension enabled: NVEXT

and loads `nvapi64.dll` and `nvngx.dll` on a machine with no NVIDIA hardware
anywhere near it.

**And the engine build alone decides whether the protector kills the process.**
Same binary, same bottle: Preview terminates, stable does not. That is a
reproducible-to-the-byte difference worth handing to CodeWeavers, and it is the
most useful thing this investigation produced.
