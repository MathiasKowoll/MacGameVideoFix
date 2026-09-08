# Engine source patches

`scripts/build-winegstreamer.sh` builds `winegstreamer.dll` and `winegstreamer.so`
from a CrossOver source tree with a chosen set of patches applied, and the pair
is installed into the engine. This file records **which patches, whose they are,
and why each one is applied**, because until now that was only visible as a list
of numbers on a command line.

    scripts/build-winegstreamer.sh --patches 0002 0003 0006 0008 mgvf-0001

Every built pair records the set that produced it. `runtime/engine-payload/`
carries `built-for.json`; beside `runtime/install-engine-media.sh` there is one
`engine-built-for*.json` per set -- `engine-built-for.json` and
`engine-built-for-stock.json` today -- each naming the engine it was built for,
and the installer chooses between them by the name of the engine it is pointed
at. All of them record the patch set above.

`scripts/build-controller-bus.sh` builds a **second, optional set** from the
same tree, with `mgvf-0002`, `mgvf-0003`, `mgvf-0004` and `mgvf-0005` applied
on top: `winebus.sys`, `setupapi.dll` and `ntoskrnl.exe`, three PE files and no
unix half. It is stamped apart, in `runtime/engine-controller-built-for.json`
beside `runtime/install-engine-controller.sh` and mirrored as
`runtime/engine-payload-controller/built-for.json`, and that stamp records only
those four. The media stamps above record only the media patch set, and that
stays so: the four are not applied to the winegstreamer pair and the pair is
not rebuilt when they change.

`build-winegstreamer.sh` resolves each number here first, and only then in the
fallback directory `MGVF_PATCHES` names, which is where winevideo's patches sit
when that project is installed. Until these files existed the fallback was the
only place it looked, so the build could not run on a machine without that
project installed -- and the command written above, with `mgvf-0001` in the list,
aborted unless the variable had been exported by hand.

## Whose patches these are

**0002, 0003, 0006 and 0008 are winevideo's**, from its 0.5.0 series of 35. They
are **carried here, unchanged, with their provenance and their licence**, so that
building this engine does not require that project to be installed. Each
file opens with a header saying it is not ours. They modify Wine, which is
LGPL-2.1-or-later, and a patch to LGPL code carries the same terms -- which is
what makes carrying them permitted. Take them from winevideo rather than from
us if you want them: theirs is where they are maintained, and these are a
snapshot that will go stale.

The summaries below are ours, written from their commit messages; the messages
themselves are the authority.

**`mgvf-0001` is ours**, and lives here in full. It opens our own series, kept
deliberately apart from winevideo's numbering: a patch of ours numbered `0036`
read as the next one of theirs, which is how the first question anyone asked
about it was where to find it in their repository. Ours are `mgvf-NNNN` and
theirs are `NNNN`, and the two can never be confused again. See
`mgvf-0001-winegstreamer-2D-capable-media-source-samples.patch`.

**`mgvf-0002`, `mgvf-0003` and `mgvf-0004` are ours too**, written on 2026-09-08
for one fault between them. They touch `winebus.sys`, `setupapi.dll` and
`ntoskrnl.exe`, not winegstreamer, and go into the optional controller-bus set
rather than the media pair. Each opens with the same header: where it came
from, the fault, the change — and that nothing in it is specific to this
project. **`mgvf-0005` is ours as well**, from the same day and in the same
set, and is the opposite of the three: where they tell a client the truth
about the bus, it lets one pad lie about it, per device, off by default, for
the two consumers that turned out not to want the truth.

## What each one is for

### 0002 — VP9/AV1 caps mapping, decoder input types
Touches `video_decoder.c` and `wg_media_type.c`. Groundwork for the VP9 decoder
that 0003 then advertises: without it the caps mapping has nowhere to land.
0003 does not apply without it.

### 0003 — a real VP9 decoder MFT, advertised through MFTEnumEx
Adds `CLSID_wg_vp9_decoder`, mirroring the h264 MFT, and wires it into
`mfplat.c`'s class objects. Its message names the symptom it exists for: a game's
VP9 capability probe finds no decoder and shows *"Failed to Play VP9"*. Their
message names NINJA GAIDEN 4 as the probe they saw, and that title is here too --
though what answers its probe in this project is its own carrier DLL rather than
this patch, and what was actually missing for it was a demuxer.
`wiki/Ninja-Gaiden-4.md` records what was measured.

