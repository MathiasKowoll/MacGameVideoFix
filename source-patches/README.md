# Engine source patches

`scripts/build-winegstreamer.sh` builds `winegstreamer.dll` and `winegstreamer.so`
from a CrossOver source tree with a chosen set of patches applied, and the pair
is installed into the engine. This file records **which patches, whose they are,
and why each one is applied**, because until now that was only visible as four
numbers on a command line.

    scripts/build-winegstreamer.sh --patches 0002 0003 0006 0008 mgvf-0001

`runtime/engine-built-for.json` records the set that produced the binaries
currently shipped in `runtime/engine-payload/`.

## Whose patches these are

**0002, 0003, 0006 and 0008 are winevideo's**, from its series of 35. They are
**carried here, unchanged, with their provenance and their licence**, so that
building this engine does not require their application to be installed. Each
file opens with a header saying it is not ours. They modify Wine, which is
LGPL-2.1-or-later, and a patch to LGPL code carries the same terms -- which is
what makes carrying them permitted. Take them from winevideo rather than from
us if you want them: theirs is where they are maintained, and these are a
snapshot that will go stale.

The summaries below are ours, written from their commit messages; the messages
themselves are the authority.

**`mgvf-0001` is ours**, and lives here in full. It opens our own series, kept
deliberately apart from winevideo's numbering: a patch of ours numbered `0036`
read as the next one of theirs, which is how the first question anyone asked
about it was where to find it in their repository. Ours are `mgvf-NNNN` and
theirs are `NNNN`, and the two can never be confused again. See
`mgvf-0001-winegstreamer-2D-capable-media-source-samples.patch`.

## What each one is for

### 0002 — VP9/AV1 caps mapping, decoder input types
Touches `video_decoder.c` and `wg_media_type.c`. Groundwork for the VP9 decoder
that 0003 then advertises: without it the caps mapping has nowhere to land.
0003 does not apply without it.

### 0003 — a real VP9 decoder MFT, advertised through MFTEnumEx
Adds `CLSID_wg_vp9_decoder`, mirroring the h264 MFT, and wires it into
`mfplat.c`'s class objects. Its message names the symptom it exists for: a game's
VP9 capability probe finds no decoder and shows *"Failed to Play VP9"*. That is
NINJA GAIDEN 4's fault in this project.

### 0006 — drop D3D awareness on macOS
No macOS backend can create NV12 D3D11 video textures, so D3D-bound decoder
output can never be displayed. CrossOver's own workaround hides NV12 from
`GetOutputAvailableType` instead, which starves consumers that drive an
`IMFTransform` directly and require NV12: they enumerate output types, find none,
and abandon playback. That is the black-cutscene-with-audio shape, and it is why
UE Electra titles here need this patch.

### 0008 — always provide 2D-capable output samples on macOS
UE Electra queries a decoded frame's media buffer for `IMF2DBuffer` and rejects
the frame when it is absent — which it always was, because without a D3D manager
the decoder filled a caller-provided plain memory buffer. Sets
`MFT_OUTPUT_STREAM_PROVIDES_SAMPLES` unconditionally on macOS and routes output
through the sample allocator, whose system-memory buffers do implement it.

### mgvf-0001 — the same, for the media source *(ours)*
0008 covers the decoder **MFT**. A title that resolves its own byte stream never
reaches that MFT: winegstreamer's **media source** produces its own samples, and
those were plain memory buffers. METAL GEAR SOLID: Peace Walker asks such a frame
for `IMF2DBuffer2`, gets `E_NOINTERFACE`, does not check the HRESULT, and
dereferences the NULL. Full evidence is in the patch's own header.

## If another of their patches is ever needed

The remaining 31 are not applied here, and several address titles this project
also carries. Adding one means: name it in `--patches`, record the new set in
`runtime/engine-built-for.json`, rebuild the payload, and **add a section above
saying what it is for and which title needs it**. A patch that is applied but not
described here is indistinguishable from one applied by accident.

They also do not all apply standalone. 0003 needs 0002, and 0008 needs its
predecessors; testing one against a pristine tree reports a failure that says
nothing about the patch. Apply them in ascending order, as the build does.

## Credit

The winevideo project is at <https://github.com/Jfishin/winevideo>. The four
patches above are its work and its findings; this project applies them and says
so. Nothing here should be read as those patches originating in MacGameVideoFix.
