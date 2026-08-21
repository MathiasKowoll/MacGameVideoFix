#!/usr/bin/env bash
#
# Launch a bottle under a chosen CrossOver engine.
#
# Bottles are shared between CrossOver installs -- the .app supplies the engine,
# the bottle supplies the prefix -- so the same games can be run with winevideo
# present or absent without reinstalling anything. That is what makes "does this
# game actually need winevideo?" answerable rather than a matter of opinion.
#
# Use two builds of the SAME CrossOver version, differing only in winevideo.
# A version difference confounds the result, and a build carrying
# libgstmatroska but not libgstvpx is a half state: it can demux a WebM and
# still not decode the VP9 inside it.
#
#     diagnostics/launch-with.sh --list
#     diagnostics/launch-with.sh CrossOver SteamVp9            # no winevideo
#     diagnostics/launch-with.sh CrossOver-winevideo SteamVp9  # with it
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u

list() {
  printf '  %-30s %-14s %s\n' ENGINE VERSION 'winevideo (libgstvpx)'
  for root in /Applications "$HOME/Applications"; do
    for app in "$root"/*[Cc]ross[Oo]ver*.app; do
      [ -d "$app" ] || continue
      local base="$app/Contents/SharedSupport/CrossOver"
      local v vpx=no
      v=$(defaults read "$app/Contents/Info" CFBundleShortVersionString 2>/dev/null)
      for d in "$base/lib64/gstreamer-1.0" "$base/lib/x86_64/gstreamer-1.0"; do
        [ -f "$d/libgstvpx.dylib" ] && vpx=YES
      done
      printf '  %-30s %-14s %s\n' "$(basename "$app" .app)" "${v:-?}" "$vpx"
    done
  done 2>/dev/null
}

[ $# -ge 1 ] || { sed -n '3,20p' "$0" >&2; exit 1; }
[ "$1" = "--list" ] && { list; exit 0; }
[ $# -ge 2 ] || { echo "usage: $0 <engine> <bottle>" >&2; exit 1; }

ENGINE="$1"; BOTTLE="$2"
APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app in /Applications or ~/Applications" >&2; list >&2; exit 1; }

WINE="$APP/Contents/SharedSupport/CrossOver/bin/wine"
[ -x "$WINE" ] || { echo "error: no wine binary inside $APP" >&2; exit 1; }

B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
[ -d "$B" ] || { echo "error: no bottle named $BOTTLE" >&2; ls "$HOME/Library/Application Support/CrossOver/Bottles" >&2; exit 1; }

# Say out loud which side of the experiment this is, so a screenshot of the
# terminal is enough to tell the two runs apart afterwards.
VPX=no
for d in "$APP/Contents/SharedSupport/CrossOver/lib64/gstreamer-1.0" \
         "$APP/Contents/SharedSupport/CrossOver/lib/x86_64/gstreamer-1.0"; do
  [ -f "$d/libgstvpx.dylib" ] && VPX=YES
done
echo "engine : $ENGINE ($(defaults read "$APP/Contents/Info" CFBundleShortVersionString 2>/dev/null))"
echo "bottle : $BOTTLE"
echo "winevideo (libgstvpx): $VPX"
echo

STEAM='C:\Program Files (x86)\Steam\steam.exe'
echo "launching Steam -- start the game from inside it, since Steam titles"
echo "usually refuse to run with the client absent."
exec "$WINE" --bottle "$BOTTLE" --cx-app "$STEAM"
