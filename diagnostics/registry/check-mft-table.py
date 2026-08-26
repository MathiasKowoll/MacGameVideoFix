#!/usr/bin/env python3
#
# Is a bottle's Media Foundation transform table intact?
#
#     check-mft-table.py                  report on every bottle found
#     check-mft-table.py <bottle> ...     report on these
#     check-mft-table.py <bottle> --repair
#
# Written after an afternoon spent concluding, wrongly and three times over,
# that an engine had no H.264 decoder. It had one. What it did not have was a
# usable REGISTRATION of it: the InputTypes value under
#
#     HKLM\Software\Classes\MediaFoundation\Transforms\<clsid>
#
# was zero bytes long. MFTEnumEx matches a caller's format filter against that
# blob, so an empty one matches nothing and the decoder is invisible -- while
# still being installed, still instantiable by CLSID, and still reporting
# itself perfectly in every other way. Nothing anywhere says "this is broken".
#
# The cause was not the engine. A launcher rewrote the whole system.reg to set
# two DWORDs, and its parser did not know that a long REG_BINARY continues on
# the next line with a trailing backslash. Each split value swallowed the
# declaration of the one below it, so the damage was not one key: it was every
# multi-line value in the file, which is every decoder in the table at once --
# H.264, VP9, WMV/VC-1, WMA, AAC, MP3.
#
# So this asks the only question worth asking about that table, and asks it of
# the table rather than of the engine that filled it.
#
# The reference is other bottles. These registrations are written by the same
# builtin registrar in every bottle on a machine, so they agree byte for byte,
# and a damaged one stands out against its neighbours without needing a
# known-good copy shipped from anywhere. Where the neighbours disagree, this
# says nothing: a value with two opinions is not evidence of damage.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

import os, re, shutil, subprocess, sys

# The three views of the same key. A win64 bottle keeps HKLM\Software\Classes
# for 64-bit callers and mirrors it twice for 32-bit ones, and a repair that
# writes only the first leaves a 32-bit game exactly as blind as it was.
HDR = re.compile(
    r"^\[Software\\\\(Classes\\\\Wow6432Node|Wow6432Node\\\\Classes|Classes)"
    r"\\\\MediaFoundation\\\\Transforms\\\\([0-9a-f-]{36})\]", re.I)
VAL = re.compile(r'^"(InputTypes|OutputTypes)"=hex(?:\(0\))?:(.*)$')

# How many bottles must hold a value before their agreement counts as one.
#
# Unanimity alone is not enough, and the first draft proved it the hard way:
# run against a small pool that included one known-damaged file, it declared
# the HEALTHY bottles damaged and named the damaged file's bytes as the
# correction -- because for values only that one file carried, "everyone who
# has an opinion agrees" was a majority of one. A guard that can recommend the
# damage is worse than no guard. Three witnesses, or this says nothing.
MIN_VOTERS = 3


def bottle_roots():
    """Every root a bottle can live under. Asked for, never assumed."""
    roots = []
    env = os.environ.get("MGVF_BOTTLES", "")
    if env:
        roots.append(env)
    roots.append(os.path.expanduser("~/Library/Application Support/CrossOver/Bottles"))
    try:
        out = subprocess.run(["defaults", "read", "com.codeweavers.CrossOver", "BottleDir"],
                             capture_output=True, text=True).stdout.strip()
        if out:
            roots.append(out)
    except OSError:
        pass
    # A launcher that ships its own copy of CrossOver declares where it keeps
    # bottles in that copy's etc/CrossOver.conf. Product names were hardcoded
    # here once and went stale the week a fork was renamed; the engines knew.
    for apps in ("/Applications", os.path.expanduser("~/Applications")):
        for name in sorted(os.listdir(apps)) if os.path.isdir(apps) else []:
            conf = os.path.join(apps, name, "Contents/SharedSupport/CrossOver/etc/CrossOver.conf")
            try:
                text = open(conf, errors="replace").read()
            except OSError:
                continue
            m = re.search(r'"CX_BOTTLE_PATH"\s*=\s*"([^"]+)"', text)
            if m:
                roots.append(os.path.expanduser(m.group(1)))
    seen, out = set(), []
    for r in roots:
        r = r.rstrip("/")
        if r and r not in seen and os.path.isdir(r):
            seen.add(r)
            out.append(r)
    return out


def find_bottles(args):
    if args:
        return [a.rstrip("/") for a in args]
    found = []
    for root in bottle_roots():
        for name in sorted(os.listdir(root)):
            d = os.path.join(root, name)
            if os.path.isfile(os.path.join(d, "system.reg")):
                found.append(d)
    return found


def parse(path):
    """[(view, clsid, block_end, {key: (start, end, bytes, literal text)})]"""
    lines = open(path, errors="replace").read().splitlines(keepends=True)
    blocks, i = [], 0
    while i < len(lines):
        m = HDR.match(lines[i])
        if not m:
            i += 1
            continue
        view, clsid, j, vals = m.group(1).lower(), m.group(2).lower(), i + 1, {}
        while j < len(lines) and lines[j].strip():
            v = VAL.match(lines[j].rstrip("\n"))
            if v:
                start, data = j, v.group(2)
                # A value continues while its line ends in a backslash. Reading
                # it any other way is the whole bug this file exists for.
                while data.endswith("\\") and j + 1 < len(lines):
                    j += 1
                    data = data[:-1] + lines[j].strip()
                vals[v.group(1)] = (start, j + 1,
                                    bytes.fromhex(re.sub(r"[^0-9a-fA-F]", "", data)),
                                    "".join(lines[start:j + 1]))
            j += 1
        blocks.append((view, clsid, j, vals))
        i = j
    return lines, blocks