### 0006 — drop D3D awareness on macOS
No macOS backend can create NV12 D3D11 video textures, so D3D-bound decoder
output can never be displayed. CrossOver's own workaround hides NV12 from
`GetOutputAvailableType` instead, which starves consumers that drive an
`IMFTransform` directly and require NV12: they enumerate output types, find none,
and abandon playback. That is the black-cutscene-with-audio shape, and it is why
UE Electra titles here need this patch.

### 0008 — always provide 2D-capable output samples on macOS
UE Electra queries a decoded frame's media buffer for `IMF2DBuffer` and rejects
the frame when it is absent — which it always was, because without a D3D manager
the decoder filled a caller-provided plain memory buffer. Sets
`MFT_OUTPUT_STREAM_PROVIDES_SAMPLES` unconditionally on macOS and routes output
through the sample allocator, whose system-memory buffers do implement it.

### mgvf-0001 — the same, for the media source *(ours)*
0008 covers the decoder **MFT**. A title that resolves its own byte stream never
reaches that MFT: winegstreamer's **media source** produces its own samples, and
those were plain memory buffers. METAL GEAR SOLID: Peace Walker asks such a frame
for `IMF2DBuffer2`, gets `E_NOINTERFACE`, does not check the HRESULT, and
dereferences the NULL.

The buffer for a video stream now comes from `MFCreateMediaBufferFromMediaType`,
which is 2D-capable when the media type carries a subtype and a frame size and
falls back to a plain buffer when it does not -- the previous behaviour exactly.
Two guards ride along: a locked linear view SMALLER than the parser produced
sends that frame back to a plain buffer rather than overrunning the allocation,
and a LARGER one has its tail filled with luma 0 and chroma 128, because a fresh
allocation is not blank and zeros are bright green in YUV. Audio stays on
`MFCreateMemoryBuffer`: it has no geometry and nothing asks it for the interface.
Full evidence is in the patch's own header.

A green band along the bottom of the frame remains in some of that title's
cutscenes. It is not caused by this patch -- measured from inside the process,
the buffer's current, maximum and contiguous lengths all agree, so there is no
unwritten tail for the fill above to touch -- and it is unexplained.

### mgvf-0002 — winebus names the bus in a device's compatible ids *(ours)*
Under wine, winebus reported `WINEBUS\WINE_COMP_HID` and
`WINEBUS\WINE_COMP_XINPUT` as a device's compatible ids and nothing else,
whatever the bus. Every Windows client decides USB against Bluetooth the same
way — hidapi asks the HID device's parent devnode for its compatible ids and
searches them for `BTHENUM` — so that search could never succeed, and Steam's
log said *"bluetooth 0"* for a DualSense that was on Bluetooth. Its driver then
stayed in the USB format, whose output report the pad refuses silently: no
rumble. `get_compatible_ids` now prepends the id a real Bluetooth HID child
carries, `BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}`, when the device's
bus is Bluetooth, and `USB\Class_03` when it is USB; the two old ids follow, so
nothing that matched on them changes. winebus already knew the bus from IOKit's
Transport property and used it only for the PnP prefix and the Bluetooth input
fixups.

### mgvf-0003 — setupapi: `CM_Get_Parent` for HID children *(ours)*
`CM_Get_Parent` was a stub: FIXME, no parent, `CR_NO_SUCH_DEVNODE`. It is the
third call of hidapi's four-call bus detection, and the one that stopped it.
Wine's device tree is flat, but hidclass names a HID child after the bus PDO it
hangs from, so the parent of `HID\VID_x&PID_y[&...]\<inst>` is whichever of
`BTHENUM|USB|WINEBUS\VID_x&PID_y\<inst>` exists under `Enum` — winebus registers
them there with their `CompatibleIDs`, which setupapi already answers.
`CM_Get_Parent` looks the bus PDO up that way and allocates a devnode for it.
Only HID children are known; any other devnode keeps the old answer, FIXME and
all.

