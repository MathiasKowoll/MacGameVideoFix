#!/usr/bin/env bash
#
# Install (or remove) the winegstreamer pair this project builds, into a
# CrossOver engine.
#
#   install-engine-media.sh <engine app>            install
#   install-engine-media.sh <engine app> --restore  remove
#   install-engine-media.sh <engine app> --status   report what is in place
#
# <engine app> is the .app itself, e.g.
#   ~/Applications/Crossover_patched.app
#
# WHAT THIS IS, AND WHY IT IS NOT LIKE THE OTHERS.
#
# Every other installer here writes into a game folder or into a bottle. This
# one writes into the ENGINE, which every bottle and every game on that engine
# shares. That is a wider blast radius than anything else in this project, and
# it is why this refuses to run against an engine it was not built for rather
# than trying and hoping.
#
# Two halves, and they are not separable: winegstreamer is a PE DLL and a Unix
# .so that talk to each other across an interface that changes between wine
# revisions. Installing one without the other, or either onto a different wine,
# is how a working engine stops loading media at all.
#
# WHY IT EXISTS. Codecs that were present would not show. The pair was built
# from wine source with the patches recorded in engine-built-for.json, against
# one engine version and one wine revision, both named there.
#
# WHAT IS NOT HERE. The d3d9.dll this project also built is deliberately absent.
# It was made with winevideo's 0008 and 0009 patches for the Nioh work and did
# not fix it -- docs/codecs-inside-the-engine.md recorded that the day it was
# built. The bridge those titles use ships in the game folder as
# p5s-video-bridge.c. Worse, installing it displaces d9vk, which RaccoonBot's
# own patcher puts there. Shipping a binary that does nothing and removes
# something that does is the opposite of a fix.
#
# MGVF-SCOPE: engine
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

