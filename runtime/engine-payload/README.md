# engine-payload

Laid out the way a CrossOver engine is, so a patcher can copy this tree over the
engine it is building without a mapping table of its own:

    wine/x86_64-windows/winegstreamer.dll   ->  <CX>/lib/wine/x86_64-windows/winegstreamer.dll
    wine/x86_64-unix/winegstreamer.so       ->  <CX>/lib/wine/x86_64-unix/winegstreamer.so
    lib64/gstreamer-1.0/*.dylib             ->  <CX>/lib64/gstreamer-1.0/
    lib64/*.dylib                           ->  <CX>/lib64/

`built-for.json` and the two `.md` files are not copied into the engine.

## Two halves that do different jobs

**The bridge** is the `winegstreamer` pair, built here. It is what lets a
Windows process reach GStreamer at all, and it carries no decoder — `otool -L`
shows it linking `libgstreamer`, `libgstvideo`, `libgstaudio`, `libgsttag` and
`libglib`, and nothing else. Read `built-for.json` before installing it and
**check both fields**: the two halves speak an interface that changes between
wine revisions, and onto a different wine they do not degrade — they stop
loading media, failing in the shape of the fault they were built to repair. The
version alone does not identify an engine, because a patched fork and a stock
CrossOver both report `26.3.0.39832`; the app name is what tells them apart.

**The decoders** are the twelve files under `lib64`, copied verbatim from
winevideo's CrossOver build and not ours. Stock CrossOver 26.3 has eighteen
plugins and none of them decodes VC-1, WMV3 or VP9. See `CODEC-LICENCES.md` for
what each one is, its licence, and its sha256 — `check-builds.sh` verifies them
against that table.

Neither half substitutes for the other. A bridge with no decoders decodes
nothing; decoders with a mismatched bridge fail in a way that looks exactly like
the fault the bridge was meant to fix.

## What the list is, and why it is longer than three files

The `lib64` set is the **transitive closure** of what the three plugins need and
stock CrossOver does not already carry, walked with `otool -L` rather than
guessed. That is why `liborc`, `libvpx`, `libz` and `libbz2` are here and why
`libswresample` is, and it is what makes a copy of this tree leave no dangling
`@rpath`. Adding a plugin means walking it again, not appending a filename.

## The destination is not the same on every engine

CrossOver 26.3 keeps plugins in `lib64/gstreamer-1.0`; CrossOver 27 keeps them
in `lib/<arch>/gstreamer-1.0`. **Do not create a `lib64` on a 27-based engine**
— the block in its `wine` that sets `GST_PLUGIN_SYSTEM_PATH` *replaces* the path
rather than appending, so a half-filled `lib64` hides the engine's own twenty
plugins. This tree is laid out for a 26.3-based engine.

## Not here on purpose

The `d3d9.dll` this project also built. It never fixed the title it was made
for, the bridge that does ships in the game folder, and it occupies the path a
patcher writes d9vk to.
