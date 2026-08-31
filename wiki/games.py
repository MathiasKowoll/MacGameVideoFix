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
import textwrap
import sys

# game, engine, symptom, fix, backend, dx, crossover, status, page
#
# The CrossOver cell is what a title was measured on, never what it might work
# on -- and there is one answer for every row now, so it is written once here
# and shared. The supported engine is stable CrossOver 26.3.0.39832 and no
# other: the winegstreamer pair this project ships was built against that
# engine, and install-engine-media.sh refuses any other version.
#
# The cell names the engine as well as the version, because a bare "26.3" reads
# as the build CodeWeavers ships and the engine these runs were made on is not
# that one: the version is stable 26.3, and the engine carries this project's
# winegstreamer. A filter that missed exactly that distinction once counted
# METAL GEAR SOLID: Peace Walker as running on a stock engine, against a
# measurement that a stock engine cannot play its cutscenes -- so the cell says
# both halves outright rather than leaving one of them to be inferred.
#
# It is not the Motor column and does not answer for one. This says what the
# runs were made on; Motor says what a fix needs an engine to carry, which for
# most rows is nothing.
#
# CrossOver Preview was an engine here until 2026-08-31 and is not one now. No
# cell names it as a place a title runs. The past-tense findings in the note
# below that mention it are kept as history and marked as history.
MEASURED = "26.3, our winegstreamer"

