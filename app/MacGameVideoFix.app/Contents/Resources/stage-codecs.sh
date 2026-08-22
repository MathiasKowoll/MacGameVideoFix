#!/usr/bin/env bash
#
# Stage the codecs CrossOver does not ship, without patching CrossOver.
#
# Preview decodes VP9, H.264 and AAC on its own, but has no WMV3, VC-1 or WMA
# decoder -- which Persona 5 Strikers and the Nioh titles need. The official
# GStreamer.framework has them, in libgstlibav (ffmpeg).
#
# Loading that plugin in place crashes: dyld ends up with two copies of
# libgstreamer and two GObject type registries, and Preview ships no
# gst-plugin-scanner, so there is no forked scanner to absorb it.
#
#     objc: Class GstCocoaApplicationDelegate is implemented in both ...
#
# Re-homed it works. The plugin resolves its dependencies through
# @loader_path/.., so a directory of its own with ffmpeg beside it and the
# GStreamer core symlinked to CROSSOVER'S copy gives one core, one registry,
# and the decoders registered.
#
# Then one line in the bottle: GST_PLUGIN_PATH = <this directory>. Preview's
# launcher sets only GST_PLUGIN_SYSTEM_PATH and never touches GST_PLUGIN_PATH,
# and the bottle's environment is applied first, so it survives.
#
#     runtime/stage-codecs.sh [arch] [engine]
#     runtime/stage-codecs.sh x86_64 "CrossOver Preview"
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ARCH="${1:-x86_64}"
ENGINE="${2:-CrossOver Preview}"
FRAMEWORK=/Library/Frameworks/GStreamer.framework/Versions/1.0
OUT="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs/$ARCH"

APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app" >&2; exit 1; }
CX="$APP/Contents/SharedSupport/CrossOver"

SRC="$CX/lib/$ARCH"
[ -d "$SRC" ] || SRC="$CX/lib64"
[ -d "$SRC" ] || { echo "error: no $ENGINE library directory for $ARCH" >&2; exit 1; }
[ -d "$FRAMEWORK" ] || {
  echo "error: GStreamer.framework is not installed." >&2
  echo "       Get it from https://gstreamer.freedesktop.org (runtime package)." >&2
  exit 1
}

echo "engine    : $ENGINE ($ARCH)"
echo "framework : $FRAMEWORK"
echo "staging   : $OUT"

# Layout matters. GST_PLUGIN_PATH points at a directory GStreamer scans, and
# it tries to load everything in it as a plugin -- so the support libraries go
# one level out, in lib/, where the plugin's own @loader_path/../lib finds them
# and the scanner never looks.
rm -rf "$OUT"
mkdir -p "$OUT/gstreamer-1.0" "$OUT/lib"

# The plugin itself, and ffmpeg, which is the whole point of taking it.
cp "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib" "$OUT/gstreamer-1.0/"

# Everything the plugin and ffmpeg need. Names beginning libgst, libglib,
# libgobject and friends must come from CrossOver -- taking those from the
# framework is exactly what produces two cores and a crash.
from_crossover() {
  case "$1" in
    libgst*|libglib*|libgobject*|libgmodule*|libgthread*|libgio*|libintl*|libffi*|libpcre*) return 0;;
    *) return 1;;
  esac
}

needed() { otool -L "$1" 2>/dev/null | grep -oE '@rpath/[^ ]+\.dylib' | sed 's|@rpath/||' | sort -u; }

pending=$(needed "$OUT/gstreamer-1.0/libgstlibav.dylib")
seen=""
while [ -n "$pending" ]; do
  next=""
  for lib in $pending; do
    case " $seen " in *" $lib "*) continue;; esac
    seen="$seen $lib"
    if from_crossover "$lib"; then
      [ -e "$SRC/$lib" ] && ln -sf "$SRC/$lib" "$OUT/lib/$lib"
    elif [ -f "$FRAMEWORK/lib/$lib" ]; then
      cp -f "$FRAMEWORK/lib/$lib" "$OUT/lib/$lib"
      next="$next $(needed "$OUT/lib/$lib")"
    fi
  done
  pending="$next"
done

copied=$(find "$OUT" -type f -name '*.dylib' | wc -l | tr -d ' ')
linked=$(find "$OUT" -type l | wc -l | tr -d ' ')
echo
echo "  ffmpeg and friends copied : $copied"
echo "  CrossOver libraries linked: $linked"
echo
echo "Add this to the bottle's cxbottle.conf, under [EnvironmentVariables]:"
echo
echo "  \"GST_PLUGIN_PATH\" = \"$OUT/gstreamer-1.0\""
