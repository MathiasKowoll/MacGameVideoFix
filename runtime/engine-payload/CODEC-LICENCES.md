# The codecs in this payload, and their licences

Twelve files, 21,632,848 bytes, copied verbatim from
**winevideo** 0.5 (https://github.com/Jfishin/winevideo) on 2026-08-29. Not
built here and not modified:
byte-identical to that build, which is what the hashes below are for.

> **Another project verifies these by hash before it will build an engine.**
> RaccoonBot stopped carrying its own copy of the twelve as of its 0.2.0: it
> reads them out of this app's Resources and checks all twelve against a sha256
> table of its own before starting `make-engine-copy.sh`. So rebuilding these
> binaries without that table moving in the same release stops their engine build
> with a mismatch, and the failure appears on their side for a change made here.
>
> Told to us by the RaccoonBot session on 2026-09-01, unprompted, as a coupling
> they created and we had not agreed to. Recorded here rather than in a message,
> because the person who rebuilds these will read this file and not that message.

**Why they are here.** Stock CrossOver 26.3 ships 17 GStreamer plugins and
none of them decodes VC-1, WMV3 or VP9. Seven of the titles in this project need
one that it does not ship. The two routes that existed took them from a
GStreamer the user had to install themselves — which fails silently when it is
absent and worse when it is a different 1.24.x — so they travel here instead,
pinned by hash.

**They are not ours.** The list is the transitive closure of what the three
plugins need and stock CrossOver does not already have, walked with `otool -L`
rather than guessed, so a copy of this tree leaves no dangling `@rpath`.

| file | bytes | sha256 |
| --- | --- | --- |
| `lib64/gstreamer-1.0/libgstlibav.dylib` | 267,696 | `b748843c176a4715d111036674cf6859d8f43fc6b4e98a3abaa5750d57233ac9` |
| `lib64/gstreamer-1.0/libgstmatroska.dylib` | 366,768 | `9e7d08da9252f30113732981c214323faa13f12648cf3a6bbb48ee88bce0c1b2` |
| `lib64/gstreamer-1.0/libgstvpx.dylib` | 110,416 | `2afef0cee64b0bd606660aaf2294dae7d68049e235814fa99dbd3fd1f1b7c14c` |
| `lib64/libavcodec.60.dylib` | 13,607,312 | `ea7e2f3022e14d5c1d4787c00f626f5185a15b76ac88ae0f5e74a297608e2601` |
| `lib64/libavfilter.9.dylib` | 153,664 | `ce46cb51430efb4152703e7b0b80361e6caaa90b746e5a7c1e49de3f7e901b6e` |
| `lib64/libavformat.60.dylib` | 1,995,760 | `e44d159a8b96360112bd5fd4c62f7a7d470b884b16452fc7b884330d2a5a62b3` |
| `lib64/libavutil.58.dylib` | 750,560 | `fac49f53ec9a7cdaee223d30ecf9038c372a3c88b3e62276c428cfdf7488bf33` |
| `lib64/libbz2.1.dylib` | 82,144 | `70e19fe6eb3cb98d24369de344622a4228cadb4dd6fc6888dcb404b14568685d` |
| `lib64/liborc-0.4.0.dylib` | 863,328 | `b79f70b7bcdf71fdb2b1a5b2155d3efc7766a6ef0f9b451437be9d2d0b363064` |
| `lib64/libswresample.4.dylib` | 172,000 | `6e705fe847c804dab38a1f408aae70c369ea7678b8ef568b4583d414e10ffad7` |
| `lib64/libvpx.9.dylib` | 3,160,624 | `516301130035afb711a427b4f4d9e53b9b7a16a34b0440ea0f0921d0b2d20b02` |
| `lib64/libz.1.dylib` | 102,576 | `ed695ed72de58ce69632c86b40dacb8e2bf61db469efccc2e99da62b5825c8fa` |

## Licences

- **GStreamer** (`libgstlibav`, `libgstmatroska`, `libgstvpx`, `liborc`) — LGPL
  v2.1 or later. https://gstreamer.freedesktop.org/
- **FFmpeg** (`libavcodec`, `libavformat`, `libavfilter`, `libavutil`,
  `libswresample`) — LGPL v2.1 or later as built here. https://ffmpeg.org/
- **libvpx** — BSD 3-clause, Google. https://chromium.googlesource.com/webm/libvpx
- **zlib** (`libz`) — zlib licence. **bzip2** (`libbz2`) — BSD-like.

The LGPL components are dynamically linked and replaceable: each is a separate
`.dylib` resolved through `@rpath`, so a recipient can substitute their own
build by replacing the file. That is the mechanism the licence asks for and it
is satisfied by how these are shipped, not by a promise.

**Corresponding source.** These are unmodified upstream builds redistributed
from winevideo's CrossOver. Sources for the exact versions are the upstream
projects above; anyone wanting the corresponding source for a build in this
tree should ask through the repository's issues and it will be provided.

**If you change a file here, change its hash.** `check-builds.sh` verifies every
one of these against this table, for the same reason the NINJA GAIDEN 3 DLLs are
verified: a copy taken from a build can outlive the build it came from, and this
engine already proved it — three of these files sat in it for days while nobody
could say where they came from.
