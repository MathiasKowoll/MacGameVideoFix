# Persona 5 Strikers

Koei Tecmo's in-house engine. The video never starts — sound only, no picture,
and no error anywhere.

| | |
| --- | --- |
| Video | VC-1 in ASF, measured by header scan of all five archives |
| Played by | `IMFSourceReader` on Media Foundation, presented through D3D9 |
| Symptom | Sound plays, the picture never appears |
| Fix | Stage a VC-1 decoder, then bridge D3D9 to D3D11 |
| Backend | **DXMT only.** D3DMetal cannot produce a shared handle at all |

## Not VP9

The obvious guess, and wrong. All five `data/pd/movie*.bin` archives carry ASF
headers with VC-1 inside — measured in the first 64 MB of each, where the
headers live, and consistent across every one.

That matters because CrossOver Preview decodes VP9, H.264 and AAC on its own,
and has **no VC-1 decoder at all**. This is the only game here that genuinely
needs a codec CrossOver does not ship.

## Staging the codec, without patching CrossOver

The official GStreamer.framework has VC-1 in `libgstlibav` (ffmpeg). Install
the macOS **runtime** package from
[gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/data/pkg/osx/1.24.13/)
— winevideo specifies **1.24.13** for exactly these titles, and **1.24.14** is
what is measured working here, so the constraint in practice is the 1.24 series
rather than the exact patch. Nothing is redistributed: the decoder is borrowed
from an install you already have, which is also how winevideo does it. Loading
that plugin in place crashes: dyld ends up with two copies of libgstreamer and
two GObject type registries, and Preview ships no `gst-plugin-scanner`, so
there is no forked scanner to absorb it.

```
objc: Class GstCocoaApplicationDelegate is implemented in both
      <Preview>/libgstreamer-1.0.0.dylib and /Library/Frameworks/…
```

Re-homed into a directory of its own, with ffmpeg beside it and the GStreamer
core symlinked to **CrossOver's** copy, it loads and registers. One folder, and
one line in the bottle:

```
GST_PLUGIN_PATH = …/gst-codecs/x86_64/gstreamer-1.0
```

Preview's launcher sets only `GST_PLUGIN_SYSTEM_PATH` and never touches
`GST_PLUGIN_PATH`, and the bottle's environment is applied first, so it
survives. `runtime/stage-codecs.sh` builds it.

Layout matters and cost a first attempt: `GST_PLUGIN_PATH` names a directory
GStreamer scans and tries to load everything in as a plugin, so the support
libraries sit one level out where the plugin's own `@loader_path/../lib` finds
them and the scanner never looks.

## Then the frame has nowhere to go

With the codec in, the reader opened the ASF, agreed types on video and audio —
and never called `ReadSample`. Not once, across 98 readers. This is why:

```
CreateRenderTarget(1920x1080 fmt=22, SHARED requested)
    -> 0x00000000, handle 0000000000000000
```

Wine's D3D9 does not fail that call. It returns `S_OK` and hands back a **null
handle** — it warns and carries on. The game believes it succeeded and gives up
quietly later, which is worse than failing, because there is nothing to find.

Five things had to line up, and each was measured rather than reasoned:

**A genuine shared handle.** From a DXMT D3D11 texture. DXMT implements sharing
where Wine does not — `GetSharedHandle` appears 17 times in its `d3d11.dll` and
not once in Wine's — and that is why this game is only fixable on that backend.
Handing one back is what makes the game start reading.

**The texture the game actually reads.** It opens the handle on its *own*
device, and writes to a texture on ours are not visible there. A hundred
converted frames went into something nobody was looking at while the screen
stayed magenta. So `OpenSharedResource` is intercepted and answered with a
texture created on the game's device.

**That texture has to look shared.** Bindable as a render target as well as a
shader resource, and carrying `MISC_SHARED`. Without it the run ended early,
somewhere far from the cause.

**`ID3D11Multithread`, which DXMT ships disabled.** Turning it on is what makes
writing from the video thread to the renderer's device legal, with ordinary
D3D11 ordering against the engine's reads.

**NV12 to BGRA**, from the surface the game blits *from*. The destination cannot
be read back — `GetRenderTargetData` answers `D3DERR_INVALIDCALL` — and the
source is NV12, which is why raw bytes produced noise.

## Caveats

- **DXMT, not D3DMetal.** D3DMetal has no shared-handle support to build on.
- **Do not use this on a game with anti-cheat or anti-tamper.** It patches a
  running process.
- Steam's *verify integrity of game files* undoes the install.

## What it cost, and the two measurements that paid for it

The screen was black, then magenta, then black again, and those look identical
from the sofa while meaning opposite things. Two measurements separated them.

**Averaging the luma plane** said whether the source had a picture at all:
16,16 then 15..50 — two frames of pure black and then something dark, which is
a fade in, not an empty surface. The frame was there.

**Writing solid magenta** said whether our writes landed. It filled the screen,
which proved in one run that the whole bridge worked and the fault was in the
content — the cheaper half.

And the content fault was as small as they come. `nv12_to_bgra` indexes a
saturation table instead of branching per channel, and the table is filled by
`build_clamp_table()`. The converter came across from the
[DYNASTY WARRIORS bridge](Dynasty-Warriors-Origins.md); the call did not. With
the table all zeroes, every pixel resolved to zero and perfectly good input
produced a uniformly black frame. Magenta worked because it never went through
the converter.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md)
