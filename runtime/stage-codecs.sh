#!/usr/bin/env bash
#
# Stage the codecs CrossOver does not ship, without patching CrossOver.
#
# The supported engine is stable CrossOver 26.3 (26.3.0.39832), and only that.
# CrossOver Preview was dropped on 2026-08-31; where Preview is still named below
# it is a record of where something was measured, not a place to run this.
#
# CrossOver decodes VP9, H.264 and AAC on its own, but has no WMV3, VC-1 or WMA
# decoder -- which Persona 5 Strikers and the Nioh titles need. The official
# GStreamer.framework has them, in libgstlibav (ffmpeg).
#
# Loading that plugin in place crashes: dyld ends up with two copies of
# libgstreamer and two GObject type registries, and CrossOver ships no
# gst-plugin-scanner, so there is no forked scanner to absorb it.
#
#     objc: Class GstCocoaApplicationDelegate is implemented in both ...
#
# Re-homed it works. The plugin resolves its dependencies through
# @loader_path/.., so a directory of its own with ffmpeg beside it and the
# GStreamer core symlinked to CROSSOVER'S copy gives one core, one registry,
# and the decoders registered.
#
# Then one line in the bottle: GST_PLUGIN_PATH = <this directory>. CrossOver's
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
#     runtime/stage-codecs.sh                          every engine, every arch
#     runtime/stage-codecs.sh all 26.3.0.39832         that engine, every arch
#     runtime/stage-codecs.sh x86_64                   one arch only
#
# BOTH ARCHITECTURES, IN ONE PASS. CrossOver 27 runs ARM Windows binaries as
# well as x86_64 ones, and a bottle says which it is: `"WineArch" = "arm64"`
# against `"win64"`. They do not share a plugin directory, and staging only
# x86_64 left an ARM bottle with no VC-1, WMV or WMA decoder at all -- the
# GStreamer framework is universal, so the material was there the whole time and
# the gap was this script's. Each engine gets every architecture it actually
# ships; naming one stages only that one.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

ARCH="${1:-all}"
WANT="${2:-}"
FRAMEWORK=/Library/Frameworks/GStreamer.framework/Versions/1.0
ROOT="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs"

# Find CrossOver by what it declares, not by what its file is called. One of
# the installs on the machine this was fixed on was a Preview build named
# Crossover_patched.app -- searching for "CrossOver Preview.app" would never
# have seen it, and staging against the wrong engine is a crash, not a warning.
# Preview is no longer supported, but the rule it taught is general: an engine
# is identified by what its Info.plist declares, never by its filename.
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
#
# Unique by the whole line, NOT by the version field. A tool that copies a
# CrossOver bundle and patches the copy -- which is how Procyon and CXPatcher
# work -- produces a second engine declaring the SAME CFBundleVersion as the
# original it was copied from. Deduplicating on the version dropped one of each
# pair silently: on the machine this was found, two of four engines never
# reached the map, and the bottle pointing at one of them could not be repaired
# because the app was never told it existed.
ENGINES="$(printf '%s' "$ENGINES" | sort -u | grep -v '^$' || true)"
[ -n "$ENGINES" ] || { echo "error: no CrossOver installation found" >&2; exit 1; }