def names(path):
    """{clsid: friendly name}, for reports a person can read."""
    out, clsid = {}, None
    for line in open(path, errors="replace"):
        m = HDR.match(line)
        if m:
            clsid = m.group(2).lower()
        elif clsid and line.startswith("@="):
            out.setdefault(clsid, line[2:].strip().strip('"'))
            clsid = None
    return out


def consensus(tables, exclude):
    """Unanimous (view, clsid, key) -> literal text, across every other bottle.

    Unanimity is the point. Two bottles holding different bytes for one value
    means the value legitimately varies, and a repair that picked a side would
    be inventing an answer rather than restoring one.
    """
    votes = {}
    for path, blocks in tables.items():
        if path == exclude:
            continue
        for view, clsid, _end, vals in blocks:
            for key, (_s, _e, raw, text) in vals.items():
                votes.setdefault((view, clsid, key), {}).setdefault(raw, [text, 0])[1] += 1
    out = {}
    for k, opinions in votes.items():
        if len(opinions) != 1:
            continue
        text, count = next(iter(opinions.values()))
        if count >= MIN_VOTERS:
            out[k] = text
    return out


def hexof(text):
    return bytes.fromhex(re.sub(r"[^0-9a-fA-F]", "", text.split(":", 1)[1]))


def busy(bottle):
    """Is anything running in this prefix? Editing under a wineserver is lost.

    wineserver holds the registry in memory and flushes it when the last
    process leaves, so an edit made while it runs is overwritten without a word
    -- the same lazy write that has made three checks in this repository pass
    against a registry that had already thrown the answer away.
    """
    out = subprocess.run(["ps", "ax", "-o", "command"], capture_output=True, text=True).stdout
    return any(bottle in line for line in out.splitlines())


def main(argv):
    repair = "--repair" in argv
    args = [a for a in argv if not a.startswith("--")]
    bottles = find_bottles(args)
    if not bottles:
        print("no bottles found", file=sys.stderr)
        return 1

    tables = {}
    for b in bottles:
        reg = os.path.join(b, "system.reg")
        if os.path.isfile(reg):
            tables[reg] = parse(reg)[1]
    if len(tables) < 2:
        print("need at least two bottles: one is judged against the others", file=sys.stderr)
        return 1

    damaged_any = False
    for b in bottles:
        reg = os.path.join(b, "system.reg")
        if reg not in tables:
            continue
        good = consensus(tables, reg)
        friendly = names(reg)
        lines, blocks = parse(reg)
        wrong = []
        for view, clsid, _end, vals in blocks:
            for key in ("InputTypes", "OutputTypes"):
                want = good.get((view, clsid, key))
                if want is None:
                    continue
                got = vals.get(key)
                if got is None:
                    # Absent is not the same as wrong, and conflating them
                    # makes this shout at healthy bottles. Three bottles here
                    # simply never had the MP3 decoder's 32-bit output list,
                    # which is an older engine's registration, not damage.
                    wrong.append((view, clsid, key, None, len(hexof(want))))
                elif got[2] != hexof(want):
                    wrong.append((view, clsid, key, len(got[2]), len(hexof(want))))
        if not wrong:
            print(f"  ok       {os.path.basename(b)}")
            continue

        corrupt = [w for w in wrong if w[3] is not None]
        if corrupt:
            damaged_any = True
        label = "DAMAGED " if corrupt else "partial "
        print(f"  {label} {os.path.basename(b)} -- {len(corrupt)} wrong, "
              f"{len(wrong) - len(corrupt)} absent")
        # One line per (transform, value), naming every view it is wrong in.
        # An earlier draft printed only the 64-bit view to keep the report
        # short, and a bottle damaged in a mirror alone printed the word
        # DAMAGED followed by nothing at all -- which is the same silence this
        # file exists to break.
        rolled = {}
        for view, clsid, key, have, want in wrong:
            rolled.setdefault((clsid, key), [have, want, []])[2].append(
                {"classes": "64-bit"}.get(view, "32-bit"))
        for (clsid, key), (have, want, views) in sorted(
                rolled.items(), key=lambda kv: friendly.get(kv[0][0], kv[0][0])):
            where = "" if len(views) == 3 else f"  [{', '.join(sorted(set(views)))} view]"
            state = "absent" if have is None else f"{have}B"
            print(f"             {friendly.get(clsid, clsid)[:44]:<46} "
                  f"{key:<12} {state:>6}, should be {want:>4}B{where}")

        if not repair:
            continue
        if busy(b):
            print("             not repaired: something is running in this bottle")
            continue

        backup = reg + ".pre-mft-repair"
        shutil.copy2(reg, backup)
        fixed = added = 0
        # Splice from the last block backwards, so the line numbers of the
        # blocks still to come do not move under us.
        for view, clsid, end, vals in reversed(blocks):
            for key in ("OutputTypes", "InputTypes"):
                want = good.get((view, clsid, key))
                if want is None:
                    continue
                if key in vals:
                    start, stop, raw, _ = vals[key]
                    if raw == hexof(want):
                        continue
                    lines[start:stop] = [want]
                    fixed += 1
                else:
                    lines[end:end] = [want]
                    added += 1
        open(reg, "w").write("".join(lines))
        # Read it back. A repair that reports itself done without looking is
        # the exact shape of defect this whole file was written to catch.
        _, after = parse(reg)
        left = [1 for view, clsid, _e, vals in after for key in ("InputTypes", "OutputTypes")
                if (view, clsid, key) in good
                and (key not in vals or vals[key][2] != hexof(good[(view, clsid, key)]))]
        print(f"             repaired: {fixed} rewritten, {added} inserted, "
              f"{len(left)} still wrong (backup: {os.path.basename(backup)})")
        if left:
            return 1

    return 1 if damaged_any and not repair else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
