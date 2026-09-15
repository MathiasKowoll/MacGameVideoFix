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
same tree, with `mgvf-0002`, `mgvf-0003`, `mgvf-0004`, `mgvf-0005`,
`mgvf-0006`, `mgvf-0007`, `mgvf-0008` and `mgvf-0009` applied on top: `winebus.sys`,
`setupapi.dll`, `ntoskrnl.exe` and `winebus.so` — three PE files and, since
`mgvf-0006`, the unix half of winebus as well. It is stamped apart, in
`runtime/engine-controller-built-for.json` beside
`runtime/install-engine-controller.sh` and mirrored as
`runtime/engine-payload-controller/built-for.json`, and that stamp records only
those eight. The media stamps above record only the media patch set, and that
stays so: the eight are not applied to the winegstreamer pair and the pair is
not rebuilt when they change.

The set grew a unix half because `mgvf-0006` is in `bus_iohid.c`, which is unix
code. Both halves of winebus then ship together: they are built from one tree
and read one struct, so an engine on our `winebus.sys` and CodeWeavers'
`winebus.so` is not a combination anyone should be running.

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

**Two numbers are missing and that is deliberate.** `mgvf-0013` and `mgvf-0015`
were both written and both discarded before they were ever built into anything
that shipped — `mgvf-0015` blamed Windows.Gaming.Input for a fault whose binary
turned out not to reference it at all. Neither number is reused, so a number
means one thing in every note and transcript that mentions it. A gap here is a
patch that was tried and abandoned, not one that was forgotten.

**`mgvf-0002`, `mgvf-0003` and `mgvf-0004` are ours too**, written on 2026-09-08
for one fault between them. They touch `winebus.sys`, `setupapi.dll` and
`ntoskrnl.exe`, not winegstreamer, and go into the optional controller-bus set
rather than the media pair. Each opens with the same header: where it came
from, the fault, the change — and that nothing in it is specific to this
project. **`mgvf-0005` is ours as well**, from the same day and in the same
set, and is the opposite of the three: where they tell a client the truth
about the bus, it lets one pad lie about it, per device, off by default, for
the two consumers that turned out not to want the truth. **`mgvf-0006` is ours
too**, from the same day and the same set, and is the first of ours to touch
the unix half of a driver rather than the PE half: it stops wine and macOS
writing to the same pad at once. Unlike `mgvf-0005` it is **on by default**,
because it repairs a defect rather than offering a behaviour. **`mgvf-0007` is
ours too**, and is the only one of the set that exists because of another one
of ours: it is the bill for `mgvf-0005`'s lie, and takes back out of a report
the audio settings a client asks for only because it has been told the pad is
wired. **`mgvf-0008` is ours too, and is the only one here that is not a fix**:
it is an experiment, marked as one wherever it is named, and it is in the set
because the way to run it is to ship it and read the next trace. **`mgvf-0009`
is ours too, and is neither a fix nor an experiment but a preference**: it
rewrites the vibration a client asks for, off unless a registry value asks for
it, on the strength of one person reporting that the other of the pad's two
rumble paths feels stronger.

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

### mgvf-0006 — winebus seizes a DualSense on Bluetooth *(ours)*
macOS drives a connected DualSense itself: WindowServer's
`com.apple.GameController.HID:DualSense` driver opens the pad every time it
appears and writes Bluetooth output reports to it. winebus opened the same pad
**shared** — `IOHIDDeviceOpen(IOHIDDevice, 0)` in `bus_iohid.c` — and wrote its
own. A pad on Bluetooth has one output pipe, and with two writers on it macOS's
writes time out. Measured from macOS's own log on 2026-09-08: 163
`kIOReturnTimeout` failures in a day, every one inside a minute in which a game
was running under wine, 4 to 35 a minute; after a burst its driver tore itself
down and 160 ms later bluetoothd dropped the link with `reason 10719`, six times
in six minutes across two launches, against spread-out `10722` drops earlier
the same day. That log has since left the unified log and cannot be re-read.

**Corrected on 2026-09-14: both reasons were misread.** `10722`, with HID
reason 436 and an L2CAP Disconnect sent by the host, is not the pad's idle
power-off but **the Mac closing the link**: before every retained one
WindowServer logs *"isIdle for N seconds - will disconnect if permitted"* and
*"disconnectIfIdle disconnecting..."*, then gamecontrollerd asks bluetoothd to
disconnect. Disassembled, `GCGamepadHIDServicePlugin` (GameControllerIO, loaded
in WindowServer) disconnects a Bluetooth Classic pad once more than 900.0 s,
hard-coded, have passed since the last button or stick change its own input
handler parsed; the DualSense plugin's *"if permitted"* is only
`isBluetoothClassic`. No preference changes it, and it also disconnects on
system sleep. `10719`, with HID reason 431 and SDP `STATUS 719`, is **the link
ended from the pad's side**: no host request, no host L2CAP Disconnect. (431 as
a remote L2CAP disconnect request and 719 as 700 plus HCI `0x13`, Remote User
Terminated Connection, is a reading of AOSP's `oi_status.h` names, which also
appear as strings in bluetoothd; the numeric table was not read from Apple's
binary.) Every traced unrequested removal ended with the PS button held
continuously for 4.98–5.00 s up to the last input report, and on 2026-09-14,
with no wine process running, holding PS under macOS alone produced the same
431 and 10719, the pad reconnecting by itself 2.7 s later. So holding PS drops
a Bluetooth DualSense regardless of wine, Steam or the seize. Whether the
2026-09-08 drops were PS holds is unknown; that the seize prevents them was
never measured; under the seize no `kIOReturnTimeout` has been logged in any of
seven disconnect windows checked. Two further `10719` drops, on 2026-09-13 at
03:21:07 and 22:42:21, came with no trace and no wine activity logged, and
their cause is open.

