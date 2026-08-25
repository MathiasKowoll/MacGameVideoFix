#!/usr/bin/env bash
#
# Install the NieR Replicant ver.1.22474487139 video bridge.
#
#     install-nier-bridge.sh <game folder>            install
#     install-nier-bridge.sh <game folder> --status   report
#     install-nier-bridge.sh <game folder> --restore  undo
#
# This one is different from the other nine, in two ways worth knowing before
# running it.
#
# THE CARRIER IS NOT THE GAME'S. NieR ships exactly one DLL of its own,
# steam_api64.dll, and nothing here rides on Steam's API or re-exports a
# Steamworks entry point. So the bridge rides on dinput8.dll, which the game
# imports and which has five exports and nothing to do with rendering. The
# original is CrossOver's own: this script copies it out of your bottle and
# beside the game as dinput8_real.dll. Nothing is redistributed -- the copy is
# your file -- but it is a copy, so re-run this after a CrossOver upgrade if
# input ever misbehaves.
#
# IT WRITES ONE REGISTRY KEY. Wine implements dinput8 itself and prefers its
# own build, so a DLL sitting beside the game is never loaded. The override
# below says otherwise, and is scoped to this executable alone: no other title
# in the bottle sees it. --restore removes it.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() { sed -n '3,25p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

EXE_NAME='NieR Replicant ver.1.22474487139.exe'
LIVE="$GAME/dinput8.dll"
REAL="$GAME/dinput8_real.dll"
PROXY="$HERE/dinput8-nier.dll"
EXPORTS="$HERE/pe.py"
MARKER='dwo-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ -f "$GAME/$EXE_NAME" ] || {
  echo "error: no '$EXE_NAME' in $GAME" >&2
  echo "       Pick the folder NieR Replicant is installed in." >&2
  exit 1
}

# The bottle holding this game, and the CrossOver that runs it. Both are needed
# for the registry override; the bottle is also where the original dinput8
# comes from.
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

