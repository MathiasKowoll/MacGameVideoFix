#!/usr/bin/env bash
#
# Collect a support bundle: the facts a bug report needs, and nothing else.
#
#   scripts/support-bundle.sh                 ask the questions, then collect
#   scripts/support-bundle.sh --game 1 ...    answer them on the command line
#   scripts/support-bundle.sh --help          the full list of flags
#
# The report is written to stdout and to a file, plain text, for pasting into
# a GitHub issue inside a code fence.
#
# WHAT THIS DOES NOT DO, and why:
#
#   * It never lists, globs or counts the installed game library. Q1 names one
#     title from a closed list and the user names one folder; every probe is
#     for an exact filename inside that folder. A bundle that dumped
#     steamapps/common would put someone's whole library into a public issue.
#   * It never prints a bottle name in the body -- bottle names are chosen by
#     the user and are usually game titles. Bottles are `bottle #N`, and the
#     number-to-name mapping is printed only with --names, under a fence the
#     user is told to review before sending.
#   * It never executes anything inside a CrossOver bundle. Running
#     <bundle>/.../bin/wine --version was observed to start a Preview GUI, take
#     over a wineserver already serving a live session, and re-stamp the Fonts
#     registry key in every Preview-enabled bottle within seconds -- destroying
#     the "which build touched this bottle" evidence section 4 collects.
#     Info.plist is read as a file instead.
#   * It never runs uname -a, cxgetsysinfo, or a raw df: all three print the
#     hostname or every mounted volume by name, and the /Users filter covers
#     neither.
#
# Read-only throughout. The only things executed are this repo's own --status
# flags, which read and print one word.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

# No -e: nearly every collector here is a probe that is *expected* to fail on
# some machines, and an abort halfway through is worse than a missing line.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
RUNTIME="$ROOT/runtime"
CROSSOVER_DIR="$ROOT/crossover"

BOTTLES="$HOME/Library/Application Support/CrossOver/Bottles"
STAGE_ROOT="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs"
FRAMEWORK="/Library/Frameworks/GStreamer.framework/Versions/1.0"
CXSUPPORT="$HOME/Library/Application Support/CrossOver"

OUTFILE="${SUPPORT_BUNDLE_OUT:-$HOME/Desktop/macgamevideofix-support-$(date +%Y%m%d-%H%M%S).txt}"

# Every log basename this toolkit has ever written. README.md:428 still tells
# users to look for ue5-runtime-fix.log while the shipped DLL writes
# ue5-media-fix.log, so both names have to be collected or the file that
# actually exists is missed.
KNOWN_LOGS=(ue5-media-fix.log ue5-runtime-fix.log ue5-vpx-cpupath.log
            electra-h264-fix.log electra-probe.log
            dwo-video-bridge.log p5s-video-bridge.log)

# is_ours() in the installers is a plain substring scan, so a diagnostic build
# reports "installed" and plays nothing. Naming the marker found is the only
# way a reader can tell those apart.
KNOWN_MARKERS=(ue5-media-fix.log ue5-runtime-fix.log ue5-vpx-cpupath.log
               electra-h264-fix.log electra-probe.log
               dwo-video-bridge.log p5s-video-bridge.log mf-observe.log)


# ------------------------------------------------------- the human answers ---

Q_GAME=""; Q_SYMPTOM=""; Q_COMPARED=""; Q_ENGINE=""; Q_STEAMQUIT=""
Q_ANTICHEAT=""; Q_TRIED=""; Q_CRASH=""
GAME_FOLDER=""
SHOW_NAMES=no

GAMES=("Mortal Shell 2"
       "Beast of Reincarnation"
       "Life is Strange: Reunion"
       "Life is Strange: Double Exposure"
       "Another Unreal Engine 5 title"
       "DYNASTY WARRIORS: ORIGINS"
       "Persona 5 Strikers")

SYMPTOMS=("Crashes to desktop with an Unreal crash dialog"
          "Runs fine, then freezes - picture stuck, no crash, force quit needed"
          "Cutscene: sound plays, picture stays black, the game carries on afterwards"
          "Cutscene: no picture and no sound"
          "Black screen at launch, never reaches the menu"
          "The game does not start at all since applying the fix")

COMPARED=("Same as before"
          "Different"
          "I never ran it without the fix")

ENGINES_Q=("CrossOver"
           "CrossOver Preview"
           "A patched or third-party build (e.g. winevideo)"
           "Not sure")

STEAMQUIT=("Yes" "No" "I changed nothing")

TRIED_OPTS=("Reverted and re-applied the fix"
            "Used Steam's 'verify integrity of game files'"
            "Changed CX_GRAPHICS_BACKEND by hand"
            "Gave Persona 5 Strikers a bottle of its own"
            "Installed GStreamer 1.24.x"
            "Launched the game with -dx11"
            "Nothing yet")

usage() {
  sed -n '3,10p' "$0"
  cat <<'EOF'

  --game N              1-7, the title (see the list below)
  --game-folder PATH    the folder the game is installed in
  --symptom N           1-6, what actually happens
  --compared N          1-3, versus before the fix
  --compared-note TEXT  one line, when --compared 2
  --engine N            1-4, which CrossOver you launched the bottle with
  --steam-quit N        1-3, Steam fully quit after the last settings change
  --no-anticheat        confirm the title has no anti-cheat or anti-tamper
  --tried "1,2,..."     what you already tried (see the list below)
  --crash-lines FILE    first three lines of the Unreal crash dialog
  --names               append the bottle number -> name key (review it!)
  -o PATH               where to write the report

  --game            1 Mortal Shell 2
                    2 Beast of Reincarnation
                    3 Life is Strange: Reunion
                    4 Life is Strange: Double Exposure
                    5 Another Unreal Engine 5 title
                    6 DYNASTY WARRIORS: ORIGINS
                    7 Persona 5 Strikers

  --symptom         1 Crashes to desktop with an Unreal crash dialog
                    2 Runs fine, then freezes - no crash, force quit needed
                    3 Cutscene: sound plays, picture stays black
                    4 Cutscene: no picture and no sound
                    5 Black screen at launch, never reaches the menu
                    6 The game does not start at all since applying the fix

  --compared        1 Same as before   2 Different   3 Never ran it without

  --engine          1 CrossOver   2 CrossOver Preview
                    3 A patched or third-party build   4 Not sure

  --steam-quit      1 Yes   2 No   3 I changed nothing

  --tried           1 Reverted and re-applied      2 Verified game files
                    3 Changed CX_GRAPHICS_BACKEND  4 Own bottle for P5S
                    5 Installed GStreamer 1.24.x   6 Launched with -dx11
                    7 Nothing yet
EOF
  exit "${1:-1}"
}

COMPARED_NOTE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --game)          Q_GAME="${2:-}"; shift 2 ;;
    --game-folder)   GAME_FOLDER="${2:-}"; shift 2 ;;
    --symptom)       Q_SYMPTOM="${2:-}"; shift 2 ;;
    --compared)      Q_COMPARED="${2:-}"; shift 2 ;;
    --compared-note) COMPARED_NOTE="${2:-}"; shift 2 ;;
    --engine)        Q_ENGINE="${2:-}"; shift 2 ;;
    --steam-quit)    Q_STEAMQUIT="${2:-}"; shift 2 ;;
    --no-anticheat)  Q_ANTICHEAT=yes; shift ;;
    --tried)         Q_TRIED="${2:-}"; shift 2 ;;
    --crash-lines)   Q_CRASH="${2:-}"; shift 2 ;;
    --names)         SHOW_NAMES=yes; shift ;;
    -o)              OUTFILE="${2:-}"; shift 2 ;;
    -h|--help)       usage 0 ;;
    *) echo "unknown option: $1" >&2; usage 1 ;;
  esac
done

# Prompt only when there is somebody there to answer. Piped or scripted, the
# unanswered questions are reported as unanswered rather than guessed -- a
# wrong answer to Q5 costs more than a missing one.
INTERACTIVE=no
[ -t 0 ] && [ -t 1 ] && INTERACTIVE=yes

ask_one() {  # ask_one <varname> <prompt> <option>...
  local __var="$1" prompt="$2"; shift 2
  local -a opts=("$@")
  local i reply
  eval "local cur=\${$__var}"
  [ -n "$cur" ] && return 0
  [ "$INTERACTIVE" = yes ] || return 0
  printf '\n%s\n' "$prompt" >&2
  for i in "${!opts[@]}"; do printf '  %d) %s\n' "$((i+1))" "${opts[$i]}" >&2; done
  while :; do
    printf '  choice: ' >&2
    IFS= read -r reply || { reply=""; break; }
    case "$reply" in
      ''|*[!0-9]*) ;;
      *) [ "$reply" -ge 1 ] && [ "$reply" -le "${#opts[@]}" ] && break ;;
    esac
    printf '  pick 1-%d\n' "${#opts[@]}" >&2
  done
  printf -v "$__var" '%s' "$reply"
}

pick() {  # pick <index-1-based> <option>...
  local n="$1"; shift
  case "$n" in
    ''|*[!0-9]*) echo "(not answered)"; return ;;
  esac
  [ "$n" -ge 1 ] && [ "$n" -le $# ] || { echo "(not answered)"; return; }
  eval "printf '%s' \"\${$n}\""
}

if [ "$INTERACTIVE" = yes ]; then
  cat >&2 <<'INTRO'
MacGameVideoFix support bundle.

Seven questions, then it collects. Nothing is sent anywhere: the report is
written to a file and printed, and you decide what to do with it.

It does not look at your game library. It probes for one executable name
inside one folder you name, and finds bottles by this toolkit's own log
filenames.
INTRO
fi

# Form order, with each follow-up beside the question it belongs to. The folder
# question rides with Q1 because it is about the same thing, and because every
# later probe is scoped to it.
ask_one Q_GAME "Which game?" "${GAMES[@]}"

if [ "$INTERACTIVE" = yes ] && [ -z "$GAME_FOLDER" ]; then
  printf '\nThe folder this game is installed in (drag it in, or leave blank):\n  ' >&2
  IFS= read -r GAME_FOLDER || GAME_FOLDER=""
  # Dragging a folder into Terminal is what we just asked for, and Terminal
  # backslash-escapes the spaces on the way in. A quoted paste is the other
  # common shape. Undo both, or "Application Support" arrives as two words.
  GAME_FOLDER="${GAME_FOLDER#\'}"; GAME_FOLDER="${GAME_FOLDER%\'}"
  GAME_FOLDER="${GAME_FOLDER#\"}"; GAME_FOLDER="${GAME_FOLDER%\"}"
  GAME_FOLDER="${GAME_FOLDER//\\ / }"
  GAME_FOLDER="${GAME_FOLDER%/}"
