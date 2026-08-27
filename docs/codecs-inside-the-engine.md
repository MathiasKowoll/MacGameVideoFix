# Codecs inside the engine

Measured 2026-08-26.

**This does not replace the staging, and is not meant to.** MacGameVideoFix
installs onto whatever CrossOver a person already has, and does not modify it:
a directory of compiled GStreamer plugins per engine, pointed at by
`GST_PLUGIN_PATH` in each bottle, is how that works and how it stays working.
Nothing here changes that.

What follows applies to a **launcher that owns its engine** and can put files
inside it. RaccoonBot is one, and moved to this arrangement; our own installers
did not. The two coexist: the same plugins, placed differently by whoever is in
a position to place them.

For that case, putting the files inside the engine is simpler in every
direction.

## What was placed

Into `<engine>/lib/x86_64`, for an engine built on CrossOver 27:

    lib/x86_64/gstreamer-1.0/libgstlibav.dylib
    lib/x86_64/libavcodec.60.dylib   libavfilter.9.dylib
    lib/x86_64/libavformat.60.dylib  libavutil.58.dylib
    lib/x86_64/libswresample.4.dylib libz.1.dylib  libbz2.1.dylib

Eight files, and that is the whole of it. The engine already carries `libglib`,
`libgobject`, `libgstreamer-1.0`, `libgstbase`, `libgstvideo`, `libgstaudio`,
`libgstpbutils` and `libgstmatroska`, so only libav and its own dependencies
travel. Checked with `otool -L` before launching: the dependency graph closes
without leaving the bundle.

That is also why this is cleaner than staging. A staged plugin brings its own
copy of the GStreamer core, so a process ends up with two — ours at 1.24.13 and
the engine's at 1.28. Placed inside, the plugin binds to the engine's.

**The destination differs by engine.** CrossOver 26.3 keeps plugins in
`lib64/gstreamer-1.0`; CrossOver 27 keeps them in `lib/<arch>/gstreamer-1.0`.
Do not invent a `lib64` on a 27-based engine: the block in its `wine` that sets
`GST_PLUGIN_SYSTEM_PATH` *replaces* the path rather than adding to it, so a
half-filled `lib64` would hide the engine's own twenty plugins. winevideo, being
26.3-based, reconstitutes the whole directory in `lib64` with libav added — 21
files where the stock engine has 20.

## The measurement, and the trap under it

Devil May Cry 5 is the cleanest possible subject: its entry in the catalogue is
codec-only. No proxy, no bridge, no patch — nothing of ours runs inside that
process. Its symptom is binary: without the codec it crashes when a skill
preview video plays.

The first run "worked" and proved nothing. GStreamer's registry cache stores
**absolute plugin paths** and loads from them directly, so with the staging
directory still on disk, commenting out `GST_PLUGIN_PATH` changes nothing: the
cache remembers where the plugin was and goes there. The timestamps gave it
away — the cache was older than the file placed in the engine, and the path it
credited was the staging one.

The test that counts closed all three doors: the staging directory moved aside
so the remembered path no longer exists, the cache deleted so a fresh scan is
forced, and the bottle's variable left off. The video played, and the newly
written cache says:

    only libgstlibav path credited:
      <engine>/lib/x86_64/gstreamer-1.0/libgstlibav.dylib
    registered: avdec_wmv3, avdec_vc1, avdec_h264, avdec_aac
    staging paths in the cache: 0

**Anyone repeating this owes it those three doors.** A codec test with a stale
registry cache is not a codec test.

## What it removes, for whoever can use it

Every failure the staged arrangement produced in one afternoon, none of which
announced itself. These are reasons for a launcher to prefer placement, not
faults that make staging unusable -- it carries the titles it always did:

- one plugin cache per architecture, shared by every engine on the machine, so
  two engines with different plugin sets take turns overwriting each other's view
- a `-d lib64/gstreamer-1.0` test inside CrossOver's `wine` that silently
  decides whether `GST_REGISTRY` is set at all
- bottles pointed at a staging built for a different engine's core
- and nothing checking any of it

## What it does not fix

Nioh crashes on that engine before reaching any video, with the codec placed and
with it removed, so this is not the cause. Nioh 2 — same bridge, same codec,
same engine — plays. The difference is the title, not the arrangement, and Nioh
has never been measured on that engine: its catalogue row says 26.3 and Preview.

## The other half, and why it does not travel

Placing plugins is one of the two things winevideo does. The other is replacing
part of the engine's own Wine, and that half is not portable.

Its payload carries eight binaries:

    wine-pe/    d3d9.dll  mfplat.dll  ntdll.dll  qasf.dll
                quartz.dll  winegstreamer.dll  winevideo_compat.dll
    wine-unix/  winegstreamer.so

`ntdll` among them, which is as deep as an engine patch goes. And they are built
against a specific Wine:

    CrossOver 26.3, and winevideo   wine-11.0-8726-g2e2f5fca349
    CrossOver Preview 27, the fork  wine-11.15-8895-g32f409fef6a

Two different builds. The unix-side `.so` binds to the internals of the Wine it
was compiled with, so carrying winevideo's into a 27-based engine is not
homologation, it is mixing two ABIs. That is also why the patcher pins itself:
its payload ships `stock-inventory-26.3.0.39832.json` and refuses a build it does
not recognise, which is the discipline to copy whatever else is done here.

So the split is:

| | travels | how |
| --- | --- | --- |
| GStreamer plugins | **yes** | eight files, measured on a 27-based engine |
| The replaced Wine DLLs | **no** | rebuild from the source patches against that engine's Wine |

The patches themselves are readable and are the handover: winevideo ships them
under `review/build/source-patches/0.5.0/`, and the two that matter for an
Electra title are `0018-winegstreamer-remove-compressed-queue-time-bound` and
`0019-winegstreamer-lift-decodebin-demux-time-bound`.

Whether a rebuild carried them is not a matter of trust:
`diagnostics/check-engine-media.py` reads the built binary and says so.
