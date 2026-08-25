# Ninja Gaiden 4 — fixed, and the earlier diagnosis was wrong

**Fixed.** It plays on stock CrossOver 26.3 with this project's own mechanism:
a staged GStreamer codec and the runtime probe. No CrossOver file is patched, no
registry key is needed, and winevideo does not have to be installed.

This page replaces an earlier one that reached the opposite conclusion. That
version is worth reading only for how it went wrong, which is recorded at the
bottom.

## What was missing: one demuxer

NG4's videos are WebM — 399 of 400 `Assets/Movies/*.msd` files are plain
Matroska/WebM carrying VP9 Profile 0, and one is H.264 in MP4. The `.msd`
extension is Koei Tecmo's; the bytes are ordinary.

CrossOver 26.3 ships GStreamer demuxers for ASF, AVI, ISO-MP4 and WAV, and
**none for Matroska**. So the chain that has to run —

    typefind (recognises webm)  ->  matroskademux  ->  a VP9 decoder

— breaks in the middle. Media Foundation reports the break as
`MF_E_UNSUPPORTED_BYTESTREAM_TYPE` (`0xc00d36bb`) from
`MFCreateSourceReaderFromURL`, which reads like a codec problem and is not one.
The decoder was never the difficulty: `avdec_vp9` from the `libgstlibav` this
project already staged decodes these files, and so does `vp9dec`. Sampled
25 of the `.msd` files and both decoders produced identical `I420` output.

`gst-libav` does not cover the gap. It registers 36 demuxers and Matroska is not
among them — the format is deliberately left to gst-plugins-good, whose
`libgstmatroska` owns it. Its only Matroska element is `avmux_matroska`, a muxer.

## The fix

Four parts, all inside this project's existing mechanism.

1. **Stage `libgstmatroska.dylib`** alongside `libgstlibav.dylib`.
   `runtime/stage-codecs.sh` now stages both, and the dependency walk is seeded
   from every plugin staged rather than from one of them, because the two do not
   need the same libraries — Matroska wants `libz`, `libbz2` and `libgstriff`
   where libav wants FFmpeg. Same design as before: FFmpeg and the Matroska
   support libraries are copied, and everything named `libgst*`, `libglib*` and
   friends is symlinked into the CrossOver being staged for, so exactly one
   GStreamer core ends up in the process.

2. All of it ships in `runtime/install-ng4-fix.sh` and rides `dstorage.dll`, which
the title imports directly and which has nothing to do with video. The two
levers are **on by default in the carrier**; they were environment variables for
a while and a bottle rewrite took them away seven times in one afternoon, each
time looking like a broken codec rather than a lost setting. `"0"` still turns
either of them off for a measurement.

**Answer the VP9 MFT gate in memory**, with `NG4_ANSWER_MFT=1`. Before it
   will play anything the game calls
   `MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, input={Video,VP90})` and counts the
   result. Zero is fatal and immediate: it puts up *"Windows is missing required
   components… The game will now exit"* and exits, without ever opening a file.
   The probe returns one.

3. **Force software decode**, with `BEAST_REFUSE_D3D_MANAGER=1`. Refusing
   `MFCreateDXGIDeviceManager` makes the frame arrive in system memory. Without
   it the run reaches the video and dies inside Metal —
   `gst_video_info_from_caps: assertion 'gst_caps_is_fixed (caps)' failed`
   followed by `MTLTextureDescriptorInternal validateWithDevice: failed
   assertion 'Texture Descriptor Validation'`.

4. **Disable DirectStorage**: rename `dstoragecore.dll` beside the game. This
   was already known. `DStorageGetFactory` returns `DXGI_ERROR_UNSUPPORTED`
   either way and the title takes its other I/O path; it does this identically
   under winevideo, so it is not what winevideo repairs.

## What was measured, and in what order

Each row is one run of the game, same bottle (`Steam25`, a 26.3 bottle), same
probe, changing one thing at a time.

| Run | Engine | Codec staged | Registry | Result |
|---|---|---|---|---|
| reference | 26.3 + winevideo 0.5 | winevideo's own, in its bundle | winevideo's | plays; `SourceReaderFromURL -> S_OK` |
| 1 | **stock 26.3** | none reaching the process | winevideo's | `SourceReaderFromURL -> 0xc00d36bb` |
| 2 | stock 26.3 | libav only, via `cxbottle.conf` | winevideo's | same `0xc00d36bb`; VP9 dialog; game playable |
| 3 | stock 26.3 | libav **+ matroska** | winevideo's | `SourceReaderFromURL -> S_OK`; then a Metal assertion |
| 4 | stock 26.3 | libav + matroska | winevideo's | **plays**, with `BEAST_REFUSE_D3D_MANAGER=1` |
| 5 | stock 26.3 | libav + matroska | **MFT keys removed** | `MFTEnumEx -> 0`; game exits with the "missing components" dialog |
| 6 | stock 26.3 | libav + matroska | MFT removed, `NG4_ANSWER_MFT=1` | **plays** |
| 7 | stock 26.3 | libav + matroska | **nothing of winevideo's** | **plays**, video and all |

Run 1 is the one that overturned the old diagnosis. The previous page claimed the
title "stops after `IMFDXGIDeviceManager::ResetDevice`, before any source
reader". It does not. It calls the source reader and gets a specific error from
it, and every step before that — `MFTEnumEx`, `D3D12CreateDevice`,
`MFCreateDXGIDeviceManager`, `ResetDevice` — returns exactly what it returns
under winevideo.