fi

ask_one Q_SYMPTOM "What actually happens?" "${SYMPTOMS[@]}"

ask_one Q_COMPARED "Compared with before you applied the fix?" "${COMPARED[@]}"
if [ "$INTERACTIVE" = yes ] && [ "$Q_COMPARED" = 2 ] && [ -z "$COMPARED_NOTE" ]; then
  printf '  one line saying how: ' >&2
  IFS= read -r COMPARED_NOTE || COMPARED_NOTE=""
fi

ask_one Q_ENGINE "Which CrossOver did you launch the bottle with?" "${ENGINES_Q[@]}"

ask_one Q_STEAMQUIT "After the last change to the bottle's settings, did you quit Steam
completely - not just the game - and then relaunch?" "${STEAMQUIT[@]}"

# Required, and explicit only for the generic mode -- which puts the warning in
# front of the person most likely to need it: whoever is pointing an unvalidated
# title at a toolkit that patches a running process.
if [ "$INTERACTIVE" = yes ] && [ -z "$Q_ANTICHEAT" ]; then
  if [ "$Q_GAME" = 5 ]; then
    printf '\nEvery fix here patches a running process, which is exactly what\n' >&2
    printf 'anti-cheat exists to stop, and the Beast of Reincarnation half also\n' >&2
    printf 'writes to the executable. Confirm this title has no anti-cheat or\n' >&2
    printf 'anti-tamper (y/N): ' >&2
    IFS= read -r reply || reply=""
    case "$reply" in [Yy]*) Q_ANTICHEAT=yes ;; *) Q_ANTICHEAT=no ;; esac
  else
    Q_ANTICHEAT=yes   # pre-satisfied for the six named titles
  fi
fi

if [ "$INTERACTIVE" = yes ] && [ -z "$Q_TRIED" ]; then
  printf '\nWhat have you already tried? (numbers, comma separated, or blank)\n' >&2
  for i in "${!TRIED_OPTS[@]}"; do printf '  %d) %s\n' "$((i+1))" "${TRIED_OPTS[$i]}" >&2; done
  printf '  choice: ' >&2
  IFS= read -r Q_TRIED || Q_TRIED=""
fi

CRASH_TEXT=""
if [ -n "$Q_CRASH" ] && [ -f "$Q_CRASH" ]; then
  CRASH_TEXT="$(head -3 "$Q_CRASH")"
elif [ "$INTERACTIVE" = yes ] && [ "$Q_SYMPTOM" = 1 ]; then
  printf '\nPaste the first three lines of the crash dialog, verbatim.\n' >&2
  printf '(at most three lines; blank line to finish)\n' >&2
  for _ in 1 2 3; do
    printf '  ' >&2
    IFS= read -r line || break
    [ -z "$line" ] && break
    CRASH_TEXT="${CRASH_TEXT}${CRASH_TEXT:+$'\n'}$line"
  done
fi


# ---------------------------------------------- the title Q1 routes us to ---

GAME_LABEL="$(pick "$Q_GAME" "${GAMES[@]}")"
GAME_KEY=""; GAME_EXE=""; GAME_PROJ=""; GAME_FAMILY=""
GAME_INSTALLER=""; GAME_LOG=""; GAME_BACKEND=""; GAME_CARRIER=""; GAME_REAL=""
HALVES_EXPECTED=""

case "${Q_GAME:-}" in
  1) GAME_KEY=ms2;     GAME_EXE="MortalShell2-Win64-Shipping.exe";        GAME_PROJ="MortalShell2"
     HALVES_EXPECTED="electra-vpx on | node-guard off | electra-h264 off" ;;
  2) GAME_KEY=beast;   GAME_EXE="BeastOfReincarnation-Win64-Shipping.exe"; GAME_PROJ="BeastOfReincarnation"
     HALVES_EXPECTED="electra-vpx off | node-guard off | electra-h264 on" ;;
  3) GAME_KEY=iris;    GAME_EXE="Iris-Win64-Shipping.exe";                GAME_PROJ="Iris"
     HALVES_EXPECTED="electra-vpx off | node-guard on | electra-h264 off" ;;
  4) GAME_KEY=chronos; GAME_EXE="Chronos-Win64-Shipping.exe";             GAME_PROJ="Chronos"
     HALVES_EXPECTED="electra-vpx off | node-guard on | electra-h264 off" ;;
  5) GAME_KEY=ue5;     GAME_EXE="";                                       GAME_PROJ=""
     HALVES_EXPECTED="all three on, preceded by the unknown-title warning" ;;
  6) GAME_KEY=dwo;     GAME_EXE="DWORIGINS.exe";                          GAME_PROJ="" ;;
  7) GAME_KEY=p5s;     GAME_EXE="game.exe";                               GAME_PROJ="" ;;
esac

case "$GAME_KEY" in
  ms2|beast|iris|chronos|ue5)
    GAME_FAMILY=ue5;  GAME_INSTALLER="install-runtime-fix.sh"
    GAME_LOG="ue5-media-fix.log"; GAME_BACKEND="d3dmetal"
    GAME_CARRIER="libogg_64.dll"; GAME_REAL="libogg_64_real.dll" ;;
  dwo)
    GAME_FAMILY=dwo;  GAME_INSTALLER="install-dwo-bridge.sh"
    GAME_LOG="dwo-video-bridge.log"; GAME_BACKEND="d3dmetal"
    GAME_CARRIER="libxess.dll"; GAME_REAL="libxess_real.dll" ;;
  p5s)
    GAME_FAMILY=p5s;  GAME_INSTALLER="install-p5s-bridge.sh"
    GAME_LOG="p5s-video-bridge.log"; GAME_BACKEND="dxmt"
    GAME_CARRIER="amd_ags_x64.dll"; GAME_REAL="amd_ags_x64_real.dll" ;;
esac


# -------------------------------------------------------------- utilities ---

sha_short() { [ -f "$1" ] && shasum -a 256 "$1" 2>/dev/null | cut -c1-16; }

size_of()  { [ -e "$1" ] && wc -c < "$1" 2>/dev/null | tr -d ' '; }
mtime_of() { [ -e "$1" ] && date -r "$1" '+%Y-%m-%d %H:%M' 2>/dev/null; }

yesno() { [ -e "$1" ] && echo yes || echo no; }

# Anchor on the key at the start of the line: cxbottle.conf carries the same
# keys again inside its own ;;-prefixed documentation, and values legitimately
# contain spaces ("Application Support"), so they must never be word-split.
# Do not copy launch-and-capture.sh:39 -- its [a-z0-9]* class silently yields
# empty for any value with an underscore or a hyphen in it.
conf_get() {  # conf_get <conf> <KEY>
  [ -f "$1" ] || return 1
  sed -n "s/^\"$2\"[[:space:]]*=[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$1" 2>/dev/null | head -1
}

# Which markers a DLL carries. Names them rather than collapsing to "ours",
# because a probe build satisfies is_ours() and plays nothing.
markers_in() {
  local f="$1" m out=""
  [ -f "$f" ] || return 1
  for m in "${KNOWN_MARKERS[@]}"; do
    LC_ALL=C grep -qa "$m" "$f" 2>/dev/null && out="${out}${out:+, }$m"
  done
  printf '%s' "${out:-none}"
}

# build-proxy.sh bakes "<stem>_real" into the export forwarders, so the target
# is readable straight out of the PE. This is what catches a current proxy
# sitting beside only the legacy libogg_real.dll.
forwarder_of() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -oa '[A-Za-z0-9_]*_real\.[A-Za-z0-9_@]*' "$1" 2>/dev/null \
    | sed 's/\..*//' | sort -u | head -1
}

export_count() {
  [ -f "$1" ] || return 1
  python3 "$RUNTIME/pe.py" exports "$1" 2>/dev/null | grep -c .
}


# --------------------------------------------------- the numbering contract ---
#
# ONE iteration order for every bottle-keyed line in the whole report. If a
# collector deviates, the indices stop agreeing and the section 6 key becomes
# wrong rather than merely private.

BOTTLE_DIRS=()
BOTTLE_TOTAL_DIRS=0
if [ -d "$BOTTLES" ]; then
  while IFS= read -r -d '' d; do
    BOTTLE_TOTAL_DIRS=$((BOTTLE_TOTAL_DIRS + 1))
    [ -f "$d/cxbottle.conf" ] && BOTTLE_DIRS+=("$d")
  done < <(find "$BOTTLES" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
fi

bottle_index() {  # bottle_index <dir> -> 1-based index, or empty
  local want="$1" i
  for i in "${!BOTTLE_DIRS[@]}"; do
    [ "${BOTTLE_DIRS[$i]}" = "$want" ] && { echo $((i + 1)); return 0; }
  done
  return 1
}

bottle_dir() { echo "${BOTTLE_DIRS[$(( $1 - 1 ))]:-}"; }

# A wineserver's cwd is /private/tmp/.wine-<uid>/server-<dev-hex>-<inode-hex>,
# where dev and inode are the bottle directory's. That tag discloses nothing,
# which is the point: `ps` with arguments would print the bottle path.
bottle_tag() {
  local dev ino
  read -r dev ino <<<"$(stat -f '%d %i' "$1" 2>/dev/null)" || return 1
  [ -n "${dev:-}" ] || return 1
  printf 'server-%x-%x' "$dev" "$ino"
}

# The binaries are wineserver-x86 and wineserver-arm64, so `pgrep -x wineserver`
# is a silent false negative -- it finds nothing and the report then says no
# wineserver is running while one is caching the old bottle settings.
WS_PIDS=()
while IFS= read -r p; do [ -n "$p" ] && WS_PIDS+=("$p"); done < <(
  ps -Ao pid=,comm= 2>/dev/null | sed 's|/[^/ ]*$|/&|' \
    | awk '{ n=$0; sub(/^[ ]*[0-9]+[ ]+/,"",n); sub(/.*\//,"",n);
             if (n ~ /^wineserver/) print $1 }'
)
WS_COUNT=${#WS_PIDS[@]}

WS_TAGS=""
for p in ${WS_PIDS[@]+"${WS_PIDS[@]}"}; do
  cwd="$(lsof -a -p "$p" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p' | head -1)"
  [ -n "$cwd" ] && WS_TAGS="${WS_TAGS}${WS_TAGS:+ }$(basename "$cwd")"
done

bottle_has_wineserver() {  # bottle_has_wineserver <dir>
  local tag; tag="$(bottle_tag "$1")" || return 1
  case " $WS_TAGS " in *" $tag "*) return 0 ;; esac
  return 1
}

