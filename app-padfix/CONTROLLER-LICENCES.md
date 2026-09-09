# The three binaries in this app, and their licence

`winebus.sys`, `setupapi.dll` and `ntoskrnl.exe` — shipped here as
`engine-controller-winebus.sys`, `engine-controller-setupapi.dll` and
`engine-controller-ntoskrnl.exe`, 913,408 bytes together — are **Wine**, and
Wine is **LGPL-2.1-or-later**. They are not ours in the sense that matters to
the licence: they are somebody else's program with five patches of ours applied,
and both halves of that sentence carry obligations.

They are redistributed because a fix that only works for people who can build
Wine is not a fix. The licence permits it, and requires this notice to travel
with the binaries — which is why this file is inside the application bundle and
not only in the repository.

## What they were built from

- Upstream project: Wine — <https://www.winehq.org/>
- Source tree: the wine source of **CrossOver 26.3.0.39832**, revision
  **`wine-11.0-8726-g2e2f5fca349`**, which is the build string
  `engine-controller-built-for.json` records beside these files.
- Patches applied on top, all five of them ours:
  - **`mgvf-0002`** — `winebus.sys` names the bus a device is on in its
    compatible ids, so `BTHENUM` is there for a client to find.
  - **`mgvf-0003`** — `setupapi.dll` answers `CM_Get_Parent` for HID children,
    which was a stub, so there is a parent devnode to ask at all.
  - **`mgvf-0004`** — `ntoskrnl.exe` refreshes a device's hardware and
    compatible ids on every enumeration rather than only the first time it is
    ever seen, so the first two are not defeated by the record of the first boot.
  - **`mgvf-0005`** — `winebus.sys` can present a DualSense on Bluetooth as if
    it were on USB, per device and off by default, for the two clients that
    insist on a wired pad.
  - **`mgvf-0007`** — the same emulation takes the wired-only audio settings
    back out of a report on the pad's behalf, because a client told the pad is
    on a cable asks for a speaker, a headphone jack and a microphone that a
    DualSense only has on one.
- Built by `scripts/build-controller-bus.sh` in the repository below, which also
  strips the binaries and proves that the strip changed nothing a loader reads.

The five patch files are published, in full, as
`source-patches/mgvf-0002-*.patch` … `mgvf-0007-*.patch` in

> **<https://github.com/MathiasKowoll/MacGameVideoFix>**

They modify LGPL code and are therefore LGPL themselves; nothing in them is
specific to this project, and anyone carrying a patched Wine is welcome to them.

## Corresponding source

The LGPL asks that whoever receives these binaries can get the source they were
built from. Both halves are public and neither is behind us: Wine's source at
the revision above is CodeWeavers' published CrossOver source for 26.3.0.39832,
and the five patches are in the repository named above. Anyone who cannot obtain
either should ask through that repository's issues and it will be provided.

Unlike the codec payload in MacGameVideoFix, these three are **not** separate
replaceable libraries — they are the patched program itself. That is why the
patches are published rather than only described: replacing our build with your
own means rebuilding Wine with them, and the material to do that is what this
section points at.

## Checksums of the files as shipped

One line per file, name then sha256, so a redistributed binary that has been
swapped is caught by whoever received it rather than by whoever it stops working
for.

| file | bytes | sha256 |
| --- | --- | --- |
| `engine-controller-winebus.sys` | 57,344 | `cc1e009abd258645c91a3c2778340fa1b5369712677c09e78d85036e0001324f` |
| `engine-controller-setupapi.dll` | 462,848 | `048cb4b31252376402466974a7751d8ef8a0f4b2fe2a80af6c2637608a15e231` |
| `engine-controller-ntoskrnl.exe` | 393,216 | `3a408663ec9391c134088cf1263d57e186775f2352b476e09921a16da3d0f10f` |
| `engine-controller-built-for.json` | 185 | `42fd7076e68075804d0164dabad77b006d8bf0c9adecd4bfcae0e2bc483f9c2b` |

The same bytes ship in the repository as `runtime/engine-controller-*` and, laid
out the way an engine is, as `runtime/engine-payload-controller/wine/x86_64-windows/`.
`runtime/check-builds.sh` compares those two copies, and `app-padfix/build-app.sh`
compares this one against `runtime/`.

**If you rebuild these files, change this table.** A copy taken from a build
outlives the build it came from; this project has already had three binaries sit
in an engine for days with nobody able to say where they came from.

## The rest of the application

`MacGamePadFix` itself and `install-engine-controller.sh` are part of
MacGameVideoFix and are **GPL-3.0-or-later**. The application does not link
against Wine — it copies files and runs a shell script — so the two licences do
not meet.