The open now asks for `kIOHIDOptionsTypeSeizeDevice`, so IOKit hands the device
to wine alone and macOS's driver releases it. **The cost is the mechanism:**
while a bottle holds the pad, macOS and its own applications cannot use it, and
it comes back when the bottle shuts down. A seizing open that fails falls back
to the shared one, so nothing that works today stops working.

**Exclusivity is the right behaviour while a game runs**, and the paragraph
above calls the cost by the wrong name. Two programs writing to one pad over
one Bluetooth pipe is the anomaly, and of the two the game the pad is in
somebody's hands for is the one that should win. What the seize actually
costs is the **scope** of the claim, which is the *bottle* and not the game.
A bottle outlives the game inside it — Steam stays up long after a title
exits — and the pad stays claimed for as long as the bottle does, so it is
unavailable to macOS through a stretch that is no longer a game. Narrowing
the claim to *while a client has the device open* is the improvement, and it
has not been made. Beside that there is one impression, and it is not a
measurement: with the set installed without `mgvf-0006` the vibration felt
noticeably weaker, and felt right again once the four-file set was back —
two engine builds compared by hand, on one person's hand, nothing
instrumented.

**Beside the scope there is a second cost, and it was not intended.** The idle
rule above counts only input WindowServer's plugin parses, and a seized pad
sends it none: while wine held the pad, the plugin's idle clock did not reset
through 13.5 minutes of play at about 64 reports a second. So a pad that was
connected *before* the bottle started — the plugin attached first — is cut by
macOS about 15 minutes after the last input macOS saw. A pad that connects
while a wine process already holds the IOHIDManager match gets no plugin for
that connection and no idle cut: four of four retained connections each way.
Turning the pad on, or reconnecting it, after the game is running avoids it —
seen on automatic reconnects; a deliberate power cycle was not separately
timed.

Scoped to what was measured and no wider — a DualSense (`054c:0ce6`,
`054c:0df2`) that arrived over Bluetooth — with `SeizeDevice` under
`HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>` to
decide it either way for any device. **On by default** for those pads, unlike
`mgvf-0005`'s `UsbEmulation`, because it was written against the drops seen
under a shared open on 2026-09-08; that it prevents them is not measured.
winebus refuses everything that is not a joystick or a gamepad before it opens
anything, so no value under that key can seize a keyboard or a mouse.

The unix half does not read the registry, so the value rides the per-device
settings that already cross the boundary: `struct device_options` — the list
`main.c` fills at driver start and hands to each bus inside `struct
bus_options` — gains one `INT` beside its existing `hidraw`, read from the same
subkey by the same loop. It is therefore read once, at driver start, and a
change applies when the bottle's `winedevice` next starts. What this patch
touches is the first unix code in the set, which is why the set now ships a
fourth file; `runtime/engine-payload-controller/README.md` says where it goes.

### mgvf-0007 — the USB emulation refuses a wired-only audio request *(ours)*
`mgvf-0005` tells a client the pad is on a cable, and a client that believes it
asks for what a wired pad has: its speaker, its headphone jack and its
microphone, which are the part of a DualSense a cable is for. In the first
trace of the emulation under a real title, on 2026-09-08, the title's libScePad
wrote exactly two output reports through it — the first ordinary (rumble and
both trigger-effect blocks), the second asking for nothing but the pad's audio
path — and 22 ms after the second the Bluetooth link was gone. The translation
was not at fault: both packed faithfully, CRCs and all.

On the way from the client's USB report to the Bluetooth one, the validity-flag
bits that enable the audio fields are now cleared and the bytes those bits
govern zeroed, before the CRC, so what the pad checks covers what it is
actually sent. Every other byte is left exactly as the client wrote it, a bit
is cleared only together with its own byte and only when the client set it, and
a report that asks for no audio field comes out byte for byte as it went in.
**Only in the emulation path**: a client that knows the pad is on Bluetooth and
writes the `0x31` itself is untouched. One `TRACE` line names what was dropped.

Which four bits and which four bytes those are was read out of Sony's own
libScePad rather than assumed — `scePadSetVolumeGain` and
`scePadSetAudioOutPath`, at the addresses the patch header names — and the
second flag byte is left alone entirely, because the one bit of it in the
killer packet is one libScePad sets on every report it sends, beside two
vibration fields.

**What is not established is that the request killed the link**, and the patch
header says so at length: in a two-hour session with the same pad presented
truthfully, a client sent the same audio request twice and the link stayed up
for another 900 seconds and more after each. What is measured is the sequence.
This patch rests on the narrower argument instead — the request is for hardware
the pad has on a cable, the emulation is why a client asks for it in that
shape, and nothing under wine is on the other end of the pad's audio path — and
on costing nothing measured.

### mgvf-0008 — the USB emulation answers a feature write without sending it *(ours, and an EXPERIMENT)*
**Read this one as an instrument, not as a repair.** Everything else in the set
changes something that was measured to be wrong. This changes what the
emulation does with one class of traffic so that the next trace answers a
question.

The question: whether a feature-report **write** is what makes a DualSense on
Bluetooth leave a session that `mgvf-0005` is presenting as wired. In the fatal
trace of 2026-09-08 the pad was driven for 39 seconds and then, inside 104
milliseconds, two feature reads were answered, one feature **write** went out
(report `0x08`, 48 bytes, `08 02` and then zeros, with `mgvf-0005`'s CRC
appended), a third feature read was **answered by the pad**, two output reports
went out, and the device was gone. macOS's log says the pad initiated the
parting — *"Received disconnection indication ... reason 431"* — so this was
not a link failure and not macOS letting go. That trace holds exactly one
feature write; the two-hour session in which the same pad worked perfectly on
Bluetooth under Steam holds none. Every session the pad left within seconds is
one Sony's libScePad was driving; every session it stayed is one only Steam
was.

