# Marvel's Spider-Man 2

**This is macprotips' fix, plus one improvement of ours.** The diagnosis, the
raw views and the four rewritten shaders come from
[SpiderMan2Fix](https://github.com/macprotips/SpiderMan2Fix) by macprotips,
public domain, and their code ships here unchanged in what it does. This
project adds two things: a carrier that needs no DLL override, and a barrier
that removed brief black blocks in the sandstorm stage.

| | |
| --- | --- |
| Symptom | Characters stay in a T-pose; the symbiote shows up as spikes, a flat black sheet, or not at all; brief black blocks flash in the opening sandstorm stage |
| Cause | On M1-M4 Macs, D3DMetal returns 0 for typed buffer reads past element 2^28 and drops typed writes past it. The black blocks come from a missing barrier after the volumetric fog copy |
| Fix | macprotips': the wide R32 views made raw, and four shaders rebuilt raw from your own exe. Ours: no DLL override, and a UAV barrier after `CS_VolumetricFogCopy` |
| Backend | **D3DMetal**, D3D12 |
| CrossOver | Preview 27, through NotProton; M5 Macs do not have the limit |

## The T-pose and the symbiote (macprotips)

The game keeps every character's mesh in one 2 GB pool and reads it through
typed R32 views of 536,346,624 elements. Past element 2^28, D3DMetal returns
zeros, so the meshes stored in the upper half are never skinned and those
characters stay in a T-pose. That is why it looked random. The symbiote's
triangles are written into the same pool every frame through a typed UAV of the
same size, and the writes past 2^28 are dropped.

macprotips' fix creates those views raw, which work to the end of the pool, and
replaces the four shaders that use them with copies whose accesses are raw:
`CS_ModelSkinning`, a vertex morph, and the symbiote's two spline-mesh index
passes. The copies are built on your Mac from your own `Spider-Man2.exe`, with
Microsoft's shader tools at the DXC commit they verified, and each one must hash
to the shader they verified in the game or nothing is installed. No game files
ship with the fix.

## The black blocks (ours)

With all four shaders in place, small black blocks still flashed for an instant
now and then in the sandstorm stage. They are not about the pool. vkd3d-proton
adds a barrier for this game after one compute shader ("Add missing barrier in
Spider Man 2"), and that shader is in the current build: `CS_VolumetricFogCopy`,
which copies the volumetric fog volumes. Without the barrier, the next pass can
read the fog half written.

The DLL adds one global UAV barrier after every dispatch made with that pipeline.
In one session the copy ran 8030 times, each followed by the barrier, and no
blocks were seen. Without it they showed up sporadically. That is one clean
session, so it is evidence rather than proof. `mgvf_fog_barrier = 0` in
`sm2fix.ini` turns it off.

## Where it hooks (ours)

macprotips' fix is `d3d12.dll`, so every launcher needs a per-game DLL override.
Here it rides on `amd_ags_x64.dll`, which the game imports and no runner ships,
so it loads under CrossOver, RaccoonBot or NotProton with nothing set. The game
loads `d3d12.dll` later. A loader notification catches that and points its
`D3D12CreateDevice` export at this DLL, which then hooks D3DMetal's device class
as macprotips' does.

## Install

    runtime/install-sm2-fix.sh "<…>/steamapps/common/Marvel's Spider-Man 2"
    runtime/install-sm2-fix.sh "<…>/steamapps/common/Marvel's Spider-Man 2" --status
    runtime/install-sm2-fix.sh "<…>/steamapps/common/Marvel's Spider-Man 2" --restore

Or pick the game in the app. The first install downloads macprotips' release
once (sha256 pinned) to take Microsoft's shader tools from it, and caches them.
After a game update the fix stands down until it is installed again.
