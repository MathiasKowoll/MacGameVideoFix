# MacGameVideoFix

Makes Windows games show their cutscenes under CrossOver on Apple Silicon.

Six games so far, failing for reasons that have almost nothing in common. They
install the same way: open the app, pick the game from the list, drop its
folder on it, press Apply.

| Game | Symptom | Fix | Backend | DX | CrossOver |
| --- | --- | --- | --- | --- | --- |
| [**Mortal Shell 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Mortal-Shell-2) | Crash on the first cutscene | Runtime patch | D3DMetal | 12 | 26.3 · Preview |
| [**Life is Strange: Reunion**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) | Runs, then freezes after a while | Runtime patch | D3DMetal | 12 | Preview |
| [**Life is Strange: Double Exposure**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure) | Runs, then freezes after a while | Runtime patch | D3DMetal | 12 | Preview |
| [**Beast of Reincarnation**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation) | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | D3DMetal | 12 | 26.3 · Preview |
| [**Persona 5 Strikers**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers) | Video never starts; sound only | Staged VC-1 codec, D3D9 → D3D11 bridge | **DXMT** | 11 | Preview |
| [**DYNASTY WARRIORS: ORIGINS**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins) | Cutscene plays with sound, picture black | Video bridge | D3DMetal | 12 | Preview |

The CrossOver column says which builds a title was measured on, not which
builds it might work on. Three of these do not work on stable 26.3 and one has
never been tried there; [Which CrossOver, per title](#which-crossover-per-title)
says which, and what goes wrong.

Each row links to a page in the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki) with that game's
findings and fix. Why each fault happens and why each fix is shaped the way it
is, is in [How the fixes work](docs/how-it-works.md).

Tested on an M4 Max, macOS 27, against CrossOver 26.3 and Preview build
20260821, with Game Porting Toolkit 4.0b2.

---

## Quick start