settings_caveat() {  # printed beside every bottle-settings line
  bottle_has_wineserver "$1" \
    && echo "  << a wineserver is live for this bottle -- may not be what is in effect"
}


# ---------------------------------------- the engines installed (read-only) ---
#
# Info.plist is read as a file. Nothing here shells into a CrossOver bundle.

CX_APPS=()
for root in /Applications "$HOME/Applications"; do
  [ -d "$root" ] || continue
  while IFS= read -r -d '' a; do CX_APPS+=("$a"); done < <(
    find "$root" -maxdepth 1 -name '*.app' -print0 2>/dev/null \
      | while IFS= read -r -d '' c; do
          case "$(basename "$c")" in
            *[Cc]ross[Oo]ver*) printf '%s\0' "$c" ;;
          esac
        done
  )
done

plist_get() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null; }

# CFBundleName is the only reliable tell. Both product lines ship
# CFBundleIdentifier com.codeweavers.CrossOver, and a Preview build can sit
# under a non-Preview .app filename -- which is exactly the case on the machine
# these facts were confirmed against.
cx_line() {
  local name="$1"
  case "$name" in
    *Preview*) echo "Preview" ;;
    *)         echo "stable" ;;
  esac
}


# ----------------------------------------------- the game folder Q1 selects ---
#
# Everything below is scoped to the one folder the user named. Nothing outside
# it is looked at, and the folder itself is never printed.

GAME_ROOT=""       # the Unreal project root, for the UE5 family
OGG_DIR=""         # the VS20xx carrier folder inside it
FOLDER_STATE="not given"

if [ -n "$GAME_FOLDER" ]; then
  if [ -d "$GAME_FOLDER" ]; then
    FOLDER_STATE="ok"
    if [ "$GAME_FAMILY" = ue5 ]; then
      probe="$GAME_FOLDER"
      for _ in 1 2 3 4 5; do
        [ -d "$probe/Engine/Binaries/ThirdParty/Ogg/Win64" ] && { GAME_ROOT="$probe"; break; }
        probe="$(dirname "$probe")"
      done
      if [ -n "$GAME_ROOT" ]; then
        # Same walk the installer does: the VS20xx folder name changes between
        # engine versions, and a half-install has the saved original and no
        # live DLL, which must not read as "this game ships no libogg".
        while IFS= read -r -d '' d; do
          d="${d%/}"
          if [ -f "$d/libogg_64.dll" ]; then OGG_DIR="$d"; break; fi
          if [ -f "$d/libogg_64_real.dll" ] || [ -f "$d/libogg_real.dll" ]; then OGG_DIR="$d"; fi
        done < <(find "$GAME_ROOT/Engine/Binaries/ThirdParty/Ogg/Win64" \
                      -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
      fi
    fi
  else
    FOLDER_STATE="no such folder"
  fi
fi

# Probe for the one executable Q1 names, by exact basename, inside that folder
# only -- and report yes/no, never where. game.exe is generic enough to match
# unrelated titles, so it is gated on the sibling data/pd directory, which is
# how the app identifies Persona 5 Strikers.
EXE_FOUND="not probed"
if [ -n "$GAME_ROOT$GAME_FOLDER" ] && [ "$FOLDER_STATE" = ok ] && [ -n "$GAME_EXE" ]; then
  scope="${GAME_ROOT:-$GAME_FOLDER}"
  if [ "$GAME_KEY" = p5s ]; then
    if [ -f "$GAME_FOLDER/game.exe" ] && [ -d "$GAME_FOLDER/data/pd" ]; then
      EXE_FOUND="yes (game.exe with data/pd beside it)"
    elif [ -f "$GAME_FOLDER/game.exe" ]; then
      EXE_FOUND="game.exe is there but data/pd is not -- probably not this title"
    else
      EXE_FOUND="no"
    fi
  else
    n="$(find "$scope" -maxdepth 4 -type f -name "$GAME_EXE" -print 2>/dev/null | grep -c .)"
    [ "${n:-0}" -gt 0 ] && EXE_FOUND="yes" || EXE_FOUND="no"
  fi
fi


# ---------------------------------------------------- install state (--status)

INSTALL_STATE=""; INSTALL_RC=""; INSTALL_ERR=""
if [ -n "$GAME_INSTALLER" ] && [ "$FOLDER_STATE" = ok ] && [ -x "$RUNTIME/$GAME_INSTALLER" ]; then
  ERRTMP="$(mktemp -t mgvf-status)"
  INSTALL_STATE="$(bash "$RUNTIME/$GAME_INSTALLER" "$GAME_FOLDER" --status 2>"$ERRTMP")"
  INSTALL_RC=$?
  INSTALL_ERR="$(cat "$ERRTMP")"
  rm -f "$ERRTMP"
fi


# ----------------------------------------------- where our logs actually are ---
#
# Bottles are selected by this toolkit's own exact log basenames, never by
# listing Bottles/. It satisfies the privacy rule and doubles as evidence: it
# surfaces only bottles where one of our DLLs has actually run.

LOG_PATHS=(); LOG_NAMES=(); LOG_BIDX=()
for name in "${KNOWN_LOGS[@]}"; do
  [ -d "$BOTTLES" ] || break
  while IFS= read -r -d '' f; do
    b="$(dirname "$(dirname "$f")")"
    idx="$(bottle_index "$b")" || idx="?"
    LOG_PATHS+=("$f"); LOG_NAMES+=("$name"); LOG_BIDX+=("$idx")
  done < <(find "$BOTTLES" -maxdepth 3 -type f -name "$name" -print0 2>/dev/null)
done

# The one log Q1 points at, and the bottle holding it.
TARGET_LOG=""; TARGET_BIDX=""; TARGET_BDIR=""
if [ -n "$GAME_LOG" ]; then
  for i in ${LOG_PATHS[@]+"${!LOG_PATHS[@]}"}; do
    [ "${LOG_NAMES[$i]}" = "$GAME_LOG" ] || continue
    TARGET_LOG="${LOG_PATHS[$i]}"; TARGET_BIDX="${LOG_BIDX[$i]}"
    TARGET_BDIR="$(dirname "$(dirname "$TARGET_LOG")")"
    break
  done
fi

# The log accumulates across every run and every title that has ever run in
# that bottle -- one file, opened FILE_APPEND_DATA and never truncated. A
# "VPx version checks: 4 found" line left there by a different game would be
# read as evidence about this one, so where Q1 names an exe the per-fact greps
# read only that title's lines. The head/tail excerpt and the census stay
# unfiltered, because seeing the mixture is the point of them.
SCOPED_LOG="$TARGET_LOG"
SCOPE_NOTE="the whole file (no per-process prefix to scope by)"
TITLE_LINES=""
if [ -n "$TARGET_LOG" ] && [ -n "$GAME_EXE" ] && [ "$GAME_FAMILY" != dwo ]; then
  SCOPED="$(mktemp -t mgvf-scoped)"
  grep -a -F "[$GAME_EXE]" "$TARGET_LOG" > "$SCOPED" 2>/dev/null
  TITLE_LINES="$(grep -c . "$SCOPED" 2>/dev/null)"
  if [ "${TITLE_LINES:-0}" -gt 0 ]; then
    SCOPED_LOG="$SCOPED"; SCOPE_NOTE="only the [$GAME_EXE] lines"
  else
    SCOPE_NOTE="the whole file -- NO [$GAME_EXE] lines in it at all"
  fi
fi

# Bottles that matter to this report: the ones our logs are in. Falling back to
# every bottle would force the section 6 key to disclose names irrelevant here.
RELEVANT=()
for i in ${LOG_BIDX[@]+"${!LOG_BIDX[@]}"}; do
  idx="${LOG_BIDX[$i]}"
  case " ${RELEVANT[*]-} " in *" $idx "*) ;; *) [ "$idx" != "?" ] && RELEVANT+=("$idx") ;; esac
done


# ================================================================ the report ==

RAW="$(mktemp -t mgvf-support-raw)"
trap 'rm -f "$RAW" "${SCOPED:-}"' EXIT

