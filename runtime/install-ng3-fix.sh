#!/usr/bin/env bash
#
# Install (or remove) the fix for NINJA GAIDEN 3: RAZOR'S EDGE.
#
#   install-ng3-fix.sh <bottle>            install
#   install-ng3-fix.sh <bottle> --restore  remove
#   install-ng3-fix.sh <bottle> --status   report what is in place
#
# <bottle> is the CrossOver bottle the game runs in, e.g.
#   ~/Library/Application Support/CrossOver/Bottles/Steam
#
# NOTHING IN THE GAME FOLDER IS TOUCHED. Four DLLs are placed in the bottle's
# system32 and activated for this one executable through AppDefaults, so no
# other title in the same bottle changes behaviour. A global d3d9 override would
# silently alter every other D3D9 game; that is why this is per-application.
#
# WHAT THIS FIXES.
#
# Razor's Edge is the odd one of the Master Collection: Direct3D 9 only, where
# SIGMA and SIGMA 2 are D3D11 and start fine. Under CrossOver's own DXVK it
# refuses to start with "Insufficient VRAM. Please close all running
# applications." -- which is a red herring. Its log says what really happened,
# eleven times over: DxvkAdapter: Failed to create device, after reporting
# transformFeedback and timelineSemaphore as 0. MoltenVK does not offer those
# and that DXVK build requires them. Two lines earlier the same log reports the
# machine's full 48 GB, so memory was never the problem.
#
# Its 24 movies are ASF containers with WMV3 video and WMA v2 audio, played
# through DirectShow -- quartz and qasf -- not through Media Foundation. An
# afternoon went into instrumenting MFTEnumEx and the source readers, all of
# which correctly reported nothing, because nothing was there.
#
# WHERE THE BINARIES COME FROM.
#
# Three of the four are other people's work and ship beside this script, so the
# fix does not depend on the user having installed anything else first:
#
#   d3d9.dll          d9vk, Sikarugir-App/d9vk, v1.10.3-20250511 (macOS async)
#   qasf.dll          winevideo's patched DirectShow ASF Reader
#   quartz.dll        winevideo's patched filter graph
#   winegstreamer.dll winevideo's patched PE half
#
# d9vk is DXVK, zlib-licensed. qasf, quartz and winegstreamer are Wine, LGPL --
# patched builds by winevideo's author, whose diagnosis and implementation the
# whole fix rests on. Both licences permit redistribution; both require the
# licence text to travel with the binaries, which is why licences/ is beside
# them in the bundle.
#
# The qasf change is the substantial one and it is not a codec matter: Wine's
# ASF Reader delivered a video sample on the WMReader callback, the downstream
# video pin blocked in Receive(), and audio -- which needs the same callback to
# preroll -- never got its turn, so the graph sat in VFW_S_STATE_INTERMEDIATE
# instead of reaching Running. winevideo's build moves blocking video delivery
# onto a serialized worker so audio can preroll. Credit for the diagnosis and
# the implementation belongs to winevideo's author, not to this project.
#
# KNOWN LIMIT. The boot movie decodes its first frame and freezes; one click
# skips it and the game carries on. In-game cutscenes play. winevideo 0.5 runs
# the boot movie too, because there all four pieces are one coherent build --
# here winegstreamer's PE half sits on this engine's Unix half, and only the PE
# half can be overridden per application.
#
# MGVF-SCOPE: bottle
# MGVF-GAME: NINJA GAIDEN 3: Razor's Edge | NINJA GAIDEN 3 Razor's Edge.exe |
# MGVF-WHY: Will not start at all: "Insufficient VRAM. Please close all running applications." It is a Direct3D 9 title and CrossOver's DXVK cannot create a Vulkan device for it. Needs d9vk plus winevideo's DirectShow filters, applied to this executable only.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

usage() { sed -n '3,12p' "$0" >&2; exit 1; }

# Two callers, two meanings for the first argument, and structure decides rather
# than precedence.
#
# By hand this takes a bottle. The app passes every installer the GAME FOLDER it
# found, because every other fix here goes beside the executable -- so if this
# one simply preferred $1 it would be handed a game folder and treat it as a
# bottle, and if it simply preferred the environment it would ignore a bottle
# somebody typed. Neither rule is right for both callers.
#
# So look: a bottle has a cxbottle.conf and a drive_c. If $1 is one, use it. If
# it is not and MGVF_BOTTLE names one, use that -- which is what the app sets,
# from the bottle a person chose in the wizard. If neither, refuse and say so,
# because guessing which bottle somebody meant is how a fix lands in a bottle
# they never picked.
is_bottle() { [ -n "$1" ] && [ -f "$1/cxbottle.conf" ] && [ -d "$1/drive_c" ]; }

