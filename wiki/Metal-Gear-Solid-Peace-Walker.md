# METAL GEAR SOLID: Peace Walker

Master Collection Version. The game starts, the tutorial plays, and it dies the
moment a pre-rendered cutscene begins.

| | |
| --- | --- |
| Video | `.xmx` files under `mgspw/MLG/data/Mov/` and `hqMov/`, decrypted in memory and handed to Media Foundation as a byte stream |
| Played by | winegstreamer's **media source**, reached through the source resolver |
| Symptom | Access violation reading address 0, in the game's own code. No message, no dialog |
| Fix | Engine patch `mgvf-0001`: the media source hands out 2D-capable buffers |
| Installed in the game folder | Nothing. The fix is entirely in the engine |
| CrossOver | Stable 26.3, and only on a copy of it this project patched |
| Engine | **This project's own `winegstreamer`**, carrying patch `mgvf-0001`. Stock CrossOver cannot play this title's cutscenes — measured. winevideo's `0008` does not reach this path either, which is read from the patch and not from a run |
| winevideo | Its patch `0008` is the same idea for a path this title never takes — see below. The engine here carries the whole set (`0002 0003 0006 0008 mgvf-0001`) and `mgvf-0001` alone was never tried, so whether the others matter to this title is not established |

## Not every cutscene is a video

The first scene of the game, before the tutorial, plays without touching Media
Foundation at all: `MFStartup` is never called, `winegstreamer` is never loaded,
and the probe records no file reads for thirty seconds while it is on screen.
That scene is drawn by the game's own engine.

This matters because it separates two things that look identical to a player.
"It crashes on a cutscene" was true, and testing the *first* cutscene would have
said the game was fine. The fault only appears at a scene backed by an `.xmx`.

## What the crash actually was

The probe caught it, and the registers say the whole thing:

    EXCEPTION 0xc0000005 at 0x1400609e7   movq (%rcx), %rax
    rcx = 0            the pointer being dereferenced
    rax = 0x80004002   E_NOINTERFACE, the HRESULT the last call returned
    rdx = 0x140dacf70  -> into the game's own .rdata
    r8  = 0x4ecfd78    a stack address

Those are the arguments of `QueryInterface(this, riid, ppv)` in the x64
convention, still in place after the call. The sixteen bytes at `0x140dacf70`
are `{33AE5EA6-4316-436F-8DDD-D73D22F829EC}`, which mingw's `mfobjects.h` names
`IID_IMF2DBuffer2`. So:

```c
hr = buffer->QueryInterface(IID_IMF2DBuffer2, &p2d);  /* E_NOINTERFACE, p2d = NULL */
p2d->Lock2DSize(...);                                  /* loads the vtable from NULL */
```

The game asks a decoded frame for a 2-D view, is told the interface does not
exist, does not look at the HRESULT, and dereferences the NULL it was handed.

**Wine's side.** `dlls/mfplat/buffer.c:127`, the plain 1-D memory buffer, answers
only `IID_IMFMediaBuffer` and `IID_IUnknown`; everything else gets `*out = NULL;
return E_NOINTERFACE`. The 1D/2D buffer at line 262 would have answered.

## The patch that already existed, and the title it could not reach

winevideo's `0008` — *"winegstreamer: always provide 2D-capable output samples on
macOS"* — is this exact fault, found through UE Electra. Its comment says so:

> the sample allocator's system-memory buffers implement IMF2DBuffer, which
> consumers like UE Electra require on video frames (a caller-provided plain
> memory buffer does not, so they reject every frame)

But `0008` changes `video_decoder.c` — the decoder **MFT**. A title whose video
arrives through winegstreamer's **media source** never goes near it: the media
source produces its own samples in `media_stream_send_sample`, which called
`MFCreateMemoryBuffer` and so handed out exactly the buffer `0008` exists to
avoid.

`mgvf-0001` is the same idea one file over. For video streams the buffer now
comes from `MFCreateMediaBufferFromMediaType`, which returns a 2D-capable buffer
when the media type carries a subtype and a frame size, and falls back to a 1-D
buffer of the requested length when it does not — the previous behaviour
exactly. Audio is untouched.

Two guards ride along. If the locked linear view turns out smaller than what the
parser produced, that frame goes back on a plain buffer rather than overrunning
the allocation. If it is larger, the tail would never be written, so it is filled
with black — luma 0, chroma 128 — because a fresh allocation is not blank and
zeros are bright green in YUV.

## Why a registry switch could not help

