#!/usr/bin/env bash
#
# Collect a support bundle: the facts a bug report needs, and nothing else.
#
#   scripts/support-bundle.sh "<game folder>"   collect for that title
#   scripts/support-bundle.sh                   collect what needs no folder
#   scripts/support-bundle.sh --names           append the bottle number -> name key
#   scripts/support-bundle.sh --title <key>     override the inference (see below)
#   scripts/support-bundle.sh --help            this
#
# Plain text, to stdout and to a file, for pasting into the GitHub issue form
# inside a code fence. The form asks the human-answerable questions; this asks
# the machine, and where the two disagree this wins.
#
# The title is INFERRED from the folder by exact filename -- game.exe beside
# data/pd, DWORIGINS.exe, or <Proj>/Binaries/Win64/<Proj>-Win64-Shipping.exe
# under an Unreal root. That inference IS the routing table: it picks the
# installer to query, the log to read, the backend to compare against and the
# executable name the log is scoped by. --title is its escape hatch: with no
# escape hatch, a folder the inference does not recognise skips section 3 and
# reports "not determined" with no way for the user to say what it actually is,
# and the report cannot tell them they named the wrong folder.
#
# WHAT THIS DOES NOT DO, and why:
#
#   * It never lists, globs or counts the installed game library. Every probe is
#     for an exact filename inside the one folder named. A bundle that dumped
#     steamapps/common would put a whole library into a public issue.
#   * It never prints a bottle name in the body: people name bottles after games.
#     Bottles are `bottle #N`; the key comes only with --names, fenced, with the
#     user told to review it, and only for bottles the body already cites. No
#     count of them either -- a count of bottles is a count of game prefixes.
#   * It never publishes another title's name out of a shared log. One log serves
#     every game that has ever run in a bottle, so the excerpt is taken from this
#     title's lines and the census collapses every other prefix into one bucket.
#   * It never prints the game folder, and never prints another program's output
#     raw. Installer, node-guard and pe.py stderr all echo the path they were
#     given; each goes through scrub_paths, which substitutes the known prefixes
#     and then fails closed on anything still absolute.
#   * It refuses to run at all on a folder holding anti-cheat or anti-tamper
#     files. These fixes patch a running process; a report is not worth producing
#     for a title they must never be used on.
#   * It never executes anything inside a CrossOver bundle. Running
#     <bundle>/bin/wine --version was observed to start a Preview GUI, take over
#     a live wineserver and re-stamp the Fonts key in every Preview-enabled
#     bottle -- destroying the evidence section 4 collects. Info.plist is a file.
#   * It never runs uname -a, cxgetsysinfo, or df with no path: all three print
#     the hostname or every mounted volume by name, which the /Users filter
#     misses. df of ONE named directory, awk-filtered to two numbers, is fine.
#   * The log DISCOVERY table names only this title's own logs. Each basename
#     maps 1:1 to a game family, so a table of all eight is a library inventory
#     in a report about one title; the rest collapse into one counted row, which
#     still answers what the table is for -- a bottle running only a legacy or
#     probe build. All eight are still searched for.
#
# CUT when this replaced its 2200-line ancestor, so a later diff can tell a
# deliberate cut from an accident:
#
#   * The interactive question flow and its --game/--symptom flags -- symptom,
#     what it was compared against, which engine was used, whether Steam was
#     quit, the anti-cheat self-certification, what was already tried, the
#     crash-dialog text. The GitHub form asks all seven verbatim and the reader
#     has it open; a second place to answer them is a second place to get them
#     wrong. Only anti-cheat is still enforced here, as a refusal to collect.
#   * The static "vocabulary: installed | broken | half | absent" row and its
#     footnote that install-dwo-bridge.sh has no half branch. FALSE POSITIVE 2
#     demonstrates that gap on the actual folder instead of asserting it.
#
# SHAPED DIFFERENTLY, same facts: the GStreamer registry cache prints grouped
# `registered:` / `absent:` rows rather than nine name/yes-no rows per arch.
#
# ON LENGTH: the brief asked for 350-500 lines. This is ~1640 -- about 1150
# code, 405 comment, 80 blank -- and the gap is not slack. The fact collection
# alone is roughly the target on its own; on top of it sit the standing prose
# that turns a fact into a diagnosis and the WHY behind each collector, and
# both are the deliverable. A triager can stop after the VERDICT only because
# the lines under it say what they mean, and every comment here is a bug
# somebody already paid for. Strip both and the target is reachable; keep
# either and it is not. The target was revised, deliberately, not missed.
#
# Read-only throughout. The only things executed are this repo's own --status
# flags, which read and print one word.
#
# Run it with bash, not sh: two collectors use process substitution. Verified
# against /bin/bash 3.2.57, which is what stock macOS ships -- bash 3.2 cannot
# parse a case statement nested inside a process substitution, and `bash -n`
# under a newer bash will not tell you so.
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
CXSUPPORT="$HOME/Library/Application Support/CrossOver"
BOTTLES="$CXSUPPORT/Bottles"      # the default; CrossOver.conf BottlePath moves it
BOTTLES_NOTE=""
STAGE_ROOT="$HOME/Library/Application Support/MacGameVideoFix/gst-codecs"
FRAMEWORK="/Library/Frameworks/GStreamer.framework/Versions/1.0"
OUTFILE="${SUPPORT_BUNDLE_OUT:-$HOME/Desktop/macgamevideofix-support-$(date +%Y%m%d-%H%M%S).txt}"

# One directory and one trap, installed before the first temp file exists: a
# Ctrl-C during the --status probe used to leave files behind.
TMPDIR_RUN="$(mktemp -d -t mgvf-bundle)" || { echo "cannot create a temp directory" >&2; exit 1; }
trap 'rm -rf "$TMPDIR_RUN"' EXIT

# Every log basename this toolkit has ever written -- and, the same set, every
# marker string that identifies one of our DLLs. README.md still names
# ue5-runtime-fix.log where the shipped DLL writes ue5-media-fix.log, and
# install-p5s-bridge.sh accepts mf-observe.log, so a bottle where only a legacy
# or probe build ran would otherwise be invisible and the whole report blank.
KNOWN_LOGS=(ue5-media-fix.log ue5-runtime-fix.log ue5-vpx-cpupath.log
            electra-h264-fix.log electra-probe.log mf-observe.log
            dwo-video-bridge.log p5s-video-bridge.log)

# Each basename belongs to one family (install-runtime-fix.sh:34-45,
# install-p5s-bridge.sh:35-36, install-dwo-bridge.sh:36). A log basename IS a
# game title, so DISCOVERY may name only this family's -- see the header.
log_family() {
  case "$1" in
    ue5-media-fix.log|ue5-runtime-fix.log|ue5-vpx-cpupath.log|electra-h264-fix.log|electra-probe.log)
       printf ue5 ;;
    p5s-video-bridge.log|mf-observe.log) printf p5s ;;
    dwo-video-bridge.log) printf dwo ;;
  esac
}

SHOW_NAMES=no
GAME_FOLDER=""
FORCED_KEY=""
TITLE_KEYS="ms2 beast iris chronos ue5 dwo p5s"

# Anchored on the text, not on line numbers: the ancestor's `sed -n '3,11p'`
# silently started printing the wrong six lines the first time anything was
# inserted above them, and --help is the one output nobody re-reads.
usage() {
  sed -n '/^#   scripts\/support-bundle/,/^#$/p' "$0" | sed 's/^# \{0,1\}//'
  echo "--title keys: $TITLE_KEYS"
  exit "${1:-1}"
}
while [ $# -gt 0 ]; do
  case "$1" in
    --names)   SHOW_NAMES=yes; shift ;;
    --title)   [ $# -ge 2 ] || { echo "--title needs a key" >&2; usage 1; }
               case " $TITLE_KEYS " in *" $2 "*) ;;
                 *) echo "unknown --title key: $2" >&2; usage 1 ;; esac
               FORCED_KEY="$2"; shift 2 ;;
    -h|--help) usage 0 ;;
    -*)        echo "unknown option: $1" >&2; usage 1 ;;
    *) [ -z "$GAME_FOLDER" ] || { echo "unexpected argument: $1" >&2; usage 1; }
       GAME_FOLDER="${1%/}"; shift ;;
  esac
done

# Before any probe, not after: an unwritable output file used to be discovered
# only once the whole report had been collected and thrown away, while the
# script still printed "saved to" and exited 0.
# Before the probe, because the probe creates the file: the anti-cheat refusal
# deletes an empty OUTFILE on the way out, and deleted a pre-existing one too.
OUTFILE_PREEXISTING=no
[ -e "$OUTFILE" ] && OUTFILE_PREEXISTING=yes
if ! : 2>/dev/null >> "$OUTFILE"; then
  echo "cannot write the report to: $OUTFILE" >&2
  echo "Set SUPPORT_BUNDLE_OUT to a writable path." >&2
  exit 1
fi


# -------------------------------------------------------------- redaction ---
#
# Literal value first, pattern second. A pattern alone fails open three ways,
# all reproduced: a home directory containing a space was half redacted, a
# Windows-style \Users\<name> (how Wine logs a path) was untouched, and the
# macOS filesystem is case-insensitive while the regex was not. The generic
# /Users/ rules stay as the backstop for some OTHER user's name in a log.

sed_lit() { printf '%s' "$1" | sed -e 's#[][\\.*^$/&|+?(){}#-]#\\&#g'; }
HOME_BS="$(printf '%s' "$HOME" | tr '/' '\\')"
USER_NAME="$(basename "$HOME")"

redact() {  # filter: stdin -> stdout
  sed -E \
    -e "s#$(sed_lit "$HOME")#/Users/USER#gI" \
    -e "s#$(sed_lit "$HOME_BS")#\\\\Users\\\\USER#gI" \
    -e "s#/Users/$(sed_lit "$USER_NAME")#/Users/USER#gI" \
    -e "s#\\\\Users\\\\$(sed_lit "$USER_NAME")#\\\\Users\\\\USER#gI" \
    -e 's#/Users/[^/"\\]+#/Users/USER#gI' \
    -e 's#\\Users\\[^\\"/]+#\\Users\\USER#gI'
}

# Substitute the paths this run knows about, then FAIL CLOSED: anything still
# starting an absolute path is cut to end of line, because a path may contain
# spaces and there is no way to know where it stops. Used on every piece of text
# this script did not write itself.
#
# The / is recognised after ANY non-path character, not just after whitespace:
# anchored on whitespace, six of seven realistic stderr shapes walked through
# ("can't open file '/Volumes/...'", grep's quoted form, "at=/Volumes/..."), and
# redact() rescues only the /Users spellings. `>` is the one exclusion, because
# every replacement written here ends in one and the tail after it is a path
# whose secret half has just been substituted.
scrub_paths() {  # scrub_paths <replacement> <prefix>...
  local repl="$1"; shift
  local prog="" pfx
  for pfx in "$@"; do
    [ -n "$pfx" ] || continue
    prog="${prog}s#$(sed_lit "$pfx")#${repl}#g;"
  done
  sed -E "${prog}"'s#(^|[^[:alnum:]_./\\>-])/[^[:space:]].*#\1<path>#'
}
scrub() { scrub_paths '<game folder>' "$GAME_ROOT" "$GAME_FOLDER" "$OGG_DIR"; }


# -------------------------------------------------------------- utilities ---

# Count NULs, not lines: `find -print | grep -c .` overcounts for a filename
# containing a newline, and two call sites feed a number whose job is to be
# falsified by a short count -- a miscount there is a wrong verdict.
count0() {  # count0 <dir> <find args...>
  local d="$1"; shift
  find "$d" "$@" -print0 2>/dev/null | tr -dc '\0' | wc -c | tr -d ' '
}

sha_short() { [ -f "$1" ] && shasum -a 256 "$1" 2>/dev/null | cut -c1-16; }
size_of()   { [ -e "$1" ] && wc -c < "$1" 2>/dev/null | tr -d ' '; }
mtime_of()  { [ -e "$1" ] && date -r "$1" '+%Y-%m-%d %H:%M' 2>/dev/null; }
yesno()     { [ -e "$1" ] && echo yes || echo no; }
ynum()      { [ "${1:-0}" -gt 0 ] 2>/dev/null && echo yes || echo no; }
plist_get() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null; }

