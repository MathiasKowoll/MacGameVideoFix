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

# The whole point of this script is to run a bottle under an engine other than
# its own, and that has a consequence worth saying out loud rather than letting
# somebody rediscover it.
#
# A bottle wired for the video fixes carries GST_PLUGIN_PATH pointing at a codec
# staged for ONE engine. The staged tree reaches its libraries through symlinks
# into that CrossOver's bundle, so loading the plugin under a different engine
# pulls a second libgstreamer into a process that already has one. macOS says so
# plainly -- "Class GstCocoaApplicationDelegate is implemented in both ... This
# may cause spurious casting failures and mysterious crashes" -- and then the run
# under test is measuring that, not whatever it was meant to measure.
#
# An afternoon went into a game that stalled at its loading screen before this
# was noticed in the terminal output. Removing the variable took its frame rate
# from 24 to 61. It was not the cause of that stall, but every measurement taken
# before it was found had this sitting underneath.
#
# The app already prevents this in normal use: it writes the path for the engine
# the bottle records, and re-points it when the bottle is migrated. Only this
# script crosses the two deliberately, so only this script has to say so.
CONF="$B/cxbottle.conf"
GST="$(sed -n 's/^[[:space:]]*"GST_PLUGIN_PATH" = "\(.*\)"$/\1/p' "$CONF" 2>/dev/null | head -1)"
if [ -n "$GST" ]; then
  MAP="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs/.map"
  ENGVER="$(defaults read "$APP/Contents/Info" CFBundleVersion 2>/dev/null)"
  MATCH="$(grep "^$ENGVER|" "$MAP" 2>/dev/null | cut -d'|' -f3 | head -1)"
  if [ "$GST" = "$MATCH" ]; then
    echo "codec  : staged for this engine"
  else
    echo
    echo "  WARNING: this bottle's GST_PLUGIN_PATH was staged for a different engine."
    echo "    bottle points at : $GST"
    if [ -n "$MATCH" ]; then
      echo "    this engine wants: $MATCH"
      echo "    Run with GST_PLUGIN_PATH set to the second path, or accept that two"
      echo "    copies of libgstreamer land in the process and the run is not clean."
      GST_PLUGIN_PATH="$MATCH"; export GST_PLUGIN_PATH
      echo "    -> overriding GST_PLUGIN_PATH for this run only."
    else
      echo "    no codec is staged for this engine ($ENGVER)."
      echo "    Stage one from the app, or expect two libgstreamer copies in the process."
    fi
  fi
fi
echo

STEAM='C:\Program Files (x86)\Steam\steam.exe'
echo "launching Steam -- start the game from inside it, since Steam titles"
echo "usually refuse to run with the client absent."
exec "$WINE" --bottle "$BOTTLE" --cx-app "$STEAM"
