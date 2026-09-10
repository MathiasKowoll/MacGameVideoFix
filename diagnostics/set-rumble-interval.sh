#!/usr/bin/env bash
#
# How rarely winebus may tell a DualSense's motors anything, in milliseconds.
#
#     diagnostics/set-rumble-interval.sh <ms|default> [bottle]
#     diagnostics/set-rumble-interval.sh                     -> report only
#
# mgvf-0010 reads XInputRumbleInterval under the pad's own key and uses it as a
# FLOOR on how often the link is used. The driver's own default is 8 ms.
#
# WHY A TOOL AND NOT A SETTING. Nothing in any interface shows this value, and
# nothing resets it -- the launcher writes the other values under that key per
# title and never this one. So a number put here by hand outlives the reason for
# it. That happened: 33 was measured for BLUETOOTH, where a write is expensive,
# and then followed the pad onto a cable and throttled it to 28 Hz for weeks
# while the owner reported the rumble felt weak.
#
# "default" REMOVES the value rather than writing 8. It is not the same thing: a
# build that ever changes its mind about the right floor will then be obeyed.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
MS="${1:-}"
BOTTLE="${2:-Steam}"
REG="$HOME/Library/Application Support/RaccoonBot/CXPBottles/$BOTTLE/system.reg"

[ -f "$REG" ] || { echo "no system.reg at $REG"; exit 1; }

if [ -z "$MS" ]; then
  # An ABSENT value is the normal state and is not an error: the driver's own
  # default applies. Reporting it as a failure was this tool's first bug.
  found=0
  while IFS= read -r l; do
    found=1
    printf '  %s   (%d ms)\n' "$l" "$((16#${l##*dword:}))"
  done < <(/usr/bin/grep -n "XInputRumbleInterval" "$REG" || true)
  [ "$found" = 1 ] || echo "  not set for any device -- winebus uses its own default of 8 ms"
  exit 0
fi

# The bottle reads these as the device arrives, and writing under a running
# wineserver races its own copy of the registry back over the file.
if pgrep -f wineserver >/dev/null 2>&1; then
  echo "############################################################"
  echo "##  NOTHING WAS CHANGED.  A bottle is running."
  echo "##  wineserver holds its own copy of the registry and would"
  echo "##  write it back over this change when it exits."
  echo "##  Close Steam AND the game, wait for wineserver to go,"
  echo "##  then run this again."
  echo "############################################################"
  exit 1
fi

cp "$REG" "$REG.before-interval"

# "default" REMOVES the value rather than writing a number. That is not the same
# thing: winebus falls back to DUALSENSE_HAPTICS_MIN_INTERVAL_MS, which is 8, and
# a build that ever changes its mind about the right floor will be obeyed. A
# number written here is a number nobody revisits -- 33 was measured for
# BLUETOOTH, followed the pad onto a cable, and throttled it to 28 Hz for weeks
# with nothing in any interface showing it.
if [ "$MS" = "default" ]; then
  python3 - "$REG" <<'PY2'
import sys, re
reg = sys.argv[1]
s = open(reg, encoding='utf-8', errors='surrogateescape').read()
s, n = re.subn(r'"XInputRumbleInterval"=dword:[0-9a-fA-F]{8}\n', '', s)
open(reg, 'w', encoding='utf-8', errors='surrogateescape').write(s)
print(f"  removed XInputRumbleInterval from {n} device key(s) -- winebus's own default (8 ms) applies")
PY2
  echo "  the old registry is beside it as system.reg.before-interval"
echo
echo "  --- what the bottle now carries ---"
if /usr/bin/grep -q "XInputRumbleInterval" "$REG"; then
  /usr/bin/grep -n "XInputRumbleInterval" "$REG" | while IFS= read -r l; do
    printf '  %s   (%d ms)\n' "$l" "$((16#${l##*dword:}))"
  done
else
  echo "  not set for any device -- winebus uses its own default of 8 ms"
fi
  exit 0
fi

python3 - "$REG" "$MS" <<'PYEOF'
import sys, re
reg, ms = sys.argv[1], int(sys.argv[2])
lines = open(reg, encoding='utf-8', errors='surrogateescape').read().split('\n')
want = f'"XInputRumbleInterval"=dword:{ms:08x}'

# REPLACE where it exists, INSERT where the device key exists without it. Only
# replacing was this tool's second bug: once "default" had removed the value,
# asking for a number silently wrote nothing and reported "0 device key(s)",
# and the run that followed measured the driver's default while its owner
# believed it was measuring 200 ms.
sect = re.compile(r'^\[System\\\\CurrentControlSet\\\\Services\\\\winebus\\\\Devices\\\\', re.I)
out, i, changed, added = [], 0, 0, 0
while i < len(lines):
    line = lines[i]
    if not sect.match(line):
        out.append(line); i += 1; continue
    # one device section: header, then values until a blank line or the next [
    body, j = [], i + 1
    while j < len(lines) and lines[j].strip() and not lines[j].startswith('['):
        body.append(lines[j]); j += 1
    hit = False
    for k, b in enumerate(body):
        if b.startswith('"XInputRumbleInterval"='):
            body[k] = want; hit = True; changed += 1
    if not hit:
        # keep the file's alphabetical order: after XInputRumble if it is there
        at = len(body)
        for k, b in enumerate(body):
            if b.startswith('"') and b.split('"')[1].lower() > 'xinputrumbleinterval':
                at = k; break
        body.insert(at, want); added += 1
    out.append(line); out.extend(body); i = j
open(reg, 'w', encoding='utf-8', errors='surrogateescape').write('\n'.join(out))
print(f"  XInputRumbleInterval = {ms} ms: {changed} replaced, {added} added")
if not changed and not added:
    print("  NO DEVICE KEY FOUND. winebus only READS this value, it never creates")
    print("  the key -- the launcher does, the first time it configures a pad.")
PYEOF
echo "  the old registry is beside it as system.reg.before-interval"
echo
echo "  --- what the bottle now carries ---"
if /usr/bin/grep -q "XInputRumbleInterval" "$REG"; then
  /usr/bin/grep -n "XInputRumbleInterval" "$REG" | while IFS= read -r l; do
    printf '  %s   (%d ms)\n' "$l" "$((16#${l##*dword:}))"
  done
else
  echo "  not set for any device -- winebus uses its own default of 8 ms"
fi