So in the emulation path a feature write is now **answered as if it had
succeeded** and no byte of it reaches the pad. **Answered, not refused**, and
that distinction is the design: libScePad's feature sender at `0x1800074b0`
returns a failure code when `HidD_SetFeature` fails, its caller at
`0x180003deb` is inside the library's per-device reader thread, and a negative
return there jumps to a teardown that closes the device handle and clears the
pad's record — so refusing would make that library drop the pad itself and
teach us nothing.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        ForwardFeatureWrites  REG_DWORD  absent (default): answered, not sent.
                                         Non-zero: sent, as mgvf-0005 sends it.

**Absence means swallow**, deliberately: an experiment that has to be switched
on is one nobody runs, so it is what a fresh install does, and the value is how
a session is put back to `mgvf-0005`'s behaviour between two launches without
another build. It is read through `get_device_option`, `mgvf-0005`'s route
rather than `mgvf-0006`'s, because the decision is made in the PE half in the
same function as `UsbEmulation` — and it is read *before* the lines that
rewrite the desc, so that `ProductId` cannot make it look under another pad's
key. **Feature reads, output reports and every pad the emulation is not
presenting are untouched.** One `TRACE` line per swallowed write names the
report id and length, in the shape of the CRC line it replaces.

**What is not established** is the point of the exercise and the header says it
at length: that the write is what makes the pad leave. The pad answered a
feature read 43 ms *after* it. What is measured is a correlation over a small
number of sessions, all of which are also the sessions libScePad was driving.
If the pad stays with this in place, the write is implicated; if it leaves at
the same point, the write is cleared **and this patch should come back out**.

### mgvf-0009 — a stronger rumble, and an intensity setting *(ours, and a PREFERENCE)*
**Read this one as a preference, not as a repair.** Everything else in the set
changes something that was measured to be wrong; this changes what a game asked
for into something the person holding the pad likes better, and both of its
values are absent by default.

What a game asks for was measured. In `steam-hid-game-145815.log`, a two-hour
Bluetooth session of an Unreal title driving the pad through Unreal's
WinDualShock with no emulation in the way, the title writes the pad's own report
itself: 6493 output reports of id `0x31`, 335 of them carrying a non-zero motor
byte, reaching 255 — 36 packets at right 255 with the left motor at rest, 18
with both at 255. **All 335 carry the same mode**: first flag byte `0x02`,
second `0x00`, byte 38 of the common block `0x04`. So the game is not asking for
a weak effect; at 255/255 it is asking for everything there is.

That mode is one of two the pad knows, and which is which was read out of SDL
2.30.12's own `SDL_hidapi_ps5.c` rather than assumed: below firmware 2.24 SDL
sets flag0 bit `0x01`, *"Enable rumble emulation"*, and halves both motor bytes
*"to match Xbox controllers"*; at 2.24 and above it sets byte 38 bit `0x04`,
*"Enable improved rumble emulation"*, and sends them unhalved. The log is the
second, bit for bit. The owner compared the two on the pad with a six-pulse
ladder at 255 and at 127 and reported that **the legacy compatible vibration at
255 feels clearly stronger** than the haptic mode at 255, and that the haptic
mode at 127 is what the game feels like. **That perception is the evidence for
this patch** — a preference, on one person's hand, with nothing instrumented —
and it is why nothing changes unless asked.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        VibrationMode  REG_DWORD  absent or 0 (default): the packet goes out as
                                  the client wrote it. 1: where the client
                                  selected the haptic path, the legacy
                                  compatible motors are selected instead.
        VibrationGain  REG_DWORD  a percentage over the two motor bytes, absent
                                  or 100 meaning untouched. Saturates at 255,
                                  clamped to 1000, and 0 reads as absent.

The rewrite **sets** flag0 bit `0x01` and **clears** byte 38 bit `0x04`, and
nothing else: flag0 bit `0x02` is left alone because SDL sets it on both paths
and so it is not the choice between them, the second flag byte is left alone for
`mgvf-0007`'s reason, and the motors are **not** halved the way SDL halves them,
because halving them is precisely what would undo what the ladder found. The
patch header names each bit and the evidence for it.

**Both routes**, and that is the point of it: the title measured writes `0x31`
itself, so a hook living only in `mgvf-0005`'s translation would never see a
packet this was written for. A `0x31` arriving for such a pad is rewritten on
the raw route in a copy of the client's buffer, and in the emulation path after
the translation and after `mgvf-0007`'s masking. Either way **the CRC is
computed again** over what is actually sent — a `0x31` whose CRC does not cover
its bytes is one the pad ignores, so rewriting without re-signing would take the
rumble away rather than change it. One `TRACE` line per rewritten packet carries
the mode change and both motors before and after.

**What is not established**: why the two paths feel different, whether the pad
drives different actuators or the same ones differently, and what firmware this
pad runs — nothing here reads it. No title has been played with the rewrite on
either; what is measured is the ladder, by hand, and the corpus the mode was read
out of.

### mgvf-0016 — the same preference, on a cable *(ours)*

`mgvf-0009` above was written from a Bluetooth trace and came out
Bluetooth-shaped in two places that have nothing to do with the preference
itself: the common block starts at byte 3 of a `0x31`, and the packet is signed.
So its rewrite refuses anything that is not a 78-byte `0x31`, and the options
were not even read for a pad that arrived on USB.

From outside that is worse than a missing feature. The two values were written
for the pad, the launcher said what they would do, and plugging the pad in
turned them off without saying so. Reported by the owner on 2026-09-09: *"con
usb no toma los niveles de vibracion"*.

