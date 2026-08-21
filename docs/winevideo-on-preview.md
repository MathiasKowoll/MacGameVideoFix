# What winevideo is still for, on CrossOver Preview

Measured on an M4 Max against CrossOver Preview 20260821 (27.0.0.40921).
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
VP9/WebM, are dead weight on Preview.

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
machinery -- `fix_so`, `patch_macho_compat.py`, the SHA-256 gate,
`codesign --force`, and the sibling backup tree.

**Untested caveat:** this was verified against the **aarch64** core. A
`WineArch=win64` bottle selects the x86_64 host, so a real Steam bottle needs
the same staging built against `lib/x86_64`. The framework plugins are
universal binaries, so it should transfer -- but should is not measured.

Also honest about its own limits: no WMV3 or VC-1 bitstream could be encoded
for the test (ffmpeg ships no encoder for either), so WMV2 stood in. Decoding
WMV3 and VC-1 on Preview is **[READ]**-grade, not **[RUN]**-grade.

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

Anti-tamper protected titles. Ninja Gaiden 4's executable is encrypted on disk
-- two `.text` sections, entropy 8.00/8.00 -- so nothing that patches a running
process can reach it. winevideo works there because it patches Wine, outside
the game. That approach and ours are not substitutes, and the line between them
is not the codec.