Run 7 needs a caveat about method, because the first attempt at it was wrong. A
live `wineserver` holds the bottle's registry in memory and writes it back when
it exits, so keys deleted from `system.reg` while it was running reappeared with
their original timestamps, and a run that looked registry-free was not. The
result above is from a run where every wine process was killed, the server was
confirmed gone, the file was edited afterwards, and the state was re-verified
during the run.

## Repeated in a bottle winevideo never touched

Run 7 above left one fair objection standing: `Steam25` had been through
winevideo's installer, so something it left behind might have been doing the
work even with the registry keys deleted.

Repeated in **SteamStable** -- no winevideo markers in its configuration, engine
never patched -- under stock CrossOver 26.3, carrying only `GST_PLUGIN_PATH`,
`NG4_ANSWER_MFT=1` and `BEAST_REFUSE_D3D_MANAGER=1`:

    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered
    MFCreateSourceReaderFromURL(.\Assets/Movies/88f75716-....msd) -> 0x00000000

Plays to the menu with video. The repair is in the mechanism, not in that bottle.

One caveat kept honest: SteamStable's registry does still carry the VP9 MFT keys
and the three handlers, so that run confirms codec and levers, not the
registry-free path.

That was then confirmed separately, in a third bottle. `Steam` carries **zero**
VP9 MFT keys. Launched unprepared it failed exactly as predicted -- `MFTEnumEx ->
0 decoder(s)`, "missing required components", exit. Given only the staged codec
and the two levers, no registry key anywhere, it plays. Run 7 is no longer a
single measurement.

## Preview is a separate, unsolved problem

Written down because five hypotheses were tested and killed here, and that is
worth more to the next person than the one that eventually works.

Preview 27.0.0.40921 stalls before any video call -- `MFCreateSourceReaderFromURL`
is never reached and the staged plugins are never loaded, so this page's fix is
not in play either way. `winedbg` backtraces of ~90 threads show everything
parked: main thread on a C++ condition variable, worker pools idle on shared
addresses, **nothing anywhere in `d3d12`, `dxgi`, `mfplat` or `winegstreamer`**.
The single thread whose wait leaves the process is in `setupapi`, with `plugplay`
blocked serving that RPC.

Killed by measurement: this project's probe (stalls with it removed), HID
enumeration (`DisableInput=1` changes nothing, and `joy.cpl` completes so
plugplay is not wedged), the Steam overlay (per-app DLL override, no change),
the audio driver (`Drivers\Audio=""`, no change), and refusing the MF device
manager (the control has no probe and stalls anyway).

Found on the way, and reportable on its own: **Preview ships no
`winecoreaudio.drv` in any of its three PE architectures** while 26.3 ships it in
both of its two. Preview keeps only the unix `.so` halves, its `mmdevapi.dll`
still resolves backends by name (`pulse,alsa,oss,coreaudio`), and bottles carry a
`Drivers\winecoreaudio.drv` key pointing at a module that is not there. Its role
in this stall is **not established**; the packaging gap is.

Also learned, and costly: macOS `sample` cannot unwind Rosetta-translated x86 --
it returns one repeated `ntdll.so` address and once attached to
`steamwebhelper.exe` while reporting the game's name. `winedbg --command "attach
0x<wine-pid>; bt all"` is the tool that works.

## What winevideo actually does for this title

Of the 17 files winevideo 0.5 replaces or adds, `lsof` on the live process shows
NG4 loads six: `ntdll.dll`, `mfplat.dll`, `winegstreamer.dll` (+ its `.so`), and
the plugins `libgstvpx.dylib`, `libgstmatroska.dylib`, `libvpx.9.dylib`.
`d3d9.dll`, `qasf.dll`, `quartz.dll` and `winevideo_compat.dll` are replaced and
never loaded. Replacing a file is not the same as executing it.

Of the six that do load, only the plugins supply a capability stock CrossOver
lacks, and of those only the Matroska demuxer is not substitutable. Their
`ntdll` is behaviourally inert here: its added export is gated on a 17-row table
of executable names that does not list this game, and the compat DLL that gate
would load is never mapped. It is replaced because their `winegstreamer` imports
a symbol from it, not because NG4 needs it.

Their registry additions do matter under **their** engine — the VP9 MFT CLSID
they register is wired into their `winegstreamer`'s class factory and not into
stock's, where the same GUID sits in `.rdata` with no references at all, as
linker ballast. Reading that GUID's presence in the stock binary as "the class
exists in stock" was a mistake made and corrected during this work; the control
that settles it is that stock keeps the whole *"GStreamer doesn't support X
decoding"* family of messages — AAC, H.264, MP3, WMA, WMV — and is missing
exactly one member, VP9.

None of that obliges us to ship a `.reg`. The MFT gate is a count the game
takes once, and answering it in memory is equivalent for that gate; run 7 shows
the byte-stream side needs nothing either, because Wine resolves these files by
content and the `.msd` extension never had to be registered.

## What the old page got wrong, and why

Recorded because the shape of the error is more useful than the error.

- **"It stalls before any source reader."** It does not, and it never did on
  26.3. That claim came from Preview runs and was generalised to a question it
  could not answer.
- **"What remains is inside the binaries winevideo replaces, acting before any
  video call exists."** The remaining difference was a GStreamer plugin — a file
  sitting beside those binaries, not behaviour inside them.
- **"Isolating which one would mean patching CrossOver: the line this project
  does not cross."** The line was never in the way. The question was answerable
  from outside, by adding one plugin to a directory this project already
  maintains.

The common thread: the earlier work compared *what winevideo replaces* against
what stock has, and reasoned about the replaced binaries. What settled it was
asking what the process actually **loads**, and then what capability is actually
**missing** — which turned out to be one demuxer, in a list of demuxers.