**What the wire differs by.** A wired pad is written report `0x02`, whose common
block starts at byte 1, and which carries **no CRC** — the four bytes that hold
one over Bluetooth are ordinary report bytes there. That is the whole of it, so
this adds a second **envelope** and not a second rewrite: both entry points
stand on the one function underneath, and the host test asserts that the same
common block through both comes out identical. The lengths are read rather than
assumed — `0x02` is 47 data bytes on a DualSense and 63 on an Edge, from their
own captured descriptors — so the flag byte at common 38 sits inside both; a
report too short to carry it is refused rather than read past its end.

**Which rewrite is chosen by `report[0]`, never by the client's report id**, and
that is written into the code because it was got wrong once first. `mgvf-0005`'s
emulation calls the rewrite with the client's id, `0x02`, over a buffer already
packed into a `0x31`; a dispatch that believed the id sent Bluetooth packets to
the wired rewrite, which refused them for their length, and `mgvf-0009` quietly
stopped working on both of its own routes.

**What is established is the defect, not the benefit.** `emulation-111440.log`
holds both transports in one session under one registry: the options are read
for the pad created with `bus_type 2` and never for the same model created with
`bus_type 1` eighty seconds later. That is the fault, on disk. The benefit is
another matter — of **10,684 captured wired `0x02` writes not one carries a
non-zero motor byte**, and byte 39 is `0x00` in every one, so on everything seen
so far this is a **no-op**. And the report that prompted it may not be about it:
a wired pad on the XInput route already honoured `VibrationGain`, so what this
closes is a client writing the pad's *own* `0x02` with a motor in it, which no
capture shows a wired title doing. `mgvf-0010`'s haptics thread keeps its own
gain rather than borrowing this one: it never passes the dispatch this lives in,
and it works on a report of five bytes where this one insists on forty.

### mgvf-0020 — the motors ride the game's own packet instead of costing the link one *(ours, the Bluetooth default since mgvf-0024)*

The mechanism behind the frame cost, measured twice from the same two traces by
analysts told to refute each other: the pad's Bluetooth output pipe drains
**~65 reports a second**, and a title that rumbles through XInput *still* writes
the pad's own `0x31` once a frame — 16,995 of Beast's 18,149 in one session,
every flag zero, a packet that tells the pad to do nothing. That leaves ~5 slots
a second for anyone else; `mgvf-0010`'s thread offered 30; a shallow queue
filled and **every writer paid ~21 ms a packet, the game's own synchronous write
included.** Dose-response in the trace: at 0/8/16/24/32 of our writes a second,
the game's writes over 10 ms went 3.9% → 16% → 41% → 66% → 95%. The positive
control is in the *native* title's trace: in the 1.44% of moments Mortal Shell's
own rate exceeds 64/s it suffers 82.8% of every slow write it makes.

Sony's library never pays because it carries the motors **inside** the heartbeat.
So does this: with `XInputRumbleRide`, a motor change is stamped into the game's
next inert `0x31` — two flag bits, two motor bytes, a fresh CRC — and the haptics
thread writes a packet of its own only after 100 ms of client silence. Zero added
packets. The deadband of `mgvf-0018` moves into the extension so both writers
consult one record; `mgvf-0016`'s gain is applied after the stamp by the same
function that scales the thread's packets.

**It was off until felt, and it has been felt.** `mgvf-0024` made it the
Bluetooth default once the stop behaviour was verified — 105 episodes of rumble
across three runs, every one of them ending in a zero. A cable keeps it off,
which is not a preference: a wired pad is written `0x02`s and there is no `0x31`
to ride.

Not covered: the `mgvf-0005` emulation route, whose translated `0x02` is not told
apart from the thread's own packet.

### mgvf-0019 — which descriptor the guest got is not which radio the bytes leave by *(ours)*

`ext->desc.bus_type` answers two different questions in this file, and three
sites asked the second and got the first answer. It says **which descriptor the
guest was given** — which `get_compatible_ids` and the Bluetooth input-report
naming want, and are right to ask. It does **not** say which transport a packet
leaves by, because `mgvf-0005` sets it to USB for a pad that is physically on
Bluetooth.

The consequence: with `UsbEmulation` **and** `XInputRumble` both set — a pair the
launcher offers together in one panel — `mgvf-0010`'s haptics thread concluded
"cable" for a pad on the air and wrote a raw `0x02` to a pad whose Bluetooth
descriptor declares no such report. Every write was refused and XInput rumble
did nothing at all, silently.

`dualsense_speaks_bluetooth` asks what those sites meant.
`HIDRAW_FIXUP_DUALSENSE_USB` is set only for a pad that arrived over Bluetooth
and is never cleared, so it means "physically on the radio and presented
otherwise", exactly.

**And it is what makes a default possible.** `mgvf-0018` left the deadband off
because the right value depends on a transport that could not be asked. Now:
**0 on a cable, 4 over Bluetooth.**

### mgvf-0018 — a motor step nobody can feel is not worth a slot on the link *(ours)*

`mgvf-0010` writes the motors whenever the value a client asks for changes. On a
cable that is free; over Bluetooth the link carries about 65 reports a second and
that ceiling moves for no API. Measured over one session, 1951 motor packets:
**49% moved a motor by one step of 255**, 69% by two or fewer, 76% by four or
fewer. A step is 0.4% of the range. Half of what the driver spends the link on
is dithering along a ramp.

`XInputRumbleDeadband` is the smallest change worth a packet, in steps of 255,
off by default. At four — 1.6% of the range — 73% of those writes would not have
been made.

**It is not the interval by another name.** `XInputRumbleInterval` drops changes
by *time*, and time cannot tell a hammer blow from a tremble, which is why
raising it to 60 ms made the rumble feel dead. This drops changes by *size*:
every transition a hand can notice goes out at once. Two rules make it correct —
the comparison is against the last pair the **pad was given**, so a ramp creeping
one step at a time accumulates and arrives rather than being dropped for ever;
and **zero is exempt** in both directions, because a pad still buzzing after the
game stopped asking is a defect and not a saving.

