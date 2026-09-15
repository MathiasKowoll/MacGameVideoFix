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
# Roots are ASKED FOR, not listed.
#
# A launcher that patches a copy of CrossOver declares where it keeps bottles in
# that copy's etc/CrossOver.conf, so every engine on the machine is asked and
# whatever it says is a root. Writing product names here meant the list went
# stale the moment one was renamed -- which happened: the fork became RaccoonBot,
# its support folder moved with it, and a hardcoded "Procyon/CXPBottles" knew
# about neither. The engines knew all along.
# ONE bottle, named by whoever already knows which one.
#
# The roots above answer "where could this game be launched from", and every
# installer that writes a registry override used to write to all of them. That
# is overreach the moment another product owns one: MacGameVideoFix writing into
# RaccoonBot's bottle duplicates work RaccoonBot does itself from the manifest,
# and one unreachable bottle of somebody else's turned a successful install into
# a screenful of red "failed" lines.
#
# So a caller that knows the bottle says so, and then that is the only bottle
# touched. The app passes the bottle the user picked in it; RaccoonBot uses the
# one it defines; the scan remains only for a person running the script by hand
# with nothing selected.
#
# An invalid value is an error, never a fallback to scanning everything. Falling
# back would reintroduce exactly the behaviour this exists to prevent, and would
# do it silently, on the run where somebody was trying hardest to be specific.
pinned_bottle() {
  [ -n "${MGVF_BOTTLE:-}" ] || return 1
  printf '%s\n' "${MGVF_BOTTLE%/}"
}

# Validated here, at source time, and NOT inside pinned_bottle.
#
# find_bottles runs inside $(...) and inside process substitution, so an `exit`
# raised down there kills a subshell and nothing else: the first version of this
# answered `absent` with status 0 for a bottle path that does not exist, which is
# the worst possible reply -- a confident, wrong, quiet one. This file is sourced
# by the main shell, so a check at this level stops the run.
if [ -n "${MGVF_BOTTLE:-}" ] && [ ! -d "${MGVF_BOTTLE%/}/drive_c" ]; then
  echo "error: MGVF_BOTTLE is not a bottle: ${MGVF_BOTTLE%/}" >&2
  echo "       Expected a directory containing drive_c." >&2
  exit 1
fi

# A program driving these scripts must say which bottle. A person need not.
#
# Unset used to mean "scan", for everybody. That is fine for someone at a
# terminal with one bottle, and it is exactly how a launcher build that forgets
# to pass MGVF_BOTTLE fails: silently, by writing where it was never told to.
# MGVF_FRONTEND is already set by anything that drives these scripts from a
# user interface, so it is the honest test for "is a program asking".
if [ -n "${MGVF_FRONTEND:-}" ] && [ -z "${MGVF_BOTTLE:-}" ]; then
  echo "error: MGVF_FRONTEND=${MGVF_FRONTEND} is set but MGVF_BOTTLE is not." >&2
  echo "       A launcher must name the bottle it means. Scanning for one is" >&2
  echo "       how a KINGDOM HEARTS override ended up in the Battle.net bottle." >&2
  echo "       Pass MGVF_BOTTLE as an absolute path to the bottle directory," >&2
  echo "       the one holding drive_c. Note the name: MGVF_BOTTLES, plural," >&2
  echo "       is a different setting that ADDS a root to search." >&2
  exit 1
fi


