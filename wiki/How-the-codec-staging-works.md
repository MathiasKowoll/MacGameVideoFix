# How the codec staging works

Some of the games here need something CrossOver does not ship: most need a
decoder, and two need a demuxer. Which is which, per title, is the Codec
column of [what each title actually loads](Games.md#what-each-title-actually-loads). This is what this project does about
it — two arrangements, not one — why they are built that way, and why the
obvious approach crashes.

For a CrossOver the user owns, nothing is redistributed and no CrossOver file is
touched: the whole repair is a directory of libraries and one line in a bottle's
configuration. For an engine this project copies and patches itself, the same
plugins are carried inside the copy instead. Both are described below, and they
must never be applied to the same engine.

Throughout, the CrossOver in question is stable 26.3 — the one supported engine
here — whether it is the one the user installed or a copy of it this project
patched.

## What is missing

Two different things, and telling them apart is most of the work.

**A decoder.** CrossOver decodes VP9, H.264 and AAC on its own. It has no decoder
for **VC-1**, **WMV3** or **WMA** — and Persona 5 Strikers, Nioh, Nioh 2, Devil
May Cry 5, RESIDENT EVIL 2 and RESIDENT EVIL 3 all play video in one of those.

**A demuxer.** CrossOver ships demuxers for ASF, AVI, ISO-MP4 and WAV, and none
for **Matroska** — so a WebM is recognised by typefind and then has nothing to
hand off to. Media Foundation reports that as
`MF_E_UNSUPPORTED_BYTESTREAM_TYPE`, which reads like a missing codec and is not
one. NINJA GAIDEN 4 is where this was measured: its decoder was present the whole
time. `gst-libav` does not cover it, because it deliberately leaves Matroska to
gst-plugins-good.

The official [GStreamer.framework](https://gstreamer.freedesktop.org/download/)
for macOS has both, in `libgstlibav` — GStreamer's binding to FFmpeg — and in
`libgstmatroska`. They exist on the machine already, which is where the staging
route takes them from; the engine-copy route carries its own. Either way the
problem is the same: getting them in front of CrossOver.

## Why loading it in place crashes

Point `GST_PLUGIN_PATH` at the framework's own plugin directory and the process
dies. macOS says why:

    objc: Class GstCocoaApplicationDelegate is implemented in both
      /Library/Frameworks/GStreamer.framework/.../libgstreamer-1.0.0.dylib
      /Applications/CrossOver.app/.../libgstreamer-1.0.0.dylib
    This may cause spurious casting failures and mysterious crashes.

The plugin drags in the framework's own GStreamer core, and the process now has
two: two copies of the library, two GObject type registries, and objects from
one being handed to the other. On a normal desktop this is absorbed by
`gst-plugin-scanner`, a separate process that inspects plugins and reports back.
CrossOver ships no scanner, so there is nothing to absorb it.

## Two routes, and why they must not overlap

Which arrangement applies depends on whose engine it is.

**Staged beside a bottle**, for a CrossOver the user owns. Two plugins —
`libgstlibav` and `libgstmatroska` — in a directory of their own, taken from the
user's own GStreamer framework, reached by one `GST_PLUGIN_PATH` line per
bottle. Nothing is written into CrossOver. That is the arrangement the rest of
this page describes.

**Carried inside the engine**, for a copy this project made. Three plugins —
`libgstvpx` as well — copied into the copy's own `lib64/gstreamer-1.0` beside
CrossOver's own, from `runtime/engine-payload/lib64` rather than from the user's
framework. No bottle on that engine needs `GST_PLUGIN_PATH` at all, and the
GStreamer framework is not a prerequisite for that engine.
`scripts/make-engine-copy.sh` puts them there, and
[what ships and where it goes](What-ships-and-where-it-goes.md) covers the copy
itself.

**Doing both to one engine puts a second copy of each plugin on the search
path**, which is the crash above arrived at from the other side: two GStreamer
cores in one process. So `stage-codecs.sh` is written to look first — if the
engine's own `lib64/gstreamer-1.0` already holds `libgstlibav`,
`libgstmatroska` and `libgstvpx`, it says so, stages nothing, and reports that
no bottle on that engine needs `GST_PLUGIN_PATH`.

**As the script stands today that check does not run.** It reads `$ENGINE`
before the variable is assigned, and the script runs under `set -u`, so the
first engine it reaches ends the run:

    runtime/stage-codecs.sh: line 193: ENGINE: unbound variable

Measured against the real script with an isolated `HOME`, so nothing on this
machine changed: nothing was staged, for any engine. The check wants the bundle
path it was passed; `ENGINE` is the display name, and it is not given a value
until further down the same function. Recorded rather than described as working,
because a stand-down that ends the run is not a stand-down.

## The shape that works

Give the plugin a directory of its own, and be deliberate about which
dependencies come from where.

    gst-codecs/<CrossOver>/<arch>/
      gstreamer-1.0/libgstlibav.dylib     the decoders
      gstreamer-1.0/libgstmatroska.dylib  the demuxer
      lib/                                 what they need
      .built-against                       the engine version it was built for
      .complete                            written last, when it really is

The plugin resolves its dependencies through `@rpath`, and its rpath list
includes `@loader_path/../lib` — so `lib/` beside it is where everything is
found. That directory is filled two different ways, and the difference is the
whole design:

- **FFmpeg is copied**, and so are the few libraries Matroska needs. Seven real
  files — `libavcodec`, `libavformat`, `libavfilter`, `libavutil` and
  `libswresample`, with `libz` and `libbz2` behind them. CrossOver does not have
  these at all, which is the entire reason for doing any of this. The `copied`
  count the script prints at the end is 9 rather than 7, because it counts the
  two plugins as well.
- **GStreamer's own core is symlinked into CrossOver.** Thirteen links, pointing
  at that CrossOver's `libgstreamer`, `libglib`, `libgobject`, `libgstvideo` and
  the rest.

That second half is what avoids the crash. The plugin loads **CrossOver's**
GStreamer, not the framework's, so there is one core, one type registry, and the
decoders register into the same place everything else lives.

## Two architectures, because a bottle picks one

CrossOver 27 runs ARM Windows binaries as well as x86_64 ones, and a bottle says
which it is: `"WineArch" = "arm64"` against `"win64"`. The two do not share a
plugin directory, and pointing an ARM bottle at the x86_64 staging points it at
libraries it cannot load.

So each engine is staged for **every architecture it actually ships**, in one
pass, and the app reads the bottle's own `WineArch` to decide which directory
that bottle's `GST_PLUGIN_PATH` gets. The GStreamer framework is universal, so
the material was always there; staging only x86_64 was a gap in the script, and
it left an ARM bottle with no VC-1, WMV or WMA decoder at all.

Two things follow from it being per-engine rather than per-machine:

- CrossOver 26.3 ships no ARM GStreamer, so an ARM bottle recorded against it
  has no staging to point at and cannot have one. The app says that instead of
  writing a path that is not there — a key that is set, on a bottle that reads
  as configured, with nothing behind it.
- The old single-architecture layout, `lib64`, holds x86_64. The script falls
  back to it only when staging x86_64; doing it for `aarch64` would fill an ARM
  directory with x86_64 libraries that resolve while staging and fail on load.

## One staging per engine, and why the name is the key

Every installed CrossOver gets its own directory, named after the application
rather than its version:

    gst-codecs/CrossOver/            .built-against  26.3.0.39832
    gst-codecs/Crossover_patched/    .built-against  26.3.0.39832

The second is a copy this project patched for its own use, not a CrossOver
anyone can go and get.

Named after the application because a CrossOver updated in place keeps its path:
the links still resolve, and a bottle pointing at the directory stays correct.
Keying on the version instead would orphan the staging on every update and
report drift for something nobody did. The version is recorded in
`.built-against` rather than in the name, which is what lets the app say
"CrossOver has been updated, run this again" instead of waiting for a crash to
say it.

The directory name is the **filename**, not the name the bundle declares: two
installs can call themselves the same thing. On the machine this was written on,
two do.

A version map is written alongside, so a bottle's recorded engine resolves to a
directory:

    26.3.0.39832|CrossOver|…/gst-codecs/CrossOver/x86_64/gstreamer-1.0

One line per engine and architecture. 26.3 ships x86_64 GStreamer and no ARM, as
above, so it contributes one line rather than two.

## Getting it in front of CrossOver

One line in the bottle's `cxbottle.conf`:

    "GST_PLUGIN_PATH" = "…/gst-codecs/<CrossOver>/<arch>/gstreamer-1.0"

A bottle's environment is applied before CrossOver's launcher runs, and the
launcher sets only `GST_PLUGIN_SYSTEM_PATH` and never touches this one, so the
entry survives. A live wineserver caches the old configuration, which is why a
game has to be closed entirely rather than relaunched.

**Which bottle gets it is a choice, not a guess.** The app used to decide by
looking for a save folder from a game that needs the decoder — and RE Engine
games keep their saves in Steam Cloud and leave nothing behind, so a bottle where
Devil May Cry 5 had crashed reported that it needed nothing. Staging is global
and idempotent; the bottle is picked in the Bottles sheet, and can be unpicked
there too.

## The mismatch worth knowing about

A staged directory is bound to the CrossOver it was built for, through those
thirteen symlinks. Run a bottle under a *different* CrossOver and the plugin
pulls that engine's GStreamer in beside the running one — the two-cores crash
again, arrived at from the one direction the design does not cover.

The app prevents this in normal use: it writes the path for the engine the
bottle records and re-points it when the bottle migrates. Only
`diagnostics/launch-with.sh` crosses engines deliberately, and it now says so and
corrects the path for that run.

Worth checking separately: the framework the plugin comes from should be the
same GStreamer series as the core it plugs into. `stage-codecs.sh` compares them
and **refuses** where they differ — it prints `REFUSED:` for that engine and
stages nothing for it, because a plugin from another series does not register.
The framework installed here is 1.24.13. The CrossOver Preview build measured
here had a core a series above that, and was refused rather than staged — a
record of the check doing its job, kept now that Preview is no longer a
supported engine and stable 26.3 is the engine the staging is for.

Advise 1.24.13. It is what winevideo names, and a launcher that places the
plugins inside its own engine may require that release exactly, so recommending
a later 1.24 can break the other route on the same machine.

**The version the script prints is one too high**, which is worth knowing before
anyone chases it. It reads the framework's version out of the compatibility
number in `libgstreamer-1.0.0.dylib`, as `compat/100` and `compat%100`. That
number is 2414 here, so it prints 1.24.14 — and the framework really is 1.24.13:
`gst-inspect-1.0 --version` says so, the framework's `Info.plist` records
`12413`, and `pkgutil` says 1.24.13 for every GStreamer package on this machine.
The compatibility number runs one ahead of `minor*100 + micro`, so the patch
digit comes out one high. Only that digit is affected — the series the refusal
rests on is `compat/100`, which is 24 either way.

## What the script does, step by step

All of it is in
[`runtime/stage-codecs.sh`](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/runtime/stage-codecs.sh),
457 lines of shell with the reasoning in comments beside each decision. The app
runs exactly this file — it ships a copy in its bundle and shells out to it, so
what you read is what runs.

    runtime/stage-codecs.sh                              every engine, every arch
    runtime/stage-codecs.sh all 26.3.0.39832             that engine, every arch
    runtime/stage-codecs.sh all ~/Applications/One.app   that one bundle
    runtime/stage-codecs.sh x86_64                       one architecture only

The second argument takes a bundle path as well as a version, and the reason is
this release's whole situation: a version is ambiguous the moment a copy exists,
because the copy inherits it. A path is not.

What it does, in order:

1. **Find the framework.** `/Library/Frameworks/GStreamer.framework/Versions/1.0`.
   If it is not there the script stops and prints where to get it: the macOS
   runtime package at
   `https://gstreamer.freedesktop.org/data/pkg/osx/1.24.13/`, 1.24.13 exactly,
   with the reason beside it — it is what is installed and working here, it is
   what winevideo names, and a launcher that owns its engine may require it
   exactly.
2. **Read its GStreamer version**, from the compatibility number in
   `libgstreamer-1.0.0.dylib` rather than a plist. What the rest of the script
   uses is the series, `compat/100`, and that is right; the patch digit it also
   prints is one too high, as above. The plist this step passes over is the one
   that would have been exact.
3. **Compare that against every installed CrossOver.** Each engine's own core is
   read the same way, and an engine of a different series is **refused** rather
   than staged. This used to say a mismatch usually works. It does not: a
   rebuild under a 1.28 engine came back with zero `avdec_*` entries, and a
   title that had been playing stopped with nothing anywhere saying why.
4. **Find every CrossOver**, in `/Applications` and `~/Applications`, by whether
   the bundle contains `Contents/SharedSupport/CrossOver` — not by its name.
5. **For each engine**, look first for `libgstlibav`, `libgstmatroska` and
   `libgstvpx` already in the engine's own `lib64/gstreamer-1.0`, and stand down
   if all three are there — the check described above, which does not run as the
   script stands. Otherwise skip the engine if `.built-against` already records
   this exact version, unless forced, and build a fresh staging in a temporary
   directory:
   - copy `libgstlibav.dylib` and `libgstmatroska.dylib`, and the libraries they
     need. The dependency walk is seeded from **every** plugin staged, not from
     one of them: the two do not want the same libraries, and a walk seeded from
     a single plugin leaves the other short one and silently unloadable.
   - walk the plugin's dependencies and, for each one CrossOver already has,
     symlink to CrossOver's copy instead of taking the framework's
   - write `.built-against`, then `.complete` **last**, so a half-built staging
     never reads as finished
6. **Swap it in.** The new directory is moved into place before the old one is
   removed, not after: deleting first left the path a bottle points at absent for
   as long as an `rm -rf` takes, and a game started in that window finds nothing.
7. **Record it** in the version map, replacing that engine's line rather than
   appending to it.
8. **Print what is staged per engine**, which is what the app reads back.

The two counts it prints at the end of each engine are the whole design in one
line:

    ffmpeg and friends copied : 9
    CrossOver libraries linked: 13

## What the staging is not

Every line here is about `stage-codecs.sh` and the route it builds. The engine
copy is the other route, and the first two do not hold for it: it carries
winevideo's binaries and it puts them inside an engine.

- Nothing is redistributed. The decoder is borrowed from a GStreamer the user
  installed themselves.
- No CrossOver file is modified, added or removed.
- No file is placed beside any game, and no registry key is written.
- It is idempotent. Running it again re-checks each engine and rebuilds only
  what changed.
