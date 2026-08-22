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
     "Fixed", "Mortal-Shell-2"),
    ("Life is Strange: Reunion", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard",
     "Fixed", "Life-is-Strange-Reunion"),
    ("Life is Strange: Double Exposure", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard, same DLL",
     "Fixed", "Life-is-Strange-Double-Exposure"),
    ("DYNASTY WARRIORS: ORIGINS", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "Video bridge, D3D11 to D3D12",
     "Fixed", "Dynasty-Warriors-Origins"),
    ("Beast of Reincarnation", "Unreal Engine 5",
     "Startup video plays with sound, no picture", "NV12 restored, Electra forced to software",
     "Fixed", "Beast-of-Reincarnation"),
    ("Persona 5 Strikers", "Koei Tecmo, in-house",
     "Video never starts; sound only", "Staged VC-1 codec, and a D3D9 to D3D11 bridge",
     "Fixed", "Persona-5-Strikers"),
]

HEAD = ("| Game | Engine | Symptom | Fix | Status |\n"
        "| --- | --- | --- | --- | --- |\n")

NOTE = """
**None of these games needs CrossOver patched.** That was not true when this
project started, and it is the single biggest thing that changed: CrossOver
Preview decodes VP9 profile 0 and 2, H.264 and AAC on its own. Persona 5
Strikers needs a VC-1 decoder CrossOver does not ship, and that is staged
beside it rather than patched into it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.
"""

BEGIN, END = "<!-- games:begin -->", "<!-- games:end -->"


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {status} |\n"
        for g, engine, sym, fix, status, page in GAMES)
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
