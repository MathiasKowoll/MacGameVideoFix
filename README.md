# MacGameVideoFix

Makes Windows games show their cutscenes under CrossOver on Apple Silicon.

Six games so far. They install the same way: open the app, pick the game from
the list, drop its folder on it, press Apply.

| Game | Symptom | CrossOver |
| --- | --- | --- |
| [**Mortal Shell 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Mortal-Shell-2) | Crash on the first cutscene | 26.3 and Preview |
| [**Life is Strange: Reunion**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) | Runs, then freezes after a while | Preview — crashes on 26.3 |
| [**Life is Strange: Double Exposure**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure) | Runs, then freezes after a while | Preview — crashes on 26.3 |
| [**Beast of Reincarnation**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation) | Startup video plays with sound, no picture | 26.3 and Preview |
| [**Persona 5 Strikers**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers) | Video never starts; sound only | Preview — 26.3 expected, not measured |
| [**DYNASTY WARRIORS: ORIGINS**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins) | Cutscene plays with sound, picture black | Preview — never launched on 26.3 |

"Preview" in that column, and everywhere else in this file, means
`crossover-preview-arm64-20260821`. Everything runs on it. Two are confirmed on
stable 26.3 as well. Three qualifications belong with that column, because it
says which builds a title was measured on rather than which it might work on:

- **Both Life is Strange titles crash on 26.3.** That is our defect, and it is
  open and unexplained. Use Preview for those two.
- **Persona 5 Strikers has not been tried on stable.** It stages its own
  decoder, so what CrossOver ships stops mattering, and it is expected to work
  there. That is a prediction from how it was fixed, not a measurement.
- **DYNASTY WARRIORS has never been launched on stable at all.** Stable 26.3
  ships nothing that can open a WebM, which is what its 355 cutscenes are. That
  is read from the two installs' plugin sets, not from a run.

Each row links to a page in the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki) with that game's
findings and fix. Why each fault happens, why each fix is shaped the way it is,
and what was tried and did not work, is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings).

Tested on an M4 Max, macOS 27, against CrossOver 26.3 and
`crossover-preview-arm64-20260821`, with Game Porting Toolkit 4.0b2.

---

## Quick start

