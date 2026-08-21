# Taking this to winevideo

Notes for later. Nothing here is started.

The fixes in this repository are per-game, and they do not have to be. The
per-game part is only the **carrier** — the DLL each one rides in on. The
logic is not specific to either title.

## Why it is per-game today

Both fixes need to run inside the game's process before it does anything
interesting, and the only way in from outside is to be a DLL it already loads.
So each needs a carrier: `libogg_64.dll` for Unreal titles, `libxess.dll` for
DYNASTY WARRIORS: ORIGINS. That is the whole reason a user has to point an app
at a game folder.

## Three pieces, in order

### 1. Ship what exists, unchanged

winevideo already has the precedent: `build/d3d12-notifier-shim/` is a
standalone DLL with its own build script, installed from `install-vp9.sh`
alongside the gst plugins and registry work. `build/d3d12-video-bridge/` would
sit beside it with the same shape.

Still per-game, but it hands them a proven technique and a real case that is
black on their stack today.

### 2. Remove the per-game part — this is where the value is

Apple's GPTK DLLs are tiny:

| DLL | exports |
| --- | --- |
| `d3d11.dll` | 3 |
| `dxgi.dll` | 7 |
| `d3d12.dll` | 11 |

A proxy at the CrossOver level **exports `D3D11CreateDevice` and
`D3D12CreateDevice` itself**, so there is no import table to hook and no
argument with NVIDIA Streamline over who gets there first — which is the
detour that made the D3D12 device look unreachable from inside the game.

Same code, loaded globally. Any game with this shape is fixed without being
touched, and nobody selects a folder.

### 3. What not to attempt

A *generic* shared-resource bridge — making any D3D11 texture visible to D3D12
— is much harder than what is here. It needs to know when D3D12 is about to
read the resource in order to synchronise, and without D3DMetal internals that
means a readback and re-upload each time.

This works because it cheats: it intercepts the frame in Media Foundation, so
it knows when there is a new one because it produced it. That cheat is the
idea worth passing on, and it is worth being explicit that it is one.

## Attribution, when the time comes

`build/patches/SOURCES.md` is where winevideo records provenance, and one
thing there needs stating plainly: **their D3D9 bridge never manufactured a
handle.** `0008-d3d9-dxmt-video-bridge-handle.patch` creates a texture with
`D3D11_RESOURCE_MISC_SHARED`, calls the real `IDXGIResource::GetSharedHandle`,
and fails with `E_FAIL` if it does not work. What it substitutes is Wine's
*unimplemented* D3D9 sharing with D3D11 sharing that DXMT *does* implement.

The one call their design rests on is exactly the one that returns `E_NOTIMPL`
under D3DMetal, so the sentinel handle here is original work against public
Microsoft APIs, not a port of theirs. What did come from them is the upload
recipe in `0009` — `UpdateSubresource`, then a `D3D11_QUERY_EVENT` waited on
with a deadline rather than a bare `Flush`.

## Also worth telling them

DXMT implements `GetSharedHandle`; D3DMetal does not
(`dxmt/src/d3d11/d3d11_texture_device.cpp:288`). A game in this position may
simply work under a different backend, which is worth knowing before anyone
writes a bridge.
