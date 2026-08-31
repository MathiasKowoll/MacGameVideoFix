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
| GPTK | 3.0 -- this title is the exception to the project's 4.0b2 rule |
| CrossOver | Stable 26.3.0.39832, measured on a stock install as well as on this project's patched engine |

## The message is misleading, and precisely so

The dialog names the VP9 codec, so that is where eight earlier runs went looking.
The decoder was never missing. `avdec_vp9` -- from the `libgstlibav` this project
already stages for the other titles whose Codec column names it -- decodes these
files, and so does `vp9dec`; sampling twenty-five of them produced identical
`I420` output either way.

What is missing sits one step earlier. NG4's videos live in
`Assets/Movies/*.msd`, an extension Koei Tecmo invented, and 399 of the 400 files <!-- count-ok -->
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

All four are what the app installs and configures; none of them is a setting to
remember. That last part was learned the hard way: for a while the two runtime
levers lived only as environment variables in a bottle's configuration, and
CrossOver rewrites that file often enough that they vanished **seven times in one
afternoon** -- each time surfacing as the game's own "the VP9 codec is not
installed" dialog, which looks like a broken install rather than a lost setting.
They are now the default inside the carrier, and the variables remain only as a
way to switch them **off** for a measurement.

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

So it was repeated in **a second bottle that has never had winevideo**
installed -- no `WINEVIDEO_COMPAT_PROFILE`, no `WINEVIDEO_INSTALL_MARKER`, no
patched engine -- under stock CrossOver 26.3, with only this project's staged
codec and the two probe levers in its configuration. Same result:

    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered
    MFCreateSourceReaderFromURL(.\Assets/Movies/88f75716-....msd) -> 0x00000000

Game reaches its menu, video plays. The repair travels with the mechanism, not
with the bottle.

**And a third bottle settled the registry question, by accident.** The second
bottle's registry does still carry the VP9 MFT keys, so that run confirms codec and levers
rather than the registry-free path. A third bottle had never been prepared for this title at all -- **zero VP9 MFT keys**, no lever, no staged codec -- and launching
the game there produced precisely the failure the model predicts for "neither
route active":

    MFTEnumEx flags=0x3f -> 0x00000000, 0 decoder(s) offered
    -> "Windows is missing required components… The game will now exit"

Given only `GST_PLUGIN_PATH`, `NG4_ANSWER_MFT=1` and
`BEAST_REFUSE_D3D_MANAGER=1`, and no registry key anywhere, it plays:

    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered
    MFCreateSourceReaderFromURL(.\Assets/Movies/88f75716-....msd) -> 0x00000000

The gate is real, either route satisfies it, and this project ships no `.reg`.

## History: CrossOver Preview, a different and unsolved problem

**CrossOver Preview is no longer a supported engine here; stable 26.3 is the
only one.** This section is kept as a record of what was measured on Preview
while it was still on the table. Nothing in it is an instruction, and none of it
describes an engine to run this title on.

**Preview 27.0.0.40921 did not run this title, and the reason was not the one
this page fixes.** The stall came long before any video call: the game never
reached `MFCreateSourceReaderFromURL` at all, and the staged plugins were never
loaded, so the demuxer could not be at fault either way.

