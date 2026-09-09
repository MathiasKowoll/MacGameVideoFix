# MacGamePadFix

A small macOS application that installs one thing into one CrossOver: the
**controller-bus engine set** this project builds, three patched Wine files that
let a Windows game learn whether a controller is on USB or on Bluetooth.

It is the sibling of `app/`, and it is separate on purpose. MacGameVideoFix is
about cutscenes and knows about games, bottles and Steam libraries;
this one carries three files and does three things — say what is installed,
install it, put CrossOver's files back. It is meant to be handed to somebody who
has this one problem and nothing else.

## What it fixes

A DualSense on Bluetooth does not rumble under CrossOver, in any Windows game,
and its PS button and touchpad do not work either. Over a cable the same pad
does all three.

The reason is not the game and not the pad. A DualSense on Bluetooth wants
output report `0x31`, with a CRC; over USB it wants `0x02`, and it silently
refuses the wrong one. Every Windows client decides which to send the same way:
it asks the HID device's parent devnode for its compatible ids and looks for
`BTHENUM`. Under Wine that question could never be answered — `CM_Get_Parent`
was a stub, and `winebus` named no bus at all — so the answer was always "not
Bluetooth" and the pad was always sent the report it refuses. Steam's own log
says `bluetooth 0` for a pad that is on Bluetooth.

With this set installed the answer is the true one. **Rumble, the PS button and
the touchpad work over Bluetooth**, measured on 2026-09-08 in every title tried;
trigger effects ride in the same report, and a title that sends them over
Bluetooth is reported working.

This is an **improvement, not a fix**: no game in this project's table needs it,
and every one of them runs without it. Nothing here makes a game start, or run
faster, or show a cutscene.

## Which CrossOver, and why exactly one

**Stable CrossOver 26.3.0.39832, and nothing else.** The application refuses any
other version, and shows you the refusal rather than working around it.

That is not caution for its own sake. These three files are Wine, built from the
Wine source of that exact CrossOver, and they are replacing three files that the
rest of that same Wine is compiled against. Mixing Wine binaries across versions
does not fail loudly — it produces a bottle that starts, runs, and then
misbehaves somewhere nobody would connect to a controller patch. Refusing is the
only honest answer, so the installer checks the version stamped in
`engine-controller-built-for.json` against the CrossOver you point it at.

It checks the **name** as well, and for a reason worth knowing before it
surprises you: a stock `CrossOver.app` and a copy this project has patched report
the *same* `CFBundleVersion`, so the name is the only thing that tells them
apart. A copy made by MacGameVideoFix records where it came from in
`Contents/SharedSupport/CrossOver/mgvf-origin.json`, and is accepted through
that. Any other CrossOver under any other name is refused, with the stamp shown.

It also **refuses while a bottle is running**, in both directions. `winebus.sys`
and `ntoskrnl.exe` are loaded in a running bottle's `winedevice.exe`; swapping
them under it would leave the bottle half on one set and half on the other until
it shut down. Quit Steam, let the bottle shut down, and try again.

## What it changes, and how to undo it

Three files inside the CrossOver application, in
`Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/`:

    winebus.sys
    setupapi.dll
    ntoskrnl.exe

CodeWeavers' originals are kept **beside** them, as `winebus.sys.mgvf-stock` and
so on. Nothing is deleted and nothing is downloaded.

Replacing sealed files inside a signed application breaks its signature, and a
broken seal is what Finder calls "damaged", so the installer **re-signs the
CrossOver application ad hoc** and clears its quarantine attribute. That replaces
CodeWeavers' own signature on your copy of CrossOver. If that is not a trade you
want to make, do not install this.

**To undo it:** press **Restore CrossOver's files**. The three `.mgvf-stock`
files are moved back over the patched ones and the application is re-signed
again. The window says `Not installed` afterwards, and that is the state
CrossOver shipped in.

**A CrossOver update undoes it too**, without asking. An update replaces
CodeWeavers' three files with CodeWeavers' three files, and the fix is simply
gone; the window will say `Not installed` and you install it again. If an update
also changes the version, this build no longer serves it and will refuse — that
is the same guard as above doing its job.

`Half installed` means some of the three backups are there and some are not. The
three only work together, so that is neither state; install again, or restore,
and the log says what happened.

## Running it the first time

The application is signed **ad hoc**, not with a Developer ID and not notarised,
so macOS will refuse to open it on a double click and say it cannot be checked
for malicious software. That is the signature, not the file being wrong.

1. Find `MacGamePadFix.app` in Finder.
2. **Right click** it (or Control-click) and choose **Open**.
3. A dialog appears with the same warning and an **Open** button that the double
   click did not offer. Press **Open**.

macOS remembers the decision; from then on it opens normally. On recent macOS
versions the right click may not offer the dialog either — in that case open
**System Settings → Privacy & Security**, scroll to the message naming
MacGamePadFix, and press **Open Anyway**.

## Where the binaries come from

They are **Wine**, and Wine is **LGPL-2.1-or-later**. They are built from the
Wine source of CrossOver 26.3.0.39832, revision `wine-11.0-8726-g2e2f5fca349`,
with eight patches of this project's on top — `mgvf-0002` through `mgvf-0009` —
and all eight are published in full, as patch files, in `source-patches/` of

> <https://github.com/MathiasKowoll/MacGameVideoFix>

`CONTROLLER-LICENCES.md`, which ships **inside the application bundle**, says
what each patch does, gives the sha256 of every file as shipped, and says where
the corresponding source is. Read it before redistributing these binaries; the
licence asks that it travel with them.

Three of the four files are PE and go into `lib/wine/x86_64-windows/`; the
fourth, `winebus.so`, is the unix half of `winebus` and goes into
`lib/wine/x86_64-unix/`. They come from one source tree and read one struct, so
they only work as a set.

Three of the eight are worth knowing about, and all three do nothing unless a
registry value is set — `runtime/engine-payload-controller/README.md` in the
repository says how, for each of them. `mgvf-0005` lets a DualSense on Bluetooth
be *presented* as if it were on USB, per device and off by default, for the two
clients that refuse to work with a pad they know is wireless; the same file says
plainly that the driver half has not yet run against a live pad. `mgvf-0008` is
an **experiment** rather than a fix and is marked as one wherever it is named.
`mgvf-0009` is a **preference** rather than either: it can rewrite the vibration
a game asks for into the pad's other rumble mode, and scale it, on the strength
of one person reporting that the other mode feels stronger.

## Building it

    app-padfix/build-app.sh

One Swift file, `swiftc`, no dependencies. It copies six files out of `runtime/`
unchanged — the installer, its four engine files and their stamp — writes the
`Info.plist`, reuses `app/AppIcon.icns`, and signs ad hoc. It fails before
compiling anything if one of the six is missing, checks afterwards that every file the installer
names is in the bundle, and checks that the copies are byte for byte what
`runtime/` holds.

The application reimplements none of the installer's logic. It finds CrossOver
installs, runs `install-engine-controller.sh` — with `--status`, with no verb to
install, with `--restore` to undo — and shows what the script says, including
its refusals, word for word. Anything you can read about that script in the
repository's README or its wiki is true of this application, because it is the
same script.