### mgvf-0017 — build the motors and do not write them, on request *(ours, and an INSTRUMENT)*

The second thing in this series that is an experiment rather than a change, after
`mgvf-0008`, and marked as one wherever it is named. It is **off** by default.

A DualSense on Bluetooth costs an Unreal title 3–5 fps and drops its audio while
the motors are being driven; the same pad on a cable costs nothing. Every
mechanism proposed for that has been eliminated by measurement — not CPU, not
the pad's input stream, not a control-channel handshake, not Bluetooth audio,
not sniff mode. What was never separated is the **write to the radio** from the
**path the request takes through the guest to reach it**: `XInputSetState`,
hidclass's dispatch, the IOCTL that takes the device lock, the event that
round-trips wineserver — all on the game's own thread and all under Rosetta.
Turning the rumble off removes both at once, which is what made every control
run so far inconclusive.

`XInputRumbleDryRun` removes exactly one. Everything happens except the last
step; the pad does not buzz, and an `ERR` at device arrival says so, so that a
log cannot later be misread as a pad that failed. Its header states its own
edges: on Bluetooth the skipped branch would also have packed and signed the
`0x31`, which is a few microseconds on the haptics thread and nothing on the
game's.

### mgvf-0010 — the pad's own motors, offered to XInput *(ours, OFF by default)*

Most Windows games ask XInput for "controller 1" and expect an Xbox pad. XInput
had no motors to offer a DualSense, because a DualSense does not keep its motors
where an Xbox pad does. This adds a **haptics collection** — a second top-level
collection beside the pad's own, so the pad keeps every byte of its descriptor
and the motors arrive next door on a small device of their own.

Nesting it inside the pad's collection was the first attempt and it was wrong:
a game reading the pad through raw HID *and* through XInput then heard
everything twice, and not merely twice — HID counts the Y axis downwards and
XInput upwards, so the two cancelled and the left stick lost its vertical while
keeping its horizontal.

The motors are written from a thread of its own, so the game's thread never
blocks on the link. On Bluetooth it also **names the fields of report `0x31`**,
which the pad sends as one opaque vendor block; without that xinput finds the
motors, writes to them, and still cannot read an axis — which a game feels as a
stick jammed in a corner.

### mgvf-0011 — `hidclass` offers such a pad to xinput as well *(ours)*

Wine's xinput enumerates `GUID_DEVINTERFACE_WINEXINPUT`, which only winexinput's
own children carry. A pad with a haptics collection is offered under that
interface too, exactly the way `hidclass` already offers a mouse and a keyboard
under theirs. Devices that already carry the private class are left alone, so an
Xbox pad behaves as before.

### mgvf-0012 — the other gamepad convention *(ours)*

A gamepad's axes and buttons come in two conventions, not one. Read the wrong way
round, a trigger's rest position is full deflection on a stick and the game moves
untouched. Sony pads put the right stick on Z/Rz and the triggers on Rx/Ry;
Microsoft's order is the one wine assumed. See `mgvf-0027` for how the choice
between them is made, which was wrong here for eighteen days.

### mgvf-0014 — no options is not a request for silence *(ours)*

`mgvf-0009` stored "no options" as all zeros, and one of its two write paths read
a gain of zero as a request for silence. So a pad with nothing configured could
be silenced by the absence of configuration. Superseded in full by `mgvf-0022`,
which replaced the sentinel rather than patching the reader.

### mgvf-0021 — the pad's own haptic path, on request *(ours)*

A DualSense knows two ways to be told to vibrate. **LEGACY** is flag0 bit `0x01`,
where the pad drives its voice coils in imitation of a pair of rotating-mass
motors. **HAPTIC** is flag2 bit `0x04`, the branch SDL's PS5 driver takes for
firmware 2.24 and above. Both also set flag0 `0x02`, which is why that bit is
named for what it does — disable audio haptics — and not for a path.

`mgvf-0010` had always sent legacy, on the strength of a six-pulse ladder in
which legacy felt stronger at the same value. That ladder was run when this
driver could deliver about sixteen updates a second. At the sixty a second
`mgvf-0020` delivers, the comparison was worth making again, and this is the
option that made it possible without a rebuild between the two runs.

Sony's library takes the haptic path on all 8,608 packets of the session captured
here and never once sets bit `0x01`.

### mgvf-0022 — absence is a neutral request, not a zeroed one *(ours)*

`mgvf-0009` zeroed its options struct for a pad with nothing configured, and the
write paths tested for that — which made `{mode CLIENT, gain 0}`, a gain of zero
written **on purpose**, byte-identical to an absence. Silence by gain alone
reached no motor, and the comments in the file promised the opposite. The absence
is now `{CLIENT, 100}`, the pair that changes nothing, so zero can go on meaning
zero.

### mgvf-0023 — the motor power field, which nothing here had ever claimed *(ours, ON by default)*

One byte-level difference remained between what Sony's library sends a DualSense
and what this driver sent it: bit `0x40` of the second flag byte, and the byte it
governs. `mgvf-0007`'s disassembly had already recorded that libScePad sets it on
every packet together with report byte 37 and flag2's haptic bit, and called the
three "vibration fields". The capture agrees: of 10,886 native writes, 10,862
carry the bit and 10,860 carry zero in the byte; of the 283 carrying a motor, 282
carry both.

Three runs settled what it does, with only that field between them: unclaimed and
claimed-with-zero felt **the same**, and claimed with `0x77` dropped the rumble to
almost imperceptible. So the pad honours the field, it is a **reduction**, and it
was already at none. Nothing had been attenuating this driver, and there is no
second intensity field to find — the gain scales the two motor bytes and
saturates at 255; this one can only take away.

