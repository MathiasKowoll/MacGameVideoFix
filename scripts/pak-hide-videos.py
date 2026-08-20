#!/usr/bin/env python3
"""
Hide the video files inside an Unreal Engine .pak so the engine falls back to
the loose copies in Content/Movies.

Why: UE5's Electra VPx decoder has no CVar escape from the D3D12 output buffer
pool (only H.264/H.265 do, via Electra.Win.*UseOldOutputPath), and that pool
needs ID3DDestructionNotifier, which Apple's D3DMetal does not implement.  So a
VP9 cutscene is an instant crash.  Re-encoding the movies to H.264 fixes it --
but the engine reads them from the .pak, not from disk, so the pak entries have
to go first.

How: the pak's file data is never touched.  A new FullDirectoryIndex without the
video entries, a new primary index and a new footer are appended at the end of
the file; the footer is what tells UE where the live index is.  Reverting is a
plain truncate back to the original size.

The PathHashIndex is switched off (bReaderHasPathHashIndex = 0) rather than
rebuilt.  It maps a hash of each path to its entry, so dropping an entry would
mean reimplementing UE's FCrc::StrCrc32 exactly; with only the directory index
present, FPakFile::LoadIndexInternal forces bWillUseFullDirectoryIndex anyway.

Usage:
    pak-hide-videos.py <pak>            list what would be hidden
    pak-hide-videos.py <pak> --apply    hide them
    pak-hide-videos.py <pak> --restore  undo
"""

# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import hashlib
import json
import os
import struct
import sys

FOOTER_SIZE = 221                 # v11: 16 guid + 1 flag + 4 magic + 4 ver + 8 + 8 + 20 + 160
MAGIC = b"\xe1\x12\x6f\x5a"
DEFAULT_EXTS = (".mp4", ".webm", ".ivf", ".mkv", ".mov")


class PakError(Exception):
    pass


def read_fstring(buf, pos):
    """UE FString: int32 length, then ANSI (positive) or UTF-16 (negative) bytes."""
    (n,) = struct.unpack_from("<i", buf, pos)
    end = pos + 4 + (n if n >= 0 else -2 * n)
    raw = buf[pos:end]
    if n >= 0:
        text = buf[pos + 4:pos + 4 + n].split(b"\x00")[0].decode("latin1")
    else:
        text = buf[pos + 4:end].decode("utf-16-le").split("\x00")[0]
    return text, raw, end


def read_footer(f, size):
    f.seek(size - FOOTER_SIZE)
    footer = f.read(FOOTER_SIZE)
    if footer[17:21] != MAGIC:
        raise PakError("not a recognisable .pak (no magic in the footer)")
    (version,) = struct.unpack_from("<I", footer, 21)
    if version != 11:
        raise PakError(f"pak version {version}; this script only handles v11")
    if footer[16]:
        raise PakError("the index is encrypted, which is not supported")
    index_offset, index_size = struct.unpack_from("<QQ", footer, 25)
    return footer, index_offset, index_size


def parse_index(index):
    """Locate the fields we need to rewrite inside the primary index."""
    pos = 0
    _, _, pos = read_fstring(index, pos)          # MountPoint
    num_entries_pos = pos
    (num_entries,) = struct.unpack_from("<i", index, pos)
    pos += 4
    seed_pos = pos
    pos += 8                                      # PathHashSeed
    path_hash_flag_pos = pos
    (has_path_hash,) = struct.unpack_from("<i", index, pos)
    pos += 4
    if has_path_hash:
        pos += 16 + 20                            # offset, size, hash
    (has_full_dir,) = struct.unpack_from("<i", index, pos)
    pos += 4
    if not has_full_dir:
        raise PakError("this pak has no FullDirectoryIndex, nothing to rewrite")
    full_dir_offset, full_dir_size = struct.unpack_from("<qq", index, pos)
    pos += 16 + 20
    return {
        "num_entries": num_entries,
        "num_entries_pos": num_entries_pos,
        "seed_pos": seed_pos,
        "path_hash_flag_pos": path_hash_flag_pos,
        "full_dir_offset": full_dir_offset,
        "full_dir_size": full_dir_size,
        "tail_pos": pos,                          # EncodedPakEntries + Files, copied verbatim
    }


def rewrite_directory_index(blob, exts):
    """Return (new blob, list of removed paths). Kept entries are copied byte for byte."""
    pos = 0
    (num_dirs,) = struct.unpack_from("<i", blob, pos)
    pos += 4
    out = bytearray(struct.pack("<i", num_dirs))
    removed = []
    for _ in range(num_dirs):
        dir_name, dir_raw, pos = read_fstring(blob, pos)
        (num_files,) = struct.unpack_from("<i", blob, pos)
        pos += 4
        kept = bytearray()
        kept_count = 0
        for _ in range(num_files):
            file_name, file_raw, pos = read_fstring(blob, pos)
            entry = blob[pos:pos + 4]
            pos += 4
            if file_name.lower().endswith(exts):
                removed.append(dir_name + file_name)
                continue
            kept += file_raw + entry
            kept_count += 1
        out += dir_raw + struct.pack("<i", kept_count) + kept
    return bytes(out), removed


def meta_path(pak):
    return os.path.join(os.path.dirname(pak) or ".", "." + os.path.basename(pak) + ".hidden-videos.json")