report() {

hr() { printf '%s\n' "------------------------------------------------------------------------"; }

cat <<EOF
========================================================================
MacGameVideoFix support bundle
collected $(date '+%Y-%m-%d %H:%M %Z')
========================================================================
EOF

# ---- the one line a triager reads first -------------------------------------
{
  gst="$(otool -L "$FRAMEWORK/lib/libgstreamer-1.0.0.dylib" 2>/dev/null \
         | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1)"
  if [ -n "${gst:-}" ] && [ "$gst" -gt 0 ] 2>/dev/null; then
    gst="1.$((gst / 100)).$((gst % 100))"
  else
    gst="not installed"
  fi
  if [ -n "$TARGET_BDIR" ]; then
    live_backend="$(conf_get "$TARGET_BDIR/cxbottle.conf" CX_GRAPHICS_BACKEND)"
    [ -z "$live_backend" ] && live_backend="<unset>"
  else
    live_backend="n/a, no bottle"
  fi
  printf 'VERDICT: %s | state %s | log %s | backend %s (needs %s) | gstreamer %s\n' \
    "${GAME_LABEL:-(no title given)}" \
    "${INSTALL_STATE:-not checked, no folder given}" \
    "$([ -n "$TARGET_LOG" ] && echo "present in bottle #$TARGET_BIDX" || echo absent)" \
    "$live_backend" "${GAME_BACKEND:-?}" "$gst"
}
echo

# ============================== SECTION 1 =====================================
hr
echo "SECTION 1 - VERDICT BLOCK"
echo "(a reader should be able to stop here)"
hr

echo "1. TITLE + REQUIRED BACKEND"
echo "   title            : ${GAME_LABEL:-(not answered)}"
if [ -n "$GAME_BACKEND" ]; then
  echo "   requires         : CX_GRAPHICS_BACKEND = $GAME_BACKEND"
  if [ "$GAME_BACKEND" = dxmt ]; then
    echo "                      dxmt and only dxmt; it cannot share a bottle with"
    echo "                      the five d3dmetal titles."
  else
    echo "                      d3dmetal; Persona 5 Strikers needs dxmt instead and"
    echo "                      cannot share a bottle with this one."
  fi
fi
echo "   carrier DLL      : ${GAME_CARRIER:-(unknown)}"
echo

echo "2. INSTALL STATE  (${GAME_INSTALLER:-no installer selected} \"<game folder>\" --status)"
if [ -z "$GAME_INSTALLER" ]; then
  echo "   not run          : no title given"
elif [ "$FOLDER_STATE" != ok ]; then
  echo "   not run          : game folder $FOLDER_STATE"
else
  echo "   state word       : ${INSTALL_STATE:-<none>}"
  echo "   exit code        : ${INSTALL_RC:-?}"
  if [ "${INSTALL_RC:-0}" != 0 ]; then
    echo "                      rc=1 means status() was never reached: wrong folder,"
    echo "                      or this build ships no carrier. Different from rc=0"
    echo "                      absent, which means the carrier is there and we are"
    echo "                      simply not installed."
  fi
  if [ -n "$INSTALL_ERR" ]; then
    echo "   stderr           :"
    printf '%s\n' "$INSTALL_ERR" | sed 's/^/     /'
  else
    echo "   stderr           : (none)"
  fi
  echo "   vocabulary       : installed | broken | half | absent"
  [ "$GAME_FAMILY" = dwo ] && \
  echo "                      install-dwo-bridge.sh has no half branch and emits"
  [ "$GAME_FAMILY" = dwo ] && \
  echo "                      only installed|broken|absent (see section 3)."
  echo "   exe probe        : ${GAME_EXE:-(generic UE5 title, no fixed name)} -> $EXE_FOUND"
fi
echo

echo "3. DID THE DLL LOAD"
if [ -z "$GAME_LOG" ]; then
  echo "   not determined   : no title given"
elif [ -n "$TARGET_LOG" ]; then
  echo "   $GAME_LOG"
  echo "     bottle         : #$TARGET_BIDX"
  echo "     size           : $(size_of "$TARGET_LOG") bytes"
  echo "     last written   : $(mtime_of "$TARGET_LOG")"
  if [ -n "$GAME_EXE" ] && [ "$GAME_FAMILY" != dwo ]; then
    echo "     lines from $GAME_EXE: ${TITLE_LINES:-0}"
    if [ "${TITLE_LINES:-0}" -eq 0 ]; then
      echo "                      THE FILE EXISTS BUT THIS TITLE NEVER WROTE TO IT."
      echo "                      The proxy has run for some other game in this bottle;"
      echo "                      for this one it was never mapped."
    fi
  fi
else
  echo "   $GAME_LOG       : NO FILE AT ALL"
  echo "     The proxy was never mapped. Nothing downstream is worth reading:"
  echo "     no log means the carrier is not being loaded by this process."
fi
echo

echo "4. CX_GRAPHICS_BACKEND of the bottle holding that log"
if [ -n "$TARGET_BDIR" ]; then
  v="$(conf_get "$TARGET_BDIR/cxbottle.conf" CX_GRAPHICS_BACKEND)"
  if [ -n "$v" ]; then
    printf '   %-17s: "%s"%s\n' "bottle #$TARGET_BIDX" "$v" "$(settings_caveat "$TARGET_BDIR")"
    [ -n "$GAME_BACKEND" ] && [ "$v" != "$GAME_BACKEND" ] && \
      echo "                      MISMATCH -- this title needs \"$GAME_BACKEND\""
  else
    printf '   %-17s: <unset -- CrossOver default>%s\n' "bottle #$TARGET_BIDX" "$(settings_caveat "$TARGET_BDIR")"
    echo "                      Unset is a third state, distinct from a wrong value."
  fi
  echo "                      Nothing in this project ever writes this key, so it"
  echo "                      is always a hand edit and never self-heals."
else
  echo "   not determined   : no bottle identified (see fact 3)"
fi
echo

echo "5. LIVE WINESERVER"
echo "   wineserver procs : $WS_COUNT   (binaries are wineserver-x86 / wineserver-arm64)"
if [ -n "$TARGET_BDIR" ]; then
  if bottle_has_wineserver "$TARGET_BDIR"; then
    printf '   %-17s: ALIVE\n' "bottle #$TARGET_BIDX"
    echo "                      Both bottle settings are read at bottle start and a"
    echo "                      live wineserver caches the old copy. EVERY bottle-"
    echo "                      settings line in this report may not be what is in"
    echo "                      effect. Closing the game is not enough; Steam must"
    echo "                      be fully closed."
  else
    printf '   %-17s: none\n' "bottle #$TARGET_BIDX"
  fi
fi
echo

echo "6. ENGINES INSTALLED + ENGINE USED"
if [ ${#CX_APPS[@]} -eq 0 ]; then
  echo "   no CrossOver bundle found in /Applications or ~/Applications"
else
  for a in "${CX_APPS[@]}"; do
    n="$(plist_get "$a" CFBundleName)"
    printf '   %-18s %-14s %-16s %s\n' "${n:-?}" \
      "$(plist_get "$a" CFBundleShortVersionString)" \
      "$(plist_get "$a" CFBundleVersion)" "$(cx_line "${n:-}")"
  done
  echo "   (CFBundleName is the only reliable stable-vs-Preview tell: both lines"
  echo "    ship CFBundleIdentifier com.codeweavers.CrossOver, and a Preview build"
  echo "    can sit under a non-Preview .app filename.)"
fi
echo "   Q4, engine used  : $(pick "$Q_ENGINE" "${ENGINES_Q[@]}")"
echo

echo "7. HOST"
echo "   macOS            : $(sw_vers -productVersion 2>/dev/null) ($(sw_vers -buildVersion 2>/dev/null))"
echo "   chip             : $(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
echo "   kernel arch      : $(uname -m 2>/dev/null)"
echo "   collecting shell : $(arch 2>/dev/null)   proc_translated=$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)"
if [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)" = 1 ]; then
  echo "                      COLLECTED UNDER ROSETTA. This bundle reports x86_64"
  echo "                      for the shell and misleads every later inference."
fi
echo "   Rosetta present  : $(yesno /Library/Apple/usr/libexec/oah)"
echo "                      A WineArch=win64 bottle selects the x86_64 host, so"
echo "                      without Rosetta it cannot run at all."
echo

echo "8. UE5 FINGERPRINT"
if [ "$GAME_FAMILY" != ue5 ]; then
  echo "   n/a for this title"
elif [ -z "$TARGET_LOG" ]; then
  echo "   no log to read"
else
  h="$(grep -a 'halves for this title:' "$SCOPED_LOG" 2>/dev/null | tail -1)"
  w="$(grep -ac 'not a title this build knows -- arming everything' "$SCOPED_LOG" 2>/dev/null)"
  echo "   read from        : $SCOPE_NOTE"
  echo "   observed         : ${h:-<no halves line for this title>}"
  echo "   expected for Q1  : $HALVES_EXPECTED"
  echo "   unknown-title warning present: $([ "${w:-0}" -gt 0 ] && echo YES || echo no)"
  if [ "${w:-0}" -gt 0 ] && [ "$GAME_KEY" != ue5 ]; then
    echo "                      The warning on a named title means an older DLL"
    echo "                      whose policy table predates that game."
  fi
  echo "                      A halves line that does not match the title means the"
  echo "                      exe was renamed or the wrong game was patched."
fi
echo

echo "9. DWO FULL LOG"
if [ "$GAME_FAMILY" != dwo ]; then
  echo "   n/a for this title"
elif [ -z "$TARGET_LOG" ]; then
  echo "   no dwo-video-bridge.log"
else
  echo "   (safe whole: no process prefix, no paths, no line cap)"
  sed 's/^/     /' "$TARGET_LOG"
fi
echo

echo "10. THE TWO HUMAN ANSWERS THAT CHANGE THE READING"
c="$(pick "$Q_COMPARED" "${COMPARED[@]}")"
echo "   Q3 vs before fix : $c"
[ -n "$COMPARED_NOTE" ] && echo "                      how: $COMPARED_NOTE"
echo "   Q5 Steam quit    : $(pick "$Q_STEAMQUIT" "${STEAMQUIT[@]}")"
if [ "${Q_STEAMQUIT:-}" = 2 ]; then
  echo "                      \"No\" beside a settings mismatch closes a large class"
  echo "                      of reports without opening a log."
fi
echo
echo "   Q2 symptom       : $(pick "$Q_SYMPTOM" "${SYMPTOMS[@]}")"
echo "   Q6 anti-cheat    : $([ "$Q_ANTICHEAT" = yes ] && echo "confirmed none" || echo "NOT CONFIRMED")"
if [ "$Q_ANTICHEAT" != yes ]; then
  echo "                      These fixes patch a running process and must NEVER be"
  echo "                      used on a title with anti-cheat or anti-tamper."
fi
echo "   Q7 already tried :"
if [ -n "$Q_TRIED" ]; then
  printf '%s\n' "$Q_TRIED" | tr ',' '\n' | while IFS= read -r t; do
    t="$(printf '%s' "$t" | tr -d '[:space:]')"
    [ -n "$t" ] || continue
    echo "     - $(pick "$t" "${TRIED_OPTS[@]}")"
  done
else
  echo "     (not answered)"
fi
if [ -n "$CRASH_TEXT" ]; then
  echo "   Q8 crash dialog, first three lines:"
  printf '%s\n' "$CRASH_TEXT" | head -3 | sed 's/^/     | /'
  echo "     (the runtime patch aims at one crash: EXCEPTION_ACCESS_VIOLATION"
  echo "      reading 0x0 inside FElectraMediaDecoderOutputBufferPoolBlock_DX12::"
  echo "      AllocateBuffer, from FVideoDecoderVPxElectra::"
  echo "      ConvertDecodedImageToNV12orP010. A different function is a crash"
  echo "      this tool will not help with.)"
fi
echo

# ============================== SECTION 2 =====================================
hr
echo "SECTION 2 - HOW FAR THE RUN GOT (log evidence)"
echo "(read only when section 1 says the DLL loaded and the state word is installed)"
hr

echo "DISCOVERY, by exact basename, across all bottles:"
if [ ${#LOG_PATHS[@]} -eq 0 ]; then
  echo "   none of the seven known log names exists in any bottle"
else
  for i in "${!LOG_PATHS[@]}"; do
    printf '   %-22s bottle #%-3s %8s bytes  %s\n' \
      "${LOG_NAMES[$i]}" "${LOG_BIDX[$i]}" \
      "$(size_of "${LOG_PATHS[$i]}")" "$(mtime_of "${LOG_PATHS[$i]}")"
  done
fi
echo "   (README.md:428 still names ue5-runtime-fix.log while the shipped DLL"
echo "    writes ue5-media-fix.log, so both are collected.)"
echo

if [ -n "$TARGET_LOG" ] && [ "$GAME_FAMILY" != dwo ]; then
  case "$GAME_FAMILY" in
    ue5) cap="200 lines per process start (ue5-media-fix.c:94)" ;;
    p5s) cap="300 lines per process start (p5s-video-bridge.c:80)" ;;
    *)   cap="unknown" ;;
  esac
  echo "EXCERPT of $GAME_LOG  -- per-run line cap: $cap"
  echo "   (without the cap stated, \"the log just ends\" reads as a crash that"
  echo "    never happened)"
  echo "   --- head -30 ---"
  head -30 "$TARGET_LOG" | sed 's/^/   | /'
  echo "   --- tail -20 ---"
  tail -20 "$TARGET_LOG" | sed 's/^/   | /'
  echo

  echo "PER-PROCESS CENSUS of that file"
  echo "   The file is opened FILE_APPEND_DATA / OPEN_ALWAYS and never truncated,"
  echo "   so it accumulates across every run and every title in that bottle --"
  echo "   the top of it may be months old. Each line is prefixed [<exe>], so this"
  echo "   census names which of the supported titles has run in that bottle."
  awk '{
      if (match($0, /^\[[^]]*\]/)) { e = substr($0, 2, RLENGTH - 2) }
      else { e = "(no prefix)" }
      n[e]++; if (!(e in f)) f[e] = NR; l[e] = NR
    }
    END { for (e in n) printf "   %-44s %6d lines, %d..%d\n", e, n[e], f[e], l[e] }' \
    "$TARGET_LOG"
  echo
