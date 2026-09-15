# The ten binaries in this app, and their licence

`winebus.sys`, `setupapi.dll`, `ntoskrnl.exe`, `hidclass.sys`, `xinput1_1.dll`,
`xinput1_2.dll`, `xinput1_3.dll`, `xinput1_4.dll`, `xinputuap.dll` and
`winebus.so` — shipped here with an `engine-controller-` prefix on each name,
1,352,752 bytes together — are **Wine**, and Wine is **LGPL-2.1-or-later**. They
are not ours in the sense that matters to the licence: they are somebody else's
program with thirty-two patches of ours applied, and both halves of that sentence
carry obligations.

The last is the **unix half** of `winebus`: nine of these are PE files that go
into `lib/wine/x86_64-windows/` and `winebus.so` is a Mach-O that goes into
`lib/wine/x86_64-unix/`. They are built from one source tree and read one
struct, so they only work as a set. The five `xinput` DLLs are one patch and one
source file: Wine builds `xinput1_1`, `1_2`, `1_4` and `xinputuap` from
`xinput1_3`'s sources, and a game links whichever it was built against, so all
five have to ship for any of them to help.

They are redistributed because a fix that only works for people who can build
Wine is not a fix. The licence permits it, and requires this notice to travel
with the binaries — which is why this file is inside the application bundle and
not only in the repository.

## What they were built from

- Upstream project: Wine — <https://www.winehq.org/>
- Source tree: the wine source of **CrossOver 26.3.0.39832**, revision
  **`wine-11.0-8726-g2e2f5fca349`**, which is the build string
  `engine-controller-built-for.json` records beside these files.
- Patches applied on top, all thirty-two of them ours:
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
  - **`mgvf-0006`** — `winebus.so` can open a DualSense that arrived over
    Bluetooth exclusively, so macOS's own driver stops writing to the same pad;
    this is the patch the fourth file exists for. It was the default until
    `mgvf-0034`, and is now asked for by a registry value.
  - **`mgvf-0007`** — the emulation of `mgvf-0005` takes the wired-only audio
    settings back out of a report on the pad's behalf, because a client told the
    pad is on a cable asks for a speaker, a headphone jack and a microphone that
    a DualSense only has on one.
  - **`mgvf-0008`** — an **experiment**, marked as one wherever it is named: in
    that same emulation a feature-report write is answered as if it had
    succeeded and no byte of it goes to the pad, so that the next trace answers
    a question. A registry value puts the write back on the wire.
  - **`mgvf-0009`** — a **preference**, and off unless asked for: two per-device
    values that rewrite the vibration a client asks for, one selecting the pad's
    legacy compatible motors where the client selected its haptic path and one
    scaling the two motor bytes by a percentage.
  - **`mgvf-0010`** — `winebus.sys` gives a DualSense the HID haptics collection
    Wine's own `xinput` looks for, in a top-level collection of its own so the
    pad keeps every byte of its own descriptor, and writes the motors from a
    thread of its own so the game's thread never blocks on the link.
  - **`mgvf-0011`** — `hidclass.sys` offers such a pad to `xinput` as well as to
    everything that had it before, instead of only to one of them.
  - **`mgvf-0012`** — `xinput1_3` learns that a gamepad's axes and buttons come
    in two conventions and not one. Read the wrong way round, a trigger's rest
    position is full deflection on a stick, and the game moves untouched.
  - **`mgvf-0014`** — nothing asked of the vibration, nothing rewritten:
    `mgvf-0009` stored "no options" as all zeros and one of its two write paths
    read that as a request for silence.
  - **`mgvf-0016`** — `mgvf-0009`'s preference reaches a pad on a cable too. It
    was written from a Bluetooth trace and refused a wired pad's own report,
    whose common block starts two bytes earlier and which carries no CRC.
  - **`mgvf-0017`** — the rumble thread can be asked to do everything except the
    write itself, so that the cost of a write can be measured by taking it away
    rather than by reasoning about it.
  - **`mgvf-0018`** — a motor asked to move by less than a few parts in 255 is
    not worth a slot on a Bluetooth link that carries about sixty-five of them a
    second. A stop is always worth one.
  - **`mgvf-0019`** — a pad presented to Windows as wired may still be speaking
    over a radio, so the transport is asked of the radio and not of the
    presentation.
  - **`mgvf-0020`** — the motors ride the game's own packet. A title rumbling
    through `xinput` still writes an empty output report every frame, so the
    motors go out inside that one and no packet of ours is added to the link.
  - **`mgvf-0021`** — the other way to ask a DualSense to vibrate: the pad's own
    haptic path, which is the one Sony's library uses, instead of the legacy
    rotating-mass emulation this driver had always sent.
  - **`mgvf-0022`** — an absent preference is a neutral request and not a zeroed
    one, so a gain of zero can go on meaning silence.
  - **`mgvf-0023`** — the motor power field, and the bit that says the packet
    means it. Sony's library claims it on every packet and this driver never
    has, so a pad left reduced by something else stayed reduced.
  - **`mgvf-0024`** — on Bluetooth the motors ride the game's own packet and
    take the pad's own haptic path, by default. Both were measured first, the
    stop included: 105 episodes of rumble, 105 of them ended in a zero.
  - **`mgvf-0025`** — and a cable answers the path question the same way a
    radio does. One menu entry that meant the finer path or the harder one
    depending on whether a wire was plugged in is not a setting, it is a pad
    that feels inconsistent.
  - **`mgvf-0026`** — the rumble deadband measures the movement at the pad
    rather than the one the game asked for, so a higher strength stops making
    the rumble coarser; and it no longer holds back a packet that was leaving
    anyway.
  - **`mgvf-0027`** — the Sony axis order is chosen by reading the descriptor
    and not the vendor badge, so a pad whose descriptor wine replaced on the
    SDL route stops being given an order that descriptor is not in.
  - **`mgvf-0028`** — the small device that carries the motors is offered to
    `xinput` and to nothing else, so a client that finds its controller by
    vendor id no longer sees two of them and refuses to start.
  - **`mgvf-0029`** — that same device can be asked to declare no button at all,
    for a title that loses a held button while the motors are offered to
    `xinput`.
  - **`mgvf-0030`** — and that becomes the default, because it was measured: a
    held L3 survives without the button and does not with it. `xinput` stops
    allocating from a button count it has not checked, so a device with no
    buttons is no longer refused for having none.
  - **`mgvf-0031`** — `winebus.sys` can put a lightbar colour and a player
    number the user chose into the light changes a client already sends, per
    pad model and off unless a registry value asks for it. It adds no packet
    while you play, except a one-time lightbar release on Bluetooth whose need
    is not measured yet; a title that never sends a light change gets nothing.
  - **`mgvf-0032`** — `winebus.so` tells SDL, before it starts, not to open a
    pad the hidraw route already owns, so SDL's own driver stops holding a
    second handle to it and writing a keep-alive the seize refuses.
  - **`mgvf-0033`** — `winebus.so` asks a DualSense on Bluetooth that it holds
    seized to turn itself off after a stretch with no stick, trigger or button
    input, 20 minutes unless a registry value says otherwise. It does nothing
    once wine has exited, and inside wine it is not measured yet.
  - **`mgvf-0034`** — `winebus.so` shares a DualSense with macOS by default
    instead of seizing it; a registry value still asks for the seize. Measured
    shared over three sessions of play and a day of Bluetooth log with no drop
    during play.
  - **`mgvf-0035`** — `winebus.sys` sends the lightbar colour and player lights
    a user chose again, as a lights-only packet with no motor in it, whenever
    two seconds pass without one, because macOS repaints the lightbar of a pad
    it shares. On a pad it is not measured yet.