GAMES = [
    ("METAL GEAR SOLID: Peace Walker", "Konami, Master Collection",
     "Dies the moment a pre-rendered cutscene starts",
     "Engine patch mgvf-0001: 2D-capable buffers from the media source",
     "D3DMetal", "11", MEASURED,
     "Fixed -- crash cured; a green band in some cutscenes is unexplained", "Metal-Gear-Solid-Peace-Walker"),
    ("Mortal Shell 2", "Unreal Engine 5.6.1",
     "Crash on the first cutscene", "Runtime patch, 4 sites",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Mortal-Shell-2"),
    ("Life is Strange: Reunion", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard",
     "D3DMetal", "12", MEASURED,
     "Runaway node walk stopped; no freeze-free session recorded", "Life-is-Strange-Reunion"),
    ("Life is Strange: Double Exposure", "Unreal Engine 5",
     "Freezes after a while, anywhere", "DXGI node guard, same DLL",
     "D3DMetal", "12", MEASURED,
     "Guard installs; the freeze was never reproduced or cured", "Life-is-Strange-Double-Exposure"),
    ("DYNASTY WARRIORS: ORIGINS", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "Video bridge, D3D11 to D3D12",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Dynasty-Warriors-Origins"),
    ("Beast of Reincarnation", "Unreal Engine 5",
     "Startup video plays with sound, no picture",
     "Console variable, and two IsSoftware call sites patched by address; **needs winevideo**",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Beast-of-Reincarnation"),
    ("Persona 5 Strikers", "Koei Tecmo, in-house",
     "Video never starts; sound only", "Staged VC-1 codec, and a D3D9 to D3D11 bridge",
     "**DXMT**", "11", MEASURED,
     "Fixed", "Persona-5-Strikers"),
    ("NINJA GAIDEN 3: Razor's Edge", "Koei Tecmo, in-house",
     "Will not start: \"Insufficient VRAM\"", "d9vk, and winevideo's DirectShow filters",
     "**DXVK**", "9", MEASURED,
     "Starts, 60 fps, in-game cutscenes; the boot movie freezes, one click skips it", "Ninja-Gaiden-3-Razors-Edge"),
    ("Nioh", "Koei Tecmo, in-house",
     "Cutscene refuses to play, then crashes", "Staged WMV3 codec, and the same D3D9 to D3D11 bridge",
     "**DXMT**", "11", MEASURED,
     "Fixed", "Nioh"),
    ("Nioh 2", "Koei Tecmo, in-house",
     "Cutscene refuses to play, then crashes", "Same codec and same bridge as Nioh, unchanged",
     "**DXMT**", "11", MEASURED,
     "Fixed", "Nioh-2"),
    ("Nioh 3", "Koei Tecmo, in-house",
     "Failed to play movie", "The DYNASTY WARRIORS bridge, with ordinal hooking added for this title",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Nioh-3"),
    ("Wo Long: Fallen Dynasty", "Koei Tecmo, in-house",
     "Cutscene runs with sound, picture black", "The DYNASTY WARRIORS bridge, with ordinal hooking",
     "D3DMetal", "12", MEASURED,
     "Bridge installs; picture measured only on a patched engine", "Wo-Long-Fallen-Dynasty"),
    ("NieR Replicant ver.1.22474487139", "Toylogic, in-house",
     "Crashes when the first video starts", "Software decode, and the frame written into the game's target",
     "D3DMetal", "11", MEASURED,
     "Fixed", "NieR-Replicant"),
    ("KINGDOM HEARTS Dream Drop Distance", "Square Enix, in-house",
     "Cutscene runs with sound, picture solid green; a crash dialog on leaving",
     "Software decode with the planes written into the game's own textures, and the shutdown fault swallowed",
     "D3DMetal", "11 + 12", MEASURED,
     "Picture restored; the exit dialog was measured on 1.5+2.5", "Kingdom-Hearts"),
    # Its own row while its fault is its own. The 2.8 package holds both this and
    # Dream Drop Distance, and one row for the pair hid a working title behind a
    # broken one. When 0.2 works, fold this back into the entry above -- which is
    # how it was, and where it belongs once they behave alike.
    ("KINGDOM HEARTS 0.2 Birth by Sleep", "Unreal Engine 4",
     "Would not start from a launcher; ran fine launched by hand",
     "**None from us.** The launcher had to declare microphone use -- see the page",
     "D3DMetal", "11", MEASURED,
     "Works with nothing of ours -- fixed in the launcher", "Kingdom-Hearts"),
    ("KINGDOM HEARTS HD 1.5+2.5 ReMIX", "Square Enix, in-house",
     "Cutscene runs with sound, picture solid green; a crash dialog on leaving",
     "The Dream Drop Distance fix, unchanged -- six executables, same route",
     "D3DMetal", "11 + 12", MEASURED,
     "Fixed", "Kingdom-Hearts"),
    ("TMNT: Splintered Fate", "Rebirth, in-house",
     "Opens a window, then closes silently",
     "A guard on the D3D12 call that ends the process instead of failing",
     "D3DMetal", "12", MEASURED,
     "Fixed", "TMNT-Splintered-Fate"),
    ("Tormented Souls 2", "Unreal Engine 5",
     "Fatal error before the first frame",
     "16:9 modes added to a list that offered none",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Tormented-Souls-2"),
    ("Devil May Cry 5", "RE Engine",
     "Crashes when a skill preview video plays",
     "Staged VC-1 codec. Nothing installed beside the game",
     "D3DMetal", "12", MEASURED,
     "Fixed", "RE-Engine-VC1"),
    ("RESIDENT EVIL 2", "RE Engine",
     "Crashes when a video plays",
     "The same staged VC-1 codec, unchanged",
     "D3DMetal", "12", MEASURED,
     "Fixed", "RE-Engine-VC1"),
    ("RESIDENT EVIL 3", "RE Engine",
     "Crashes when a video plays",
     "The same staged VC-1 codec, unchanged",
     "D3DMetal", "12", MEASURED,
     "Fixed", "RE-Engine-VC1"),
    ("NINJA GAIDEN 4", "Koei Tecmo, in-house",
     "Says the VP9 codec is missing, then exits",
     "Staged Matroska demuxer, and the MFT gate answered",
     "D3DMetal", "12", MEASURED,
     "Fixed", "Ninja-Gaiden-4"),
    ("RESONANCE: A PLAGUE TALE LEGACY", "Asobo, in-house",
     "Fatal error: Shader Model 6.7 is not supported", "Shader model floor lowered in memory; needs a 16:9 display",
     "D3DMetal", "12", MEASURED,
     "Starts on a 16:9 display -- its cutscenes have never been visible", "Resonance-A-Plague-Tale-Legacy"),
]

HEAD = ("| Game | Engine | Symptom | Fix | Backend | DX | GPTK | Motor | CrossOver | Status |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n")

NOTE = """
**One engine, and update its toolkit.** The supported engine is stable
CrossOver 26.3.0.39832 and nothing else: the winegstreamer pair this project
ships was built against that engine, and `install-engine-media.sh` refuses any
other version rather than installing onto a wine it was not compiled for. Every
fix here was written against Apple's Game Porting Toolkit 4.0b2, which 26.3 does
**not** ship -- it carries D3DMetal 3.0, and on 3.0 these patches do not find
what they were written to find. So 26.3 is a perfectly good engine for all of
this once its toolkit is replaced, and a poor one until then. The app does the
replacing in a copy of the CrossOver you point it at, and keeps both halves of
the original inside that copy, so on that route the CrossOver you installed is
not touched. With the copy turned off in Set up, a toolkit you ask for goes into
the CrossOver you installed instead, and the app says so where that choice is
made.

The exception is NINJA GAIDEN 4, which is the other way round: it is measured
working on 3.0, and on 4.0b2 its cutscene plays its audio and no picture
appears. It does not stall there and it does not exit; where the frames stop
has not been established.

**The GPTK column is the one that decides.**
Apple's Game Porting Toolkit is what actually draws these games, and CrossOver
ships it inside the bundle rather than as something you pick: 26.3 carries
D3DMetal 3.0. A launcher can put another generation in front of it -- RaccoonBot
carries d3dMetal3 and d3dMetal4 side by side and injects one at launch -- and
that is how the rows needing 4.0b2 run on this engine. So "this only works on
the Preview build" meant, for {gptk_n} of the rows here, "this needs the newer
toolkit" and nothing about Wine at all -- which is why dropping Preview costs
the table nothing.

Those {gptk_n} rows in bold are where that stops being a footnote, and they
fall into two camps pointing opposite ways. **NINJA GAIDEN 4 is measured working
on 3.0, and on 4.0b2 its video has sound and no picture. Life is Strange -- both
packages -- runs on 4.0b2 and crashes
on 3.0.** Opposite requirements, same machine, so there is no single toolkit
that serves the whole table.
Both were measured by moving the toolkit under a fixed CrossOver, which is the
only way to separate the two.

**Everywhere else the column says "not measured", and that is deliberate.**
4.0b2 is what these titles need as a general rule, with NINJA GAIDEN 4 the
exception at 3.0 -- but a rule is not a run, and a row whose toolkit nobody
varied has no measurement of its own to report. The cells that used to hold one
were derived from the CrossOver build rather than run, and that derivation is
gone: a launcher chooses the generation, so the build implies nothing about it.

**Backend and DX are not preferences, they are requirements.** Persona 5
Strikers, Nioh and Nioh 2 only work on DXMT: all three need a shared D3D9
surface handle, and DXMT implements sharing where D3DMetal has none to build on.
Nioh 3, despite the name, belongs with the other group -- it is D3D12 on
D3DMetal and never touches D3D9, and NieR Replicant is D3D11 on D3DMetal. The
rest run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and one answer for every row.** Every measurement here was
taken on stable CrossOver 26.3, on an engine carrying this project's
winegstreamer, and the column says exactly that on every row because it is the
same answer on every row. Both halves of it are load-bearing: the version is
stable 26.3, and the engine carries this project's winegstreamer rather than
the one CodeWeavers ships. What a fix needs an engine to carry is a different
question, and the Motor column answers it per row.

**CrossOver Preview is no longer a supported engine.** It was measured against
here, and on 2026-08-31 it was dropped rather than half-supported: only titles
patched without winegstreamer could have worked on it, and the rest were out of
the equation. The findings below that mention it are kept, because they record
what was measured and a deleted measurement is not a correction. They are
history. None of them is guidance about where to run a game.

**The KINGDOM HEARTS 2.8 package holds three entries, not two** -- Dream Drop
Distance, 0.2 Birth by Sleep and the Back Cover film -- and they do not share a
fault. Dream Drop Distance needs the bridge and has it. 0.2 needs nothing at
all: no installer here has ever covered it, its executable lives in a subfolder
this project does not reach, and launched directly it runs and plays its video,
four times out of four.

**0.2 would not start from a launcher because the launcher declared no
microphone use.** Steam initialises voice detection at startup; without
`NSMicrophoneUsageDescription` macOS can neither prompt nor grant, the request
never resolves, and Steam's own main loop wedges -- its assertion says so. A
stalled Steam never answers the second request this package makes of it, which
is why no ordinary title showed the same fault. Declaring the permission fixed
it, and fixes any title that touches the microphone.

Its status says Fixed because this column answers "does the title work", and it
does. Its **Fix** column says "none needed", and that is the half to read before
concluding anything was shipped for it: nothing was. Every other Fixed row in
this table names something this project installs; this one names nothing.

**History: NINJA GAIDEN 4 and Beast of Reincarnation were both measured
stalling on Preview**, and both were recorded working on 26.3, which is the
opposite direction to the one this project expected. NINJA GAIDEN 4 stalled
there before any video call, with no thread in it touching D3D12, DXGI, Media
Foundation or winegstreamer, and what held it was never established. Beast of
Reincarnation stalled there as well, and carries a separate requirement of its
own: it needs winevideo since the game update of 2026-08-24. The shared lesson
is that "runs on Preview" was never the safe assumption this project began
with.

Two lessons paid for by rows that were wrong for a while, and are worth more
than the statuses they corrected:

- **A staged codec is built against one CrossOver and is not usable under
  another.** Persona 5 Strikers was recorded as not working on 26.3 after a
  first attempt failed there; the codec simply had not been built for 26.3 yet.
- **A fix that reports itself installed is not necessarily loaded.** NieR
  Replicant was recorded as Preview-only because its 26.3 runs died at the first
  video. The bridge was never executing in those runs: the registry override it
  depends on had gone missing, and the installer answered `installed` from the
  files alone.

**The Motor column says what the engine itself has to carry, and for most rows
it is nothing.** Those ask nothing of ours from the engine: the fix sits beside
the game and, where the video needs one, a plugin goes in front of it. That was
not true when this project started, and it is the single biggest thing that
changed. The exceptions are the rows that name something instead: Beast of
Reincarnation needs a winegstreamer carrying winevideo's patches, and METAL GEAR
SOLID: Peace Walker is the first title here whose whole fix is an engine patch
of ours, with nothing installed beside the game at all -- it was tried on a
stable 26.3 as CodeWeavers ships it and could not play its cutscenes there,
which is the measurement that puts **Ours** in its cell.

**A dagger on "Stock" means inferred rather than run.** No run on those rows
isolated the engine's own `winegstreamer` as the thing that carried the video,
so Stock there is read off the mechanism rather than established. That is not a
smaller claim than Stock; it is an untested one, and the column says which rows
it applies to. It is not a statement about the CrossOver cell beside it. That
cell records what the runs were made on -- stable 26.3 with this project's
winegstreamer in the engine, on every row -- and Motor records what a fix needs
an engine to carry. A row reading Stock in one and our winegstreamer in the
other is not two claims in contradiction: the runs were made on the engine this
project supports, and Stock says the fix does not depend on it.

**History, and the finding that closed a gap.** Both builds decoded VP9 the
same way, and for a long time what only Preview could do was **open** a WebM --
which was the whole of the difference. DYNASTY WARRIORS ships 355 `.webm`
cutscenes and could not get as far as decoding on stable, while Mortal Shell 2
ships the same codec in `.mp4`, which both builds handled. The plugin-by-plugin
comparison that conclusion rested on is in [Findings](Findings.md), under *The
container, not the codec*.

**That gap is closed, and it was a missing plugin rather than a missing
engine.** Stable 26.3 ships `libgstmatroska` for neither architecture and the
Preview build shipped it for both -- so the difference between them on a WebM
was one plugin the whole time. Staging it beside the decoder gives 26.3 one
too, and NINJA GAIDEN 4 is where that was measured: it plays on stock 26.3,
video and all, with nothing patched into CrossOver.

Several titles need a codec no CrossOver ships -- VC-1, WMV3, WMV2 or WMA --
and where it comes from depends on the engine. On a stock CrossOver the plugin
is staged from a GStreamer runtime you installed, one staging per engine, with
the bottle pointed at it; on an engine copy this project makes, the same plugins
are already inside the engine's own `lib64/gstreamer-1.0` and nothing is staged
at all. Neither route patches a decoder into a CrossOver you installed. Which
titles, and which plugin each one needs, is the Codec column of
[what each title actually loads](Games.md#what-each-title-actually-loads); the
count is derived there rather than repeated here, because the number written
here was three for as long as it took two more titles to join the list. Nioh 3
needs none: its video is already NV12 by the time Media Foundation is asked for
it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.
"""


WORDS = ("zero one two three four five six seven eight nine ten eleven twelve "
         "thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty "
         "twenty-one twenty-two twenty-three twenty-four twenty-five twenty-six "
         "twenty-seven twenty-eight twenty-nine thirty").split()


def _word(n):
    """The count as this project writes counts: a word while there is one."""
    return WORDS[n] if n < len(WORDS) else str(n)


# A table of phrases sat here that sorted the CrossOver cells into the rows
# measured on a stable 26.3 as CodeWeavers ships it and the rows measured on
# something this project had patched, and a paragraph was rendered from it
# naming each row and the claim its cell made. It sorted nothing once the cells
# became one cell, and a filter over a single value renders a sentence that
# says the same thing about every row or, worse, nothing at all.
#
# Its lesson is worth more than the code was, so it is kept here: three
# different claims were living in one column and the prose had to say which one
# it was holding. "Not tried" is an absence -- nobody made the run. "Stalls" and
# "freezes" are results, and somebody watched them happen. A run made on an
# engine this project had patched is a third thing again, and the bug that list
# carried into release was reading that third kind as the first: a cell saying
# "26.3 only on a patched engine" matched none of the phrases, so METAL GEAR
# SOLID: Peace Walker counted as running on stable -- against a measurement
# that a stock CrossOver cannot play its cutscenes.
#
# That is why MEASURED names the engine and not only the version. The
# distinction now lives in the cell instead of in a filter over it.


def _note():
    """Fill the note's counts from the rows, so it cannot say nineteen at 21.

    This paragraph sat inside the generated block as a literal for two titles
    longer than it was true. Being inside the markers is what made it look safe
    -- hand_counts() skips generated blocks precisely because they are supposed
    to be rewritten from the rows every run, and this one was not.
    """
    # One count survives the move to one engine: the rows whose toolkit is a
    # requirement rather than something they happened to be run on. The rest of
    # what used to be counted here -- how many rows ran on a stable engine, how
    # many stalled on the other build -- was a count of a distinction the
    # CrossOver cell no longer draws.
    gptk_req = [g for g in GAMES if "only" in gptk(g[0])]
    values = dict(gptk_n=_word(len(gptk_req)))
    # Substituting a word of a different length leaves the paragraph ragged, so
    # the ones that take a value are rewrapped and the rest are left exactly as
    # they were written. Only prose is touched: a bullet reflowed into the
    # paragraph above it would be a worse bug than a long line.
    out = []
    for block in NOTE.split("\n\n"):
        if "{" not in block or block.lstrip().startswith(("- ", "* ", "|", "    ")):
            out.append(block)
            continue
        out.append(textwrap.fill(" ".join(block.split()).format(**values),
                                 width=78, break_long_words=False,
                                 break_on_hyphens=False))
    return "\n\n".join(out)


# --------------------------------------------------------------- the stack
#
# The table above answers "does this title work". This one answers a different
# question that used to need reading five files to settle: what does a title
# actually load, and which half of it is ours.
#
# NOTHING BELOW IS TYPED TWICE. Every cell but one is read from the file that
# already decides it -- the app for which installer serves a title, that
# installer for its carrier and whether it writes a registry key, the shipped
# DLL for which bridge it was built from, and that bridge's source for the
# environment variables it can read. Copying those into a tuple here would put
# them on course to disagree with the code by the following week, which is the
# failure this whole file exists to prevent.
#
# The exception is the codec column. Which plugin a title needs follows from the
# format of its video, and no file in the repository records that -- so it is
# stated here, and a title nobody has looked at says so rather than inheriting
# its neighbour's answer.

REPO = pathlib.Path(__file__).resolve().parent.parent
NONE, UNKNOWN = "—", "not measured"

CODEC = {
    # Some titles need a decoder CrossOver does not ship and some need a
    # demuxer; the counts are derived below rather than written here, because
    # the ones written here went stale the moment a title changed group. NieR
    # joined the decoder group by measurement, having been filed for a long time
    # as needing nothing: its video is WMV2 with WMA v2 audio in an ASF that
    # starts sixteen bytes into the game's own .arc.
    #
    # Nothing is installed for this title anywhere: no carrier, no bridge, and
    # nothing staged. Its fix IS the engine -- winegstreamer carrying mgvf-0001
    # -- and the engine that carries it also carries the three plugins in its
    # own lib64/gstreamer-1.0, so "which plugin must be staged" has no answer
    # here rather than an unmeasured one. It is not NONE either: that would read
    # as "measured, and none needed", which no run here could have shown.
    "METAL GEAR SOLID: Peace Walker": "**in the engine**",
    "Persona 5 Strikers": "`libgstlibav`",
    "Nioh": "`libgstlibav`",
    "Nioh 2": "`libgstlibav`",
    "Devil May Cry 5": "`libgstlibav`",
    "RESIDENT EVIL 2": "`libgstlibav`",
    "RESIDENT EVIL 3": "`libgstlibav`",
    "NieR Replicant ver.1.22474487139": "`libgstlibav`",
    "NINJA GAIDEN 4": "`libgstmatroska`",
    "DYNASTY WARRIORS: ORIGINS": "`libgstmatroska`",
    # Stated absences, each one measured: the reader reports a native type the
    # engine can already produce. The two Kingdom Hearts are on the same
    # footing -- H.264 with AAC in MP4, which every CrossOver decodes, and
    # install-kh-bridge.sh says so in as many words when it finishes.
    "KINGDOM HEARTS Dream Drop Distance": NONE,
    "KINGDOM HEARTS HD 1.5+2.5 ReMIX": NONE,
    "Mortal Shell 2": NONE,
    "Wo Long: Fallen Dynasty": NONE,
    "Nioh 3": NONE,
    "RESONANCE: A PLAGUE TALE LEGACY": NONE,
    "Beast of Reincarnation": NONE,
    "Life is Strange: Reunion": NONE,
    "Life is Strange: Double Exposure": NONE,
    "TMNT: Splintered Fate": NONE,
    "Tormented Souls 2": NONE,
}


def _switch(prop):
    """{case name: returned string} for one switch in the app's SupportedGame.

    Read rather than mirrored. The app is where a title is bound to its
    installer, and a second copy of that binding here would be a second thing
    to remember to change.
    """
    text = (REPO / "app" / "MacGameVideoFix.swift").read_text()
    start = text.index("var %s: String {" % prop)
    body = text[start:text.index("\n    }", start)]
    out, fallback = {}, None
    for cases, value in re.findall(r'case ([^:\n]+):\s*return "([^"]*)"', body):
        for c in cases.split(","):
            out[c.strip().lstrip(".")] = value
    d = re.search(r'default:\s*return "([^"]*)"', body)
    if d:
        fallback = d.group(1)
    return out, fallback


def _sources():
    """{log file name: (directory, source file)} for every fix in the tree.

    A shipped DLL names the log it writes, and that string is the only thing
    tying a binary back to the C it was built from -- the same trick every
    installer's is_ours() uses to recognise its own work.
    """
    out = {}
    for d in ("runtime", "diagnostics"):
        for src in sorted((REPO / d).glob("*.c")):
            for mark in re.findall(r'"([A-Za-z0-9_.\\:-]+\.log)"', src.read_text()):
                out.setdefault(mark.split("\\")[-1], (d, src.name))
    return out


def _carrier(script):
    """(carrier DLL, name the original is kept under, proxy, registry) ."""
    text = (REPO / "runtime" / script).read_text()

    def base(*names):
        for n in names:
            m = re.search(r'^%s="[^"]*?([^"/{}]+)"' % n, text, re.M)
            if m:
                return m.group(1)
        return ""

    proxy = ""
    m = re.search(r'^PROXY="(?:\$\{PROXY_DLL:-)?\$HERE/([^"}]+)', text, re.M)
    if m:
        proxy = m.group(1)
    # Whether the fix needs Wine told to prefer it. Only the carriers Wine
    # implements itself need this, and it is the part that goes missing.
    return base("LIVE", "CARRIER"), base("REAL"), proxy, ("yes" if "reg.exe" in text else NONE)


def _bridge(proxy):
    """(source the proxy was built from, environment variables it reads)."""
    path = REPO / "runtime" / proxy
    if not proxy or not path.exists():
        return "", ""
    blob = path.read_bytes()
    for mark, (folder, name) in sorted(_sources().items()):
        if mark.encode() not in blob:
            continue
        src = (REPO / folder / name).read_text()
        levers = sorted(set(re.findall(r'GetEnvironmentVariable[AW]?\(\s*"([A-Z0-9_]+)"', src))
                        | set(re.findall(r'getenv\(\s*"([A-Z0-9_]+)"', src)))
        return name, ", ".join("`%s`" % v for v in levers) or NONE
    return "", ""


STACK_HEAD = (
    "| Game | Backend | DX | GPTK | Carrier | Kept as | Bridge | Codec | "
    "Env levers | Registry |\n"
    "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n"
)

def _stack_note(bridges, decoders, demuxers, registry):
    """The note, with every count derived from the rows above it.

    The first version of this typed "four sources" and "six titles", and both
    were wrong within the hour -- NINJA GAIDEN 4 had already brought a fifth
    source and NieR a seventh decoder. Writing a number beside a generated table
    is the same mistake the table exists to prevent, one paragraph lower.
    """
    return f"""
**Carrier** is the DLL the fix rides on -- one the game already loads, chosen
because it has nothing to do with video. **Kept as** is what the original is
renamed to; where the carrier is a DLL Wine implements itself, that original is
a copy taken from your own CrossOver and nothing is redistributed. **Bridge** is
the source the shipped proxy was built from: {bridges} sources serve every title
here, so a fault found in one is often already fixed in the others.

**Registry** says whether Wine has to be told to prefer our DLL. {registry} fixes
need it, and for the same reason: their carrier is `dinput8`, which Wine
implements, so without the override the file beside the game is never opened.
That key is the part that goes missing on its own -- a bottle reset, a bottle
made after a CrossOver upgrade -- and a fix whose files are present is not
therefore a fix that is running. Both installers check the registry now, and
answer `broken` rather than `installed` when it is gone.

**Env levers** are the environment variables the shipped DLL reads. They are
levers, not requirements: each one defaults to the setting the fix was measured
with, and they exist so a failing title can be bisected without a rebuild.

**Codec** is the plugin that has to be in front of CrossOver before the title
can play. {decoders} titles need a decoder no CrossOver ships, {demuxers} need a
demuxer, and telling those two cases apart is most of the work -- see
[How the codec staging works](How-the-codec-staging-works.md). Stable 26.3
ships no `matroska` plugin at all, so the demuxer rows say what has to be put
in front of it.

Where it comes from depends on the engine. On a stock CrossOver
`stage-codecs.sh` puts it there; on one of this project's engine copies the
plugins are already inside the engine, and that script finds them and stands
down, because a second copy on the search path means two GStreamer cores in one
process. The row marked **in the engine** is METAL GEAR SOLID: Peace Walker,
where that is the whole fix: nothing beside the game, and nothing staged.
"""


STACK_BEGIN, STACK_END = "<!-- stack:begin -->", "<!-- stack:end -->"


def stack():
    names, _ = _switch("name")
    installers, fallback = _switch("installer")
    by_title = {title: case for case, title in names.items()}
    rows, bridges_seen, registry_seen = "", [], []
    for g, _engine, _sym, _fix, backend, dx, _cx, _status, page in GAMES:
        case = by_title.get(g)
        script = installers.get(case, fallback) if case else None
        if script:
            carrier, kept, proxy, registry = _carrier(script)
            bridge, levers = _bridge(proxy)
        else:
            # No case in the app means no proxy by design: these titles need the
            # staged codec and nothing beside the game.
            carrier = kept = bridge = ""
            levers = registry = NONE
        bridges_seen.append(bridge)
        # Counted by installer, not by row: the two Kingdom Hearts packages
        # share one script, and saying "three fixes" for two would be the same
        # hand-counting error one paragraph lower.
        if registry == "yes":
            registry_seen.append(script)
        rows += (
            f"| [{g}]({page}.md) | {backend} | {dx} | {gptk(g)} "
            f"| {'`%s`' % carrier if carrier else NONE} "
            f"| {'`%s`' % kept if kept else NONE} "
            f"| {'`%s`' % bridge if bridge else NONE} "
            f"| {CODEC.get(g, UNKNOWN)} | {levers or NONE} | {registry} |\n"
        )
    note = _stack_note(
        bridges=len({b for b in bridges_seen if b}),
        decoders=sum(1 for v in CODEC.values() if "libav" in v),
        demuxers=sum(1 for v in CODEC.values() if "matroska" in v),
        registry=len(set(registry_seen)),
    )
    return f"{STACK_BEGIN}\n\n{STACK_HEAD}{rows}{note}\n{STACK_END}"


BEGIN, END = "<!-- games:begin -->", "<!-- games:end -->"


# ------------------------------------------------------------------ engine
#
# Which winegstreamer a reader must have. The CrossOver cell says what the runs
# were made on, which is one answer for every row; it has never said whether a
# particular fix NEEDS anything of this project inside the engine, and three
# different things were being conflated in one word.
#
#   Stock      any CrossOver of the stated version, untouched
#   winevideo  needs a winegstreamer carrying winevideo's patches -- their
#              engine, or a rebuild of that same patch set
#   Ours       needs the winegstreamer scripts/build-winegstreamer.sh produces,
#              with a patch that exists only here (source-patches/mgvf-0001)
#   None       no Wine engine is involved in the fault or in the fix
#
# The dagger means: inferred from the mechanism, and not established by a run
# that isolated the engine. The rows carrying it are not making a smaller claim
# than "Stock" -- they are making an untested one.
#
# It is not a statement about the CrossOver cell, and it was written as one for a
# while: "nobody has watched them play on an untouched engine" was contradicted
# by rows it covered whose cells recorded plain runs. That cell now records the
# same engine for every row, so it cannot answer for a single row's requirement
# at all -- the two columns answer different questions, and this is the one that
# answers what a fix needs.

ENGINE_NEEDED = {
    "METAL GEAR SOLID: Peace Walker": "**Ours**",
    "Beast of Reincarnation": "winevideo",
    "KINGDOM HEARTS 0.2 Birth by Sleep": "None",
    "DYNASTY WARRIORS: ORIGINS": "Stock&dagger;",
    "Persona 5 Strikers": "Stock&dagger;",
    "Nioh": "Stock&dagger;",
    "Nioh 2": "Stock&dagger;",
    "Nioh 3": "Stock&dagger;",
    "Wo Long: Fallen Dynasty": "Stock&dagger;",
    "KINGDOM HEARTS Dream Drop Distance": "Stock&dagger;",
}


def engine_needed(title):
    """Stock unless a fix demonstrably needs something inside the engine."""
    return ENGINE_NEEDED.get(title, "Stock")


def gptk(title):
    """Which Game Porting Toolkit a title was measured against.

    4.0b2 is what these titles need as a general rule and NINJA GAIDEN 4 is the
    exception at 3.0, but a rule is not a run: this reports only what
    GPTK_BY_TITLE holds. A title with no toolkit measurement of its own says so,
    and the note beside the table states the rule once rather than rendering it
    into every cell as if somebody had watched it.

    This used to derive a generation from the CrossOver cell -- 26.3 read as
    3.0, Preview read as 4.0b2 -- and that derivation went with the second
    build. It could not have survived it anyway.

    **The toolkit is not the engine, and this column reads as if it were.**
    That inference holds only where the two arrive together, which is a stock
    CrossOver. A launcher that carries both toolkits and injects one at launch
    -- RaccoonBot ships d3dMetal3 and d3dMetal4 side by side -- breaks it, and
    then reading "needs 4.0b2" as "needs Preview" is simply wrong. It was read
    that way once, to conclude that dropping Preview would cost the two Life is
    Strange titles. It would not: they need the toolkit, and the launcher
    supplies it on 26.3.

    process-features.json keeps them apart, which is what a launcher able to
    choose actually needs: `gptk` says which generation a title was measured
    on, `needs_engine` says when an engine itself is required.
    """
    return GPTK_BY_TITLE.get(title, UNKNOWN)


# A second table keyed on the CrossOver cell used to sit here, holding NINJA
# GAIDEN 4's toolkit finding. It was dead -- GPTK_BY_TITLE below answers first
# for that title -- and what it held was the retracted "4.0b2 stalls it",
# waiting to be rendered again the day somebody removed the entry that shadowed
# it. Keying on the cell was the other half of the mistake: every title shares
# one cell now, and they do not share a toolkit.

# Every toolkit measurement there is, and nothing else. Most of these are titles
# measured on 26.3 with its toolkit replaced by 4.0b2, which is not what that
# CrossOver ships -- reading the generation off the engine would have called
# them 3.0, and 3.0 is the one generation they were never tried on.
#
# A title absent from here has no toolkit measurement of its own and renders as
# "not measured". That is not the same as having no answer: 4.0b2 is the rule
# for these titles and NINJA GAIDEN 4 is the exception, and the note beside the
# table says so once. Writing the rule into a cell would dress it as a run.
GPTK_BY_TITLE = {
    # Not a toolkit finding at all, and nothing here may manufacture one. What
    # was varied for this title was the engine, and that is what the Motor
    # column records. No generation is claimed because none was measured.
    "METAL GEAR SOLID: Peace Walker": "not measured -- the engine was the variable",
    "Nioh": "4.0b2",
    "Nioh 2": "4.0b2",
    "Nioh 3": "4.0b2",
    "Wo Long: Fallen Dynasty": "4.0b2",
    "TMNT: Splintered Fate": "4.0b2",
    "Tormented Souls 2": "4.0b2",
    "DYNASTY WARRIORS: ORIGINS": "4.0b2",
    # Was recorded as running on 4.0b2 under Wine 27 and dying on the same
    # toolkit under Wine 26.3. It was not the Wine: the registry override its
    # bridge depends on had gone missing, so the 26.3 runs measured the game
    # with no fix loaded. With the key written it runs on both.
    "NieR Replicant ver.1.22474487139": "4.0b2",
    # The other direction, and the only title that goes this way. Most rows here
    # want 4.0b2; this one is measured working on 3.0, and on 4.0b2 -- on a
    # patched 26.3, that generation selected and saved -- it runs, its cutscene
    # plays its audio and no picture appears. It neither stalls nor exits there.
    #
    # Two earlier values are wrong and neither should come back. "4.0b2 stalls
    # it" is retracted; it was never measured. "Preview stalls before video" is
    # measured but names a CrossOver build, which is what the CrossOver cell is
    # for -- putting it here said nothing about the toolkit twice over.
    "NINJA GAIDEN 4": "**3.0 only** -- on 4.0b2 the video has sound and no picture",
    "Life is Strange: Reunion": "**4.0b2 only** -- 3.0 crashes it",
    "Life is Strange: Double Exposure": "**4.0b2 only** -- 3.0 crashes it",
}


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {backend} | {dx} "
        f"| {gptk(g)} | {engine_needed(g)} | {cx} | {status} |\n"
        for g, engine, sym, fix, backend, dx, cx, status, page in GAMES)
    return f"{BEGIN}\n\n{HEAD}{rows}{_note()}\n{END}"


# ------------------------------------------------------------------ README
#
# The README carried its own copy of the table, kept by hand, with absolute
# links because it is read on the front page rather than inside the wiki. It fell
# a row behind the wiki -- NINJA GAIDEN 4 was fixed, shipped and documented, and
# never reached the one table most people see first.
#
# Same rows, same source, different link style. The symptom column is shortened
# the way the README already shortened it: the front page is a list, not a
# report. It carries Status and Motor, and neither is decoration; see below.

WIKI = "https://github.com/MathiasKowoll/MacGameVideoFix/wiki"
README_BEGIN, README_END = "<!-- readme-games:begin -->", "<!-- readme-games:end -->"


def readme_table():
    """The README's table carries Status and Motor.

    It carried neither, so a row could be measured not working and still read
    here as one of the fixed ones -- the caveat rode entirely in the CrossOver
    cell, which a person skimming a list of games will not parse. The README is
    the front door; a title under investigation has to say so where somebody
    decides whether to try it.

    Motor is here for the same reason and was the sharper gap: METAL GEAR SOLID:
    Peace Walker read "26.3 | Fixed" on the front page with nothing saying its
    fix is an engine this project builds. Beast of Reincarnation only escaped
    that by happening to carry its requirement inside its CrossOver cell.
    """
    rows = "".join(
        f"| [**{g}**]({WIKI}/{page}) | {sym} | {cx} | {engine_needed(g)} | {status} |\n"
        for g, engine, sym, fix, backend, dx, cx, status, page in GAMES)
    head = ("| Game | Symptom | CrossOver | Motor | Status |\n"
            "| --- | --- | --- | --- | --- |\n")
    return f"{README_BEGIN}\n\n{head}{rows}\n{README_END}"



# ------------------------------------------------------- hand-counted claims
#
# A number written into prose is a claim that was true once. This repository has
# now been caught by that five separate times in one day -- "three titles need a  # count-ok
# codec" when it was seven, "six of the eighteen run on stable" when it was  # count-ok
# seventeen of nineteen, "four sources" when there were five, "the other nine"  # count-ok
# when there were twelve, "what the nine titles have in common" over a table of  # count-ok
# nineteen. Each one was written beside a generated table that already knew
# better.
#
# So this refuses them. Outside the generated blocks, a sentence that counts the
# things this file counts is an error, not a style preference: say "the titles  # count-ok
# marked X in the table" and let the table do the counting.
#
# It cannot know the right number -- that is the point. It knows that a number
# should not be there.

COUNTED = r"(?:one|two|three|four|five|six|seven|eight|nine|ten|eleven|twelve|" \
          r"thirteen|fourteen|fifteen|sixteen|seventeen|eighteen|nineteen|twenty|\d+)"
CORPUS = r"(?:titles?|games?|entries|rows|fixes)"

# Deliberately narrow. A number is fine -- "36 demuxers", "luma 14..238", "one
# of them" -- and banning numbers would be banning prose. What is not fine is a
# number that counts THE SET THIS FILE ALREADY COUNTS, because that number lives
# in two places from the moment it is written and only one of them is kept up to
# date.
CLAIMS = [
    # "Six of the eighteen", "Three of the nine"  # count-ok
    re.compile(rf"\b{COUNTED}\s+of\s+the\s+{COUNTED}\b", re.I),
    # "seven titles here", "nine games here"  # count-ok
    re.compile(rf"\b{COUNTED}\s+(?:of\s+the\s+)?{CORPUS}\s+here\b", re.I),
    # "what the nine titles have in common", "the nine have in common"  # count-ok
    re.compile(rf"\bthe\s+{COUNTED}\s+(?:{CORPUS}\s+)?have\s+in\s+common\b", re.I),
    # "The other nine run on", "the other five titles need"  # count-ok
    re.compile(rf"\bthe\s+other\s+{COUNTED}\s+(?:{CORPUS}\s+)?(?:run|need|are|use|go)\b", re.I),
    # "the mechanism these six share", "these six run"  # count-ok
    re.compile(rf"\bthese\s+{COUNTED}\s+(?:share|run|need|are|use)\b", re.I),
    # "Three also run on stable", "Seventeen of the nineteen run on stable"  # count-ok
    re.compile(rf"\b{COUNTED}\s+(?:\w+\s+){{0,2}}?(?:also\s+)?run\s+on\s+stable\b", re.I),
    # "Six games borrow a decoder", "Three titles need a codec"  # count-ok
    re.compile(rf"\b{COUNTED}\s+{CORPUS}\s+(?:borrow|need|require|play|ship)\b", re.I),
    # "Twenty entries, and more games than that" -- a bare count opening a
    # sentence, which every pattern above missed because none of them fire
    # without a following verb or the word "here". It sat in the README saying
    # twenty while the table held twenty-one, and it took somebody asking why a
    # different game was missing to notice it.
    re.compile(rf"^\s*{COUNTED}\s+{CORPUS}\b", re.I),
    # "the only title here", "Persona 5 Strikers, and only it"  # count-ok
    re.compile(r"\bonly\s+title\s+here\b", re.I),
    re.compile(r"\band\s+only\s+it\b", re.I),
]
# Not a count of the corpus: versions, formats, sizes, D3D levels.
EXEMPT = re.compile(r"(?:D3D\d|DX1|GPTK|\d\.\d|bits?/byte|MB\b|KB\b|fps|20\d\d|"
                    r"VP9|H\.?26|AAC|NV12|VC-1|WMV|WMA|build\s+\d)", re.I)


def hand_counts(paths):
    """Every hand-written count found outside a generated block."""
    out = []
    for path in paths:
        text = path.read_text(errors="replace")
        # Generated blocks count for themselves; they are rewritten from the
        # rows every run and cannot go stale.
        for first, last in ((BEGIN, END), (STACK_BEGIN, STACK_END)):
            text = re.sub(re.escape(first) + r".*?" + re.escape(last), "", text, flags=re.S)
        for n, line in enumerate(text.splitlines(), 1):
            # A count of something local -- code paths, executables in one
            # package, video modes, files in one folder -- is not the thing this
            # refuses. Marking it says so once, in the line itself, where the
            # next person reading the sentence can see the claim was deliberate.
            if "count-ok" in line or EXEMPT.search(line):
                continue
            for rx in CLAIMS:
                m = rx.search(line)
                if m:
                    out.append((path, n, m.group(0).strip(), line.strip()))
                    break
    return out


# ------------------------------------------------- what a title needs set up
#
# A launcher applying one fix to one game also has to configure the bottle for
# it, and the two facts it needs are measured here already: which graphics
# backend the title requires, and whether it is tied to one Game Porting
# Toolkit generation.
#
# Emitted rather than copied. The fixes bundle ships the installers and not this
# file, so the data has to travel -- but travelling as a second hand-written
# list is how "three titles need a codec" became wrong while seven did.  # count-ok
#
# A generation is reported ONLY where it is a requirement, never where it is
# merely what the title happened to be measured on. A bare "4.0b2" says a run
# was made there and pins nothing; "**3.0 only** -- on 4.0b2 the video has sound
# and no picture" is a requirement and reports 3. Getting that backwards would
# have a launcher pin a toolkit for a game that did not care, which is worse
# than leaving it alone. A cell reporting no generation -- "not measured" --
# pins nothing, which is the right answer for a row whose toolkit nobody varied
# and is what most rows now say, because 4.0b2 being the rule for these titles
# is not a measurement of any one of them.


# Titles whose whole fix is the staged codec: nothing is installed beside the
# game, so there is no installer to declare them and they were invisible to
# anything reading the manifest.
#
# The executable is read off a real install rather than guessed, which is why
# this table is short: Devil May Cry 5 is on the machine this was written on and
# the two RESIDENT EVIL folders are empty shells. The generator says out loud
# which titles it could not carry, so the gap is a line of output rather than an
# absence nobody notices.
CODEC_ONLY_EXE = {
    "Devil May Cry 5": "DevilMayCry5.exe",
}

# METAL GEAR SOLID: Peace Walker is absent from this file for a reason worth
# writing down: nothing is installed for it, so there is no installer to carry an
# MGVF-GAME line, and no file in this repository records the name of its
# executable. So the one title release 5 is announced on cannot tell a launcher
# that it needs an engine of ours. Guessing a filename would close the gap with
# something unmeasured; titles_without_an_executable publishes it instead.


def config_json():
    """Per-title setup, for runtime/make-fixes-bundle.sh. JSON, no dependencies."""
    import json
    out = {}
    for g, engine, sym, fix, backend, dx, cx, status, page in GAMES:
        want = gptk(g)
        gen = ""
        if "only" in want:
            if "3.0 only" in want:
                gen = "3"
            elif "4.0b2 only" in want:
                gen = "4"
        out[g] = {
            # Three now, not two. NINJA GAIDEN 3 is the first title here that
            # needs DXVK, and the old two-way test called it d3dmetal -- which
            # is the one backend that cannot start it. A launcher acting on that
            # would have set the thing that fails.
            "backend": ("dxmt" if "DXMT" in backend.upper()
                        else "dxvk" if "DXVK" in backend.upper()
                        else "d3dmetal"),
            "gptk": gen,
            # Which plugin stage-codecs.sh has to put in front of CrossOver for
            # this title. It travelled as prose inside `why` -- "needs the
            # staged Matroska demuxer too" -- which no interface can act on.
            # Stripped of the backticks the table needs and a launcher does not.
            "codec": CODEC.get(g, "").strip("`") if "libgst" in CODEC.get(g, "") else "",
            # Nothing has yet needed one as a standing requirement. D3DM_MTL4=0
            # was tried on this project and never became one.
            "env": {},
        }
    return json.dumps(out, indent=2, sort_keys=True)



# ------------------------------------------------- the process feature table
#
# What an engine needs from this catalogue, in the shape an engine can use.
#
# winevideo already carries a unified process feature table -- patch 0024 --
# that maps an executable to the behaviour it needs, and its series also holds
# per-game patches: Soulcalibur VI, Mortal Shell II, Kingdom Hearts. So the
# shape is proven and it is not ours to invent. What it lacks is the data, and
# that is the one thing this repository has: the rows of the table above, each
# measured, and nearly all of them already declaring an executable to an
# installer.
#
# Why this file rather than more DLLs beside games. Measured on 2026-08-26:
# every half of the Beast of Reincarnation fix went inert on winevideo, because
# the engine did each job better from inside; the bottle that plays it carries
# no GST_PLUGIN_PATH at all, so an engine with the codecs built in removes the
# whole class of failure the staging kept producing -- one plugin cache per
# architecture shared between engines, a lib64 test that silently decides
# whether GST_REGISTRY is set, bottles pointed at a staging built for another
# core, and nothing checking any of it.
#
# An observation against that reading, recorded rather than resolved. On
# 2026-08-31 the title updated, every address moved by 0x4070, the guard read
# the bytes, refused to patch and said so -- and the title stopped working until
# the two IsSoftware call sites were followed to their new addresses. Whatever
# "inert" measured on 2026-08-26, those two sites are load-bearing on the engine
# this runs on now. Both readings are here; neither has been explained away.
#
# The risk this shape carries, named here because it is the same shape as the
# addresses that died with a game update: a table keyed on an executable name
# goes silent when a game renames its binary. Whoever consumes this owes it a
# check, and --check below is ours.
FEATURES_JSON = "process-features.json"


def _exe_by_title():
    """{title: executable}, read from the installers that declare it.

    Not a second list. Every installer carries an MGVF-GAME line naming the
    title and the executable it belongs to, and check-builds.sh already refuses
    a run where those disagree with the app.
    """
    out = {}
    for script in sorted((REPO / "runtime").glob("install-*.sh")):
        for line in script.read_text().splitlines():
            if "MGVF-GAME:" not in line:
                continue
            parts = [f.strip() for f in line.split("MGVF-GAME:", 1)[1].split("|")]
            if len(parts) >= 2 and parts[1]:
                out[parts[0]] = parts[1]
    out.update(CODEC_ONLY_EXE)
    return out


def features_json():
    """The catalogue as {executable: what that process needs}."""
    import json
    exes = _exe_by_title()
    cfg = json.loads(config_json()) if isinstance(config_json(), str) else config_json()
    procs = {}
    for g, engine, sym, fix, backend, dx, cx, status, page in GAMES:
        exe = exes.get(g)
        if not exe:
            continue
        row = cfg.get(g, {})
        entry = {
            "title": g,
            "backend": row.get("backend", ""),
            "codec": row.get("codec", ""),
            "gptk": row.get("gptk", ""),
            "env": row.get("env", {}),
            "symptom": sym,
        }
        # An engine requirement is a fact about the engine, not about the game,
        # and it is the one thing here a launcher cannot work around. So it is
        # read from the column that answers it and not from the prose of the Fix
        # column, which reads the same for two different arrangements: NINJA
        # GAIDEN 3's fix names winevideo because its DirectShow filters came
        # from there, and those files ship into the bottle's system32 for one
        # executable. The engine that runs it is stock, and publishing
        # "needs_engine" for it would send a launcher hunting for an engine no
        # part of that fix wants.
        motor = engine_needed(g)
        if motor not in ("Stock", "Stock&dagger;", "None"):
            entry["needs_engine"] = motor.replace("**", "")
        procs[exe] = entry
    missing = sorted(g for g, *_ in GAMES if g not in exes)
    # An executable name is a key here, and some of them are not distinctive
    # enough to be one. Persona 5 Strikers ships as game.exe: an engine keyed on
    # that would hand its settings to anything else called the same. Harmless
    # while these names only pick an installer, which is what they did until
    # now, so it is called out rather than corrected -- whoever consumes this
    # needs to know which keys are weak.
    weak = sorted(e for e in procs
                  if e.lower() in ("game.exe", "launcher.exe", "start.exe",
                                   "win64-shipping.exe", "app.exe"))
    return json.dumps({
        "note": "Generated by wiki/games.py. Keyed by executable; see that file.",
        "titles_without_an_executable": missing,
        "keys_too_generic_to_match_on_alone": weak,
        "processes": dict(sorted(procs.items(), key=lambda kv: kv[0].lower())),
    }, indent=2, ensure_ascii=False) + "\n"


def main():
    if "--config-json" in sys.argv:
        print(config_json())
        return 0
    if "--features-json" in sys.argv:
        print(features_json(), end="")
        return 0
    check = "--check" in sys.argv
    here = pathlib.Path(__file__).parent
    stale = []

    # The process feature table travels with the pages: generated from the same
    # rows, refused when it drifts from them.
    feat = here.parent / FEATURES_JSON
    want = features_json()
    if feat.exists() and feat.read_text() == want:
        print(f"  up to date  {FEATURES_JSON}")
    elif check:
        stale.append(FEATURES_JSON)
    else:
        feat.write_text(want)
        print(f"  rewritten   {FEATURES_JSON}")

    pages = sorted(here.glob("*.md")) + [here.parent / "README.md"]
    for page in pages:
        if not page.exists():
            continue
        text = page.read_text()
        if not any(m in text for m in (BEGIN, STACK_BEGIN, README_BEGIN)):
            continue
        new = text
        for first, last, render in ((BEGIN, END, table),
                                    (STACK_BEGIN, STACK_END, stack),
                                    (README_BEGIN, README_END, readme_table)):
            if first not in new:
                continue
            new = re.sub(re.escape(first) + r".*?" + re.escape(last),
                         lambda _, r=render: r(), new, flags=re.S)
        if new == text:
            print(f"  up to date  {page.name}")
        elif check:
            stale.append(page.name)
        else:
            page.write_text(new)
            print(f"  rewritten   {page.name}")
    # The other half of keeping one truth: a generated table cannot stop
    # somebody writing its numbers into the prose beside it, so this refuses
    # those outright. It runs on every check, not only when a page is stale --
    # the counts go wrong when the ROWS change, which is exactly when the pages
    # are rewritten and look fine.
    # This file too. The counts it writes into the generated block are counts
    # of the title set like any other, and being inside the markers is what let
    # "Seventeen of the nineteen" survive two new titles: the block is only  # count-ok
    # rewritten from the rows where it actually reads them, and that sentence
    # did not. Scanning the source catches a literal the rendered page cannot.
    scanned = pages + [here.parent / "app" / "MacGameVideoFix.swift",
                       pathlib.Path(__file__).resolve()]
    counts = hand_counts([q for q in scanned if q.exists()])
    for q, n, frag, line in counts:
        rel = q.relative_to(here.parent)
        print(f"  hand-counted  {rel}:{n}  \"{frag}\"", file=sys.stderr)

    if stale or counts:
        if stale:
            print(f"\nout of date: {', '.join(stale)}\nrun wiki/games.py", file=sys.stderr)
        if counts:
            print(f"\n{len(counts)} hand-written count(s) of the title set.", file=sys.stderr)
            print("Say \"the titles marked X in the table\" instead, and let the", file=sys.stderr)
            print("table do the counting. A local count -- executables in one", file=sys.stderr)
            print("package, modes in one list -- is fine: mark it <!-- count-ok -->.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
