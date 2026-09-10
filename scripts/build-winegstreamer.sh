#!/usr/bin/env bash
#
# Build winegstreamer from the CrossOver sources on this machine.
#
#     scripts/build-winegstreamer.sh [--patches 0002 0006 ...]
#
# Everything below was found by doing it, and three of the steps are not
# guessable from any documentation:
#
#   - macOS ships bison 2.3 and GStreamer wants 2.4, so Homebrew's goes first
#     on PATH. Without it meson stops before generating the enum headers.
#   - declaring --host puts autoconf in cross mode and it loses the SDK, which
#     surfaces as "C compiler cannot create executables" while the same command
#     works by hand. SDKROOT has to be explicit.
#   - and the one that cost a launch: the engine's dylibs carry an install_name
#     of /opt/cxoffice/lib64/..., CodeWeavers' build prefix, which exists on no
#     Mac. Link against them and the linker copies that path into the result,
#     which then cannot load its own dependencies. The stock binary uses @rpath
#     and a second rpath three levels up; both have to be put back afterwards.
#
# The build is pinned to the glib API the engine ships rather than the newest
# one on the machine: compiled against Homebrew's 2.88 headers, two symbols that
# only exist from 2.80 went missing at link time.
#
# Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -u

SOURCES="${MGVF_WINE_SOURCES:-$HOME/Development/sources}"
WINE="$SOURCES/wine"
GST="$SOURCES/gstreamer/subprojects"
# Not under "Application Support": Wine's build system does not survive a space
# in its path, and the failure reads as clang being handed half a filename.
OUT="${MGVF_BUILD_OUT:-$HOME/Development/mgvf-winegstreamer-build}"
PATCHDIR="${MGVF_PATCHES:-/Applications/winevideo Patcher.app/Contents/Resources/review/build/source-patches/0.5.0}"
# Our own patches live in the repo, winevideo's in its installed app. Looking in
# one place only meant the command this project documents -- with mgvf-0001 in
# the list -- aborted unless MGVF_PATCHES had been exported by hand, which is how
# it was run all day without anyone noticing the default was wrong.
OWNPATCHES="$(cd "$(dirname "$0")/.." && pwd)/source-patches"
ENGINE="${MGVF_ENGINE:-$HOME/Applications/Crossover_patched.app/Contents/SharedSupport/CrossOver}"

want=()
if [ "${1:-}" = "--patches" ]; then shift; want=("$@"); fi

export PATH="/opt/homebrew/opt/bison/bin:$HOME/.local/cxge/toolchains/llvm-mingw/bin:$PATH"
export SDKROOT="$(xcrun --show-sdk-path)"
case "$OUT" in *\ *) printf '  the build path must not contain a space: %s\n' "$OUT"; exit 1 ;; esac
mkdir -p "$OUT"

say() { printf '  %s\n' "$*"; }

# ---- 1. the generated GStreamer headers, which no source tree carries --------
if [ ! -f "$OUT/gst-build/gst/gstenumtypes.h" ]; then
  say "generating the GStreamer headers"
  ( cd "$GST/gstreamer" && meson setup "$OUT/gst-build" --buildtype=release \
      -Dtests=disabled -Dexamples=disabled -Dbenchmarks=disabled -Dtools=disabled \
      -Dintrospection=disabled -Ddoc=disabled >"$OUT/gst-setup.log" 2>&1 )
  ( cd "$OUT/gst-build" && ninja gst/gstenumtypes.h >/dev/null 2>&1 )
fi
if [ ! -f "$OUT/base-build/gst-libs/gst/video/video-enumtypes.h" ]; then
  say "generating the base library headers"
  ( cd "$GST/gst-plugins-base" && \
    PKG_CONFIG_PATH="$OUT/gst-build/meson-uninstalled:${PKG_CONFIG_PATH:-}" \
    meson setup "$OUT/base-build" --buildtype=release \
      -Dtests=disabled -Dexamples=disabled -Dintrospection=disabled -Ddoc=disabled \
      >"$OUT/base-setup.log" 2>&1 )
  ( cd "$OUT/base-build" && for t in $(ninja -t targets all 2>/dev/null |
        grep -oE "^gst-libs/gst/[a-z]+/[a-z-]*enumtypes\.h" | sort -u); do
      ninja "$t" >/dev/null 2>&1
    done )
fi

