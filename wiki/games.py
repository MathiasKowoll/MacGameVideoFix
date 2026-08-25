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
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Mortal-Shell-2"),
    ("Life is Strange: Reunion", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Life-is-Strange-Reunion"),
    ("Life is Strange: Double Exposure", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard, same DLL",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Life-is-Strange-Double-Exposure"),
    ("DYNASTY WARRIORS: ORIGINS", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "Video bridge, D3D11 to D3D12",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Dynasty-Warriors-Origins"),
    ("Beast of Reincarnation", "Unreal Engine 5",
     "Startup video plays with sound, no picture", "NV12 restored, Electra forced to software",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Beast-of-Reincarnation"),
    ("Persona 5 Strikers", "Koei Tecmo, in-house",
     "Video never starts; sound only", "Staged VC-1 codec, and a D3D9 to D3D11 bridge",
     "**DXMT**", "11", "26.3 and Preview",
     "Fixed", "Persona-5-Strikers"),
    ("Nioh", "Koei Tecmo, in-house",
     "Cutscene refuses to play, then crashes", "Staged WMV3 codec, and the same D3D9 to D3D11 bridge",
     "**DXMT**", "11", "26.3 and Preview",
     "Fixed", "Nioh"),
    ("Nioh 2", "Koei Tecmo, in-house",
     "Cutscene refuses to play, then crashes", "Same codec and same bridge as Nioh, unchanged",
     "**DXMT**", "11", "26.3 and Preview",
     "Fixed", "Nioh-2"),
    ("Nioh 3", "Koei Tecmo, in-house",
     "Failed to play movie", "The DYNASTY WARRIORS bridge, unchanged",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Nioh-3"),
    ("Wo Long: Fallen Dynasty", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "The DYNASTY WARRIORS bridge, unchanged",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Wo-Long-Fallen-Dynasty"),
    ("NieR Replicant ver.1.22474487139", "Toylogic, in-house",
     "Crashes when the first video starts", "Software decode, and the frame written into the game's target",
     "D3DMetal", "11", "Preview -- not tried on 26.3",
     "Fixed", "NieR-Replicant"),
    ("KINGDOM HEARTS Dream Drop Distance", "Square Enix, in-house",
     "Cutscene runs with sound, picture solid green",
     "Software decode, and the luma and chroma planes written into the game's own textures",
     "D3DMetal", "11 + 12", "Preview -- not tried on 26.3",
     "Fixed", "Kingdom-Hearts"),
    ("KINGDOM HEARTS HD 1.5+2.5 ReMIX", "Square Enix, in-house",
     "Cutscene runs with sound, picture solid green",
     "The Dream Drop Distance fix, unchanged -- six executables, same route",
     "D3DMetal", "11 + 12", "Preview -- not tried on 26.3",
     "Fixed", "Kingdom-Hearts"),
    ("TMNT: Splintered Fate", "Rebirth, in-house",
     "Opens a window, then closes silently",
     "A guard on the D3D12 call that ends the process instead of failing",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "TMNT-Splintered-Fate"),
    ("Tormented Souls 2", "Unreal Engine 5",
     "Fatal error before the first frame",
     "16:9 modes added to a list that offered none",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "Tormented-Souls-2"),
    ("Devil May Cry 5", "RE Engine",
     "Crashes when a skill preview video plays",
     "Staged VC-1 codec. Nothing installed beside the game",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "RE-Engine-VC1"),
    ("RESIDENT EVIL 2", "RE Engine",
     "Crashes when a video plays",
     "The same staged VC-1 codec, unchanged",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "RE-Engine-VC1"),
    ("RESIDENT EVIL 3", "RE Engine",
     "Crashes when a video plays",
     "The same staged VC-1 codec, unchanged",
     "D3DMetal", "12", "26.3 and Preview",
     "Fixed", "RE-Engine-VC1"),
    ("NINJA GAIDEN 4", "Koei Tecmo, in-house",
     "Says the VP9 codec is missing, then exits",
     "Staged Matroska demuxer, and the MFT gate answered",
     "D3DMetal", "12", "26.3 only -- Preview stalls before video",
     "Fixed", "Ninja-Gaiden-4"),
]

HEAD = ("| Game | Engine | Symptom | Fix | Backend | DX | GPTK | CrossOver | Status |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n")

NOTE = """
**Update the toolkit, then pick a CrossOver.** Every fix here was written
against Apple's Game Porting Toolkit 4.0b2, which is what CrossOver Preview
ships and what CrossOver 26.3 does **not** -- 26.3 carries D3DMetal 3.0, and on
3.0 these patches do not find what they were written to find. So 26.3 is a
perfectly good engine for all of this once its toolkit is replaced, and a poor
one until then. The app does the replacing, and keeps the original beside it.

The exception is NINJA GAIDEN 4, which is the other way round: it runs on 3.0
and stalls on 4.0b2, before its first frame and for reasons inside the toolkit
that nothing here can reach.

**The GPTK column is the one that decides, and it is newer than this table.**
Apple's Game Porting Toolkit is what actually draws these games, and CrossOver
ships it inside the bundle rather than as something you pick: 26.3 carries
D3DMetal 3.0, Preview 27.0 carries 4.0b2 and uses it unless
`CX_GRAPHICS_BACKEND_VERSION` says otherwise. So "this only works on Preview"
has, for at least two titles here, meant "this needs the newer toolkit" and
nothing about Wine at all.

The two rows in bold are where that stops being a footnote. **NINJA GAIDEN 4
runs on 3.0 and stalls on 4.0b2. Life is Strange runs on 4.0b2 and crashes on
3.0.** Opposite requirements, same machine, so there is no single toolkit that
serves the whole table and no version of CrossOver that is simply "better".
Both were measured by moving the toolkit under a fixed CrossOver, which is the
only way to separate the two.

Everything else in the column is derived rather than freshly run: a title
measured on 26.3 was measured on 3.0, and one measured on Preview was measured
on 4.0b2. A cell naming one generation means the other was never tried, not that
it fails.

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
other nine run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and what "Preview" means.** Every measurement here was taken
against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the CrossOver
column says which of the two a title was measured on rather than which it might
work on. Everything runs on that Preview. Three are confirmed on 26.3 as well.
This project targets that Preview build, and it is what every title is measured
and supported on. Three also run on stable 26.3 and the column says so, but as a
bonus rather than a promise -- stable is not what gets tested before a release.

What stops the other three is in the engine. Both Life is Strange titles freeze
on 26.3 with the fix removed exactly as they do with it, so nothing installed
beside the game is involved; what differs is D3DMetal, 3.0 against 4.0b2. And
DYNASTY WARRIORS needs a WebM demuxer that 26.3 has no way to reach -- staging
the plugin was tried and the video still never starts. Persona 5 Strikers plays on both, which is what its
fix predicted: it stages its own decoder, so what CrossOver ships stops
mattering. A first attempt on 26.3 failed and was recorded as the title not
working there -- wrongly. The staged codec is built against one CrossOver and is
not usable under another, and none had been built for 26.3 yet.
DYNASTY WARRIORS crashes there too; that much was run, while the reason given for
it is read from the two installs' plugin sets rather than from watching it fail.

Four rows record an absence rather than a result: the three Nioh titles and
NieR Replicant were fixed on Preview and none was launched on 26.3, so nothing
is claimed either way.
Their codec is staged the same way Strikers' is, which is the half that made
Strikers portable, but the bridge half has only ever run against the Preview
build's DXMT.

**None of these games needs CrossOver patched, wherever the container can be
opened.** That was not true when this project started, and it is the single
biggest thing that changed. The qualifier is the whole of what remains, and it
is a container question rather than a codec one.

Both builds decode VP9 the same way, and for a long time what only Preview could
do was **open** a WebM -- which was the whole of the difference. DYNASTY WARRIORS
ships 355 `.webm` cutscenes and could not get as far as decoding on stable, while
Mortal Shell 2 ships the same codec in `.mp4`, which both builds handle. The
plugin-by-plugin comparison that conclusion rested on is in
[Findings](Findings.md), under *The container, not the codec*.

**That gap is now closed, and it was a missing plugin rather than a missing
engine.** Neither build ships a Matroska demuxer; Preview reached WebM by another
route. Staging `libgstmatroska` beside the decoder gives stable one too, and
NINJA GAIDEN 4 is where it was measured -- it plays on stock 26.3, video and all,
with nothing patched into CrossOver. What this means for the titles above has not
been re-measured: their rows still say what each was measured on, and DYNASTY
WARRIORS in particular deserves a fresh run on 26.3 before its row changes.

Three titles need a codec no CrossOver ships -- VC-1 for Persona 5 Strikers,
WMV3 for Nioh and Nioh 2 -- and it is staged beside the game rather than patched
into it. Nioh 3 needs none: its video is already NV12 by the time Media
Foundation is asked for it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.
"""

BEGIN, END = "<!-- games:begin -->", "<!-- games:end -->"


def gptk(cx, title=None):
    """Which Game Porting Toolkit a title was measured against.

    Not a new measurement: every run recorded in the CrossOver cell already
    carries this, unnamed. CrossOver 26.3 ships one toolkit and it is D3DMetal
    3.0; CrossOver Preview 27.0 ships two and defaults to 4.0b2. So a row
    measured on 26.3 was measured on 3.0, a row measured on Preview was measured
    on 4.0b2, and the pair says which generations a title is known to run on.

    Read as positive evidence only. "not tried on 26.3" contains the string
    "26.3" and means the opposite of having been measured there -- an earlier
    version of this function counted fifteen such rows as tested on both.

    Titles where both toolkits were tried deliberately are in GPTK_OVERRIDE.
    """
    if title in GPTK_BY_TITLE:
        return GPTK_BY_TITLE[title]
    if cx in GPTK_OVERRIDE:
        return GPTK_OVERRIDE[cx]
    denied = ("not tried on 26.3", "crashes on 26.3", "26.3 crashes")
    on263 = "26.3" in cx and not any(d in cx for d in denied)
    onprev = "Preview" in cx and "Preview stalls" not in cx \
             and "Preview not yet measured" not in cx
    if on263 and onprev: return "3.0 and 4.0b2"
    if on263:            return "3.0"
    if onprev:           return "4.0b2"
    return "not measured"


# Titles where both toolkits were tried on purpose. Keyed on the CrossOver cell
# so the two never drift apart.
GPTK_OVERRIDE = {
    "26.3 only -- Preview stalls before video": "**3.0 only** -- 4.0b2 stalls it",
}

# Titles measured on 26.3 only after its toolkit had been replaced with 4.0b2,
# which is not what that CrossOver ships. Deriving from the engine would read
# them as 3.0, and 3.0 is the one generation they were never tried on.
#
# Keyed by title rather than by the CrossOver cell, because several titles now
# share a cell and do not share a toolkit -- which is the whole reason this
# column exists.
GPTK_BY_TITLE = {
    "Nioh": "4.0b2",
    "Nioh 2": "4.0b2",
    "Nioh 3": "4.0b2",
    "Wo Long: Fallen Dynasty": "4.0b2",
    "TMNT: Splintered Fate": "4.0b2",
    "Tormented Souls 2": "4.0b2",
    "DYNASTY WARRIORS: ORIGINS": "4.0b2",
    "Life is Strange: Reunion": "**4.0b2 only** -- 3.0 crashes it",
    "Life is Strange: Double Exposure": "**4.0b2 only** -- 3.0 crashes it",
}


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {backend} | {dx} "
        f"| {gptk(cx, g)} | {cx} | {status} |\n"
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
