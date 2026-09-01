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
PLACED=""
BOTTLE_PATH=""      # required: a path, or the word "default". Never guessed.
NO_AUTOUPDATE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --from)         FROM="$2"; shift 2 ;;
    --from-archive) ARCHIVE="$2"; shift 2 ;;
    --name)         NAME="$2"; shift 2 ;;
    --gptk)         GPTK="$2"; shift 2 ;;
    --bottle-path)  BOTTLE_PATH="$2"; shift 2 ;;
    --no-autoupdate) NO_AUTOUPDATE=1; shift ;;
    --force)        FORCE=1; shift ;;
    --check)        CHECK=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

DEST="$HOME/Applications/$NAME"
say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# --bottle-path is required and has no default, and that is the whole point.
#
# CX_BOTTLE_PATH is not a configuration line we also set: it IS the isolation.
# Stock CrossOver ships no [EnvironmentVariables] section at all, so an engine
# without the key falls through to CrossOver's own default root -- which on this
# machine holds the user's seven bottles, including ones this project has never
# touched. An engine built for a launcher and handed that default would operate
# on somebody else's bottles without a word: not a degraded state, a wrong one
# that looks normal.
#
# A default of any kind is a guess about where a person's bottles live, and the
# failure mode of guessing wrong is silent. So it must be stated. Saying
# "default" is a statement too -- it means "use CrossOver's own root, and I know
# which one that is" -- and it is spelled out below rather than assumed.
[ -n "$BOTTLE_PATH" ] || die "--bottle-path is required.
       Give a directory for this engine's bottles, or the word \"default\" to
       use CrossOver's own root at ~/Library/Application Support/CrossOver/Bottles.
       There is no default because guessing wrong is silent: an engine without
       the key operates on whatever bottles CrossOver finds, including ones this
       project never touched."

