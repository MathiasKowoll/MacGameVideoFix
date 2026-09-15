#!/usr/bin/env bash
#
# Read a +hid trace for what it says about a Sony pad.
#
#     diagnostics/read-hid-trace.sh ~/Desktop/hid-114844.log
#
# Made by diagnostics/capture-hid-trace.sh. Every section is a question that has
# cost a session at least once:
#
#   how winebus created the pad     -- bus_type 1 is USB, 2 is Bluetooth, and an
#                                      emulated pad reports USB while being on
#                                      Bluetooth, so this is where to start
#   whether the options were read   -- mgvf-0009 and mgvf-0016; absent means the
#                                      driver never saw the registry, which is
#                                      not the same as the rewrite doing nothing
#   what was rewritten              -- id 49 is a Bluetooth 0x31, id 2 a wired 0x02
#   what XInput asked and what left -- mgvf-0010; the second half is the only
#                                      place the scaled bytes can be seen at all
#   what a write costs              -- in microseconds. Cross-check it against
#                                      the loop period below before quoting it
#   whether writing disturbs input  -- the two rows must be compared, not read
#   what the title itself wrote     -- a title whose rumble is XInput writes
#                                      output reports without a motor in them
#   the motor power field           -- mgvf-0023; flag1 0x40 on every row or the
#                                      option never reached the driver at all
#   the lights                      -- mgvf-0031; what clients asked of the
#                                      lightbar and player lights, and whether
#                                      the driver read and rewrote them
#   whether SDL left the pad alone  -- mgvf-0032's hint line
#   whether an idle pad was turned  -- mgvf-0033; the limit at start, the
#   off                                request (a FIXME, so it is there without
#                                      +hid too), and input after it
#
# IT REFUSES AN EMPTY TRACE. A log with no trace:hid lines reads exactly like a
# pad that did nothing: every count comes out zero and looks like a finding.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later
set -uo pipefail
L="${1:?usage: read-hid.sh <log>}"
say() { printf '\n== %s\n' "$1"; }

# First: is this a trace at all? Every other number below is meaningless if the
# channel was never on, and an empty +hid log reads exactly like a pad that did
# nothing -- which cost a run once.
n=$(/usr/bin/grep -c "trace:hid" "$L")
if [ "$n" -eq 0 ]; then
  echo "THIS LOG HAS NO HID TRACE AT ALL ($(wc -l <"$L") lines, 0 of them trace:hid)."
  echo "The channel was not on. Nothing below would mean anything -- use"
  echo "CX_DEBUGMSG, not WINEDEBUG, and start with the bottle closed."
  exit 1
fi
echo "$n trace:hid lines"

say "the pad, as winebus created it  (bus_type 1 = USB, 2 = Bluetooth)"
/usr/bin/grep "bus_create_hid_device desc {vid 054c" "$L" | tail -5

say "were the vibration options read for it?  (mgvf-0016's own line)"
/usr/bin/grep "dualsense_vibration_options" "$L" | tail -5 || echo "  none -- the options were not read at all"

say "did anything get rewritten?  (id 2 = the cable, id 49 = Bluetooth)"
/usr/bin/grep -c "rewrote the vibration in output report" "$L"
/usr/bin/grep "rewrote the vibration in output report" "$L" | head -3

say "is the title rumbling through XInput instead?"
/usr/bin/grep -c "telling the motors rumble" "$L"
/usr/bin/grep "telling the motors rumble" "$L" | head -3

say "who carried the motors  (mgvf-0020: a change may ride the client's own packet)"
printf '  rode the client 0x31 : '; /usr/bin/grep -ac "rode the client's 0x31" "$L"
printf '  thread wrote its own : '; /usr/bin/grep -ac "telling the motors rumble" "$L"
printf '  held under the band  : '; /usr/bin/grep -ac "under the band of" "$L"
printf '  dry run, not stamped : '; /usr/bin/grep -ac "would have ridden" "$L"
echo "  (with XInputRumbleRide on and a title writing its own 0x31, the first line"
echo "   should carry nearly everything and the second nearly nothing: no packet"
echo "   of ours on the link is the whole point)"

say "what XInput actually sent to the motors  (asked -> carried, after the gain)"
paste -d'|' <(/usr/bin/grep -o "telling the motors rumble [0-9]*, buzz [0-9]*" "$L") \
            <(/usr/bin/grep -o "the packet carries motors [0-9]*/[0-9]*, flag0 [^,]*, [0-9]* bytes, at gain [0-9]*%" "$L") \
  2>/dev/null | sort | uniq -c | sort -rn | head -8