# The selector is a bundle path or a version, and the difference matters.
#
# A version is ambiguous the moment a patched copy exists, because the copy
# inherits it. A path is not: the caller that already knows which CrossOver it
# is about to launch -- a launcher handing us its own engine -- can say so
# exactly. Both forms are exact matches, never prefixes: two bundles can share
# a prefix and staging against the wrong engine is a crash, not a warning.
if [ -n "$WANT" ]; then
  case "$WANT" in
    /*|*.app)
      WANT_PATH="${WANT%/}"
      ENGINES="$(printf '%s\n' "$ENGINES" | awk -F'|' -v p="$WANT_PATH" '$2 == p' || true)"
      [ -n "$ENGINES" ] || {
        echo "error: no CrossOver bundle at $WANT_PATH" >&2
        echo "       It must exist and contain Contents/SharedSupport/CrossOver." >&2
        exit 1
      }
      ;;
    *)
      ENGINES="$(printf '%s\n' "$ENGINES" | awk -F'|' -v v="$WANT" '$1 == v' || true)"
      [ -n "$ENGINES" ] || { echo "error: no CrossOver with version $WANT" >&2; exit 1; }
      ;;
  esac
fi

[ -d "$FRAMEWORK" ] || {
  echo "error: GStreamer.framework is not installed." >&2
  echo "       Install the macOS *runtime* package:" >&2
  echo "       https://gstreamer.freedesktop.org/data/pkg/osx/1.24.13/" >&2
  echo "       1.24.13 is what is installed and working here, and it is the" >&2
  echo "       version winevideo names. A launcher that owns its engine may" >&2
  echo "       require it exactly, so prefer 1.24.13 over a later 1.24." >&2
  exit 1
}

# Which GStreamer this is, from the library rather than a plist -- the version
# is encoded in its compatibility number as 1.MINOR.PATCH.
#
# winevideo names 1.24.13 exactly, and 1.24.13 is what is installed and
# measured working here -- pkgutil says so for every gstreamer package on this
# machine. This text used to name 1.24.14 as "the version this was verified
# with", which was never true of this install and pointed a user at a version
# RaccoonBot refuses: it requires 1.24.13 exactly, so following our own
# instructions could have broken the other route on the same machine.
#
# What actually has to hold is looser: the plugin has to be ABI-compatible with
# the CrossOver core it is re-homed onto, and GStreamer guarantees that across
# 1.x. So anything outside 1.24 is reported rather than refused -- refusing
# something that might work is as unhelpful as staying quiet about something
# that might not. Advising a version and accepting a range are different jobs,
# and the advice should name what both routes accept.
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
# What matters is not which version this framework is, but whether it is the same
# series as the core it will be plugged into. The plugin resolves libgstreamer
# through a symlink to CROSSOVER's copy, so the pairing that has to hold is
# framework-plugin against engine-core -- and a hardcoded "24" said nothing about
# that. It called a 1.24 framework verified while the engine being staged for ran
# 1.28, which is the one comparison worth making and the one it was not making.
#
# A different series still stages: GStreamer keeps its ABI across 1.x and it
# usually works. But it says so, so that a decoder which fails to register later
# has somewhere to point.
if [ -n "$gst_minor" ]; then
  printf '%s\n' "$ENGINES" | while IFS='|' read -r eng_ver eng_app; do
    [ -n "$eng_app" ] || continue
    eng_core="$eng_app/Contents/SharedSupport/CrossOver/lib/x86_64/libgstreamer-1.0.0.dylib"
    [ -f "$eng_core" ] || eng_core="$eng_app/Contents/SharedSupport/CrossOver/lib/aarch64/libgstreamer-1.0.0.dylib"
    [ -f "$eng_core" ] || eng_core="$eng_app/Contents/SharedSupport/CrossOver/lib64/libgstreamer-1.0.0.dylib"
    [ -f "$eng_core" ] || continue
    eng_compat="$(otool -L "$eng_core" 2>/dev/null \
                  | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1)"
    [ -n "$eng_compat" ] && [ "$eng_compat" -gt 0 ] 2>/dev/null || continue
    eng_minor=$(( eng_compat / 100 ))
    if [ "$eng_minor" = "$gst_minor" ]; then
      echo "            same series as $(basename "$eng_app" .app): 1.${gst_minor}"
    else
      echo "            REFUSED: $(basename "$eng_app" .app) runs GStreamer 1.${eng_minor}," >&2
      echo "            this framework is 1.${gst_minor}. Install a GStreamer 1.${eng_minor}" >&2
      echo "            runtime and run this again." >&2
    fi
  done
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
  # the same name -- this machine had two calling themselves "CrossOver Preview"
  # -- and a directory shared between two engines is the two-cores crash again.
  SLUG="$(printf '%s' "$(basename "$APP" .app)" | tr -c 'A-Za-z0-9._-' '-')"
  CX="$APP/Contents/SharedSupport/CrossOver"

  # An engine that already carries these plugins needs nothing staged.
  #
  # Staging exists because stock CrossOver ships 17 GStreamer plugins and none
  # of them decodes VC-1, WMV3 or VP9: the three that do are put in a directory
  # of their own and the bottle is pointed at it with GST_PLUGIN_PATH. That is a
  # per-bottle arrangement with a per-engine directory behind it, and this
  # project now also builds engines that carry the three inside
  # lib64/gstreamer-1.0, where CrossOver's own 17 live. For those, staging would
  # put a second copy of each plugin on the search path -- two GStreamer cores in
  # one process is the crash this whole script is written to avoid.
  #
  # So: look, and if they are already there, say so and do nothing.
  #
  # This check was first written at the top of the function, before VER and APP
  # were read from the arguments, and testing "$ENGINE" -- which is the engine's
  # NAME, not a path. Under set -u the unset variable aborted the whole script on
  # its first call, so staging was unreachable for every engine and not merely
  # wrong for one. It belongs after $CX exists, which is the path it wanted.
  if [ -d "$CX/lib64/gstreamer-1.0" ]; then
    have=0
    for plug in libgstlibav libgstmatroska libgstvpx; do
      [ -f "$CX/lib64/gstreamer-1.0/$plug.dylib" ] && have=$((have + 1))
    done
    if [ "$have" = 3 ]; then
      echo "engine    : $ENGINE"
      echo "            carries libgstlibav, libgstmatroska and libgstvpx already."
      echo "            Nothing staged, and no GST_PLUGIN_PATH is needed for a bottle"
      echo "            on this engine."
      return 0
    fi
  fi
  # A staging built from a different GStreamer series does not register, and a
  # plugin that does not register is worse than no plugin at all: the directory
  # exists, the bottle points at it, the app reports it staged, and the decoder
  # is simply absent. Measured, not feared -- the shared plugin registry on this
  # machine came back from a rebuild with zero avdec_* entries under a 1.28
  # engine, which is why a title that had been playing stopped after it changed
  # codec and nothing anywhere said why.
  #
  # This used to be a note that said "usually fine". It is not.
  eng_core="$CX/lib/$ARCH/libgstreamer-1.0.0.dylib"
  [ -f "$eng_core" ] || eng_core="$CX/lib64/libgstreamer-1.0.0.dylib"
  if [ -n "${gst_minor:-}" ] && [ -f "$eng_core" ]; then
    eng_compat="$(otool -L "$eng_core" 2>/dev/null \
                  | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1)"
    if [ -n "$eng_compat" ] && [ "$((eng_compat / 100))" != "$gst_minor" ]; then
      echo "  skipped   : $ENGINE ($VER) runs GStreamer 1.$((eng_compat / 100)), this framework is 1.${gst_minor}" >&2
      echo "              A plugin from another series does not register. Install a" >&2
      echo "              GStreamer 1.$((eng_compat / 100)) runtime and run this again." >&2
      return 0
    fi
  fi

  SRC="$CX/lib/$ARCH"
  # lib64 is the old single-architecture layout, and it holds x86_64. Falling
  # back to it for aarch64 would fill an ARM directory with x86_64 libraries
  # that resolve at stage time and fail at load.
  if [ ! -d "$SRC" ] && [ "$ARCH" = x86_64 ]; then SRC="$CX/lib64"; fi
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
    # Replace this engine's line rather than append to it. The rebuild path
    # further down already did this; this one did not, so re-staging a single
    # engine that was already built appended a second answer for the same
    # version -- and the map is what tells a bottle which directory is its own.
    if [ -f "$ROOT/.map" ]; then
      awk -F'|' -v p="$OUT/gstreamer-1.0" '$3 != p' "$ROOT/.map" > "$ROOT/.map.new" 2>/dev/null || true
      mv "$ROOT/.map.new" "$ROOT/.map"
    fi
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

  # The plugins themselves, and ffmpeg, which is the whole point of taking the
  # first one.
  #
  # libgstlibav is the decoder half: VC-1, WMV3, WMA, and VP9 as avdec_vp9.
  #
  # libgstmatroska is the container half, and it is here because a decoder with
  # nothing to feed it is not a repair. CrossOver ships demuxers for asf, avi,
  # isomp4 and wav, and none for Matroska -- so a WebM reaches typefind, is
  # correctly identified as webm, and then finds no matroskademux to hand it to.
  # Media Foundation reports that as MF_E_UNSUPPORTED_BYTESTREAM_TYPE, which
  # reads like a codec problem and is not one. gst-libav does not cover it:
  # it deliberately does not register avdemux_matroska, because gst-plugins-good
  # owns that format. Ninja Gaiden 4 is where this was measured -- its first
  # video is a Matroska stream, and every part of the chain except the demuxer
  # was already present.
  PLUGINS="libgstlibav.dylib libgstmatroska.dylib"
  for plugin in $PLUGINS; do
    [ -f "$FRAMEWORK/lib/gstreamer-1.0/$plugin" ] || {
      echo "  warning: $plugin not in this framework -- skipped" >&2
      continue
    }
    cp "$FRAMEWORK/lib/gstreamer-1.0/$plugin" "$TMP/gstreamer-1.0/"
  done

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

  # Seed the walk from every plugin staged above, not from one of them. They do
  # not need the same libraries -- libgstmatroska wants libz and libgstpbutils
  # where libgstlibav wants ffmpeg -- and a walk seeded from a single plugin
  # leaves the other one short a dependency and silently unloadable.
  pending=""
  for plugin in "$TMP"/gstreamer-1.0/*.dylib; do
    [ -e "$plugin" ] || continue
    pending="$pending $(needed "$plugin")"
  done
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
  #
  # Keyed on the staging directory, not the version. Two engines can declare
  # the same CFBundleVersion -- a patched copy always does -- and keying the
  # replacement on the version made the second one delete the first one's line
  # instead of adding its own.
  if [ -f "$ROOT/.map" ]; then
    awk -F'|' -v p="$OUT/gstreamer-1.0" '$3 != p' "$ROOT/.map" > "$ROOT/.map.new" 2>/dev/null || true
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
# Truncated once, before any architecture runs. Doing it inside the loop made
# the second pass delete the first one's lines, and the map is what tells a
# bottle which directory is its own.
[ -n "$WANT" ] || : > "$ROOT/.map"

case "$ARCH" in
  all|"") ARCHES="x86_64 aarch64" ;;
  *)      ARCHES="$ARCH" ;;
esac

for ARCH in $ARCHES; do
  echo
  echo "=== $ARCH ==="
  printf '%s\n' "$ENGINES" | while IFS='|' read -r ver app; do
    [ -n "$ver" ] || continue
    echo
    stage_one "$ver" "$app"
  done
done

# The map is what the app reads to point each bottle at its own engine: a
# bottle's cxbottle.conf records the CFBundleVersion that last updated it, and
# that is the first field here.
echo
echo "staged per engine:"
grep -v '^$' "$ROOT/.map" 2>/dev/null | while IFS='|' read -r ver name path; do
  printf '  %-16s %-20s %s\n' "$ver" "$name" "$path"
done
