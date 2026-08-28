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

Whether a rebuild carried them is not answerable by reading strings out of the
binary. That was tried -- a check that separated winevideo's build from stock
across five engines -- and this project's own four-patch build carries none of
those strings and plays the title anyway. Correlation, not mechanism. The check
was withdrawn. What answers the question is running the titles.

## The d3d9 video bridge is two patches, and we had one

Nioh's page says its repair is "hand the game a share handle that exists". That
comes from winevideo's patch set, and it is in two halves:

    0008-d3d9-dxmt-video-bridge-handle.patch    the surface, and its shared handle
    0009-d3d9-dxmt-video-bridge-upload.patch    the pixels, and a query to wait on

0008 gives a d3d9 surface an ID3D11Device, context and texture, and a handle that
can be shared. 0009 adds `dxmt_video_map_data`, `dxmt_video_map_pitch` and an
`ID3D11Query` -- it is the half that actually uploads the decoded frame and waits
for the upload to finish. With only 0008, a game gets a valid handle to a surface
nothing ever writes to.

`built-for.json` records `patches 0002 0003 0006 0008`, which reads as though we
had that first half. We did not: `scripts/build-winegstreamer.sh` takes its
patches from `winevideo Patcher.app/.../source-patches/0.5.0`, a different set
with different numbering, so its 0008 is some other patch entirely. The number in
that file is not a cross-reference to this repository's copy of winevideo, and
reading it as one is how an afternoon gets spent.

Both applied cleanly to wine-11.0-8726 on 27 Aug 2026 and d3d9.dll builds with
`make dlls/d3d9/all` -- with llvm-mingw first on PATH, because the tree is
configured for clang and a real x86_64-w64-mingw32-gcc rejects `-fms-hotpatch`
and `-ffp-exception-behavior=maytrap` before it compiles a line. The result is
1.5 MB against winevideo's 294 KB, which is `-Wl,-debug:dwarf` and not more code.

It did not fix Nioh, and that is worth recording too: with the complete bridge in
the engine the title still dies with zero calls to it -- no CreateTexture, no
CreateOffscreen, no StretchRect. It never reaches video. What kills it is
Steam's client pipe:

    src\common\pipes.cpp (879) : CClientPipe::BWriteAndReadResult: BWrite failed
    src\common\pipes.cpp (879) : Fatal assert; application exiting

Four runs, always the same. Note the verb: MGS4 fails to *read* that pipe because
it is too busy to answer, and a real yield fixed it. Nioh fails to *write*, which
is what a pipe already closed at the other end looks like -- so the two are not
the same fault wearing the same message.