fi

if [ "$GAME_FAMILY" = ue5 ] && [ -n "$SCOPED_LOG" ]; then
  echo "The facts below are read from: $SCOPE_NOTE"
  echo
  echo "MEDIA FOUNDATION REACHED AT ALL"
  m="$(grep -a 'Media Foundation IS in play' "$SCOPED_LOG" 2>/dev/null | tail -2)"
  if [ -n "$m" ]; then printf '%s\n' "$m" | sed 's/^/   | /'; else
    echo "   no MFStartup line."
    echo "   These titles delay-load MF, so the hook fires only when the game"
    echo "   resolves it. With a cutscene attempted and no MFStartup line, the"
    echo "   game plays video by another path and the MF half is irrelevant."
  fi
  g="$(grep -a 'GetProcAddress' "$SCOPED_LOG" 2>/dev/null | grep -a 'MF' | tail -5)"
  [ -n "$g" ] && { echo "   GetProcAddress(\"MF*\"):"; printf '%s\n' "$g" | sed 's/^/   | /'; }
  echo

  echo "DECODER AVAILABILITY"
  d="$(grep -a 'decoder(s) offered' "$SCOPED_LOG" 2>/dev/null | tail -5)"
  w="$(grep -a 'wants to decode' "$SCOPED_LOG" 2>/dev/null | tail -5)"
  none="$(grep -ac 'NOTHING can decode that here' "$SCOPED_LOG" 2>/dev/null)"
  [ -n "$d" ] && printf '%s\n' "$d" | sed 's/^/   | /' || echo "   no MFTEnumEx line"
  [ -n "$w" ] && printf '%s\n' "$w" | sed 's/^/   | /'
  echo "   \"NOTHING can decode that here\": $([ "${none:-0}" -gt 0 ] && echo "YES x${none}" || echo no)"
  if [ "${none:-0}" -gt 0 ]; then
    echo "   Zero decoders with a FourCC of WVC1/WMV3/WMA means the engine has no"
    echo "   such codec: the answer is the staged GStreamer plugin (section 5),"
    echo "   not the DLL. This is the line that most directly matches"
    echo "   \"sound plays, picture black\"."
  fi
  echo

  echo "FRAMES"
  ok="$(grep -ac 'decoded OK' "$SCOPED_LOG" 2>/dev/null)"
  hi="$(grep -ao 'frame [0-9]* decoded OK' "$SCOPED_LOG" 2>/dev/null \
        | awk '{ if ($2+0 > m) m = $2+0 } END { print m+0 }')"
  echo "   \"decoded OK\" lines : ${ok:-0}   highest frame N: ${hi:-0}"
  po="$(grep -a 'ProcessOutput ->' "$SCOPED_LOG" 2>/dev/null | tail -3)"
  [ -n "$po" ] && printf '%s\n' "$po" | sed 's/^/   | /'
  gi="$(grep -a 'GetOutputStreamInfo:' "$SCOPED_LOG" 2>/dev/null | tail -3)"
  [ -n "$gi" ] && printf '%s\n' "$gi" | sed 's/^/   | /'
  echo "   Frames decoded plus a black screen is a presentation problem (backend"
  echo "   / D3D path), not a decode problem. Repeated 0xC00D6D72 alone is"
  echo "   MF_E_TRANSFORM_NEED_MORE_INPUT and normal; zero frames with any other"
  echo "   HRESULT is a decode failure."
  echo

  echo "UE5 PATCH RESULT (meaningful only when the halves line says electra-vpx on)"
  v="$(grep -a 'VPx version checks:' "$SCOPED_LOG" 2>/dev/null | tail -3)"
  [ -n "$v" ] && printf '%s\n' "$v" | sed 's/^/   | /' || echo "   no \"VPx version checks\" line"
  grep -a 'raised threshold at\|could not write at\|nothing matched -- this build' \
       "$SCOPED_LOG" 2>/dev/null | tail -6 | sed 's/^/   | /'
  echo "   found=0 means the game build moved that code and the fix is inert -- the"
  echo "   crash is untouched. found>0 with patched=0 means VirtualProtect or the"
  echo "   write failed."
  echo

  echo "NODE GUARD, two separate facts"
  ng="$(grep -a 'node guard:' "$SCOPED_LOG" 2>/dev/null | tail -2)"
  [ -n "$ng" ] && printf '%s\n' "$ng" | sed 's/^/   | /' || echo "   | no \"node guard:\" line"
  ref="$(grep -ac 'does not exist -- refused' "$SCOPED_LOG" 2>/dev/null)"
  echo "   a node was actually refused: $([ "${ref:-0}" -gt 0 ] && echo "YES x${ref}" || echo no)"
  echo "   \"armed\" says only that the three CreateDXGIFactory imports were hooked."
  echo "   No refusal line means the game never made the adapter-node walk, so a"
  echo "   freeze is something else entirely."
  echo
fi

if [ "$GAME_FAMILY" = p5s ] && [ -n "$SCOPED_LOG" ]; then
  echo "The facts below are read from: $SCOPE_NOTE"
  echo
  echo "PERSONA 5 STRIKERS BANNER"
  grep -a 'import table:' "$SCOPED_LOG" 2>/dev/null | tail -2 | sed 's/^/   | /'
  grep -a -- '---- write-path hooks' "$SCOPED_LOG" 2>/dev/null | tail -2 | sed 's/^/   | /'
  grep -a -- '---- armed:' "$SCOPED_LOG" 2>/dev/null | tail -2 | sed 's/^/   | /'
  echo "   \"0 of 6\" is the load-bearing failure: loaded and hooking nothing."
  echo "   Shipped defaults are all off, so a stock run reads \"painting the real"
  echo "   frames / D3D manager passed / NV12 relabel off / allowed\". Anything else"
  echo "   means BEAST_REFUSE_D3D_MANAGER, BEAST_FORCE_NV12 or P5S_REAL_FRAMES is"
  echo "   set and the run is not stock."
  echo
  echo "PERSONA 5 STRIKERS CHAIN"
  a="$(grep -ac 'ReadSample: sample' "$SCOPED_LOG" 2>/dev/null)"
  e="$(grep -a 'ReadSample -> ' "$SCOPED_LOG" 2>/dev/null | tail -3)"
  c="$(grep -ac 'carry: frame' "$SCOPED_LOG" 2>/dev/null)"
  fl="$(grep -ac 'fill: frame' "$SCOPED_LOG" 2>/dev/null)"
  echo "   samples arrived  : ${a:-0}"
  [ -n "$e" ] && printf '%s\n' "$e" | sed 's/^/   | /'
  echo "   carry lines      : ${c:-0}     fill lines: ${fl:-0}"
  echo "   Nothing out of ReadSample points at a missing codec (section 5). Samples"
  echo "   arriving with no carry/fill line points at DXMT and the shared handle,"
  echo "   which is why this title cannot use d3dmetal."
  echo
fi

if [ "$GAME_FAMILY" = dwo ] && [ -n "$TARGET_LOG" ]; then
  echo "DYNASTY WARRIORS THREE-LINE SEQUENCE (each a distinct stage)"
  s1="$(grep -ac 'dwo-video-bridge: d3d11' "$TARGET_LOG" 2>/dev/null)"
  s2="$(grep -ac 'D3D12 device reached, bridge armed' "$TARGET_LOG" 2>/dev/null)"
  s3="$(grep -ac 'bridge ready:' "$TARGET_LOG" 2>/dev/null)"
  echo "   1 hooks reported    : $([ "${s1:-0}" -gt 0 ] && echo yes || echo NO)"
  echo "                         \"not imported\" means the game resolves those"
  echo "                         another way and the bridge is deaf."
  echo "   2 D3D12 device      : $([ "${s2:-0}" -gt 0 ] && echo yes || echo NO)"
  echo "   3 bridge ready WxH  : $([ "${s3:-0}" -gt 0 ] && echo yes || echo NO)"
  echo "                         1 and 2 without 3 means the video never reached"
  echo "                         the bridge."
  grep -a 'bridge: .* failed, hr' "$TARGET_LOG" 2>/dev/null | tail -5 | sed 's/^/   | /'
  echo
fi

# ============================== SECTION 3 =====================================
hr
echo "SECTION 3 - INSTALL STATE ON DISK (what --status cannot say)"
hr

if [ "$FOLDER_STATE" != ok ]; then
  echo "no game folder given ($FOLDER_STATE) -- this whole section is skipped."
  echo "Re-run with --game-folder \"<the folder the game is installed in>\"."
  echo
