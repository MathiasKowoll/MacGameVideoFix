#!/usr/bin/env bash
#
# Install the KINGDOM HEARTS video bridge.
#
#     install-kh-bridge.sh <game folder>            install
#     install-kh-bridge.sh <game folder> --status   report
#     install-kh-bridge.sh <game folder> --restore  undo
#
# Two packages take this fix, and between them eight executables play video:
#
#   HD 2.8 Final Chapter Prologue   Dream Drop Distance, and the launcher,
#                                   which is what plays chi Back Cover
#   HD 1.5+2.5 ReMIX                FINAL MIX, Re_Chain of Memories,
#                                   II FINAL MIX, Birth by Sleep FINAL MIX,
#                                   the Theater and the launcher
#
# Give it the package folder -- the one Steam installed, holding those
# executables. It works out which are there.
#
# 0.2 Birth by Sleep ships inside 2.8 and is not in the list: its cutscenes are
# software MPEG-1 through CriWare, they already play, and this does nothing for
# them. KINGDOM HEARTS III is the same case and is not supported here either.
#
# THE CARRIER IS NOT THE GAME'S. The only DLL beside these executables is
# steam_api64.dll, and nothing here rides on Steam's API or re-exports a
# Steamworks entry point. So the bridge rides on dinput8.dll, which every one of
# them imports and which has five exports and nothing to do with rendering. The
# original is CrossOver's own: this script copies it out of your bottle and
# beside the game as dinput8_real.dll. Nothing is redistributed -- the copy is
# your file -- but it is a copy, so re-run this after a CrossOver upgrade if
# input ever misbehaves.
#
# IT WRITES ONE REGISTRY KEY PER EXECUTABLE. Wine implements dinput8 itself and
# prefers its own build, so a DLL sitting beside the game is never loaded. The
# override says otherwise, and is scoped to those executables alone: no other
# title in the bottle sees it. --restore removes them.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# WHAT THIS SCRIPT IS FOR, in a form something other than a human can read.
#
# One MGVF-GAME line per title this installer serves -- four of them serve more
# than one, which the manifest could not express before. The fields are the
# game's name, its shipping executable, and where the carrier sits relative to
# the game folder (empty means the folder itself). The executable is the
# identity: there is no AppID anywhere in this project, and the folder name is
# Valve's to choose -- Mortal Shell 2 installs into one called Sparta.
#
# runtime/check-builds.sh checks these against the app's own table, so the two
# copies cannot drift apart in silence.
#
# MGVF-GAME: KINGDOM HEARTS Dream Drop Distance | KINGDOM HEARTS Dream Drop Distance.exe | 
# MGVF-GAME: KINGDOM HEARTS HD 1.5+2.5 ReMIX | KINGDOM HEARTS HD 1.5+2.5 Launcher.exe | 
# MGVF-WHY: Cutscenes run with sound and a solid green picture: the luma and chroma planes never reach the game's own textures.

set -euo pipefail

# HOME is required, and its absence must not be answerable.
#
# Both bottle roots are built from it, and under `set -u` a missing HOME kills
# the function that finds them -- after which --status still printed a state
# word, reporting `broken` for a fix it had not been able to look at. A wrong
# answer is worse than no answer, so this refuses instead.
#
# It goes missing in exactly one situation, and it is the situation this script
# is heading for: an application that runs it with an explicit environment
# dictionary rather than inheriting one.
: "${HOME:?this needs HOME; a caller passing an explicit environment must include it}"