What the stall looked like, from `winedbg` backtraces of all ~90 threads (macOS
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
plays. Whatever Preview was missing, it was not a registry key.

**One real defect was found on the way, and it stands on its own.** CrossOver
Preview 27.0.0.40921 shipped **no `winecoreaudio.drv`** in any of its three PE
architectures (`aarch64-windows`, `i386-windows`, `x86_64-windows`), while 26.3
ships it in both of its two. Preview kept the unix half
(`x86_64-unix/winecoreaudio.so`, `aarch64-unix/winecoreaudio.so`), its own
`mmdevapi.dll` still resolved audio backends by name -- `pulse,alsa,oss,coreaudio`
-- and bottles carried a `HKCU\Software\Wine\Drivers\winecoreaudio.drv` key
pointing at the missing module. Whether it explained this stall was **never
established**; that it was a packaging gap is.

## What this changes for other titles

For a long time the project's position was that both CrossOver builds decode VP9
the same way, and that opening a WebM was something only Preview could do. That
was measured and it was true. It was also a **plugin** difference rather than an
engine one, and staging the demuxer closes it on stable as well.

DYNASTY WARRIORS: ORIGINS ships 355 `.webm` cutscenes, and its row records 26.3
-- but with a dagger on Stock, because the picture was only ever read there on a
patched engine. Nobody has yet watched it play on a stock 26.3 with the demuxer
staged, and that is the run still owed.

## Open: with GPTK 4.0b2 it has sound and no picture

**This title is the exception to the project's toolkit rule.** The general rule
is GPTK 4.0b2; NINJA GAIDEN 4 is measured working on 3.0, and that is what its
row says. What follows is what 4.0b2 does instead. It is being looked at now.

Observed 2026-08-31, on this project's patched stable 26.3 engine with the
toolkit set to 4.0b2 and saved: the game runs, the cutscene plays its audio, no
picture appears, and the window never reaches full screen. It does not stall and
it does not exit.

From the outside that is the shape several other titles here turned out to have,
sound without picture, where the decoder produces frames and something
downstream never draws them. **It is not that shape.** No video file was ever
opened in that run and Media Foundation recorded nothing, so there is no frame
to lose, and neither the staged demuxer nor the MFT gate this page is about is
involved. The fault sits before the video path rather than in it.

**Where it is waiting.** Sampling the process put the Metal submission queue in
`IOGPUCommandQueueWaitMTLEvent` for essentially every sample, with the GPU at 0%
utilisation, and D3DMetal's own thread waiting on `os_sync_wait_on_address`.

The wait is not immediate and not certain. A second run was still doing real
work a minute and a half in, with the GPU at 27%, so this is a state the process
falls into rather than one it starts in.

**Then it was run, and the queue went away.** Apple's D3DMetal 4.0b2 reads an
environment variable `D3DM_MTL4` that its 3.0 generation does not have -- the
switch for the Metal 4 path whose queue was measured parked. Set to `0` in
CrossOver's own Run dialog, the thread `com.Metal4.SubmissionQueue` no longer
exists at all, `IOGPUCommandQueueWaitMTLEvent` drops to zero samples, work is
submitted again, and the game reaches full screen and opens its cutscene file.

One caveat on that, because two things changed at once: `MTL_HUD_ENABLED=1` was
set in the same run and had not been set before. The attribution to `D3DM_MTL4`
still holds on structural grounds -- a HUD does not remove a submission queue --
but whether the HUD also contributed to the unstall is not separated by a single
run, and the stall was intermittent to begin with.

**How long each toolkit takes to reach a cutscene**, same engine, same bottle,
only the toolkit changed: about **2 minutes 30 seconds** on 4.0b2 with the Metal
4 path off, and about **28 seconds** on 3.0. Five times, before anything to do
with video.

**And underneath it, a second fault that is not the toolkit's.** With the stall
gone, the cutscene file opens and `ReadSample` returns no sample, 200 calls
running, flags 0. The same happens on 3.0 -- 200 calls, nothing -- so this one is
not a 4.0b2 regression. GStreamer's own debug output names it:

    wg_format.c:675:wg_format_from_caps: Unhandled caps video/x-vp9,
        width=1920, height=1080, framerate=60000/1001, ...

Everything upstream works: `matroskademux` parses the WebM and finds the track,
`vp9dec` is instantiated, the caps are complete. What is missing is the mapping
from those caps into winegstreamer's own `struct wg_format`, which the media
source needs. The carried patch 0002 adds VP9 in the other direction, for the
decoder transform, and does not reach this one.

So the table's `**3.0 only**` stands, but it should be read for what it is: on
3.0 this title **runs and plays** -- 60 fps, 7.8 ms of GPU at the title screen --
not that its cutscenes are seen. No NG4 log on any engine, on any date, has ever
recorded a frame coming out.

**Retracted, and kept because it was written down.** An earlier version of the
tables said the stall was the toolkit "executing command lists concurrently with
no lever to turn that off". That was never measured, and what has now been
measured is a submission queue waiting on an event. The old sentence was removed
rather than corrected; it is recorded here so its removal is visible.

Worth separating from the toolkit-selection bug found the same day: several
titles were measured against a generation other than the one selected, because
the selection was not applied at launch. These observations were made after that
was understood, with 4.0b2 selected and saved.


## Caveats

Measured on stable 26.3 on an M4 Max, on a stock install and on this project's
patched engine. Preview 27.0.0.40921 was measured too, back when it was still an
engine this project looked at; it stalled before any video call, and the history
section above says what was found. The video path is software decode, which is
the same trade the rows whose Fix column names software decode already make.