# --- the toolkit backups a launcher restores from -----------------------------
#
# RaccoonBot's installd3dMetal restores every .orig under apple_gptk/wine before
# it installs a generation, then lets its own copy step re-create them. That
# cycle is self-maintaining only if the FIRST set is true stock. If it finds no
# .orig it treats whatever is there as the engine's own and saves that -- so a
# copy handed over without these enshrines OUR --gptk choice as "the engine's
# own", permanently and silently, on the first launch.
#
# Two things about this that are one word apart and only one of them is right:
#
#   "back up before you write" -- passes any test that checks a .orig exists,
#                                 and is wrong the moment we have touched the
#                                 directory, because it preserves our choice
#                                 under a name that claims to be CodeWeavers'.
#   "back up from the source"  -- reads them out of the bundle this copy was
#                                 MADE from. That is the only version that is
#                                 true regardless of what we do afterwards.
#
# The six unix entries are SYMLINKS into ../../external. Measured: cp with no
# flags resolves them and yields a 95,952-byte regular file where a 33-byte link
# belongs, and the launcher's restore is a rename, so that copy would be moved
# into place and libd3dshared.dylib would exist twice with nothing linking them.
# Hence cp -a, and hence the assertion afterwards rather than trusting the flag.
#
# Names are DESTINATION names: generation 4 ships nvngx-on-metalfx and is renamed
# on the way in, so the backup is nvngx.*.orig and never nvngx-on-metalfx.*.orig.
# Derived, not listed. Stock CrossOver 26.3 ships d3d11, d3d12, dxgi, nvapi64,
# nvngx and atidxx64 under apple_gptk/wine -- no d3d10, which a hand-written list
# taken from a patched engine does include, and no atidxx64, which that same list
# omits. A fixed list is wrong in both directions the moment two engines differ,
# so back up whatever is actually there.
lay_orig_backups() {
  src=$1        # a CrossOver dir known to be stock
  dst=$2        # the CrossOver dir inside our copy
  laid=0; kept=0; links=0

  for arch in x86_64-unix x86_64-windows; do
    d="$src/lib64/apple_gptk/wine/$arch"
    [ -d "$d" ] || continue
    mkdir -p "$dst/lib64/apple_gptk/wine/$arch"
    for from in "$d"/*; do
      [ -e "$from" ] || [ -L "$from" ] || continue
      case "$(basename "$from")" in *.orig) continue ;; esac
      to="$dst/lib64/apple_gptk/wine/$arch/$(basename "$from").orig"
      if [ -e "$to" ] || [ -L "$to" ]; then kept=$((kept + 1)); continue; fi
      cp -a "$from" "$to" || die "could not lay $(basename "$to")"
      laid=$((laid + 1))
      [ -L "$to" ] && links=$((links + 1))
    done
  done

  # external is a whole-directory backup, and libMoltenVK sits outside apple_gptk
  # entirely -- easy to miss, and missing it is silent.
  if [ -d "$src/lib64/apple_gptk/external" ] && [ ! -e "$dst/lib64/apple_gptk/external.orig" ]; then
    /usr/bin/ditto "$src/lib64/apple_gptk/external" "$dst/lib64/apple_gptk/external.orig"
    laid=$((laid + 1))
  fi
  if [ -e "$src/lib64/libMoltenVK.dylib" ] && [ ! -e "$dst/lib64/libMoltenVK.dylib.orig" ]; then
    cp -a "$src/lib64/libMoltenVK.dylib" "$dst/lib64/libMoltenVK.dylib.orig"
    laid=$((laid + 1))
  fi

  # Asserted, not assumed: anything that was a link must still be one. If cp ever
  # resolves them the restore installs a fat duplicate and nothing complains.
  bad=0
  for arch in x86_64-unix x86_64-windows; do
    for f in "$dst/lib64/apple_gptk/wine/$arch"/*.orig; do
      [ -e "$f" ] || continue
      orig="${f%.orig}"
      src_f="$src/lib64/apple_gptk/wine/$arch/$(basename "$orig")"
      if [ -L "$src_f" ] && [ ! -L "$f" ]; then
        say "      $(basename "$f") is a regular file where stock has a link"
        bad=$((bad + 1))
      fi
    done
  done
  [ "$bad" = 0 ] || die "toolkit backups were resolved instead of linked; a restore would break the engine"
  say "      $laid backup(s) laid from stock, $kept already present, $links of them links"
}

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

# Seeded here and nowhere later, because THIS is the only instant at which the
# copy is known to be stock: nothing below has run yet. Prefer the source bundle
# when we have one, since it is a thing we never write to at all; fall back to
# the copy itself, which is only true at this line and would quietly stop being
# true if a step were inserted above.
if [ -n "$ARCHIVE" ]; then
  lay_orig_backups "$CX" "$CX"
else
  lay_orig_backups "$FROM/Contents/SharedSupport/CrossOver" "$CX"
fi

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

  # The swap made a fresh apple_gptk, so the set laid down after the copy went
  # with the old directory into apple_gptk_bak. Lay it again, still from stock,
  # or a launcher restoring from .orig would find nothing and enshrine what we
  # just installed. apple_gptk_bak is itself stock, so it is a valid source and
  # is the one that still exists if --from-archive was used.
  if [ -d "$BAK" ]; then
    for arch in x86_64-unix x86_64-windows; do
      [ -d "$BAK/wine/$arch" ] || continue
      mkdir -p "$DST/wine/$arch"
      for f in "$BAK/wine/$arch"/*; do
        [ -e "$f" ] || [ -L "$f" ] || continue
        case "$(basename "$f")" in *.orig) continue ;; esac
        t="$DST/wine/$arch/$(basename "$f").orig"
        [ -e "$t" ] || [ -L "$t" ] || cp -a "$f" "$t"
      done
    done
    [ -e "$DST/external.orig" ] || [ ! -d "$BAK/external" ] || /usr/bin/ditto "$BAK/external" "$DST/external.orig"
    say "      toolkit backups re-laid into the new apple_gptk"
  fi

  # --- what this install PLACED, as opposed to what it displaced --------------
  #
  # A backup answers "what was here before". It cannot answer "what did we add
  # that was never here", and that is a real state: generation 4 ships d3d10 and
  # CrossOver does not, so installing 4 creates a file with no backup, and a
  # restore gives back what was displaced and cannot give back an absence. The
  # engine then reports as generation 3 while holding one file of 4, and nothing
  # anywhere says so.
  #
  # The obvious repair -- delete files with no .orig whose names belong to the
  # other generation -- was written and withdrawn on the launcher's side because
  # it deleted CrossOver's OWN atidxx64 and nvngx: after a restore a stock file
  # has no backup either, and nothing on disk tells one apart from the other.
  # Names cannot answer this. Only a record written at the time can.
  #
  # apple_gptk_bak is the engine exactly as it was, so the difference between it
  # and the directory we just built IS the answer, with no inference involved.
  PLACED=""
  if [ -d "$BAK" ]; then
    for f in $(cd "$DST" && /usr/bin/find . \( -type f -o -type l \) 2>/dev/null | sed 's|^\./||' | sort); do
      case "$f" in *.orig|*/.orig) continue ;; esac
      if [ ! -e "$BAK/$f" ] && [ ! -L "$BAK/$f" ]; then
        PLACED="$PLACED$f
"
      fi
    done
  fi
  n_placed=$(printf '%s' "$PLACED" | /usr/bin/grep -c . || true)
  say "      $n_placed file(s) placed that the engine never had"
else
  say "[4/6] toolkit: unchanged"
fi