usage() { sed -n '3,36p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

# Every executable in either package that opens a source reader. Checked
# against the folder, so only the ones actually there are touched.
KNOWN=(
  'KINGDOM HEARTS Dream Drop Distance.exe'
  'KINGDOM HEARTS HD 2.8 Launcher.exe'
  'KINGDOM HEARTS FINAL MIX.exe'
  'KINGDOM HEARTS Re_Chain of Memories.exe'
  'KINGDOM HEARTS II FINAL MIX.exe'
  'KINGDOM HEARTS Birth by Sleep FINAL MIX.exe'
  'KINGDOM HEARTS Theater.exe'
  'KINGDOM HEARTS HD 1.5+2.5 Launcher.exe'
)

EXE_NAMES=()
for e in "${KNOWN[@]}"; do
  [ -f "$GAME/$e" ] && EXE_NAMES+=("$e")
done

LIVE="$GAME/dinput8.dll"
REAL="$GAME/dinput8_real.dll"
PROXY="$HERE/dinput8-kh.dll"
EXPORTS="$HERE/pe.pl"
MARKER='dwo-video-bridge.log'

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

[ ${#EXE_NAMES[@]} -gt 0 ] || {
  echo "error: no KINGDOM HEARTS executable found in $GAME" >&2
  echo "       Pick the package folder -- HD 2.8 Final Chapter Prologue, or" >&2
  echo "       HD 1.5+2.5 ReMIX -- the one holding the game's .exe files." >&2
  exit 1
}

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

find_bottles() {
  local b root vdf lib key hit=0
  lib="${GAME%/steamapps/common/*}"
  key="$(printf '%s' "${lib#/}" | tr -d '/\\' | tr '[:upper:]' '[:lower:]')"
  if [ "$lib" != "$GAME" ] && [ -n "$key" ]; then
    while IFS= read -r root; do
      for b in "$root"/*/; do
        [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
        vdf="$(find "$b/drive_c" -maxdepth 7 -iname libraryfolders.vdf 2>/dev/null | head -1)"
        [ -n "$vdf" ] || continue
        LC_ALL=C tr -d '/\\' < "$vdf" | tr '[:upper:]' '[:lower:]' \
          | LC_ALL=C grep -qaF "$key" || continue
        printf '%s\n' "${b%/}"; hit=1
      done
    done < <(bottle_roots)
  fi
  [ "$hit" = 1 ] && return 0
  while IFS= read -r root; do
    for b in "$root"/*/; do
      [ -f "$b/drive_c/windows/system32/dinput8.dll" ] || continue
      printf '%s\n' "${b%/}"; return 0
    done
  done < <(bottle_roots)
  return 1
}

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
# Run a CrossOver command against a bottle identified by its PATH.
#
# `wine --bottle` takes a NAME and resolves it against that CrossOver's own
# bottle root, so a bottle living in another product's root cannot be reached
# by name at all -- and worse, a name that also exists in the default root
# resolves THERE instead, silently.
#
# That is measured, not feared. With a stock engine and no CX_BOTTLE_PATH,
# `--bottle SteamArm` reached ~/Library/Application Support/CrossOver/Bottles/
# SteamARM rather than the intended bottle in another root, because macOS does
# not distinguish the case: the two registries answered differently and only
# a key present in one of them gave it away. Writing an override that way puts
# it in a bottle the user never plays in, and says nothing.
#
# CX_BOTTLE_PATH names the root explicitly, which removes the ambiguity and
# makes every bottle addressable regardless of which product created it. The
# check after each write still stands on its own: a write that did not land is
# never counted, whatever the addressing did.
wine_in_bottle() {
  local bottle="$1" cx="$2"
  shift 2
  CX_BOTTLE_PATH="$(dirname "$bottle")" \
    "$cx/bin/wine" --bottle "$(basename "$bottle")" "$@"
}

# The CrossOver that can actually open a given bottle. A bottle records the
# CFBundleVersion of the engine that last updated it, and an engine refuses a
# bottle newer than itself SILENTLY -- exit 0 and no output. Picking one
# CrossOver for the whole machine writes the keys into whichever bottles happen
# to match and counts the rest as done.
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
  wine_in_bottle "$1" "$2" --cx-app reg.exe query \
    "HKEY_CURRENT_USER\\Software" >/dev/null 2>&1
}

# Whether every override this package needs is really there.
#
# The proxy and the copied original are two of the three things the bridge
# needs; without the keys Wine loads its own dinput8 and the proxy is never
# opened. And here there is one key PER EXECUTABLE: a package can be five games
# deep, so a partial answer is the dangerous one -- four titles playing and one
# silently without cutscenes still has to read as broken.
override_ok() {
  local b cx exe seen=0
  while read -r b; do
    [ -n "$b" ] || continue
    cx="$(crossover_for_bottle "$b")" || continue
    reachable "$b" "$cx" || continue
    seen=$((seen + 1))
    for exe in "${EXE_NAMES[@]}"; do
      wine_in_bottle "$b" "$cx" --cx-app reg.exe query \
        "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
        /v dinput8 >/dev/null 2>&1 || return 1
    done
  done < <(find_bottles || true)
  [ "$seen" -gt 0 ]
}

case "$MODE" in
--status)
  # The wine calls cost a wineserver, so they only happen once the file pair has
  # already answered `installed` -- that is, only for a package that is patched.
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
    CX="$(crossover_for_bottle "$b")" || continue
    for exe in "${EXE_NAMES[@]}"; do
      wine_in_bottle "$b" "$CX" --cx-app reg.exe delete \
        "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
        /v dinput8 /f >/dev/null 2>&1 || true
    done
  done < <(find_bottles || true)
  echo "restored — the bridge and the dinput8 overrides are gone"
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

# Already-installed is not a reason to do nothing: the keys are the part that
# goes missing on its own -- a bottle reset, a bottle created after a CrossOver
# upgrade -- and re-running is the documented remedy. The file steps are skipped
# and every key is asserted again.
SKIP_FILES=0
if is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "the bridge files are already in place; re-asserting the overrides"
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
echo "      ${#EXE_NAMES[@]} executable(s) that play video in this package"

if [ "$SKIP_FILES" = 0 ]; then
echo "[2/4] taking a copy of the bottle's own dinput8"
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
  echo "error: could not copy the original beside the package" >&2
  exit 1
}

