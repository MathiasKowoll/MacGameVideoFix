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

. "$(cd "$(dirname "$0")/../runtime" && pwd)/bottles.sh"

APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app" >&2; exit 1; }

B="$(find_bottle_dir "$BOTTLE")" || { echo "error: no bottle named $BOTTLE in any root" >&2; exit 1; }
[ -d "$B" ] || { echo "error: no bottle named $BOTTLE" >&2; exit 1; }

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
