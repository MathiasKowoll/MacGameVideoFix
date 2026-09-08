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
`built-for.json` records — with three patches of ours on top: `mgvf-0002`,
`mgvf-0003` and `mgvf-0004`. `source-patches/README.md` says what each one
does. There is no unix half and no decoder: nothing in these files links
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
| `wine/x86_64-windows/winebus.sys` | 53,248 | 0 | 69 |
| `wine/x86_64-windows/setupapi.dll` | 462,848 | 617 | 210 |
| `wine/x86_64-windows/ntoskrnl.exe` | 393,216 | 1,667 | 678 |

The configured tree compiles PE with `-g` and links with `-Wl,-debug:dwarf`,
so what `make` produces carries a COFF symbol table and six `.debug_*`
sections: 200,704, 1,794,048 and 1,245,184 bytes for the three.
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
and `ResolveDelayLoadedAPI`, plus `__chkstk`. `winebus.sys`: 69 against 68;
ours imports `strcpy` and `wcscpy` and not `memcmp`, a compiler's choice, and
takes `_vsnprintf`, `strchr`, `strcmp` and `strlen` from `ntoskrnl.exe` where
theirs takes them from `ucrtbase.dll` and `ntdll.dll`. All three are measured
working with those differences. Recorded as an observation: how CodeWeavers'
build resolves those imports has not been looked into.
