#!/usr/bin/env bash
#
# Keep everything winebus says about a controller, for one session.
#
#     diagnostics/capture-hid-trace.sh <bottle> [engine.app name]
#
# WHY THIS IS NOT launch-and-capture.sh. That one keeps a game's own stderr and
# deliberately leaves the debug channels alone. This one is for the pad: the
# traffic that answers a controller question comes from winebus, which lives in
# winedevice.exe -- a process wineserver starts when the BOTTLE comes up, not
# when the game does. So the channel has to be set before that happens, and a
# bottle that is already running has a winedevice without it.
#
# CX_DEBUGMSG AND NOT WINEDEBUG. This cost a whole session before it was written
# down. CrossOver's bin/wine is a Perl script that builds the environment of
# everything it starts, and it feeds WINEDEBUG from its own CX_DEBUGMSG:
#
#     $ENV{WINEDEBUG} = $opt_debugmsg if (defined $opt_debugmsg);   # bin/wine
#
# A WINEDEBUG set in the shell never reaches the wineserver it forks, so it
# never reaches winedevice. What that failure looks like is a log full of msync
# and MoltenVK lines and not one line of trace:hid -- output flowing, channel
# off, and every count in the reader reading as "the pad did nothing".
#
# -all FIRST, then +hid. Asking for "+hid" alone leaves unwind, module, process,
# seh and loaddll on as well: 1.6 million lines in under two minutes, a third of
# them nothing to do with the pad, and the game crawling. A session that should
# have reached a rumble then does not get there, so the trace changes the thing
# it is measuring.
#
# Read what comes out with diagnostics/read-hid-trace.sh.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

BOTTLE="${1:?usage: capture-hid-trace.sh <bottle name or full path> [engine .app name]}"
ENGINE="${2:-CrossOver}"

APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app" >&2; exit 1; }

# THE ENGINE DECIDES WHICH BOTTLE, not the search. --bottle becomes CX_BOTTLE,
# which is a NAME, and the root it is looked up in is the CX_BOTTLE_PATH that
# this engine declares in its own etc/CrossOver.conf. So a name that exists in
# two roots is not ambiguous here at all: only one of them is the one this
# engine can open. Asking find_bottle_dir first made the tool refuse to start
# over an ambiguity the engine does not have.
BOTTLE="$(basename "$BOTTLE")"
CONF="$APP/Contents/SharedSupport/CrossOver/etc/CrossOver.conf"
ROOT="$(/usr/bin/grep -a '"CX_BOTTLE_PATH"' "$CONF" 2>/dev/null | head -1 | cut -d'"' -f4)"
case "$ROOT" in "~"*) ROOT="$HOME${ROOT#\~}" ;; esac
[ -n "$ROOT" ] || ROOT="$HOME/Library/Application Support/CrossOver/Bottles/"
B="${ROOT%/}/$BOTTLE"
[ -d "$B" ] || {
  echo "error: $ENGINE has no bottle called $BOTTLE" >&2
  echo "       it opens bottles in: ${ROOT%/}" >&2
  echo "       which holds:" >&2
  ls -1 "${ROOT%/}" 2>/dev/null | sed 's/^/         /' >&2
  exit 1
}
echo "root   : ${ROOT%/}   (declared by $ENGINE itself)"

OUT="$HOME/Desktop/hid-$(date +%H%M%S).log"

# A wine process from an earlier run keeps its own file descriptor AND its own
# offset into whatever log it was writing. Truncating that file and letting it
# carry on leaves a hole: hundreds of megabytes of NUL bytes followed by real
# lines, which greps as an empty trace. That destroyed one session's data here.
if /usr/sbin/lsof -- "$HOME/Desktop"/hid-*.log 2>/dev/null | grep -q .; then
  echo "error: something still has an old hid-*.log open -- a wine process from" >&2
  echo "an earlier run is alive. Close it, or this log will be a hole of zeros:" >&2
  /usr/sbin/lsof -- "$HOME/Desktop"/hid-*.log 2>/dev/null | sed 's/^/  /' >&2
  exit 1
fi
[ -e "$OUT" ] && { echo "error: $OUT already exists -- refusing to truncate it" >&2; exit 1; }

