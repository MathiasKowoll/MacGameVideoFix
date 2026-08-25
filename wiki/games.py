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
     "D3DMetal", "11", "26.3 and Preview",
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
rest run on
D3DMetal with the D3D12 renderer, which is also what keeps PSO precompilation
-- `-dx11` dodges some of these faults and costs permanent shader-compilation
stutter.

**Which CrossOver, and what "Preview" means.** Every measurement here was taken
against CrossOver 26.3 and `crossover-preview-arm64-20260821`, and the CrossOver
column says which of the two a title was measured on rather than which it might
work on. **Seventeen of the nineteen run on stable 26.3**, which inverts where
this project started: stable was the exception and is now the rule, and the
toolkit -- not the engine -- turned out to be the axis that decides most of
these titles.

The two exceptions point in opposite directions. The Kingdom Hearts pair has
only ever been launched on Preview, so its rows record an absence rather than a
result and nothing is claimed either way. NINJA GAIDEN 4 is the reverse: it runs
on stock 26.3 and stalls on Preview, and what stalls it is the toolkit, which
executes command lists concurrently with no lever to turn that off.

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
engine.** Preview ships `libgstmatroska`, for both architectures, and stable
26.3 ships it for neither -- so the difference between the two builds on a WebM
was one plugin the whole time. Staging it beside the decoder gives stable one
too, and NINJA GAIDEN 4 is where that was measured: it plays on stock 26.3,
video and all, with nothing patched into CrossOver.

Several titles need a codec no CrossOver ships -- VC-1, WMV3, WMV2 or WMA -- and
it is staged beside the game rather than patched into it. Which titles, and
which plugin each one needs, is the Codec column of
[what each title actually loads](Games.md#what-each-title-actually-loads); the
count is derived there rather than repeated here, because the number written
here was three for as long as it took two more titles to join the list.
Nioh 3 needs none: its video is already NV12 by the time Media Foundation is
asked for it.

None of these fixes decodes anything. The frames existed all along; they were
being crashed on, mislabelled, or thrown away.

Every row is a title we deliberately took on, and every claim on the linked
page comes from a measurement on an installed copy.
"""


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

**Codec** is the plugin `stage-codecs.sh` must put in front of CrossOver.
{decoders} titles need a decoder no CrossOver ships, {demuxers} need a demuxer,
and telling those two cases apart is most of the work -- see
[How the codec staging works](How-the-codec-staging-works.md). The decoder
column is the same on every build; the demuxer one is not, because Preview
ships `matroska` and stable 26.3 does not, so those rows describe what stable
needs and what Preview already has.
"""


STACK_BEGIN, STACK_END = "<!-- stack:begin -->", "<!-- stack:end -->"


def stack():
    names, _ = _switch("name")
    installers, fallback = _switch("installer")
    by_title = {title: case for case, title in names.items()}
    rows, bridges_seen, registry_seen = "", [], []
    for g, _engine, _sym, _fix, backend, dx, cx, _status, page in GAMES:
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
            f"| [{g}]({page}.md) | {backend} | {dx} | {gptk(cx, g)} "
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
    # Was recorded as running on 4.0b2 under Wine 27 and dying on the same
    # toolkit under Wine 26.3. It was not the Wine: the registry override its
    # bridge depends on had gone missing, so the 26.3 runs measured the game
    # with no fix loaded. With the key written it runs on both.
    "NieR Replicant ver.1.22474487139": "4.0b2",
    "Life is Strange: Reunion": "**4.0b2 only** -- 3.0 crashes it",
    "Life is Strange: Double Exposure": "**4.0b2 only** -- 3.0 crashes it",
}


def table():
    rows = "".join(
        f"| [{g}]({page}.md) | {engine} | {sym} | {fix} | {backend} | {dx} "
        f"| {gptk(cx, g)} | {cx} | {status} |\n"
        for g, engine, sym, fix, backend, dx, cx, status, page in GAMES)
    return f"{BEGIN}\n\n{HEAD}{rows}{NOTE}\n{END}"