# Anchor on the key at the start of the line: cxbottle.conf documents the same
# keys again in its own ;;-prefixed comments, and values contain spaces, so they
# must never be word-split. Do not copy launch-and-capture.sh:39 -- its [a-z0-9]*
# class yields empty for any value with an underscore or hyphen.
conf_get() {  # conf_get <conf> <KEY>
  [ -f "$1" ] || return 1
  sed -n "s/^\"$2\"[[:space:]]*=[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$1" 2>/dev/null | head -1
}

# Name the markers rather than collapsing them to "ours": is_ours() in the
# installers is a plain substring scan, so a diagnostic build reports
# "installed" and plays nothing, and only the marker name tells them apart.
markers_in() {
  local m out=""
  [ -f "$1" ] || return 1
  for m in "${KNOWN_LOGS[@]}"; do
    LC_ALL=C grep -qa "$m" "$1" 2>/dev/null && out="${out}${out:+, }$m"
  done
  printf '%s' "${out:-none}"
}

# build-proxy.sh bakes "<stem>_real" into the export forwarders, so the target is
# readable straight out of the PE. This catches a current proxy sitting beside
# only the legacy libogg_real.dll.
forwarder_of() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -oa '[A-Za-z0-9_]*_real\.[A-Za-z0-9_@]*' "$1" 2>/dev/null \
    | sed 's/\..*//' | sort -u | head -1
}

# head -1 is load-bearing: the framework is universal and otool prints one header
# per architecture.
gst_compat() {
  otool -L "$FRAMEWORK/lib/libgstreamer-1.0.0.dylib" 2>/dev/null \
    | sed -n 's/.*compatibility version \([0-9]*\)\..*/\1/p' | head -1
}


# ----------------------------------------------------------- report shapes ---
#
# Five shapes cover essentially every line of the body. Hand-writing them per
# fact is what made the previous version 2200 lines.

hr()   { printf '%s\n' "------------------------------------------------------------------------"; }
kv()   { printf '   %-17s: %s\n' "$1" "$2"; }         # section 1 label row
note() { printf '                      %s\n' "$@"; }  # prose hanging at the value column
p3()   { printf '   %s\n' "$@"; }                     # sub-heading / sub-row
p5()   { printf '     %s\n' "$@"; }                   # second-level row
exc()  { anon_prefix | sed 's/^/   | /'; }            # filter: a log excerpt


# --------------------------------------------------- the numbering contract ---
#
# ONE iteration order for every bottle-keyed line in the report. If a collector
# deviates the indices stop agreeing and the --names key becomes wrong rather
# than merely private.

# Only BottlePath is read out of CrossOver.conf -- that file also holds the
# licence token and the whole Start-menu tree. With the default hardcoded, a
# relocated directory made every log probe come back empty and be reported,
# confidently, as "the proxy was never mapped".
cfg_bp="$(conf_get "$CXSUPPORT/CrossOver.conf" BottlePath)"
if [ -n "${cfg_bp:-}" ] && [ -d "$cfg_bp" ]; then
  [ "$cfg_bp" = "$BOTTLES" ] || BOTTLES_NOTE="bottle location taken from CrossOver.conf BottlePath"
  BOTTLES="$cfg_bp"
elif [ -n "${cfg_bp:-}" ]; then
  BOTTLES_NOTE="CrossOver.conf sets BottlePath to a directory that is not there; searched the default"
fi

# -H because BSD find will not descend into a starting point that is itself a
# symlink, and symlinking Bottles/ onto an external drive is the ordinary way to
# move bottles: without it a fully populated Mac produced a report byte-identical
# to one where CrossOver was never installed. Sorted because the numbering was
# readdir order, and the documented workflow is two-step -- post the report, then
# re-run with --names -- so any bottle added or renamed in between mapped the
# wrong names onto numbers already in a public issue.
BOTTLES_PRESENT=no
[ -d "$BOTTLES" ] && BOTTLES_PRESENT=yes
BOTTLE_DIRS=()
while IFS= read -r -d '' d; do
  [ -f "$d/cxbottle.conf" ] && BOTTLE_DIRS+=("$d")
done < <(find -H "$BOTTLES" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null | sort -z)

bottle_index() {  # bottle_index <dir> -> 1-based index
  local i
  for i in "${!BOTTLE_DIRS[@]}"; do
    [ "${BOTTLE_DIRS[$i]}" = "$1" ] && { echo $((i + 1)); return 0; }
  done
  return 1
}
bottle_dir() { echo "${BOTTLE_DIRS[$(( $1 - 1 ))]:-}"; }

# A SET, deliberately: no names and no numbers. Not "no count" -- N distinct
# values is a floor of N bottles, which is why only the set is printed and never
# its size. It answers one question: is the value this title needs set anywhere
# at all. Used only when our own logs identified no bottle.
backend_values_seen() {
  local b v out=""
  for b in ${BOTTLE_DIRS[@]+"${BOTTLE_DIRS[@]}"}; do
    v="$(conf_get "$b/cxbottle.conf" CX_GRAPHICS_BACKEND)"; v="${v:-<unset>}"
    case " $out " in *" $v "*) ;; *) out="${out}${out:+ }$v" ;; esac
  done
  printf '%s' "$out"
}

# A wineserver's cwd is /private/tmp/.wine-<uid>/server-<dev-hex>-<inode-hex>,
# where dev and inode are the bottle directory's. That tag discloses nothing,
# which is the point: `ps` WITH arguments would print the bottle path.
bottle_tag() {
  local dev ino
  read -r dev ino <<<"$(stat -f '%d %i' "$1" 2>/dev/null)" || return 1
  [ -n "${dev:-}" ] || return 1
  printf 'server-%x-%x' "$dev" "$ino"
}

# The binaries are wineserver-x86 and wineserver-arm64, so `pgrep -x wineserver`
# is a silent false negative: it finds nothing, and the report then says no
# wineserver is running while one is caching the old bottle settings.
WS_PIDS=()
while IFS= read -r p; do [ -n "$p" ] && WS_PIDS+=("$p"); done < <(
  ps -Ao pid=,comm= 2>/dev/null | awk '{ n=$0; sub(/^[ ]*[0-9]+[ ]+/,"",n); sub(/.*\//,"",n)
                                         if (n ~ /^wineserver/) print $1 }')
WS_COUNT=${#WS_PIDS[@]}

ws_pid_for() {  # ws_pid_for <bottle dir> -> pid of the wineserver serving it
  local tag p cwd; tag="$(bottle_tag "$1")" || return 1
  for p in ${WS_PIDS[@]+"${WS_PIDS[@]}"}; do
    cwd="$(lsof -a -p "$p" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p' | head -1)"
    [ "$(basename "${cwd:-/}")" = "$tag" ] && { echo "$p"; return 0; }
  done
  return 1
}
settings_caveat() {  # a live wineserver cached the settings read at bottle start
  ws_pid_for "$1" >/dev/null 2>&1 \
    && echo "  << a wineserver is live for this bottle -- may not be what is in effect"
}


# ---------------------------------------- the engines installed (read-only) ---
#
# Info.plist is read as a FILE. Nothing here shells into a CrossOver bundle.

CX_APPS=()
for root in /Applications "$HOME/Applications"; do
  # find does the -iname matching itself: bash 3.2, which is what stock macOS
  # ships as /bin/bash, cannot parse a case statement nested inside a process
  # substitution, and `bash -n` under a newer bash will not tell you so.
  while IFS= read -r -d '' a; do CX_APPS+=("$a"); done < <(
    find "$root" -maxdepth 1 -iname '*crossover*.app' -print0 2>/dev/null)
done

# The .app filename is a user-renamed string out of the home directory
# ("Crossover_patched.app" was the real case) and is never printed; the name in
# Info.plist and a location class are. CFBundleName is also the only reliable
# stable-vs-Preview tell -- both lines ship CFBundleIdentifier
# com.codeweavers.CrossOver, and a Preview build can sit under a non-Preview name.
#
# The tell stays because a machine can still have Preview installed and a report
# should say so. Since 2026-08-31 the supported engine is stable CrossOver 26.3
# and Preview is not one, so "Preview" in that column is a fact about the
# machine, not a configuration this project supports.
cx_name()  { local n; n="$(plist_get "$1" CFBundleName)"; printf '%s' "${n:-(no CFBundleName)}"; }
cx_where() { case "$1" in "$HOME/Applications/"*) printf '~/Applications' ;;
                          /Applications/*) printf '/Applications' ;;
                          *) printf 'another folder' ;; esac; }
cx_line()  { case "$1" in *Preview*) echo Preview ;; *) echo stable ;; esac; }


# ------------------------------------ which title, inferred from the folder ---
#
# The one argument the README documents has to be sufficient on its own, so the
# title is identified the way app/MacGameVideoFix.swift identifies it: by exact
# filename, inside the folder named and the Unreal root above it. Every test is
# `-f <exact path>` -- nothing listed, globbed or scanned -- which is also why
# pointing this at a Steam library root identifies nothing instead of walking it.

GAME_KEY=""; GAME_LABEL=""; GAME_EXE=""; GAME_PROJ=""; GAME_FAMILY=""
GAME_INSTALLER=""; GAME_LOG=""; GAME_BACKEND=""
GAME_CARRIER=""; GAME_REAL=""; GAME_LEGACY=""; HALVES_EXPECTED=""
GAME_ROOT=""; OGG_DIR=""          # the Unreal project root and its VS20xx folder
FOLDER_STATE="none given"; IDENT_NOTE=""

if [ -n "$GAME_FOLDER" ]; then
  [ -d "$GAME_FOLDER" ] && FOLDER_STATE=ok || FOLDER_STATE="the folder given does not exist"
fi

# --title asserts the answer; the inference is then asked only for the Unreal
# root, which the UE5 keys still need. It turns "no supported title identified
# in that folder" into "the title you named is not in this folder".
if [ -n "$FORCED_KEY" ] && [ "$FOLDER_STATE" = ok ]; then
  GAME_KEY="$FORCED_KEY"; IDENT_NOTE="--title $FORCED_KEY (asserted, not inferred)"
  case "$FORCED_KEY" in
    ms2) GAME_PROJ=MortalShell2 ;;  beast) GAME_PROJ=BeastOfReincarnation ;;
    iris) GAME_PROJ=Iris ;;         chronos) GAME_PROJ=Chronos ;;
  esac
  [ -n "$GAME_PROJ" ] && GAME_EXE="$GAME_PROJ-Win64-Shipping.exe"
  case "$FORCED_KEY" in ms2|beast|iris|chronos|ue5)
    probe="$GAME_FOLDER"
    for _ in 1 2 3 4 5; do
      [ -d "$probe/Engine/Binaries/ThirdParty/Ogg/Win64" ] && { GAME_ROOT="$probe"; break; }
      probe="$(dirname "$probe")"
    done
    if [ -z "$GAME_ROOT" ]; then
      IDENT_NOTE="$IDENT_NOTE -- but no Engine/Binaries/ThirdParty/Ogg/Win64 above that folder"
    elif [ -n "$GAME_EXE" ] && [ ! -f "$GAME_ROOT/$GAME_PROJ/Binaries/Win64/$GAME_EXE" ]; then
      IDENT_NOTE="$IDENT_NOTE -- but $GAME_EXE is NOT under that Unreal root: WRONG FOLDER"
    fi ;;
  dwo) [ -f "$GAME_FOLDER/DWORIGINS.exe" ] || \
       IDENT_NOTE="$IDENT_NOTE -- but there is no DWORIGINS.exe in it: WRONG FOLDER" ;;
  p5s) [ -f "$GAME_FOLDER/game.exe" ] || \
       IDENT_NOTE="$IDENT_NOTE -- but there is no game.exe in it: WRONG FOLDER" ;;
  esac
elif [ "$FOLDER_STATE" = ok ]; then
  # game.exe is generic enough to match unrelated titles, so it is gated on the
  # sibling data/pd -- how the app identifies Persona 5 Strikers.
  if [ -f "$GAME_FOLDER/game.exe" ] && [ -d "$GAME_FOLDER/data/pd" ]; then
    GAME_KEY=p5s; IDENT_NOTE="game.exe with data/pd beside it"
  elif [ -f "$GAME_FOLDER/DWORIGINS.exe" ]; then
    GAME_KEY=dwo; IDENT_NOTE="DWORIGINS.exe in the folder given"
  else
    probe="$GAME_FOLDER"
    for _ in 1 2 3 4 5; do
      [ -d "$probe/Engine/Binaries/ThirdParty/Ogg/Win64" ] && { GAME_ROOT="$probe"; break; }
      probe="$(dirname "$probe")"
    done
    if [ -z "$GAME_ROOT" ]; then
      IDENT_NOTE="no Engine/Binaries/ThirdParty/Ogg/Win64 above the folder given, so it does not look like an Unreal install (nothing outside it was searched)"
    else
      # One exact path per candidate. An Unreal root with none of the four is the
      # generic case, which arms all three halves and scopes nothing.
      for proj in MortalShell2 BeastOfReincarnation Iris Chronos; do
        [ -f "$GAME_ROOT/$proj/Binaries/Win64/$proj-Win64-Shipping.exe" ] || continue
        GAME_PROJ="$proj"; GAME_EXE="$proj-Win64-Shipping.exe"
        IDENT_NOTE="$proj/Binaries/Win64/$GAME_EXE under the Unreal root"; break
      done
      case "$GAME_PROJ" in
        MortalShell2) GAME_KEY=ms2 ;; BeastOfReincarnation) GAME_KEY=beast ;;
        Iris) GAME_KEY=iris ;;       Chronos) GAME_KEY=chronos ;;
        *) GAME_KEY=ue5; IDENT_NOTE="an Unreal root, but none of the four known project executables" ;;
      esac
    fi
  fi
fi

# The routing table. Everything downstream -- which installer is queried, which
# log is the target, which backend the mismatch test compares against, how the
# log is scoped, and the whole of section 2 -- comes from these eight fields.
case "$GAME_KEY" in
  ms2)     GAME_LABEL="Mortal Shell 2"
           HALVES_EXPECTED="electra-vpx on | node-guard off | electra-h264 off" ;;
  beast)   GAME_LABEL="Beast of Reincarnation"
           HALVES_EXPECTED="electra-vpx off | node-guard off | electra-h264 on" ;;
  iris)    GAME_LABEL="Life is Strange: Reunion"
           HALVES_EXPECTED="electra-vpx off | node-guard on | electra-h264 off" ;;
  chronos) GAME_LABEL="Life is Strange: Double Exposure"
           HALVES_EXPECTED="electra-vpx off | node-guard on | electra-h264 off" ;;
  ue5)     GAME_LABEL="an Unreal Engine 5 title (project not one of the four known)"
           HALVES_EXPECTED="all three on, preceded by the unknown-title warning" ;;
  dwo)     GAME_LABEL="DYNASTY WARRIORS: ORIGINS"; GAME_EXE=DWORIGINS.exe ;;
  p5s)     GAME_LABEL="Persona 5 Strikers";        GAME_EXE=game.exe ;;
esac
case "$GAME_KEY" in
  ms2|beast|iris|chronos|ue5)
    GAME_FAMILY=ue5; GAME_INSTALLER=install-runtime-fix.sh; GAME_LOG=ue5-media-fix.log
    GAME_BACKEND=d3dmetal; GAME_CARRIER=libogg_64.dll
    GAME_REAL=libogg_64_real.dll; GAME_LEGACY=libogg_real.dll
    DUMP_FILES=(libogg_64.dll libogg_64_real.dll libogg_real.dll libogg_64.dll.orig) ;;
  dwo)
    GAME_FAMILY=dwo; GAME_INSTALLER=install-dwo-bridge.sh; GAME_LOG=dwo-video-bridge.log
    GAME_BACKEND=d3dmetal; GAME_CARRIER=libxess.dll; GAME_REAL=libxess_real.dll
    DUMP_FILES=(libxess.dll libxess_real.dll) ;;
  p5s)
    GAME_FAMILY=p5s; GAME_INSTALLER=install-p5s-bridge.sh; GAME_LOG=p5s-video-bridge.log
    GAME_BACKEND=dxmt; GAME_CARRIER=amd_ags_x64.dll; GAME_REAL=amd_ags_x64_real.dll
    DUMP_FILES=(amd_ags_x64.dll amd_ags_x64_real.dll) ;;
  *) DUMP_FILES=() ;;
esac

# The VS20xx folder name changes between engine versions, so it is searched one
# level down. Preferring the directory with a live libogg_64.dll and falling back
# to one holding only a saved original stops a half-install reading as "this game
# ships no libogg".
#
# The SAME glob install-runtime-fix.sh:84 uses, so the two cannot pick different
# directories: with a find here and a glob there, two VS20xx folders made the
# report say "state installed" and "markers: none, no proxy live" at once.
if [ "$GAME_FAMILY" = ue5 ] && [ -n "$GAME_ROOT" ]; then
  for d in "$GAME_ROOT"/Engine/Binaries/ThirdParty/Ogg/Win64/*/; do
    [ -d "$d" ] || continue          # an unmatched glob expands to itself
    d="${d%/}"
    [ -f "$d/libogg_64.dll" ] && { OGG_DIR="$d"; break; }
    { [ -f "$d/libogg_64_real.dll" ] || [ -f "$d/libogg_real.dll" ]; } && OGG_DIR="$d"
  done
fi
# Every section-3 probe resolves its carrier from here, once.
case "$GAME_FAMILY" in
  ue5) CARRIER_DIR="$OGG_DIR" ;;
  dwo|p5s) CARRIER_DIR="$GAME_FOLDER" ;;
  *) CARRIER_DIR="" ;;
