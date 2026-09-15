#!/usr/bin/env bash
#
# Set or remove any per-device winebus option in a bottle's registry.
#
#     diagnostics/set-pad-option.sh <name> <value|remove> [bottle]
#     diagnostics/set-pad-option.sh                        -> list what is set
#
# The general form of set-rumble-interval.sh, which stays because the interval
# has a story worth carrying. Values winebus reads under a pad's own key:
# Hidraw, SeizeDevice, UsbEmulation, ProductId, ForwardFeatureWrites,
# VibrationMode, VibrationGain, XInputRumble, XInputRumbleInterval,
# XInputRumbleDryRun, XInputRumbleDeadband, XInputRumbleRide,
# XInputRumbleHapticPath, XInputRumbleMotorPower, XInputRumbleRideBand,
# XInputRumbleStubButton, LightbarColour, PlayerLights, LightbarRelease,
# IdlePowerOffMinutes.
#
# IdlePowerOffMinutes (mgvf-0033) is minutes, in decimal: a DualSense on
# Bluetooth that this bottle holds is asked to turn itself off after that long
# with no stick, trigger or button input. Absent: 20. 0: never. Anything else is
# clamped to 5..240. It is read as the bottle's wine starts, like SeizeDevice.
#
# XInputRumbleMotorPower carries a BYTE and not a switch: 0 to 255 is written
# into the motor power field with the bit that claims it, and 256 or above -- or
# the value removed -- leaves the field alone, which is what every build before
# mgvf-0023 did. 0 is what Sony's own library sends on every packet.
#
# The three light values (mgvf-0031) carry a marker, so give them in hex:
# LightbarColour 0x01RRGGBB (0x01000000 is the lightbar off), PlayerLights
# 0x100 | pattern (0x100 off, 0x104 / 0x10A / 0x115 / 0x11B players 1 to 4),
# anything without the marker meaning "as the client asks". LightbarRelease is
# 0, 1 or 2 and is a measurement knob only.
#
# The launcher rewrites six of those per title, writes the two light values per
# title as well (0 when the game decides), and never touches the rest -- so
# anything set here that is not one of its eight, LightbarRelease included,
# outlives every launch. That is how a Bluetooth-measured interval once followed
# a pad onto a cable for weeks.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
NAME="${1:-}"; VAL="${2:-}"; BOTTLE="${3:-Steam}"
REG="$HOME/Library/Application Support/RaccoonBot/CXPBottles/$BOTTLE/system.reg"
[ -f "$REG" ] || { echo "no system.reg at $REG"; exit 1; }

# Read INSIDE the winebus device sections only. A bare grep for these names
# finds a "ProductId" string elsewhere in system.reg that has nothing to do with
# a pad, and then tries to read it as hex.
report() {
  echo "  --- what the bottle carries under the pad keys ---"
  python3 - "$REG" <<'PYR'
import sys, re
lines = open(sys.argv[1], encoding='utf-8', errors='surrogateescape').read().split('\n')
sect = re.compile(r'^\[System\\\\CurrentControlSet\\\\Services\\\\winebus\\\\Devices\\\\(.*)\]', re.I)
i = 0
while i < len(lines):
    m = sect.match(lines[i])
    if not m: i += 1; continue
    print(f"    {m.group(1)}")
    j = i + 1
    while j < len(lines) and lines[j].strip() and not lines[j].startswith('['):
        v = re.match(r'"([^"]+)"=dword:([0-9a-fA-F]{8})', lines[j])
        if v: print(f"      {v.group(1):<24s} {int(v.group(2), 16)}")
        j += 1
    i = j
PYR
}
[ -z "$NAME" ] && { report; exit 0; }

if pgrep -f wineserver >/dev/null 2>&1; then
  echo "############################################################"
  echo "##  NOTHING WAS CHANGED.  A bottle is running -- wineserver"
  echo "##  would write its own registry back over this. Close Steam"
  echo "##  AND the game, wait for wineserver to go, then retry."
  echo "############################################################"
  exit 1
fi

cp "$REG" "$REG.before-option"
python3 - "$REG" "$NAME" "$VAL" <<'PYEOF'
import sys, re
reg, name, val = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(reg, encoding='utf-8', errors='surrogateescape').read().split('\n')
sect = re.compile(r'^\[System\\\\CurrentControlSet\\\\Services\\\\winebus\\\\Devices\\\\', re.I)
want = None if val == 'remove' else f'"{name}"=dword:{int(val, 0):08x}'
out, i, done = [], 0, 0
while i < len(lines):
    line = lines[i]
    if not sect.match(line):
        out.append(line); i += 1; continue
    body, j = [], i + 1
    while j < len(lines) and lines[j].strip() and not lines[j].startswith('['):
        body.append(lines[j]); j += 1
    body = [b for b in body if not b.startswith(f'"{name}"=')]
    if want:
        at = len(body)
        for k, b in enumerate(body):
            if b.startswith('"') and b.split('"')[1].lower() > name.lower(): at = k; break
        body.insert(at, want)
    done += 1
    out.append(line); out.extend(body); i = j
open(reg, 'w', encoding='utf-8', errors='surrogateescape').write('\n'.join(out))
print(f"  {name} = {val} in {done} device key(s)")
if not done: print("  NO DEVICE KEY FOUND -- the launcher creates it the first time it configures a pad.")
PYEOF
echo
report