else
  echo "CARRIER DUMP (basenames only, never the containing path)"
  dump_dir=""
  case "$GAME_FAMILY" in
    ue5) dump_dir="$OGG_DIR"; files=(libogg_64.dll libogg_64_real.dll libogg_real.dll libogg_64.dll.orig) ;;
    dwo) dump_dir="$GAME_FOLDER"; files=(libxess.dll libxess_real.dll) ;;
    p5s) dump_dir="$GAME_FOLDER"; files=(amd_ags_x64.dll amd_ags_x64_real.dll) ;;
  esac
  if [ -z "$dump_dir" ]; then
    echo "   no carrier folder found under the folder given"
    [ "$GAME_FAMILY" = ue5 ] && \
      echo "   (no Engine/Binaries/ThirdParty/Ogg/Win64/<VS20xx> above it)"
  else
    for f in "${files[@]}"; do
      p="$dump_dir/$f"
      if [ -f "$p" ]; then
        printf '   %-24s %9s bytes  sha256 %s\n' "$f" "$(size_of "$p")" "$(sha_short "$p")"
        printf '     markers      : %s\n' "$(markers_in "$p")"
        fw="$(forwarder_of "$p")"
        printf '     forwards-to  : %s\n' "${fw:-<none -- not one of our proxies>}"
      else
        printf '   %-24s absent\n' "$f"
      fi
    done
  fi
  echo
  echo "   Naming the marker matters: is_ours() is a plain substring scan that"
  echo "   accepts diagnostic builds. install-runtime-fix.sh matches"
  echo "   ue5-media-fix.log, ue5-runtime-fix.log, ue5-vpx-cpupath.log,"
  echo "   electra-h264-fix.log AND electra-probe.log; install-p5s-bridge.sh"
  echo "   matches p5s-video-bridge.log AND mf-observe.log. A probe DLL left in"
  echo "   place reports \"installed\" and plays nothing."
  echo "   Shipped markers, one each: libogg_64.dll = ue5-media-fix.log,"
  echo "   libxess.dll = dwo-video-bridge.log, amd_ags_x64.dll ="
  echo "   p5s-video-bridge.log, libogg_64_electra.dll = electra-h264-fix.log."
  echo

  echo "FALSE POSITIVE 1 - forwarder vs saved name"
  if [ "$GAME_FAMILY" = ue5 ] && [ -n "$dump_dir" ]; then
    fw="$(forwarder_of "$dump_dir/libogg_64.dll")"
    if [ -n "${fw:-}" ]; then
      if [ -f "$dump_dir/${fw}.dll" ]; then
        echo "   ok: the live proxy forwards to ${fw}.dll and that file is present"
      else
        echo "   MISMATCH: the live proxy forwards to ${fw}.dll, which is NOT there."
        for alt in libogg_64_real.dll libogg_real.dll; do
          [ -f "$dump_dir/$alt" ] && echo "   Saved original actually present as: $alt"
        done
        echo "   build-proxy.sh bakes REAL=\"<stem>_real\" into the export table, so a"
        echo "   current proxy beside only the legacy libogg_real.dll reports"
        echo "   \"installed\" and cannot resolve. (Whether it fails at module load or"
        echo "   at first forwarded call is UNCONFIRMED.) README.md still documents"
        echo "   the legacy libogg_real.dll name and is stale relative to the script."
      fi
    else
      echo "   n/a: no proxy live"
    fi
  else
    echo "   n/a for this title"
  fi
  echo

  echo "FALSE POSITIVE 2 - DWO half-install reported as absent"
  if [ "$GAME_FAMILY" = dwo ]; then
    echo "   libxess.dll      : $(yesno "$GAME_FOLDER/libxess.dll")"
    echo "   libxess_real.dll : $(yesno "$GAME_FOLDER/libxess_real.dll")"
    if [ ! -f "$GAME_FOLDER/libxess.dll" ] && [ -f "$GAME_FOLDER/libxess_real.dll" ]; then
      echo "   HALF-INSTALLED. install-dwo-bridge.sh:62-66 has no half branch, so"
      echo "   this prints \"absent\" rc=0: a game that will not start, reported as"
      echo "   clean. --restore puts the original back."
    fi
  else
    echo "   n/a for this title"
  fi
  echo

  echo "FALSE POSITIVE 3 - P5S carrier-missing reported as absent"
  if [ "$GAME_FAMILY" = p5s ]; then
    echo "   amd_ags_x64.dll      : $(yesno "$GAME_FOLDER/amd_ags_x64.dll")"
    echo "   amd_ags_x64_real.dll : $(yesno "$GAME_FOLDER/amd_ags_x64_real.dll")"
    if [ ! -f "$GAME_FOLDER/amd_ags_x64.dll" ] && [ ! -f "$GAME_FOLDER/amd_ags_x64_real.dll" ]; then
      echo "   NO CARRIER AT ALL. install-p5s-bridge.sh pre-checks only game.exe, so"
      echo "   it prints \"absent\" rc=0 here, where the other two scripts exit 1."
    fi
  else
    echo "   n/a for this title"
  fi
  echo

  echo "EXPORT-FORWARDING GATE (read-only replay of the installer's [2/4] check)"
  orig=""; proxy=""
  case "$GAME_FAMILY" in
    ue5) proxy="$RUNTIME/libogg_64.dll"
         for c in libogg_64_real.dll libogg_real.dll libogg_64.dll; do
           [ -f "$dump_dir/$c" ] && { markers_in "$dump_dir/$c" | grep -q '^none$' \
             && orig="$dump_dir/$c" && break; }
         done ;;
    dwo) proxy="$RUNTIME/libxess.dll"
         for c in libxess_real.dll libxess.dll; do
           [ -f "$GAME_FOLDER/$c" ] && { markers_in "$GAME_FOLDER/$c" | grep -q '^none$' \
             && orig="$GAME_FOLDER/$c" && break; }
         done ;;
    p5s) proxy="$RUNTIME/amd_ags_x64.dll"
         for c in amd_ags_x64_real.dll amd_ags_x64.dll; do
           [ -f "$GAME_FOLDER/$c" ] && { markers_in "$GAME_FOLDER/$c" | grep -q '^none$' \
             && orig="$GAME_FOLDER/$c" && break; }
         done ;;
  esac
  if [ -n "$orig" ] && [ -f "$proxy" ] && [ -f "$RUNTIME/pe.py" ]; then
    echo "   game's carrier exports : $(export_count "$orig")"
    echo "   shipped proxy exports  : $(export_count "$proxy")"
    echo "   reference counts       : libogg 64, libxess 27, amd_ags_x64 38, dxgi 7"
    miss="$(comm -23 <(python3 "$RUNTIME/pe.py" exports "$orig" 2>/dev/null | sort) \
                     <(python3 "$RUNTIME/pe.py" exports "$proxy" 2>/dev/null | sort))"
    if [ -n "$miss" ]; then
      echo "   MISSING from the proxy (a missing-entry-point failure at launch):"
      printf '%s\n' "$miss" | sed 's/^/     /'
    else
      echo "   missing from the proxy : none -- everything this copy needs is forwarded"
    fi
  else
    echo "   not run: no untouched carrier found to compare against"
  fi
  echo

  echo "LIVE PROXY vs SHIPPED PROXY"
  live=""
  case "$GAME_FAMILY" in
    ue5) live="$dump_dir/libogg_64.dll" ;;
    dwo) live="$GAME_FOLDER/libxess.dll" ;;
    p5s) live="$GAME_FOLDER/amd_ags_x64.dll" ;;
  esac
  if [ -n "$live" ] && [ -f "$live" ] && [ -f "$RUNTIME/$GAME_CARRIER" ]; then
    a="$(sha_short "$live")"; b="$(sha_short "$RUNTIME/$GAME_CARRIER")"
    echo "   installed $GAME_CARRIER : $a"
    echo "   tooling   $GAME_CARRIER : $b"
    if [ "$a" = "$b" ]; then echo "   equal -- this is the current release"
    else echo "   DIFFERENT -- an older or hand-built proxy is installed."
         echo "   The marker alone cannot tell these apart, since every release since"
         echo "   the merge carries ue5-media-fix.log."
    fi
  else
    echo "   not comparable (nothing live, or no tooling copy beside this script)"
  fi
  echo

  echo "STALE RE-ENCODE LEFTOVERS (basenames and counts, never a listing)"
  if [ "$GAME_FAMILY" = ue5 ] && [ -n "$GAME_ROOT" ] && [ -n "$GAME_PROJ" ]; then
    content="$GAME_ROOT/$GAME_PROJ/Content"
    [ -d "$content" ] || content=""
  else
    content=""
  fi
  if [ -n "$content" ]; then
    n=0
    while IFS= read -r -d '' j; do
      n=$((n + 1)); echo "   marker: $(basename "$j")"
    done < <(find "$content/Paks" -maxdepth 1 -type f -name '.*.hidden-videos.json' -print0 2>/dev/null)
    [ "$n" -eq 0 ] && echo "   marker: none"
    if [ -d "$content/Movies_VP9_backup" ]; then
      c="$(find "$content/Movies_VP9_backup" -maxdepth 1 -type f -print 2>/dev/null | grep -c .)"
      echo "   Movies_VP9_backup: present, $c entries"
    else
      echo "   Movies_VP9_backup: absent"
    fi
    if { [ "$n" -gt 0 ] && [ ! -d "$content/Movies_VP9_backup" ]; } \
       || { [ "$n" -eq 0 ] && [ -d "$content/Movies_VP9_backup" ]; }; then
      echo "   HALF-FINISHED RE-ENCODE from an older version. It changes what the"
      echo "   cutscenes even are, and no --status will ever mention it."
    fi
  else
    echo "   n/a (no Content folder resolved for this title)"
  fi
  echo
fi

echo "BUILD IDENTITY"
echo "   There is no version string anywhere in the shipped product:"
echo "   build-app.sh hardcodes CFBundleVersion and CFBundleShortVersionString to"
echo "   \"1.0\", MacGameVideoFix.swift has no version constant, and no DLL stamps a"
echo "   version into its log. So: hashes of the copy that actually ran."
echo "   (A one-line build stamp in each worker() would remove this whole problem"
echo "    -- a suggestion to the maintainer, not a fact from the repo.)"
for f in install-runtime-fix.sh install-dwo-bridge.sh install-p5s-bridge.sh \
         stage-codecs.sh pe.py libogg_64.dll libxess.dll amd_ags_x64.dll; do
  p="$RUNTIME/$f"
  if [ -f "$p" ]; then
    printf '   %-24s %9s bytes  sha256 %s\n' "$f" "$(size_of "$p")" "$(sha_short "$p")"
  else
    printf '   %-24s absent from the tooling directory\n' "$f"
  fi
done
if [ -d "$ROOT/.git" ]; then
  echo "   git describe          : $(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo '(no tags)')"
else
  echo "   git describe          : not a checkout"
fi
echo

echo "BUNDLE DRIFT (repo runtime/ vs MacGameVideoFix.app/Contents/Resources)"
APPRES=""
for cand in "$ROOT/app/MacGameVideoFix.app" "/Applications/MacGameVideoFix.app" \
            "$HOME/Applications/MacGameVideoFix.app"; do
  [ -d "$cand/Contents/Resources" ] && { APPRES="$cand/Contents/Resources"; break; }
done
if [ -z "$APPRES" ]; then
  echo "   no MacGameVideoFix.app found -- nothing to compare"