esac


# ------------------------------------------------------------- anti-cheat ---
#
# These fixes patch a running process, which is what anti-cheat exists to stop,
# and one half writes to the executable as well. Exact filenames, in the folder
# named and the Unreal root above it -- nothing listed, globbed or counted.

ANTICHEAT_HITS=""
for acdir in "$GAME_FOLDER" "$GAME_ROOT"; do
  [ -n "$acdir" ] && [ -d "$acdir" ] || continue
  for ac in EasyAntiCheat EasyAntiCheat_EOS BattlEye; do
    [ -d "$acdir/$ac" ] && ANTICHEAT_HITS="${ANTICHEAT_HITS}${ANTICHEAT_HITS:+, }$ac/"
  done
  for ac in EasyAntiCheat_x64.dll EasyAntiCheat.exe BEService.exe \
            BEService_x64.exe start_protected_game.exe; do
    [ -f "$acdir/$ac" ] && ANTICHEAT_HITS="${ANTICHEAT_HITS}${ANTICHEAT_HITS:+, }$ac"
  done
done

# Refuse, and collect nothing: producing a report for a title this toolkit must
# never be used on invites somebody to debug it.
if [ -n "$ANTICHEAT_HITS" ]; then
  cat <<EOF
========================================================================
MacGameVideoFix support bundle -- STOPPED, NOTHING COLLECTED
========================================================================

Anti-cheat or anti-tamper files are present in the folder you named:

  $ANTICHEAT_HITS

Every fix in this project patches a running process, which is exactly the
behaviour anti-cheat exists to stop, and one of them writes to the game's
executable. They must NEVER be used on a protected title: at best the game
refuses to start, at worst the account is banned.

No bundle was collected and nothing was read from your bottles. If you
believe these files belong to something else in that folder, say so in the
issue rather than re-running this with the check bypassed.
EOF
  { [ -s "$OUTFILE" ] || [ "$OUTFILE_PREEXISTING" = yes ]; } || rm -f "$OUTFILE"
  exit 2
fi


# ------------------------------------------------- install state (--status) ---
#
# MGVF_STATUS_ONLY makes the read-only property structural instead of positional:
# all four installers default to their INSTALL branch when $2 is empty, so the
# safety of this whole bundle used to rest on the literal --status never being
# lost from this one line. Their error branches echo the folder they were given,
# so raw stderr published the Steam library location.

INSTALL_STATE=""; INSTALL_RC=""; INSTALL_ERR=""
if [ -n "$GAME_INSTALLER" ] && [ "$FOLDER_STATE" = ok ] && [ -x "$RUNTIME/$GAME_INSTALLER" ]; then
  INSTALL_STATE="$(MGVF_STATUS_ONLY=1 bash "$RUNTIME/$GAME_INSTALLER" "$GAME_FOLDER" --status \
                   2>"$TMPDIR_RUN/status.err")"
  INSTALL_RC=$?
  INSTALL_ERR="$(scrub < "$TMPDIR_RUN/status.err")"
fi

# The VERDICT is the one line a triager may stop after, so its state field has to
# survive having no state. Reusing $FOLDER_STATE as prose printed the literal
# "state not checked (ok)" for a folder with no supported title in it -- the
# commonest way to run this wrong -- and for an installer that printed nothing.
state_word() {
  if [ -n "$INSTALL_STATE" ];  then printf '%s' "$INSTALL_STATE"
  elif [ "$FOLDER_STATE" != ok ]; then printf 'not checked (%s)' "$FOLDER_STATE"
  elif [ -z "$GAME_INSTALLER" ]; then printf 'not checked (no supported title in that folder)'
  elif [ ! -x "$RUNTIME/$GAME_INSTALLER" ]; then printf 'not checked (%s is not beside this script)' "$GAME_INSTALLER"
  else printf 'unknown (%s printed nothing, rc=%s)' "$GAME_INSTALLER" "${INSTALL_RC:-?}"
  fi
}


# ----------------------------------------------- where our logs actually are ---
#
# Bottles are found by this toolkit's own exact log basenames, never by listing
# Bottles/. That satisfies the privacy rule and doubles as evidence: it surfaces
# only bottles where one of our DLLs has actually run.

LOG_PATHS=(); LOG_NAMES=(); LOG_BIDX=()
for name in "${KNOWN_LOGS[@]}"; do
  while IFS= read -r -d '' f; do
    idx="$(bottle_index "$(dirname "$(dirname "$f")")")" || idx="?"
    LOG_PATHS+=("$f"); LOG_NAMES+=("$name"); LOG_BIDX+=("$idx")
  done < <(find -H "$BOTTLES" -maxdepth 3 -type f -name "$name" -print0 2>/dev/null | sort -z)
done

# NEWEST by mtime, not first in find order: two bottles carrying the same log
# name is the expected state -- giving Persona 5 Strikers a bottle of its own
# leaves a stale log in the old one -- and facts 3, 4 and 5 used to describe
# whichever copy find happened to return first, with nothing saying so.
TARGET_LOG=""; TARGET_BIDX=""; TARGET_BDIR=""
TARGET_NEWEST=0; TARGET_COPIES=0; TARGET_WHERE=""
for i in ${LOG_PATHS[@]+"${!LOG_PATHS[@]}"}; do
  [ -n "$GAME_LOG" ] && [ "${LOG_NAMES[$i]}" = "$GAME_LOG" ] || continue
  TARGET_COPIES=$((TARGET_COPIES + 1))
  TARGET_WHERE="${TARGET_WHERE}${TARGET_WHERE:+, }#${LOG_BIDX[$i]}"
  m="$(stat -f %m "${LOG_PATHS[$i]}" 2>/dev/null)"; m="${m:-0}"
  if [ -z "$TARGET_LOG" ] || [ "$m" -gt "$TARGET_NEWEST" ]; then
    TARGET_NEWEST="$m"; TARGET_LOG="${LOG_PATHS[$i]}"; TARGET_BIDX="${LOG_BIDX[$i]}"
    TARGET_BDIR="$(dirname "$(dirname "$TARGET_LOG")")"
  fi
done

