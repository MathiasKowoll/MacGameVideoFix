# engine-payload-controller

The optional controller-bus set, laid out the way a CrossOver engine is, so a
patcher can copy this tree over the engine it is building without a mapping
table of its own:

    wine/x86_64-windows/winebus.sys    ->  <CX>/lib/wine/x86_64-windows/winebus.sys
    wine/x86_64-windows/setupapi.dll   ->  <CX>/lib/wine/x86_64-windows/setupapi.dll
    wine/x86_64-windows/ntoskrnl.exe   ->  <CX>/lib/wine/x86_64-windows/ntoskrnl.exe
    wine/x86_64-unix/winebus.so        ->  <CX>/lib/wine/x86_64-unix/winebus.so

Note the fourth line. `winebus.so` is the **unix half** of `winebus` and does
not live with the PE files: `x86_64-unix`, not `x86_64-windows`. It is the one
thing about this layout that cannot be guessed from the other three, and a
patcher that assumes the directory from them writes a Mach-O over a PE file.

`built-for.json` and this file are not copied into the engine.

This tree is a **sibling** of `engine-payload/`, not a part of it, on purpose:
overlaying `engine-payload/` installs the media set and nothing else, and
overlaying this tree installs this set and nothing else. A patcher chooses per
set. The same bytes ship flat beside `runtime/install-engine-controller.sh` as
`engine-controller-winebus.sys`, `engine-controller-setupapi.dll`,
`engine-controller-ntoskrnl.exe`, `engine-controller-winebus.so` and
`engine-controller-built-for.json`, and
`check-builds.sh` compares the two copies. The `engine-controller-` prefix is
deliberate: the media installer picks its set by reading `engine-built-for*.json`
and `engine-winegstreamer*`, and these must never be taken for one of those.

## What this is

**Four files, eight patches.** Three PE files and the unix half of `winebus`,
built from the engine's own wine source — the revision `built-for.json` records
— with `mgvf-0002`, `mgvf-0003`, `mgvf-0004`, `mgvf-0005`, `mgvf-0006`,
`mgvf-0007`, `mgvf-0008` and `mgvf-0009` on top. `source-patches/README.md` says
what each one does. `mgvf-0008` is an **experiment** rather than a fix and
`mgvf-0009` is a **preference** rather than either; both are written out as what
they are below.

The fourth file is new in `mgvf-0006` and it is why the set is no longer PE
only: that patch changes how winebus **opens** the pad, which is `bus_iohid.c`,
which compiles into `winebus.so`. Both halves of winebus ship together — they
are built from one tree and read one struct — so an engine on three of these
files and CodeWeavers' `winebus.so` is not a supported combination, and
`--status` calls it `broken`.

No decoder, and nothing here links against the engine's own libraries: one
build serves every engine of the name and version the stamp records, and a copy
this project made is served through `copied_from` in its `mgvf-origin.json`, the
way the media set is. Read `built-for.json` before installing and **check both
fields**: a patched fork and a stock CrossOver report the same version, and the
app name is what tells them apart. That check matters more for the unix half
than for the other three: it links `ntdll.so` and speaks wine's internal
interfaces, which are not stable between wine revisions.

**What they change.** A Windows client decides whether a controller is on USB
or Bluetooth by asking the HID device's parent devnode for its compatible ids
and looking for `BTHENUM`. Under wine that could never succeed: `CM_Get_Parent`
was a stub and winebus named no bus at all. With this set `winebus.sys` names
the bus in a device's compatible ids, `setupapi.dll` answers `CM_Get_Parent`
for HID children, and `ntoskrnl.exe` refreshes a device's hardware and
compatible ids on every enumeration rather than only the first time it is ever
seen. The three only work together: `mgvf-0004` exists because with the first
two alone a client still read the record of the first boot.

**What that fixes.** A DualSense on Bluetooth wants output report `0x31` with a
CRC; a client that believes the pad is on USB sends the USB report instead,
and the pad refuses it silently. Once the transport is known the client sends
the right one: rumble, the PS button and the touchpad work over Bluetooth, as
they always did over USB, measured on 2026-09-08 in every title tried; trigger
effects ride in the same report, and a title that sends them over Bluetooth is
reported working.

## Presenting a DualSense on Bluetooth as if it were on USB

