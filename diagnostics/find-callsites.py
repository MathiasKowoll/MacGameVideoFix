#!/usr/bin/env python3
"""
Find where a PE calls an imported function, without running it.

A call through the import table is `call qword ptr [rip+disp32]` -- FF 15,
then a displacement relative to the next instruction. Resolve every one of
those against the address of the import's IAT slot and the matches are the
call sites.

Cheaper than a hooked run, and it works on a game you cannot reach the right
screen in.

    find-callsites.py <file.exe> <symbol> [symbol...]

SPDX-License-Identifier: GPL-3.0-or-later
"""

import struct
import sys


class PEError(Exception):
    pass


def parse(path):
    buf = open(path, "rb").read()
    if buf[:2] != b"MZ":
        raise PEError("not a PE file")
    pe = struct.unpack_from("<I", buf, 0x3C)[0]
    nsec = struct.unpack_from("<H", buf, pe + 6)[0]
    optsize = struct.unpack_from("<H", buf, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", buf, opt)[0]
    if magic != 0x20B:
        raise PEError("only 64-bit images have this call form")
    base = struct.unpack_from("<Q", buf, opt + 24)[0]
    dirs = opt + 112

    sections = []
    off = opt + optsize
    for i in range(nsec):
        s = off + i * 40
        name = buf[s:s + 8].rstrip(b"\x00").decode("latin1")
        va, vsize = struct.unpack_from("<II", buf, s + 12)[1], struct.unpack_from("<I", buf, s + 8)[0]
        va = struct.unpack_from("<I", buf, s + 12)[0]
        rawsize, rawptr = struct.unpack_from("<II", buf, s + 16)
        flags = struct.unpack_from("<I", buf, s + 36)[0]
        sections.append((name, va, vsize, rawsize, rawptr, flags))
    return buf, base, dirs, sections


def rva_to_off(sections, rva):
    for _, va, vsize, rawsize, rawptr, _ in sections:
        if va <= rva < va + max(vsize, rawsize):
            return rawptr + (rva - va)
    raise PEError(f"RVA {rva:#x} outside every section")


def iat_slots(buf, dirs, sections, wanted):
    """{symbol: IAT slot RVA} for the names we were asked about."""
    imp_rva = struct.unpack_from("<I", buf, dirs + 8)[0]
    off = rva_to_off(sections, imp_rva)
    found = {}
    while True:
        orig, name_rva, first = (struct.unpack_from("<I", buf, off)[0],
                                 struct.unpack_from("<I", buf, off + 12)[0],
                                 struct.unpack_from("<I", buf, off + 16)[0])
        if not name_rva:
            break
        names_off = rva_to_off(sections, orig or first)
        i = 0
        while True:
            value = struct.unpack_from("<Q", buf, names_off + i * 8)[0]
            if not value:
                break
            if not value & (1 << 63):
                end = buf.index(b"\x00", rva_to_off(sections, value) + 2)
                sym = buf[rva_to_off(sections, value) + 2:end].decode("latin1")
                if sym in wanted:
                    found[sym] = first + i * 8
            i += 1
        off += 20
    return found


def call_sites(buf, sections, slot_rva):
    """RVAs of every `call qword ptr [rip+disp32]` that lands on slot_rva."""
    out = []
    for name, va, vsize, rawsize, rawptr, flags in sections:
        if not flags & 0x20000000:          # IMAGE_SCN_MEM_EXECUTE
            continue
        size = min(vsize or rawsize, rawsize)
        data = buf[rawptr:rawptr + size]
        start = 0
        while True:
            i = data.find(b"\xff\x15", start)
            if i < 0 or i + 6 > len(data):
                break
            start = i + 1
            disp = struct.unpack_from("<i", data, i + 2)[0]
            if va + i + 6 + disp == slot_rva:
                out.append(va + i)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip())
    path, wanted = sys.argv[1], set(sys.argv[2:])
    buf, base, dirs, sections = parse(path)
    slots = iat_slots(buf, dirs, sections, wanted)

    for sym in sys.argv[2:]:
        if sym not in slots:
            print(f"{sym}: not imported")
            continue
        sites = call_sites(buf, sections, slots[sym])
        print(f"{sym}: IAT slot at rva {slots[sym]:#x}, {len(sites)} call site(s)")
        for rva in sites:
            print(f"    rva {rva:#x}    va {base + rva:#x}")


if __name__ == "__main__":
    main()