usage() { sed -n '3,11p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

APP="${1%/}"
ACTION="${2:-install}"
# A read-only caller sets MGVF_STATUS_ONLY=1. The default above is the
# DESTRUCTIVE branch, so without this the read-only property of a survey rests
# on the literal --status never being lost from one line of one caller.
# Structural beats positional.
#
# Four installers were missing this while the other nine had it, so a launcher
# setting the variable got a read-only guarantee that silently did not cover
# them. Found by the RaccoonBot session, which set the variable and then went
# and checked which scripts actually read it rather than trusting that they did.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then ACTION=--status; fi


# Named literally so make-fixes-bundle.sh collects them.
# Which set of engine binaries, when more than one is here.
#
# The pair is compiled against one engine's libraries; installing it into a
# different engine is not degraded, it is an engine that may not load media at
# all. That is why a mismatch was refused rather than warned about. With one set
# present, refusing was the whole story. With two -- one for stock CrossOver and
# one for a patched fork -- refusing while the right one sits beside the wrong
# one would be obtuse. So: find the set built for THIS engine, and refuse only
# when there is none.
#
# A set is three files with a common suffix: engine-winegstreamer<S>.dll,
# engine-winegstreamer<S>.so and engine-built-for<S>.json. The empty suffix is a
# set like any other, so a bundle carrying only one behaves exactly as before.
choose_set() {
  want=$1
  for j in "$HERE"/engine-built-for*.json; do
    [ -f "$j" ] || continue
    app=$(/usr/bin/sed -n 's/.*"engine_app": *"\([^"]*\)".*/\1/p' "$j")
    [ "$app" = "$want" ] || continue
    sfx=$(basename "$j"); sfx=${sfx#engine-built-for}; sfx=${sfx%.json}
    if [ -f "$HERE/engine-winegstreamer$sfx.dll" ] && [ -f "$HERE/engine-winegstreamer$sfx.so" ]; then
      printf '%s\n' "$sfx"
      return 0
    fi
  done
  return 1
}

# A copy this project made is not a different engine; it is the same engine under
# another name, and the name is the only thing the guard can see. So the copy
# records where it came from, in a file written when it was made, and the set
# built for the original serves it. Nothing else is accepted: an engine with no
# marker and no matching name is still refused.
target_app=$(basename "$APP")
if ! choose_set "$target_app" >/dev/null 2>&1; then
  origin="$APP/Contents/SharedSupport/CrossOver/mgvf-origin.json"
  if [ -f "$origin" ]; then
    from=$(/usr/bin/sed -n 's/.*"copied_from": *"\([^"]*\)".*/\1/p' "$origin")
    if [ -n "$from" ] && choose_set "$from" >/dev/null 2>&1; then
      echo "note: $target_app records itself as a copy of $from made by this project;"
      echo "      using the set built for $from."
      target_app=$from
    fi
  fi
fi

if ! SUFFIX=$(choose_set "$target_app"); then
  echo "error: nothing here was built for $(basename "$APP")." >&2
  echo "       Sets present:" >&2
  for j in "$HERE"/engine-built-for*.json; do
    [ -f "$j" ] || continue
    echo "         $(/usr/bin/sed -n 's/.*"engine_app": *"\([^"]*\)".*/\1/p' "$j")  ($(basename "$j"))" >&2
  done
  echo "       Build one with scripts/build-winegstreamer.sh against this engine," >&2
  echo "       or point this at an engine these were built for." >&2
  exit 1
fi

PE="$HERE/engine-winegstreamer$SUFFIX.dll"
UNIX="$HERE/engine-winegstreamer$SUFFIX.so"
BUILTFOR="$HERE/engine-built-for$SUFFIX.json"

CX="$APP/Contents/SharedSupport/CrossOver"
PE_DEST="$CX/lib/wine/x86_64-windows/winegstreamer.dll"
UNIX_DEST="$CX/lib/wine/x86_64-unix/winegstreamer.so"

[ -d "$CX" ] || { echo "error: not a CrossOver app: $APP" >&2; exit 1; }

status() {
  if [ -f "$PE_DEST.mgvf-stock" ] && [ -f "$UNIX_DEST.mgvf-stock" ]; then
    echo installed
  elif [ -f "$PE_DEST.mgvf-stock" ] || [ -f "$UNIX_DEST.mgvf-stock" ]; then
    # One half in place is worse than neither: the two speak an interface that
    # changes between wine revisions, so a mismatched pair is how an engine
    # stops loading media at all.
    echo broken
  else
    echo absent
  fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      n=0
      for d in "$PE_DEST" "$UNIX_DEST"; do
        if [ -f "$d.mgvf-stock" ]; then mv -f "$d.mgvf-stock" "$d"; n=$((n+1)); fi
      done
      [ "$n" -gt 0 ] && echo "restored $n of 2" || echo "nothing to restore"
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

for f in "$PE" "$UNIX" "$BUILTFOR"; do
  [ -f "$f" ] || { echo "error: $(basename "$f") is not beside this script" >&2; exit 1; }
done

# Refuse an engine these were not built for.
#
# The pair is compiled against one wine revision. Installing it onto another is
# not a degraded experience, it is an engine that may not load media at all --
# and the failure would look like the fault it was built to repair.
want_engine="$(/usr/bin/sed -n 's/.*"engine_version": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
have_engine="$(/usr/bin/defaults read "$APP/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo "")"
if [ -z "$have_engine" ]; then
  echo "error: could not read a version out of $APP" >&2
  exit 1
fi
# The version alone is not enough, and finding that out cost a stock CrossOver
# being written to during a test. The patched fork and the stock application
# report the SAME CFBundleVersion -- 26.3.0.39832 for both -- so a version match
# says nothing about which of them this is. built-for.json records the app name
# for exactly this reason and it was being ignored.
want_app="$(/usr/bin/sed -n 's/.*"engine_app": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
# Compared against the identity resolved above, not the bundle's own name: a
# copy this project made answers to the engine it was copied from, and the
# check that follows exists to catch a set built for a DIFFERENT engine, not
# to reject a renamed one it already accepted.
have_app="$target_app"
if [ -n "$want_app" ] && [ "$want_app" != "$have_app" ]; then
  echo "error: these were built for $want_app and this is $have_app." >&2
  echo "       Both report the same version, so the name is the only thing that" >&2
  echo "       tells them apart. Refusing rather than writing into the wrong" >&2
  echo "       engine -- pass the app these were built for, or rebuild." >&2
  exit 1
fi
if [ "$want_engine" != "$have_engine" ]; then
  echo "error: these were built for engine $want_engine and this is $have_engine." >&2
  echo "       Refusing rather than installing a winegstreamer built against a" >&2
  echo "       different wine. Rebuild with scripts/build-winegstreamer.sh" >&2
  echo "       against this engine, or leave it alone." >&2
  exit 1
fi

install_one() {
  local src="$1" dest="$2"
  [ -f "$dest" ] || { echo "error: no $dest to replace" >&2; exit 1; }

  # Back up the ORIGINAL, and never our own build.
  #
  # This used to test only whether the backup already existed. On an engine
  # that already had our winegstreamer in it -- which is how this one got here,
  # installed by hand long before this script existed -- the very first run
  # saved the patched file as "stock". The result is a backup that is byte for
  # byte the patch, and a --restore that puts the patch back and reports
  # success. Anything downstream asking "is this engine clean?" by comparing
  # against .mgvf-stock then answers yes when it is not.
  #
  # Presence answers "have I run before". The question is "is what I am about
  # to overwrite the original", and only the content answers that.
  if [ -f "$dest.mgvf-stock" ]; then
    # An existing backup that IS our build is poisoned. Carrying on would leave
    # --restore lying, so it stops here rather than compounding it.
    if cmp -s "$src" "$dest.mgvf-stock"; then
      echo "error: $dest.mgvf-stock is this same build, not the original." >&2
      echo "       It was taken from an engine that already carried our copy, so" >&2
      echo "       --restore would reinstall the patch and report success." >&2
      echo "       Delete it, put the real original back at $dest, and re-run." >&2
      echo "       A CrossOver patcher usually leaves one as $dest.stock." >&2
      exit 1
    fi
  elif ! cmp -s "$src" "$dest"; then
    cp "$dest" "$dest.mgvf-stock"
  else
    echo "  note: $(basename "$dest") is already this build; no backup taken" >&2
  fi

  cp "$src" "$dest"
  echo "  $(basename "$dest")  <- $(basename "$src")"
}
install_one "$PE" "$PE_DEST"
install_one "$UNIX" "$UNIX_DEST"
echo "installed into $(basename "$APP") ($have_engine)"
