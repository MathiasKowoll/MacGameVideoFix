# Running the scripts directly

The app runs all of these, and the app is the supported way to use this project.
This page is for working from a clone: reproducing a fix by hand, checking what
an installer does before trusting it, or building a proxy for a title that has
none.

Nothing here is needed to use a release. If you downloaded the app, the
[README](https://github.com/MathiasKowoll/MacGameVideoFix#readme) is the whole
story.

The app runs these; each also works on its own. The installers take `--status`
to report and `--restore` to undo. Two of them are not installers and answer
neither. `stage-codecs.sh` builds a staging directory of its own and modifies
neither CrossOver nor the game, so there is nothing for it to reverse.
`make-engine-copy.sh` creates a copy rather than changing what is already there:
`--check` says what it would do and creates nothing, `--force` replaces a copy
already made, and undoing it is deleting the copy.

**These paths are in the source repository.** The app download carries the
installers it offers, `stage-codecs.sh`, `make-engine-copy.sh`,
`install-engine-media.sh` with the engine sets and the codec plugins beside it,
`pe.pl`, the prebuilt DLLs and the two undo scripts inside its own bundle. It
does not carry `runtime/build-proxy.sh`, `runtime/install-ng3-fix.sh`,
`crossover/`, `scripts/build-winegstreamer.sh`,
`scripts/install-winegstreamer.sh`, `source-patches/` or `app/build-app.sh`.
Clone the repository, or take the source tarball from
[Releases](../../releases), to run the commands below as written.

### The patched engine copy

The first step of everything else, and a copy on purpose: the CrossOver a person
paid for stays exactly as CodeWeavers shipped it, and everything this project
changes in an engine is changed in the copy.

```bash
scripts/make-engine-copy.sh                                   # copy /Applications/CrossOver.app
scripts/make-engine-copy.sh --from "/Applications/CrossOver.app" --name Crossover_MGVF.app
scripts/make-engine-copy.sh --from-archive ~/Downloads/crossover.zip
scripts/make-engine-copy.sh --gptk "/path/to/apple_gptk_4"    # replace the toolkit as well
scripts/make-engine-copy.sh --check                           # say what it would do, create nothing
```

The copy lands in `~/Applications` under `--name`, which is `Crossover_MGVF.app`
unless you say otherwise, and `--force` replaces one already there.
`--from-archive` takes a zip with an `.app` at its root. The script needs
`runtime/install-engine-media.sh`: from a clone that is one directory over, and
inside the app both sit flat in Resources — it looks for the installer rather
than assuming a layout.

Six steps, and the order of the last two is the whole trick: copy, write
`mgvf-origin.json`, install the winegstreamer pair, replace the toolkit if
`--gptk` named one, put the three GStreamer plugins in the engine's own
`lib64/gstreamer-1.0`, then `codesign`, and only then `xattr -cr`. Signing before
the last change leaves a seal the next change breaks, and Finder calls that
"damaged" — which is its wording for a signature that does not validate, not a
statement about a date.

`--gptk` names a directory already on the machine; nothing of Apple's is carried
here. The engine's own toolkit is moved aside whole as `apple_gptk_bak` rather
than half at a time, so a revert cannot restore one half and leave the other
running.

The copy carries `mgvf-origin.json` inside its `CrossOver` directory, which is
how the installers here recognise it. A copy can be named anything, so the name
is not the identity.

It ends with three checks and refuses to hand over a copy that fails any of
them: `codesign --verify` passes, ad-hoc; no quarantine attribute is left
anywhere in the bundle; and
`Contents/SharedSupport/CrossOver/bin/wineloader --version` prints a line. The
last is there because a copy being killed on launch prints nothing at all.

### The winegstreamer pair, into an engine

`make-engine-copy.sh` runs this as its third step. It also runs on its own,
against an engine one of the built sets was made for.

**That engine is stable CrossOver 26.3, and only that.** Both sets shipped here
record `26.3.0.39832` in their `engine-built-for*.json`, and the script compares
that against the target's `CFBundleVersion` and refuses anything else. It is not
a preference: the pair was compiled against that engine's wine, and installing
it onto another is not a degraded experience but an engine that may not load
media at all.

```bash
runtime/install-engine-media.sh ~/Applications/Crossover_MGVF.app            # install
runtime/install-engine-media.sh ~/Applications/Crossover_MGVF.app --status   # installed, broken or absent
runtime/install-engine-media.sh ~/Applications/Crossover_MGVF.app --restore  # put the originals back
```

It writes into the engine rather than into a bottle or a game folder — every
bottle and every game on that engine shares what it does — so it refuses an
engine it was not built for rather than trying.

**How it decides which set to install.** A set is three files with a common
suffix: `engine-winegstreamer<S>.dll`, `engine-winegstreamer<S>.so` and
`engine-built-for<S>.json`, and the empty suffix is a set like any other. It
reads every `engine-built-for*.json` beside it and takes the one whose
`engine_app` is the name of the app it was pointed at, provided both halves of
that set are present. Failing that it reads `mgvf-origin.json` inside the target
and tries again under the name recorded there — which is how a copy is served by
the set built for the engine it was copied from, and it says so when it does
that. Nothing else is accepted: an engine with no marker and no matching name is
refused, with the sets present listed.

Then it checks the version too, because a copy and the engine it came from
report the same `CFBundleVersion` and the name is the only thing separating them.
The originals are kept beside the new files as `.mgvf-stock`, and it stops if the
backup it finds is byte for byte the build it is about to install: a backup like
that makes `--restore` reinstall the patch and report success.

### Building the winegstreamer pair

Only needed to rebuild what the two scripts above install. It wants a wine tree
and a GStreamer tree already on the machine, Homebrew's bison ahead of Apple's on
`PATH`, and a build path with no space in it — Wine's build system does not
survive one, and the failure reads as clang being handed half a filename.

```bash
scripts/build-winegstreamer.sh --patches 0002 0003 0006 0008 mgvf-0001
scripts/install-winegstreamer.sh ~/Development/mgvf-winegstreamer-build \
  ~/Applications/Crossover_MGVF.app/Contents/SharedSupport/CrossOver
```

**Each number is resolved in this repository's own `source-patches/` first**, and
only then in the fallback directory `MGVF_PATCHES` names, which is where
winevideo's patches sit when that project is installed. That is new. It used to
be the other way round, so the build did not run at all on a machine without that
project installed. `source-patches/README.md` says whose each patch is and what
it is for.

`MGVF_WINE_SOURCES`, `MGVF_BUILD_OUT` and `MGVF_ENGINE` name the source tree, the
build directory and the engine to link against. The build refuses to hand back a
binary with an `@rpath` dependency it cannot find in that engine, or one that
kept an absolute `/opt/cxoffice` path, and it writes `built-for.json` beside the
pair naming the engine, the wine revision and the patch set.

`install-winegstreamer.sh` compares that wine revision against the engine it is
pointed at and stops if they differ. It keeps the originals as `.stock`, which is
a different backup from the `.mgvf-stock` that `install-engine-media.sh` writes;
the two do not see each other.

### Unreal titles: the runtime patch

Mortal Shell 2, both Life is Strange titles, Beast of Reincarnation.

```bash
runtime/install-runtime-fix.sh "/path/to/<Game>/Content"            # install
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --status   # report
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --restore  # remove
```

It expects `libogg_64.dll` and `pe.pl` beside it — the installers read PE
exports with `/usr/bin/perl`, which every Mac has. The release ships that DLL
prebuilt; building your own is under
[Building](https://github.com/MathiasKowoll/MacGameVideoFix#building).

### DYNASTY WARRIORS: ORIGINS

```bash
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS"            # install
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --status   # report
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --restore  # remove
```

It expects `libxess.dll` and `pe.pl` beside it.

### Persona 5 Strikers

Two steps, and the bridge alone will not make the picture appear — without the
codec there is nothing for it to carry.

```bash
runtime/stage-codecs.sh x86_64 "/Applications/CrossOver.app"    # stage the VC-1 decoder
runtime/install-p5s-bridge.sh "/path/to/steamapps/common/P5S"   # install the bridge
```

`stage-codecs.sh` takes an architecture and then a CrossOver. `x86_64` is what a
`WineArch=win64` Steam bottle selects; `all`, or no argument at all, does both
architectures and skips one the engine has no libraries for. The second argument
is either the bundle path, as above, or an engine's `CFBundleVersion` — both
exact matches, never prefixes, because two bundles can share a prefix and staging
against the wrong engine is a crash rather than a warning. It is not an
application name: passing one matches nothing. Given no second argument it stages
for every CrossOver in `/Applications` and `~/Applications`.

**It is unnecessary on an engine this project copied, and doing it anyway undoes
what the copy is for.** A copy from `make-engine-copy.sh` carries `libgstlibav`,
`libgstmatroska` and `libgstvpx` in its own `lib64/gstreamer-1.0`, beside
CrossOver's own, so a bottle on that engine needs no `GST_PLUGIN_PATH` at all.
Staging a second copy of those onto the search path would put two GStreamer cores
in one process, which is the crash staging exists to avoid. Staging is for a
stock CrossOver, and for a copy made where `runtime/engine-payload/` was not
present — that run says `none in runtime/engine-payload -- skipped` and the copy
has no plugins in it.

Remember the backend: this title runs on `dxmt` and nothing else.

### NINJA GAIDEN 3: Razor's Edge

The one fix in this project with no route through the app, and the only
installer that takes a bottle rather than a game folder. Nothing in the game's
own folder is touched.

```bash
runtime/install-ng3-fix.sh "~/Library/Application Support/CrossOver/Bottles/Steam"            # install
runtime/install-ng3-fix.sh "~/Library/Application Support/CrossOver/Bottles/Steam" --status   # report
runtime/install-ng3-fix.sh "~/Library/Application Support/CrossOver/Bottles/Steam" --restore  # remove
```

Four DLLs go into the bottle's `system32`, activated for that one executable
through `AppDefaults`, so no other title in the same bottle changes behaviour.

### The CrossOver-wide node guard

The freeze both Life is Strange titles hit is in Unreal's D3D12 renderer rather
than in either game, so the same guard can be installed once into a CrossOver
build instead of once per game.

```bash
crossover/install-node-guard.sh ~/Applications/Crossover_MGVF.app            # install
crossover/install-node-guard.sh ~/Applications/Crossover_MGVF.app --status   # report
crossover/install-node-guard.sh ~/Applications/Crossover_MGVF.app --restore  # remove
```

It replaces Apple's `dxgi.dll` with a proxy that handles all seven exports and
corrects one call. It affects every game in every bottle using that CrossOver,
which is the point of it and also the risk, and modifying the bundle invalidates
its code signature as any CrossOver patch does. Point it at a copy, which is what
`make-engine-copy.sh` above is for, rather than at the CrossOver you rely on. It
does not re-sign afterwards, so a copy that was already signed needs
`codesign --force --deep --sign -` and then `xattr -cr`, in that order, before
it is opened again. It reads exports with `runtime/pe.py`, so it is the one script here that
wants a working `python3`. The per-game fix remains the default; the trade
between them is in
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