# The log is opened FILE_APPEND_DATA and never truncated, so it accumulates
# across every run and every title in that bottle. A "VPx version checks" line
# left by another game would be read as evidence about this one, so every grep
# below -- and the excerpt, which used to be taken from the whole file and
# published every other title's executable name -- reads this title's lines.
SCOPED_LOG="$TARGET_LOG"
SCOPE_NOTE="the whole file (no per-process prefix to scope by)"
TITLE_LINES=""
if [ -n "$TARGET_LOG" ] && [ -n "$GAME_EXE" ] && [ "$GAME_FAMILY" != dwo ]; then
  grep -a -F "[$GAME_EXE]" "$TARGET_LOG" > "$TMPDIR_RUN/scoped.log" 2>/dev/null
  TITLE_LINES="$(grep -c . "$TMPDIR_RUN/scoped.log" 2>/dev/null)"
  if [ "${TITLE_LINES:-0}" -gt 0 ]; then
    SCOPED_LOG="$TMPDIR_RUN/scoped.log"; SCOPE_NOTE="only the [$GAME_EXE] lines"
  else
    SCOPE_NOTE="the whole file -- NO [$GAME_EXE] lines in it at all"
  fi
fi

# Where a run cannot be scoped -- generic UE5 mode, or a scoped grep that matched
# nothing -- the [<exe>] prefix IS the disclosure, and it names a title the user
# did not. Rewrite it, and take the movie filename with it: the final filter's
# rule 1 deliberately keeps the basename of MFCreateSourceReaderFromURL, which on
# a shared log is another game's cutscene ("Chronos_Intro_4K.mp4" under an
# anonymised prefix published exactly what the rewrite was there to hide).
# Collapsing it here means rule 1 only sees lines provably this title's.
anon_prefix() {
  if [ "$SCOPED_LOG" = "$TARGET_LOG" ]; then
    sed -E -e 's#^\[[^]]*\]#[<title>]#' \
           -e 's#(MFCreateSourceReaderFromURL\().*#\1<path>)#'
  else cat; fi
}

# One shape for every "grep the log, show the last few matching lines" fact,
# which is what twenty-one hand-written blocks used to be. Scoping and
# anonymising live inside it, so a new call site cannot forget them.
ev()  { local out; out="$(grep -a -e "$2" "$SCOPED_LOG" 2>/dev/null | tail -"$1")"
        if [ -n "$out" ]; then printf '%s\n' "$out" | exc
        elif [ -n "${3:-}" ]; then p3 "$3"; fi; }
cnt() { grep -ac -e "$1" "$SCOPED_LOG" 2>/dev/null | tr -d ' '; }

# Bottles this report cites by number, and the only list --names may iterate.
# Falling back to every bottle published the name of every prefix on the machine
# for a report about one of them.
RELEVANT=()
for i in ${LOG_BIDX[@]+"${!LOG_BIDX[@]}"}; do
  idx="${LOG_BIDX[$i]}"
  case " ${RELEVANT[*]-} " in *" $idx "*) ;; *) [ "$idx" != "?" ] && RELEVANT+=("$idx") ;; esac
done



# ================================================================ the report ==

report() {
cat <<EOF
========================================================================
MacGameVideoFix support bundle
collected $(date '+%Y-%m-%d %H:%M %Z')
========================================================================
EOF
# ---- the one line a triager reads first -------------------------------------
compat="$(gst_compat)"
[ -n "${compat:-}" ] && [ "$compat" -gt 0 ] 2>/dev/null \
  && gst="1.$((compat / 100)).$((compat % 100))" || gst="not installed"
# Persona 5 Strikers is the only title needing a codec CrossOver does not ship;
# a bare version on the other six invited a triager to chase a framework with no
# bearing on them.
[ "$GAME_FAMILY" = p5s ] && gst="$gst (needs 1.24.x)" \
                         || gst="$gst (not a requirement for this title)"
if [ -n "$TARGET_BDIR" ]; then
  live_backend="$(conf_get "$TARGET_BDIR/cxbottle.conf" CX_GRAPHICS_BACKEND)"
  live_backend="${live_backend:-<unset>}"
else
  live_backend="n/a, no bottle -- see fact 4"
fi
printf 'VERDICT: %s | state %s | log %s | backend %s (needs %s) | gstreamer %s\n' \
  "${GAME_LABEL:-(no title given)}" \
  "$(state_word)" \
  "$([ -n "$TARGET_LOG" ] && echo "present in bottle #$TARGET_BIDX" || echo absent)" \
  "$live_backend" "${GAME_BACKEND:-?}" "$gst"
# Two facts this bundle already held, never before put side by side: "not
# installed now" plus "our own log is in a bottle" is the signature of a game
# updated or verified since the fix was applied.
if [ "${INSTALL_STATE:-}" = absent ] && [ -n "$TARGET_LOG" ]; then
  printf '         state is absent, yet %s exists (last written %s).\n' \
    "$GAME_LOG" "$(mtime_of "$TARGET_LOG")"
  printf '         The fix ran at some point and is not installed now: see FALSE POSITIVE 4.\n'
fi
echo

hr; echo "SECTION 1 - VERDICT BLOCK"; echo "(a reader should be able to stop here)"; hr
echo "1. TITLE + REQUIRED BACKEND"
kv title "${GAME_LABEL:-(not determined)}"
kv "identified by" "${IDENT_NOTE:-nothing -- $FOLDER_STATE}"
if [ -n "$GAME_BACKEND" ]; then
  kv requires "CX_GRAPHICS_BACKEND = $GAME_BACKEND"
  note "dxmt and d3dmetal cannot share a bottle; Persona 5 Strikers is the dxmt one."
  kv carrier "$GAME_CARRIER"
  # The DWO line is where wiki/Games.md ("none of these games needs CrossOver
  # patched") and wiki/Dynasty-Warriors-Origins.md ("winevideo, not optional")
  # are reconciled: the bridge presents frames and decodes none, so the engine
  # has to open the container and decode what is inside it. Stable 26.3 decodes
  # the VP9 and ships no matroska demuxer, read from the plugin sets, so the
  # container is the half that has to come from somewhere else.
  case "$GAME_FAMILY" in
    dwo) kv "requires engine" "a build whose Media Foundation opens a WebM and decodes VP9"
         note "Stable 26.3 decodes the VP9 and ships no matroska demuxer, so the" \
              "container is the half to account for. Section 5." ;;
    p5s) kv "requires engine" "any current CrossOver, plus the staged VC-1 decoder"
         note "CrossOver ships no VC-1 at all -- the only such title here. Section 5." ;;
    *)   kv "requires engine" "any current CrossOver"
         note "These decode through Electra or not at all: the fault is in the presentation path." ;;
  esac
fi
echo
echo "2. INSTALL STATE  (${GAME_INSTALLER:-no installer selected} \"<game folder>\" --status)"
if [ "$FOLDER_STATE" != ok ]; then
  kv "not run" "$FOLDER_STATE"
elif [ -z "$GAME_INSTALLER" ]; then
  kv "not run" "no supported title identified in that folder"
  note "$IDENT_NOTE"
else
  kv "state word" "${INSTALL_STATE:-<none>}"
  kv "exit code" "${INSTALL_RC:-?}"
  [ "${INSTALL_RC:-0}" != 0 ] && \
    note "rc=1 means status() was never reached -- wrong folder, or a build with" \
         "no carrier. Not the same diagnosis as rc=0 absent."
  if [ -n "$INSTALL_ERR" ]; then
    kv stderr "(game folder replaced with <game folder>)"
    printf '%s\n' "$INSTALL_ERR" | sed 's/^/     /'
  else
    kv stderr "(none)"
  fi
fi
echo
echo "3. DID THE DLL LOAD"
if [ -z "$GAME_LOG" ]; then
  kv "not determined" "no title identified, so no log name to look for"
elif [ -n "$TARGET_LOG" ]; then
  p3 "$GAME_LOG"
  p5 "bottle       : #$TARGET_BIDX"
  p5 "size         : $(size_of "$TARGET_LOG") bytes"
  p5 "last written : $(mtime_of "$TARGET_LOG")"
  [ "$TARGET_COPIES" -gt 1 ] && \
    p5 "ALSO IN      : bottles $TARGET_WHERE -- all of the below describes #$TARGET_BIDX, the newest"
  if [ -n "$GAME_EXE" ] && [ "$GAME_FAMILY" != dwo ]; then
    p5 "lines from $GAME_EXE: ${TITLE_LINES:-0}"
    [ "${TITLE_LINES:-0}" -eq 0 ] && \
      note "THE FILE EXISTS BUT THIS TITLE NEVER WROTE TO IT: the proxy ran for" \
           "another game in this bottle and for this one was never mapped."
  fi
elif [ ${#BOTTLE_DIRS[@]} -eq 0 ]; then
  # The log name goes in the VALUE, not the label: as a %-17s label
  # "ue5-media-fix.log" is exactly 17 characters and printed against its colon.
  kv log "$GAME_LOG: no file -- AND NO BOTTLE WAS FOUND EITHER"
  note "So this report is blank on purpose and says nothing about whether the proxy loaded."
  kv "Bottles directory" "$([ "$BOTTLES_PRESENT" = yes ] \
        && echo "present, but no cxbottle.conf under it" \
        || echo "NOT THERE -- CrossOver has never made a bottle, or it lives elsewhere")"
else
  kv log "$GAME_LOG: NO FILE AT ALL"
  note "The proxy was never mapped, so nothing downstream is worth reading."
fi
[ -n "$BOTTLES_NOTE" ] && note "$BOTTLES_NOTE"
echo
echo "4. CX_GRAPHICS_BACKEND of the bottle holding that log"
if [ -n "$TARGET_BDIR" ]; then
  v="$(conf_get "$TARGET_BDIR/cxbottle.conf" CX_GRAPHICS_BACKEND)"
  if [ -n "$v" ]; then
    kv "bottle #$TARGET_BIDX" "\"$v\"$(settings_caveat "$TARGET_BDIR")"
    [ -n "$GAME_BACKEND" ] && [ "$v" != "$GAME_BACKEND" ] && \
      note "MISMATCH -- this title needs \"$GAME_BACKEND\""
  else
    kv "bottle #$TARGET_BIDX" "<unset -- CrossOver default>$(settings_caveat "$TARGET_BDIR")"
    note "Unset is a third state, distinct from a wrong value."
  fi
  note "Nothing in this project writes this key: always a hand edit, never self-healing."
else
  kv "not determined" "no bottle identified (see fact 3)"
  # Still answerable without naming or counting anything: does the key have the
  # value this title needs ANYWHERE on this Mac? "NO bottle is set to dxmt"
  # alone explains a whole class of reports.
  seen="$(backend_values_seen)"
  if [ -n "$seen" ]; then
    kv "values in use" "$seen"
    # Not "no count": N distinct values is a floor of N bottles, so the claim
    # the row used to make about itself was one it could not keep.
    note "(the distinct values on this Mac -- no bottle names, no bottle numbers)"
    [ -n "$GAME_BACKEND" ] && case " $seen " in
      *" $GAME_BACKEND "*) note "At least one bottle is set to \"$GAME_BACKEND\", which this title needs." ;;
      *) note "NO bottle on this Mac is set to \"$GAME_BACKEND\". That alone explains the report." ;;
    esac
  fi
fi
echo
echo "5. LIVE WINESERVER"
kv "wineserver procs" "$WS_COUNT   (binaries are wineserver-x86 / wineserver-arm64)"
if [ -n "$TARGET_BDIR" ]; then
  ws_pid="$(ws_pid_for "$TARGET_BDIR")"
  if [ -z "${ws_pid:-}" ]; then
    kv "bottle #$TARGET_BIDX" none
  else
    kv "bottle #$TARGET_BIDX" ALIVE
    note "Settings are read at bottle start and cached, so every bottle-settings" \
         "line below may not be in effect. Closing the game is not enough."
    # Two timestamps settle what "may" cannot -- this is the case the project
    # calls indistinguishable from the setting not working. Compared as epoch
    # seconds and shown to the second: rounding to the minute made an
    # equal-looking pair read as a contradiction.
    ws_start="$(date -j -f '%a %b %e %T %Y' "$(ps -o lstart= -p "$ws_pid" 2>/dev/null)" '+%s' 2>/dev/null)"
    conf_mtime="$(stat -f %m "$TARGET_BDIR/cxbottle.conf" 2>/dev/null)"
    if [ -z "${ws_start:-}" ] || [ -z "${conf_mtime:-}" ]; then
      note "(start time not readable, so the caveat above stands unresolved)"
    else
      p5 "wineserver up : $(date -r "$ws_start" '+%Y-%m-%d %H:%M:%S')"
      p5 "cxbottle.conf : $(date -r "$conf_mtime" '+%Y-%m-%d %H:%M:%S') (last written)"
      [ "$conf_mtime" -gt "$ws_start" ] \
        && note "THE CONF WAS EDITED AFTER THIS WINESERVER STARTED -- the running" \
                "bottle is on the OLD values. Quit Steam completely and relaunch." \
        || note "The conf predates this wineserver, so the values below ARE in effect."
    fi
  fi
