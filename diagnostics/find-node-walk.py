#!/usr/bin/env python3
"""
Find the adapter-node walk that hangs under D3DMetal.

Unreal's D3D12 RHI accumulates video memory across the adapter's nodes and
ends the walk when the call fails:

    callq *0x70(%rax)     ; IDXGIAdapter3::QueryVideoMemoryInfo, slot 14
    testl %eax, %eax
    jns   <backwards>     ; keep going while it succeeds

On Windows the call returns an error once the index passes the number of
nodes. D3DMetal answers S_OK for every index, so the walk never ends: one
thread pinned at two hundred million iterations a second, and a game that
freezes after a while wherever it happens to be.

The backwards jump is what makes it a loop, and what makes this worth
scanning for: the call-and-check shape alone appears twenty times in a
typical executable, and the loop appears once or not at all.

    find-node-walk.py <file.exe> [more...]

SPDX-License-Identifier: GPL-3.0-or-later
"""

import struct
import sys


def scan(path):
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsize = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from("<H", d, opt)[0] != 0x20B:
        raise ValueError("only 64-bit images have this call form")
    base = struct.unpack_from("<Q", d, opt + 24)[0]

    hits = []
    for i in range(nsec):
        s = opt + optsize + i * 40
        if not struct.unpack_from("<I", d, s + 36)[0] & 0x20000000:
            continue                                  # not executable
        va = struct.unpack_from("<I", d, s + 12)[0]
        vsize = struct.unpack_from("<I", d, s + 8)[0]
        rawsize, rawptr = struct.unpack_from("<II", d, s + 16)
        blob = d[rawptr:rawptr + min(vsize or rawsize, rawsize)]

        start = 0
        while True:
            j = blob.find(b"\xff\x50\x70\x85\xc0", start)   # call [rax+0x70]; test eax,eax
            if j < 0:
                break
            start = j + 1
            k = j + 5
            if blob[k:k + 1] == b"\x79":                    # jns rel8
                back = struct.unpack_from("<b", blob, k + 1)[0] < 0
            elif blob[k:k + 2] == b"\x0f\x89":              # jns rel32
                back = struct.unpack_from("<i", blob, k + 2)[0] < 0
            else:
                continue
            if back:
                hits.append(base + va + j)
    return base, hits


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip())
    for path in sys.argv[1:]:
        name = path.rsplit("/", 1)[-1]
        try:
            base, hits = scan(path)
        except (OSError, ValueError) as err:
            print(f"{name}: {err}")
            continue
        if hits:
            print(f"{name}: affected, {len(hits)} loop(s)")
            for h in hits:
                print(f"    +0x{h - base:x}")
        else:
            print(f"{name}: no node walk found")


if __name__ == "__main__":
    main()