Off by default, per device, and the opposite of the rest of the set. Two
consumers do not want the truth the set makes reachable. Sony's libScePad
(1.0.4.1) decides USB against Bluetooth from the HidP capabilities alone and
then writes the USB output report `0x02` only, which a pad on Bluetooth does
not have; it drops the pad. Steam, for a title whose store category is 57
("PS5 controller support") without 58 ("PS5 support over Bluetooth"), shows
its *plug in your controller* dialog whenever hidapi says Bluetooth. With
`mgvf-0005` the `winebus.sys` in this set can present such a pad as wired:
bus type USB, the USB report descriptor, and every report translated at the
boundary so the pad still receives what it expects — the `0x02` a client
writes goes out as the `0x31` with sequence byte and CRC-32 that the pad
wants, the pad's `0x31` input comes back as the USB `0x01`, feature reads
lose their CRC tail (the calibration report excepted: a wired pad returns
that one with its CRC) and feature writes gain one. Rumble and trigger
effects work through the translation, because they are the same block in
both formats.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        UsbEmulation   REG_DWORD   0 (default): as before. 1: present as USB.
        ProductId      REG_DWORD   0 (default): the pad's own id. Otherwise
                                   the id to present, e.g. 0x0ce6 on an Edge.

The key is named after the pad's real ids, lower-case hex with a slash —
`054c/0df2` for a DualSense Edge, `054c/0ce6` for a plain DualSense — the
shape winebus already uses for its `Hidraw` option under the same `Devices`
key. Both values are read when the pad arrives — when it connects, or when
the bottle's wine starts with it already connected, as a Steam launch on a
cold bottle does — and nothing else re-reads them, so a change applies on
the next connect or the next start. Only a DualSense on Bluetooth is
affected: any other device, and a DualSense on USB, is created exactly as
before. `ProductId` exists for a client that knows
only the plain pad; an id whose USB descriptor is not built in is ignored
with a warning in a `+hid` log, and so is `UsbEmulation` itself for a pad
whose own descriptor is not.

**Both pads are served.** The plain DualSense's USB report descriptor (289
bytes) and the Edge's (405) are each read from that pad on a cable and built
in, so `UsbEmulation` works under a `054c/0ce6` key as well as a `054c/0df2`
one, and `ProductId` `0x0ce6` on an Edge presents it as a plain pad. Nothing
is derived from the other pad's descriptor: they differ, and the difference
matters — output `0x02` is 48 bytes on the plain pad and 64 on the Edge. The
`0x31` that goes to the pad is 78 bytes either way, with the effects block at
the same offsets, so the same translation carries both lengths.

**What the lie costs the pad, and what the emulation now refuses.** Telling a
client the pad is wired invites it to ask for the part of the pad a cable is
for. A DualSense's speaker, headphone jack and microphone hang off its USB
audio interfaces, and a client that believes the pad is on a cable will ask to
configure them: in the first trace of this emulation under a real title, on
2026-09-08, the title's libScePad wrote exactly two output reports through it,
an ordinary one — rumble and both trigger effects — and one asking for nothing
but the pad's audio path, and 22 milliseconds after the second the pad's
Bluetooth link was gone. With `mgvf-0007` the emulation takes those fields back
out on the pad's behalf: the flag bits that enable the audio settings are
cleared and the bytes they govern zeroed, before the CRC, and every other byte
goes to the pad exactly as the client wrote it — rumble, trigger effects,
lightbar and player LEDs are untouched, and so is a client that speaks
Bluetooth natively. Which bits and bytes those are was read out of Sony's own
libScePad and not guessed. **That the request is what killed the link is not
established**, and the honest form of it is worth having: in a two-hour session
with the same pad presented truthfully, over Bluetooth, a client sent the same
audio request twice and the link stayed up for another fifteen minutes and more
after each. What is measured is the order of events. What the patch rests on is
narrower: the request asks for hardware the pad has only on a cable, the
emulation is the reason a client asks for it here at all, and nothing under
wine is on the other end of the pad's audio path — so dropping it costs nothing
that was measured and leaves less of the lie reaching the pad.