- Built by `scripts/build-controller-bus.sh` in the repository below, which also
  strips the binaries and proves that the strip changed nothing a loader reads.

The thirty-two patch files are published, in full, as
`source-patches/mgvf-0002-*.patch` … `mgvf-0035-*.patch` in

> **<https://github.com/MathiasKowoll/MacGameVideoFix>**

They modify LGPL code and are therefore LGPL themselves; nothing in them is
specific to this project, and anyone carrying a patched Wine is welcome to them.

## Corresponding source

The LGPL asks that whoever receives these binaries can get the source they were
built from. Both halves are public and neither is behind us: Wine's source at
the revision above is CodeWeavers' published CrossOver source for 26.3.0.39832,
and the thirty-two patches are in the repository named above. Anyone who cannot
obtain either should ask through that repository's issues and it will be
provided.

Unlike the codec payload in MacGameVideoFix, these four are **not** separate
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
| `engine-controller-winebus.sys` | 81,920 | `31cb2a2e285af3f62836322dfaf382895cc0d0bf9b7f89bed632a76f7d99c603` |
| `engine-controller-setupapi.dll` | 462,848 | `5b6b893ef3721150dfab7675c5f26b2a09aa29dbcd262e90058c002b1ccadbd6` |
| `engine-controller-ntoskrnl.exe` | 393,216 | `fdb9a3b606259d0f2d7e23fe4ad488db170d7e325715dc5f64f9f385b7f903d5` |
| `engine-controller-hidclass.sys` | 53,248 | `28ab707ffe16047316f2f9348ef6c5c4dd83a128b3d330e0f5e18299cff6ebb7` |
| `engine-controller-xinput1_1.dll` | 57,344 | `6cb3d6ea89a9797a555d4759329edfb440972617cb5a5d9595ba6695647dbd5c` |
| `engine-controller-xinput1_2.dll` | 57,344 | `64066e3b618fe69d88bcdbec9ac3bb9c1ad401f06b10f293ab6d5713a09e2849` |
| `engine-controller-xinput1_3.dll` | 57,344 | `4d4c750960926296da85685b21b4c12b71c6724c434dbcf751372df455a2c7ce` |
| `engine-controller-xinput1_4.dll` | 61,440 | `26b979fcd7558a1c92cc5a64411a00073b2d2ae5ef05bb0aedfb4174b1c00054` |
| `engine-controller-xinputuap.dll` | 61,440 | `550ace3262451322e8a342816b1a07db0fe4bfad02628836e3885dc49bf13adf` |
| `engine-controller-winebus.so` | 66,608 | `3b9d5e3e8b68e51551f1e27b9b7a68c4fa71424f1952eedbb155813b7c25f2f6` |
| `engine-controller-built-for.json` | 455 | `53f3c8ca4fa23be2b84a16c1931d882b652273f56b96b39a7fd333785ac5e0f4` |

The same bytes ship in the repository as `runtime/engine-controller-*` and, laid
out the way an engine is, as `runtime/engine-payload-controller/wine/x86_64-windows/`
and `.../wine/x86_64-unix/`.
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