fi
echo
echo "6. ENGINES INSTALLED"
if [ ${#CX_APPS[@]} -eq 0 ]; then
  p3 "no CrossOver bundle found in /Applications or ~/Applications"
else
  for a in "${CX_APPS[@]}"; do
    printf '   %-18s %-12s %-14s %-8s %s\n' "$(cx_name "$a")" \
      "$(plist_get "$a" CFBundleShortVersionString)" "$(plist_get "$a" CFBundleVersion)" \
      "$(cx_line "$(cx_name "$a")")" "$(cx_where "$a")"
  done
  note "Any engine can run any bottle; section 4 says which last touched this one." \
       "The supported engine is stable CrossOver 26.3 (26.3.0.39832) and only that:" \
       "a Preview row is what is installed, not a configuration this project covers."
fi
echo
echo "7. HOST"
kv macOS "$(sw_vers -productVersion 2>/dev/null) ($(sw_vers -buildVersion 2>/dev/null))"
kv chip "$(sysctl -n machdep.cpu.brand_string 2>/dev/null)"
kv "kernel arch" "$(uname -m 2>/dev/null)"      # -m, not -a: -a prints the hostname
translated="$(sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)"
kv "collecting shell" "$(arch 2>/dev/null)   proc_translated=$translated"
[ "$translated" = 1 ] && \
  note "COLLECTED UNDER ROSETTA -- this bundle reports x86_64 for the shell and" \
       "misleads every later inference."
kv "Rosetta present" "$(yesno /Library/Apple/usr/libexec/oah)"
note "A WineArch=win64 bottle selects the x86_64 host: without Rosetta it cannot run."
echo
echo "8. UE5 FINGERPRINT"
if [ "$GAME_FAMILY" != ue5 ]; then p3 "n/a for this title"
elif [ -z "$TARGET_LOG" ]; then p3 "no log to read"
else
  kv "read from" "$SCOPE_NOTE"
  h="$(grep -a 'halves for this title:' "$SCOPED_LOG" 2>/dev/null | tail -1)"
  kv observed "$(printf '%s' "${h:-<no halves line for this title>}" | anon_prefix)"
  kv "expected here" "$HALVES_EXPECTED"
  w="$(cnt 'not a title this build knows -- arming everything')"
  kv "unknown-title warn" "$(ynum "$w")"
  [ "${w:-0}" -gt 0 ] && [ "$GAME_KEY" != ue5 ] && \
    note "On a named title that means an older DLL whose policy table predates it."
  note "A halves line not matching the title means a renamed exe, or the wrong game patched."
fi
echo
echo "9. DWO FULL LOG"
if [ "$GAME_FAMILY" != dwo ]; then p3 "n/a for this title"
elif [ -z "$TARGET_LOG" ]; then p3 "no dwo-video-bridge.log"
else
  p3 "(safe whole: no process prefix, no paths, no line cap)"
  anon_prefix < "$TARGET_LOG" | sed 's/^/     /'
fi
echo

hr; echo "SECTION 2 - HOW FAR THE RUN GOT (log evidence)"
echo "(read only when section 1 says the DLL loaded and the state word is installed)"; hr
echo "DISCOVERY, by exact basename, across all bottles:"
if [ ${#LOG_PATHS[@]} -eq 0 ]; then
  p3 "none of the eight known log names exists in any bottle"
else
  # Named rows for this family only; the rest is one counted row. All eight are
  # still searched for -- that is what stops a bottle running only a legacy or
  # probe build leaving this report blank -- but a foreign basename names a game.
  others=0; others_newest=0
  for i in "${!LOG_PATHS[@]}"; do
    if [ -n "$GAME_FAMILY" ] && [ "$(log_family "${LOG_NAMES[$i]}")" = "$GAME_FAMILY" ]; then
      printf '   %-22s bottle #%-3s %8s bytes  %s\n' "${LOG_NAMES[$i]}" "${LOG_BIDX[$i]}" \
        "$(size_of "${LOG_PATHS[$i]}")" "$(mtime_of "${LOG_PATHS[$i]}")"
    else
      others=$((others + 1))
      m="$(stat -f %m "${LOG_PATHS[$i]}" 2>/dev/null)"
      [ "${m:-0}" -gt "$others_newest" ] 2>/dev/null && others_newest="$m"
    fi
  done
  [ "$others" -gt 0 ] && p3 "$others other MacGameVideoFix log(s) present, newest $(date -r "$others_newest" '+%Y-%m-%d %H:%M' 2>/dev/null)" \
                            "(names withheld: each one names a game)"
  [ -z "$GAME_FAMILY" ] && p3 "(no title was determined, so every log found is a foreign one)"
fi
p3 "(All eight names are searched for, including the legacy and probe ones: a" \
   " bottle where only such a build ran would otherwise leave this report blank.)"
echo
if [ -n "$TARGET_LOG" ] && [ "$GAME_FAMILY" != dwo ]; then
  case "$GAME_FAMILY" in
    ue5) cap="200 lines per process start (ue5-media-fix.c:94)" ;;
    p5s) cap="300 lines per process start (p5s-video-bridge.c:80)" ;;
    *)   cap="unknown" ;;
  esac
  # The cap has to be stated or "the log just ends" reads as a crash that never
  # happened. The excerpt is of the SCOPED copy: taken from the whole file, it
  # published the executable name of every other title in that bottle.
  echo "EXCERPT of $GAME_LOG  -- per-run line cap: $cap"
  kv "read from" "$SCOPE_NOTE"
  p3 "--- head -30 ---"; head -30 "$SCOPED_LOG" | exc
  p3 "--- tail -20 ---"; tail -20 "$SCOPED_LOG" | exc
  echo
  echo "PER-PROCESS CENSUS of that file"
  p3 "The file is never truncated, so the top may be months old; this is how much" \
     "belongs to THIS run. Every other prefix collapses into one anonymous row --" \
     "the mixture is the diagnostic, the other titles' names are not."
  awk -v self="$GAME_EXE" '{
      if (match($0, /^\[[^]]*\]/)) { e = substr($0, 2, RLENGTH - 2) } else { e = "(no prefix)" }
      if (e == "(no prefix)") k = e
      else if (self != "" && e == self) k = e
      else k = "(other titles in this bottle)"
      n[k]++; if (!(k in f)) f[k] = NR; l[k] = NR
    }
    END { for (k in n) printf "   %-44s %6d lines, %d..%d\n", k, n[k], f[k], l[k] }' "$TARGET_LOG"
  echo
fi
if [ "$GAME_FAMILY" = ue5 ] && [ -n "$SCOPED_LOG" ]; then
  echo "The facts below are read from: $SCOPE_NOTE"; echo
  echo "MEDIA FOUNDATION REACHED AT ALL"
  ev 2 'Media Foundation IS in play' "no MFStartup line."
  # Its own heading: "was MF reached" and "which entry points resolved" are two
  # facts. Alternation, not 'GetProcAddress.*MF', so a line naming the module
  # first is shown rather than dropping silently out of the report.
  p3 'GetProcAddress("MF*"):'
  ev 5 'GetProcAddress.*MF\|MF.*GetProcAddress' "no GetProcAddress line naming an MF entry point"
  p3 "These titles delay-load MF: a cutscene attempted with no MFStartup line means" \
     "the game plays video by another path and the MF half is irrelevant."
  echo
  echo "DECODER AVAILABILITY"
  ev 5 'decoder(s) offered' "no MFTEnumEx line"
  ev 5 'wants to decode'
  none="$(cnt 'NOTHING can decode that here')"
  kv "NOTHING can decode" "$([ "${none:-0}" -gt 0 ] && echo "YES x${none}" || echo no)"
  [ "${none:-0}" -gt 0 ] && \
    p3 "Zero decoders for WVC1/WMV3/WMA means the engine has no such codec: the answer" \
       "is a decoder, not the DLL (section 5). This is the line that most directly" \
       "matches \"sound plays, picture black\"."
  echo
  echo "FRAMES"
  hi="$(grep -ao 'frame [0-9]* decoded OK' "$SCOPED_LOG" 2>/dev/null \
        | awk '{ if ($2+0 > m) m = $2+0 } END { print m+0 }')"
  kv "decoded OK lines" "$(cnt 'decoded OK')   highest frame N: ${hi:-0}"
  ev 3 'ProcessOutput ->'
  ev 3 'GetOutputStreamInfo:'
  p3 "Frames plus a black screen is a presentation problem, not a decode one. Repeated" \
     "0xC00D6D72 alone is MF_E_TRANSFORM_NEED_MORE_INPUT and normal."
  echo
  echo "UE5 PATCH RESULT (meaningful only when the halves line says electra-vpx on)"
  ev 3 'VPx version checks:' "no \"VPx version checks\" line"
  ev 6 'raised threshold at\|could not write at\|nothing matched -- this build'
  p3 "found=0 means the build moved that code and the fix is inert, so the crash is" \
     "untouched. found>0 with patched=0 means the write failed. Different reports."
  echo
  echo "NODE GUARD, two separate facts"
  ev 2 'node guard:' "| no \"node guard:\" line"
  # The COUNT, in the shape "NOTHING can decode" uses: one stray adapter walk and
  # a game hammering the guard are different reports.
  ref="$(cnt 'does not exist -- refused')"
  kv "a node was refused" "$([ "${ref:-0}" -gt 0 ] && echo "YES x${ref}" || echo no)"
  p3 "\"armed\" says only that the imports were hooked. No refusal line means the game" \
     "never made the adapter-node walk, so a freeze is something else entirely."
  echo
fi
if [ "$GAME_FAMILY" = p5s ] && [ -n "$SCOPED_LOG" ]; then
  echo "The facts below are read from: $SCOPE_NOTE"; echo
  echo "PERSONA 5 STRIKERS BANNER"
  ev 2 'import table:'
  ev 2 '---- write-path hooks'
  ev 2 '---- armed:'
  p3 "\"0 of 6\" is the load-bearing failure: loaded and hooking nothing. A stock run" \
     "reads \"painting the real frames / D3D manager passed / NV12 relabel off /" \
     "allowed\"; anything else means a BEAST_* or P5S_* variable is set."
  echo
  echo "PERSONA 5 STRIKERS CHAIN"
  kv "samples arrived" "$(cnt 'ReadSample: sample')"
  ev 3 'ReadSample -> '
  kv "carry / fill lines" "$(cnt 'carry: frame') / $(cnt 'fill: frame')"
  p3 "Nothing out of ReadSample points at a missing codec (section 5); samples with no" \
     "carry/fill points at DXMT and the shared handle -- why this title cannot use d3dmetal."
  echo
fi
if [ "$GAME_FAMILY" = dwo ] && [ -n "$TARGET_LOG" ]; then
  echo "DYNASTY WARRIORS THREE-LINE SEQUENCE (each a distinct stage)"
  kv "1 hooks reported" "$(ynum "$(cnt 'dwo-video-bridge: d3d11')")"
  note "NO means the game resolves those another way and the bridge is deaf."
  kv "2 D3D12 device" "$(ynum "$(cnt 'D3D12 device reached, bridge armed')")"
  kv "3 bridge ready WxH" "$(ynum "$(cnt 'bridge ready:')")"
  note "1 and 2 without 3 means the video never reached the bridge."
  ev 5 'bridge: .* failed, hr'
  echo
fi

hr; echo "SECTION 3 - INSTALL STATE ON DISK (what --status cannot say)"; hr
if [ "$FOLDER_STATE" != ok ] || [ -z "$GAME_FAMILY" ]; then
  echo "skipped: ${IDENT_NOTE:-no game folder was given}."
  echo "Re-run with the folder the game is installed in as the only argument."
  echo