**An experiment rides here too, and it is on by default.** `mgvf-0008` is the
one thing in this set that is not a repair. In the fatal trace above the pad
did not merely receive an audio request: 51 milliseconds before it, the title's
libScePad **wrote a feature report** to the pad — report `0x08`, 48 bytes,
`08 02` and then zeros — and macOS's own log for the moment the link ended says
the pad initiated the parting, *"Received disconnection indication on device
DualSense Edge Wireless Controller reason 431"*. Not a link failure and not
macOS letting go: the pad chose to leave. That trace holds exactly one feature
write. The two-hour session in which the same pad worked perfectly on
Bluetooth under Steam holds none, and every session the pad left within seconds
is a session Sony's library was driving. So while this experiment is running,
**a feature write in the emulation path is answered as if it had succeeded and
no byte of it reaches the pad** — answered, not refused, because that library
drops the pad itself when a feature write fails, which would measure the
library and not the pad. Feature *reads* are untouched, output reports are
untouched, and a pad the emulation is not presenting is untouched.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        ForwardFeatureWrites  REG_DWORD  absent (default): the write is
                                         answered and not sent. Non-zero: it
                                         goes to the pad, as before this
                                         experiment.

Absence means swallow on purpose: an experiment that has to be switched on is
one nobody runs. Set the value to `1` to put the write back on the wire without
another build. It is read when the pad arrives, under the pad's **real** ids,
beside `UsbEmulation` — so a change applies on the next connect or the next
start of the bottle, exactly as `UsbEmulation` does. A `+hid` trace shows which
of the two happened: *"swallowed the feature write, report id ... length ..."*
where it used to say *"appended the Bluetooth CRC to feature report id ..."*.
**What this does not establish** is that the write is what makes the pad leave
— the pad answered a feature read 43 ms after it — and if the next trace shows
the pad leaving at the same point anyway, this comes back out.

**Limits.** No pad but a DualSense, and that is a decision rather than a gap:
a DualShock 4's descriptor, input reports and output reports all differ, so it
would be a second translation layer, and nothing measured asks for one. A
client that does know the pad is on Bluetooth — Steam for a title with
category 58, SDL — sees a wired pad while the option is on and writes the USB
report instead; that is translated too, so nothing is lost, but the option is
per device and off is the answer for every pad that does not need it: set it
for a pad and a title that do, and leave it off otherwise. And the driver half
is not yet measured on a live pad: the packing is proven byte for byte against
packets that rumbled the pad, but the option has not yet been turned on under
a running title, so the first use belongs in a `+hid` trace.

## Taking the pad away from macOS

**On by default, and it costs something.** This is the one thing in this set
that changes what macOS can do while a bottle is up, so it is written out
plainly.

macOS drives a connected DualSense itself. WindowServer's
`com.apple.GameController.HID:DualSense` driver opens the pad every time it
appears and writes Bluetooth output reports to it. wine's winebus used to open
the same pad *shared* and write its own. A DualSense on Bluetooth has one
output pipe, and with two writers on it macOS's writes time out. Measured from
macOS's own log on 2026-09-08: 163 timeouts in a day — *"(Async) Unable to send
BT output report to DualSense - error -536870186"*, which is
`kIOReturnTimeout` — every one of them inside a minute in which a game was
running under wine, 4 to 35 a minute. After a burst the driver tears itself
down and 160 ms later bluetoothd drops the link, *"reason 10719"*: six drops in
six minutes across two launches, where the drops earlier that day carried
`10722`, the pad's ordinary idle power-off. **The pad was not turning itself off
during play. The link was being killed by contention, and wine was the second
writer.**

With `mgvf-0006` winebus opens such a pad with `kIOHIDOptionsTypeSeizeDevice`:
IOKit hands it to wine alone and macOS's driver releases it, so there is one
writer again.

**What that costs.** While a bottle holds the pad, **macOS and its own
applications cannot use it** — not the desktop, not a Mac-native game, not
another CrossOver bottle. The pad comes back when the bottle shuts down, which
is when the process that seized it goes away. That is the mechanism and not a
side effect: the fault is two writers, and the fix is one.

