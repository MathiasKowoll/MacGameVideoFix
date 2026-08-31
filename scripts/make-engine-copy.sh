#!/usr/bin/env bash
#
# Make a patched copy of CrossOver, leaving the original untouched.
#
#     scripts/make-engine-copy.sh [--from <CrossOver.app>|--from-archive <zip>]
#                                 [--name <Name.app>] [--gptk <apple_gptk_4 dir>]
#                                 [--force] [--check]
#
# WHY A COPY. The CrossOver a person paid for stays byte-identical to what
# CodeWeavers shipped. Everything this project changes in an engine is changed
# here instead, so a support question about a modified engine is separable from
# one about theirs, and the original is always there to reproduce against.
#
# THE ORDER IS THE WHOLE TRICK, and getting it wrong costs an hour:
#
#     copy -> every change -> codesign -> xattr -cr
#
# Signing before the last change leaves a seal the next change breaks, and Finder
# then says "is damaged and can't be opened. This file was downloaded on an
# unknown date." Nothing is damaged: that is Gatekeeper's wording for a signature
# that does not validate, and "unknown date" only means there is no download
# timestamp. It is not about the date. Clearing attributes before signing does
# not help either -- quarantine has to be gone at the END, because Finder checks
# it when the app is opened.
#
# WHAT GOES IN. The winegstreamer pair this project builds; the three GStreamer
# plugins CrossOver does not ship, placed in the engine's own lib64/gstreamer-1.0
# so that no bottle needs GST_PLUGIN_PATH; and, if a source is given, Apple's
# D3DMetal 4 over the engine's apple_gptk -- with BOTH halves backed up, which is
# more than the launchers here do, so that reverting really reverts.
#
# WHAT DOES NOT GO IN. Nothing of Apple's is carried by this project. --gptk
# names a directory already on the machine; without it the copy keeps the
# toolkit CrossOver shipped.
#
# Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# Two layouts, because this runs from two places. In the repository it sits in
# scripts/ with runtime/ beside it; inside the app it sits flat in Resources with
# everything else. Rather than assume, look for the installer and take the layout
# from where it turns out to be.
if [ -f "$HERE/../runtime/install-engine-media.sh" ]; then
  ENGINE_INSTALLER="$(cd "$HERE/.." && pwd)/runtime/install-engine-media.sh"
  PAYLOAD="$(cd "$HERE/.." && pwd)/runtime/engine-payload/lib64"
elif [ -f "$HERE/install-engine-media.sh" ]; then
  ENGINE_INSTALLER="$HERE/install-engine-media.sh"
  PAYLOAD="$HERE"
else
  echo "error: install-engine-media.sh is not beside this script or in runtime/" >&2
  exit 1
fi
FROM="/Applications/CrossOver.app"
ARCHIVE=""
NAME="Crossover_MGVF.app"
GPTK=""
FORCE=0
CHECK=0

while [ $# -gt 0 ]; do
  case "$1" in
    --from)         FROM="$2"; shift 2 ;;
    --from-archive) ARCHIVE="$2"; shift 2 ;;
    --name)         NAME="$2"; shift 2 ;;
    --gptk)         GPTK="$2"; shift 2 ;;
    --force)        FORCE=1; shift ;;
    --check)        CHECK=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

DEST="$HOME/Applications/$NAME"
say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# --- what we are about to do -------------------------------------------------
if [ -n "$ARCHIVE" ]; then
  [ -f "$ARCHIVE" ] || die "no archive at $ARCHIVE"
  say "source    : $ARCHIVE (archive)"
else
  [ -d "$FROM/Contents/SharedSupport/CrossOver" ] || die "no CrossOver at $FROM"
  say "source    : $FROM"
  codesign --verify "$FROM" >/dev/null 2>&1 \
    || say "            note: this install's seal is already broken, so the copy inherits whatever was done to it."
fi
say "copy      : $DEST"
[ -n "$GPTK" ] && say "toolkit   : $GPTK" || say "toolkit   : left as CrossOver shipped it"
[ "$CHECK" = 1 ] && { say "--check: nothing was created."; exit 0; }

[ -e "$DEST" ] && [ "$FORCE" = 0 ] && die "$DEST already exists (pass --force to replace it)"

# --force means "replace what is there", and step 1 removes the destination
# before copying into it. If the source IS the destination -- which is what
# choosing the copy instead of the original gives you on a second run, since
# the copy appears in the picker too -- that removes the source, and then
# there is nothing left to copy from. Compare the resolved paths, not the
# strings, so a symlink or a trailing slash cannot walk around it.
if [ -z "$ARCHIVE" ] && [ -d "$FROM" ] && [ -d "$DEST" ] \
   && [ "$(cd "$FROM" && pwd -P)" = "$(cd "$DEST" && pwd -P)" ]; then
  die "the source and the copy are the same bundle: $DEST
       Point this at the CrossOver you installed, not at a copy of it."
fi

# --- 1. copy -----------------------------------------------------------------
say "[1/6] copying"
rm -rf "$DEST"
mkdir -p "$HOME/Applications"
if [ -n "$ARCHIVE" ]; then
  TMP="$HOME/Applications/.mgvf-extract.$$"
  rm -rf "$TMP"; mkdir -p "$TMP"
  unzip -q "$ARCHIVE" -d "$TMP"
  inner="$(find "$TMP" -maxdepth 1 -name "*.app" | head -1)"
  [ -n "$inner" ] || die "the archive has no .app at its root"
  mv "$inner" "$DEST"; rm -rf "$TMP"
else
  /usr/bin/ditto "$FROM" "$DEST"