# ------------------------------------------------------------------ README
#
# The README carried its own copy of the table, kept by hand, with absolute
# links because it is read on the front page rather than inside the wiki. It had
# eighteen rows against the wiki's nineteen -- NINJA GAIDEN 4 was fixed, shipped
# and documented, and never reached the one table most people see first.
#
# Same rows, same source, different link style. The symptom column is shortened
# the way the README already shortened it: the front page is a list, not a
# report.

WIKI = "https://github.com/MathiasKowoll/MacGameVideoFix/wiki"
README_BEGIN, README_END = "<!-- readme-games:begin -->", "<!-- readme-games:end -->"


def readme_table():
    rows = "".join(
        f"| [**{g}**]({WIKI}/{page}) | {sym} | {cx} |\n"
        for g, engine, sym, fix, backend, dx, cx, status, page in GAMES)
    head = "| Game | Symptom | CrossOver |\n| --- | --- | --- |\n"
    return f"{README_BEGIN}\n\n{head}{rows}\n{README_END}"



# ------------------------------------------------------- hand-counted claims
#
# A number written into prose is a claim that was true once. This repository has
# now been caught by that five separate times in one day -- "three titles need a
# codec" when it was seven, "six of the eighteen run on stable" when it was
# seventeen of nineteen, "four sources" when there were five, "the other nine"
# when there were twelve, "what the nine titles have in common" over a table of
# nineteen. Each one was written beside a generated table that already knew
# better.
#
# So this refuses them. Outside the generated blocks, a sentence that counts the
# things this file counts is an error, not a style preference: say "the titles
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
    # "Six of the eighteen", "Three of the nine"
    re.compile(rf"\b{COUNTED}\s+of\s+the\s+{COUNTED}\b", re.I),
    # "seven titles here", "nine games here"
    re.compile(rf"\b{COUNTED}\s+(?:of\s+the\s+)?{CORPUS}\s+here\b", re.I),
    # "what the nine titles have in common", "the nine have in common"
    re.compile(rf"\bthe\s+{COUNTED}\s+(?:{CORPUS}\s+)?have\s+in\s+common\b", re.I),
    # "The other nine run on", "the other five titles need"
    re.compile(rf"\bthe\s+other\s+{COUNTED}\s+(?:{CORPUS}\s+)?(?:run|need|are|use|go)\b", re.I),
    # "the mechanism these six share", "these six run"
    re.compile(rf"\bthese\s+{COUNTED}\s+(?:share|run|need|are|use)\b", re.I),
    # "Three also run on stable", "Seventeen of the nineteen run on stable"
    re.compile(rf"\b{COUNTED}\s+(?:\w+\s+){{0,2}}?(?:also\s+)?run\s+on\s+stable\b", re.I),
    # "Six games borrow a decoder", "Three titles need a codec"
    re.compile(rf"\b{COUNTED}\s+{CORPUS}\s+(?:borrow|need|require|play|ship)\b", re.I),
    # "the only title here", "Persona 5 Strikers, and only it"
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
# list is how "three titles need a codec" became wrong while seven did.
#
# A generation is reported ONLY where it is a requirement, never where it is
# merely what the title happened to be measured on. "3.0 and 4.0b2" means both
# work and the field stays empty; "**3.0 only** -- 4.0b2 stalls it" is a
# requirement and reports 3. Getting that backwards would have a launcher pin a
# toolkit for a game that did not care, which is worse than leaving it alone.


def config_json():
    """Per-title setup, for runtime/make-fixes-bundle.sh. JSON, no dependencies."""
    import json
    out = {}
    for g, engine, sym, fix, backend, dx, cx, status, page in GAMES:
        want = gptk(cx, g)
        gen = ""
        if "only" in want:
            if "3.0 only" in want:
                gen = "3"
            elif "4.0b2 only" in want:
                gen = "4"
        out[g] = {
            "backend": "dxmt" if "DXMT" in backend.upper() else "d3dmetal",
            "gptk": gen,
            # Nothing has yet needed one as a standing requirement. D3DM_MTL4=0
            # was tried on this project and never became one.
            "env": {},
        }
    return json.dumps(out, indent=2, sort_keys=True)


def main():
    if "--config-json" in sys.argv:
        print(config_json())
        return 0
    check = "--check" in sys.argv
    here = pathlib.Path(__file__).parent
    stale = []
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
    scanned = pages + [here.parent / "app" / "MacGameVideoFix.swift"]
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