**And exclusivity is the right behaviour while a game runs.** The paragraph
above calls this a cost, and that is only half of it: two programs writing
to one pad over one Bluetooth pipe is the anomaly, and of the two the game
the pad is in your hands for is the one that should win. What it actually
costs is the **scope** of the claim: it lasts as long as the **bottle**, not
as long as the game. A bottle outlives the game inside it — Steam stays
running long after a title exits — and the pad stays claimed until the
bottle shuts down, so it is unavailable to macOS through a stretch that is
no longer a game. Narrowing the claim to *while a client has the device
open* is the improvement, and it has not been made yet. One impression
belongs beside this and it is not a measurement: with the set installed
without `mgvf-0006` the vibration felt noticeably weaker, and felt right
again once the four-file set was back — two engine builds compared by hand,
on one person's hand, nothing instrumented.

**What it applies to.** Only a **DualSense** (`054c:0ce6`, `054c:0df2`) that
arrived over **Bluetooth**, which is where the timeouts were counted. A pad on
a cable, a DualShock 4 and every other device are opened shared, exactly as
before. Keyboards and mice cannot be reached by this at all: winebus refuses
everything that is not a joystick or a gamepad before it opens anything. And if
the seizing open fails — another process holds the device exclusively already —
winebus falls back to the shared open and the pad still works.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        SeizeDevice    REG_DWORD   absent: as above. 0: share the pad with
                                   macOS, as before this patch. 1: seize it.

The key is the one `UsbEmulation` and winebus's own `Hidraw` live under, named
after the pad's real ids in lower-case hex with a slash — `054c/0ce6`,
`054c/0df2`. **Unlike `UsbEmulation` this one is on by default** for the pads
above, because it repairs a defect rather than adding a behaviour: without it
the link drops mid-game. Set it to `0` if you would rather share the pad; set
it to `1` on some other pad if you have measured the same timeouts and want to
try it. It is read once, when the bottle's wine starts — not per connect, as
`UsbEmulation` is — so a change applies on the next start of the bottle.

**Not yet measured:** that the seize *stops* the drops. What is measured is the
contention, its consequence, and that wine is the second writer. The first run
with this file belongs in the same log the timeouts were counted in, looking
for the absence of *"Unable to send BT output report"*.

## Making the rumble stronger

**A preference, not a repair, and off unless you ask for it.** Everything above
repairs something that was measured to be wrong. This changes what a game asked
for into something the person holding the pad likes better, which is a different
kind of claim, and a build installed with nothing under the pad's key sends
every packet exactly as the client wrote it.

**What a game actually asks for.** In `steam-hid-game-145815.log` — a two-hour
Bluetooth session of an Unreal title that drives the pad through Unreal's
WinDualShock, with no emulation in the way — the title writes the pad's own
report itself: 6493 output reports of id `0x31`, of which 335 carry a non-zero
motor byte and reach the top of the scale, 36 packets at right 255 with the left
motor at rest and 18 with both at 255. **All 335 carry the same mode**: first
flag byte `0x02`, second `0x00`, byte 38 of the common block `0x04`. The game is
not asking for a weak effect.

**But that is one of two ways to ask.** SDL 2.30.12's own PS5 driver picks
between them by the pad's firmware version: below 2.24 it sets flag0 bit `0x01`,
which its comment calls *"Enable rumble emulation"*, and halves both motor bytes
*"to match Xbox controllers"*; at 2.24 and above it sets byte 38 bit `0x04`,
*"Enable improved rumble emulation"*, unhalved. The log is the second of those.
Compared directly on the pad with a six-pulse ladder at 255 and at 127, **the
legacy compatible vibration at 255 felt clearly stronger** than the haptic mode
at 255, and the haptic mode at 127 felt like the game. **That is a perception,
on one person's hand, and it is the whole of the evidence** — no accelerometer,
no repetition, nothing blind — which is why this has to be switched on.

    HKLM\System\CurrentControlSet\Services\winebus\Devices\<vid>/<pid>
        VibrationMode  REG_DWORD   absent or 0: every packet goes out as the
                                   client wrote it. 1: where the client selected
                                   the haptic path, the legacy compatible motors
                                   are selected instead.
        VibrationGain  REG_DWORD   a percentage over the two motor bytes, absent
                                   or 100 meaning untouched. It saturates at
                                   255, so a game asking for 89 can be made to
                                   ask for more and one already asking for 255
                                   cannot go higher. Clamped to 1000; 0 reads as
                                   absent, and 1 is how to make it nearly
                                   silent.