echo "  (a carried value of 255 means the gain saturated: the pad was already"
echo "   being asked for everything, and a larger percentage cannot add to it)"

say "what a write to the pad actually costs, in microseconds"
/usr/bin/grep -ao "the write took [0-9]* us" "$L" | /usr/bin/grep -o "[0-9]*" | python3 -c "
import sys
v=sorted(int(x) for x in sys.stdin)
if not v: print('  no writes measured'); raise SystemExit
n=len(v)
print(f'  {n} writes   median {v[n//2]} us   p90 {v[int(n*.9)]} us   p99 {v[int(n*.99)]} us   max {v[-1]} us')
print(f'  under 100us: {sum(1 for x in v if x<100)*100//n}%   over 1ms: {sum(1 for x in v if x>=1000)*100//n}%   over 5ms: {sum(1 for x in v if x>=5000)*100//n}%')
"
echo "  (a cable on this build measured 1-2 ms per write; this is the like-for-like"
echo "   number the Bluetooth side has never had)"

say "the period of the motor loop -- the cross-check on the number above"
/usr/bin/grep -ao "the link is used at most every [0-9]* ms" "$L" | tail -1 | sed 's/^/  configured floor: /'
/usr/bin/grep -ao "^[0-9]*\\.[0-9]*:.*telling the motors" "$L" | /usr/bin/grep -o "^[0-9]*\\.[0-9]*" | python3 -c "
import sys
t=[float(x) for x in sys.stdin]
d=sorted(round((t[i+1]-t[i])*1000) for i in range(len(t)-1) if 0<=(t[i+1]-t[i])*1000<2000)
if not d: print('  no loop to measure'); raise SystemExit
print(f'  loop period: median {d[len(d)//2]} ms   p90 {d[int(len(d)*.9)]} ms')
print('  The period is the write plus the floor. If the median period minus the')
print('  floor does not match the median write above, one of the two is lying.')
"

say "does writing the motors disturb the pad's INPUT stream?"
echo "  (the hypothesis for why Bluetooth costs frames and a cable does not:"
echo "   input and output share one link, so writing perturbs what the game reads)"
python3 - "$L" <<'PY2'
import sys, re, bisect
ts_in, ts_out = [], []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'(\d+\.\d+):', line)
    if not m: continue
    t = float(m.group(1))
    if 'process_hid_report' in line: ts_in.append(t)
    elif 'telling the motors rumble' in line: ts_out.append(t)
if len(ts_in) < 100 or not ts_out:
    print("  not enough to say: %d input reports, %d motor writes" % (len(ts_in), len(ts_out)))
    raise SystemExit
ts_out.sort()
def near(t, w=0.050):
    i = bisect.bisect_left(ts_out, t - w)
    return i < len(ts_out) and ts_out[i] <= t + w
gaps_busy, gaps_quiet = [], []
for a, b in zip(ts_in, ts_in[1:]):
    g = (b - a) * 1000
    if g < 0 or g > 200: continue
    (gaps_busy if near(a) else gaps_quiet).append(g)
def stat(name, g):
    if len(g) < 20: print(f"  {name:22s} too few samples ({len(g)})"); return
    g = sorted(g); n = len(g)
    print(f"  {name:22s} n={n:<7d} median {g[n//2]:6.2f} ms   p95 {g[int(n*.95)]:6.2f} ms   max {g[-1]:7.2f} ms")
stat("while writing motors", gaps_busy)
stat("while quiet", gaps_quiet)
print("  If the two rows match, our writes are not disturbing the input stream")
print("  and the frame cost is somewhere else. If p95 and max blow up on the")
print("  first row, that is the mechanism.")
PY2

say "what the title wrote ITSELF over Bluetooth  (report 0x31, the native path)"
python3 - "$L" <<'PY3'
import sys, re, collections
rows=[]; cur=None
for line in open(sys.argv[1], errors='replace'):
    m=re.match(r'(\d+\.\d+):', line)
    if not m: continue
    t=float(m.group(1))
    if 'write output report id 49' in line: cur=(t,{}); rows.append(cur); continue
    if cur is not None:
        h=re.search(r'hid_internal_dispatch (\d{8})  ((?:[0-9a-f]{2} ?)+)', line)
        if h:
            off=int(h.group(1),16)
            for i,v in enumerate(h.group(2).split()): cur[1][off+i]=int(v,16)
        else: cur=None
if not rows:
    print("  none -- either the pad is wired, or the title drives it through XInput")
