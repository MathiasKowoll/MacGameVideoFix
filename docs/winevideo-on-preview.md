# What winevideo is still for, on CrossOver Preview

Measured on an M4 Max against `crossover-preview-arm64-20260821`
(CFBundleVersion 27.0.0.40921).
Claims are marked **[RUN]** where something was executed and **[READ]** where
only a binary was inspected, because the difference decided several of these.

## The short version

Preview already decodes most of what winevideo was installed for. Its codec
payload is largely redundant there, and the part that is not redundant does not
need CrossOver patched at all.

| Codec | Preview alone | How |
| --- | --- | --- |
| VP9 profile 0 and 2 | **yes** [RUN] | `vp9parse` → `vtdec_hw` |
| VP9 profile 1 (4:4:4) | no [RUN] | vtdec's caps template is `profile={0,2}` |
| H.264 | **yes** [RUN] | |
| AAC | **yes** [RUN] | |
| WMV3 / VC-1 / WMA | **no** [RUN] | hard error, no decoder registered |

VP9 decoding works but is **not** hardware accelerated:
`VTIsHardwareDecodeSupported('vp09')` answers no, while
`VTDecompressionSessionCreate` accepts the stream anyway. An earlier reading of
this as hardware decode was wrong, and it came from finding
`gst_vtdec_check_vp9_support` in the binary rather than from running anything.

So `libgstvpx` and `libgstmatroska`, the two plugins winevideo drops in for
VP9/WebM, are dead weight **on Preview**. On stable 26.3 the second of them is
the whole dependency: that build ships no `matroska` plugin, so a `.webm` never
opens there whatever can decode what is inside it. That is the finding this
page's framing predates — see
[the container, not the codec](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#the-container-not-the-codec).

## Importing the codecs that are missing

The official GStreamer.framework has `libgstlibav`, which covers WMV3, VC-1 and
WMA. Loading it **in place** into Preview's core crashes: dyld ends up with two
copies of libgstreamer and two GObject type registries, and Preview ships no
`gst-plugin-scanner`, so there is no fork to isolate it. [RUN]

    objc: Class GstCocoaApplicationDelegate is implemented in both
    <Preview>/libgstreamer-1.0.0.dylib and /Library/Frameworks/...

Re-homed into its own directory with symlinks to Preview's own libraries, the
same plugin loads and decodes. [RUN] The framework's plugins carry
`@loader_path/..` rpaths, so a staged directory resolves itself wherever it
sits.

And it needs no patching of the app bundle. Preview's `bin/wine` sets only
`GST_PLUGIN_SYSTEM_PATH` and `GST_REGISTRY`; it never touches
`GST_PLUGIN_PATH`, and the bottle's environment is applied first, so a
`cxbottle.conf` entry survives:

    GST_PLUGIN_PATH = /path/to/staged/gstreamer-1.0

That is one folder and one config line, in place of winevideo's app-side
machinery — `fix_so`, `patch_macho_compat.py`, the SHA-256 gate,
`codesign --force`, and the sibling backup tree.

**That caveat has since been closed.** The staging above was first verified
against the **aarch64** core, while a `WineArch=win64` bottle selects the
x86_64 host — so whether it transferred was an open question. It does:
`runtime/stage-codecs.sh x86_64` is what the shipping Persona 5 Strikers fix
uses, and the game plays on Preview through it. [RUN]

That closes the VC-1 half of the codec question too. VC-1 through the staged
`libgstlibav` is **[RUN]**-grade now, measured by the game playing rather than
by a bitstream test. WMV3 and WMA remain **[READ]**-grade and untried, and the
reason is worth keeping: ffmpeg ships no encoder for either, so no test
bitstream could be made and WMV2 stood in.

## A missing codec is loud, not black

Worth knowing, because it rules out a whole class of misdiagnosis. Every
missing-codec case produced a bus error naming the codec: [RUN]

    no suitable plugins found: Missing decoder: VP9 (video/x-vp9, profile=1)
    Missing decoder: Windows Media Video 8 (video/x-wmv, wmvversion=2)

`decodebin` refuses to expose a pad it cannot fill, so source creation fails
and Media Foundation returns a failing HRESULT. Registering `.webm` with no
decoder behind it gives a failed open, never a picture decoded to black.

**So a black picture with working sound is never a missing codec.** It is the
game ignoring a failed HRESULT, or something after the decoder.

The genuinely silent failure is on the MFT side and runs the other way:
`MFTEnumEx` is a registry read that loads no DLL, so a bogus entry makes a
capability probe pass and only fails later at `CoCreateInstance`.

## What this does not change

Ninja Gaiden 4, and the reason is a codec after all. An earlier version of this
section said that title was out of reach because its executable is encrypted on
disk — two `.text` sections, entropy 8.00/8.00 — and that was wrong about what
this project needs. A proxy DLL beside the game and a hook in a COM vtable
write nothing into the protected image.

Where it actually stops is a Media Foundation gate.
`MFVideoFormat_VP90` does not appear anywhere inside Preview's
`winegstreamer.so`, so there is no VP9 decoder MFT to register and the game's
`MFTEnumEx` is answered with zero. winevideo reaches it by adding that MFT
inside winegstreamer, which is a change to the engine and so a change this
project cannot make. The full account, including a DirectStorage detour that
had nothing to do with video, is in
[docs/ninja-gaiden-4.md](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/docs/ninja-gaiden-4.md),
which is a repository file and is not in any release.

Anti-tamper is a real limit, but a narrower one: it defeats the fixes that
write into a game's own code, as Beast of Reincarnation's did. It does not
defeat a carrier DLL. That is the line between the two approaches, and it is
not where this page used to draw it.