# ---- 2. one include tree: sources for the API, build dirs for the generated --
INC="$OUT/include"
rm -rf "$INC"; mkdir -p "$INC/gst"
for h in "$GST/gstreamer/gst"/*.h; do ln -sf "$h" "$INC/gst/"; done
for h in "$OUT/gst-build/gst"/*.h; do [ -f "$h" ] && ln -sf "$h" "$INC/gst/"; done
for lib in base controller net check; do
  [ -d "$GST/gstreamer/libs/gst/$lib" ] || continue
  mkdir -p "$INC/gst/$lib"
  for h in "$GST/gstreamer/libs/gst/$lib"/*.h; do [ -f "$h" ] && ln -sf "$h" "$INC/gst/$lib/"; done
done
for lib in video audio tag pbutils; do
  mkdir -p "$INC/gst/$lib"
  for h in "$GST/gst-plugins-base/gst-libs/gst/$lib"/*.h; do [ -f "$h" ] && ln -sf "$h" "$INC/gst/$lib/"; done
  [ -d "$OUT/base-build/gst-libs/gst/$lib" ] && \
    find "$OUT/base-build/gst-libs/gst/$lib" -maxdepth 1 -name "*.h" -exec ln -sf {} "$INC/gst/$lib/" \;
done

# ---- 3. patches, applied to a copy so the sources stay pristine --------------
TREE="$OUT/wine-src"
rm -rf "$TREE"; cp -R "$WINE" "$TREE"
for p in "${want[@]:-}"; do
  [ -n "$p" ] || continue
  # Ours first. Every patch this build needs now lives in the repository --
  # winevideo's four are carried there with their provenance and their licence --
  # so an installed winevideo Patcher is an override, not a requirement. It used
  # to be the other way round, and the build simply did not run without their
  # application present.
  f=$(ls "$OWNPATCHES/$p"-*.patch 2>/dev/null | head -1)
  [ -n "$f" ] || f=$(ls "$PATCHDIR/$p"-*.patch 2>/dev/null | head -1)
  [ -n "$f" ] || { say "no patch numbered $p in $PATCHDIR or $OWNPATCHES"; exit 1; }
  if ( cd "$TREE" && patch -p1 --forward -l -F3 <"$f" >/dev/null 2>&1 ); then
    say "applied $(basename "$f")"
  else
    say "FAILED to apply $(basename "$f")"; exit 1
  fi
done

# ---- 4. configure and build just this one dll --------------------------------
say "configuring"
rm -rf "$OUT/wine-build"; mkdir -p "$OUT/wine-build"
export GSTREAMER_CFLAGS="-I$INC $(pkg-config --cflags glib-2.0) \
  -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_78 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_78"
export GSTREAMER_LIBS="-L$ENGINE/lib64 -lgstreamer-1.0 -lgstvideo-1.0 -lgstaudio-1.0 \
  -lgsttag-1.0 -lgobject-2.0 -lglib-2.0"
export CC="clang -arch x86_64 -isysroot $SDKROOT" LDFLAGS="-isysroot $SDKROOT"
# SDL2 is the one optional dependency below that is NOT turned off, and it is
# worth saying why, because everything around it is.
#
# This configure was written for one dll, winegstreamer, and every --without-
# there is a dependency that dll does not need. Then the controller set started
# riding on the same tree, and it ships winebus's UNIX half -- which is where
# wine's SDL joystick bus lives. Built --without-sdl, that bus is compiled out
# entirely, so installing the set REPLACED the engine's winebus.so with one
# that has only IOHID: our build was 45 KB against CodeWeavers' 85 KB, and a
# bottle told to prefer SDL found no bus at all and no pad at all. Measured on
# 2026-09-09, after three attempts to run a control that could never have run.
#
# The flags are set explicitly rather than left to pkg-config so that the build
# does not quietly depend on what is on PATH, which is how the loss happened.
# The headers can come from anywhere -- they carry no architecture -- but the
# LIBRARY has to be the engine's own: this build is x86_64 and homebrew's SDL2
# on an Apple Silicon machine is arm64, which does not even link.
export SDL2_CFLAGS="-I/opt/homebrew/include/SDL2 -D_THREAD_SAFE"
export SDL2_LIBS="-L$ENGINE/lib64 -lSDL2"

# And the name winebus will dlopen at run time, which is NOT the soname
# configure would find on its own.
#
# bus_sdl.c does dlopen(SONAME_LIBSDL2), and configure fills that in with the
# library's leaf name, "libSDL2-2.0.0.dylib". On macOS today a leaf name reaches
# only /usr/lib and the cryptexes -- measured on 2026-09-09: not $HOME/lib, not
# /usr/local/lib, both of which the old fallback list used to include. The
# engine keeps its copy in lib64, so the dlopen simply fails, and it fails the
# same way in the file CodeWeavers ship: the SDL bus has never been able to
# start on this stack. DYLD_FALLBACK_LIBRARY_PATH is no way out either, because
# bin/wine is a Perl script and SIP strips DYLD_* on the way into /usr/bin/perl.
#
# @loader_path IS honoured by dlopen, and winebus.so lands at a fixed depth --
# <CX>/lib/wine/x86_64-unix/winebus.so -- so three levels up and into lib64
# reaches the engine's own copy wherever the engine is installed. Passed as a
# cache variable so that configure records it instead of probing for it.
SDL2_SONAME="@loader_path/../../../lib64/libSDL2-2.0.0.dylib"
( cd "$OUT/wine-build" && "$TREE/configure" --host=x86_64-apple-darwin --enable-win64 \
    --with-mingw --without-freetype --without-x --without-alsa --without-oss \
    --without-pulse --without-sane --without-capi --without-gphoto --without-krb5 \
    --without-gssapi --without-opencl --without-pcap --without-usb --without-v4l2 \
    --without-vulkan --without-cups --without-dbus --without-fontconfig \
    --without-gnutls --without-inotify --without-netapi --without-opengl \
    --with-sdl --without-udev --without-unwind --without-xml --without-ffmpeg \
    ac_cv_lib_soname_SDL2="$SDL2_SONAME" \
    >"$OUT/configure.log" 2>&1 ) || { say "configure failed, see $OUT/configure.log"; exit 1; }
grep -q "gst_pad_new in -lgstreamer-1.0... yes" "$OUT/configure.log" \
  || { say "configure did not find GStreamer -- it would build without it"; exit 1; }
# The same question for SDL2, for the same reason: a build that silently loses
# it produces a winebus.so that looks fine and has one bus fewer than the file
# it replaces.
# The pattern is anchored on #define: config.h carries a commented "#undef
# SONAME_LIBSDL2" line when the library was NOT found, and an unanchored match
# would find that and call it success.
grep -q "^#define SONAME_LIBSDL2 \"$SDL2_SONAME\"" "$OUT/wine-build/include/config.h" \
  || { say "configure did not take the SDL2 soname -- winebus.so would ship a bus that cannot load it"; exit 1; }

say "building"
( cd "$OUT/wine-build" && make -j8 dlls/winegstreamer/all >"$OUT/make.log" 2>&1 ) \
  || { say "build failed, see $OUT/make.log"; exit 1; }

# ---- 5. put back the shape the engine expects --------------------------------
SO="$OUT/wine-build/dlls/winegstreamer/winegstreamer.so"
DLL="$OUT/wine-build/dlls/winegstreamer/x86_64-windows/winegstreamer.dll"
for lib in $(otool -L "$SO" | grep -oE "/opt/cxoffice/lib64/[^ ]+\.dylib"); do
  install_name_tool -change "$lib" "@rpath/$(basename "$lib")" "$SO"
done
otool -l "$SO" | grep -q "lib64" || install_name_tool -add_rpath "@loader_path/../../../lib64" "$SO"

# and refuse to hand back something that cannot load
for d in $(otool -L "$SO" | grep -oE "@rpath/[^ ]+\.dylib" | sed 's|@rpath/||'); do
  [ -f "$ENGINE/lib64/$d" ] || [ "$d" = "winegstreamer.so" ] || \
    { say "unresolved dependency: $d"; exit 1; }
done
otool -L "$SO" | grep -q "/opt/cxoffice" && { say "an absolute cxoffice path survived"; exit 1; }

cp "$SO" "$OUT/winegstreamer.so"; cp "$DLL" "$OUT/winegstreamer.dll"

# Which engine this was built for, recorded beside it.
#
# The unix half links against Wine's own internals, and those are not stable
# between releases: a binary built for one CrossOver must not be dropped onto
# the next. Measured the hard way earlier -- winevideo's pair, built on Wine
# 11.0, could not be used on an engine running 11.15, and the only reason the
# transplant later worked was that the target had moved to 11.0 too.
#
# So the pairing is written down rather than remembered, and install-winegstreamer
# below refuses a mismatch. winevideo does the same thing with a stock inventory,
# and it is the part of their design most worth copying.
ENGINE_APP="$(cd "$ENGINE/../../.." && pwd)"
cat > "$OUT/built-for.json" <<JSON
{
  "engine_app": "$(basename "$ENGINE_APP")",
  "engine_version": "$(defaults read "$ENGINE_APP/Contents/Info" CFBundleVersion 2>/dev/null)",
  "wine_build": "$(strings -a "$ENGINE/lib/wine/x86_64-unix/ntdll.so" | grep -oE 'wine-[0-9]+\.[0-9]+[^ ]*' | head -1)",
  "patches": "${want[*]:-none}"
}
JSON
say "built for: $(sed -n 's/.*"wine_build": "\(.*\)".*/\1/p' "$OUT/built-for.json")"
say "built:  $OUT/winegstreamer.so  ($(stat -f %z "$OUT/winegstreamer.so") bytes)"
say "        $OUT/winegstreamer.dll ($(stat -f %z "$OUT/winegstreamer.dll") bytes)"
say "install with: cp them into <engine>/lib/wine/x86_64-{unix,windows}/"