The default is 0 — claimed, at no reduction — which changed nothing on the pad
measured and is kept because an unclaimed field carries whatever the pad was last
told by anything else on the machine.

### mgvf-0024 — on Bluetooth, ride the packet and take the pad's own path *(ours)*

Two options that had been off since they were written become the Bluetooth
default, each because it was measured.

The **ride** carried 92–95% of every motor change inside a packet the game was
sending anyway, added nothing to a link that drains about sixty-five reports a
second, and removed the frame cost this series started from. The **stop** is what
it was waiting on, because a ride that cannot deliver a zero leaves a pad buzzing
in a menu: across three runs, 105 episodes of non-zero rumble, 105 of them ended
in a zero, median 17 ms after the last non-zero packet and worst 100 ms, and all
three traces end with the pad silenced.

The **haptic path** was preferred by the same hand at sixty updates a second that
had preferred legacy at sixteen. Still untested in the field and named rather
than implied: the thread's 100 ms fallback for a client that stops writing
mid-rumble. That title writes its own `0x31` about forty-five times a second
without a pause, so the ride always had a carrier.

### mgvf-0025 — the motor path is a question about the pad, not about the cable *(ours)*

`mgvf-0024` left a cable on the legacy motors for want of a measurement, which
made one menu entry mean the finer path or the harder one depending on whether a
wire was plugged in. Nobody reports that as a setting; they report it as the pad
feeling different on a cable. Both transports answer the same way now.

What made it safe to unify arrived the day after, on a title that drives the pad
through Sony's library — so running it twice with only the rewrite moved is the
same content against itself rather than one game against another. Same motor
bytes, gain neutral: the legacy path is clearly **harder**, the haptic path is
**finer**. Two characters, both real, and a preference belongs to the person
rather than to the transport.

The ride is untouched: it stamps a `0x31` and a wired pad is written `0x02`s, so
there is nothing on a cable to ride. That one really is a fact about the
transport.

### mgvf-0026 — the deadband belongs at the pad, not at the request *(ours)*

The band is four of 255 and it was compared against what the **game** asked for,
while the gain that decides what the pad receives is applied afterwards. So it
meant four of 255 at a strength of ×1 and forty at ×10: **turning the strength up
made the rumble coarser instead of stronger**, which is the opposite of what the
control says and exactly what the hand holding the pad reported, twice, before
anybody read the line.

| at a strength of | changes held | worth ≥10 of 255 at the pad |
|---|---|---|
| ×4 | 358 | 16% |
| ×10 | 632 | **52%** |

It compares the movement at the pad now, and it no longer holds back a change
that rides inside a packet the game was sending anyway — those leave either way,
so holding one only made its motors stale. Measured across two runs of one title
with only that between them: the ride carried **5.1 motor changes a second where
it had carried 1.5**, and the pad's input stream is no worse while the motors
move than while they are quiet. `XInputRumbleRideBand 1` restores the old
behaviour on ridden packets without a rebuild.

Not claimed: that this makes the rumble stronger. It cannot. What comes back is
everything between the peaks, which is where a rumble is felt.

### mgvf-0027 — a Sony badge is not a Sony descriptor *(ours)*

`mgvf-0012` decided which convention a pad follows by reading its **vendor id**.
That was right for as long as a Sony pad reached xinput carrying its own report
descriptor. It stops being right through winebus's SDL backend, where wine throws
the pad's descriptor away and builds an Xbox-shaped one — while the vendor id
stays `0x054c`, because it is still the same physical pad. So the Sony order was
applied to a descriptor already in Xbox order and the two corrections cancelled
into a fault: a title on that route read its right stick off the triggers. This
project's own regression, open since `mgvf-0012` shipped.

The descriptor is asked now. A Sony pad declares X as an unsigned byte, logical
`0..255`; wine's synthetic gamepad declares a signed 16-bit axis with a negative
minimum. Checked rather than assumed, because a pad that works today must not
stop: all five descriptors captured in this project — both DualSense models over
both transports — declare X as `0..255`, so the raw route keeps the Sony order
exactly as it had it.

### mgvf-0028 — a device that carries motors and nothing else is not a controller *(ours)*

`hidclass` makes one device per top-level collection, so `mgvf-0010`'s haptics
arrives on a device of its own carrying the pad's vendor and product ids — it is
the same physical pad. A client that finds its controller by walking the HID
interface and reading those ids therefore sees **two** DualSense, and the second
answers nothing a DualSense would. Sony's library does exactly that, and no title
using it would start while `XInputRumble` was on.

It costs nothing to fix, which is what makes it worth doing: xinput does not look
for this device under the HID interface. Three conditions mark it, and each does
work, because getting this wrong the permissive way would erase a real
force-feedback controller from every application: the multi-axis usage
`mgvf-0010` chose on purpose, a haptics collection, and **no generic-desktop axis
at all**.

Mortal Shell 2 starts with the switch on. Not proved by that run: the narrow half
— there is no real force-feedback wheel here to confirm a genuine one is left
alone.

### mgvf-0029 — the haptics device can be asked to declare no button *(ours, superseded by mgvf-0030)*

`mgvf-0010`'s device declared **one button that is never pressed**, so a client
counting button caps would not see zero. Ghost of Tsushima would not keep L3 held
while `XInputRumble` was on, and a device reporting every button released on
every poll is the shape of a held button being cleared. This is the one-bit
experiment that tells a candidate from a diagnosis: `XInputRumbleStubButton 0`
builds the same device with the button gone and eight bits of padding in its
place, so the report is the same two bytes and nothing downstream moves.

### mgvf-0030 — no buttons is a legal device, and the haptics stub has none *(ours)*

The experiment answered the same day: with the button gone, L3 holds; with it
there, it does not. So the better packet becomes the default, in two halves and
the order matters.

