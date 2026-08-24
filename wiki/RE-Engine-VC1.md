# Devil May Cry 5, RESIDENT EVIL 2 and RESIDENT EVIL 3

Three Capcom titles on the same engine, with the same fault and the same
one-line answer. Nothing is installed beside the game for these: the app already
stages what they need, and the fix is to let it.

| | |
| --- | --- |
| Symptom | Crashes when a particular video plays. In Devil May Cry 5 it is the skill previews on the Customize screen |
| Cause | The video is VC-1, and CrossOver ships no VC-1 decoder |
| Fix | The staged codec, wired into the bottle. The app does this |
| Backend | D3DMetal, D3D12 |
| CrossOver | `crossover-preview-arm64-20260821` |

## Two kinds of video, and only one of them works out of the box

RE Engine plays more than one format, which is what made this confusing before it
was measured. Asking GStreamer to name the elements it builds shows both:

    creating element "h264parse"
    creating element "vtdec_hw"          <- Apple VideoToolbox, in hardware

    creating element "asfdemux"
    creating element "avdec_vc1"         <- from the staged codec

The first is H.264 and CrossOver decodes it on its own, in hardware. The second
is the one that crashes without the codec:

    video/x-wmv, wmvversion=(int)3, format=(string)WVC1
    Using libavcodec version 60.3.100

`WVC1` is VC-1 Advanced Profile in an ASF container. And `60.3.100` is the
FFmpeg 6.0 inside the staged tree — the same `Lavc60.3.100` the plugin carries,
which is how we know the decoder comes from what this project stages rather than
from anything CrossOver ships.

## What the crash looks like

A null pointer, dereferenced by the game itself:

    page fault on read access to 0x0000000000000000
    rip: devilmaycry5+0x29526cb
    movq (%rcx), %rax        with rcx = 0

Every resolvable frame belongs to the game. `winegstreamer`, `mfplat`,
`mfreadwrite` and `mfasfsrcsnk` are all loaded, and so is `libgstasf` — the
container is opened and demuxed. What is missing is the decoder, and the game
does not check what it got back before using it.

That last part matters for reading the symptom: the crash is not where the fault
is. Nothing in the graphics stack is involved, and the address it dies at says
nothing about video.

## Why the same fix covers three games

The control is the bottle's own configuration. With `GST_PLUGIN_PATH` commented
out, all three crash; with it in place, all three play. One line, three titles,
same engine.

The decoder trace above was taken on Devil May Cry 5, at the Customize screen,
which is a reliable place to reproduce it in about a minute of play. RESIDENT
EVIL 2 and RESIDENT EVIL 3 were confirmed by the same with-and-without
comparison rather than by their own traces.

## What to do

Open the app, let it stage the codec for the CrossOver you run, and apply it to
the bottle these games live in. There is no proxy DLL, no registry override and
no file placed beside the game — the whole repair is one environment variable
naming a directory of decoders that CrossOver does not ship.

This is the same staging Persona 5 Strikers and the Nioh titles need, which is
worth saying plainly: it is not three more fixes, it is three more games covered
by one that already existed.