# --- 4b. the provenance, now that there is something to record ---------------
#
# Rewritten rather than appended, because step 2 had to write it before the
# engine media installer ran -- that installer recognises one of our copies by
# this file, so it has to exist first. What it could not know then is what the
# toolkit step would do.
#
# toolkit is READ FROM THE BINARY that ended up installed, not from the flag that
# asked for it. A record of what was requested and a record of what happened
# diverge exactly when somebody needs the truth, and this project has already
# paid for that once -- three titles were diagnosed against a toolkit generation
# that was not the one running.
TOOLKIT="$(/usr/bin/defaults read "$CX/lib64/apple_gptk/external/D3DMetal.framework/Resources/Info" CFBundleShortVersionString 2>/dev/null || true)"
{
  printf '{\n'
  printf '  "made_by": "MacGameVideoFix",\n'
  printf '  "made_on": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf '  "copied_from": "%s",\n' "$([ -n "$ARCHIVE" ] && echo "CrossOver.app" || basename "$FROM")"
  printf '  "engine_version": "%s",\n' "$(/usr/bin/defaults read "$DEST/Contents/Info.plist" CFBundleVersion 2>/dev/null)"
  printf '  "source": "%s",\n' "$([ -n "$ARCHIVE" ] && basename "$ARCHIVE" || echo "$FROM")"
  printf '  "toolkit": "%s",\n' "${TOOLKIT:-unknown}"
  printf '  "bottles": "%s",\n' "$BOTTLE_PATH"
  printf '  "placed": ['
  first=1
  printf '%s' "$PLACED" | while IFS= read -r f; do
    [ -n "$f" ] || continue
    [ "$first" = 1 ] || printf ','
    printf '\n    "%s"' "$f"
    first=0
  done
  [ -n "$(printf '%s' "$PLACED" | /usr/bin/grep -c . || true)" ] && printf '\n  '
  printf '],\n'
  printf '  "placedNote": "Files this copy created that the engine never had, so a restore cannot remove them by putting something back. Read this to clean up precisely; do not infer from names, because after a restore CrossOver'"'"'s own files have no backup either.",\n'
  printf '  "why": "A copy, so the CrossOver a person bought stays exactly as CodeWeavers shipped it.",\n'
  printf '  "order": "Patch first, sign second, clear extended attributes last."\n'
  printf '}\n'
} > "$CX/mgvf-origin.json"

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

# --- 5b. the two things a launcher needs set before it is signed -------------
#
# Both edit files inside the bundle, so both must happen before [6/6]: signing
# after every change is the order that stops Finder calling the copy damaged.
if [ "$BOTTLE_PATH" = "default" ]; then
  say "      bottles   : CrossOver's own root (no CX_BOTTLE_PATH written)"
else
  CONF="$CX/etc/CrossOver.conf"
  [ -f "$CONF" ] || die "the copy has no etc/CrossOver.conf to write CX_BOTTLE_PATH into"
  # Strip then insert, for the reason the app's own config writer does it: a key
  # left outside its section, or written twice, gives the file two answers and
  # therefore none. Stock ships no [EnvironmentVariables] at all, so it is added.
  /usr/bin/sed -i '' '/^"CX_BOTTLE_PATH"[[:space:]]*=/d' "$CONF"
  /usr/bin/grep -q '^\[EnvironmentVariables\]' "$CONF" || printf '[EnvironmentVariables]\n' >> "$CONF"
  printf '"CX_BOTTLE_PATH" = "%s"\n' "$BOTTLE_PATH" >> "$CONF"
  say "      bottles   : $BOTTLE_PATH"
fi

if [ "$NO_AUTOUPDATE" = 1 ]; then
  /usr/bin/defaults write "$DEST/Contents/Info" SUFeedURL "" 2>/dev/null \
    || die "could not blank SUFeedURL"
  /bin/chmod 644 "$DEST/Contents/Info.plist" 2>/dev/null || true
  say "      updates   : SUFeedURL blanked"
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
# Read back, because this is the one whose absence produces no error anywhere
# downstream: everything keeps working and quietly works on the wrong bottles.
if [ "$BOTTLE_PATH" = "default" ]; then
  /usr/bin/grep -q '^"CX_BOTTLE_PATH"' "$CX/etc/CrossOver.conf" 2>/dev/null \
    && { say "  bottles   : a CX_BOTTLE_PATH is present and none was asked for"; fail=1; } \
    || say "  bottles   : CrossOver's own root, as asked"
else
  got=$(/usr/bin/sed -n 's/^"CX_BOTTLE_PATH"[[:space:]]*=[[:space:]]*"\(.*\)"/\1/p' "$CX/etc/CrossOver.conf" 2>/dev/null | tail -1)
  [ "$got" = "$BOTTLE_PATH" ] && say "  bottles   : $got" \
    || { say "  bottles   : asked for '$BOTTLE_PATH', reads back '$got'"; fail=1; }
fi
if [ "$NO_AUTOUPDATE" = 1 ]; then
  u=$(/usr/bin/defaults read "$DEST/Contents/Info" SUFeedURL 2>/dev/null)
  [ -z "$u" ] && say "  updates   : off" || { say "  updates   : SUFeedURL still $u"; fail=1; }
fi
v=$("$CX/bin/wineloader" --version 2>&1 | head -1 || true)
[ -n "$v" ] && say "  wine      : $v" || { say "  wine      : NO OUTPUT -- it is being killed, do not hand this to anyone"; fail=1; }
say ""
[ "$fail" = 0 ] || die "the copy is not usable; the checks above say why"
say "ready: $DEST"
say "Open it once from Finder, pick a bottle, and it runs with this engine."