# Which bottles can actually run this copy of the game.
#
# Picking the first bottle that happens to have a dinput8.dll is wrong: a Mac
# can hold several, and the override has to land in the one the game is
# launched from. So match on the Steam library the game sits in -- every bottle
# whose libraryfolders.vdf lists it is a candidate, and each gets the override.
# Naming them all is deliberate: the user may switch bottles between runs, and
# an override for one executable is inert in a bottle that never runs it.
find_bottles() {
  local b root vdf lib key hit=0
  # The library root, as a slash-free lowercase key: libraryfolders.vdf writes
  # it as Z:\Volumes\Disk\Library, doubling every separator, so compare with
  # all separators removed and the escaping stops mattering.
  lib="${GAME%/steamapps/common/*}"
  key="$(printf '%s' "${lib#/}" | tr -d '/\' | tr '[:upper:]' '[:lower:]')"
  if [ "$lib" != "$GAME" ] && [ -n "$key" ]; then
    while IFS= read -r root; do
      for b in "$root"/*/; do
        [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
        vdf="$(find "$b/drive_c" -maxdepth 7 -iname libraryfolders.vdf 2>/dev/null | head -1)"
        [ -n "$vdf" ] || continue
        LC_ALL=C tr -d '/\' < "$vdf" | tr '[:upper:]' '[:lower:]' \
          | LC_ALL=C grep -qaF "$key" || continue
        printf '%s\n' "${b%/}"; hit=1
      done
    done < <(bottle_roots)
  fi
  [ "$hit" = 1 ] && return 0
  # Not a Steam layout, or no bottle claims the library: fall back to any
  # bottle that could supply a dinput8 at all.
  while IFS= read -r root; do
    for b in "$root"/*/; do
      [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
      printf '%s\n' "${b%/}"; return 0
    done
  done < <(bottle_roots)
  return 1
}
# The first of them, for the things that need exactly one: the copy of the
# original dinput8. Not "find_bottles | head -1" -- head closes the pipe, and
# under pipefail the SIGPIPE that follows reads as a failure to find anything.
find_bottle() {
  local out
  out="$(find_bottles)" || return 1
  [ -n "$out" ] || return 1
  printf '%s' "${out%%$'\n'*}"
}
# Is there any CrossOver at all? Used only to fail early with a clear message;
# which engine runs which bottle is crossover_for_bottle's question, and the
# answer to this one is never used to run anything.
#
# It looks where crossover_for_bottle looks. Two hardcoded paths meant a machine
# whose CrossOver lives in ~/Applications was told none was installed, while the
# per-bottle lookup would have found it.
find_crossover() {
  local a
  for a in /Applications/*.app "$HOME"/Applications/*.app; do
    [ -x "$a/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    printf '%s' "$a/Contents/SharedSupport/CrossOver"; return 0
  done
  return 1
}

# Can we even address this bottle?
#
# `wine --bottle` takes a NAME, and resolves it against that CrossOver's own
# bottle directory -- there is no way to hand it a path. CX_BOTTLE_PATH was
# tried and lands nowhere. So a bottle in another product's root cannot be
# written to by name, and worse, a bottle called "Steam" in two roots collapses
# onto whichever one the engine finds first: the run that exposed this wrote
# "Steam" twice and then reported "failed: Steam" for the copy it could not
# reach, which took a correctly installed game to `broken` and kept it there.
#
# Finding those bottles is still useful -- it is how the user is told a fix is
# not covering the bottle they actually play in. Claiming to have written to
# them is not. They are skipped out loud.
addressable() {
  case "$1" in
    "$HOME/Library/Application Support/CrossOver/Bottles"/*) return 0 ;;
    *) return 1 ;;
  esac
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
  local want a ver
  want="$(sed -n 's/^"Version" = "\(.*\)"$/\1/p' "$1/cxbottle.conf" 2>/dev/null | head -1)"
  [ -n "$want" ] || return 1
  for a in /Applications/*.app "$HOME"/Applications/*.app; do
    [ -x "$a/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    ver="$(defaults read "$a/Contents/Info" CFBundleVersion 2>/dev/null)"
    [ "$ver" = "$want" ] || continue
    printf '%s' "$a/Contents/SharedSupport/CrossOver"; return 0
  done
  return 1
}

# Can this bottle answer at all?
#
# A query that fails is not evidence of a missing key. An ARM bottle recorded
# against a CrossOver with no ARM support has its keys on disk and cannot run
# reg.exe to say so, and counting that as "the override is gone" reported a
# working game as broken -- the same mistake as claiming to have written to a
# bottle we cannot address, arrived at from the other side.
#
# So the bottle is asked something that must be there. If even that fails, the
# bottle is unreachable and is skipped rather than judged.
reachable() {
  "$2/bin/wine" --bottle "$(basename "$1")" --cx-app reg.exe query \
    "HKEY_CURRENT_USER\\Software" >/dev/null 2>&1
}

# Whether the override is really there.
#
# The bridge needs three things and the file pair is only two of them: without
# this key Wine loads its own dinput8 and the proxy beside the game is never
# opened. Reporting `installed` from the files alone is how this title spent a
# long time recorded as broken on stable CrossOver -- the fix was not running in
# any of those measurements, and nothing said so.
#
# It asks the registry rather than reading user.reg, because wineserver flushes
# that file when it feels like it and a lazy flush reads as a missing key.
override_ok() {
  local b cx seen=0
  while read -r b; do
    [ -n "$b" ] || continue
    addressable "$b" || continue
    cx="$(crossover_for_bottle "$b")" || continue
    reachable "$b" "$cx" || continue
    seen=$((seen + 1))
    # Symmetric with [4/4]: that step writes the key into EVERY candidate
    # bottle, on purpose, because the user may switch bottles between runs. So
    # one bottle holding it is not the question -- the question is whether any
    # candidate is missing it, because that is the run where the bridge silently
    # does not load.
    "$cx/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe query \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
      /v dinput8 >/dev/null 2>&1 || return 1
  done < <(find_bottles || true)
  [ "$seen" -gt 0 ]
}

case "$MODE" in
--status)
  # The wine call costs a wineserver, so it is only made once the file pair has
  # already answered `installed` -- that is, only for a game that is patched.
  if is_ours "$LIVE" && [ -f "$REAL" ]; then
    if override_ok; then echo installed; else echo broken; fi
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  exit 0
  ;;
--restore)
  # Symmetric with the guard [2/4] grew: if what is live is not ours, it
  # belongs to somebody else -- a mod, an input wrapper -- and restoring must
  # not delete it just because our saved copy happens to sit beside it.
  if [ -f "$LIVE" ] && ! is_ours "$LIVE"; then
    echo "error: $LIVE is not ours, so nothing was removed." >&2
    echo "       Delete $REAL by hand if you want the leftover copy gone." >&2
    exit 1
  fi
  rm -f "$LIVE" "$REAL"
  while read -r b; do
    [ -n "$b" ] || continue
    cx="$(crossover_for_bottle "$b")" || continue
    "$cx/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe delete \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
      /v dinput8 /f >/dev/null 2>&1 || true
  done < <(find_bottles || true)
  echo "restored — the bridge and the dinput8 override are gone"
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

# Already-installed is not a reason to do nothing: the override is the part that
# goes missing on its own -- a bottle reset, a bottle created after a CrossOver
# upgrade -- and re-running is the documented remedy for exactly that. So the
# file steps are skipped and the key is asserted again.
SKIP_FILES=0
if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the bridge files are already in place; re-asserting the override"
  SKIP_FILES=1
fi

echo "[1/4] finding the bottle and the CrossOver that runs it"
BOTTLE="$(find_bottle)" || {
  echo "error: no CrossOver bottle with a dinput8.dll was found" >&2
  echo "       Run the game once first, so its bottle exists." >&2
  exit 1
}
CX="$(find_crossover)" || {
  echo "error: no CrossOver installation was found in /Applications" >&2
  exit 1
}
echo "      bottle: $(basename "$BOTTLE")"

if [ "$SKIP_FILES" = 0 ]; then
echo "[2/4] taking a copy of the bottle's own dinput8"
# Never over a proxy: if $LIVE is already ours, $REAL would be overwritten with
# the proxy and the original lost for good.
if is_ours "$LIVE"; then
  echo "error: $LIVE is already a proxy but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
# A dinput8.dll that is here and is not ours belongs to somebody else -- a mod,
# a ReShade, an input wrapper. This script does not move it aside, it copies the
# bottle's own over $REAL and then writes the proxy into $LIVE, so carrying on
# would overwrite that file with no copy kept. And unlike a game DLL it is not
# in Steam's manifest: "Verify the game files" does not bring it back.
if [ -f "$LIVE" ]; then
  echo "error: $LIVE already exists and is not ours." >&2
  echo "       Something else installed a dinput8 here -- a mod or an input" >&2
  echo "       wrapper. Move it away yourself if you want the bridge instead;" >&2
  echo "       nothing was changed." >&2
  exit 1
fi
cp "$BOTTLE/drive_c/windows/system32/dinput8.dll" "$REAL" || {
  echo "error: could not copy the original beside the game" >&2
  exit 1
}

echo "[3/4] checking the proxy forwards everything the original exports"
if ! real_exports="$(python3 "$EXPORTS" exports "$REAL" 2>&1)"; then
  echo "error: cannot read the exports of $REAL" >&2; rm -f "$REAL"; exit 1
fi
if ! proxy_exports="$(python3 "$EXPORTS" exports "$PROXY" 2>&1)"; then
  echo "error: cannot read the exports of $PROXY" >&2; rm -f "$REAL"; exit 1
fi
missing="$(comm -23 <(printf '%s\n' "$real_exports" | sort) \
                    <(printf '%s\n' "$proxy_exports" | sort))"
if [ -n "$missing" ]; then
  echo "error: this CrossOver's dinput8 exports symbols the shipped proxy does not:" >&2
  echo "$missing" | sed 's/^/       /' >&2
  echo "       Rebuild the proxy against it; nothing was installed." >&2
  rm -f "$REAL"
  exit 1
fi
cp "$PROXY" "$LIVE" || { echo "error: could not install the bridge" >&2; rm -f "$REAL"; exit 1; }
fi

echo "[4/4] telling Wine to prefer it, for this game only"
wrote=0
skipped=0
failed=0
while read -r b; do
  [ -n "$b" ] || continue
  addressable "$b" || {
    echo "      skipped $(basename "$b"): it lives outside CrossOver's own bottle" >&2
    echo "               directory, and --bottle can only name bottles there" >&2
    skipped=$((skipped + 1)); continue
  }
  CX="$(crossover_for_bottle "$b")" || {
    echo "      skipped $(basename "$b"): no installed CrossOver matches its engine" >&2
    skipped=$((skipped + 1)); continue
  }
  # Both steps counted, and loudly. A bare `|| continue` here made the two
  # halves of this script disagree: [4/4] called the install a success on one
  # bottle out of five while override_ok, one screen up, requires all of them --
  # so a mixed run printed `installed` and --status answered `broken`, for ever,
  # with the app re-offering a repair that could not change the answer.
  if ! "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe add \
       "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
       /v dinput8 /d "native,builtin" /f >/dev/null 2>&1 \
     || ! "$CX/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe query \
       "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
       /v dinput8 >/dev/null 2>&1; then
    # Asked back rather than grepped from user.reg: wineserver decides when to
    # flush that file, and a lazy flush would read as a failed write.
    echo "      failed: $(basename "$b") -- the override did not stick" >&2
    failed=$((failed + 1)); continue
  fi
  echo "      $(basename "$b")"
  wrote=$((wrote + 1))
done < <(find_bottles || true)
# Symmetric with override_ok: one candidate bottle without the key is the run in
# which the bridge does not load, so it is not `installed`.
if [ "$wrote" = 0 ] || [ "$failed" -gt 0 ]; then
  if [ "$wrote" = 0 ]; then
    echo "error: the registry override could not be written to any bottle." >&2
    echo "       Without it Wine loads its own dinput8 and the bridge never runs." >&2
  else
    echo "error: $failed of $((wrote + failed)) bottle(s) rejected the override." >&2
    echo "       Close the game and any CrossOver window, then run this again." >&2
  fi
  [ "$skipped" = 0 ] || echo "       $skipped bottle(s) run an engine that is not installed here." >&2
  # Undo only what this run created. $REAL is a COPY of the bottle's own
  # dinput8, not a file the game shipped, so moving it back over $LIVE would
  # leave a foreign native dinput8 beside the executable -- and --status would
  # then answer `absent` about a half-built game.
  # Files are only rolled back when nothing landed anywhere. Deleting them
  # because one bottle of five refused would throw away a mostly-correct
  # install.
  if [ "$SKIP_FILES" = 0 ] && [ "$wrote" = 0 ]; then
    rm -f "$LIVE" "$REAL"
    while read -r b; do
      [ -n "$b" ] || continue
      cx="$(crossover_for_bottle "$b")" || continue
      "$cx/bin/wine" --bottle "$(basename "$b")" --cx-app reg.exe delete \
        "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE_NAME\\DllOverrides" \
        /v dinput8 /f >/dev/null 2>&1 || true
    done < <(find_bottles || true)
  fi
  exit 1
fi
echo
echo "installed"
echo "  the video bridge is in place, and dinput8 is overridden for this game only"
echo "  the staged codec is needed too: the video is WMV2 with WMA v2 audio in ASF,"
echo "  and CrossOver demuxes ASF while decoding neither stream"