def apply(pak, exts, dry_run):
    meta = meta_path(pak)
    if os.path.exists(meta):
        raise PakError("this pak is already patched — run --restore first")

    with open(pak, "r+b") as f:
        f.seek(0, os.SEEK_END)
        original_size = f.tell()
        footer, index_offset, index_size = read_footer(f, original_size)
        f.seek(index_offset)
        index = f.read(index_size)
        if hashlib.sha1(index).digest() != footer[41:61]:
            raise PakError("index hash mismatch — the pak may be corrupt")

        fields = parse_index(index)
        f.seek(fields["full_dir_offset"])
        directory_blob = f.read(fields["full_dir_size"])
        new_dir, removed = rewrite_directory_index(directory_blob, exts)

        if not removed:
            print("no video files in the index, nothing to do")
            return
        print(f"{len(removed)} video entries in the index:")
        for path in removed[:8]:
            print(f"   {path}")
        if len(removed) > 8:
            print(f"   ... and {len(removed) - 8} more")
        if dry_run:
            print("\n(listing only — add --apply to write the change)")
            return

        new_dir_offset = original_size
        new_index = bytearray()
        new_index += index[:fields["num_entries_pos"]]
        new_index += struct.pack("<i", fields["num_entries"] - len(removed))
        new_index += index[fields["seed_pos"]:fields["path_hash_flag_pos"]]
        new_index += struct.pack("<i", 0)                      # bReaderHasPathHashIndex
        new_index += struct.pack("<i", 1)                      # bReaderHasFullDirectoryIndex
        new_index += struct.pack("<qq", new_dir_offset, len(new_dir))
        new_index += hashlib.sha1(new_dir).digest()
        new_index += index[fields["tail_pos"]:]
        new_index = bytes(new_index)

        new_index_offset = new_dir_offset + len(new_dir)
        new_footer = bytearray(footer)
        struct.pack_into("<QQ", new_footer, 25, new_index_offset, len(new_index))
        new_footer[41:61] = hashlib.sha1(new_index).digest()

        f.seek(original_size)
        f.write(new_dir)
        f.write(new_index)
        f.write(bytes(new_footer))
        f.flush()
        os.fsync(f.fileno())

    # Record enough to prove, on restore, that this is still the file we
    # patched. A game update replaces the pak in place: without these checks a
    # later --restore would truncate the *new* pak to the *old* size and
    # destroy it.
    patched_size = os.path.getsize(pak)
    with open(meta, "w") as fh:
        json.dump({
            "original_size": original_size,
            "patched_size": patched_size,
            "footer_sha1": hashlib.sha1(bytes(new_footer)).hexdigest(),
            "removed": len(removed),
        }, fh)
    print(f"\ndone: {original_size} -> {os.path.getsize(pak)} bytes")
    print("the engine will now read the loose files in Content/Movies")


def restore(pak):
    meta = meta_path(pak)
    if not os.path.exists(meta):
        raise PakError("no record of a previous patch for this pak")

    with open(meta) as fh:
        record = json.load(fh)
    original_size = record["original_size"]
    current_size = os.path.getsize(pak)

    # Records written before this check existed only had original_size. Treat a
    # missing patched_size as "unverifiable" rather than trusting it.
    patched_size = record.get("patched_size")
    footer_sha1 = record.get("footer_sha1")

    replaced_hint = (
        "The pak is not the one this tool patched -- a game update or a Steam "
        "file verification most likely replaced it.\n"
        "Nothing was changed. Delete the stale record and re-apply if you want "
        "the fix back:\n"
        f"  rm {meta}"
    )

    if patched_size is None or footer_sha1 is None:
        # Old record. Fall back to structural checks: the file must still carry
        # the index we appended, and the original footer must sit exactly where
        # we would truncate to.
        if current_size <= original_size:
            raise PakError(
                f"this pak is {current_size} bytes, not larger than the "
                f"recorded original of {original_size}.\n" + replaced_hint)
    else:
        if current_size != patched_size:
            raise PakError(
                f"this pak is {current_size} bytes; the patch left it at "
                f"{patched_size}.\n" + replaced_hint)
        with open(pak, "rb") as f:
            f.seek(current_size - FOOTER_SIZE)
            if hashlib.sha1(f.read(FOOTER_SIZE)).hexdigest() != footer_sha1:
                raise PakError("the pak's footer is not the one we wrote.\n"
                               + replaced_hint)

    # Whatever the record said, only truncate if a valid original footer is
    # actually sitting at that offset. This is the check that would have
    # prevented cutting a replaced pak short.
    with open(pak, "rb") as f:
        try:
            read_footer(f, original_size)
        except PakError as err:
            raise PakError(
                f"truncating to {original_size} would not leave a valid pak "
                f"({err}).\n" + replaced_hint)

    with open(pak, "r+b") as f:
        f.truncate(original_size)

    # Confirm the result really is a working pak before dropping the record.
    with open(pak, "rb") as f:
        read_footer(f, original_size)

    os.remove(meta)
    print(f"restored to {original_size} bytes")


def main():
    ap = argparse.ArgumentParser(description="Hide video files in an Unreal Engine .pak index.")
    ap.add_argument("pak")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--apply", action="store_true", help="write the change")
    group.add_argument("--restore", action="store_true", help="undo")
    ap.add_argument("--ext", action="append", metavar=".mp4",
                    help="extension to hide, repeatable (default: %s)" % " ".join(DEFAULT_EXTS))
    args = ap.parse_args()

    exts = tuple(e.lower() if e.startswith(".") else "." + e.lower() for e in (args.ext or DEFAULT_EXTS))
    try:
        if args.restore:
            restore(args.pak)
        else:
            apply(args.pak, exts, dry_run=not args.apply)
    except (PakError, OSError) as err:
        sys.exit(f"error: {err}")


if __name__ == "__main__":
    main()