engine_bottle_roots() {
  local a conf r
  for a in /Applications/*.app "$HOME"/Applications/*.app; do
    conf="$a/Contents/SharedSupport/CrossOver/etc/CrossOver.conf"
    [ -f "$conf" ] || continue
    r="$(grep -a '"CX_BOTTLE_PATH"' "$conf" 2>/dev/null | head -1 | cut -d'"' -f4)"
    [ -n "$r" ] || continue
    case "$r" in "~"*) r="$HOME${r#\~}" ;; esac
    printf '%s\n' "$r"
  done
}

bottle_roots() {
  local r seen=""
  # Read as lines, never word-split: every one of these paths contains
  # "Application Support", and an unquoted $(...) turns each into three
  # fragments that match no directory at all.
  {
    printf '%s\n' "${MGVF_BOTTLES:-}"
    defaults read com.codeweavers.CrossOver BottleDir 2>/dev/null || true
    printf '%s\n' "$HOME/Library/Application Support/CrossOver/Bottles"
    engine_bottle_roots
  } | while IFS= read -r r; do
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

# IS A WINESERVER ALIVE FOR THIS BOTTLE, answered without starting one.
#
# Every installer here reads the registry by asking reg.exe, and in a bottle
# where nothing runs, asking starts wine: wineserver, services.exe,
# winedevice.exe and plugplay.exe, which stay up for seconds after reg.exe has
# answered. Measured on 2026-09-14: RaccoonBot asks install-ng3-fix.sh --status
# on every start of the app, and a game launched about five seconds later joined
# the prefix that question had brought up. The launch lost its HID trace's
# winebus lines and its per-title registry write, and the live wineserver's
# environment still carried the status run's MGVF_EXE and MGVF_STATUS_ONLY. A
# status check that brings a bottle up changes the launch that follows it, so
# the installers answer from user.reg whenever no server holds the bottle.
#
# server_dir_of, held_by and has_server are copied unchanged from
# diagnostics/capture-hid-trace.sh, which says why each is written the way it
# is. In short: wine keeps one server directory per prefix, named after the
# prefix's device and inode in lowercase hex, and every process of that prefix
# holds files open in it, so lsof on it names that bottle's processes and no
# other bottle's. Checked here on 2026-09-14 against RaccoonBot's Steam bottle
# while a game ran in it: the derived directory was held by that bottle's
# wineserver, its services and the game.
server_dir_of() {  # server_dir_of <bottle dir>
  local dev ino
  read -r dev ino <<<"$(stat -f '%d %i' "$1" 2>/dev/null)"
  [ -n "${dev:-}" ] && [ -n "${ino:-}" ] || return 1
  printf '/private/tmp/.wine-%s/server-%x-%x\n' "$(id -u)" "$dev" "$ino"
}

# One "pid name" line per process holding the directory. -F prints a p line,
# a c line and then f lines for every open file; only the first two are wanted.
held_by() {  # held_by <server dir>
  [ -d "$1" ] || return 0
  /usr/sbin/lsof -Fpc +D "$1" 2>/dev/null |
    awk '/^p/ { pid = substr($0, 2) } /^c/ { print pid " " substr($0, 2) }'
}

# Matched on the prefix: support-bundle.sh records engines whose server binary
# is wineserver-x86 or wineserver-arm64, and an exact name would miss those.
has_server() {  # has_server <held_by output>
  /usr/bin/grep -Eq '^[0-9]+ wineserver' <<<"$1"
}

# 0 when a wineserver holds the bottle, 1 when none does, 2 when this cannot
# tell. A caller must never read 2 as 1: the one thing 1 is used for is trusting
# user.reg, and a live server can hold keys the file does not have yet.
#
# held_by's own status is ignored on purpose. lsof exits 1 when it finds nothing,
# and under pipefail that 1 is what held_by returns, so a bottle with no server
# would otherwise end a `set -e` caller rather than answer.
bottle_server_alive() {  # bottle_server_alive <bottle dir>
  local server held
  [ -x /usr/sbin/lsof ] || return 2
  server="$(server_dir_of "$1")" || return 2
  held="$(held_by "$server")" || true
  if has_server "$held"; then return 0; fi
  return 1
}

# WHETHER HKEY_CURRENT_USER HOLDS A VALUE, read from the bottle's user.reg.
#
# Only an answer while bottle_server_alive says 1. A live server holds the
# registry in memory and saves the file on a period of its own (save_period in
# wine's server/registry.c), so the file can lag what reg.exe would say. With no
# server the file is the whole registry. Wine saves it by writing a temporary
# file and renaming it over user.reg (save_branch, same file), so a reader never
# meets one half written.
#
# The key is given as reg.exe takes it under HKCU, with single backslashes, and
# the value by its name. Both are compared the way wine writes them. A key is a
# header line, [Software\\Wine\\AppDefaults\\<exe>\\DllOverrides] followed by a
# timestamp; a value is a line beginning "<name>"=. dump_strW in server/unicode.c
# puts a backslash before every backslash, before [ and ] in a key name, and
# before " in a value name. Registry names are case-insensitive, so the
# comparison is too. Outside printable ASCII, dump_strW writes escapes that this
# does not reproduce, so such a name answers 2 instead of a false absent.
#
# 0 present, 1 absent, 2 the file cannot answer: missing, unreadable, or a name
# this does not encode.
user_reg_has_value() {  # user_reg_has_value <bottle dir> <key under HKCU> <value name>
  local reg="$1/user.reg" rc=0
  [ -f "$reg" ] && [ -r "$reg" ] || return 2
  MGVF_REG_KEY="$2" MGVF_REG_VALUE="$3" LC_ALL=C /usr/bin/awk '
    function enc(s, specials,   out, i, c) {
      out = ""
      for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c < " " || c > "~") { bad = 1; return "" }
        if (index(specials, c)) out = out "\\"
        out = out c
      }
      return out
    }
    BEGIN {
      bad = 0; found = 0; inside = 0
      key = "[" tolower(enc(ENVIRON["MGVF_REG_KEY"], "\\[]")) "]"
      val = "\"" tolower(enc(ENVIRON["MGVF_REG_VALUE"], "\\\"")) "\"="
      if (bad) exit
    }
    /^\[/ {
      line = tolower($0)
      inside = (line == key || substr(line, 1, length(key) + 1) == key " ")
      next
    }
    inside && substr(tolower($0), 1, length(val)) == val { found = 1; exit }
    END {
      if (bad) exit 3
      if (found) exit 0
      exit 1
    }
  ' "$reg" || rc=$?
  case "$rc" in 0|1) return "$rc" ;; esac
  return 2
}
