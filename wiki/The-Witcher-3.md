# The Witcher 3: Wild Hunt

Patch 5.00b (28 September 2026) made the game DX12-only, and on D3DMetal it
stopped at a black window. Two separate faults, one DLL.

| | |
| --- | --- |
| Symptom | A black window at full CPU on every launch; once past it, DLSS listed as not supported |
| Cause | One stream-output pipeline crashes D3DMetal's shader converter; the game switches NVIDIA Streamline off when it finds Wine |
| Fix | Refuse that pipeline; tell the one Streamline check that there is no Wine |
| Backend | **D3DMetal**, D3D12, Metal 4 on |
| CrossOver | Stable 26.3 with Game Porting Toolkit 4.0b2 (2026-09-30) |

## The black screen

The diagnosis is [tholtman1-del/witcher3-crossover-fix](https://github.com/tholtman1-del/witcher3-crossover-fix)
(MIT), and the rule here is the same one. During startup the game creates one
graphics pipeline with a geometry shader that streams out and has no pixel
shader. Compiling it crashes Apple's shader converter (`libmetalirconverter`)
inside D3DMetal, and the game's own crash handler then deadlocks: a black
window at 100% CPU, and no crash report.

The fix refuses any pipeline with a geometry shader that streams out or has no
pixel shader, returning `E_INVALIDARG` before D3DMetal sees it. The game carries
on without it. Measured: exactly one pipeline is refused per launch (three
stream-output entries), and the game reaches the menu and plays. The effect it
belonged to may be missing somewhere in the game.

## DLSS

With the black screen gone, the graphics menu said DLSS was not supported. It
was not D3DMetal, and it was not NVAPI: D3DMetal presents the adapter with
NVIDIA's vendor id, and NVAPI initialises and reports driver 561.09.

The game itself decides. Before it creates Streamline it asks `ntdll` for
`wine_get_version`, and if that exists it never calls `slInit`. Everything after
follows from that: `slSetD3DDevice` answers error 21, "not initialised", and
the menu reports DLSS unsupported.

The game asks for `wine_get_version` in five places (six after the 2026-10-08
update). Only the call in front of Streamline is answered "no such function";
it is recognised by the instructions that follow it, not by an address. The
update moved the register that code compares (r14 became r13), so the match
now accepts any register. The other calls still see Wine. With that one answer, `slInit` returns 0, Streamline
loads `sl.common` and `sl.dlss`, reports DLSS and Ray Reconstruction as
supported, and DLSS runs through D3DMetal's MetalFX path and looks right.
Frame generation does not work.

## Ray tracing

Since the 2026-10-08 update the menu shows ray tracing off, even with
`EnableRT=true` in the settings. D3DMetal is not what says no: it offers the
game DXR tier 1.1.

The game decides again. One of its other Wine checks, the call that returns to
`witcher3.exe+0x1ee5630`, turns ray tracing off. Measured 2026-10-09 on
D3DMetal 4.0b2: answer that one call "no such function" and the ray-tracing
options come back. This fix does not do that, because ray tracing then
cannot run:

- The game builds a ray-tracing pipeline that grows as content streams in. One
  of its shaders uses a 64-bit atomic compare-exchange. Metal refuses to
  compile it: `Encountered unlowered function call to
  air.atomic.global.cmpxchg.weak.i64` (measured on an M4 Max).
- D3DMetal then aborts. The game shows a crash report, or hangs on the loading
  screen when the abort lands on a compile thread.
- Nothing in the menu avoids it. Global illumination cannot be turned off while
  ray tracing is on, and the game creates the same pipelines whatever the
  ray-tracing settings say. Refusing that pipeline makes the game crash instead.

On the 2026-09-30 build ray tracing ran on the same engine and toolkit, so that
shader appears to have come with the update. Ray tracing needs D3DMetal to
handle 64-bit compare-exchange, which is Apple's to do.

## bin/x64

The launch can still ask for `bin\x64\witcher3.exe`, the old DX11 build that
5.00b no longer ships, and then fails with "path not found". Measured here: the
game ran from `bin\x64`. The installer links `bin/x64` to `x64_dx12` when it is
missing. A folder already there, such as a copy made by hand, is left alone and
reported. A copy runs the game's original DLL instead of this fix, and goes stale
with the next update.

## Where it hooks

NVIDIA Streamline wraps the game's device. So as soon as `d3d12.dll` is loaded,
the DLL creates a device of its own and patches the two pipeline creators of
D3DMetal's device class, `CreateGraphicsPipelineState` and `CreatePipelineState`,
which the game's device shares. The Wine answer is a patch of the game's own
import of `GetProcAddress`.

It is carried on `amd_fidelityfx_loader_dx12.dll`, which the game imports, and
its five exports are forwarded to the renamed original. It logs to
`C:\w3-streamout-fix.log` in the bottle.

## Install

    runtime/install-w3-fix.sh "<…>/steamapps/common/The Witcher 3"
    runtime/install-w3-fix.sh "<…>/steamapps/common/The Witcher 3" --status
    runtime/install-w3-fix.sh "<…>/steamapps/common/The Witcher 3" --restore

Or pick the game in the app. Run it again after a game update or a file
verification, because either one puts the original DLL back.

Recommended in game: HDR off, NVIDIA Reflex off, frame generation off, VSync
off. Cap the frame rate in the launcher at 70, which holds a flat 60 on a
ProMotion display; an uncapped rate between two of the display's slots runs
faster on average and feels uneven. Measured 2026-09-30 on an M4 Max: 60 fps,
smooth, with DLSS and ray tracing on.
