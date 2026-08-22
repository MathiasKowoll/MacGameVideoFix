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
# Every installed CrossOver gets its own staging directory, keyed by the
# engine's CFBundleVersion. That is the same string a bottle records as its
# "Version", so a bottle can be matched to the staging it needs without
# guessing -- and it survives the case that broke the old code: a Preview build
# living under an .app filename that does not say Preview.
#
#     runtime/stage-codecs.sh [arch] [engine version]
#     runtime/stage-codecs.sh x86_64                   every engine installed
#     runtime/stage-codecs.sh x86_64 26.3.0.39832      just that one
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ARCH="${1:-x86_64}"
WANT="${2:-}"
FRAMEWORK=/Library/Frameworks/GStreamer.framework/Versions/1.0
ROOT="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs"

# Find CrossOver by what it declares, not by what its file is called. One of
# the installs on the machine this was fixed on is a Preview build named
# Crossover_patched.app -- searching for "CrossOver Preview.app" would never
# have seen it, and staging against the wrong engine is a crash, not a warning.
plist_value() { defaults read "$1/Contents/Info.plist" "$2" 2>/dev/null; }

ENGINES=""
while IFS= read -r app; do
  [ -d "$app/Contents/SharedSupport/CrossOver" ] || continue
  ver="$(plist_value "$app" CFBundleVersion)"
  [ -n "$ver" ] || continue
  ENGINES="$ENGINES$ver|$app"$'\n'
