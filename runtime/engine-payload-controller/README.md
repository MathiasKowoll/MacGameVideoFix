# engine-payload-controller

The optional controller-bus set, laid out the way a CrossOver engine is, so a
patcher can copy this tree over the engine it is building without a mapping
table of its own:

    wine/x86_64-windows/winebus.sys    ->  <CX>/lib/wine/x86_64-windows/winebus.sys
    wine/x86_64-windows/setupapi.dll   ->  <CX>/lib/wine/x86_64-windows/setupapi.dll
    wine/x86_64-windows/ntoskrnl.exe   ->  <CX>/lib/wine/x86_64-windows/ntoskrnl.exe

`built-for.json` and this file are not copied into the engine.

This tree is a **sibling** of `engine-payload/`, not a part of it, on purpose:
overlaying `engine-payload/` installs the media set and nothing else, and
overlaying this tree installs this set and nothing else. A patcher chooses per
set. The same bytes ship flat beside `runtime/install-engine-controller.sh` as
`engine-controller-winebus.sys`, `engine-controller-setupapi.dll`,
`engine-controller-ntoskrnl.exe` and `engine-controller-built-for.json`, and
`check-builds.sh` compares the two copies. The `engine-controller-` prefix is
deliberate: the media installer picks its set by reading `engine-built-for*.json`
and `engine-winegstreamer*`, and these must never be taken for one of those.

## What this is

Three PE files built from the engine's own wine source — the revision
`built-for.json` records — with four patches of ours on top: `mgvf-0002`,
`mgvf-0003`, `mgvf-0004` and `mgvf-0005`. `source-patches/README.md` says what
each one does. There is no unix half and no decoder: nothing in these files links
against the engine, so one build serves every engine of the name and version
the stamp records, and a copy this project made is served through `copied_from`
in its `mgvf-origin.json`, the way the media set is. Read `built-for.json`
before installing and **check both fields**: a patched fork and a stock
CrossOver report the same version, and the app name is what tells them apart.

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
    runtime/install-engine-controller.sh <engine app> --restore  # put CodeWeavers' three files back

The contract is `install-engine-media.sh`'s: the engine's name is checked
against the stamp, then `copied_from` in its `mgvf-origin.json`, then its
version, and anything else is refused; the originals are kept beside the new
files as `.mgvf-stock`; a backup that is byte for byte our own build is refused
rather than carried; `--status` answers `installed` when all three backups are
there, `broken` when some are, `absent` when none is. Two differences. It
refuses while a bottle is running — `winedevice.exe` holds `winebus.sys` and
`ntoskrnl.exe`, and a swap while it runs leaves the bottle half on each set
until it shuts down — in both directions, so close Steam first. And it re-signs
the bundle and clears its quarantine attribute itself, in that order, because it
runs after the engine copy exists with no signing step after it.

## Stripped, and what that means

| file | bytes | exports | imported symbols |
| --- | --- | --- | --- |
| `wine/x86_64-windows/winebus.sys` | 57,344 | 0 | 70 |
| `wine/x86_64-windows/setupapi.dll` | 462,848 | 617 | 210 |
| `wine/x86_64-windows/ntoskrnl.exe` | 393,216 | 1,667 | 678 |

The configured tree compiles PE with `-g` and links with `-Wl,-debug:dwarf`,
so what `make` produces carries a COFF symbol table and six `.debug_*`
sections: 225,280, 1,794,048 and 1,245,184 bytes for the three.
`scripts/build-controller-bus.sh` strips them with `llvm-strip --strip-all` and
proves, as it does so, that the strip changed nothing a loader reads: the export
table and the import table (`Name:` and `Symbol:` lines of `llvm-readobj`,
sorted) compare identical before and after, no `.debug_` section remains and
`SymbolCount` is 0. It refuses to write the stamp otherwise. CodeWeavers' own
files are the same shape — no symbol table, no debug sections — at 43,584,
477,776 and 389,200 bytes. The build directory keeps the unstripped file beside
the shipped one as `<name>.unstripped`.

`check-builds.sh` reads the **exports** and **imported symbols** columns of this
table and verifies each shipped file against them, and that it is still
stripped — the same way the codec hashes are read out of `CODEC-LICENCES.md`.
"Imported symbols" is the count of `Symbol:` lines in `llvm-readobj
--coff-imports`, import and delay-import blocks together, DLL names not
counted. The **bytes** column is as built on 2026-09-08 and is for the reader;
a rebuild that moves any of these numbers should refresh this table, and
`scripts/install-controller-build.sh` prints the row it measured.

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
