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

# game, engine, symptom, fix, backend, dx, crossover, status, page
#
# The CrossOver cell is what a title was measured on, never what it might work
# on. "Preview" throughout is crossover-preview-arm64-20260821, which is the
# build every measurement here was taken against.
GAMES = [
    ("Mortal Shell 2", "Unreal Engine 5.6.1",
     "Crash on the first cutscene", "Runtime patch, 4 sites",
     "D3DMetal", "12", "26.3 · Preview",
     "Fixed", "Mortal-Shell-2"),
    ("Life is Strange: Reunion", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard",
     "D3DMetal", "12", "Preview -- 26.3 crashes",
     "Fixed", "Life-is-Strange-Reunion"),
    ("Life is Strange: Double Exposure", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard, same DLL",
     "D3DMetal", "12", "Preview -- 26.3 crashes",
     "Fixed", "Life-is-Strange-Double-Exposure"),
    ("DYNASTY WARRIORS: ORIGINS", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "Video bridge, D3D11 to D3D12",
     "D3DMetal", "12", "Preview -- 26.3 never launched",
     "Fixed", "Dynasty-Warriors-Origins"),
    ("Beast of Reincarnation", "Unreal Engine 5",
     "Startup video plays with sound, no picture", "NV12 restored, Electra forced to software",
     "D3DMetal", "12", "26.3 · Preview",
     "Fixed", "Beast-of-Reincarnation"),
    ("Persona 5 Strikers", "Koei Tecmo, in-house",
     "Video never starts; sound only", "Staged VC-1 codec, and a D3D9 to D3D11 bridge",
     "**DXMT**", "11", "Preview -- 26.3 not tried",
     "Fixed", "Persona-5-Strikers"),
]

HEAD = ("| Game | Engine | Symptom | Fix | Backend | DX | CrossOver | Status |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- |\n")

NOTE = """
**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers only works on DXMT: it needs a shared D3D9 surface handle, and DXMT
implements sharing where D3DMetal has none to build on. The other five run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and what "Preview" means.** Every measurement here was taken
against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the CrossOver
column says which of the two a title was measured on rather than which it might
work on. Everything runs on that Preview. Two are confirmed on 26.3 as well.
Both Life is Strange titles crash on 26.3, which is our defect and is open and
unexplained. Persona 5 Strikers has not been tried on stable and is expected to
work there, because it depends on no decoder of CrossOver's. DYNASTY WARRIORS
has never been launched on stable at all; what is claimed about it there is read
from the two installs' plugin sets.

**None of these games needs CrossOver patched, wherever the container can be
opened.** That was not true when this project started, and it is the single
biggest thing that changed. The qualifier is the whole of what remains, and it
is a container question rather than a codec one.

Both builds decode VP9 the same way; what only Preview can do is open a WebM,
which is the whole of the difference. DYNASTY WARRIORS ships 355 `.webm`
cutscenes and cannot get as far as decoding on stable, while Mortal Shell 2
ships the same codec in `.mp4`, which both builds handle. The plugin-by-plugin
comparison the conclusion rests on is in [Findings](Findings.md), under *The
container, not the codec*.

Persona 5 Strikers is the one title needing a codec no CrossOver ships -- VC-1 --
and that is staged beside it rather than patched into it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.
"""

BEGIN, END = "<!-- games:begin -->", "<!-- games:end -->"


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {backend} | {dx} | {cx} | {status} |\n"
        for g, engine, sym, fix, backend, dx, cx, status, page in GAMES)
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
