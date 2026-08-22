# MacGameVideoFix

Makes Windows games show their cutscenes under CrossOver on Apple Silicon.

> ### This is for CrossOver Preview
>
> Specifically **`crossover-preview-arm64-20260821`**. That is where every title
> here is measured, and it is the only configuration this project supports.
>
> Three of the nine also run on stable CrossOver 26.3 and the table says which.
> Treat that as a bonus: stable is not tested before a release, and what stops
> the other three is in the engine rather than in anything installed beside the
> game.

Nine games so far. They install the same way: open the app, pick the game from
the list, drop its folder on it, press Apply.

| Game | Symptom | CrossOver |
| --- | --- | --- |
| [**Mortal Shell 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Mortal-Shell-2) | Crash on the first cutscene | 26.3 and Preview 20260821 |
| [**Life is Strange: Reunion**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) | Runs, then freezes after a while | Preview 20260821 — freezes on 26.3 |
| [**Life is Strange: Double Exposure**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure) | Runs, then freezes after a while | Preview 20260821 — freezes on 26.3 |
| [**Beast of Reincarnation**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation) | Startup video plays with sound, no picture | 26.3 and Preview 20260821 |
| [**Persona 5 Strikers**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers) | Video never starts; sound only | 26.3 and Preview 20260821 |
| [**DYNASTY WARRIORS: ORIGINS**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins) | Cutscene plays with sound, picture black | Preview 20260821 — no video on 26.3 |
| [**Nioh**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh) | Cutscene refuses to play, then crashes | Preview 20260821 — not tried on 26.3 |
| [**Nioh 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh-2) | Cutscene refuses to play, then crashes | Preview 20260821 — not tried on 26.3 |
| [**Nioh 3**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh-3) | Failed to play movie | Preview 20260821 — not tried on 26.3 |

**This project targets `crossover-preview-arm64-20260821`**, and that is what
"Preview" means in the column above and everywhere else in this file. Every title
is measured and supported there.

Three also run on stable 26.3, and the column says so — but that is a bonus
rather than a promise. Stable is not what gets tested before a release, and a
title that stops working there will not hold one up.

That is a choice, not neglect. Preview is far enough ahead — a newer D3DMetal, a
larger GStreamer plugin set, a newer Wine — that supporting both would mean
rebuilding each fix against the older engine and keeping it there. What stops the
other three is in the engine rather than in anything installed beside the game:
the two Life is Strange titles freeze on 26.3 with the fix removed exactly as
they do with it, and DYNASTY WARRIORS needs a WebM demuxer that 26.3 has no way
to reach.

Each row links to a page in the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki) with that game's
findings and fix. Why each fault happens, why each fix is shaped the way it is,
and what was tried and did not work, is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings).

Tested on an M4 Max, macOS 27, against CrossOver 26.3 and
`crossover-preview-arm64-20260821`, with Game Porting Toolkit 4.0b2.

---

## Quick start

