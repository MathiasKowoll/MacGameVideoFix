# RESONANCE: A PLAGUE TALE LEGACY

Refuses to start. What stops it is one comparison, and everything either side of
it is working correctly.

| | |
| --- | --- |
| Engine | Asobo, in-house — no Unreal strings in the binary, ten `ASOBO_` ones |
| Symptom | `Fatal error — Shader Model 6.7 is not supported by this device!` |
| Fix | Lower the shader model floor to 6.6, in memory |
| Carrier | `NvCloth_x64.dll` → `NvCloth_x64_real.dll`, 42 forwarders |
| Backend | D3DMetal, D3D12 |
| Display | **Must be 16:9.** Nothing else in this project can supply that |
| CrossOver | 26.3 (`cxoffice-26.3.0rc2`), Game Porting Toolkit 3.0 and 4.0b2 alike |

## The floor, not the device

D3DMetal reports Shader Model 6.6, because that is what it has. The title wants
6.7 and will not start below it:

    8B 85 B8 07 00 00     mov  eax, [rbp+7B8h]     ; the highest model it has
    83 F8 67              cmp  eax, 67h            ; 6.7
    0F 8D 8C 00 00 00     jge  past_the_error

`67h` becomes `66h` at run time and the check passes. Its own store page asks
for 6.6 anyway.

**Nothing on disk is touched.** `Resonance.exe` stays as Steam installed it, so a
verification does not undo this and an update does not fight it — which is the
whole difference between this and the hex edit going around. That edit also
names an address, and the address is per-build: `0x15C73E6` holds `FFh` in the
copy this was written against, where the comparison is at `0x15CD606`. Found by
pattern instead, and refused unless exactly one place in `.text` matches.

What it costs is known and small: some puzzle textures and effects are missing,
because the title does have 6.7 paths and this keeps it off them. That is the
same trade the community fix makes.

### The wrong way to do it, which was tried first

Hooking `CheckFeatureSupport` and handing back the model that was asked for
makes the dialog go away and the game run to a black screen at five hundred
draws a frame. Told 6.7 exists, it takes its 6.7 paths, and those shaders do not
compile on a device that has 6.6. Lowering the floor keeps it on the paths it
can run; raising the device buys a launch and pays for it later, somewhere with
no error message.

## It also needs a 16:9 display

Measured on a 3456x2234 panel — 1.547:1, which is every Apple laptop. With the
floor lowered the title starts, loads 2.5 GB of textures, renders four to five
hundred draws a frame at eighty frames a second, answers input, plays its menu
sounds, and **shows nothing at all**. It filters the display mode list for 16:9,
finds none, and composes into a region that is never presented.

At 1920x1080 it plays. This is why the same title runs for people on 16:9
monitors with the same D3DMetal and nothing but the byte, and why the community
fix reads as complete to them and does nothing here.

Set the display with the game's own graphics options, a tool like BetterDisplay,
or a CrossOver virtual desktop. Its command line accepts `-width`, `-height`,
`-windowed` and `-borderless`, and they were not honoured when tried.

## The videos decode, arrive, and are never drawn

Not a video fault, and worth writing down because establishing it took an
evening. The logo and tutorial MP4s never appear at any resolution. They are
H.264 High in MP4 with AAC — the most ordinary file there is — and the whole
path works:

    MediaEngineClassFactory::CreateInstance -> 0x00000000
    OnVideoStreamTick: asked 1200 times, a frame was ready 428 of them
    TransferVideoFrame -> ok (360 so far)       0 failures

The title plays them through `IMFMediaEngine`, created over COM, which is why
nothing saw them for hours: it never calls `MFTEnumEx` and never creates a
source reader. Underneath, `mfmp4srcsnk` demuxes and `winegstreamer` decodes,
both measured present in the process and both working. The frames reach a
texture of the game's own without one error.

Then they are never drawn. Proved by painting rather than by return code, which
had said `S_OK` three hundred and sixty times while the screen was black: with
the surrounds of every transferred frame filled opaque magenta — forty percent
of the surface, in a colour the game does not contain — nothing appeared. What
happens after `TransferVideoFrame` is inside Asobo's engine, and there is
nothing here to hook.

## Ruled out, so nobody repeats it

Each of these cost a launch, and none of them is the fault: MetalFX and its
temporal scaling; the Metal 4 backend; the video memory budget, where the
adapter reports 76 GB and the zero in the game's own crash report turns out to
be its own number; D3D11 against D3D12; Game Porting Toolkit 3.0 against 4.0b2;
the Steam overlay; and patching `Resonance.exe` on disk exactly as the community
fix says, with no DLL of ours loaded, on two engines — black either way, which is
what finally established that our byte and theirs are the same byte and the
remaining fault was the display.

The white line at the window edge is not this fix: it is still there with the
executable untouched and nothing of ours loaded.
