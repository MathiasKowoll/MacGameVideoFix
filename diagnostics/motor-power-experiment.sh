#!/usr/bin/env bash
#
# Set up one run of the mgvf-0023 experiment, and refuse a run that would
# measure something else.
#
#     diagnostics/motor-power-experiment.sh a|b|c [bottle] [engine app]
#     diagnostics/motor-power-experiment.sh              -> what is set now
#
# THE QUESTION. Sony's library claims the motor power field on every packet it
# sends a DualSense -- flag1 bit 0x40, and common byte 36 -- and writes zero
# into it. This driver has never written that field at all. A field nobody
# claims keeps whatever it was last told, so if anything on this Mac ever
# reduced the pad's motors, every comparison of the two vibration paths made
# here was made through an attenuator neither path knew about.
#
#   a   the field unclaimed          -- what every build before mgvf-0023 sent
#   b   claimed, written 0x00        -- byte for byte what libScePad sends
#   c   claimed, written 0x77        -- the control: if the community's reading
#                                      of the field as power REDUCTION is right,
#                                      this must be felt as clearly weaker
#
# Run a, then b. If they feel the same, run c: it is the only thing that says
# whether the field does anything at all, and without it "no difference" and
# "the driver never reached the pad" are the same sentence.
#
# WHAT IT REFUSES, because each of these has cost a run in this project at
# least once: a bottle that is up (wineserver writes its own registry back), an
# engine without the patch (the option is read by a driver that has never heard
# of it and nothing says so anywhere), and a VibrationMode of 1, which rewrites
# the packet AFTER the stamp and turns the haptic path back into the legacy one
# -- so the run measures the path it was meant to be comparing away from.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
STEP="${1:-}"
BOTTLE="${2:-Steam}"
ENGINE="${3:-$HOME/Applications/Crossover_MGVF.app}"
REG="$HOME/Library/Application Support/RaccoonBot/CXPBottles/$BOTTLE/system.reg"
set_option() { "$HERE/set-pad-option.sh" "$1" "$2" "$BOTTLE" >/dev/null; }

[ -f "$REG" ] || { echo "no system.reg at $REG"; exit 1; }

if [ -z "$STEP" ]; then "$HERE/set-pad-option.sh" "" "" "$BOTTLE"; exit 0; fi

case "$STEP" in
  a) POWER=remove ;;
  b) POWER=0 ;;
  c) POWER=119 ;;   # 0x77
  *) echo "usage: $(basename "$0") a|b|c [bottle] [engine app]"; exit 1 ;;
esac

# 1. The bottle. set-pad-option.sh refuses this too; refusing here as well means
# nothing at all is half-applied when it does.
if pgrep -f wineserver >/dev/null 2>&1; then
  echo "############################################################"
  echo "##  NOTHING WAS CHANGED.  A bottle is running. Close the"
  echo "##  game AND Steam, wait for wineserver to go, then retry."
  echo "############################################################"
  exit 1
fi

# 2. The engine. Asked of the binary that reads the value, by the value's own
# name: a winebus without mgvf-0023 contains no such string, and one with it
# cannot not contain it. Nothing here trusts a version or a stamp.
BUS="$ENGINE/Contents/SharedSupport/CrossOver/lib/wine/x86_64-windows/winebus.sys"
[ -f "$BUS" ] || { echo "no winebus.sys in $ENGINE -- name the engine app as the third argument"; exit 1; }
if ! python3 -c "
import sys
sys.exit(0 if 'XInputRumbleMotorPower'.encode('utf-16-le') in open(sys.argv[1],'rb').read() else 1)" "$BUS"; then
  echo "############################################################"
  echo "##  NOTHING WAS CHANGED.  This engine's winebus does not"
  echo "##  carry mgvf-0023, so the option would be written into a"
  echo "##  registry nothing reads. Install the set first:"
  echo "##"
  echo "##    runtime/install-engine-controller.sh \"$ENGINE\""
  echo "############################################################"
  exit 1
fi

# 3. Everything the comparison needs, set rather than assumed. The four that are
# the same in every run are set every time: a run that differs from the last one
# in two things measures neither.
set_option XInputRumble 1
set_option XInputRumbleRide 1
set_option XInputRumbleHapticPath 1
set_option XInputRumbleMotorPower "$POWER"

echo "  step $STEP: the motor power field is $([ "$POWER" = remove ] && echo 'left unclaimed' || printf 'claimed and written %#04x' "$POWER")"
echo

# 4. And the one that the launcher rewrites at every launch, so it is reported
# rather than set: it comes from the menu, and the menu is where to change it.
MODE=$("$HERE/set-pad-option.sh" "" "" "$BOTTLE" | awk '/VibrationMode/ {print $2; exit}')
if [ "${MODE:-0}" != "0" ]; then
  echo "  ##  VibrationMode is $MODE, which rewrites the packet AFTER the stamp and"
  echo "  ##  turns the haptic path back into the legacy motors. This run would"
  echo "  ##  measure the legacy path in both steps. In RaccoonBot set the title's"
  echo "  ##  Vibration to \"As the game asks\" and launch again -- the launcher"
  echo "  ##  writes this value itself, so setting it here would not survive."
  echo
fi

"$HERE/set-pad-option.sh" "" "" "$BOTTLE"
echo
echo "  Now launch from RaccoonBot with the trace on, play the same stretch as the"
echo "  other steps, quit, and read it with:"
echo "      diagnostics/read-hid-trace.sh ~/Desktop/hid-<time>.log"
