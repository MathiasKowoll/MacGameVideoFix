# MacGameVideoFix

Makes Windows games show their cutscenes under CrossOver on Apple Silicon.

> ### New in 5: METAL GEAR SOLID: Peace Walker
>
> It used to die the moment a pre-rendered cutscene started. The cure is this
> project's own engine patch `mgvf-0001` — 2D-capable buffers from
> winegstreamer's media source — and **nothing at all is installed into the
> game's folder**. Measured over about eleven minutes of play: two Media
> Foundation sessions, two `.xmx` files played, no exceptions.
>
> A green band along the bottom of the frame remains in *some* cutscenes, and it
> is **unexplained**. The buffer's current length, its maximum length and its
> contiguous length all agree, so the band is already in the frame when it
> arrives — that is as far as the measurement goes, and it is not a claim that
> every cause has been ruled out.
>
> **By default the app no longer modifies your CrossOver.** It makes a copy, at
> your direction, and patches the copy, so that nobody takes a support complaint
> to CodeWeavers about a CrossOver we modified. Turning the copy off in **Set
> up** is the other route, and on that one a toolkit you ask for does go into
> the CrossOver you installed. [How it runs](#how-it-runs) is the whole flow.

More games than rows: the two KINGDOM HEARTS packages hold seven playable titles between them. <!-- count-ok: titles inside those two packages, not entries in the table -->

Most of these are video faults, which is what the name says. Several are not —
the rows whose Symptom column says the game does not start — and what stops each
one is answered the same way everything else here is fixed: by giving a better
answer to a call the game already makes. RESONANCE needs one thing besides: a
16:9 display, which no fix can supply. Tormented Souls 2 is the entry whose
fault is the game's own rather than the translation layer's: it keeps 16:9
resolutions and nothing else, and a laptop display has none.

<!-- readme-games:begin -->

| Game | Symptom | CrossOver | Motor | Status |
| --- | --- | --- | --- | --- |
| [**METAL GEAR SOLID: Peace Walker**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Metal-Gear-Solid-Peace-Walker) | Dies the moment a pre-rendered cutscene starts | 26.3, our winegstreamer | **Ours** | Fixed -- crash cured; a green band in some cutscenes is unexplained |
| [**Mortal Shell 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Mortal-Shell-2) | Crash on the first cutscene | 26.3, our winegstreamer | Stock | Fixed |
| [**Life is Strange: Reunion**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) | Freezes after a while, anywhere | 26.3, our winegstreamer | Stock | Runaway node walk stopped; no freeze-free session recorded |
| [**Life is Strange: Double Exposure**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure) | Freezes after a while, anywhere | 26.3, our winegstreamer | Stock | Guard installs; the freeze was never reproduced or cured |
| [**DYNASTY WARRIORS: ORIGINS**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins) | Cutscene runs with sound, picture black | 26.3, our winegstreamer | Stock&dagger; | Fixed |
| [**Beast of Reincarnation**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation) | Startup video plays with sound, no picture | 26.3, our winegstreamer | winevideo | Fixed |
| [**Persona 5 Strikers**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers) | Video never starts; sound only | 26.3, our winegstreamer | Stock&dagger; | Fixed |
| [**NINJA GAIDEN 3: Razor's Edge**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Ninja-Gaiden-3-Razors-Edge) | Will not start: "Insufficient VRAM" | 26.3, our winegstreamer | Stock | Starts, 60 fps, in-game cutscenes; the boot movie freezes, one click skips it |
| [**Nioh**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh) | Cutscene refuses to play, then crashes | 26.3, our winegstreamer | Stock&dagger; | Fixed |
| [**Nioh 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh-2) | Cutscene refuses to play, then crashes | 26.3, our winegstreamer | Stock&dagger; | Fixed |
| [**Nioh 3**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Nioh-3) | Failed to play movie | 26.3, our winegstreamer | Stock&dagger; | Fixed |
| [**Wo Long: Fallen Dynasty**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Wo-Long-Fallen-Dynasty) | Cutscene runs with sound, picture black | 26.3, our winegstreamer | Stock&dagger; | Bridge installs; picture measured only on a patched engine |
| [**NieR Replicant ver.1.22474487139**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/NieR-Replicant) | Crashes when the first video starts | 26.3, our winegstreamer | Stock | Fixed |
| [**KINGDOM HEARTS Dream Drop Distance**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Kingdom-Hearts) | Cutscene runs with sound, picture solid green; a crash dialog on leaving | 26.3, our winegstreamer | Stock&dagger; | Picture restored; the exit dialog was measured on 1.5+2.5 |
| [**KINGDOM HEARTS 0.2 Birth by Sleep**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Kingdom-Hearts) | Would not start from a launcher; ran fine launched by hand | 26.3, our winegstreamer | None | Works with nothing of ours -- fixed in the launcher |
| [**KINGDOM HEARTS HD 1.5+2.5 ReMIX**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Kingdom-Hearts) | Cutscene runs with sound, picture solid green; a crash dialog on leaving | 26.3, our winegstreamer | Stock | Fixed |
| [**TMNT: Splintered Fate**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/TMNT-Splintered-Fate) | Opens a window, then closes silently | 26.3, our winegstreamer | Stock | Fixed |
| [**Tormented Souls 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Tormented-Souls-2) | Fatal error before the first frame | 26.3, our winegstreamer | Stock | Fixed |
| [**Devil May Cry 5**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/RE-Engine-VC1) | Crashes when a skill preview video plays | 26.3, our winegstreamer | Stock | Fixed |
| [**RESIDENT EVIL 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/RE-Engine-VC1) | Crashes when a video plays | 26.3, our winegstreamer | Stock | Fixed |
| [**RESIDENT EVIL 3**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/RE-Engine-VC1) | Crashes when a video plays | 26.3, our winegstreamer | Stock | Fixed |
| [**NINJA GAIDEN 4**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Ninja-Gaiden-4) | Says the VP9 codec is missing, then exits | 26.3, our winegstreamer | Stock | Fixed |
| [**RESONANCE: A PLAGUE TALE LEGACY**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Resonance-A-Plague-Tale-Legacy) | Fatal error: Shader Model 6.7 is not supported | 26.3, our winegstreamer | Stock | Starts on a 16:9 display -- its cutscenes have never been visible |

<!-- readme-games:end -->

**The CrossOver column says what each row was measured on, and it is the same
answer on every row.** The version is stable CrossOver 26.3, and the engine
those runs were made on carries this project's `winegstreamer` rather than the
one CodeWeavers ships — which is the engine the copy route in
[How it runs](#how-it-runs) produces. **26.3.0.39832 is the supported engine and
the only one**: the `winegstreamer` pair inside the app was built against that
build, and `runtime/install-engine-media.sh` refuses any other.

**CrossOver Preview is not supported.** It is not an engine to run these games
on, the table above records no run on it, and the copy route refuses it along
with every build that is not 26.3.0.39832. It was measured against here for a
long time, and the notes further down that say what it did are kept as history
rather than as guidance — see
[Which CrossOver, and why we patch a copy](#which-crossover-and-why-we-patch-a-copy).

**Motor** says what the engine itself has to carry. **Stock** is a CrossOver as
CodeWeavers shipped it, which is most of the table; a dagger on it means no run
on that row isolated the engine's own `winegstreamer` as the thing that carried
the video, so Stock there is read off the mechanism rather than established. It
is not a statement about the CrossOver cell beside it: a dagger neither adds a
qualifier to that cell nor takes one away. That cell records what the runs were
made on, which is the same on every row; Motor records what a fix needs an
engine to carry, so **Stock** beside it says the fix does not depend on ours.
The rows naming something else are the ones where the engine is part of the
fix — Beast of Reincarnation wants a `winegstreamer` carrying winevideo's
patches, and METAL GEAR SOLID: Peace Walker wants ours.

Each row links to a page in the
[wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki) with that game's
findings and fix. Why each fault happens, why each fix is shaped the way it is,
and what was tried and did not work, is in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings).

Tested on an M4 Max, macOS 27, against stable CrossOver 26.3 — copies of it
patched by this project, carrying our `winegstreamer`, which is what **Set up**
makes.

Apple's Game Porting Toolkit is the other half of that answer, and the two
generations are not interchangeable. **4.0b2 is what these titles need as a
general rule, and NINJA GAIDEN 4 is the exception at 3.0.** A rule is not a run,
though: the rows marked **3.0 only** or **4.0b2 only** in the GPTK column of the
wiki's table are the ones measured refusing the other generation, and a row with
no measurement of its own says so rather than inheriting the rule as a result.

---

## How it runs

Download `MacGameVideoFix.app` from [Releases](../../releases), or build it
yourself with `app/build-app.sh`, and open it. **Set up** asks four questions in
order, and the order is the point: a bottle runs under one CrossOver, so that
answer decides every answer after it.

1. **Choose a CrossOver.** On the copy route — the default — it is copied
   rather than modified, and the copy has to be made from **CrossOver
   26.3.0.39832**: the `winegstreamer` pair inside the app is built against that
   one engine, and `runtime/install-engine-media.sh` refuses any other and
   exits. **Read your own build off this picker before you go any further**:
   every CrossOver it offers is listed with its version beside its name, and the
   only entry without one is a copy this app already made, which is labelled
   *patched by this app* instead. The refusal comes after the copy has been
   made, so pointing it at another build leaves a half-finished bundle in
   `~/Applications` — copied, with none of the media libraries in it and never
   re-signed. The app reports the refusal, naming the build the libraries were
   built for and the one it found; delete that bundle rather than opening it.

   **If that other build is the only CrossOver you have, take the other route
   rather than stopping here.** Turn **Work on a copy of CrossOver** off in step
   3 and the codecs are staged beside your bottle instead of going inside an
   engine. That route is not tied to one build: the staging is made from your
   own GStreamer install, for whichever CrossOver the bottle records. It needs
   GStreamer installed. Being able to run there is not the same as being
   measured there — every row in the table at the top of this file was measured
   on stable 26.3, so another build is untried rather than known to work, and
   CrossOver Preview is not supported at all. The route is described under
   [CrossOver and bottles, without a copy](#crossover-and-bottles-without-a-copy).
2. **Choose a bottle** — the one the games run in.
3. **Say what goes into it.** By default the app copies the CrossOver you chose
   to `Crossover_MGVF.app` in your own `~/Applications`, and patches the copy.
   In go the `winegstreamer` pair this project builds, and the GStreamer plugins
   CrossOver does not ship — those into the engine's own `lib64/gstreamer-1.0`,
   beside CrossOver's own. No bottle needs `GST_PLUGIN_PATH` and nothing is
   staged. Apple's Game Porting Toolkit goes in here too if you ask for it, with
   both halves of the toolkit it replaces kept inside the copy. **Nothing of
   Apple's is carried by this project**: the toolkit is taken from another
   CrossOver already installed on this Mac that ships a newer one than the
   CrossOver you chose, and the app offers the option only when it finds such an
   engine. Without one the toggle does not appear and the copy keeps the toolkit
   CrossOver shipped.
4. **Point it at your games** and apply.

**Play from the copy, every session.** `Crossover_MGVF.app` in your own
`~/Applications` is the CrossOver you start games with from now on: open it from
Finder, pick the bottle you chose in step 2, and play there rather than in the
CrossOver you installed. This is not a first-launch formality that can be done
once and forgotten. The media libraries and the plugins went into that copy and
into nothing else, so a game started from the CrossOver you installed runs
without them — this time and every later time. Nothing was written into the
bottle either: on this route the plugins are inside the engine, so no bottle is
told anything. The copy carries an `mgvf-origin.json`, which is how the
installers recognise it later — a copy can be named anything, so the name is not
the identity.

**On a second run, choose your original CrossOver again, not the copy.** The
copy appears in the same picker, labelled *patched by this app*, and **Work on a
copy** stays on — so picking it would ask the app to copy a bundle over itself.
It refuses rather than doing that, and says to point it at the CrossOver you
installed.

**Applying again rebuilds the copy from scratch rather than adding to it.** The
app always passes `--force`, and the script removes `Crossover_MGVF.app` before
it copies, so what you end up with is a fresh copy of your original CrossOver
carrying this run's answers. Nothing from the run before survives it — including
a toolkit you asked for last time, which has to be asked for again if you still
want it.

The order the copy is made in is `copy → every change → codesign → xattr -cr`.
Signing before the last change is what makes Finder call a bundle *"damaged"*,
which is a signature message rather than a date one, whatever the wording about
an unknown download date suggests. What to do when a copy comes out that way is
in [Troubleshooting](#troubleshooting).

**One game at a time** is still there and unchanged: **pick your game from the
list**, drop its folder on it, and press **Apply Fix**.

Picking the game first is what tells the app which folder to ask for, and it
says so on the drop zone. It also checks that the game's shipping executable is
really under the folder you dropped, and refuses the folder if it is not, naming
the file it could not find. There is no continue-anyway.

The list carries one more entry, **Another Unreal Engine 5 title**, which is
how an untried Unreal game is attempted. It is the one entry with no shipping
executable to check against, so it is taken at its word — and no claim that the
fix works applies to it.

The table carries rows that list does not, and for opposite reasons. **METAL
GEAR SOLID: Peace Walker** needs nothing picked — the patched engine is its
whole fix, and there is nothing to put beside the game. **NINJA GAIDEN 3:
Razor's Edge** is applied from a clone instead, with `runtime/install-ng3-fix.sh`,
which takes a bottle rather than a game folder; see
[Running the scripts directly](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Running-the-scripts).

**Revert** puts everything back.

### CrossOver and bottles, without a copy

Turning the copy off in **Set up** is the other route, and it is the one the
rest of this section describes. Then the codecs are staged beside a bottle
instead, and a toolkit you ask for goes into the CrossOver you installed rather
than into a copy of it.

The **CrossOver and bottles…** button opens the two things that then live
outside the game folder. **Stage codec** borrows the decoders and the demuxer
from your own GStreamer install, once per installed CrossOver. The list under it
says which CrossOver each bottle runs under and which staging it points at,
repairs a bottle whose CrossOver has changed since it was last configured, and
lets you name the CrossOver a bottle uses when the automatic answer is wrong.

Both the button and the codec banner are hidden while you are working on a copy,
because a copy carries the plugins inside the engine and staging a second set in
front of it would put two GStreamer cores in one process.

The titles with a dash in the wiki's Codec column need nothing from either
route.

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

One game at a time, which is what the drop zone is for and the fallback when a  <!-- count-ok: how the drop zone works, not a count of anything -->
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
- **Stable CrossOver 26.3.0.39832, and nothing else.** That is the supported
  engine: the `winegstreamer` pair in the app is built against that build, and
  `runtime/install-engine-media.sh` refuses any other and exits. **CrossOver
  Preview is not supported**, and neither is any other version. Every row in the
  table at the top of this file was measured on stable 26.3, and why the app
  patches a copy of it rather than the original is in the
  [disclaimer](#which-crossover-and-why-we-patch-a-copy). That installer checks
  the bundle's name as well as its version — a stock `CrossOver.app` and a copy
  this project has patched report the same version, so the name is the only
  thing that tells them apart. `scripts/make-engine-copy.sh` invokes that
  installer partway through, so a refusal leaves the copied bundle in
  `~/Applications` with none of the media libraries in it and the signing step
  never run.
- **A second CrossOver, if you want the newer toolkit.** Apple's Game Porting
  Toolkit is not carried by this project. The app takes it from another
  CrossOver already installed on this Mac, and offers the option only when it
  finds one running a newer toolkit than the CrossOver you chose. 4.0b2 is what
  these titles need as a general rule — NINJA GAIDEN 4 is the exception and
  wants 3.0, which is what 26.3 already ships. Without such a second engine
  there is nothing to take from: a 26.3 keeps D3DMetal 3.0, and the rows the
  wiki's GPTK column marks **4.0b2 only** have no way to run.
- **GStreamer, on one route only.** The titles the wiki's Codec column marks
  need a decoder or a demuxer no CrossOver ships, and where that comes from
  depends on the answer you gave in **Set up**:
  - *Working on a copy* — nothing to install. The plugins travel with the app
    and go inside the engine copy, with their licences and their sha256 sums
    recorded in `runtime/engine-payload/CODEC-LICENCES.md`.
  - *Not working on a copy* — the official GStreamer runtime package,
    [1.24.13](https://gstreamer.freedesktop.org/data/pkg/osx/1.24.13/)
    (`gstreamer-1.0-1.24.13-universal.pkg`; the development package is not
    needed), installed at `/Library/Frameworks`. Then nothing is redistributed:
    the decoder is borrowed from that install and staged in front of your
    CrossOver. Prefer that exact release over a later 1.24: it is what winevideo
    names, what is installed and working here, and what a launcher that owns its
    engine may require exactly. Earlier notes in this repository name 1.24.14
    because that is what was installed when they were written.

  Where the decoders come from, and why they are staged rather than patched into
  a CrossOver you installed, is in
  [Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-staged-codecs-and-where-they-come-from).

## What goes in the bottle

Two settings live in `cxbottle.conf` rather than beside the game.

`CX_GRAPHICS_BACKEND` is a requirement rather than a preference, and it divides
these titles into groups that cannot share a bottle:

| Backend | Games |
| --- | --- |
| `d3dmetal` | the titles marked **D3DMetal** in the wiki's stack table |
| `dxmt` | the titles marked **DXMT** in the wiki's stack table |
| `dxvk` | the titles marked **DXVK** in the wiki's stack table |

So the DXMT group wants a bottle of its own. Steam libraries are shared between
bottles, so a second bottle sees the same installed games without re-downloading
anything. Why nothing but DXMT will do is on
[Persona 5 Strikers' page](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers).

`GST_PLUGIN_PATH` is the staging route only. **On a copy no bottle is told
anything**: the plugins are inside the engine, and pointing a bottle at a second
set would put two GStreamer cores in one process. Without a copy the setting
points at the staged decoder, and **the app writes it for you**:

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

**The copy was refused, and there is half a bundle in `~/Applications`.** The
copy route serves CrossOver 26.3.0.39832 and refuses any other build, and that
check runs after the copy has been made. Delete the half-made
`Crossover_MGVF.app`: it has none of the media libraries in it and was never
re-signed, so it is not a CrossOver to open. Then either point step 1 at
26.3.0.39832 if you have it, or turn **Work on a copy of CrossOver** off and
take the staging route, which is not tied to that build.

**macOS says `Crossover_MGVF.app` is damaged.** That is Gatekeeper's wording for
a signature that does not validate, whatever it says about an unknown download
date, and what breaks the seal is a change made to the bundle after it was
signed. A copy in that state is not repaired in place: make it again from **Set
up**, which rebuilds it from your original CrossOver, and then leave the result
alone — changing anything inside a signed copy by hand is what puts it here. The
run ends by verifying its own signature and refuses to hand over a copy that
fails.

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

**Persona 5 Strikers installs cleanly and still shows nothing** — check what
lives outside the game folder: the bottle's backend has to be `dxmt`, and the
decoder has to be reachable — inside the engine if you are on a copy, and
pointed at by `GST_PLUGIN_PATH` if you are not. The bottle reads both when it
starts, so close Steam completely before retrying.

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

### The engine

```bash
scripts/build-winegstreamer.sh --patches 0002 0003 0006 0008 mgvf-0001
scripts/make-engine-copy.sh --from /Applications/CrossOver.app
```

The first builds the `winegstreamer` pair from a CrossOver source tree with that
set of patches applied; the second makes the copy, puts the pair and the plugins
into it, signs it and clears its quarantine, in that order.

The patches are in `source-patches/`, which is new in 5 — the build used to
point at another project's patch directory and could only run on a machine that
had that project installed. `mgvf-0001` is ours and lives here in full. **0002,
0003, 0006 and 0008 are winevideo's**, carried here unchanged with their
provenance and their licence, and each file opens with a header saying so; take
them from [winevideo](https://github.com/Jfishin/winevideo) rather than from us
if you want them, because theirs is where they are maintained.
`source-patches/README.md` says what each one does and which needs which.

## Credits

- [CrossOver](https://www.codeweavers.com/crossover) by CodeWeavers, and
  [Wine](https://www.winehq.org/) underneath it.
- **[GStreamer](https://gstreamer.freedesktop.org)**, whose official macOS build
  supplies the decoders and the demuxer CrossOver does not ship. On the staging
  route they are borrowed from an installation you already have and nothing is
  redistributed; on the copy route they travel with the app, and
  `runtime/engine-payload/CODEC-LICENCES.md` records what each file is, its
  licence and its sha256.
- **[winevideo](https://github.com/Jfishin/winevideo) by Jfishin.** None of this
  would exist without it. Its patches are where these faults were first
  identified: that Electra will accept NV12 and nothing else, that CrossOver
  censors that format on macOS, that Electra decides in software by asking its
  own platform handle, and that a D3D9 surface has to be bridged rather than
  shared. The idea of importing codecs from the user's own official GStreamer
  install rather than shipping any is winevideo's too, and where this project
  does carry them — inside an engine copy — they are byte-identical copies of
  that project's own build, listed by hash and licence in
  `runtime/engine-payload/CODEC-LICENCES.md`. Some of the engine patches are
  theirs as well; `source-patches/README.md` says which. How the two projects
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
installation, and by default it makes and patches a copy of CrossOver;
everything is backed up and reversible, and on that route the CrossOver you
installed is not touched, but back up anything you care about first. With the
copy turned off in **Set up**, a toolkit you ask for goes into the CrossOver you
installed instead. Not affiliated with or endorsed by CodeWeavers, Apple, or any
of the publishers or developers of the games listed here.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop. Everything
here is for single-player titles whose cutscenes do not play.

### Which CrossOver, and why we patch a copy

**The copy is the answer to a support question, not a technical one.** A
CrossOver we have changed is not the CrossOver CodeWeavers shipped, and a
complaint about the first should never arrive at their door as a complaint about
the second. So by default the app leaves your install byte-identical and works
on a copy you asked it to make. It also means the original is always there to
reproduce against, which is how "the game update moved every address" was told
apart from "our engine broke it".

**History: why the project once named a single Preview build.** CrossOver
Preview stopped being a supported engine on 2026-08-31. It was dropped rather
than half-supported: only titles patched without `winegstreamer` could have
worked on it, and the rest were out of the equation. What follows is what was
measured there while it was still the engine everything ran on, and it is kept
because a deleted measurement is not a correction. It is a record, not guidance
about where to run a game.

For a long time `crossover-preview-arm64-20260821` was where everything was
measured, and the reason was narrower than it looked. Comparing the two installs
plugin by plugin on this machine, stable CrossOver 26.3 ships 17 GStreamer
plugins and that Preview shipped 19, and the two Preview had to itself were
`matroska` and `osxaudio`. Both builds decoded VP9 identically, through
`applemedia` and VideoToolbox; neither shipped `libgstvpx` or `libgstlibav`. So
Preview's advantage was a container one rather than a codec one: only it could
open a WebM. DYNASTY WARRIORS ships 355 `.webm` cutscenes and could not get as
far as a decoder on a stable CrossOver as CodeWeavers ships it, while Mortal
Shell 2 ships the same codec in `.mp4`, which `isomp4` handles on both.

**That gap is closed, and it was a plugin rather than an engine.** Staging
`libgstmatroska` gives stable a demuxer too, and NINJA GAIDEN 4 plays on stock
26.3 with it. What each row still needs is per row, and the CrossOver and Motor
columns say it per row — and there is no single Game Porting Toolkit generation
that serves the whole table.