First **xinput stops allocating from a count it has not looked at**. A device
with no buttons is legal HID, `malloc(0)` may return NULL, and on that line NULL
meant the whole device was refused — a controller offering nothing but motors
dropped for having no buttons to count, which is the one thing it was never going
to have.

Then the stub's default turns over. The button existed only to keep that
allocation away from zero, and it was always **our** xinput that would have hit
it: since `mgvf-0028` this device is published under the xinput interface and no
other, so the only code that opens it is code this set ships.

Not claimed: why the game behaves that way. What is measured is the outcome, on
one title, with one option between the two runs.

### mgvf-0031 — the lights a user chose *(ours)*

A DualSense's lightbar and player LEDs showed whatever the last writer said, and
on this route nobody asked the user. Three values under the pad's own winebus
key, read as it arrives and under its real model, change that: `LightbarColour`
(`0x01RRGGBB`, `0x01000000` is off), `PlayerLights` (`0x100 | pattern`, `0x100`
is off, `0x104`/`0x10A`/`0x115`/`0x11B` are players 1 to 4) and
`LightbarRelease` (0, 1, 2). A value without its marker asks for nothing, so the
default is byte-identical to the build before it.

What it changes is the **client's own light packets**, on the native Bluetooth
`0x31`, the wired `0x02` and the emulated `0x02`, and only the fields whose
enable bit that client set: the colour under flag1 `0x04`, the pattern under
flag1 `0x10`, keeping the client's instant bit. No flag bit moves, a Bluetooth
packet is re-signed only when a byte changed, and nothing the haptics thread
writes passes through it. By construction it adds no packet of its own while
you play, apart from the release below, at most once per arrival; that it costs
no frame is not measured yet.

What was measured: in `hid-203611.log` (Mortal Shell 2, Steam Input, Bluetooth),
8 of 1858 client writes set a light and all 8 are Steam's; a ninth is Steam's
`00/08` release-LEDs packet, which sets none. At each config activation and
again on a reconnect Steam sends player `0x24` with colour `00ffff`, then the
colour alone, then player `0x00` with the colour. The Desktop activation is
different: `00/08`, then player `0x00` with `00ffff`, then `00ffff` alone, and
no `0x24`. The game sent none. No packet in that session carries the lightbar setup release, and the
macOS logs of the same evening show macOS's own colour writes carrying it and
Steam's reaching the radio without it.

The release: Linux `hid-playstation` says a Bluetooth DualSense ignores lightbar
programming until it is sent a setup release. So with a colour chosen, before
the first client lightbar packet on Bluetooth, `LightbarRelease 1` (the default)
sends one release-only report of ours and `2` folds the same two fields into the
client's packet instead; `0` sends nothing.

Measured on 2026-09-14 with the default, `LightbarRelease 1`, on a DualSense Edge
on Bluetooth under Steam Input (`hid-090147.log`): the release went out with
status 0, then Steam's own light packets were rewritten as they passed — colour
`000040` and `00ffff` became `ff0000`, player `0x00` became `0x04` and `0x24`
kept its instant bit — and the pad showed the red bar and the centre light, in
two titles. A Sony-library title launched with no choice made (both values
written as 0) kept the lights the game sets; that session was seen, not traced.

Not claimed:

- that this pad needs the release at all, or that form `2` would work too —
  form `1` works and the others were not tried;
- anything for a title that never sends a light change, such as a pure XInput
  title without Steam Input: this phase writes no packet at arrival;
- brightness, the mic LED and Steam's release-LEDs packet, which pass through
  untouched;
- per-pad lights: values are keyed by model, so two pads of one model in one
  bottle share them;
- the xinput slot: it cannot be seen from winebus, so the player number is the
  user's choice, not the game's.

### mgvf-0032 — SDL leaves a hidraw pad alone *(ours)*

A pad whose key says `Hidraw` goes through `bus_iohid.c`, and `main.c` throws
away the copy the SDL bus reports. SDL did not let go of it. In the same
winedevice.exe:
- `sdl_add_device 054c/0df2` is traced 4 ms before the seize;
- ioreg shows a second client on the pad with `ClientOptions` 0;
- that client's `SetReportErrCnt` climbs at exactly 2.00/s against
  mgvf-0006's seize.

`sdl_device_stop` closes only the joystick, and SDL 2.30's PS5 driver sends a
CRC-less Bluetooth keep-alive every 500 ms. That is read from source and
matches the rate; no trace of SDL's own writes ties them. Under the seize every
write is refused. Without the seize it would be a third writer.

Before `SDL_Init`, the SDL bus now names those pads in two of SDL's own hints:
- `SDL_HIDAPI_IGNORE_DEVICES`, so hidapi never lists them;
- `SDL_GAMECONTROLLER_IGNORE_DEVICES`, so the IOKit and GameController backends
  do not pick them up once hidapi lets go.

Both names and hidapi's match formats are in the engine's libSDL2 (2.30.12). A
variable already set in the environment wins, with a WARN. The scope is exactly
a key naming a vendor and a product with a non-zero `Hidraw`, and no
`DisableHidraw`.

Unchanged:
- the launcher's SDL route, which writes `Hidraw` 0;
- a pad with no `Hidraw` value;
- a vendor-only key;
- product `0000`.

The SDL devices it removes were already ignored by `main.c`, so nothing a game
enumerates changes. Trace (`+hid`): `SDL will not open 0x054c/0x0ce6,0x054c/0x0df2:
the hidraw route owns them`.

Not claimed:

- that the second client leaves ioreg. Measured on 2026-09-14 with the patch
  (`hid-090147.log` has the hint line): the winedevice.exe client with
  `ClientOptions` 0 showed `SetReportErrCnt` 0 and `SetReportCnt` 0 across a
  20 s sample, where before it climbed at 2.00/s. One shared client with every
  counter at zero remains; that it is the IOKit backend's HID manager is a
  reading;