The same key as `UsbEmulation`, `SeizeDevice` and `ForwardFeatureWrites`, named
after the pad's real ids in lower-case hex with a slash — `054c/0ce6`,
`054c/0df2`. Both are read once, as the device arrives, so a change applies the
next time the pad connects. Only a **DualSense** (`054c:0ce6`, `054c:0df2`) that
arrived over **Bluetooth** is affected; every other device is untouched.

**What is rewritten, exactly.** Flag0 bit `0x01` is set and byte 38 bit `0x04` is
cleared, and nothing else. Flag0 bit `0x02` is left alone because SDL sets it on
both paths and so it is not the choice between them; the second flag byte is left
alone entirely; and the motors are **not** halved the way SDL halves them on that
path, because halving them is exactly what would undo what was preferred.
`VibrationGain` is the separate, explicit way to change the number.

**Both routes.** The title measured writes `0x31` itself, so this cannot live
only in the USB emulation: such a packet is rewritten on the raw route too, in a
copy of the client's buffer, and in the emulation path after the translation.
Either way **the CRC is computed again** over what is actually sent — the pad
drops a `0x31` whose CRC does not cover its bytes, so rewriting without
re-signing would take the rumble away rather than change it. One `TRACE` line per
rewritten packet gives the mode change and both motors before and after.

**Not established:** why the two paths feel different, and what firmware this pad
runs — nothing here reads it. No title has been played with the rewrite on; what
is measured is the ladder, by hand, and the corpus the mode was read out of.

## Optional

An improvement, not a fix. No title in the README's table needs it, every one
of them runs without it, and the **Motor** column does not change.
`make-engine-copy.sh` does not install it, the app's **Set up** does not offer
it, and a launcher reading `manifest.json` finds it under `engineOptional` —
apart from `engine` and `engineSets`, with `"optional": true` — as something to
offer as a switch and never to install unasked.

## Install, status, restore

    runtime/install-engine-controller.sh <engine app>            # install
    runtime/install-engine-controller.sh <engine app> --status   # installed, broken or absent
    runtime/install-engine-controller.sh <engine app> --restore  # put CodeWeavers' four files back

The contract is `install-engine-media.sh`'s: the engine's name is checked
against the stamp, then `copied_from` in its `mgvf-origin.json`, then its
version, and anything else is refused; the originals are kept beside the new
files as `.mgvf-stock`; a backup that is byte for byte our own build is refused
rather than carried; `--status` answers `installed` when all four backups are
there, `broken` when some are, `absent` when none is — so an engine still
carrying the earlier three-file install reads as `broken`, correctly, and
installing puts the fourth file in. Two differences. It refuses while a bottle
is running — `winedevice.exe` holds `winebus.sys`, `winebus.so` and
`ntoskrnl.exe`, and a swap while it runs leaves the bottle half on each set
until it shuts down — in both directions, so close Steam first. And it re-signs
the bundle and clears its quarantine attribute itself, in that order, because it
runs after the engine copy exists with no signing step after it.

## Stripped, and what that means

| file | bytes | exports | imported symbols |
| --- | --- | --- | --- |
| `wine/x86_64-windows/winebus.sys` | 61,440 | 0 | 70 |
| `wine/x86_64-windows/setupapi.dll` | 462,848 | 617 | 210 |
| `wine/x86_64-windows/ntoskrnl.exe` | 393,216 | 1,667 | 678 |
| `wine/x86_64-unix/winebus.so` | 45,576 | 2 | 5 |

The last row is the Mach-O and its two numbers mean something else, which is
said under the table rather than in a fifth column nothing else would fill.

The configured tree compiles PE with `-g` and links with `-Wl,-debug:dwarf`,
so what `make` produces carries a COFF symbol table and six `.debug_*`
sections: 241,664, 1,794,048 and 1,245,184 bytes for the three.
`scripts/build-controller-bus.sh` strips them with `llvm-strip --strip-all` and
proves, as it does so, that the strip changed nothing a loader reads: the export
table and the import table (`Name:` and `Symbol:` lines of `llvm-readobj`,
sorted) compare identical before and after, no `.debug_` section remains and
`SymbolCount` is 0. It refuses to write the stamp otherwise. CodeWeavers' own
files are the same shape — no symbol table, no debug sections — at 43,584,
477,776 and 389,200 bytes. The build directory keeps the unstripped file beside
the shipped one as `<name>.unstripped`.