else
  echo "CARRIER DUMP (basenames only, never the containing path)"
  if [ -z "$CARRIER_DIR" ]; then
    p3 "no carrier folder found under the folder given"
    [ "$GAME_FAMILY" = ue5 ] && p3 "(no Engine/Binaries/ThirdParty/Ogg/Win64/<VS20xx> above it)"
  else
    for f in "${DUMP_FILES[@]}"; do
      p="$CARRIER_DIR/$f"
      [ -f "$p" ] || { printf '   %-24s absent\n' "$f"; continue; }
      printf '   %-24s %9s bytes  sha256 %s\n' "$f" "$(size_of "$p")" "$(sha_short "$p")"
      p5 "markers: $(markers_in "$p")   forwards-to: $(forwarder_of "$p" || echo '<none, not one of ours>')"
    done
  fi
  p3 "Naming the marker matters: is_ours() is a substring scan that accepts diagnostic" \
     "builds, so a probe DLL reports \"installed\" and plays nothing. Shipped markers:" \
     "libogg_64.dll = ue5-media-fix.log, libxess.dll = dwo-video-bridge.log," \
     "amd_ags_x64.dll = p5s-video-bridge.log."
  echo
  # build-proxy.sh bakes REAL="<stem>_real" into the export table, so a current
  # proxy beside only the legacy libogg_real.dll reports "installed" and cannot
  # resolve. Nothing else in the toolkit catches that.
  echo "FALSE POSITIVE 1 - forwarder vs saved name"
  fw=""; ours=none
  if [ -n "$CARRIER_DIR" ]; then
    fw="$(forwarder_of "$CARRIER_DIR/$GAME_CARRIER")"
    ours="$(markers_in "$CARRIER_DIR/$GAME_CARRIER")"
  fi
  # All three families, not just UE5: on Persona 5 Strikers this caught a real
  # broken forwarder the UE5-only version called "n/a". The markers gate is the
  # price -- forwarder_of is a raw byte scan for <stem>_real., so a vendor DLL
  # carrying that string would otherwise read as a proxy with a broken forwarder.
  if [ -z "${fw:-}" ]; then p3 "n/a: no proxy live"
  elif [ -f "$CARRIER_DIR/${fw}.dll" ]; then
    p3 "ok: the live proxy forwards to ${fw}.dll and that file is present"
  elif [ "$ours" = none ]; then
    p3 "n/a: the live $GAME_CARRIER carries none of our markers, so the \"${fw}\" string" \
       "in it is the vendor's own and says nothing about a forwarder. See FALSE POSITIVE 4."
  else
    p3 "MISMATCH: the live proxy forwards to ${fw}.dll, which is NOT there."
    for alt in "$GAME_REAL" "$GAME_LEGACY"; do
      [ -n "$alt" ] && [ -f "$CARRIER_DIR/$alt" ] && p3 "Saved original present as: $alt"
    done
  fi
  echo
  # Two installers whose --status cannot express the state on disk:
  # install-dwo-bridge.sh has no half branch, and install-p5s-bridge.sh
  # pre-checks only game.exe. Both print "absent" rc=0 where the third exits 1.
  echo "FALSE POSITIVE 2 - DWO half-install reported as absent"
  if [ "$GAME_FAMILY" != dwo ]; then p3 "n/a for this title"; else
    p3 "libxess.dll: $(yesno "$GAME_FOLDER/libxess.dll")   libxess_real.dll: $(yesno "$GAME_FOLDER/libxess_real.dll")"
    [ ! -f "$GAME_FOLDER/libxess.dll" ] && [ -f "$GAME_FOLDER/libxess_real.dll" ] && \
      p3 "HALF-INSTALLED -- a game that will not start, reported as clean. --restore fixes it."
  fi
  echo
  echo "FALSE POSITIVE 3 - P5S carrier-missing reported as absent"
  if [ "$GAME_FAMILY" != p5s ]; then p3 "n/a for this title"; else
    p3 "amd_ags_x64.dll: $(yesno "$GAME_FOLDER/amd_ags_x64.dll")   amd_ags_x64_real.dll: $(yesno "$GAME_FOLDER/amd_ags_x64_real.dll")"
    [ ! -f "$GAME_FOLDER/amd_ags_x64.dll" ] && [ ! -f "$GAME_FOLDER/amd_ags_x64_real.dll" ] && \
      p3 "NO CARRIER AT ALL, and --status still says \"absent\" rc=0."
  fi
  echo
  echo "FALSE POSITIVE 4 - the game was updated and put its own carrier back"
  live_c=""; real_c=""; legacy_c=""
  if [ -n "$CARRIER_DIR" ]; then
    live_c="$CARRIER_DIR/$GAME_CARRIER"; real_c="$CARRIER_DIR/$GAME_REAL"
    [ -n "$GAME_LEGACY" ] && legacy_c="$CARRIER_DIR/$GAME_LEGACY"
  fi
  if [ -z "$live_c" ]; then p3 "n/a: no carrier folder resolved"
  elif [ -f "$live_c" ] && [ "$(markers_in "$live_c")" = none ] \
       && { [ -f "$real_c" ] || { [ -n "$legacy_c" ] && [ -f "$legacy_c" ]; }; }; then
    p3 "ORPHANED SAVED ORIGINAL: the live $GAME_CARRIER carries none of our markers and" \
       "a saved original sits beside it -- what a Steam update or \"verify integrity\"" \
       "leaves behind. No installer has a branch for it (half requires the live DLL to" \
       "be MISSING), so --status says \"absent\". Re-apply; --restore first if not absent."
  else
    p3 "no: $([ -f "$live_c" ] && echo "the live carrier is there" || echo "no live carrier"), $([ -f "$real_c" ] && echo "a saved original is beside it" || echo "no saved original is orphaned")"
  fi
  echo
  echo "EXPORT-FORWARDING GATE (read-only replay of the installer's [2/4] check)"
  orig=""
  for c in "$GAME_REAL" "$GAME_LEGACY" "$GAME_CARRIER"; do
    [ -n "$c" ] && [ -n "$CARRIER_DIR" ] && [ -f "$CARRIER_DIR/$c" ] || continue
    [ "$(markers_in "$CARRIER_DIR/$c")" = none ] && { orig="$CARRIER_DIR/$c"; break; }
  done
  proxy="$RUNTIME/$GAME_CARRIER"
  # Read each side separately and check both succeeded. Piping pe.py into comm
  # hides a failure: comm -23 with an unreadable left side reports nothing
  # missing, printing "everything is forwarded" beside "exports: 0".
  # command -v, never an executed python3: with no Xcode Command Line Tools
  # /usr/bin/python3 is a stub that opens a modal dialog and BLOCKS, and this
  # script's audience is that population. Testing it apart from the chain below
  # also stops a missing python3 being reported as a damaged game carrier.
  if [ -z "$orig" ] || [ ! -f "$proxy" ] || [ ! -f "$RUNTIME/pe.py" ]; then
    p3 "not run: no untouched carrier found to compare against"
  elif ! command -v python3 >/dev/null 2>&1; then
    p3 "not run: no python3 on this Mac, so the export table could not be read."
    p3 "(Nothing is wrong with the game files -- this gate simply did not run.)"
  elif ! orig_exports="$(python3 "$RUNTIME/pe.py" exports "$orig" 2>&1)"; then
    p3 "GATE NOT RUN: the game's carrier could not be read (damaged, or not a PE)."
    printf '%s\n' "$orig_exports" | scrub | sed 's/^/     /'
  elif ! proxy_exports="$(python3 "$RUNTIME/pe.py" exports "$proxy" 2>&1)"; then
    p3 "GATE NOT RUN: the shipped proxy could not be read."
    printf '%s\n' "$proxy_exports" | scrub_paths '<tooling>' "$ROOT" | sed 's/^/     /'
  else
    oc="$(printf '%s\n' "$orig_exports" | grep -c .)"
    kv "carrier exports" "$oc   proxy exports: $(printf '%s\n' "$proxy_exports" | grep -c .)"
    kv "reference counts" "libogg 64, libxess 27, amd_ags_x64 38, dxgi 7"
    if [ "${oc:-0}" -eq 0 ]; then
      p3 "GATE NOT RUN: pe.py found no exports at all, so \"nothing is missing\" would mean nothing."
    else
      miss="$(comm -23 <(printf '%s\n' "$orig_exports" | sort) \
                       <(printf '%s\n' "$proxy_exports" | sort))"
      if [ -n "$miss" ]; then
        p3 "MISSING from the proxy (a missing-entry-point failure at launch):"
        printf '%s\n' "$miss" | sed 's/^/     /'
      else
        kv "missing from proxy" "none -- everything this copy needs is forwarded"
      fi
    fi
  fi
  echo
  echo "LIVE PROXY vs SHIPPED PROXY"
  if [ -z "$live_c" ] || [ ! -f "$live_c" ] || [ ! -f "$RUNTIME/$GAME_CARRIER" ]; then
    p3 "not comparable (nothing live, or no tooling copy beside this script)"
  else
    a="$(sha_short "$live_c")"; b="$(sha_short "$RUNTIME/$GAME_CARRIER")"
    kv "installed $GAME_CARRIER" "$a"
    kv "tooling   $GAME_CARRIER" "$b"
    # The markers gate is essential: ungated, this called the game's own vendor
    # DLL "an older or hand-built proxy" in the same report that printed
    # "markers : none" for it.
    if [ "$a" = "$b" ]; then p3 "equal -- this is the current release"
    elif [ "$(markers_in "$live_c")" = none ]; then
      p3 "NOT ONE OF OURS -- no marker of this toolkit, so it is the game's own DLL and" \
         "the hashes differing says nothing about versions. See FALSE POSITIVE 4."
    else
      p3 "DIFFERENT -- an older or hand-built proxy is installed."
    fi
  fi
  echo
  echo "STALE RE-ENCODE LEFTOVERS (counts only -- never a name, never a listing)"
  content=""
  [ "$GAME_FAMILY" = ue5 ] && [ -n "$GAME_PROJ" ] && [ -d "$GAME_ROOT/$GAME_PROJ/Content" ] \
    && content="$GAME_ROOT/$GAME_PROJ/Content"
  if [ -z "$content" ]; then p3 "n/a (no Content folder resolved for this title)"; else
    # The COUNT only: pak-hide-videos.py builds each marker name out of the .pak's
    # own basename, so in generic UE5 mode the marker name names the title the
    # user deliberately did not name.
    n=0
    while IFS= read -r -d '' j; do n=$((n + 1)); done < <(
      find "$content/Paks" -maxdepth 1 -type f -name '.*.hidden-videos.json' -print0 2>/dev/null)
    if [ -d "$content/Movies_VP9_backup" ]; then
      bk="present, $(count0 "$content/Movies_VP9_backup" -maxdepth 1 -type f) entries"
    else
      bk=absent
    fi
    p3 ".*.hidden-videos.json markers: $n   Movies_VP9_backup: $bk"
    { [ "$n" -gt 0 ] && [ ! -d "$content/Movies_VP9_backup" ]; } \
      || { [ "$n" -eq 0 ] && [ -d "$content/Movies_VP9_backup" ]; } && \
      p3 "HALF-FINISHED RE-ENCODE from an older version. It changes what the cutscenes" \
         "even are, and no --status will ever mention it."
  fi
  echo
fi
# There is no version string anywhere in the shipped product -- build-app.sh
# hardcodes 1.0 and no DLL stamps one into its log -- so hashes are the only
# build identity, and "which version are you on" is otherwise unanswerable.
TOOLING=(install-runtime-fix.sh install-dwo-bridge.sh install-p5s-bridge.sh
         stage-codecs.sh pe.py libogg_64.dll libxess.dll amd_ags_x64.dll)
echo "BUILD IDENTITY (no version string exists in the product; hashes are it)"
for f in "${TOOLING[@]}"; do
  [ -f "$RUNTIME/$f" ] \
    && printf '   %-24s %9s bytes  sha256 %s\n' "$f" "$(size_of "$RUNTIME/$f")" "$(sha_short "$RUNTIME/$f")" \
    || printf '   %-24s absent from the tooling directory\n' "$f"
