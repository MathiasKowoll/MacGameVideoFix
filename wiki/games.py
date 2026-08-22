#!/usr/bin/env python3
"""Render the games table into every page that carries it.

The table appears on more than one page. Keeping one copy per page invites
exactly the kind of drift that has already bitten this repository twice, so
the rows live here and get injected between the markers instead.

    wiki/games.py            # rewrite the pages
    wiki/games.py --check    # fail if a page is out of date

SPDX-License-Identifier: GPL-3.0-or-later
"""

import pathlib
import re
import sys

# game, engine, symptom, fix, winevideo, status, page
GAMES = [
    ("Mortal Shell 2", "Unreal Engine 5.6.1",
     "Crash on the first cutscene", "Runtime patch, 4 sites",
     "No <sup>1</sup>", "Fixed", "Mortal-Shell-2"),
    ("Life is Strange: Reunion", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard",
     "No <sup>1</sup>", "Fixed", "Life-is-Strange-Reunion"),
    ("Life is Strange: Double Exposure", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard, same DLL",
     "No <sup>2</sup>", "Installed, not yet confirmed in play",
     "Life-is-Strange-Double-Exposure"),
    ("DYNASTY WARRIORS: ORIGINS", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "Video bridge, D3D11 to D3D12",
     "No <sup>3</sup>", "Fixed", "Dynasty-Warriors-Origins"),
    ("Beast of Reincarnation", "Unreal Engine 5",
     "Startup video plays with sound, no picture", "NV12 restored, Electra forced to software",
     "No <sup>1</sup>", "Fixed", "Beast-of-Reincarnation"),
]

HEAD = ("| Game | Engine | Symptom | Fix | winevideo | Status |\n"
        "| --- | --- | --- | --- | --- | --- |\n")

NOTE = """
<sup>1</sup> Measured, not assumed. Mortal Shell 2 and Life is Strange:
Reunion were played on a CrossOver carrying no winevideo and again on one that
did, same version, differing only in the GStreamer plugins. Beast of
Reincarnation was measured differently and more strictly: every run of it was in
a bottle winevideo has never touched.

<sup>2</sup> Inferred from Reunion rather than measured: identical fault,
identical DLL.

<sup>3</sup> Measured on CrossOver Preview 20260821, in a bottle winevideo had
never touched and with no `.webm` byte-stream handler registered. It was
expected to fail there and did not. **How the WebM is opened at all under those
conditions is not yet explained** -- Preview's own mfplat contains neither the
handler's CLSID nor the string "webm" -- so this is recorded as a measurement
with an open question behind it, not as an understood result.

**No game here needs CrossOver patched with winevideo.** That was not true when
this project started, and it is the single biggest change: Preview decodes VP9
profile 0 and 2, H.264 and AAC on its own. What is still needed is everything
in the Fix column, because none of it is decoding.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy. Where something was
established by reading the executable rather than by playing to the failure,
the page says so.
"""

BEGIN, END = "<!-- games:begin -->", "<!-- games:end -->"


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {wv} | {status} |\n"
        for g, engine, sym, fix, wv, status, page in GAMES)
    return f"{BEGIN}\n\n{HEAD}{rows}{NOTE}\n{END}"


def main():
    check = "--check" in sys.argv
    here = pathlib.Path(__file__).parent
    stale = []
    for page in sorted(here.glob("*.md")):
        text = page.read_text()
        if BEGIN not in text:
            continue
        new = re.sub(re.escape(BEGIN) + r".*?" + re.escape(END),
                     lambda _: table(), text, flags=re.S)
        if new == text:
            print(f"  up to date  {page.name}")
        elif check:
            stale.append(page.name)
        else:
            page.write_text(new)
            print(f"  rewritten   {page.name}")
    if stale:
        print(f"\nout of date: {', '.join(stale)}\nrun wiki/games.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