`mfsrcsnk/media_source.c:2098` routes MP4 to winegstreamer's byte-stream handler
when winedmo cannot demux it **or** when `use_gst_byte_stream_handler()` is true,
and that returns true when
`HKCU\Software\Wine\MediaFoundation\DisableGstByteStreamHandler` is absent.
Setting it to 1 was tried. The game crashed at the same address and
`winegstreamer` loaded anyway.

The reason is structural: that switch guards only the handlers `mfsrcsnk`
registers per extension and per MIME type. This game decrypts its `.xmx` in
memory and passes it through `MFCreateMFByteStreamOnStream`, so there is no
extension and no MIME type, and `mfplat/main.c:6363` calls
`resolver_create_gstreamer_handler()` directly. Wine's own comment above it calls
this a *"wine specific fallback to predefined handler"*. **Do not retry this
route for any title that resolves from a raw byte stream.**

## A green band, and what it is not

With `mgvf-0001` the crash is gone and cutscenes play — measured: about eleven
minutes of play, two separate Media Foundation sessions, two different `.xmx`
files, zero exceptions. A green band appears along the bottom of the frame in
**some** cutscenes, not all.

**It is not this patch, and it is not the buffer it hands over.** Measured from
inside the process on the first sample of a cutscene:

    stream asked for: 'I420'
    frame geometry: 2D yes | current 3110400 | max 3110400 | contiguous 3110400

3,110,400 bytes is exactly 1920x1080 in I420, and the three lengths are equal:
there is no unwritten tail. The plain buffer this patch replaced was allocated at
exactly the same size, so the bytes the game receives are the same either way —
only the buffer's type changed. A fault in the copy would also appear in every
cutscene, since nothing in the path distinguishes one from another.

The tail fill was tried before any of those numbers were read, and the band
survived it — which is what sent somebody to look at the buffer's own numbers in
the first place. So the patch is excluded, and so is the buffer it hands over.
Nothing else is.

**What the band is remains unexplained.** What the measurement says is only that
it is already in the frame when it arrives. Nobody had seen a frame of this title
before, because it crashed on the first one. Two candidates have been raised and
neither is established: subtitles drawn over the picture, which the player
suggested, and the game's own comic styling, which uses deliberate colour
fringing elsewhere and makes a coloured edge hard to call corrupt by eye.

The band was written down as ours, introduced by this patch, twice — here and in
the patch's own message — and retracted both times after measuring. It was
written from a mechanism that sounded right: an uninitialised tail, zeros reading
as green in YUV. The measurement above says the tail does not exist. The patch
still fills a tail when one occurs, which it does not here; that guard is
defensive, not the fix for this.

**To settle it:** read the last rows of the frame out of the buffer. If they are
zero there, the band is in what the decoder produced and the fix is upstream; if
they carry ordinary picture, it is added when the game draws.

## How it was found

This is how the fault was found, not what ships. Nothing below is installed for
this title: the fix is the engine patch, and the game folder is left as it was.

The probe is `diagnostics/mf-observe.c`, built onto `d3dcompiler_47.dll` as its
carrier. That choice was not free: of the three DLLs the game imports and Wine
supplies, `winmm` and `xaudio2_9` are Wine builtins and loading a builtin as
native from the game folder is not safe, while `d3dcompiler_47` in this bottle is
a genuine Microsoft redistributable. Its 29 exports are forwarded by the loader,
so no code of ours runs in the call path. The override is per-executable.

**The probe had to be corrected once.** Its module list is polled every five
seconds, so a "now loaded" line means "seen at this tick", not "loaded now". In
two runs the first exception and the `winegstreamer` load fell inside the same
tick, and the order between them decides whether the faulting buffer could have
come from the media source at all. Reading an order out of that would have been
reading the instrument rather than the game. The handler now dumps the module set
at the instant of the fault, which settled it:

    [112584 ms] EXCEPTION 0xc0000005 at 00000001400609E7
    [112584 ms]     media modules at this fault: mfplat.dll mfreadwrite.dll
                    winegstreamer.dll msdmo.dll mfmp4srcsnk.dll d3d11.dll

The periodic poll never got there: its last tick was seven seconds earlier.

## Caveats

- The executable cannot be disassembled statically. `.text` has an entropy of
  8.00 and there is a `.bind` section — Steam DRM decrypts it at run time. The
  IID is readable because `.rdata` is not encrypted, and it appears exactly once.
- The fix is in the engine, so it reaches every bottle that uses that engine, not
  only the one this game runs in.
- The game survives the first access violation and dies on the second. Whatever
  absorbs the first one was never identified.