else
  echo "   comparing against: .../$(basename "$(dirname "$(dirname "$APPRES")")")/Contents/Resources"
  drift=0
  for f in install-runtime-fix.sh install-dwo-bridge.sh install-p5s-bridge.sh \
           stage-codecs.sh pe.py libogg_64.dll libxess.dll amd_ags_x64.dll; do
    if [ ! -f "$APPRES/$f" ]; then
      printf '   %-24s not in the bundle\n' "$f"
    elif cmp -s "$RUNTIME/$f" "$APPRES/$f"; then
      printf '   %-24s same\n' "$f"
    else
      printf '   %-24s DIFFERENT\n' "$f"; drift=1
    fi
  done
  [ "$drift" = 1 ] && {
    echo "   The app runs the Resources copies and never runtime/, so a bundle not"
    echo "   rebuilt after the scripts changed is the whole explanation for"
    echo "   \"I updated the repo and nothing changed\"."
  }
fi
echo

echo "NODE GUARD (repo-only; build-app.sh does not copy it into the .app)"
if [ ! -x "$CROSSOVER_DIR/install-node-guard.sh" ]; then
  echo "   crossover/install-node-guard.sh not present beside this script"
elif [ ${#CX_APPS[@]} -eq 0 ]; then
  echo "   no CrossOver bundle to query"
else
  for a in "${CX_APPS[@]}"; do
    err="$(mktemp -t mgvf-ng)"
    out="$(bash "$CROSSOVER_DIR/install-node-guard.sh" "$a" --status 2>"$err")"; rc=$?
    if [ "$rc" = 0 ]; then
      echo "   ${out%% *}  $(basename "$a")"
    else
      echo "   rc=$rc  $(basename "$a")"
      sed 's/^/     /' "$err"
      echo "     rc=1 with \"no Game Porting Toolkit DLLs\" means that engine has no"
      echo "     lib64/apple_gptk/wine/x86_64-windows at all -- not the same as"
      echo "     \"not installed\"."
    fi
    rm -f "$err"
  done
  echo "   It patches dxgi.dll inside the CrossOver bundle, so it affects every"
  echo "   game in every bottle on that engine."
fi
echo

# ============================== SECTION 4 =====================================
hr
echo "SECTION 4 - ENGINES AND BOTTLE PROVENANCE"
hr

echo "EVERY INSTALLED CROSSOVER BUNDLE"
if [ ${#CX_APPS[@]} -eq 0 ]; then
  echo "   none found"
else
  for a in "${CX_APPS[@]}"; do
    n="$(plist_get "$a" CFBundleName)"
    sv="$(plist_get "$a" CFBundleShortVersionString)"
    bv="$(plist_get "$a" CFBundleVersion)"
    echo "   $a"
    printf '     CFBundleName=%s  short=%s  version=%s  -> %s\n' \
      "${n:-?}" "${sv:-?}" "${bv:-?}" "$(cx_line "${n:-}")"
    cx="$a/Contents/SharedSupport/CrossOver"
    for d in lib64/gstreamer-1.0 lib/x86_64/gstreamer-1.0 lib/aarch64/gstreamer-1.0; do
      [ -d "$cx/$d" ] || continue
      printf '     %-26s libgstmatroska=%s  libgstapplemedia=%s\n' "$d" \
        "$(yesno "$cx/$d/libgstmatroska.dylib")" "$(yesno "$cx/$d/libgstapplemedia.dylib")"
    done
    for g in "$cx"/lib/apple_gptk*/ "$cx"/lib64/apple_gptk*/; do
      [ -d "$g" ] || continue
      p="${g}external/D3DMetal.framework/Resources/Info.plist"
      [ -f "$p" ] || continue
      printf '     D3DMetal %-14s short=%s  version=%s\n' "$(basename "${g%/}")" \
        "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$p" 2>/dev/null)" \
        "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$p" 2>/dev/null)"
    done
    if [ -d "$cx/lib/dxmt" ]; then
      subs=""
      while IFS= read -r -d '' s; do subs="${subs}${subs:+ }$(basename "$s")"; done < <(
        find "$cx/lib/dxmt" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
      echo "     dxmt: present  [$subs]"
    else
      echo "     dxmt: absent -- this build cannot honour CX_GRAPHICS_BACKEND=dxmt"
    fi
  done
  echo
  echo "   libgstmatroska is the plugin that actually differs between the lines"
  echo "   (present on the 27.x Preview, absent on 26.3). libgstvpx is NOT a"
  echo "   winevideo verdict: neither shipping build has it -- VP9 arrives via"
  echo "   libgstapplemedia/VideoToolbox -- so a report quoting it would mislead."
  echo "   diagnostics/launch-with.sh:24-40 probes only the first two plugin roots"
  echo "   and is therefore wrong for the 27.x line, which keeps aarch64 plugins"
  echo "   in the third."
  echo "   UNCONFIRMED, and marked so: which apple_gptk generation (4.0b2 vs 3.0)"
  echo "   or which dxmt revision a given run actually selects is not readable"
  echo "   offline -- no version string was found in lib/dxmt/*/d3d11.dll."
fi
echo

echo "PER RELEVANT BOTTLE (only the bottles our own logs were found in)"
if [ ${#RELEVANT[@]} -eq 0 ]; then
  echo "   none -- no log from this toolkit was found in any bottle."
else
  for idx in "${RELEVANT[@]}"; do
    b="$(bottle_dir "$idx")"; conf="$b/cxbottle.conf"
    echo "   bottle #$idx$(settings_caveat "$b")"
    printf '     Version=%s  Timestamp=%s  Preview=%s\n' \
      "$(conf_get "$conf" Version)" "$(conf_get "$conf" Timestamp)" "$(conf_get "$conf" Preview)"
    printf '     WineArch=%s  Template=%s\n' \
      "$(conf_get "$conf" WineArch)" "$(conf_get "$conf" Template)"
    printf '     CX_GRAPHICS_BACKEND=%s\n' \
      "$(conf_get "$conf" CX_GRAPHICS_BACKEND || true)"
    # Stronger than the conf Version in one way: it names the BUNDLE, including
    # builds no longer installed -- the "user is on a build they no longer have"
    # case the installed-app scan cannot produce. Match only; system.reg holds
    # installed-software paths and library locations for the whole prefix.
    if [ -f "$b/system.reg" ]; then
      fonts="$(awk '
        /^\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Fonts\]/ { f=1; ts=$NF; next }
        f && /^\[/ { exit }
        f && match($0, /CrossOver[^"\\]*\.app/) { print ts, substr($0, RSTART, RLENGTH); exit }
      ' "$b/system.reg" 2>/dev/null)"
      if [ -n "$fonts" ]; then
        set -- $fonts
        stamp="$1"; shift
        printf '     system.reg Fonts key: %s   stamped %s\n' "$*" \
          "$(date -r "$stamp" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "$stamp")"
      else
        echo "     system.reg Fonts key: no CrossOver bundle name recorded"
      fi
    else
      echo "     system.reg: absent"
    fi
    [ -f "$conf" ] || echo "     cxbottle.conf MISSING -- a half-created bottle"
  done
  echo
  echo "   cxbottle.conf Version is a build number (e.g. 27.0.0.40921) while a"
  echo "   Preview bundle's CFBundleShortVersionString is a date (e.g. 20260821):"
  echo "   the two are not comparable, and any engine can run any bottle -- which"
  echo "   is exactly why Q4 is asked."
  echo "   The raw Preview value is reported as found. That \"1\" means \"created by"
  echo "   CrossOver Preview\" is consistent with observation but is stated nowhere"
  echo "   in the repo: UNCONFIRMED."
  echo "   UNCONFIRMED: exactly which CrossOver operations rewrite the Fonts key."
  echo "   Observed only that it postdates the bottle's last upgrade and that"
  echo "   merely running the GUI updated it."
fi
echo

echo "BOTTLE COUNT SANITY"
echo "   directories=$BOTTLE_TOTAL_DIRS bottles=${#BOTTLE_DIRS[@]}   (bottles = carrying a cxbottle.conf)"
if [ -d "$BOTTLES" ]; then
  df -k "$BOTTLES" 2>/dev/null | tail -1 | awk '{
    printf "   free space on the bottles volume: %.1f GB available, %s used\n", $4/1048576, $5 }'
else
  echo "   no Bottles directory at all"
fi
echo

# ============================== SECTION 5 =====================================
if [ "$GAME_FAMILY" = p5s ] || [ "$GAME_FAMILY" = dwo ]; then
hr
echo "SECTION 5 - CODEC PLUMBING"
hr

echo "GSTREAMER.FRAMEWORK"
if [ ! -d "$FRAMEWORK" ]; then
  echo "   NOT INSTALLED at /Library/Frameworks/GStreamer.framework"
  echo "   The 1.24 series is what is required; 1.24.14 is the verified build."
else
  # head -1 is required: the framework is a universal binary and otool prints a
  # header line per architecture.
  compat="$(otool -L "$FRAMEWORK/lib/libgstreamer-1.0.0.dylib" 2>/dev/null \
            | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1)"
  if [ -n "${compat:-}" ] && [ "$compat" -gt 0 ] 2>/dev/null; then
    echo "   version          : 1.$((compat / 100)).$((compat % 100))  (compat $compat)"
    [ "$((compat / 100))" = 24 ] || \
      echo "                      OUTSIDE THE 1.24 SERIES. Reported, not refused --"
    [ "$((compat / 100))" = 24 ] || \
      echo "                      so a wrong-series framework shows up as an error"
    [ "$((compat / 100))" = 24 ] || \
      echo "                      nowhere else. 1.24.14 is the verified build."
  else
    echo "   version          : not readable"
  fi
  fw_plugin="$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib"
  echo "   libgstlibav      : $(yesno "$fw_plugin")   archs: $(lipo -archs "$fw_plugin" 2>/dev/null || echo n/a)"
fi
echo

echo "STAGED TREE"
if [ ! -d "$STAGE_ROOT" ]; then
  echo "   nothing staged under ~/Library/Application Support/MacGameVideoFix/"
else
  while IFS= read -r -d '' ad; do
    arch="$(basename "$ad")"
    plug="$ad/gstreamer-1.0/libgstlibav.dylib"
    echo "   arch $arch"
    if [ -f "$plug" ]; then
      echo "     libgstlibav.dylib : $(size_of "$plug") bytes  sha256 $(sha_short "$plug")"
    else
      echo "     libgstlibav.dylib : MISSING -- staging did not finish"
    fi
    cp_n="$(find "$ad" -type f -name '*.dylib' -print 2>/dev/null | grep -c .)"
    ln_n="$(find "$ad" -type l -print 2>/dev/null | grep -c .)"
    echo "     copied dylibs=$cp_n  symlinks=$ln_n"
    echo "     expected shape: the plugin, ~7 copied ffmpeg dylibs and ~7 symlinks"
    dang=0
    while IFS= read -r -d '' l; do
      if [ ! -e "$l" ]; then
        dang=$((dang + 1))
        echo "     DANGLING $(basename "$l") -> $(readlink "$l")"
      fi
    done < <(find "$ad/lib" -type l -print0 2>/dev/null)
    [ "$dang" -eq 0 ] && echo "     dangling symlinks : none"
    # The links are absolute paths into one specific CrossOver bundle, so
    # reading one recovers WHICH engine the staging was built against --
    # recorded nowhere else on disk.
    one="$(find "$ad/lib" -type l -print0 2>/dev/null | { IFS= read -r -d '' l && readlink "$l"; })"
    if [ -n "${one:-}" ]; then
      echo "     staged against    : $(printf '%s' "$one" | sed -E 's#^(.*/[^/]*\.app)/.*#\1#')"
      echo "                         (rename, move or update that CrossOver and every"
      echo "                          link breaks, the plugin silently fails to load,"
      echo "                          and the staged folder still looks present)"
    fi
    if [ -f "$plug" ] && [ -f "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib" ]; then
      a="$(sha_short "$plug")"; b="$(sha_short "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib")"
      if [ "$a" = "$b" ]; then
        echo "     vs the installed framework: identical -- staged from the GStreamer"
        echo "                         that is installed right now"
      else
        echo "     vs the installed framework: DIFFERENT -- the framework was upgraded"
        echo "                         underneath the staging. Nothing records which"
        echo "                         GStreamer the staging came from; this compare is"
        echo "                         the only way to tell."
      fi
    fi
  done < <(find "$STAGE_ROOT" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
fi
echo

echo "GST_PLUGIN_PATH PER RELEVANT BOTTLE"
if [ ${#RELEVANT[@]} -eq 0 ]; then
  echo "   no relevant bottle identified"
else
  for idx in "${RELEVANT[@]}"; do
    b="$(bottle_dir "$idx")"; conf="$b/cxbottle.conf"
    v="$(conf_get "$conf" GST_PLUGIN_PATH)"
    echo "   bottle #$idx$(settings_caveat "$b")"
    if [ -z "$v" ]; then
      echo "     GST_PLUGIN_PATH  : <unset>"
    else
      echo "     GST_PLUGIN_PATH  : \"$v\""
      # Both checks are needed. Codecs.configure() skips any conf whose text
      # already contains the string, so a path left over from a deleted or
      # re-staged directory is never rewritten and never warned about.
      if [ -f "$v/libgstlibav.dylib" ]; then
        echo "     libgstlibav there: yes"
      else
        echo "     libgstlibav there: NO -- set but nothing staged at that exact path."
        echo "                        A silent-failure state that only this pair"
        echo "                        distinguishes from \"correctly staged\"."
      fi
      case "$v" in
        */gstreamer-1.0) echo "     suffix           : ok (/gstreamer-1.0)" ;;
        *) echo "     suffix           : WRONG. The value must end in /gstreamer-1.0."
           echo "                        The support dylibs sit one level out in lib/"
           echo "                        precisely because GST_PLUGIN_PATH names a"
           echo "                        directory GStreamer scans wholesale. A value"
           echo "                        ending at .../gst-codecs/<arch> makes it scan"
           echo "                        a directory holding lib/ and find nothing." ;;
      esac
    fi
    wa="$(conf_get "$conf" WineArch)"
    echo "     WineArch         : ${wa:-<unset>}   staged arch written by the tooling: x86_64"
    if [ -n "$wa" ] && [ "$wa" != win64 ]; then
      echo "                        MISMATCH: stage-codecs.sh defaults to x86_64 and"
      echo "                        the Swift app always passes x86_64, so this bottle"
      echo "                        is pointed at a staging built for the wrong host."
    fi
    if grep -q '^\[EnvironmentVariables\]' "$conf" 2>/dev/null; then
      echo "     [EnvironmentVariables] section: present"
    else
      echo "     [EnvironmentVariables] section: MISSING. Codecs.configure() bails and"
      echo "                        writes nothing, and stageCodecs() counts only a nil"
      echo "                        return as touched -- so this bottle is silently"
      echo "                        excluded from the \"N bottle(s) pointed at it\" count"
      echo "                        the app reports."
    fi
  done
  echo "   docs/winevideo-on-preview.md flags the x86_64 transfer as its own"
  echo "   untested caveat: the approach was verified against the aarch64 core,"
  echo "   and \"should -- but should is not measured\"."
fi
echo

echo "GSTREAMER REGISTRY CACHE"
echo "   (the offline replacement for check-winevideo-use.sh's lsof probe;"
echo "    grep -aoE for fixed names only -- the file is a binary blob containing"
echo "    absolute plugin paths including the username)"
found_reg=no
for arch in x86_64 aarch64; do
  R="$CXSUPPORT/gstreamer-1.0-registry.$arch.bin"
  [ -f "$R" ] || continue
  found_reg=yes
  echo "   $arch   (last written $(mtime_of "$R"))"
  for n in libgstlibav avdec_vc1 avdec_wmv3 avdec_wmav2 avdec_vp9 vtdec_hw \
           vp9parse matroskademux libgstvpx; do
    c="$(grep -aoE "$n" "$R" 2>/dev/null | grep -c .)"
    printf '     %-16s %s\n' "$n" "$([ "${c:-0}" -gt 0 ] && echo yes || echo no)"
  done
done
[ "$found_reg" = no ] && echo "   no registry cache found"
echo "   CAVEAT: the cache is global to CrossOver per host architecture, NOT per"
echo "   bottle, so a decoder listed here proves only that the last bottle of"
echo "   that arch to start had it registered. Pair it with that bottle's own"
echo "   GST_PLUGIN_PATH before concluding anything. It is nonetheless the only"
echo "   proof GST_PLUGIN_PATH was read and the re-homed plugin loaded."
echo "   UNCONFIRMED: whether the cache is rewritten on every bottle start or"
echo "   only when winegstreamer initialises."
echo

if [ "$GAME_FAMILY" = dwo ]; then
  echo ".webm BYTESTREAMHANDLER MAPPING (DYNASTY WARRIORS: ORIGINS only)"
  echo "   The only registry write anywhere in the repo:"
  echo "   HKLM\\Software\\Microsoft\\Windows Media Foundation\\ByteStreamHandlers"
  echo "   \\.webm = {317df618-5e5a-468a-9f15-d827a9a08162}"
  if [ ${#RELEVANT[@]} -eq 0 ]; then
    echo "   no relevant bottle identified"
  else
    for idx in "${RELEVANT[@]}"; do
      b="$(bottle_dir "$idx")"; S="$b/system.reg"
      if [ ! -f "$S" ]; then
        echo "   bottle #$idx: no system.reg"; continue
      fi
      # Match only. Never cat it, never grep it broadly, never dump surrounding
      # lines: it holds installed-software paths, Steam paths and library
      # locations for the whole prefix.
      have="$(grep -ac 'ByteStreamHandlers\\\\\.webm\]' "$S" 2>/dev/null)"
      total="$(grep -ac '^\[.*ByteStreamHandlers\\\\\.' "$S" 2>/dev/null)"
      printf '   bottle #%s: .webm %s   (%s extensions mapped in total)\n' \
        "$idx" "$([ "${have:-0}" -gt 0 ] && echo present || echo ABSENT)" "${total:-0}"
      [ "${have:-0}" -gt 0 ] || \
        echo "              Without it Media Foundation refuses to open the file and"
      [ "${have:-0}" -gt 0 ] || \
        echo "              the bridge never sees a frame. That separates \"the fix is"
      [ "${have:-0}" -gt 0 ] || \
        echo "              broken\" from \"nothing ever handed the fix a video\"."
    done
  fi
  echo
fi
fi

# ---- documentation hazards, for whoever reads this -------------------------
hr
echo "NOTES FOR THE READER"
hr
echo " * README.md:428 names ue5-runtime-fix.log; the shipped app writes"
echo "   ue5-media-fix.log. Both names are collected above."
echo " * README.md around lines 95-112 is textually damaged: the DYNASTY"
echo "   WARRIORS requirement sentence is missing after \"additionally:\", and a"
echo "   Mortal Shell 2 paragraph is duplicated mid-sentence. A user answering"
echo "   Q4 from the README finds nothing there."
echo " * wiki/Home.md and wiki/Games.md say none of these games needs a patched"
echo "   CrossOver, while README.md:100-104 and wiki/Dynasty-Warriors-Origins.md:12"
echo "   say it is \"not optional\" for DWO. Nothing says which supersedes which,"
echo "   so a DWO report cannot be read without Q4's answer."
if [ "$SHOW_NAMES" = yes ]; then
  echo " * Bottle names appear ONLY in the fenced key appended below. Review it"
  echo "   before sending, and delete it if you would rather not post them."
else
  echo " * Bottle names are never printed in this report. Re-run with --names to"
  echo "   append the number-to-name key, and review it before sending."
fi
echo

}  # end report


# ============================== assemble and filter ==========================

report > "$RAW" 2>/dev/null

# MFCreateSourceReaderFromURL lines print the movie's FULL Windows path -- a
# Steam library path such as Z:\Volumes\<drive>\steamapps\common\<Game>\...
# Confirmed in ue5-media-fix.c:1020, electra-h264-fix.c:1010 and
# p5s-video-bridge.c:1097. Reduce to the bare filename BEFORE the /Users pass.
# This filter must never be dropped.
#
# Then ONE final redaction pass over the whole assembled bundle rather than per
# command -- per-command redaction lets things slip through the seams.
sed -E 's#MFCreateSourceReaderFromURL\([^)]*[\\/]([^\\/)]+)\)#MFCreateSourceReaderFromURL(<path>/\1)#g' "$RAW" \
  | sed -E 's#/Users/[^/ ]+#/Users/USER#g' > "$OUTFILE"

# The ONLY place in the whole bundle that emits user-chosen bottle names, and
# only when explicitly asked for. Never inlined into the body, never included
# by default in anything auto-attached.
if [ "$SHOW_NAMES" = yes ]; then
  {
    echo
    echo "--- REVIEW BEFORE SENDING: bottle number -> name ---"
    echo "These are your own bottle names. They are often game titles. Delete this"
    echo "block if you would rather not post them."
    for i in "${!BOTTLE_DIRS[@]}"; do
      printf '  bottle #%-3s %s\n' "$((i + 1))" "$(basename "${BOTTLE_DIRS[$i]}")"
    done
    echo "--- end ---"
  } | sed -E 's#/Users/[^/ ]+#/Users/USER#g' >> "$OUTFILE"
fi

cat "$OUTFILE"
echo
echo "saved to: $(printf '%s' "$OUTFILE" | sed -E 's#/Users/[^/ ]+#/Users/USER#g')"
echo "(the real path on your machine is $OUTFILE)"