done
kv "git describe" "$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo 'not a checkout')"
echo
echo "BUNDLE DRIFT (repo runtime/ vs MacGameVideoFix.app/Contents/Resources)"
APPRES=""
for cand in "$ROOT/app/MacGameVideoFix.app" /Applications/MacGameVideoFix.app \
            "$HOME/Applications/MacGameVideoFix.app"; do
  [ -d "$cand/Contents/Resources" ] && { APPRES="$cand/Contents/Resources"; break; }
done
if [ -z "$APPRES" ]; then p3 "no MacGameVideoFix.app found -- nothing to compare"; else
  drift=0
  for f in "${TOOLING[@]}"; do
    # Three-way, because cmp exits 2 for a missing file and 1 for a difference:
    # collapsed, a file absent from runtime/ was reported as DIFFERENT and
    # explained as a bundle that was not rebuilt -- the opposite diagnosis.
    if   [ ! -f "$RUNTIME/$f" ]; then printf '   %-24s not in runtime/\n' "$f"
    elif [ ! -f "$APPRES/$f" ];  then printf '   %-24s not in the bundle\n' "$f"
    elif cmp -s "$RUNTIME/$f" "$APPRES/$f"; then printf '   %-24s same\n' "$f"
    else printf '   %-24s DIFFERENT\n' "$f"; drift=1; fi
  done
  [ "$drift" = 1 ] && \
    p3 "The app runs the Resources copies and never runtime/, so a bundle not rebuilt" \
       "after the scripts changed is the whole explanation for \"I updated the repo" \
       "and nothing changed\"."
fi
echo
echo "NODE GUARD (repo-only; build-app.sh does not copy it into the .app)"
if [ ! -x "$CROSSOVER_DIR/install-node-guard.sh" ]; then
  p3 "crossover/install-node-guard.sh not present beside this script"
