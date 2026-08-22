# Nioh

Koei Tecmo's in-house engine, and the second title fixed by the bridge written
for [Persona 5 Strikers](Persona-5-Strikers.md) — with no change to what that
bridge does.

| | |
| --- | --- |
| Video | WMV3 in ASF, played through DirectShow rather than Media Foundation |
| Played by | `IGraphBuilder` into VMR9, presenting onto a shared D3D9 surface |
| Symptom | `Failure to play movie. (RTM_ID_EV0001)`, then a crash |
| Fix | Stage the WMV3 decoder, then hand the game a share handle that exists |
| Backend | **DXMT only.** The sidecar needs `GetSharedHandle`, `E_NOTIMPL` under D3DMetal |
| CrossOver | `crossover-preview-arm64-20260821`. Not tried on 26.3 |

## Two faults, one behind the other

The title failed twice over, and the first fault hid the second.

**The decoder was never there.** Nioh's cutscenes are WMV3, which no CrossOver
ships. Wine's chain for it runs `qasf` → `wmvdecod.dll`, a shim → winegstreamer's
`wg_wmv_decoder`, which probes a fixed WMV3-to-I420 transform before it will
construct anything. With nothing able to serve that, `IDMOWrapperFilter::Init`
returned `0xD0000001` and the graph never built. That is the message on screen.

Staging `libgstlibav` beside the game supplies it, exactly as for Strikers. The
staging turned out to be broken for everyone in a way described under
[Findings](Findings.md) — a missing `libgsttag` meant the plugin had never
loaded at all — and once fixed, `Init` returned `S_OK` and the full DirectShow
graph built.

**Then it crashed instead.** With the decoder in place the game reached a point
it had never reached before, and died on a worker thread at
`mov rdx, [rdx+0x10]` with `rdx` at zero.

That null has a source. The game asks D3D9 for a shared render target; Wine
creates it, returns `S_OK`, and hands back a share handle of zero. The game
takes the handle at face value and dereferences it.

    CreateRenderTarget(1920x1080 fmt=21, SHARED requested) -> S_OK, handle 0
      << succeeded but handed back no handle: nothing to share

## The fix is one slot number

The Strikers bridge already knew what to do with that: create a real texture on
the D3D11 side, take a real shared handle from it, and give the game that
instead of the null. What it did not know was where Nioh creates its device.

The bridge watched `IDirect3D9::CreateDevice`, slot 16. Nioh goes through
`Direct3DCreate9Ex` and creates its device with `CreateDeviceEx`, slot 20 — so
the bridge armed a vtable the game never called, logged two lines, and watched
the cutscene fail from the sidelines. The crash in that run was the game's own;
no call had reached bridge code at all.

With slot 20 hooked, the whole path runs:

    IDirect3D9Ex::CreateDeviceEx -> S_OK
    sidecar: 1920x1080 texture, GetSharedHandle -> S_OK, handle 40000082
      handed the game a real shared handle instead of null
    OpenSharedResource: our handle -- made a texture on the game's own device
    StretchRect INTO it   << ON OUR SHARED SURFACE
    source luma [420]: average 11, range 0..166   << has picture

Slot 20 is written only on objects that came from `Direct3DCreate9Ex`; on a
plain `IDirect3D9` that index is past the end of the vtable.

The luma readings are the reason this is recorded as fixed rather than as
reported fixed. A bridge that hands over a valid but empty surface gives a game
that runs and a screen that stays black, and nothing distinguishes the two from
outside.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on Preview and on DXMT only. 26.3 has not been tried. Under D3DMetal the
sidecar has nothing to build on — `GetSharedHandle` is `E_NOTIMPL` there — so it
is not expected to work, but that has not been run either.

[Nioh 2](Nioh-2.md) turned out to use the same path and is fixed by the same
bridge, with no code change and even the same built proxy.

## What it cost

Three runs. The first arrived at the crash with the bridge watching the wrong
slot and produced a two-line log, which was enough to establish that the crash
was not ours. The second, with `CreateDeviceEx` hooked, armed the device and
reached gameplay. The third reached a cutscene and played it.

The independent confirmation that this was the right shape came from
[winevideo](https://github.com/Jfishin/winevideo), which names Nioh, Nioh 2 and
Persona 5 Strikers as one shared-texture cutscene path and repairs it with a
bridge and a sidecar of its own — inside a patched Wine `d3d9.dll` rather than
from the game process. The comparison is under *Living outside CrossOver* in
[Findings](Findings.md).
