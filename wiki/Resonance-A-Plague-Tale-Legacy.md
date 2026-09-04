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
| Display | Any. The desktop resolution is fine -- see the retraction below |
| Second fix | **`HDR10 0`** in `ENGINESETTINGS`, or it starts to a black screen |
| CrossOver | Stable 26.3 (`cxoffice-26.3.0rc2`), Game Porting Toolkit 3.0 and 4.0b2 alike |

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

## The black screen was HDR, not the aspect ratio

**Retracted, 2026-09-03.** This page said the title needed a 16:9 display and
that no fix here could supply one. That was wrong, and the section it replaced
is kept below so the reasoning that produced it stays visible.

The title writes its own settings to
`AppData/Roaming/Resonance A Plague Tale Legacy/ENGINESETTINGS`, and on a
display it reads as HDR it turns HDR10 on by itself:

    Adapter "AMD Compatibility Mode"
    Resolution 2560 1440
    HDR10 1                 <- written by the game, nobody set it

Set that line to `HDR10 0` and the title starts and draws at the desktop
resolution. No 16:9, no 1920x1080, no virtual desktop. Measured on a
Liquid Retina XDR panel, which is HDR and is what triggers it.

The file is created on first run, so it can be edited straight after the first
launch. Once the graphics options are how you want them the file can be made
read-only, and the title stops rewriting it.

**What was actually established, and what was not.** That HDR10 off is
sufficient at native resolution is measured. *Why 1920x1080 also worked* is not:
the plausible reading is that changing the mode made the title renegotiate the
display and drop HDR10, so the resolution was a way of turning HDR off without
knowing it. That is a hypothesis; nobody has watched the file across a mode
change. What is certain is that the aspect ratio was never the requirement, and
a page that said so sent people to buy a monitor for a one-line setting.

### What this replaced, kept for the reasoning

Measured on a 3456x2234 panel -- 1.547:1, which is every Apple laptop. With the
floor lowered the title started, loaded 2.5 GB of textures, rendered four to
five hundred draws a frame at eighty frames a second, answered input, played its
menu sounds, and **showed nothing at all**. At 1920x1080 it played. From that it
was concluded that the title filtered the display mode list for 16:9, found
none, and composed into a region that was never presented -- and that this was
why the same title ran for people on 16:9 monitors with the same D3DMetal and
nothing but the byte.

Every one of those observations still stands. The conclusion drawn from them did
not: the same black screen has a cause that a resolution change happened to
clear, and the mode list was never shown to be filtered.

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
