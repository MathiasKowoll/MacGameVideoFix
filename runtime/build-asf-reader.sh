#!/usr/bin/env bash
# Build mgvf-asf-reader.dll: asf-reader.c (a Media Foundation source reader) and
# wmv-dmo.c (a DirectShow WMV decoder) over a minimal, static FFmpeg.
#
#   runtime/build-asf-reader.sh            build into runtime/
#
# FFmpeg is configured with nothing but the ASF demuxer, the VC-1 parser and
# the VC-1, WMV3, WMV2, WMV1, WMA Pro and WMA v2 decoders. No assembly (nasm is not needed),
# no programs, no network, no autodetected libraries: LGPL 2.1 or later. The
# tarball is fetched once into ~/Development/sources/ffmpeg and checked against
# the sha256 below before anything is built from it.
#
# Needs llvm-mingw; set MINGW_BIN if it is not in the default place.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FFVER=9.0.2
FFSHA=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
SRC="${FFMPEG_SRC:-$HOME/Development/sources/ffmpeg}"
MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
export PATH="$MINGW_BIN:$PATH"
CC=x86_64-w64-mingw32-clang
command -v "$CC" >/dev/null || { echo "error: $CC not found; set MINGW_BIN" >&2; exit 1; }

mkdir -p "$SRC"
TAR="$SRC/ffmpeg-$FFVER.tar.xz"
[ -f "$TAR" ] || curl -fsSL -o "$TAR" "https://ffmpeg.org/releases/ffmpeg-$FFVER.tar.xz"
echo "$FFSHA  $TAR" | shasum -a 256 -c - >/dev/null || { echo "error: $TAR does not match its sha256" >&2; exit 1; }

# The configuration is part of the prefix's name, so a change to the decoder
# list builds afresh instead of linking a stale library.
FFCODECS="vc1,wmv3,wmv2,wmv1,wmapro,wmav2"
PREFIX="$SRC/win64-$FFVER-$(printf '%s' "$FFCODECS" | shasum | cut -c1-8)"
if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
  BUILD="$SRC/build-$FFVER"
  rm -rf "$BUILD"; mkdir -p "$BUILD"
  tar xf "$TAR" -C "$BUILD"
  ( cd "$BUILD/ffmpeg-$FFVER"
    ./configure --prefix="$PREFIX" --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
      --cross-prefix=x86_64-w64-mingw32- --cc="$CC" \
      --disable-everything --disable-programs --disable-doc --disable-network \
      --disable-autodetect --disable-x86asm --disable-debug \
      --enable-static --disable-shared --enable-w32threads \
      --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
      --enable-decoder="$FFCODECS" --enable-demuxer=asf --enable-parser=vc1 >/dev/null
    make -j"$(sysctl -n hw.ncpu)" >/dev/null
    make install >/dev/null )
fi

"$CC" -shared -O2 -Wall -I"$PREFIX/include" -o "$HERE/mgvf-asf-reader.dll" "$HERE/asf-reader.c" "$HERE/wmv-dmo.c" "$HERE/wm-reader.c" "$HERE/dshow-graph.c" \
  "$PREFIX/lib/libavformat.a" "$PREFIX/lib/libavcodec.a" "$PREFIX/lib/libavutil.a" \
  -lmfplat -lmfuuid -lole32 -lstrmiids -luuid -lbcrypt -Wl,--no-insert-timestamp -s
echo "built $HERE/mgvf-asf-reader.dll"
shasum -a 256 "$HERE/mgvf-asf-reader.dll"
