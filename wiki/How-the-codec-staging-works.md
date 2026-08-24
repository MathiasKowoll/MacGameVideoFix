# How the codec staging works

Six of the games here need a decoder CrossOver does not ship. This is what the
app does about it, why it is built that way, and why the obvious approach
crashes.

Nothing is redistributed and no CrossOver file is touched. The whole repair is a
directory of libraries and one line in a bottle's configuration.

## What is missing

CrossOver decodes VP9, H.264 and AAC on its own. It has no decoder for **VC-1**,
**WMV3** or **WMA** — and Persona 5 Strikers, Nioh, Nioh 2, Devil May Cry 5,
RESIDENT EVIL 2 and RESIDENT EVIL 3 all play video in one of those.

The official [GStreamer.framework](https://gstreamer.freedesktop.org/download/)
for macOS has them, in `libgstlibav` — GStreamer's binding to FFmpeg. The
decoder exists on the machine already. The problem is getting it in front of
CrossOver.

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

## The shape that works

Give the plugin a directory of its own, and be deliberate about which
dependencies come from where.

    gst-codecs/<CrossOver>/x86_64/
      gstreamer-1.0/libgstlibav.dylib     the decoder
      lib/                                 what it needs
      .built-against                       the engine version it was built for
      .complete                            written last, when it really is

The plugin resolves its dependencies through `@rpath`, and its rpath list
includes `@loader_path/../lib` — so `lib/` beside it is where everything is
found. That directory is filled two different ways, and the difference is the
whole design:

- **FFmpeg is copied.** Seven real files — `libavcodec`, `libavformat`,
  `libavfilter`, `libavutil`, `libswresample` and their support. CrossOver does
  not have these at all, which is the entire reason for doing any of this.
- **GStreamer's own core is symlinked into CrossOver.** Twelve links, pointing
  at that CrossOver's `libgstreamer`, `libglib`, `libgobject`, `libgstvideo` and
  the rest.

That second half is what avoids the crash. The plugin loads **CrossOver's**
GStreamer, not the framework's, so there is one core, one type registry, and the
decoders register into the same place everything else lives.

## One staging per engine, and why the name is the key

Every installed CrossOver gets its own directory, named after the application
rather than its version:

    gst-codecs/CrossOver/            .built-against  26.3.0.39832
    gst-codecs/CrossOver-Preview/    .built-against  27.0.0.40921
    gst-codecs/Crossover_patched/    .built-against  27.0.0.40817

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
    27.0.0.40921|CrossOver Preview|…/gst-codecs/CrossOver-Preview/x86_64/gstreamer-1.0

## Getting it in front of CrossOver

One line in the bottle's `cxbottle.conf`:

    "GST_PLUGIN_PATH" = "…/gst-codecs/<CrossOver>/x86_64/gstreamer-1.0"

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
twelve symlinks. Run a bottle under a *different* CrossOver and the plugin pulls
that engine's GStreamer in beside the running one — the two-cores crash again,
arrived at from the one direction the design does not cover.

The app prevents this in normal use: it writes the path for the engine the
bottle records and re-points it when the bottle migrates. Only
`diagnostics/launch-with.sh` crosses engines deliberately, and it now says so and
corrects the path for that run.

Worth checking separately: the framework the plugin comes from should be the
same GStreamer series as the core it plugs into. `stage-codecs.sh` compares them
and says which engines match. As this was written the framework was 1.24.14
while Preview shipped 1.28.5 — a series apart, which usually works and is not
the closest fit.

## What the script does, step by step

All of it is in
[`runtime/stage-codecs.sh`](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/runtime/stage-codecs.sh),
312 lines of shell with the reasoning in comments beside each decision. The app
runs exactly this file — it ships a copy in its bundle and shells out to it, so
what you read is what runs.

    runtime/stage-codecs.sh x86_64                  every engine installed
    runtime/stage-codecs.sh x86_64 26.3.0.39832     just that one

What it does, in order:

1. **Find the framework.** `/Library/Frameworks/GStreamer.framework/Versions/1.0`.
   If it is not there the script stops and prints where to get it, naming the
   1.24 runtime package as the version this was verified with.
2. **Read its GStreamer version**, from the compatibility number in
   `libgstreamer-1.0.0.dylib` rather than a plist — the number encodes
   `1.MINOR.PATCH` directly.
3. **Compare that against every installed CrossOver.** Each engine's own core is
   read the same way, and the script says which ones are the same series and
   which are not. A mismatch still stages, because GStreamer keeps its ABI
   across 1.x, but it says so.
4. **Find every CrossOver**, in `/Applications` and `~/Applications`, by whether
   the bundle contains `Contents/SharedSupport/CrossOver` — not by its name.
5. **For each engine**, skip it if `.built-against` already records this exact
   version, unless forced. Otherwise build a fresh staging in a temporary
   directory:
   - copy `libgstlibav.dylib` and the FFmpeg libraries it needs
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

    ffmpeg and friends copied : 7
    CrossOver libraries linked: 12

## What it is not

- Nothing is redistributed. The decoder is borrowed from a GStreamer the user
  installed themselves.
- No CrossOver file is modified, added or removed.
- No file is placed beside any game, and no registry key is written.
- It is idempotent. Running it again re-checks each engine and rebuilds only
  what changed.