fi
CX="$DEST/Contents/SharedSupport/CrossOver"
[ -d "$CX" ] || die "the copy has no Contents/SharedSupport/CrossOver"

# --- 2. say where it came from ----------------------------------------------
# The installer refuses an engine it cannot identify, and a copy answers to a
# different name than the engine its binaries were built for. This is what lets
# it be recognised without weakening that refusal.
say "[2/6] provenance"
cat > "$CX/mgvf-origin.json" <<EOF
{
  "made_by": "MacGameVideoFix",
  "made_on": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
  "copied_from": "$([ -n "$ARCHIVE" ] && echo "CrossOver.app" || basename "$FROM")",
  "engine_version": "$(/usr/bin/defaults read "$DEST/Contents/Info.plist" CFBundleVersion 2>/dev/null)",
  "source": "$([ -n "$ARCHIVE" ] && basename "$ARCHIVE" || echo "$FROM")",
  "why": "A copy, so the CrossOver a person bought stays exactly as CodeWeavers shipped it.",
  "order": "Patch first, sign second, clear extended attributes last."
}
EOF

# --- 3. the engine media pair ------------------------------------------------
say "[3/6] winegstreamer"
"$ENGINE_INSTALLER" "$DEST" 2>&1 | sed 's/^/      /'

# --- 4. the toolkit, if a source was named -----------------------------------
if [ -n "$GPTK" ]; then
  say "[4/6] toolkit"
  [ -d "$GPTK" ] || die "no toolkit directory at $GPTK"
  DST="$CX/lib64/apple_gptk"
  BAK="$CX/lib64/apple_gptk_bak"
  # The whole directory is moved aside, not each half separately.
  #
  # Apple's instructions rename external and wine one at a time, and doing it
  # that way here produced a copy the app could not undo: its Restore button
  # looks for apple_gptk_bak, which is how it has always saved a toolkit. Two
  # schemes for the same thing means one of them silently does not work, and the
  # one that fails is always the revert -- discovered when it is needed.
  #
  # Moving the directory also keeps both halves together by construction, which
  # is the property that matters: a revert that restores external and leaves the
  # new wine in place reports success and leaves half the new toolkit running.
  if [ ! -d "$BAK" ]; then
    mv "$DST" "$BAK" || die "could not set the engine's own toolkit aside"
    say "      the engine's own toolkit kept as apple_gptk_bak"
  else
    say "      apple_gptk_bak already holds the engine's own toolkit; not overwritten"
    rm -rf "$DST"
  fi
  mkdir -p "$DST"
  for half in external wine; do
    [ -d "$GPTK/$half" ] || continue
    /usr/bin/ditto "$GPTK/$half" "$DST/$half"
    say "      $half installed"
  done
else
  say "[4/6] toolkit: unchanged"
fi

# --- 5. the codecs, inside the engine ----------------------------------------
# In the engine rather than staged beside a bottle: one place instead of one per
# bottle, no GST_PLUGIN_PATH to write, and no second GStreamer core on the search
# path -- which is the crash the staging arrangement exists to avoid.
say "[5/6] codecs"
PL="$PAYLOAD"
# In the repository the three plugins are under gstreamer-1.0/ and their support
# libraries one level out; in the app everything is flat in Resources. The three
# are named, so flat costs nothing: a plugin goes where GStreamer scans and a
# support library goes where the plugin's @rpath finds it.
plugin_names="libgstlibav libgstmatroska libgstvpx"
staged=0
if [ -d "$PL/gstreamer-1.0" ]; then
  for f in "$PL/gstreamer-1.0"/*.dylib; do cp "$f" "$CX/lib64/gstreamer-1.0/"; staged=$((staged + 1)); done
  for f in "$PL"/*.dylib; do [ -f "$CX/lib64/$(basename "$f")" ] || cp "$f" "$CX/lib64/"; done
elif [ -f "$PL/libgstlibav.dylib" ]; then
  for f in "$PL"/*.dylib; do
    base="$(basename "$f" .dylib)"
    case " $plugin_names " in
      *" $base "*) cp "$f" "$CX/lib64/gstreamer-1.0/"; staged=$((staged + 1)) ;;
      *) [ -f "$CX/lib64/$(basename "$f")" ] || cp "$f" "$CX/lib64/" ;;
    esac
  done
fi
if [ "$staged" -gt 0 ]; then
  say "      $(ls -1 "$CX/lib64/gstreamer-1.0"/*.dylib | wc -l | tr -d ' ') plugins in the engine"
else
  say "      none in runtime/engine-payload -- skipped"
fi

# --- 6. sign, then clear attributes, in that order ---------------------------
say "[6/6] signing"
/usr/bin/codesign --force --deep --sign - "$DEST" 2>&1 | sed 's/^/      /'
/usr/bin/xattr -cr "$DEST" 2>/dev/null || true

# --- the three checks that must pass before anyone is handed this ------------
say ""
fail=0
codesign --verify "$DEST" >/dev/null 2>&1 && say "  signature : valid (ad-hoc)" || { say "  signature : INVALID"; fail=1; }
q=$(xattr -r "$DEST" 2>/dev/null | grep -c quarantine || true)
[ "$q" = 0 ] && say "  quarantine: none" || { say "  quarantine: $q left"; fail=1; }
v=$("$CX/bin/wineloader" --version 2>&1 | head -1 || true)
[ -n "$v" ] && say "  wine      : $v" || { say "  wine      : NO OUTPUT -- it is being killed, do not hand this to anyone"; fail=1; }
say ""
[ "$fail" = 0 ] || die "the copy is not usable; the checks above say why"
say "ready: $DEST"
say "Open it once from Finder, pick a bottle, and it runs with this engine."
