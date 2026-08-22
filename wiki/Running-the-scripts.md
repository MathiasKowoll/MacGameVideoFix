# Running the scripts directly

The app runs all of these, and the app is the supported way to use this project.
This page is for working from a clone: reproducing a fix by hand, checking what
an installer does before trusting it, or building a proxy for a title that has
none.

Nothing here is needed to use a release. If you downloaded the app, the
[README](https://github.com/MathiasKowoll/MacGameVideoFix#readme) is the whole
story.

The app runs these; each also works on its own. The installers take `--status`
to report and `--restore` to undo. `stage-codecs.sh` is the exception: it only
builds a staging directory of its own and modifies neither CrossOver nor the
game, so there is nothing for it to reverse.

**These paths are in the source repository.** The app download carries the
installers, `stage-codecs.sh`, `pe.py`, the prebuilt DLLs and the two undo
scripts inside its own bundle, but not `runtime/build-proxy.sh`, not
`crossover/`, and not `app/build-app.sh`. Clone the repository, or take the
source tarball from [Releases](../../releases), to run the commands below as
written.

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
runtime/stage-codecs.sh x86_64 "CrossOver Preview"              # stage the VC-1 decoder
runtime/install-p5s-bridge.sh "/path/to/steamapps/common/P5S"   # install the bridge
```

`stage-codecs.sh` takes an architecture; `x86_64` is what a `WineArch=win64`
Steam bottle selects. The second argument is the CrossOver to stage for, and it
changed after v4.2.1: the released copy takes an application name and defaults
to `CrossOver Preview`, staging for that one install, which is the form written
above. The current source takes an engine `CFBundleVersion` instead and, given
no second argument, stages for every CrossOver installed.

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