### mgvf-0004 — ntoskrnl refreshes a device's ids on every enumeration *(ours)*
With the first two in place a `+setupapi` trace showed Steam asking for the
parent, getting it, reading its `CompatibleIds` — and still logging
*"bluetooth 0"*. The value it read was the old one: `enumerate_new_device` wrote
`SPDRP_HARDWAREID` and `SPDRP_COMPATIBLEIDS` only inside
`install_device_driver`, which runs only when the `Enum` key has no driver yet,
that is, the first time a device is ever seen. Every later boot kept the record
of that first boot, whatever the bus said now. It now asks the bus for both on
every enumeration and writes them before the driver question, which is what
Windows's PnP manager does. **The three only work together**: this one exists
because the first two alone still read the first boot's record.

### mgvf-0005 — winebus presents a DualSense on Bluetooth as if it were on USB, on request *(ours)*
The three above tell a client the truth about the bus, and two consumers
turned out not to want it. Sony's libScePad (1.0.4.1) decides USB against
Bluetooth from the HidP capabilities alone — feature caps on usage page
`0xFF00` are the USB descriptor — and then writes output report `0x02` only,
the USB format, which a pad on Bluetooth does not have: hidclass refuses the
write and the library drops the pad. Steam decides from hidapi's flag and the
title's store categories, and for a title with 57 ("PS5 controller support")
but not 58 ("PS5 support over Bluetooth") shows its *plug in your controller*
dialog whenever the flag says Bluetooth. Both are content with a pad that
looks wired, so this patch makes one look wired, per device and only when
asked: `UsbEmulation` under
`HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>`. The pad
is created with bus type USB, its report descriptor is replaced at start by
the USB one, and every report is translated at the boundary — input `0x31/78`
to `0x01/64`, the short `0x01/10` spread into the USB layout after a one-shot
feature read that makes the pad stop sending it, output `0x02` packed into a
`0x31/78` with sequence byte and CRC-32, feature reads with their CRC tail
zeroed, feature writes with a CRC appended. An optional `ProductId` presents
another DualSense's id. Both DualSense product ids are served: the plain pad's
289-byte USB descriptor and the Edge's 405-byte one are each read from that
pad on a cable and built in, so `UsbEmulation` works on either, and so does
presenting an Edge as `0x0ce6` for a client that knows only the plain pad.
Output `0x02` is 48 bytes on the plain pad and 64 on the Edge; the `0x31` is
78 bytes either way and the effects block sits at the same offsets, so one
translation serves both. The byte layouts live in a new file,
`dualsense_usb.c`, with no wine includes, and a host test beside the
descriptors in the build directory checks the packing byte for byte against
packets Steam itself wrote to the pad. Off by default: without the value the
shipped `winebus.sys` behaves exactly as before. What it does not cover — a
DualShock 4, deliberately, and the driver half has not yet run against a live
pad — and the risks are in the patch's own header; how to turn it on is in
`runtime/engine-payload-controller/README.md`.

## If another of their patches is ever needed

The remaining 31 are not applied here, and several address titles this project
also carries. Adding one means: name it in `--patches`, rebuild the pair, refresh
`runtime/engine-payload/` and every `built-for` record that describes it, and
**add a section above saying what it is for and which title needs it**. A patch
that is applied but not described here is indistinguishable from one applied by
accident.

The optional set has records of its own to refresh: `runtime/engine-controller-built-for.json`
and `runtime/engine-payload-controller/built-for.json`, both written by
`scripts/install-controller-build.sh` from the build's `controller-built-for.json`,
and the table in `runtime/engine-payload-controller/README.md` that
`check-builds.sh` reads. A patch added to that set is named in
`build-controller-bus.sh`'s own list rather than in `--patches`, and the
sentence above about describing it here applies just the same.

They also do not all apply standalone. 0003 needs 0002, and 0008 needs its
predecessors; testing one against a pristine tree reports a failure that says
nothing about the patch. Apply them in ascending order, as the build does.

## Credit

The winevideo project is at <https://github.com/Jfishin/winevideo>. The four
patches above are its work and its findings; this project applies them and says
so. Nothing here should be read as those patches originating in MacGameVideoFix.
