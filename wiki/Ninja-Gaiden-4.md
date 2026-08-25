# NINJA GAIDEN 4

The title that closed the container gap. Every other repair here works on a
frame that already exists; this one is about a file nothing could open, and the
answer turned out to be a single missing demuxer.

| | |
| --- | --- |
| Symptom | *"Windows is missing required components… install Windows Media Foundation and the VP9 Codec. The game will now exit"* -- and it exits |
| Cause | CrossOver ships no Matroska demuxer, so a WebM is recognised and then has nothing to hand off to |
| Fix | Stage `libgstmatroska` beside the decoder; answer the VP9 MFT gate and decode in software |
| Backend | **D3DMetal**, D3D12 |
| CrossOver | 26.3, stock. Preview not yet measured |

## The message is misleading, and precisely so

The dialog names the VP9 codec, so that is where eight earlier runs went looking.
The decoder was never missing. `avdec_vp9` -- from the `libgstlibav` this project
already staged for six other games -- decodes these files, and so does `vp9dec`;
sampling twenty-five of them produced identical `I420` output either way.

What is missing sits one step earlier. NG4's videos live in
`Assets/Movies/*.msd`, an extension Koei Tecmo invented, and 399 of the 400 files
are ordinary Matroska/WebM carrying VP9 Profile 0. The chain that has to run is

    typefind  ->  matroskademux  ->  a VP9 decoder

and CrossOver 26.3 ships demuxers for ASF, AVI, ISO-MP4 and WAV. There is no
Matroska among them. The file is identified correctly as WebM and then goes
nowhere.

Media Foundation reports that as `MF_E_UNSUPPORTED_BYTESTREAM_TYPE`
(`0xc00d36bb`) out of `MFCreateSourceReaderFromURL` -- an error about a *byte
stream*, not about a codec. The game turns it into a sentence about VP9, and the
sentence sends you to the wrong half of the pipeline.

`gst-libav` does not close it either: it registers 36 demuxers and deliberately
leaves Matroska to gst-plugins-good, whose `libgstmatroska` owns the format. Its
only Matroska element is a muxer.

## The fix, in four parts

**One demuxer, staged.** `runtime/stage-codecs.sh` now stages
`libgstmatroska.dylib` alongside `libgstlibav.dylib`, by the same rule as before:
support libraries copied, everything named `libgst*` or `libglib*` symlinked into
the CrossOver being staged for, so exactly one GStreamer core is in the process.
The dependency walk is seeded from every staged plugin rather than one, because
Matroska wants `libz`, `libbz2` and `libgstriff` where libav wants FFmpeg.

**The MFT gate, answered in memory.** Before opening anything the game calls
`MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, input={Video,VP90})` and counts what comes
back. Zero is fatal and immediate -- that is the dialog, and the game exits
without ever touching a file. `NG4_ANSWER_MFT=1` returns one. This is a count the
game takes once, and answering it is equivalent to registering a decoder for that
gate.

**Software decode.** `BEAST_REFUSE_D3D_MANAGER=1` refuses
`MFCreateDXGIDeviceManager`, so frames arrive in system memory. Without it the
run reaches the video and dies inside Metal, with
`gst_video_info_from_caps: assertion 'gst_caps_is_fixed (caps)' failed` followed
by a texture-descriptor assertion.

**DirectStorage off.** Rename `dstoragecore.dll` beside the game.
`DStorageGetFactory` returns `DXGI_ERROR_UNSUPPORTED` regardless and the title
takes its other I/O path -- it does this identically in a configuration that
plays, so it is a precondition rather than a repair.

No registry key is needed. No CrossOver file is touched.

## Measured, one variable at a time

Same bottle, same probe, same game; only the named thing changes.

| Codec staged | Extra | Result |
|---|---|---|
| none reaching the process | -- | `SourceReaderFromURL -> 0xc00d36bb` |
| libav only | -- | same error; VP9 dialog; game itself playable |
| libav **+ matroska** | -- | `SourceReaderFromURL -> S_OK`, then a Metal assertion |
| libav + matroska | software decode | **plays** |
| libav + matroska | MFT keys deleted from the registry | `MFTEnumEx -> 0`, game exits |
| libav + matroska | MFT deleted, answered in memory instead | **plays** |
| libav + matroska | every registry key removed | **plays**, video and all |

The last row is the one that says a `.reg` is not part of this fix. It needs a
caveat about method: a live `wineserver` keeps the bottle's registry in memory
and writes it back when it exits, so keys deleted while it was running come back
with their original timestamps. A first attempt at that row looked registry-free
and was not. The result above comes from a run where every wine process was
killed, the server was confirmed gone, the registry was edited afterwards, and
the state was checked again during the run.

## Validated twice, in two bottles