BOTTLE=""
if is_bottle "${1:-}"; then
  BOTTLE="${1%/}"
elif is_bottle "${MGVF_BOTTLE:-}"; then
  BOTTLE="${MGVF_BOTTLE%/}"
else
  echo "error: no bottle to work on." >&2
  echo "       Pass one, e.g. ~/Library/Application Support/CrossOver/Bottles/Steam," >&2
  echo "       or set MGVF_BOTTLE. A bottle is a folder with cxbottle.conf and drive_c." >&2
  [ -n "${1:-}" ] && echo "       '$1' is not one." >&2
  [ -n "${MGVF_BOTTLE:-}" ] && echo "       MGVF_BOTTLE='$MGVF_BOTTLE' is not one either." >&2
  exit 1
fi
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

EXE="NINJA GAIDEN 3 Razor's Edge.exe"
SYS="$BOTTLE/drive_c/windows/system32"
REG="$BOTTLE/user.reg"
KEY="[Software\\\\Wine\\\\AppDefaults\\\\$EXE\\\\DllOverrides]"
DLLS="d3d9.dll qasf.dll quartz.dll winegstreamer.dll"

# The registry is asked, not edited.
#
# The first version of this script appended the AppDefaults key to user.reg with
# a text editor, and therefore had to refuse to run while a wineserver was
# alive: the server holds the registry in memory and writes it back on exit,
# silently undoing anything written underneath it. Kingdom Hearts and NieR had
# already solved this here by going through reg.exe inside the bottle, and
# NieR's script even says why. Doing it their way costs nothing and removes the
# requirement to bring the bottle down -- which for a launcher means quitting
# Steam and ending the prefix before it can even tell a user whether their game
# needs the fix.
# bottles.sh carries crossover_for_bottle. Sourced BEFORE this file's own
# helpers so that anything defined here still wins -- this script is shipped
# standalone as well, so it must work whether or not that file is beside it.
[ -f "$HERE/bottles.sh" ] && . "$HERE/bottles.sh"

wine_in_bottle() {
  local bottle="$1" cx="$2"
  shift 2
  CX_BOTTLE_PATH="$(dirname "$bottle")" \
    "$cx/bin/wine" --bottle "$(basename "$bottle")" "$@"
}

find_crossover() {
  # An engine named by the caller wins over anything this script can work out.
  #
  # A launcher knows which engine it built; a script can only infer. Set MGVF_CX
  # to the CrossOver directory -- the one holding bin/wine, not the .app -- and
  # that is what runs reg.exe.
  #
  # Set but wrong is an ERROR, not a fallback. A caller that names an engine and
  # is silently ignored is the worst of the three outcomes: it believes it chose,
  # and the wrong engine does not fail loudly -- which is the whole reason this
  # function was rewritten. Unset is fine and falls through to asking the bottle.
  if [ -n "${MGVF_CX:-}" ]; then
    if [ -x "${MGVF_CX%/}/bin/wine" ]; then
      printf '%s' "${MGVF_CX%/}"; return 0
    fi
    echo "error: MGVF_CX is set to '$MGVF_CX' and there is no bin/wine there." >&2
    echo "       Point it at a CrossOver directory, e.g." >&2
    echo "       /Applications/CrossOver.app/Contents/SharedSupport/CrossOver," >&2
    echo "       or unset it and this will ask the bottle which engine is its own." >&2
    return 1
  fi

  # Ask the BOTTLE which engine belongs to it, before falling back to names.
  #
  # The fallback below searches by name, and its names went stale: it looks for
  # Crossover_patched.app, which this project stopped making, and for
  # "$HOME/Applications/CrossOver"*.app -- a glob that never matches a copy named
  # Crossover_MGVF.app, because sh compares globs case-sensitively even where the
  # filesystem does not. So it fell through to /Applications/CrossOver.app and ran
  # reg.exe under stock CrossOver against a bottle a patched engine had built.
  #
  # Wine then does what it always does when a bottle meets an unfamiliar engine:
  # it updates it. Measured on this machine on 2026-08-31 -- 1,475 files rewritten
  # under drive_c/windows, which undid three of the four DLLs this installer had
  # just placed and left the override naming all four. The install reported
  # success. Nothing failed loudly, which is the whole problem.
  #
  # crossover_for_bottle answers from the bottle: first the engine whose own
  # CX_BOTTLE_PATH holds it, then the version. Its comment already described this
  # exact failure; it simply was not the function being called.
  if command -v crossover_for_bottle >/dev/null 2>&1 && [ -n "${BOTTLE:-}" ]; then
    local byBottle
    if byBottle="$(crossover_for_bottle "$BOTTLE" 2>/dev/null)" && [ -n "$byBottle" ]; then
      printf '%s' "$byBottle"; return 0
    fi
  fi
  local c
  for c in "$HOME/Applications/Crossover_patched.app" \
           "$HOME/Applications/CrossOver"*.app \
           "/Applications/CrossOver.app" \
           "/Applications/CrossOver"*.app; do
    [ -x "$c/Contents/SharedSupport/CrossOver/bin/wine" ] || continue
    echo "$c/Contents/SharedSupport/CrossOver"; return 0
  done
  return 1
}

