#!/usr/bin/env bash
#
# Launch a bottle and keep everything it says.
#
# Wine, D3DMetal and DXMT write their diagnostics to stderr, which vanishes
# when a game is started from the CrossOver interface or from Steam. Started
# from a terminal, it can be kept -- and a message like
#
#     D3DMetal: ID3DDestructionNotifier ...
#
# is often the only statement of what is actually wrong, particularly with a
# game that hangs on a black screen and leaves no crash report.
#
#     diagnostics/launch-and-capture.sh Steam
#     diagnostics/launch-and-capture.sh Steam "CrossOver Preview"
#
# Everything is written to ~/Desktop/crossover-capture-<time>.log and shown as
# it happens. Close the game to finish.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

# No default. Bottle names belong to whoever made them -- ours are not a
# reasonable guess for anybody else's machine, and a default that happens to
# exist would capture the wrong one without saying so.
BOTTLE="${1:?usage: launch-and-capture.sh <bottle name or full path> [...]}"
ENGINE="${2:-CrossOver Preview}"

# The bottle by name, in whatever root holds it -- fixes are raised against
# Procyon now, whose bottles live under its own support folder rather than
# CrossOver's. Naming the root here would have meant this tool could not reach
# the environment the fixes are being developed in.
. "$(cd "$(dirname "$0")/../runtime" && pwd)/bottles.sh"

APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app" >&2; exit 1; }

B="$(find_bottle_dir "$BOTTLE")" || { echo "error: no bottle named $BOTTLE in any root" >&2; exit 1; }
[ -d "$B" ] || { echo "error: no bottle named $BOTTLE" >&2; exit 1; }

OUT="$HOME/Desktop/crossover-capture-$(date +%H%M%S).log"

echo "engine : $ENGINE"
echo "bottle : $BOTTLE  ($(grep -o '"CX_GRAPHICS_BACKEND" = "[a-z0-9]*"' "$B/cxbottle.conf" 2>/dev/null | cut -d'"' -f4))"
echo "log    : $OUT"
echo
echo "Start the game from Steam. Everything it says lands in that file."
echo "Close the game when you are done, then Ctrl-C here."
echo

# Launching Steam captures nothing, and it took a run to see why: Steam forks
# and returns, so the command finishes before the game starts and the game is a
# different process whose output goes nowhere. Two lines of msync and an empty
# file is what that looks like.
#
# So the game is launched directly, as a child of this terminal. Steam has to
# be running already -- most titles call SteamAPI_RestartAppIfNecessary, which
# finds the running client and carries on rather than relaunching.
#
# WINEDEBUG is left alone deliberately. Turning on channels floods the output
# and buries the one line that matters; the defaults already carry D3DMetal's
# and DXMT's own complaints.
if [ -n "${EXE:-}" ]; then
  echo "launching $EXE directly, with Steam expected to be already running"
  "$APP/Contents/SharedSupport/CrossOver/bin/wine" \
    --bottle "$BOTTLE" --cx-app "$EXE" 2>&1 | tee "$OUT"
else
  echo "no EXE given -- launching Steam, which will NOT capture the game's own"
  echo "output because Steam detaches. Set EXE to the game to capture it:"
  echo "  EXE='Z:\\path\\to\\game.exe' $0 $BOTTLE"
  "$APP/Contents/SharedSupport/CrossOver/bin/wine" \
    --bottle "$BOTTLE" --cx-app 'C:\Program Files (x86)\Steam\steam.exe' 2>&1 | tee "$OUT"
fi