**The unix half is stripped on the same argument, in the terms a Mach-O has.**
It has no COFF tables and no `.debug_` section to look for — macOS leaves the
DWARF in the `.o` files — so what a loader reads there is the **exported symbol
list** and the **linked libraries**, and what an unstripped file still carries
is a **local symbol table**. `/usr/bin/strip -S -x` takes 60,472 bytes to
45,576; the exported symbols (`nm -gU`) and the linked libraries (`otool -L`)
are listed before and after and compared identical, and the 143 local symbols
must be 0 afterwards, or the stamp is not written. CodeWeavers' own
`winebus.so` is the same shape, with no local symbols, at 85,664 bytes. Two
further checks the PE files do not need, borrowed from
`build-winegstreamer.sh`: no `/opt/cxoffice` path — CodeWeavers' build prefix,
which exists on no Mac — may survive in it, and every `@rpath` dependency must
exist in the engine beside it, or the file cannot load its own dependencies.

`check-builds.sh` reads the **exports** and **imported symbols** columns of this
table and verifies each shipped file against them, and that it is still
stripped — the same way the codec hashes are read out of `CODEC-LICENCES.md`.
For the three PE rows, "imported symbols" is the count of `Symbol:` lines in
`llvm-readobj --coff-imports`, import and delay-import blocks together, DLL
names not counted. For the `x86_64-unix` row the two columns are the count of
**exported symbols** (`nm -gU`: the two `__wine_unix_call*` tables, and nothing
else is meant to be there) and of **linked libraries** (`otool -L`, the file's
own install name excluded). The **bytes** column is as built on 2026-09-08 and
is for the reader; a rebuild that moves any of these numbers should refresh this
table, and `scripts/install-controller-build.sh` prints the row it measured.

## Not the same bytes as CodeWeavers'

The export tables are identical to the engine's stock files: 0, 617 and 1,667
names. The import tables are not, and the difference is not the strip. Our
`setupapi.dll` carries six delay-import blocks (`cabinet`, `shell32`,
`wintrust`, `ole32`, `comdlg32`, `user32`) and our `ntoskrnl.exe` two
(`rpcrt4`, `setupapi`), from `DELAYIMPORTS` in the upstream tree's
`dlls/setupapi/Makefile.in` and `dlls/ntoskrnl.exe/Makefile.in`; CodeWeavers'
files have an empty delay-import directory entry, though their `setupapi.dll`
and `ntoskrnl.exe` still import `ResolveDelayLoadedAPI` and
`DelayLoadFailureHook`. The imported-symbol counts differ with it.
`ntoskrnl.exe`: 678 against 652, and the 26 are exactly the delay-imported
names. `setupapi.dll`: 210 against 184; 25 are delay-imported names, and the
other six are `kernelbase.dll` standing where theirs has `kernel32.dll` for
`DelayLoadFailureHook`, `GetModuleHandleW`, `GetTickCount`, `RaiseException`
and `ResolveDelayLoadedAPI`, plus `__chkstk`. `winebus.sys`: 70 against 68;
ours imports `strcpy` and `wcscpy` and not `memcmp`, a compiler's choice,
takes `_vsnprintf`, `strchr`, `strcmp` and `strlen` from `ntoskrnl.exe` where
theirs takes them from `ucrtbase.dll` and `ntdll.dll`, and imports
`RtlComputeCrc32` from `ntoskrnl.exe`, which is `mgvf-0005`'s CRC and the one
import the patch added. All three are measured working with those
differences. Recorded as an observation: how CodeWeavers'
build resolves those imports has not been looked into.

The unix half differs from theirs too, and in one way worth writing down. Both
export the same two symbols. Ours links five libraries where theirs links six:
theirs takes `@rpath/libinotify.0.dylib`, and carries two extra `LC_RPATH`
entries into `lib64` to find it, and ours does not, because this project's
`configure` line passes `--without-inotify`. inotify is used only by
`bus_udev.c`, which no macOS engine compiles in — `HAVE_UDEV` is not defined
here and the file builds to nothing — so what is lost with the link is nothing
that ran. `ntdll.so`, `IOKit`, `CoreFoundation` and `libSystem` are the same in
both, and ours resolves `ntdll.so` through its own `@loader_path/`, the
directory it is installed into.