# A query that fails is not evidence of a missing key -- an unreachable bottle
# answers nothing at all, and reading that as "the override is gone" reports a
# working game as broken. So ask for something that must exist first.
reachable() {
  wine_in_bottle "$1" "$2" --cx-app reg.exe query "HKEY_CURRENT_USER\\Software" \
    >/dev/null 2>&1
}

KEY="HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\$EXE\\DllOverrides"

override_present() {
  local cx="$1"
  wine_in_bottle "$BOTTLE" "$cx" --cx-app reg.exe query "$KEY" /v "*d3d9" \
    >/dev/null 2>&1
}


[ -d "$BOTTLE/drive_c" ] || { echo "error: not a bottle: $BOTTLE" >&2; exit 1; }

# Named literally, because make-fixes-bundle.sh works out what an installer
# needs by collecting its "$HERE/<file>" references. A lookup that builds the
# name at runtime is invisible to it, and the bundle would ship the script
# without the four DLLs it depends on -- which fails at the user's machine
# rather than here.
NG3_D3D9="$HERE/ng3-d3d9.dll"
NG3_QASF="$HERE/ng3-qasf.dll"
NG3_QUARTZ="$HERE/ng3-quartz.dll"
NG3_WINEGST="$HERE/ng3-winegstreamer.dll"
NG3_LICENCES="$HERE/ng3-THIRD-PARTY-LICENCES.md"

# The DLLs ship beside this script. A winevideo installation is still accepted
# as a source, because someone building from the repository may not have run
# make-fixes-bundle.sh, and the file it would find there is the same one.
roots() {
  echo "$HERE"
  local c
  # A SEARCH PATTERN, not a citation. winevideo is
  # https://github.com/Jfishin/winevideo and that is what documentation should
  # name; this glob only guesses where somebody filed their build, because the
  # script has to find it on disk. Leave it matching loosely.
  for c in "$HOME/Applications/CrossOver-winevideo-"*.app \
           "/Applications/CrossOver-winevideo-"*.app \
           "/Applications/winevideo Patcher.app"; do
    [ -d "$c" ] || continue
    echo "$c/Contents/Resources/patcher/payload/wine-pe"
    echo "$c/Contents/SharedSupport/winevideo/third-party/d9vk/x64"
    echo "$c/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows"
  done
}

# Ours are prefixed so a flat bundle cannot collide with another game's carrier.
src_for() {
  local ours r
  case "$1" in
    d3d9.dll)          ours="$NG3_D3D9" ;;
    qasf.dll)          ours="$NG3_QASF" ;;
    quartz.dll)        ours="$NG3_QUARTZ" ;;
    winegstreamer.dll) ours="$NG3_WINEGST" ;;
  esac
  [ -f "$ours" ] && { echo "$ours"; return 0; }
  while read -r r; do
    [ -f "$r/ng3-$1" ] && { echo "$r/ng3-$1"; return 0; }
    [ -f "$r/$1" ]     && { echo "$r/$1"; return 0; }
  done < <(roots)
  return 1
}