Read [Requirements](#requirements) first. Four of the six titles have only been
measured on CrossOver Preview, two of those crash on stable 26.3, and one needs
a GStreamer package installed before it can work at all. Each of those looks
exactly like the fix not working.

1. Download `MacGameVideoFix.app` from [Releases](../../releases), or build it
   yourself with `app/build-app.sh`.
2. Open the app, **pick your game from the list**, drop its folder on it, and
   press **Apply Fix**.

Picking the game first is what tells the app which folder to ask for, and it
says so on the drop zone. It also checks that the game's shipping executable is
really under the folder you dropped, and refuses the folder if it is not, naming
the file it could not find. There is no continue-anyway.

The list carries a seventh entry, **Another Unreal Engine 5 title**, which is
how an untried Unreal game is attempted. It is the one entry with no shipping
executable to check against, so it is taken at its word — and no claim that the
fix works applies to it. An untried title is best tried on Preview.

**Revert** puts everything back.

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

Once the fix is applied, **Apply Fix** is disabled until you revert.

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

Two settings live in the bottle rather than beside the game, and one of them
divides these titles into two groups that cannot share a bottle.

### The graphics backend, and the one conflict

`CX_GRAPHICS_BACKEND` in `cxbottle.conf`:

| Backend | Games |
| --- | --- |
| `d3dmetal` | Mortal Shell 2, both Life is Strange, Beast of Reincarnation, DYNASTY WARRIORS: ORIGINS |
| `dxmt` | **Persona 5 Strikers**, and only it |

This is a requirement rather than a preference: Persona 5 Strikers needs a
shared D3D9 surface handle, and DXMT implements the sharing that neither Wine's
D3D9 nor D3DMetal has. The measurements behind that are on
[the title's page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers).

**So Persona 5 Strikers wants a bottle of its own.** Steam libraries are shared
between bottles, so a second bottle sees the same installed games without
re-downloading anything. Switching the backend back and forth in one bottle
works, but means remembering to switch it, and forgetting looks exactly like the
fix having stopped working.

### The codec path

`GST_PLUGIN_PATH`, also in `cxbottle.conf`, points at the staged VC-1 decoder.
**The app writes this for you**, and where it points is not a free choice: the
staged decoder's support libraries are symlinks into one CrossOver's own bundle,
so a staged directory belongs to that CrossOver and no other. Using it under a
different one gives dyld two GStreamer cores and a crash.

So there is one staged directory per installed CrossOver, named for the version
that engine declares:

```
"GST_PLUGIN_PATH" = "…/MacGameVideoFix/gst-codecs/<CrossOver version>/x86_64/gstreamer-1.0"
```

That version is the same string a bottle records as its own `"Version"`, which is
how the app knows which one a bottle needs — and how it notices when a bottle is
opened with a different CrossOver and needs re-pointing. It offers to repair that
itself; there is nothing to edit by hand.

Only Persona 5 Strikers needs the decoder, so the line is written only into
bottles where that game has run, or one you pick yourself. Earlier versions wrote
it into every bottle on the machine and later ones take it back out again.

### Changing either one

Both are read when the bottle starts, and a live `wineserver` keeps the old copy
— so **close Steam completely**, not just the game. A setting that has not taken
looks identical to a setting that did not work, which is worth knowing before
spending an evening on it.

## Using the scripts directly

The app runs these; each also works on its own. The installers take `--status`
to report and `--restore` to undo. `stage-codecs.sh` is the exception: it only
builds a staging directory of its own and modifies neither CrossOver nor the
game, so there is nothing for it to reverse.

**These paths are in the source repository.** The app download carries the
installers, the prebuilt DLLs and the two undo scripts inside its own bundle,
but not `runtime/build-proxy.sh`, not `crossover/`, and not `app/build-app.sh`.
Clone the repository, or take the source tarball from
[Releases](../../releases), to run the commands below as written.

### Unreal titles: the runtime patch

Mortal Shell 2, both Life is Strange titles, Beast of Reincarnation.

```bash
runtime/install-runtime-fix.sh "/path/to/<Game>/Content"            # install
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --status   # report
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --restore  # remove
```

It expects `libogg_64.dll` and `pe.py` beside it. The release ships that DLL
prebuilt; building your own is under [Building](#building).

### DYNASTY WARRIORS: ORIGINS

```bash
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS"            # install
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --status   # report
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --restore  # remove
```

It expects `libxess.dll` and `pe.py` beside it.

### Persona 5 Strikers

Two steps, and the bridge alone will not make the picture appear — without the
codec there is nothing for it to carry.

```bash
runtime/stage-codecs.sh x86_64                                 # stage the VC-1 decoder
runtime/install-p5s-bridge.sh "/path/to/steamapps/common/P5S"  # install the bridge
```

`stage-codecs.sh` takes an architecture; `x86_64` is what a `WineArch=win64`
Steam bottle selects. Its optional second argument narrows the work to a single
CrossOver install — the script's own usage header says what that argument is in
the copy you have, since it changed between the release and the current source.

Remember the backend: this title runs on `dxmt` and nothing else.

### The CrossOver-wide node guard

The freeze both Life is Strange titles hit is in Unreal's D3D12 renderer rather
than in either game, so the same guard can be installed once into a CrossOver
build instead of once per game.

```bash
crossover/install-node-guard.sh "/Applications/CrossOver.app"            # install
crossover/install-node-guard.sh "/Applications/CrossOver.app" --status   # report
crossover/install-node-guard.sh "/Applications/CrossOver.app" --restore  # remove
```

It replaces Apple's `dxgi.dll` with a proxy that handles all seven exports and
corrects one call. It affects every game in every bottle using that CrossOver,
which is the point of it and also the risk, and modifying the bundle invalidates
its code signature as any CrossOver patch does. Point it at a copy if you would
rather not touch the build you rely on. The per-game fix remains the default;
the trade between them is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-adapter-node-walk-and-where-the-guard-can-live).

### Undoing an older release's re-encode

Only for a copy an earlier version transcoded. That mode has been removed.

```bash
scripts/pak-hide-videos.py ".../Content/Paks/pakchunk0-Windows.pak" --restore
scripts/transcode-movies.sh "/path/to/<Game>/Content" --restore
```

Neither is a no-op on a game that never had the re-encode applied: both refuse
and change nothing, `pak-hide-videos.py` with "no record of a previous patch for
this pak" and `transcode-movies.sh` with an error naming the folder or backup it
could not find. What the pak patch did, and why undoing it is a truncate, is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#what-the-pak-patch-did).

## Troubleshooting

**Steam's "verify integrity of game files" undoes this.** It puts the game's own
carrier DLL back and the proxy is gone. Same after a game patch. Run the fix
again.

**Still crashing in `AllocateBuffer`** — the proxy is not being loaded. Check
what the app reports, and look for `C:\ue5-media-fix.log` in the bottle's
`drive_c`: if it does not exist, the DLL never ran. Releases before the three
halves were merged wrote `C:\ue5-runtime-fix.log`, which is the name to look for
in an old log.

**Still freezing after a while** — check the same log. The node guard writes one
line the first time it refuses a node that does not exist, and that line
appearing is what says the fix took effect. If the log has the Electra lines but
not that one, the game is not making the adapter-node walk and the freeze is
something else; the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
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
runtime/build-proxy.sh "/path/to/<Game>/Engine/Binaries/ThirdParty/Ogg/Win64/libogg_64.dll" ue5-media-fix.c
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
  identified, and the idea of importing codecs from the user's own official
  GStreamer install rather than shipping any is winevideo's too. This project
  reaches several of the same places from inside the game process instead of by
  patching Wine, which is a different trade-off, not a better one — and it is
  only possible because winevideo had already worked out what was wrong. Where
  the two differ most: winevideo works outside the game, so it reaches titles
  protected against tampering, which nothing here can.
- [DXMT](https://github.com/3Shain/dxmt) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), whose
  source made the root cause legible.

## License

[GPL-3.0-or-later](LICENSE). The tooling here exists because Wine, vkd3d and
DXMT are free software that can be read and modified — copyleft keeps any
derivative of this work equally available.

## Why this targets CrossOver Preview

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

Preview's native media support being ahead of the stable line's is why the work
was done there, and the comparison above says exactly where that lead is: in
what the build can open, not in what it can decode. It is not a recommendation
about which build to run generally, and two titles here are confirmed on stable
26.3.

## Disclaimer

Unofficial community tooling, provided as-is. It modifies files in your game
installation; everything is backed up and reversible, but back up anything you
care about first. Not affiliated with or endorsed by CodeWeavers, Apple, or any
of the publishers or developers of the games listed here.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop. Everything
here is for single-player titles whose cutscenes do not play.
