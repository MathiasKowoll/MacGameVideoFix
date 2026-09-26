# CONTROL Resonant

Two faults, one fixed by a setting and one hidden rather than fixed. The game
plays through on D3DMetal; what it loses is the lit rooms behind the city's
windows, which show as dark glass.

| | |
| --- | --- |
| Symptom | Faces and hair render as black boxes; the lit rooms behind windows show as flat violet, cyan and yellow, spreading to neighbouring windows as the camera moves |
| Cause | Faces: Metal 4 on. Windows: one pixel shader, `interiorPS`, whose non-uniform texture read D3DMetal runs as uniform, plus a second fault, outside the shaders, that shows up after about a minute of play |
| Fix | Metal 4 off for this title; the pipelines built with `interiorPS` are created writing nothing |
| Backend | **D3DMetal**, D3D12 |
| CrossOver | Stable 26.3 with Game Porting Toolkit 4.0b2 (2026-09-26) |

## Faces: Metal 4 off

With D3DMetal's Metal 4 path on (the default in 4.0b2), faces, skin and hair
render as black boxes. Metal 4 off (`D3DM_MTL4=0`) cures all of them. It needs
nothing else: `D3DM_SAMPLE_NAN_TO_ZERO` and `D3DM_FLUSH_POS_INF_TO_NAN` each
remove part of the damage with Metal 4 on, and neither does all of it.

Keep ray tracing and path tracing off in the game's own settings. With either on
the title crashes on this stack.

## Windows: the shader

Found by muting pipelines, eleven rounds of them, until one pixel shader alone
removed every wrong colour from every building at every angle. Its entry point
is `interiorPS`. It turns a view direction into panorama coordinates and reads a
room image out of an unbounded `Texture2D[]`, with the index marked
`NonUniformResourceIndex`: neighbouring pixels are meant to read different
rooms.

A rebuilt `interiorPS` that serves each distinct index in its own pass drew the
rooms as they look on Windows. That shows D3DMetal runs the read as if the index
were uniform, with one pixel's room used for the whole group. The "spreading" the
player sees is exactly that. The rebuilt shader was not enough, though. After
about a minute the colours came back even with every non-uniform shader in the
frame rebuilt, so a second fault lies outside the shaders. Texture streaming is
the suspect. It has not been measured.

A shader substitution that holds for one minute is not a fix, so the shipped DLL
does the one thing that measured clean for a whole session: it keeps
`interiorPS` from drawing. Every pipeline built with it is created with each
render target's write mask at 0. The rest of the frame is untouched.

The match is on a hash of the whole shader. An update that changes the shader
changes the hash, and the DLL then matches nothing and does nothing.

## Where it hooks

NVIDIA Streamline wraps the device the game creates, so the game's own device is
never visible to a proxy. The DLL waits for `d3d12.dll`, creates a device of its
own three seconds later, patches the two pipeline creators of D3DMetal's device
class (`CreateGraphicsPipelineState`, `CreatePipelineState`), and releases the
device. The patch is on the class, so it covers the game's device too. Nothing
inside Streamline is touched.

It is carried on `PxFoundation_x64.dll`, which the game imports; every export is
forwarded to the renamed original. It logs to `C:\ctl-window-fix.log` in the
bottle, and two minutes in it writes how many pipelines it hid.

## Install

    runtime/install-ctl-fix.sh "<…>/steamapps/common/CONTROL Resonant"
    runtime/install-ctl-fix.sh "<…>/steamapps/common/CONTROL Resonant" --status
    runtime/install-ctl-fix.sh "<…>/steamapps/common/CONTROL Resonant" --restore

Or pick the game in the app.

The investigation, round by round, with every candidate shader, is in
`diagnostics/ctl-window-shaders.md`.