else:
    span=rows[-1][0]-rows[0][0] or 1
    mot=[(t,b) for t,b in rows if b.get(5,0) or b.get(6,0)]
    print(f"  {len(rows)} writes over {span:.0f} s = {len(rows)/span:.1f}/s average; {len(mot)} carry a motor")
    if len(mot) > 2:
        ts=[t for t,_ in mot]
        g=sorted(round((ts[i+1]-ts[i])*1000) for i in range(len(ts)-1) if 0<(ts[i+1]-ts[i])*1000<5000)
        peak=max(sum(1 for x in ts if t<=x<t+1) for t in ts)
        print(f"  gap between motor writes: median {g[len(g)//2]} ms, p10 {g[len(g)//10]} ms")
        print(f"  PEAK: {peak} motor writes in one second  (the link carries about 65)")
        d=[max(abs(mot[i+1][1].get(5,0)-mot[i][1].get(5,0)), abs(mot[i+1][1].get(6,0)-mot[i][1].get(6,0))) for i in range(len(mot)-1)]
        if d:
            tiny=sum(1 for x in d if 0<x<4)
            print(f"  of its own changes, {100*tiny//len(d)}% move a motor by fewer than 4 of 255")
            print("  (ours was 76% -- if a native title is far below that, it is writing")
            print("   deliberately where we are writing on every twitch)")
PY3

say "mgvf-0031: the lights  (client packets with flag1 0x04 or 0x10, as the client wrote them)"
printf '  lights will be set          : '; /usr/bin/grep -ac "lights will be set" "$L"
printf "  set the client's lights     : "; /usr/bin/grep -ac "set the client's lights" "$L"
printf '  released the lightbar       : '; /usr/bin/grep -ac "released the lightbar before the first colour" "$L"
/usr/bin/grep -a "lights will be set" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "released the lightbar before the first colour" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "lightbar release could not be written\|carrying the lightbar release could not be written" "$L" | tail -2 | sed 's/^/  /'
python3 - "$L" <<'PY5'
import sys, re, collections
# Offsets into the report as dumped, report id at 0. Bluetooth 0x31: the common
# block starts at 3. Wired and emulated 0x02: at 1. flag1 is common[1], flag2
# common[38], the lightbar setup common[41], player common[43], RGB common[44..46].
rows = collections.Counter(); cur = None; kind = None
def flush():
    if cur is None: return
    base = 3 if kind == 49 else 1
    if cur.get(0) != kind or len(cur) < base + 47: return
    f1 = cur.get(base + 1, 0)
    if not f1 & 0x14: return
    rgb = "%02x%02x%02x" % (cur.get(base + 44, 0), cur.get(base + 45, 0), cur.get(base + 46, 0))
    rows[("0x31" if kind == 49 else "0x02", f1, cur.get(base + 38, 0), cur.get(base + 41, 0), cur.get(base + 43, 0), rgb)] += 1
for line in open(sys.argv[1], errors='replace'):
    m = re.search(r'write output report id (49|2) length', line)
    if m:
        flush(); cur = {}; kind = int(m.group(1)); continue
    if cur is not None:
        h = re.search(r'hid_internal_dispatch (\d{8})  ((?:[0-9a-f]{2} ?)+)', line)
        if h:
            off = int(h.group(1), 16)
            for i, v in enumerate(h.group(2).split()): cur[off + i] = int(v, 16)
            continue
        flush(); cur = None
flush()
if not rows:
    print("  no client packet asked for a light -- nothing for mgvf-0031 to rewrite")
else:
    print(f"  {sum(rows.values())} client packets carry a light:")
    for (rid, f1, f2, setup, player, rgb), c in rows.most_common(12):
        print(f"    {rid}  flag1 {f1:#04x}  flag2 {f2:#04x}  setup {setup:#04x}  player {player:#04x}  rgb {rgb}   x{c}")
    print("  These are the client's bytes, before any rewrite. flag2 0x02 with setup 0x02")
    print("  is a lightbar release; Steam's session in hid-203611 carried none.")
PY5

say "mgvf-0032: did SDL leave the hidraw pads alone?"
/usr/bin/grep -a "SDL will not open" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "SDL may still open\|too many hidraw devices for SDL" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -aq "SDL will not open\|SDL may still open" "$L" || echo "  no hint line -- no key with Hidraw set, or a build before mgvf-0032"

say "mgvf-0033: was an idle pad asked to turn itself off?"
printf '  armed (limit at start)      : '; /usr/bin/grep -ac "asking it to turn itself off after" "$L"
printf '  requests sent               : '; /usr/bin/grep -ac "asking it to turn itself off (feature 0x08)" "$L"
printf '  input again, no removal     : '; /usr/bin/grep -ac "the pad did not turn off" "$L"
/usr/bin/grep -a "IdlePowerOffMinutes" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "asking it to turn itself off after" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "asking it to turn itself off (feature 0x08)" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -a "the pad did not turn off" "$L" | tail -2 | sed 's/^/  /'
/usr/bin/grep -aq "asking it to turn itself off after" "$L" || echo "  not armed -- no seized DualSense on Bluetooth was started, IdlePowerOffMinutes is 0, or a build before mgvf-0033"

