#!/usr/bin/env python3
#
# Can this engine play an Electra title?
#
#     check-engine-media.py
#
# Measured 2026-08-26, and this is the whole of it: a UE5 title playing through
# Electra hands its decoder one GOP and waits for pictures before sending more.
# An H.264 decoder holds frames until it knows nothing earlier is coming. On an
# engine whose winegstreamer never configures its queue limits, neither side
# moves and the video ends after 150 ms -- measured on CrossOver 26.3 (7 access
# units in, 1 frame out), on Preview 27 (8 in, 2 out) and on the Procyon fork's
# engine (8 in, 2 out), with one DLL and one build of the game.
#
# The same DLL and the same build play the cutscene whole on winevideo: 475
# frames, one every 16.7 ms of wall clock, ending on its own.
#
# The difference between those engines is four strings in one binary, and this
# looks for them. What it does NOT do is explain the fix: the strings were first
# attributed to patches 0018 and 0019, and reading those patches showed 0019
# reverting 0018 and both touching the source reader, which an Electra title does
# not use. They survive because 0030 reintroduces the queue behind a per-process
# flag. The likely fix is 0008, which makes the transform provide its own output
# samples rather than waiting for the caller to allocate -- which is the
# stand-off this title dies of. That is a candidate, not a finding.
#
# The strings still separate the builds, which is all this needs to do. That makes the question answerable without launching anything,
# and answerable again after somebody ports those patches -- the same search
# says whether the new binary really carries them, rather than trusting that a
# patch number was applied.
#
# Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

import os
import sys

# winevideo's 0018 and 0019, as they appear in a built binary.
QUEUE_BOUNDS = (b"max-size-time", b"max-size-buffers", b"max-size-bytes")
DEMUX_BOUND = b"decodebin_parser_init_gst"
# Its 0006 and the NV12 restore leave no comparable mark, so they are not
# claimed here. This reports the one thing it can actually see.

RELATIVE = "Contents/SharedSupport/CrossOver/lib/wine/x86_64-unix/winegstreamer.so"


def engines():
    for root in ("/Applications", os.path.expanduser("~/Applications")):
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            path = os.path.join(root, name, RELATIVE)
            if os.path.isfile(path):
                yield os.path.join(root, name), path


def main():
    found = list(engines())
    if not found:
        print("no engine with a winegstreamer was found", file=sys.stderr)
        return 1

    print("  engine                              queue bounds  demux bound   Electra")
    any_ok = False
    for app, so in found:
        blob = open(so, "rb").read()
        bounds = all(n in blob for n in QUEUE_BOUNDS)
        demux = DEMUX_BOUND in blob
        ok = bounds and demux
        any_ok = any_ok or ok
        print(f"  {os.path.basename(app)[:34]:<36}"
              f"{'set' if bounds else 'ABSENT':<14}"
              f"{'set' if demux else 'ABSENT':<14}"
              f"{'plays' if ok else 'stalls after a GOP'}")

    if not any_ok:
        print("\n  No engine here can play a title that decodes through Electra.")
        print("  What is missing is winevideo's 0018 and 0019, which lift the time")
        print("  bounds on the transform's own queue -- not on the source reader,")
        print("  which is why they look at first like patches for another path.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