Read [Requirements](#requirements) first. Six of the nine titles have only been
measured on CrossOver Preview, three of those crash on stable 26.3, and three
need a GStreamer package installed before they can work at all. Each of those
looks exactly like the fix not working.

1. Download `MacGameVideoFix.app` from [Releases](../../releases), or build it
   yourself with `app/build-app.sh`.
2. Open the app, **pick your game from the list**, drop its folder on it, and
   press **Apply Fix**.

Picking the game first is what tells the app which folder to ask for, and it
says so on the drop zone. It also checks that the game's shipping executable is
really under the folder you dropped, and refuses the folder if it is not, naming
the file it could not find. There is no continue-anyway.

The list carries one more entry, **Another Unreal Engine 5 title**, which is
how an untried Unreal game is attempted. It is the one entry with no shipping
executable to check against, so it is taken at its word — and no claim that the
fix works applies to it. An untried title is best tried on Preview.

**Revert** puts everything back.

### CrossOver and bottles

The **CrossOver and bottles…** button opens the two things that live outside
the game folder. **Stage codec** borrows the VC-1 decoder Persona 5 Strikers
needs from your own GStreamer install, once per installed CrossOver. The list
under it says which CrossOver each bottle runs under and which staging it points
at, repairs a bottle whose CrossOver has changed since it was last configured,
and lets you name the CrossOver a bottle uses when the automatic answer is
wrong.

The other five titles need nothing from that sheet.

### All your games at once

There is a second way in, and it is the shorter one if more than one of these is
installed. **Find my games** or **Choose folder…** points the app at a Steam
library; it then scans, shows what it found and what it would do to each row,
and applies the lot in one pass.

Nothing is read until you press Scan — choosing a folder names it rather than
acts on it — and rows the app cannot act on carry no checkbox and say why
instead. It exists because of the naming trap described below: Steam names
install directories after the project rather than the game, which is the one
thing a person has no reason to know.

That scan stays on the machine it runs on. Nothing it finds belongs in a
published page or in a bug report.

### Which folder to pick, by hand

One game at a time, which is what the drop zone is for and the fallback when a
scan does not turn a title up.

For **DYNASTY WARRIORS: ORIGINS**, the folder holding `DWORIGINS.exe` — usually
`steamapps/common/DWORIGINS`.

For **Persona 5 Strikers**, the folder holding `game.exe` — usually
`steamapps/common/P5S`.

For an **Unreal title**, the game's own folder — the one with `Engine` in it:

```
…/steamapps/common/Sparta                   ← drop this one
├── Engine/
│   └── Binaries/ThirdParty/Ogg/Win64/      ← the fix rides in here
└── MortalShell2/
    ├── Binaries/Win64/                     ← the shipping executable
    └── Content/
```

The tell is `Engine/Binaries/ThirdParty/Ogg/Win64`. That is where the proxy DLL
goes, and an Unreal title that ships no `libogg` cannot take this fix at all —
the app says so rather than guessing.

Dropping the Steam library folder works too; the app looks one level down.

Note that Steam names install directories after the project, not the game.
Mortal Shell 2 lives under `Sparta`, Reunion under `LifeisStrangeReunion`, and
their executables are `MortalShell2-Win64-Shipping.exe` and
`Iris-Win64-Shipping.exe` respectively. Browse by path rather than by the name
on the store page — and since the app checks the executable against the title
you picked, it will tell you when the two disagree.

### While it runs

The app backs everything up first and has a **Revert** button. It shows a
progress bar and streams the underlying scripts' output live, so you can see
which file it is working on rather than staring at a frozen window.

Once the fix is applied, **Apply Fix** is disabled until you revert. Applying
twice would move the proxy DLL aside as though it were the game's own and lose
the original.

Because the app is signed ad-hoc rather than notarised, macOS will refuse the
first launch. Right click it and choose **Open**, then confirm.

## Requirements

- **Apple Silicon Mac.** The app targets macOS 14 or later, which is what it
  will launch on. Every measurement here was made on macOS 27, so anything older
  is untried rather than known good.
- **CrossOver.** Which build each title was measured on is the table at the top
  of this file, and why this project targets Preview is in the
  [disclaimer](#why-this-targets-crossover-preview).
- **For Persona 5 Strikers only:** the official GStreamer runtime package,
  [1.24.14](https://gstreamer.freedesktop.org/data/pkg/osx/1.24.14/)
  (`gstreamer-1.0-1.24.14-universal.pkg`; the development package is not
  needed), installed at `/Library/Frameworks`. It is the only title here needing
  a codec CrossOver does not ship, and nothing is redistributed — the decoder is
  borrowed from an install you already have. Where it comes from, and why it is
  staged rather than patched in, is in
  [Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-staged-codecs-and-where-they-come-from).

## What goes in the bottle

Two settings live in `cxbottle.conf` rather than beside the game.

`CX_GRAPHICS_BACKEND` is a requirement rather than a preference, and it divides
these titles into two groups that cannot share a bottle:

| Backend | Games |
| --- | --- |
| `d3dmetal` | Mortal Shell 2, both Life is Strange, Beast of Reincarnation, DYNASTY WARRIORS: ORIGINS |
| `dxmt` | **Persona 5 Strikers**, and only it |

So Persona 5 Strikers wants a bottle of its own. Steam libraries are shared
between bottles, so a second bottle sees the same installed games without
re-downloading anything. Why nothing but DXMT will do is on
[the title's page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers).

`GST_PLUGIN_PATH` points at the staged VC-1 decoder, and **the app writes it for
you**:

```
"GST_PLUGIN_PATH" = "…/MacGameVideoFix/gst-codecs/<CrossOver engine version>/x86_64/gstreamer-1.0"
```

The middle component is the `CFBundleVersion` of the CrossOver that bottle runs
under — the same string the bottle records as its own `"Version"` — and a
staging built for one engine is not usable under another. So the app writes the
line only into the bottle the title runs in, takes it back out of bottles that
have no use for it, and re-points a bottle whose CrossOver has changed. There is
nothing to edit by hand. Why the staging is per engine is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-staged-codecs-and-where-they-come-from).

Both settings are read when the bottle starts, and a live `wineserver` keeps the
old copy — so **close Steam completely**, not just the game. A setting that has
not taken looks identical to a setting that did not work.

## Working from a clone

The app carries the installers and the prebuilt DLLs inside its bundle and
runs them itself. Reproducing a fix by hand, or building a proxy for a title
that has none, is a maintainer's job rather than a user's, and it is written
up in
[Running the scripts directly](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Running-the-scripts).

## Troubleshooting

**Steam's "verify integrity of game files" undoes this.** It puts the game's own
carrier DLL back and the proxy is gone. Same after a game patch. Run the fix
again.

**Still crashing in `AllocateBuffer`** — the proxy is not being loaded. Check
what the app reports, and look for `C:\ue5-media-fix.log` in the bottle's
`drive_c`: if it does not exist, the DLL never ran.

**Still freezing after a while** — check the same log for the node guard's
refusal line. If it is not there, the freeze is something else, and
[Diagnosing a new game](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
covers how to look.

**Persona 5 Strikers installs cleanly and still shows nothing** — check the two
things that live outside the game folder: the bottle's backend has to be `dxmt`,
and `GST_PLUGIN_PATH` has to point at a staged decoder. Both are read when the
bottle starts, so close Steam completely before retrying.

**An Unreal title with no `libogg`** cannot take the runtime patch — that fix
rides in on it.

**A game that is not listed.**
[Diagnosing a new game](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
is the route: what to run, in what order, and what each answer rules out. Which
of these faults are properties of an engine rather than of a title, and how a
carrier DLL is picked, is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#other-games).

**If none of that helped**, open an issue on
[Issues](../../issues) saying which game, which CrossOver build, which backend
the bottle is set to, and what the log above did or did not contain.

## Building

Everything here is built from the source repository rather than from the app
download.

### The app

```bash
app/build-app.sh
```

Needs Xcode's Swift toolchain. Produces `app/MacGameVideoFix.app`, ad-hoc
signed, targeting arm64 macOS 14 and later.

### The proxy DLLs

The release ships these prebuilt. To build your own you need
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) — or any
`x86_64-w64-mingw32` toolchain — on `PATH` or in `MINGW_BIN`. The first argument
is the game's own untouched carrier DLL: its export table is read from it so the
forwarder list matches exactly.

```bash
runtime/build-proxy.sh "/path/to/<Game>/Engine/Binaries/ThirdParty/Ogg/Win64/<VS20xx>/libogg_64.dll" ue5-media-fix.c
runtime/build-proxy.sh "/path/to/DWORIGINS/libxess.dll" dwo-video-bridge.c
runtime/build-proxy.sh "/path/to/P5S/amd_ags_x64.dll" p5s-video-bridge.c
```

Name the source explicitly. `build-proxy.sh` defaults to `ue5-runtime-fix.c`,
which is the pre-merge Unreal source; `ue5-media-fix.c` is what the shipping
`libogg_64.dll` is built from. The `VS20xx` subfolder under `Ogg/Win64` changes
between engine versions, so check what your copy has rather than assuming.

## Credits

- [CrossOver](https://www.codeweavers.com/crossover) by CodeWeavers, and
  [Wine](https://www.winehq.org/) underneath it.
- **[GStreamer](https://gstreamer.freedesktop.org)**, whose official macOS build
  supplies the VC-1 decoder Persona 5 Strikers needs. It is borrowed from an
  installation you already have, never redistributed here.
- **[winevideo](https://github.com/Jfishin/winevideo) by Jfishin.** None of this
  would exist without it. Its patches are where these faults were first
  identified: that Electra will accept NV12 and nothing else, that CrossOver
  censors that format on macOS, that Electra decides in software by asking its
  own platform handle, and that a D3D9 surface has to be bridged rather than
  shared. The idea of importing codecs from the user's own official GStreamer
  install rather than shipping any is winevideo's too. How the two projects
  differ, and what each reaches that the other cannot, is in
  [Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-staged-codecs-and-where-they-come-from).
- [DXMT](https://github.com/3Shain/dxmt) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), whose
  source made the root cause legible.

## License

[GPL-3.0-or-later](LICENSE). The tooling here exists because Wine, vkd3d and
DXMT are free software that can be read and modified — copyleft keeps any
derivative of this work equally available.

## Disclaimer

Unofficial community tooling, provided as-is. It modifies files in your game
installation; everything is backed up and reversible, but back up anything you
care about first. Not affiliated with or endorsed by CodeWeavers, Apple, or any
of the publishers or developers of the games listed here.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop. Everything
here is for single-player titles whose cutscenes do not play.

### Why this targets CrossOver Preview

This project targets `crossover-preview-arm64-20260821`. That is the build every
measurement here was taken against, and it is named rather than called "Preview"
because the reason is a property of that build rather than of the Preview line
in general.

The reason is measured, and it is narrower than it looks. Comparing the two
installs plugin by plugin on this machine, stable CrossOver 26.3 ships 17
GStreamer plugins and `crossover-preview-arm64-20260821` ships 19, and the two
Preview has to itself are `matroska` and `osxaudio`. Both builds decode VP9
identically, through `applemedia` and VideoToolbox; neither ships `libgstvpx` or
`libgstlibav`. So Preview's advantage is a container one rather than a codec
one: only it can open a WebM. DYNASTY WARRIORS ships 355 `.webm` cutscenes and
cannot get as far as a decoder on stable, while Mortal Shell 2 ships the same
codec in `.mp4`, which `isomp4` handles on both, and works on stable.

That lead is in what the build can open, not in what it can decode. It is not a
recommendation about which build to run generally, and two titles here are
confirmed on stable 26.3.

