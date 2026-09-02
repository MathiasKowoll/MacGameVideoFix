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

## Closed on our side: with GPTK 4.0b2 nothing is ever presented

**This title is the exception to the project's toolkit rule.** The general rule
is GPTK 4.0b2; NINJA GAIDEN 4 is measured working on 3.0, and that is what its
row says. What follows is what 4.0b2 does instead, measured over 2026-09-01 and
2026-09-02 on this project's patched stable 26.3 engine, an M4 Max on macOS 27,
with the toolkit set to 4.0b2 and `D3DM_MTL4=0`.

**The symptom, stated correctly.** On 4.0b2 the game is alive and blind. Audio
plays, keyboard input takes effect, the process sits at 3 GB of resident memory
against 10 GB on 3.0, the Metal HUD never appears, and nothing is ever seen --
not the logos, not the menu, not the movie. The one run of 2026-09-01 that
showed logos turned out to be on 3.0 front-ends: its patch attempts were refused
with error 87, which is how 3.0's native D3D objects answer.

**Everything up to the screen works, and each link was measured.** In order:

| Link | Measured on 4.0b2 |
|---|---|
| Container and codec | `matroskademux` parses the WebM, `vp9dec` decodes; 681 buffers into `sink_chain_cb`, 327 queued, flush events paired |
| Media Foundation | the reader is **asynchronous** (NULL out parameters), frames arrive in `OnReadSample`: 300 of them, timestamps rising, one buffer of 3,110,400 bytes = 1920x1080 NV12 |
| The picture | the luma plane sampled across the frame reads min 25, max 222, mean 61 -- a real image, not a black frame of the right size |
| The game reads it | `Lock` on every frame; overwriting the buffer with flat white (verified by reading it back) changes nothing on screen |
| The game uploads it | at movie open it creates a placed `1920x1080 R8G8B8A8_UNORM_SRGB` texture and a buffer of exactly 8,294,400 bytes; it converts NV12 to RGBA itself |
| The swap chain | created 1280x720 RGBA8, three buffers, `SEQUENTIAL` swap effect; re-created at 2560x1440 (or 1920x1242 R10G10B10A2) on the mode switch |
| Present | thousands of `Present` calls, every one `S_OK`; the swap chain's own `GetLastPresentCount` advances in step |
| The window | exists, is on screen, 2560x1440, and captured from the host is 0% non-black; on 3.0 the same window captures 100% non-black |
| D3DMetal's own HUD | `D3DM_SHOW_HUD_STATS=1` draws nothing either |

**Excluded, each with the run that excluded it.** Wine reports nothing: with
`fixme+d3d11,fixme+dxgi` active and the DLLs loaded, zero lines -- the game does
not use D3D11 video processing. CoreAnimation and Metal log nothing. The four
capabilities 4.0b2 advertises and 3.0 does not (`DepthBoundsTest`,
`EnhancedBarriers`, `UnrestrictedBufferTextureCopyPitch`,
`UnrestrictedVertexElementAlignment`) are never queried by the title; masking
them to 3.0's values changed nothing. Format support is identical on both
toolkits. Allowing the DXGI device manager crashes the game on a null write
before any frame, with our D3D11 patch and without it -- so refusing it is load
bearing, not a leftover from Beast. Stripping `ALLOW_TEARING`, forcing the flip
model (accepted; the game then presents at interval 1), dropping the
frame-latency waitable object: accepted, still black. Forcing a windowed swap
chain crashes inside `libd3dshared`. `CaptureDisplaysForFullscreen` moves the
window from wine's level 26 to the shielding level: still black. `mtl3on4`,
Apple's Metal-3-on-4 shim on macOS 27, is mapped on both toolkits.

**So the conclusion is narrow and firm.** D3DMetal 4.0b2 accepts every
presentation and counts it, and no pixel of its own or of the game's reaches a
window on this host. That sits below anything a probe on the Windows side of
the process can reach. What is actionable: this title ships on 3.0, which
works; the chain above is the report for Apple; and the probe keeps every
switch it grew (`NG4_WATCH_PRESENT`, `NG4_WATCH_D3D12_RESOURCES`,
`NG4_WATCH_MOVIE_COPY`, `NG4_NO_TEARING`, `NG4_FLIP_MODEL`, `NG4_NO_WAITABLE`,
`NG4_FORCE_WINDOWED`, `NG4_CAPS_LIKE_3`, `NG4_WATCH_CAPS`, `NG4_PAINT_TEST`,
`NG4_WITHHOLD_D3D_FROM_MFT`, `NG4_FORCE_PATCH`), all off by default, so the
next GPTK beta can be measured in a few minutes. `NG4_WATCH_PRESENT` is a
4.0b2 instrument only: on 3.0 the DXGI front-end is patchable and the hooks
hang the title.

**Retracted, and kept because it was written down.** An earlier version of this
section said three things that were our instrument rather than the game.
*"`ReadSample` returns no sample, 200 calls running, flags 0 -- the same on
3.0"*: the reader is asynchronous, and a NULL sample pointer is what a working
async reader looks like from inside `ReadSample`; five runs were read as a dead
pipeline on the strength of it. *"No NG4 log on any engine has ever recorded a
frame coming out"*: it had, once the callback was watched. *"`wg_format_from_caps:
Unhandled caps video/x-vp9` names the fault"*: that line is a benign trace from
the route that works. Two further instruments lied by construction before being
fixed: a paint test that painted one frame in three hundred because it required
a buffer length the caller does not pass, and a resource log whose cap was spent
on loading-screen render targets before the movie opened. The Metal 4 finding
stands: with `D3DM_MTL4=1` the game never reaches the movie at all; that is an
earlier, separate wall.

**Where the fault line between the toolkits actually runs.** D3DMetal 3.0
implements its D3D11/D3D12 objects in native code that wine reports as
`MEM_FREE` (so no vtable there can be patched -- the error-87 refusals recorded
all over this project's history are that, not a property of 26.3); 4.0b2
implements them in PE, where a single cold slot at a time is tolerated and the
full set is not. That is a different object layer, not a version bump.

## Caveats

Measured on stable 26.3 on an M4 Max, on a stock install and on this project's
patched engine. Preview 27.0.0.40921 was measured too, back when it was still an
engine this project looked at; it stalled before any video call, and the history
section above says what was found. The video path is software decode, which is
the same trade the rows whose Fix column names software decode already make.
