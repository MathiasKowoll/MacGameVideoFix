#!/usr/bin/env python3
"""
Read a Windows PE binary without running Windows.

Exports are used twice: at build time to generate the proxy's forwarder .def
from the game's own libogg_64.dll, and at install time to check that the
prebuilt proxy really does forward everything this copy of the game expects.

Imports answer a different question -- which media API a game actually calls.
A game that imports mfplat is going through Media Foundation; one that imports
nothing media-related is decoding with something it ships itself. That is the
first thing worth knowing about a title whose cutscenes do not play.

Pure stdlib -- this has to run on a stock macOS with no toolchain.

Usage:
    pe.py exports <file>             one name per line
    pe.py exports <file> --ordinals  "<ordinal> <name>"
    pe.py imports <file>             "<dll>" then "    <symbol>" per line
    pe.py imports <file> --dlls      just the DLL names
"""

# SPDX-License-Identifier: GPL-3.0-or-later

import struct
import sys


class PEError(Exception):
    pass


def _sections(buf, opt_start, num_sections, opt_size):
    start = opt_start + opt_size
    out = []
    for i in range(num_sections):
        off = start + i * 40
        va, raw_size, raw_ptr = struct.unpack_from("<I", buf, off + 12)[0], \
                                struct.unpack_from("<I", buf, off + 16)[0], \
                                struct.unpack_from("<I", buf, off + 20)[0]
        out.append((va, raw_size, raw_ptr))
    return out


def _rva_to_off(sections, rva):
    for va, size, ptr in sections:
        if va <= rva < va + max(size, 1):
            return ptr + (rva - va)
    raise PEError(f"RVA {rva:#x} is not inside any section")


def _cstr(buf, off):
    end = buf.index(b"\x00", off)
    return buf[off:end].decode("ascii", "replace")


def _headers(buf):
    """(pe offset, optional header offset, sections, data directory offset, is 64-bit)."""
    if buf[:2] != b"MZ":
        raise PEError("not a PE file (no MZ)")
    pe = struct.unpack_from("<I", buf, 0x3C)[0]
    if buf[pe:pe + 4] != b"PE\x00\x00":
        raise PEError("not a PE file (no PE signature)")

    num_sections = struct.unpack_from("<H", buf, pe + 6)[0]
    opt_size = struct.unpack_from("<H", buf, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", buf, opt)[0]
    if magic == 0x20B:                       # PE32+
        dir_start, is64 = opt + 112, True
    elif magic == 0x10B:                     # PE32
        dir_start, is64 = opt + 96, False
    else:
        raise PEError(f"unknown optional header magic {magic:#x}")

    return pe, opt, _sections(buf, opt, num_sections, opt_size), dir_start, is64


def exports(path):
    with open(path, "rb") as f:
        buf = f.read()

    pe, opt, sections, dir_start, _ = _headers(buf)

    exp_rva = struct.unpack_from("<I", buf, dir_start)[0]
    if not exp_rva:
        return []
    exp = _rva_to_off(sections, exp_rva)

    base = struct.unpack_from("<I", buf, exp + 16)[0]
    num_names = struct.unpack_from("<I", buf, exp + 24)[0]
    names_rva = struct.unpack_from("<I", buf, exp + 32)[0]
    ords_rva = struct.unpack_from("<I", buf, exp + 36)[0]
    if not num_names:
        return []

    names_off = _rva_to_off(sections, names_rva)
    ords_off = _rva_to_off(sections, ords_rva)

    out = []
    for i in range(num_names):
        name_rva = struct.unpack_from("<I", buf, names_off + i * 4)[0]
        ordinal = struct.unpack_from("<H", buf, ords_off + i * 2)[0] + base
        out.append((ordinal, _cstr(buf, _rva_to_off(sections, name_rva))))
    out.sort()
    return out


def _thunk_names(buf, sections, thunk_rva, is64):
    """Walk one import descriptor's thunk array into symbol names."""
    step = 8 if is64 else 4
    fmt = "<Q" if is64 else "<I"
    ordinal_flag = 1 << (63 if is64 else 31)
    out = []
    try:
        off = _rva_to_off(sections, thunk_rva)
    except PEError:
        return out
    while True:
        (value,) = struct.unpack_from(fmt, buf, off)
        if not value:
            break
        if value & ordinal_flag:
            out.append(f"#{value & 0xFFFF}")
        else:
            try:
                # IMAGE_IMPORT_BY_NAME: WORD hint, then the name.
                out.append(_cstr(buf, _rva_to_off(sections, value) + 2))
            except (PEError, ValueError):
                out.append("?")
        off += step
    return out


def imports(path):
    """[(dll, [symbol, ...]), ...] covering both normal and delay-loaded imports."""
    with open(path, "rb") as f:
        buf = f.read()

    pe, opt, sections, dir_start, is64 = _headers(buf)
    out = []

    # DataDirectory[1] is the import table; [0] is exports, [13] delay imports.
    imp_rva = struct.unpack_from("<I", buf, dir_start + 8)[0]
    if imp_rva:
        off = _rva_to_off(sections, imp_rva)
        while True:
            orig_thunk, name_rva, thunk = (struct.unpack_from("<I", buf, off)[0],
                                           struct.unpack_from("<I", buf, off + 12)[0],
                                           struct.unpack_from("<I", buf, off + 16)[0])
            if not name_rva:
                break
            dll = _cstr(buf, _rva_to_off(sections, name_rva))
            out.append((dll, _thunk_names(buf, sections, orig_thunk or thunk, is64)))
            off += 20

    # Delay-loaded imports live in their own directory. Games commonly delay
    # mfplat and friends, so missing these would hide the very thing we want.
    delay_rva = struct.unpack_from("<I", buf, dir_start + 13 * 8)[0]
    if delay_rva:
        off = _rva_to_off(sections, delay_rva)
        while True:
            attrs, name_rva, _, thunk = struct.unpack_from("<IIII", buf, off)
            if not name_rva:
                break
            # Attributes bit 0 set means the fields are RVAs; the older form
            # stored absolute addresses, which we would have to rebase.
            if attrs & 1:
                try:
                    dll = _cstr(buf, _rva_to_off(sections, name_rva))
                    out.append((dll + "  (delay-loaded)",
                                _thunk_names(buf, sections, thunk, is64)))
                except (PEError, ValueError):
                    pass
            off += 32

    return out


def main():
    argv = sys.argv[1:]
    flags = [a for a in argv if a.startswith("-")]
    args = [a for a in argv if not a.startswith("-")]
    if len(args) != 2 or args[0] not in ("exports", "imports"):
        sys.exit(__doc__.strip())

    try:
        if args[0] == "exports":
            for ordinal, name in exports(args[1]):
                print(f"{ordinal} {name}" if "--ordinals" in flags else name)
        else:
            for dll, symbols in imports(args[1]):
                print(dll)
                if "--dlls" not in flags:
                    for sym in symbols:
                        print(f"    {sym}")
    except (PEError, OSError, ValueError, struct.error) as err:
        sys.exit(f"error: {err}")


if __name__ == "__main__":
    main()