status() {
  local n=0 d key=0 cx
  # Count what is PLACED, not what was DISPLACED.
  #
  # This counted *.mgvf-stock -- the originals moved aside -- and that is only a
  # proxy for "we installed here". A bottle with no d3d9.dll of its own displaces
  # nothing, so a perfectly good install left nothing to count and the answer came
  # back "absent" with all four of our files sitting in system32. The app maps
  # absent to "not applied" and offers to install again, so the person installs
  # twice and is told nothing happened either time.
  #
  # The honest question is whether OUR file is there, and that is answerable by
  # comparing it with the one we would install. Same fault as generation 4's
  # d3d10: state inferred from displacement is blind to what merely arrived.
  for d in $DLLS; do
    [ -f "$SYS/$d" ] && [ -f "$HERE/ng3-$d" ] && cmp -s "$SYS/$d" "$HERE/ng3-$d" && n=$((n+1))
  done

  # Ask the bottle. If it cannot answer -- no CrossOver that matches its engine,
  # or a prefix that will not run reg.exe -- fall back to reading user.reg,
  # which is stale while a server is up but is better than calling an
  # unanswerable question a missing key.
  if cx="$(find_crossover)" && reachable "$BOTTLE" "$cx" && override_present "$cx"; then
    key=1
  elif ! cx="$(find_crossover)" || ! reachable "$BOTTLE" "$cx"; then
    grep -q "AppDefaults\\\\$EXE" "$REG" 2>/dev/null && key=1
  fi

  # The same four words the other eleven installers answer with, so a launcher
  # can read every fix the same way. half is a real state and not a rounding of
  # broken: the DLLs are in place and the override is not, which is what an
  # install interrupted between its two halves leaves behind, and it is fixed by
  # installing again rather than by restoring first.
  if [ "$key" = 1 ] && [ "$n" -eq 4 ]; then
    echo installed
  elif [ "$key" = 0 ] && [ "$n" -gt 0 ]; then
    echo half
  elif [ "$n" -gt 0 ] || [ "$key" = 1 ]; then
    echo broken
  else
    echo absent
  fi
}

export MGVF_EXE="$EXE"

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      for d in $DLLS; do
        if [ -f "$SYS/$d.mgvf-stock" ]; then
          mv -f "$SYS/$d.mgvf-stock" "$SYS/$d"
        else
          rm -f "$SYS/$d"
        fi
      done
      # The last line is what a caller reads, so it must not say "restored"
      # when the registry key is still there. It was doing exactly that: the
      # explanation went to stderr and stdout ended with the word meaning done.
      # Same fault as --status counting displacements -- a true thing said where
      # nobody looks, and a false one where they do.
      if cx="$(find_crossover)" && reachable "$BOTTLE" "$cx" \
         && wine_in_bottle "$BOTTLE" "$cx" --cx-app reg.exe delete "$KEY" /f >/dev/null 2>&1; then
        echo "removed the per-application overrides"
        echo "restored"
      else
        echo "warning: the bottle could not be asked; the overrides are still" >&2
        echo "         in its registry. The DLLs have been put back, so nothing" >&2
        echo "         of ours is loaded, but the key remains. Ask again when the" >&2
        echo "         bottle is reachable; --status will say broken until then." >&2
        echo "restored-except-registry"
      fi
      exit 0 ;;
  install)
      ;;
  *) usage ;;
esac

if ! src_for d3d9.dll >/dev/null || ! src_for qasf.dll >/dev/null; then
  echo "error: the pieces this fix needs were not found." >&2
  echo "" >&2
  echo "These four should be beside this script and are not:" >&2
  echo "  ng3-d3d9.dll ng3-qasf.dll ng3-quartz.dll ng3-winegstreamer.dll" >&2
  echo "" >&2
  echo "Run it from a complete fixes bundle, or from runtime/ in a checkout." >&2
  exit 1
fi

for d in $DLLS; do
  s="$(src_for "$d")" || { echo "error: $d not found under $WV" >&2; exit 1; }
  if [ -f "$SYS/$d" ] && [ ! -f "$SYS/$d.mgvf-stock" ]; then
    mv "$SYS/$d" "$SYS/$d.mgvf-stock"
  fi
  cp "$s" "$SYS/$d"
  echo "  $d  <- ${s#$HOME/}"
done

cx="$(find_crossover)" || { echo "error: no CrossOver found in /Applications" >&2; exit 1; }
reachable "$BOTTLE" "$cx" || {
  echo "error: this bottle cannot run reg.exe, so the override cannot be" >&2
  echo "       written. The DLLs are in place; nothing loads them yet." >&2
  exit 1
}
for n in d3d9 qasf quartz winegstreamer; do
  # Written and then asked back, rather than assumed. reg.exe returning 0 is not
  # proof the value is there: this project has a script whose two halves
  # disagreed for exactly that reason, printing "installed" while --status said
  # "broken", for ever.
  if ! wine_in_bottle "$BOTTLE" "$cx" --cx-app reg.exe add "$KEY" \
         /v "*$n" /d "native,builtin" /f >/dev/null 2>&1 \
     || ! wine_in_bottle "$BOTTLE" "$cx" --cx-app reg.exe query "$KEY" \
         /v "*$n" >/dev/null 2>&1; then
    echo "error: could not write the override for $n" >&2
    exit 1
  fi
done
echo "  overrides written for this executable only, through the bottle"

echo "installed"
echo
echo "The boot movie freezes on its first frame -- one click skips it, and the"
echo "game and its cutscenes run. That part is not solved here."