# A BOTTLE THAT IS ALREADY UP. The header says this and nothing checked it. On
# 2026-09-14 it cost a session, not with this script but with RaccoonBot's
# per-game HID trace, which writes the same kind of log from its own launch.
# That launch joined a bottle that a wine command with CX_DEBUGMSG=-all and
# stderr on /dev/null had brought up five seconds earlier, and its log held
# Steam's and the game's hid.dll lines and not one line from winebus. Nothing in
# that is particular to RaccoonBot's launch: this script's launch would join a
# running bottle the same way. That is how wine is built, not bad luck: a child
# inherits unix fd 2 from whatever started it (dlls/ntdll/unix/process.c passes
# fd 2 along unconditionally) and takes its debug channels from that starter's
# environment. So the winedevice.exe of a running bottle writes wherever its
# starter's stderr went, with its starter's channels, whatever this script sets.
#
# Wine keeps one server directory per prefix, named after the prefix's device
# and inode in lowercase hex, and every process of that prefix holds files open
# in it. RaccoonBot's BottleProcesses.serverDirectory derives the same path, and
# lsof on it names that bottle's processes and no other bottle's.
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

# What wine runs for its own sake, name for name as RaccoonBot counts it: the
# union of BottleProcesses.wineFurniture and MGVFCoordinator.Running.furniture,
# so the two projects judge a bottle the same way. The second list is where
# reg.exe, regedit.exe and cmd.exe come from. In this repository the fix
# installers run reg.exe through wine, and regedit.exe is run by
# diagnostics/registry/apply-webm-handler.sh. Spaces at both ends so a name is
# matched whole, never as part of a longer one.
WINE_OWN=" wineserver winewrapper.exe services.exe winedevice.exe plugplay.exe"
WINE_OWN+=" rpcss.exe explorer.exe svchost.exe conhost.exe start.exe wineboot.exe"
WINE_OWN+=" rundll32.exe tabtip.exe winemenubuilder.exe reg.exe regedit.exe"
WINE_OWN+=" cmd.exe "

# The names in a held_by list that are not wine's own, one per line.
not_wines_own() {  # not_wines_own <held_by output>
  local pid name lower
  while read -r pid name; do
    [ -n "${pid:-}" ] || continue
    lower="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
    case "$lower" in wineserver*) continue ;; esac
    case "$WINE_OWN" in *" $lower "*) continue ;; esac
    printf '%s\n' "$name"
  done <<<"$1"
}

# Refuses a bottle whose wineserver is alive, after giving one that holds only
# wine's own processes 20 s to go by itself. Those are worth waiting for:
# RaccoonBot's PatchAll.swift records that a reg.exe run through wine leaves a
# wineserver and its services up for a few seconds afterwards. None of these
# names is a game or a launcher, but one can be the first process of a launch
# that is only starting: the first wine process of RaccoonBot's traced launch on
# 2026-09-14 was winewrapper.exe. Such a launch cannot slip through. Either its
# own executable shows during the wait, which ends the wait with the same
# refusal, or the wait runs out with the server still up, which refuses too.
# Anything else in the bottle is refused at once, since waiting would only delay
# that refusal. Nothing here ends a process: what runs in the bottle may be a
# game, and closing it is the owner's decision.
bottle_must_be_down() {  # bottle_must_be_down <bottle dir>
  local server held deadline
  server="$(server_dir_of "$1")" || {
    echo "error: cannot read the device and inode of $1" >&2
    return 1
  }
  held="$(held_by "$server")"
  has_server "$held" || return 0

  if [ -z "$(not_wines_own "$held")" ]; then
    echo "waiting up to 20 s for this bottle's wineserver to exit (only wine's own processes are in it)"
    deadline=$((SECONDS + 20))
    while has_server "$held" && [ -z "$(not_wines_own "$held")" ] && [ "$SECONDS" -lt "$deadline" ]; do
      sleep 1
      held="$(held_by "$server")"
    done
    has_server "$held" || return 0
  fi

  echo "error: this bottle is already running:" >&2
  awk '{ pid = $1; sub(/^[0-9]+ /, ""); print "  " pid "  " $0 }' <<<"$held" >&2
  echo "Its winedevice.exe, where winebus lives, keeps the debug channels and the" >&2
  echo "stderr of whatever brought the bottle up -- not this script's +hid, and not" >&2
  echo "this log -- so the log would carry no winebus lines. Close what runs in the" >&2
  echo "bottle and start again." >&2
  return 1
}

bottle_must_be_down "$B" || exit 1

echo "engine : $ENGINE"
echo "bottle : $BOTTLE"
echo "log    : $OUT"
echo
echo "The bottle comes up with +hid. Connect the pad AFTER it is up if you can:"
echo "the per-device options are read as the device ARRIVES, and that line is"
echo "usually the first thing worth looking for. Then start the game and make"
echo "it do whatever is being investigated. Close everything, then Ctrl-C here."
echo

# +timestamp because every question asked of these logs has been about ordering:
# which arrived first, the pad or the option; and how far apart two writes are.
CX_DEBUGMSG="-all,+timestamp,+hid" \
  "$APP/Contents/SharedSupport/CrossOver/bin/wine" \
  --bottle "$BOTTLE" --cx-app 'C:\Program Files (x86)\Steam\steam.exe' >"$OUT" 2>&1