say "mgvf-0023: did the motor power field go out, and with what in it?"
python3 - "$L" <<'PY4'
import sys, re, collections
ride = collections.Counter(); thread = collections.Counter(); native = []
cur=None
for line in open(sys.argv[1], errors='replace'):
    m = re.search(r"rode the client's 0x31 with motors (\d+)/(\d+), flag0 (0x[0-9a-f]+|0), flag1 (0x[0-9a-f]+|0), flag2 (0x[0-9a-f]+|0), power (0x[0-9a-f]+|0)", line)
    if m:
        ride[(m.group(3), m.group(4), m.group(5), m.group(6))] += 1
        continue
    m = re.search(r"the packet carries motors \d+/\d+, flag0 (0x[0-9a-f]+|0), flag1 (0x[0-9a-f]+|0), power (0x[0-9a-f]+|0)", line)
    if m:
        thread[(m.group(1), m.group(2), m.group(3))] += 1
        continue
    if 'write output report id 49' in line: cur={}; native.append(cur); continue
    if cur is not None:
        h=re.search(r'hid_internal_dispatch (\d{8})  ((?:[0-9a-f]{2} ?)+)', line)
        if h:
            off=int(h.group(1),16)
            for i,v in enumerate(h.group(2).split()): cur[off+i]=int(v,16)
        else: cur=None

if not ride and not thread:
    print("  neither writer named the field -- this trace predates mgvf-0023, or")
    print("  the pad never rumbled at all. The lines above say which.")
else:
    if ride:
        print(f"  the ride stamped {sum(ride.values())} packets:")
        for (f0,f1,f2,pw),c in ride.most_common(6):
            claimed = "CLAIMED" if int(f1,16) & 0x40 else "not claimed"
            print(f"    flag0 {f0}  flag1 {f1} ({claimed})  flag2 {f2}  power {pw}   x{c}")
    if thread:
        print(f"  the thread wrote {sum(thread.values())} of its own:")
        for (f0,f1,pw),c in thread.most_common(6):
            claimed = "CLAIMED" if int(f1,16) & 0x40 else "not claimed"
            print(f"    flag0 {f0}  flag1 {f1} ({claimed})  power {pw}   x{c}")
    print("  A run with XInputRumbleMotorPower set must show flag1 0x40 on every row.")
    print("  If it shows 0x00, the option did not reach the driver: check the")
    print("  options line above, and that the bottle was not running when it was set.")

nat = [b for b in native if b.get(5,0) or b.get(6,0)]
if nat:
    c = collections.Counter((b.get(4,-1), b.get(39,-1)) for b in nat)
    print(f"  and the title's OWN motor packets, for comparison ({len(nat)} of them):")
    for (f1,pw),n in c.most_common(4):
        print(f"    flag1 {f1:#04x}  power {pw:#04x}   x{n}")
    print("  libScePad's session measured here was flag1 0x40 and power 0x00 on all")
    print("  282 of its motor packets, which is what this option imitates with 0.")
PY4

say "what the title actually wrote to the pad"
/usr/bin/grep -c "write output report id 2 " "$L"
python3 - "$L" <<'PY'
import sys, re, collections
motors = collections.Counter(); cur=None; n=0
for line in open(sys.argv[1], errors='replace'):
    if 'write output report id 2 length' in line: cur={}; n+=1; continue
    if cur is not None:
        m = re.search(r'hid_internal_dispatch (\d{8})  ((?:[0-9a-f]{2} ?)+)', line)
        if m:
            off=int(m.group(1),16)
            for i,v in enumerate(m.group(2).split()): cur[off+i]=int(v,16)
            continue
        if cur.get(0)==2: motors[(cur.get(3,0), cur.get(4,0), cur.get(1,0), cur.get(39,0))]+=1
        cur=None
asked = {k:v for k,v in motors.items() if k[0] or k[1]}
print(f"  {n} wired 0x02 writes")
print(f"  with a motor asked for: {sum(asked.values())}")
for (r,l,f0,f2),c in sorted(asked.items(), key=lambda x:-x[1])[:6]:
    print(f"    motors {r}/{l}  flag0 {f0:#04x}  flag2 {f2:#04x}   x{c}")
PY