Read [Requirements](#requirements) and
[Which CrossOver, per title](#which-crossover-per-title) first. Four of the six
titles have only been measured on CrossOver Preview, two of those crash on
stable 26.3, and one needs a GStreamer package installed before it can work at
all. Each of those looks exactly like the fix not working.

1. Download `MacGameVideoFix.app` from
   [Releases](../../releases), or build it yourself with `app/build-app.sh`.
2. Open the app, **pick your game from the list**, drop its folder on it, and
   press **Apply Fix**.

Picking the game first is what tells the app which folder to ask for, and it
says so on the drop zone. It also checks the game's shipping executable is
really under the folder you dropped, so pointing Double Exposure at Reunion's
folder is caught rather than half-applied.

That check is one behaviour for every named title: the folder has to contain
that title's shipping executable before anything is written, and a folder that
does not is refused. The refusal names the file it could not find rather than
saying the folder is wrong, because the missing name is what says which folder
to pick instead. There is no continue-anyway.

The list carries a seventh entry, **Another Unreal Engine 5 title**, which is
how an untried Unreal game is attempted. It is the one entry with no shipping
executable to check against, so it is taken at its word — and no claim that the
fix works applies to it. Inside the DLL it is also the least conservative case:
an executable the build does not recognise arms all three repairs rather than a
chosen subset, and the DLL's log says so. One of the three is the H.264 half
implicated in the open 26.3 defect below, which is reason enough to try an
untried title on Preview.

**Revert** puts everything back.

### All your games at once

There is a second way in, and it is the shorter one if more than one of these
is installed. **Find my games** or **Choose folder…** points the app at a Steam
library; it then scans, shows what it found and what it would do to each row,
and applies the lot in one pass.

Nothing is read until you press Scan — choosing a folder names it rather than
acts on it — and rows the app cannot act on carry no checkbox and say why
instead. It exists because of the naming trap described below: Steam names
install directories after the project rather than the game, which is the one
thing a person has no reason to know.

That scan stays on the machine it runs on. Nothing it finds belongs in a
published page or in a bug report, and `support-bundle.sh` is built not to
collect it.

### Which folder to pick, by hand

One game at a time, which is what the drop zone is for and the fallback when a
scan does not turn a title up.

For **DYNASTY WARRIORS: ORIGINS**, the folder holding `DWORIGINS.exe` —
usually `steamapps/common/DWORIGINS`.

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

The tell is `Engine/Binaries/ThirdParty/Ogg/Win64`. That is where the proxy
DLL goes, and an Unreal title that ships no `libogg` cannot take this fix at
all — the app says so rather than guessing.

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
  will launch on. Every measurement here was made on macOS 27, so anything
  older is untried rather than known good.
- **CrossOver Preview** for three of the six, and only Preview has been tried
  for a fourth. Two are confirmed on stable 26.3 as well. The table below says
  which is which.
- **For Persona 5 Strikers only:** the official GStreamer runtime package,
  [1.24.14](https://gstreamer.freedesktop.org/data/pkg/osx/1.24.14/)
  (`gstreamer-1.0-1.24.14-universal.pkg`; the development package is not
  needed), installed at `/Library/Frameworks`. It is the only title here
  needing a codec CrossOver does not ship, and nothing is redistributed — the
  decoder is borrowed from an install you already have. Where it comes from and
  why it is staged rather than patched in:
  [How the fixes work](docs/how-it-works.md#the-staged-codecs-and-where-they-come-from).

### Which CrossOver, per title

Measured on this machine against CrossOver 26.3 and Preview build 20260821.
Blank means not measured rather than not working — the claim is only ever what
was tried.

| Title | CrossOver | |
| --- | --- | --- |
| Mortal Shell 2 | 26.3 · Preview | |
| Beast of Reincarnation | 26.3 · Preview | |
| Persona 5 Strikers | Preview | 26.3 expected, not yet measured |
| Life is Strange: Reunion | Preview | crashes on 26.3 — our defect, open |
| Life is Strange: Double Exposure | Preview | crashes on 26.3 — our defect, open |
| DYNASTY WARRIORS: ORIGINS | Preview | 26.3 cannot demux WebM |

Everything runs on that Preview. Two are confirmed on 26.3 as well.

Persona 5 Strikers ought to join them: it stages its own decoder, so what
CrossOver ships stops mattering. That is a prediction from how it was fixed, not
a measurement — it has only been tried on Preview, and this table will say so
until it has been tried on stable.

Three titles do not work on 26.3, for two unrelated reasons.

**DYNASTY WARRIORS is a container problem, not a codec one.** Both builds decode
VP9 the same way; stable 26.3 ships no plugin that can open a WebM, and this
title's 355 cutscenes are `.webm`, so there nothing gets as far as a decoder.
That was read from the two installs' plugin sets — the game has never been
launched on stable, with or without anything added. Anything that can demux
WebM would answer it: [winevideo](https://github.com/Jfishin/winevideo) ships
one today, and a staged `libgstmatroska` would do the same without patching
CrossOver, neither measured with this title. The comparison, and what would
close the gap, is in
[How the fixes work](docs/how-it-works.md#the-container-not-the-codec).

**Both Life is Strange titles crash on 26.3, and that is our defect.** What
runs for them is the node guard and nothing else: the DLL carries three
repairs, and its policy table arms only that one for those two executables, so
the NV12 restore is in the file and inert in the process. What is not inert is
the survey instrumentation — the Media Foundation hooks are installed for every
title, armed or not — and that is the candidate cause until a measurement names
another. The fault is open and being worked on; until it is closed, **use
Preview for those two.** What is known and what is not:
[How the fixes work](docs/how-it-works.md#the-open-defect-on-263).

Persona 5 Strikers is unaffected by either, because it depends on no decoder of
CrossOver's at all.

## What goes in the bottle

Two settings live in the bottle rather than beside the game, and one of them
divides these titles into two groups that cannot share a bottle.

### The graphics backend, and the one conflict

`CX_GRAPHICS_BACKEND` in `cxbottle.conf`:

| Backend | Games |
| --- | --- |
| `d3dmetal` | Mortal Shell 2, both Life is Strange, Beast of Reincarnation, DYNASTY WARRIORS: ORIGINS |
| `dxmt` | **Persona 5 Strikers**, and only it |

This is not a preference. Persona 5 Strikers is a D3D9 title, so Wine's D3D9 is
what serves it, and DXMT implements the sharing Wine does not — `GetSharedHandle`
appears 17 times in DXMT's `d3d11.dll` and not once in Wine's. D3DMetal has
nothing to build on either, measured separately on
[DYNASTY WARRIORS](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins),
where `IDXGIResource::GetSharedHandle` returns `E_NOTIMPL`.

**So Persona 5 Strikers wants a bottle of its own.** Steam libraries are shared
between bottles, so a second bottle sees the same installed games without
re-downloading anything. Switching the backend back and forth in one bottle
works, but means remembering to switch it, and forgetting looks exactly like
the fix having stopped working.

### The codec path

`GST_PLUGIN_PATH`, also in `cxbottle.conf`, points at the staged VC-1 decoder.
**The app writes this for you** when it stages the codec; it is only here so
that the file's contents are not a mystery:

```
"GST_PLUGIN_PATH" = "…/Library/Application Support/MacGameVideoFix/gst-codecs/x86_64/gstreamer-1.0"
```

Today only Persona 5 Strikers needs it, and the other five ignore it entirely,
so leaving it set in a shared bottle costs nothing. If the WebM demuxer that
DYNASTY WARRIORS wants on a stable build is ever staged, it will arrive through
this same mechanism.

### Changing either one

Both are read when the bottle starts, and a live `wineserver` keeps the old
copy — so **close Steam completely**, not just the game. A setting that has not
taken looks identical to a setting that did not work, which is worth knowing
before spending an evening on it.

## Using the scripts directly

The app runs these; each also works on its own. The installers take `--status`
to report and `--restore` to undo. `stage-codecs.sh` is the exception: it only
builds a staging directory of its own and modifies neither CrossOver nor the
game, so there is nothing for it to reverse.

### Unreal titles: the runtime patch

Mortal Shell 2, both Life is Strange titles, Beast of Reincarnation.

```bash
runtime/install-runtime-fix.sh "/path/to/<Game>/Content"            # install
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --status   # report
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --restore  # remove
```

It expects `libogg_64.dll` and `pe.py` beside it. The release ships a prebuilt
DLL; to build your own you need
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases):

```bash
runtime/build-proxy.sh "/path/to/<Game>/Engine/Binaries/ThirdParty/Ogg/Win64/VS2015/libogg_64.dll"
```

### DYNASTY WARRIORS: ORIGINS

```bash
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS"            # install
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --status   # report
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --restore  # remove
```

It expects `libxess.dll` and `pe.py` beside it. To build your own:

```bash
runtime/build-proxy.sh "/path/to/DWORIGINS/libxess.dll" dwo-video-bridge.c
```

### Persona 5 Strikers

Two steps, and the bridge alone will not make the picture appear — without the
codec there is nothing for it to carry.

```bash
runtime/stage-codecs.sh x86_64                                 # stage the VC-1 decoder
runtime/install-p5s-bridge.sh "/path/to/steamapps/common/P5S"  # install the bridge
```

`stage-codecs.sh` takes an architecture and an optional CrossOver engine
version; with no version it stages for every engine installed. `x86_64` is what
a `WineArch=win64` Steam bottle selects. The bridge rides on `amd_ags_x64.dll`,
which the game imports and barely uses under CrossOver.

Remember the backend: this title runs on `dxmt` and nothing else.

### The CrossOver-wide node guard

The freeze both Life is Strange titles hit is in Unreal's D3D12 renderer, not in
either game, so the same guard can be installed once into a CrossOver build
instead of once per game.

```bash
crossover/install-node-guard.sh "/Applications/CrossOver.app"            # install
crossover/install-node-guard.sh "/Applications/CrossOver.app" --status   # report
crossover/install-node-guard.sh "/Applications/CrossOver.app" --restore  # remove
```

It replaces Apple's `dxgi.dll` with a proxy that forwards all seven exports and
corrects one call. It affects every game in every bottle using that CrossOver,
which is the point of it and also the risk, and modifying the bundle invalidates
its code signature as any CrossOver patch does. Point it at a copy if you would
rather not touch the build you rely on. The per-game fix remains the default;
the trade between them is discussed in
[How the fixes work](docs/how-it-works.md#where-the-guard-can-live).

### Undoing an older release's re-encode

Only for a copy an earlier version transcoded. That mode has been removed. Both
of these are no-ops on a game that never had it applied:

```bash
scripts/pak-hide-videos.py ".../Content/Paks/pakchunk0-Windows.pak" --restore
scripts/transcode-movies.sh "/path/to/<Game>/Content" --restore
```

What the pak patch did, and why undoing it is a truncate, is in
[How the fixes work](docs/how-it-works.md#the-re-encode-mode-that-was-removed).

## Troubleshooting

**Steam's "verify integrity of game files" undoes this.** It puts the game's
own carrier DLL back and the proxy is gone. Same after a game patch. Run the fix
again.

**Still crashing in `AllocateBuffer`** — the proxy is not being loaded. Check
what the app reports, and look for `C:\ue5-media-fix.log` in the bottle's
`drive_c`: if it does not exist, the DLL never ran. (Releases before the three
halves were merged wrote `C:\ue5-runtime-fix.log`; the support bundle collects
both names.)

**Still freezing after a while** — check the same log. The node guard writes
one line the first time it refuses a node that does not exist, and that line
appearing is what says the fix took effect. If the log has the Electra lines
but not that one, the game is not making the adapter-node walk and the freeze
is something else; the [wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
covers how to look.

**Persona 5 Strikers installs cleanly and still shows nothing** — check the two
things that live outside the game folder: the bottle's backend has to be `dxmt`,
and `GST_PLUGIN_PATH` has to point at a staged decoder. Both are read when the
bottle starts, so close Steam completely before retrying.

**An Unreal title with no `libogg`** cannot take the runtime patch — that fix
rides in on it. Another carrier may exist for such a title; `libxess.dll` and
`amd_ags_x64.dll` are how the two non-Unreal games here are reached, and
[Diagnosing a new game](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
describes how a carrier is chosen.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop.

## Reporting a problem

If none of that helped, open an issue with the
[bug report form](../../issues/new?template=bug_report.yml). It asks a short
list of questions — all but one of them a menu — and for the output of one
command:

```bash
scripts/support-bundle.sh "/path/to/steamapps/common/<Game>"
```

Run it in Terminal with the game folder you dropped on the app, and paste
everything it prints into the form's last field, inside a code fence. That
output is the report: the install state, the bottle's settings, the runtime log
and which CrossOver builds are installed. Between them they answer most reports
without a second round of questions.

It prints only what it needs to. Your user name is redacted from every path,
bottles are numbered rather than named, the game folder never appears, and
nothing enumerates, lists or counts your installed games. The one exception is
a fenced list at the end mapping numbers to names **for the bottles the report
already refers to** — added only with `--names`, so a conversation can refer to
a bottle at all. Read it before you paste, and delete it if you would rather
not send it.

It also refuses to run on a game folder containing anti-cheat or anti-tamper
files, and collects nothing in that case.

## A game that is not listed

[Diagnosing a new game](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
is the route: what to run, in what order, and what each answer rules out.
[How the fixes work](docs/how-it-works.md#other-games) covers which of these
faults are properties of an engine rather than of a title, and how a carrier DLL
is picked for a game nobody has tried yet.

## Building the app

```bash
app/build-app.sh
```

Needs Xcode's Swift toolchain. Produces `app/MacGameVideoFix.app`, ad-hoc
signed, targeting arm64 macOS 14+.

## Credits

- [CrossOver](https://www.codeweavers.com/crossover) by CodeWeavers, and
  [Wine](https://www.winehq.org/) underneath it.
- **[GStreamer](https://gstreamer.freedesktop.org)**, whose official macOS
  build supplies the VC-1 decoder Persona 5 Strikers needs. It is borrowed from
  an installation you already have, never redistributed here.
- **[winevideo](https://github.com/Jfishin/winevideo) by Jfishin.** None of
  this would exist without it. Its patches are where every one of these faults
  was first identified: that Electra will accept NV12 and nothing else, that
  CrossOver censors that format on macOS, that Electra decides in software by
  asking its own platform handle, that a D3D9 surface has to be bridged rather
  than shared. This project reaches several of the same places from inside the
  game process instead of by patching Wine, which is a different trade-off, not
  a better one — and it is only possible because winevideo had already worked
  out what was wrong. Where the two differ most: winevideo works outside the
  game, so it reaches titles protected against tampering, which nothing here
  can.
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
