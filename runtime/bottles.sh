#!/usr/bin/env bash
#
# Where bottles are, and which engine can open each one.
#
# Sourced, never run. Two functions that every installer here needs and that the
# diagnostic tools need for the same reason: since fixes are now raised against
# Procyon rather than a stock CrossOver, "the bottle" and "the engine" stopped
# being one obvious answer each.
#
#     . "$HERE/bottles.sh"
#
# It lives in one file because it was already in two, byte for byte, and a third
# copy was about to appear in diagnostics/. The manifest generator stages any
# $HERE/<file> an installer names, so this travels in the fixes bundle beside
# the installers that source it.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

# Where bottles live. Not one directory -- a Mac can hold several roots at once.
#
# CrossOver's own root is configurable through its BottleDir preference, and a
# launcher that patches a copy of CrossOver redirects its bottles somewhere else
# entirely with CX_BOTTLE_PATH; Procyon puts them under its own support folder.
# Looking only in the default root made this script report a fix as installed
# while the override went nowhere the game would ever read it -- the DLL sat
# beside the game, Wine kept preferring its own, and nothing said so.
#
# MGVF_BOTTLES adds a root explicitly, for anything neither of those finds.
bottle_roots() {
  local r seen=""
  for r in \
    "${MGVF_BOTTLES:-}" \
    "$(defaults read com.codeweavers.CrossOver BottleDir 2>/dev/null || true)" \
    "$HOME/Library/Application Support/CrossOver/Bottles" \
    "$HOME/Library/Application Support/Procyon/CXPBottles"
  do
    [ -n "$r" ] || continue
    r="${r%/}"
    [ -d "$r" ] || continue
    case "$seen" in *"|$r|"*) continue ;; esac
    seen="$seen|$r|"
    printf '%s\n' "$r"
  done
}

# The CrossOver that can actually open a given bottle.
#
# A bottle records the CFBundleVersion of the engine that last updated it, and
# an engine refuses a bottle newer than itself SILENTLY -- exit 0, no output,
# which is the worst possible way for a registry write to fail. So picking one
# CrossOver for the whole machine is wrong wherever more than one is installed:
# it writes the override into whichever bottles happen to match, skips the rest
# without a word, and counts the ones it skipped as written.
crossover_for_bottle() {
  local want a ver root parent
  want="$(sed -n 's/^"Version" = "\(.*\)"$/\1/p' "$1/cxbottle.conf" 2>/dev/null | head -1)"
  [ -n "$want" ] || return 1
  parent="$(cd "$(dirname "$1")" && pwd)"

  # First pass: the engine whose OWN bottle root holds this bottle.
  #
  # Matching on CFBundleVersion alone is not enough, and the failure is silent.
  # A patched copy of a CrossOver declares the same version as the original it
  # was copied from -- this machine has three engines all declaring 27.0.0.40921
  # -- and only the one whose etc/CrossOver.conf redirects CX_BOTTLE_PATH at a
  # given root can open bottles there. Measured: stock Preview cannot even query
  # HKCU\Software in a bottle under another product's root, while the patched
  # copy writes and reads it.
  #
  # And the wrong engine does not fail loudly. `--bottle <name>` falls back to
  # its own root, where a bottle of the same name may well exist and may well
  # already hold the key -- so the write goes somewhere else and the check that
  # follows passes against the wrong registry.
  for a in /Applications/*.app "$HOME"/Applications/*.app; do
    [ -x "$a/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    root="$(sed -n 's/^"CX_BOTTLE_PATH" = "\(.*\)"$/\1/p' \
            "$a/Contents/SharedSupport/CrossOver/etc/CrossOver.conf" 2>/dev/null | head -1)"
    [ -n "$root" ] || continue
    [ "${root%/}" = "$parent" ] || continue
    printf '%s' "$a/Contents/SharedSupport/CrossOver"; return 0
  done

  # Second pass: the version, which is right for bottles in the default root.
  for a in /Applications/*.app "$HOME"/Applications/*.app; do
    [ -x "$a/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    ver="$(defaults read "$a/Contents/Info" CFBundleVersion 2>/dev/null)"
    [ "$ver" = "$want" ] || continue
    printf '%s' "$a/Contents/SharedSupport/CrossOver"; return 0
  done
  return 1
}

# A bottle by name, across every root, refusing to guess.
#
# The name is not unique, and it is not ours to predict: each user names their
# own bottles. Two roots can hold one that differs only in case, and macOS
# filesystems do not distinguish those, so a
# first-match-wins lookup answered "Procyon's ARM bottle" with CrossOver's --
# silently, which is the same failure that had a fix writing its override into
# the wrong bottle and then verifying against it. An absolute path is taken as
# given; an ambiguous name is an error that lists what it could have meant.
find_bottle_dir() {
  local root hits="" n=0
  case "$1" in
    /*) [ -d "$1" ] && { printf '%s' "${1%/}"; return 0; }; return 1 ;;
  esac
  while IFS= read -r root; do
    [ -d "$root/$1" ] || continue
    hits="$hits$root/$1"$'\n'
    n=$((n + 1))
  done < <(bottle_roots)
  [ "$n" = 0 ] && return 1
  if [ "$n" -gt 1 ]; then
    echo "error: '$1' names more than one bottle. Give the full path:" >&2
    printf '%s' "$hits" | sed 's/^/       /' >&2
    return 2
  fi
  printf '%s' "${hits%$'\n'}"
}