done <<EOF
$(ls -d /Applications/*.app "$HOME/Applications"/*.app 2>/dev/null)
EOF

# Both /Applications and ~/Applications. Staging every engine found is cheap
# and reversible; the choice of which one a bottle uses is made in the app,
# against this list, so a missing entry is a CrossOver the user cannot select.
ENGINES="$(printf '%s' "$ENGINES" | sort -u -t'|' -k1,1 | grep -v '^$' || true)"
[ -n "$ENGINES" ] || { echo "error: no CrossOver installation found" >&2; exit 1; }

if [ -n "$WANT" ]; then
  ENGINES="$(printf '%s\n' "$ENGINES" | grep "^$WANT|" || true)"
  [ -n "$ENGINES" ] || { echo "error: no CrossOver with version $WANT" >&2; exit 1; }
fi

[ -d "$FRAMEWORK" ] || {
  echo "error: GStreamer.framework is not installed." >&2
  echo "       Install the macOS *runtime* package, 1.24 series:" >&2
  echo "       https://gstreamer.freedesktop.org/data/pkg/osx/1.24.14/" >&2
  echo "       1.24.14 is the version this was verified with." >&2
  exit 1
}

# Which GStreamer this is, from the library rather than a plist -- the version
# is encoded in its compatibility number as 1.MINOR.PATCH.
#
# winevideo names 1.24.13 exactly. What actually has to hold is looser: the
# plugin has to be ABI-compatible with the CrossOver core it is re-homed onto,
# and GStreamer guarantees that across 1.x. 1.24.14 is what is measured working
# here. Anything outside 1.24 is untested, so it is reported rather than
# refused -- refusing something that might work is as unhelpful as staying
# quiet about something that might not.
# The framework is a universal binary, so otool prints a header line per
# architecture; matching on "compatibility version" skips those.
compat="$(otool -L "$FRAMEWORK/lib/libgstreamer-1.0.0.dylib" 2>/dev/null \
          | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1)"
if [ -n "${compat:-}" ] && [ "$compat" -gt 0 ] 2>/dev/null; then
  gst_minor=$(( compat / 100 ))
  gst_patch=$(( compat % 100 ))
  echo "gstreamer : 1.${gst_minor}.${gst_patch}"
else
  gst_minor=""
  echo "gstreamer : version not readable"
fi
if [ -n "$gst_minor" ] && [ "$gst_minor" != "24" ]; then
  echo "            note: 1.24.14 is the version this was verified with." >&2
  echo "            Yours may work perfectly well. Carrying on." >&2
fi

# One staging directory per engine. The support libraries are symlinked INTO
# the engine's bundle, so a directory is bound to the engine it was made for --
# pointing a bottle at the wrong one is the two-cores crash this whole script
# exists to avoid.
stage_one() {
  VER="$1"; APP="$2"
  ENGINE="$(plist_value "$APP" CFBundleName)"
  ENGINE="${ENGINE:-$(basename "$APP" .app)}"

  # Named for the application, not for its version.
  #
  # CFBundleVersion changes every time CrossOver updates. Keyed on that, an
  # update orphaned the staged directory, re-stamped every bottle's "Version",
  # and left the lot reading as drifted -- a screenful of repairs for something
  # nobody did. The .app's own name does not move when it is updated in place,
  # so the path a bottle holds stays valid across updates.
  #
  # It is the filename rather than CFBundleName because two installs can declare
  # the same name -- this machine has two calling themselves "CrossOver Preview"
  # -- and a directory shared between two engines is the two-cores crash again.
  SLUG="$(printf '%s' "$(basename "$APP" .app)" | tr -c 'A-Za-z0-9._-' '-')"
  CX="$APP/Contents/SharedSupport/CrossOver"
  SRC="$CX/lib/$ARCH"
  [ -d "$SRC" ] || SRC="$CX/lib64"
  if [ ! -d "$SRC" ]; then
    echo "  skipped   : $ENGINE ($VER) has no $ARCH libraries" >&2
    return 0
  fi
  OUT="$ROOT/$SLUG/$ARCH"

  # Already built, from this same engine, and finished: leave it alone.
  #
  # Re-staging an identical directory is not free. It replaces something bottles
  # point at, for no gain, every time anyone presses the button -- and the safest
  # replacement is still a replacement. The reason to rebuild is that the engine
  # has been updated underneath it, and .built-against is what says so.
  # FORCE=1 rebuilds regardless, which is the escape when a staging is suspect
  # rather than merely old.
  if [ "${FORCE:-0}" != 1 ] &&
     [ -f "$OUT/.complete" ] &&
     [ "$(cat "$OUT/.built-against" 2>/dev/null)" = "$VER" ]; then
    echo
    echo "engine    : $ENGINE  ($VER, $ARCH)"
    echo "staging   : already built from this CrossOver, left untouched"
    printf '%s|%s|%s\n' "$VER" "$ENGINE" "$OUT/gstreamer-1.0" >> "$ROOT/.map"
    return 0
  fi
  # Built somewhere else and moved into place in one step, because bottles point
  # at $OUT while this runs. Emptying it first meant a re-stage destroyed a live
  # staging for the length of the build, and a run that was stopped or crashed
  # left a half-built directory there that nothing could tell from a finished
  # one -- the app pointed bottles at it and the game failed on the first
  # cutscene with unresolved dependencies.
  TMP="$ROOT/$SLUG/.$ARCH.incoming.$$"
  mkdir -p "$ROOT/$SLUG"
  find "$ROOT/$SLUG" -maxdepth 1 -name ".$ARCH.incoming.*" -exec rm -rf {} + 2>/dev/null || true

  echo "engine    : $ENGINE  ($VER, $ARCH)"
  echo "framework : $FRAMEWORK"
  echo "staging   : $OUT"

  # Layout matters. GST_PLUGIN_PATH points at a directory GStreamer scans, and
  # it tries to load everything in it as a plugin -- so the support libraries go
  # one level out, in lib/, where the plugin's own @loader_path/../lib finds them
  # and the scanner never looks.
  mkdir -p "$TMP/gstreamer-1.0" "$TMP/lib"

  # The plugin itself, and ffmpeg, which is the whole point of taking it.
  cp "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib" "$TMP/gstreamer-1.0/"

  # Everything the plugin and ffmpeg need. Names beginning libgst, libglib,
  # libgobject and friends must come from CrossOver -- taking those from the
  # framework is exactly what produces two cores and a crash.
  from_crossover() {
    case "$1" in
      libgst*|libglib*|libgobject*|libgmodule*|libgthread*|libgio*|libintl*|libffi*|libpcre*) return 0;;
      *) return 1;;
    esac
  }

  # A library with no @rpath dependencies is normal, and grep saying so must not
  # end the run: pipefail turns that empty result into a failure and set -e acts
  # on it. The old code only ever asked this of libraries that always had some.
  needed() { otool -L "$1" 2>/dev/null | grep -oE '@rpath/[^ ]+\.dylib' \
               | sed 's|@rpath/||' | sort -u || true; }

  pending=$(needed "$TMP/gstreamer-1.0/libgstlibav.dylib")
  seen=""
  while [ -n "$pending" ]; do
    next=""
    for lib in $pending; do
      case " $seen " in *" $lib "*) continue;; esac
      seen="$seen $lib"
      if from_crossover "$lib"; then
        # Follow what CrossOver's own libraries need, too. Linking one and
        # stopping there was enough until a library CrossOver ships turned out to
        # want a sibling -- libgstpbutils wants libgsttag -- and dyld resolves
        # that @rpath against this directory, not against CrossOver's. The plugin
        # then fails to load outright, silently, and the only trace is a
        # GStreamer warning nobody sees.
        if [ -e "$SRC/$lib" ]; then
          ln -sf "$SRC/$lib" "$TMP/lib/$lib"
          next="$next $(needed "$SRC/$lib")"
        fi
      elif [ -f "$FRAMEWORK/lib/$lib" ]; then
        cp -f "$FRAMEWORK/lib/$lib" "$TMP/lib/$lib"
        next="$next $(needed "$TMP/lib/$lib")"
      fi
    done
    pending="$next"
  done

  copied=$(find "$TMP" -type f -name '*.dylib' | wc -l | tr -d ' ')
  linked=$(find "$TMP" -type l | wc -l | tr -d ' ')
  echo
  echo "  ffmpeg and friends copied : $copied"
  echo "  CrossOver libraries linked: $linked"
  echo

  # The marker the app reads, written last: completeness is a fact recorded by
  # the thing that knows it, not something inferred from a directory listing --
  # the plugin is copied before the walk above, so a listing says "staged" while
  # a dozen support libraries are still missing.
  date -u +'%Y-%m-%dT%H:%M:%SZ' > "$TMP/.complete"

  # Put the new one in place before removing the old, not after. Deleting first
  # left the path a bottle points at absent for as long as an rm -rf of thirty
  # megabytes takes, and a game started in that window finds nothing there. Two
  # renames instead: the gap is now the time between them.
  OLD=""
  if [ -d "$OUT" ]; then
    OLD="$OUT.replaced.$$"
    mv "$OUT" "$OLD" || { echo "error: could not move the previous staging aside" >&2; exit 1; }
  fi
  if ! mv "$TMP" "$OUT"; then
    echo "error: could not put the new staging in place" >&2
    [ -n "$OLD" ] && mv "$OLD" "$OUT"
    exit 1
  fi
  [ -n "$OLD" ] && rm -rf "$OLD"

  # Written to the file rather than to a variable: the loop below runs in a
  # subshell, so a variable would not survive it. This engine's own line is
  # replaced rather than appended, so re-staging one engine cannot leave the
  # map holding two answers for it.
  if [ -f "$ROOT/.map" ]; then
    grep -v "^$VER|" "$ROOT/.map" > "$ROOT/.map.new" || true
    mv "$ROOT/.map.new" "$ROOT/.map"
  fi
  # The version it was built against. The path survives an update; the contents
  # may not, because a new CrossOver can carry a different GStreamer core. This
  # is what lets the app say "CrossOver has been updated, run this again"
  # instead of waiting for a crash to say it.
  printf '%s\n' "$VER" > "$OUT/.built-against"
  printf '%s|%s|%s\n' "$VER" "$ENGINE" "$OUT/gstreamer-1.0" >> "$ROOT/.map"
}

mkdir -p "$ROOT"
# Truncated only for a full run. Per-engine staging is now the normal case --
# every repair invokes this with one version -- and wiping the map would leave
# it describing whichever engine was fixed last.
[ -n "$WANT" ] || : > "$ROOT/.map"
printf '%s\n' "$ENGINES" | while IFS='|' read -r ver app; do
  [ -n "$ver" ] || continue
  echo
  stage_one "$ver" "$app"
done

# The map is what the app reads to point each bottle at its own engine: a
# bottle's cxbottle.conf records the CFBundleVersion that last updated it, and
# that is the first field here.
echo
echo "staged per engine:"
grep -v '^$' "$ROOT/.map" 2>/dev/null | while IFS='|' read -r ver name path; do
  printf '  %-16s %-20s %s\n' "$ver" "$name" "$path"
done