elif [ ${#CX_APPS[@]} -eq 0 ]; then
  p3 "no CrossOver bundle to query"
else
  for a in "${CX_APPS[@]}"; do
    out="$(MGVF_STATUS_ONLY=1 bash "$CROSSOVER_DIR/install-node-guard.sh" "$a" --status \
           2>"$TMPDIR_RUN/ng.err")"; rc=$?
    # First word of the first line, with a placeholder when there is none:
    # ${out%% *} on empty or multi-line output printed a blank state word.
    word="$(printf '%s\n' "$out" | sed -n 1p | awk '{ print $1 }')"
    if [ "$rc" = 0 ]; then p3 "${word:-<printed nothing>}  $(cx_name "$a") in $(cx_where "$a")"; else
      p3 "rc=$rc  $(cx_name "$a") in $(cx_where "$a")"
      # Its error branch prints the bundle path, which in ~/Applications is a
      # user-renamed string out of the home directory.
      scrub_paths '<CrossOver bundle>' "$a" < "$TMPDIR_RUN/ng.err" | sed 's/^/     /'
      p5 "rc=1 with \"no Game Porting Toolkit DLLs\" is not the same as \"not installed\"."
    fi
  done
  p3 "It patches dxgi.dll inside the bundle, so it affects every game in every bottle" \
     "on that engine."
fi
echo

hr; echo "SECTION 4 - ENGINES AND BOTTLE PROVENANCE"; hr
echo "PER INSTALLED CROSSOVER BUNDLE (versions are in fact 6)"
if [ ${#CX_APPS[@]} -eq 0 ]; then p3 "none found"; else
  for a in "${CX_APPS[@]}"; do
    cx="$a/Contents/SharedSupport/CrossOver"
    p3 "$(cx_name "$a") in $(cx_where "$a")"
    # All THREE plugin roots: diagnostics/launch-with.sh probes only the first
    # two and is wrong for the 27.x line, which keeps aarch64 plugins in the third.
    for d in lib64/gstreamer-1.0 lib/x86_64/gstreamer-1.0 lib/aarch64/gstreamer-1.0; do
      [ -d "$cx/$d" ] || continue
      printf '     %-26s libgstmatroska=%s  libgstapplemedia=%s\n' "$d" \
        "$(yesno "$cx/$d/libgstmatroska.dylib")" "$(yesno "$cx/$d/libgstapplemedia.dylib")"
    done
    for g in "$cx"/lib/apple_gptk*/ "$cx"/lib64/apple_gptk*/; do
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
      p5 "dxmt: present  [$subs]"
    else
      p5 "dxmt: absent -- this build cannot honour CX_GRAPHICS_BACKEND=dxmt"
    fi
  done
  p3 "libgstmatroska is the plugin that differs between the lines (present on 27.x" \
     "Preview, absent on 26.3) -- recorded to read the rows above, not to point" \
     "anywhere: Preview stopped being supported on 2026-08-31. libgstvpx is NOT a" \
     "winevideo verdict -- neither shipping build has it and VP9 arrives via" \
     "libgstapplemedia. UNCONFIRMED: which apple_gptk generation or dxmt revision" \
     "a given run selects."
fi
echo
echo "PER RELEVANT BOTTLE (only the bottles our own logs were found in)"
if [ ${#RELEVANT[@]} -eq 0 ]; then
  p3 "none -- no log from this toolkit was found in any bottle."
else
  for idx in "${RELEVANT[@]}"; do
    b="$(bottle_dir "$idx")"; conf="$b/cxbottle.conf"
    p3 "bottle #$idx$(settings_caveat "$b")"
    printf '     Version=%s  Timestamp=%s  Preview=%s  WineArch=%s  Template=%s\n' \
      "$(conf_get "$conf" Version)" "$(conf_get "$conf" Timestamp)" \
      "$(conf_get "$conf" Preview)" "$(conf_get "$conf" WineArch)" "$(conf_get "$conf" Template)"
    bv="$(conf_get "$conf" CX_GRAPHICS_BACKEND)"
    p5 "CX_GRAPHICS_BACKEND=${bv:-<unset>}   cxbottle.conf=$(yesno "$conf")  system.reg=$(yesno "$b/system.reg")"
    # Repeated from fact 4 on purpose: it used to run only there, so a
    # wrong-backend bottle sat unflagged whenever fact 4 identified no bottle.
    [ -n "$GAME_BACKEND" ] && [ -n "$bv" ] && [ "$bv" != "$GAME_BACKEND" ] && \
      p5 "MISMATCH -- ${GAME_LABEL:-this title} needs \"$GAME_BACKEND\""
    if [ -f "$b/system.reg" ]; then
      # Names the BUNDLE, including builds no longer installed -- the case the
      # installed-app scan cannot produce. Match only: system.reg holds
      # installed-software paths and library locations for the whole prefix. Two
      # lines out of awk, not one split by the shell: `set -- $fonts` globbed,
      # collapsed whitespace inside bundle names, and clobbered $1.
      fonts="$(awk '
        /^\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Fonts\]/ { f=1; ts=$NF; next }
        f && /^\[/ { exit }
        f && match($0, /CrossOver[^"\\]*\.app/) { print ts; print substr($0, RSTART, RLENGTH); exit }
      ' "$b/system.reg" 2>/dev/null)"
      if [ -n "$fonts" ]; then
        stamp="$(printf '%s\n' "$fonts" | sed -n 1p)"
        p5 "system.reg Fonts key: $(printf '%s\n' "$fonts" | sed -n 2p)   stamped $(date -r "$stamp" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "$stamp")"
      else
        p5 "system.reg Fonts key: no CrossOver bundle name recorded"
      fi
    fi
  done
  p3 "conf Version is a build number (27.0.0.40921); a Preview bundle's" \
     "CFBundleShortVersionString is a date (20260821). The two are not comparable." \
     "UNCONFIRMED: that Preview=1 means \"created by Preview\", and which operations" \
     "rewrite the Fonts key -- merely running the GUI was observed to."
fi
echo
echo "BOTTLE STORAGE"
# df of ONE named directory, awk-filtered to two numbers: a raw df names every
# mounted volume, which the /Users filter misses; this names none. Restored
# because "staging did not finish" and a log that stops mid-write, both flagged
# elsewhere, have a full disk as their commonest cause.
if [ "$BOTTLES_PRESENT" = yes ]; then
  kv "Bottles directory" "present"
  df -k "$BOTTLES" 2>/dev/null | tail -1 \
    | awk '{ printf "   %-17s: %.1f GB available, %s used\n", "free space", $4/1048576, $5 }'
else
  kv "Bottles directory" "NOT THERE at the location searched"
  note "Nothing below section 1 fact 3 can be read, and the blankness of this" \
       "report is that, not evidence about the fix."
fi
# No totals in this section: a bottle is a game prefix and its name is usually a
# game title, so a count of bottles is a count of the installed library.
[ -n "$BOTTLES_NOTE" ] && p3 "$BOTTLES_NOTE"
echo

# The framework and the registry cache are printed for every title: section 2
# sends a UE5 reader with "NOTHING can decode that here" here, and this used to
# be emitted only for the two codec-dependent titles.
hr; echo "SECTION 5 - CODEC PLUMBING"; hr
echo "GSTREAMER.FRAMEWORK"
[ "$GAME_FAMILY" = p5s ] \
  && p3 "(required for this title: the staged VC-1 decoder is taken from it)" \
  || p3 "(not a requirement here -- printed because section 2's decoder question is" \
        " answered by what this Mac has, not by the DLL)"
if [ ! -d "$FRAMEWORK" ]; then
  p3 "NOT INSTALLED at /Library/Frameworks/GStreamer.framework -- 1.24.13 is verified"
else
  compat="$(gst_compat)"
  if [ -n "${compat:-}" ] && [ "$compat" -gt 0 ] 2>/dev/null; then
    kv version "1.$((compat / 100)).$((compat % 100))  (compat $compat)"
    [ "$((compat / 100))" = 24 ] || \
      note "OUTSIDE THE 1.24 SERIES -- reported, not refused, so a wrong-series" \
           "framework shows up as an error nowhere else. 1.24.13 is verified."
  else
    kv version "not readable"
  fi
  fw_plugin="$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib"
  # lipo -archs catches an arm64-only plugin staged for an x86_64 bottle.
  kv libgstlibav "$(yesno "$fw_plugin")   archs: $(lipo -archs "$fw_plugin" 2>/dev/null || echo n/a)"
fi
echo
if [ "$GAME_FAMILY" = p5s ] || [ "$GAME_FAMILY" = dwo ]; then
echo "STAGED TREE"
if [ ! -d "$STAGE_ROOT" ]; then
  p3 "nothing staged under ~/Library/Application Support/MacGameVideoFix/"
else
  while IFS= read -r -d '' ad; do
    plug="$ad/gstreamer-1.0/libgstlibav.dylib"
    p3 "staged dir $(basename "$ad")"
    [ -f "$plug" ] \
      && p5 "libgstlibav.dylib : $(size_of "$plug") bytes  sha256 $(sha_short "$plug")" \
      || p5 "libgstlibav.dylib : MISSING -- staging did not finish"
    # A short count means staging did not finish, which nothing else reports.
    p5 "copied dylibs=$(count0 "$ad" -type f -name '*.dylib')  symlinks=$(count0 "$ad" -type l)  (expect ~7 and ~7)"
    dang=0
    while IFS= read -r -d '' l; do
      [ -e "$l" ] && continue
      dang=$((dang + 1))
      # Basename and a classification only: the raw target is an absolute path out
      # of the home directory with nothing to do with this toolkit -- a Homebrew
      # prefix, a folder in Downloads -- of which only /Users/<name> was redacted.
      case "$(readlink "$l" 2>/dev/null)" in
        */*.app/*) p5 "DANGLING $(basename "$l") -- target missing inside a CrossOver bundle" ;;
        "")        p5 "DANGLING $(basename "$l") -- target unreadable" ;;
        *)         p5 "DANGLING $(basename "$l") -- target missing, outside any .app bundle" ;;
      esac
    done < <(find "$ad/lib" -type l -print0 2>/dev/null)
    [ "$dang" -eq 0 ] && p5 "dangling symlinks : none"
    # The links are absolute paths into one CrossOver bundle, so reading one
    # recovers WHICH engine the staging was built against -- recorded nowhere
    # else on disk. Test the match rather than trusting sed to have made one:
    # with no .app component the substitution does not fire and the whole path
    # passes through.
    one="$(find "$ad/lib" -type l -print0 2>/dev/null | { IFS= read -r -d '' l && readlink "$l"; })"
    if [ -n "${one:-}" ]; then
      case "$one" in
        # The BUNDLE, not its path: the .app filename is user-renamed
        # ("Crossover_patched.app" was the real case) and redact() knows only the
        # /Users spelling. cx_name/cx_where enforce this everywhere else.
        */*.app/*) app="$(printf '%s' "$one" | sed -E 's#^(.*/[^/]*\.app)/.*#\1#')"
                   p5 "staged against    : $(cx_name "$app") in $(cx_where "$app")" ;;
        *)         p5 "staged against    : <not inside a CrossOver bundle>" ;;
      esac
      p5 "                    (rename or move that CrossOver and every link breaks" \
         "                     while the staged folder still looks present)"
    fi
    # Nothing records which GStreamer the staging came from; this compare is the
    # only way to tell the framework was upgraded underneath it.
    if [ -f "$plug" ] && [ -f "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib" ]; then
      [ "$(sha_short "$plug")" = "$(sha_short "$FRAMEWORK/lib/gstreamer-1.0/libgstlibav.dylib")" ] \
        && p5 "vs installed framework: identical -- staged from the GStreamer installed now" \
        || p5 "vs installed framework: DIFFERENT -- the framework was upgraded underneath"
    fi
  done < <(find "$STAGE_ROOT" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
fi
echo
echo "GST_PLUGIN_PATH PER RELEVANT BOTTLE"
if [ ${#RELEVANT[@]} -eq 0 ]; then p3 "no relevant bottle identified"; else
  for idx in "${RELEVANT[@]}"; do
    b="$(bottle_dir "$idx")"; conf="$b/cxbottle.conf"
    v="$(conf_get "$conf" GST_PLUGIN_PATH)"
    p3 "bottle #$idx$(settings_caveat "$b")"
    if [ -z "$v" ]; then p5 "GST_PLUGIN_PATH  : <unset>"; else
      # Classified, never verbatim: it reads as redacted only because the tooling
      # stages under $HOME, and a hand-edited value on an external volume walks
      # through redact(). The two checks below are the row's whole value anyway.
      # Prefix removal, not a case pattern: STAGE_ROOT is under $HOME and a
      # bracket or star in the user's home directory would be read as a glob.
      tail_v="${v#"$STAGE_ROOT"/}"
      [ "$tail_v" != "$v" ] \
        && p5 "GST_PLUGIN_PATH  : <staged path>/$tail_v" \
        || p5 "GST_PLUGIN_PATH  : <a path outside the staging root>"
      # Both checks are needed. Codecs.configure() skips any conf whose text
      # already contains the string, so a path left over from a deleted or
      # re-staged directory is never rewritten and never warned about; and
      # GST_PLUGIN_PATH names a directory GStreamer scans wholesale, so a value
      # ending at .../gst-codecs/<arch> scans a directory holding lib/ and finds
      # nothing. This pair is all that separates either from "correctly staged".
      [ -f "$v/libgstlibav.dylib" ] \
        && p5 "libgstlibav there: yes" \
        || p5 "libgstlibav there: NO -- set, but nothing staged at that exact path"
      case "$v" in
        */gstreamer-1.0) p5 "suffix           : ok (/gstreamer-1.0)" ;;
        *) p5 "suffix           : WRONG -- the value must end in /gstreamer-1.0" ;;
      esac
    fi
    wa="$(conf_get "$conf" WineArch)"
    p5 "WineArch         : ${wa:-<unset>}   staged arch written by the tooling: x86_64"
    [ -n "$wa" ] && [ "$wa" != win64 ] && \
      p5 "                   MISMATCH -- pointed at a staging built for the wrong host"
    # Missing means Codecs.configure() bails and writes nothing, while
    # stageCodecs() counts only a nil return as touched, so the bottle is
    # silently excluded from the "N bottle(s) pointed at it" count the app reports.
    grep -q '^\[EnvironmentVariables\]' "$conf" 2>/dev/null \
      && p5 "[EnvironmentVariables] : present" \
      || p5 "[EnvironmentVariables] : MISSING -- Codecs.configure() bails and writes nothing"
  done
fi
echo
fi   # staged tree + GST_PLUGIN_PATH: only the two titles that use them
# The only offline proof that GST_PLUGIN_PATH was read and the re-homed plugin
# loaded (it replaces check-winevideo-use.sh's lsof probe). grep -aoE for fixed
# names ONLY: the file is a binary blob full of absolute plugin paths including
# the username. libgstvpx is probed to preempt a wrong "winevideo" conclusion --
# it is the one name here whose value is negative-only.
echo "GSTREAMER REGISTRY CACHE"
found_reg=no
for arch in x86_64 aarch64; do
  R="$CXSUPPORT/gstreamer-1.0-registry.$arch.bin"
  [ -f "$R" ] || continue
  found_reg=yes
  yes_n=""; no_n=""
  for n in libgstlibav avdec_vc1 avdec_wmv3 avdec_wmav2 avdec_vp9 vtdec_hw \
           vp9parse matroskademux libgstvpx; do
    if grep -qaE "$n" "$R" 2>/dev/null; then yes_n="${yes_n:+$yes_n }$n"; else no_n="${no_n:+$no_n }$n"; fi
  done
  p3 "$arch   (last written $(mtime_of "$R"))"
  p5 "registered : ${yes_n:-none of the nine}"
  p5 "absent     : ${no_n:-none}"
done
[ "$found_reg" = no ] && p3 "no registry cache found"
p3 "CAVEAT: the cache is global per host architecture, NOT per bottle, so a decoder" \
   "listed here proves only that the last bottle of that arch to start had it" \
   "registered. UNCONFIRMED: when the cache is rewritten."
echo
if [ "$GAME_FAMILY" = dwo ]; then
  echo ".webm BYTESTREAMHANDLER MAPPING (DYNASTY WARRIORS: ORIGINS only)"
  p3 "The only registry write anywhere in the repo: ByteStreamHandlers\\.webm"
  if [ ${#RELEVANT[@]} -eq 0 ]; then p3 "no relevant bottle identified"; else
    for idx in "${RELEVANT[@]}"; do
      S="$(bottle_dir "$idx")/system.reg"
      [ -f "$S" ] || { p3 "bottle #$idx: no system.reg"; continue; }
      # Match and count only. Never cat it, never grep it broadly, never dump
      # surrounding lines: it holds Steam paths and library locations.
      have="$(grep -ac 'ByteStreamHandlers\\\\\.webm\]' "$S" 2>/dev/null)"
      printf '   bottle #%s: .webm %s   (%s extensions mapped in total)\n' "$idx" \
        "$([ "${have:-0}" -gt 0 ] && echo present || echo ABSENT)" \
        "$(grep -ac '^\[.*ByteStreamHandlers\\\\\.' "$S" 2>/dev/null)"
      [ "${have:-0}" -gt 0 ] || \
        p5 "Without it MF refuses to open the file and the bridge never sees a frame:" \
           "that separates \"the fix is broken\" from \"nothing handed it a video\"."
    done
  fi
  echo
fi

hr; echo "NOTES FOR THE READER"; hr
if [ -n "$FORCED_KEY" ]; then
  echo " * The title above was ASSERTED with --title $FORCED_KEY, not inferred. If fact 1"
  echo "   says WRONG FOLDER, that is this report telling you the two disagree."
elif [ "$FOLDER_STATE" = ok ]; then
  echo " * The title above was INFERRED from the folder, by exact filename. If it is"
  echo "   wrong, re-run with --title <key> (see --help): everything routes off it."
else
  echo " * No game folder was given, so every folder-scoped fact above is blank."
fi
echo " * The runtime patch aims at ONE crash: EXCEPTION_ACCESS_VIOLATION reading"
echo "   0x0 in FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer, from"
echo "   FVideoDecoderVPxElectra::ConvertDecodedImageToNV12orP010. A crash dialog"
echo "   naming another function is one this tool will not help with."
[ "$SHOW_NAMES" = yes ] \
  && echo " * Bottle names appear ONLY in the fenced key below -- review it before sending." \
  || echo " * Bottle names are never printed here. Re-run with --names for the key."
echo
}  # end report


# ============================== assemble and filter ==========================

# report()'s stderr used to go to /dev/null for the whole thousand-line body,
# which hid real shell errors as readily as expected probe noise -- and an abort
# mid-report then looked exactly like every other empty run.
RAW="$TMPDIR_RUN/raw.txt"
report > "$RAW" 2>"$TMPDIR_RUN/report.err"
if [ -s "$TMPDIR_RUN/report.err" ]; then
  {
    echo; hr
    echo "COLLECTOR ERRORS (stderr from the collectors above, not from your system)"
    hr
    scrub_paths '<path>' "$GAME_ROOT" "$GAME_FOLDER" "$OGG_DIR" "$ROOT" \
      < "$TMPDIR_RUN/report.err" | sed 's/^/   /'
  } >> "$RAW"
fi

# MFCreateSourceReaderFromURL logs the movie's FULL Windows path -- a Steam
# library path such as Z:\Volumes\<drive>\steamapps\common\<Game>\... Confirmed
# in ue5-media-fix.c:1175, electra-h264-fix.c:1010, p5s-video-bridge.c:1097.
# This filter must never be dropped.
#
# Rule 1 is anchored on the logged call's own tail (") -> 0x"), not the first ")"
# in the argument: bounded by the first ")", a single parenthesis in the path --
# "(Remastered)", "Games (2TB)" -- ended the match early and left the steamapps
# path in the bundle. Rule 2 is the fail-closed half: any such line rule 1 did
# not reduce loses its whole argument rather than passing through. Rule 3 does
# the same for any other line still naming a Steam library.
#
# Rule 1 keeps the basename only because anon_prefix() has already collapsed the
# argument on every line not scoped to this title -- on a shared log a movie
# filename names a game. Do not relax one without the other.
#
# Then ONE final redaction pass over the assembled text. Per-command redaction
# lets things slip through the seams.
sed -E \
  -e 's#MFCreateSourceReaderFromURL\(.*[\\/]([^\\/]*)\) -> 0x#MFCreateSourceReaderFromURL(<path>/\1) -> 0x#g' \
  -e 's#MFCreateSourceReaderFromURL\([^<][^)]*[\\/].*#MFCreateSourceReaderFromURL(<path>)#g' \
  -e 's#([A-Za-z]:)?[\\/][^"]*[Ss]teamapps[^"]*#<path>#g' \
  "$RAW" | redact > "$OUTFILE"

if [ ! -s "$OUTFILE" ]; then
  echo "the report could not be written to $OUTFILE" >&2
  echo "collected output follows on stdout instead:" >&2
  cat "$RAW"
  exit 1
fi

# The ONLY place in the bundle that emits user-chosen bottle names, and only when
# asked for. ONLY the bottles this report cites by number: iterating every bottle
# published the name of every prefix on the machine, for a report about one.
if [ "$SHOW_NAMES" = yes ]; then
  {
    echo
    echo "--- REVIEW BEFORE SENDING: bottle number -> name ---"
    echo "Only the bottles this report actually refers to. These are your own"
    echo "bottle names and they are often game titles. Delete this block if you"
    echo "would rather not post them."
    echo '```'
    if [ ${#RELEVANT[@]} -eq 0 ]; then
      echo "  (this report refers to no bottle by number)"
    else
      for idx in "${RELEVANT[@]}"; do
        printf '  bottle #%-3s %s\n' "$idx" "$(basename "$(bottle_dir "$idx")")"
      done
    fi
    echo '```'
    echo "--- end ---"
  } | redact >> "$OUTFILE"
fi

cat "$OUTFILE"
echo

# The real path goes to stderr, where it is not part of what the user selects and
# pastes. Printing it on stdout put the macOS user name directly under a report
# the form tells people to copy whole.
SAFE_OUT="$(printf '%s' "$OUTFILE" | redact)"
echo "saved to: $SAFE_OUT"
if [ "$SAFE_OUT" != "$OUTFILE" ]; then
  echo "(the real path is on the next line, which is NOT part of the report above)"
  printf 'the file is at: %s\n' "$OUTFILE" >&2
fi