- anything about the idle disconnect, which is macOS's own mechanism and is not
  affected;
- the IOKit backend's shared HID manager open at `SDL_Init`, which comes before
  any list and is unchanged;
- the GameController backend for an Edge-only key: that backend names every
  DualSense `054c:0ce6`, and whether it sees a pad inside winedevice.exe at all
  is not measured.

### mgvf-0033 — turn off a pad nobody is using *(ours)*

A DualSense that connects over Bluetooth while wine already holds it gets no
macOS GameController plugin. So macOS's 900 s idle cut never applies to it, and
neither, in every such connection retained, does the pad's own power-off after
about eleven minutes. It is left to run flat.

What was measured, from macOS's and Steam's own logs:
- 2026-09-12/13: the pad connected at 23:28:10 with a game holding it. The game
  and Steam were gone by 00:07:48. The pad dropped by itself at 03:21:07, and
  the next connection reported 0 % battery: 3 h 13 min with, very likely, no
  wine process left;
- 2026-09-13: the same pad stayed up 27 minutes after wine exited at 22:15:44;
- 2026-09-14, a pad macOS was driving: it turned itself off after 686 s.

The mechanism, measured on 2026-09-14 on a DualSense Edge (`054c:0df2`) over
Bluetooth with no wine running. The report was sent with
`IOHIDDeviceSetReport`: feature `0x08`, 48 bytes, `08 02`, zeros, and the
Bluetooth CRC-32 (seed `0x53` over the first 44 bytes) little-endian in the last
four. For this content the tail is `e0 ef a2 23`, the tail mgvf-0005 appended to
libScePad's own write of this report on 2026-09-08.
- opened shared: the call returned 0, and bluetoothd logged the pad's own
  disconnect (HID reason 431, ACL 10719) 63 ms after the Set report. The light
  went off and the pad stayed off;
- opened with `kIOHIDOptionsTypeSeizeDevice`, as winebus opens it: the call
  returned 0, the disconnect came 64 ms after, and the pad stayed off;
- a plain DualSense (`054c:0ce6`), opened shared: the disconnect came 62 ms
  after the Set report, its light went off and it stayed off.

THE CHANGE, in `bus_iohid.c`. A started DualSense on Bluetooth that this process
holds seized is sent that report **once**, after a limit with no stick, trigger
or button input. What counts is measured against the last report that counted,
not the previous one, because the sticks of a pad on a table jitter:
- 78-byte `0x31`: sticks 2..5 and triggers 6..7 when they move more than 4;
  button bytes 9, 10, 11 and 12 on any change. Byte 11 is taken whole: in the
  seized traces `hid-090147`, `hid-203611`, `hid-114844` (`0df2`) and
  `hid-175158` (`0ce6`), none of its bits changed while the pad was still.
  Byte 12 is in every trace that dumps the raw `0x31` (33 traces, 1,569 to
  111,642 reports each) and never changed, so counting it costs nothing. Only
  bits `0x01` and `0x02` of byte 11 ever changed in those traces: no trace has
  an Edge Fn button or back paddle pressed, so where the Edge reports them is
  not measured yet, and counting bytes 11 and 12 whole covers both;
- 10-byte `0x01`: sticks 1..4, triggers 8..9, buttons 5, 6 and `7 & 0x03`. The
  rest of byte 7 is a counter: bits 2 to 5 toggled on every report in
  `hid-090147`.

Gyro, accelerometer, timestamps, battery and the touchpad do not count. The
report is sent after `iohid_cs` is released, so a pad turning itself off
cannot hold up device removal. The request is marked sent even when it fails,
so it is never repeated every ten seconds; a reconnect is a new device with a
fresh clock. A pad opened shared keeps macOS's own cut and is left alone.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
      IdlePowerOffMinutes  REG_DWORD  absent: 20 for 054c/0ce6 and 054c/0df2.
                                      0: never. Otherwise clamped to 5..240.

The value is read per model key, like `SeizeDevice`, and ignored for any other
device. The 20 is a reasoned default, not a measurement: above the pad's own
686 s and macOS's 900 s, so a held pad is never turned off sooner than one macOS
drives.

Logging. When it acts, a FIXME, visible without `+hid`, of the form
`DualSense 054c:0df2 had no input for <N> s while this process held it; asking
it to turn itself off (feature 0x08): <IOReturn>` (not seen in a run yet). With
`+hid`, a trace at start with the
limit, and one if input comes back after the request with no removal (the pad
did not turn off).

Not claimed:

- that this was measured inside wine: the build, the FIXME and the pad dropping
  within seconds of it are not measured yet;
- that a game takes the pad back after PS reconnects it. The re-seize is the
  known connect-while-held path; what each title does with a removal and
  re-arrival is not measured;
- that the pad is in use when it is only held: a pad held still in the hand for
  20 minutes, or used only for motion aiming or the touchpad, counts as unused;
- anything after wine exits, which is where the reported drain happened. This
  patch does nothing then. RaccoonBot's watcher is meant to cover that while it is running (not measured yet on a live pad);
  MacGamePadFix users without RaccoonBot are covered only while a game runs.

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
`check-builds.sh` reads — whose last row is the unix half and whose two number
columns mean exported symbols and linked libraries there rather than COFF
exports and imports. A patch added to that set is named in
`build-controller-bus.sh`'s own list rather than in `--patches`, and the
sentence above about describing it here applies just the same.

They also do not all apply standalone. 0003 needs 0002, and 0008 needs its
predecessors; testing one against a pristine tree reports a failure that says
nothing about the patch. Apply them in ascending order, as the build does.

## Credit

The winevideo project is at <https://github.com/Jfishin/winevideo>. The four
patches above are its work and its findings; this project applies them and says
so. Nothing here should be read as those patches originating in MacGameVideoFix.