The first validation was in a bottle that had winevideo installed, with its
registry keys removed one group at a time. That leaves a fair objection: the
bottle had been through winevideo's installer, and something it left behind
could be doing the work.

So it was repeated in **SteamStable**, a bottle that has never had winevideo
installed -- no `WINEVIDEO_COMPAT_PROFILE`, no `WINEVIDEO_INSTALL_MARKER`, no
patched engine -- under stock CrossOver 26.3, with only this project's staged
codec and the two probe levers in its configuration. Same result:

    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered
    MFCreateSourceReaderFromURL(.\Assets/Movies/88f75716-....msd) -> 0x00000000

Game reaches its menu, video plays. The repair travels with the mechanism, not
with the bottle.

**And a third bottle settled the registry question, by accident.** SteamStable's
registry does still carry the VP9 MFT keys, so that run confirms codec and levers
rather than the registry-free path. `Steam` had never been prepared for this
title at all -- **zero VP9 MFT keys**, no lever, no staged codec -- and launching
the game there produced precisely the failure the model predicts for "neither
route active":

    MFTEnumEx flags=0x3f -> 0x00000000, 0 decoder(s) offered
    -> "Windows is missing required components… The game will now exit"

Given only `GST_PLUGIN_PATH`, `NG4_ANSWER_MFT=1` and
`BEAST_REFUSE_D3D_MANAGER=1`, and no registry key anywhere, it plays:

    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered
    MFCreateSourceReaderFromURL(.\Assets/Movies/88f75716-....msd) -> 0x00000000

The gate is real, either route satisfies it, and this project ships no `.reg`.

## CrossOver Preview: a different, unsolved problem

**Preview 27.0.0.40921 does not run this title, and the reason is not the one
this page fixes.** The stall happens long before any video call: the game never
reaches `MFCreateSourceReaderFromURL` at all, and the staged plugins are never
loaded, so the demuxer cannot be at fault either way.

What the stall looks like, from `winedbg` backtraces of all ~90 threads (macOS
`sample` is useless here -- it cannot unwind Rosetta-translated x86 and returns
one repeated `ntdll.so` address):

- every thread is parked; CPU 2-5%, resident memory steady, no crash, no dialog
- the main thread waits on a C++ condition variable (`msvcp140`) inside the
  game's own code
- the worker pools -- groups of 12, 11, 8, 6 and 3 threads -- are all parked on
  the same few addresses, which is what an idle job system looks like
- **no thread anywhere touches `d3d12`, `dxgi`, `mfplat` or `winegstreamer`**
- the only thread whose wait leaves the process is one calling `setupapi`
  (HID enumeration), with `plugplay` blocked serving that same RPC

Ruled out by measurement, each costing a run:

| Hypothesis | Test | Result |
|---|---|---|
| This project's probe causes it | ran with the proxy DLL removed entirely | stalls identically |
| HID enumeration | `winebus\Parameters\DisableInput=1` | no change; and `joy.cpl` completes, so plugplay is not wedged |
| Steam overlay | per-app DLL override disabling `gameoverlayrenderer64` | no change |
| Missing audio driver | `HKCU\Software\Wine\Drivers\Audio=""` | no change |
| Refusing the MF device manager | control run has no probe, so no refusal | stalls anyway |

Worth stating because it removes a tempting explanation: **every Preview run was
made with the full registry in place** -- the eight VP9 MFT keys and all three
byte-stream handlers, byte-identical to the ones in the bottles where the title
plays. Whatever Preview is missing, it is not a registry key.

**One real defect was found on the way, and it stands on its own.** CrossOver
Preview 27.0.0.40921 ships **no `winecoreaudio.drv`** in any of its three PE
architectures (`aarch64-windows`, `i386-windows`, `x86_64-windows`), while 26.3
ships it in both of its two. Preview keeps the unix half
(`x86_64-unix/winecoreaudio.so`, `aarch64-unix/winecoreaudio.so`), its own
`mmdevapi.dll` still resolves audio backends by name -- `pulse,alsa,oss,coreaudio`
-- and bottles carry a `HKCU\Software\Wine\Drivers\winecoreaudio.drv` key
pointing at the missing module. Whether it explains this stall is **not
established**; that it is a packaging gap is.

## What this changes for other titles

For a long time the project's position was that both CrossOver builds decode VP9
the same way, and that opening a WebM was something only Preview could do. That
was measured and it was true. It was also a **plugin** difference rather than an
engine one, and staging the demuxer closes it on stable as well.

DYNASTY WARRIORS: ORIGINS ships 355 `.webm` cutscenes and its row still records
that it could not get as far as decoding on 26.3. That row has not been
re-measured since the demuxer was staged, and it should be before anything is
claimed for it.

## Caveats

Measured on 26.3 stock, on an M4 Max. Preview is not yet measured. The video path
is software decode, which is the same trade three other titles here already make.
