# mgvf-asf-reader.dll — FFmpeg inside

`mgvf-asf-reader.dll` is installed beside the games whose fix needs it
(Persona 5 Strikers, the RE Engine titles, NieR Replicant). It is a Media Foundation source reader written for
this project (`asf-reader.c`, GPL-3.0-or-later) and statically linked against
FFmpeg, which is other people's work. This notice travels with the binary
because the LGPL asks for it.

## FFmpeg

- Upstream: <https://ffmpeg.org/>
- Release: **9.0.2**, `ffmpeg-9.0.2.tar.xz`
- Archive sha256:
  `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`
- Licence: **LGPL-2.1-or-later**. No GPL or nonfree component is enabled.
- Configured by `build-asf-reader.sh` with `--disable-everything`, then only
  the `asf` demuxer, the `vc1` parser and the `vc1`, `wmv3`, `wmv2`, `wmv1`,
  `wmapro` and `wmav2` decoders; no assembly, no autodetected libraries.

The source for that exact configuration is the release tarball above plus
`build-asf-reader.sh`, which rebuilds the DLL from it. Relinking against a
different FFmpeg is the same script with `FFVER` and `FFSHA` changed.

## The binary, as shipped here

- `mgvf-asf-reader.dll` — `f4227b1551946a8cfb681021cba08cba5252dd429facd212ee5432ab13d0973b`