echo "[3/4] checking the proxy forwards everything the original exports"
if ! real_exports="$(/usr/bin/perl "$EXPORTS" exports "$REAL" 2>&1)"; then
  echo "error: cannot read the exports of $REAL" >&2; rm -f "$REAL"; exit 1
fi
if ! proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY" 2>&1)"; then
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

echo "[4/4] telling Wine to prefer it, for these executables only"
wrote=0
skipped=0
failed=0
while read -r b; do
  [ -n "$b" ] || continue
  CX="$(crossover_for_bottle "$b")" || {
    echo "      skipped $(basename "$b"): no installed CrossOver matches its engine" >&2
    skipped=$((skipped + 1)); continue
  }
  # Counted per executable, not per bottle. An `ok=1` set by whichever exe
  # happened to succeed reported the whole package installed while the rest of
  # its games played no cutscenes -- and the failures were silent by
  # construction, because the `continue` below skips to the next exe.
  bad=0
  for exe in "${EXE_NAMES[@]}"; do
    wine_in_bottle "$b" "$CX" --cx-app reg.exe add \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
      /v dinput8 /d "native,builtin" /f >/dev/null 2>&1 || {
        bad=$((bad + 1)); echo "      failed: $exe" >&2; continue
      }
    # Ask the registry, not the file. wineserver flushes user.reg on its own
    # schedule, so a key that was just written is often not on disk yet -- and
    # reading the file makes a lazy flush look like a failed write.
    wine_in_bottle "$b" "$CX" --cx-app reg.exe query \
      "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
      /v dinput8 >/dev/null 2>&1 || { bad=$((bad + 1)); echo "      failed: $exe" >&2; }
  done
  # A bottle that lost even one executable is a failure, not a bottle to skip
  # quietly: override_ok requires every key in every candidate bottle, so
  # leaving here without saying so let --install print `installed` while
  # --status answered `broken` on the next scan, permanently.
  [ "$bad" = 0 ] || { failed=$((failed + 1)); continue; }
  echo "      $(basename "$b")"
  wrote=$((wrote + 1))
done < <(find_bottles || true)
# Symmetric with override_ok: one candidate bottle missing one key is the run in
# which that title plays no cutscenes, so it is not `installed`.
if [ "$wrote" = 0 ] || [ "$failed" -gt 0 ]; then
  if [ "$wrote" = 0 ]; then
    echo "error: the registry overrides could not be written to any bottle." >&2
    echo "       Without them Wine loads its own dinput8 and the bridge never runs." >&2
  else
    echo "error: $failed of $((wrote + failed)) bottle(s) did not take every override." >&2
    echo "       Close the games and any CrossOver window, then run this again." >&2
  fi
  [ "$skipped" = 0 ] || echo "       $skipped bottle(s) run an engine that is not installed here." >&2
  # Undo only what this run created. $REAL is a COPY of the bottle's own
  # dinput8, not a file the package shipped, so moving it back over $LIVE would
  # leave a foreign native dinput8 beside the executables.
  # Only when nothing landed anywhere: deleting the files because one bottle of
  # five refused would throw away a mostly-correct install.
  if [ "$SKIP_FILES" = 0 ] && [ "$wrote" = 0 ]; then
    rm -f "$LIVE" "$REAL"
    while read -r b; do
      [ -n "$b" ] || continue
      cx="$(crossover_for_bottle "$b")" || continue
      for exe in "${EXE_NAMES[@]}"; do
        wine_in_bottle "$b" "$cx" --cx-app reg.exe delete \
          "HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$exe\\DllOverrides" \
          /v dinput8 /f >/dev/null 2>&1 || true
      done
    done < <(find_bottles || true)
  fi
  exit 1
fi
echo
echo "installed"
echo "  the video bridge is in place, and dinput8 is overridden for this"
echo "  package's executables only"
echo "  no staged codec is needed for this one"
