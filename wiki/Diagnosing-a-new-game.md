The tools in `diagnostics/`, each answering a different question. The numbered
ones below run in this order — every one narrows what the next has to look at.
A few more are listed at the end; they did decisive work on titles here without
belonging to a fixed step in the sequence.

## 1. What does this game ship, and what plays it?

```
diagnostics/survey-games.sh "/path/to/steamapps/common/<Game>"
```

One row: engine, how many videos, in what container, the codec of a sample, and
which media DLLs the main executable is linked against. Usually enough to say
which failure mode you are looking at, or that the game is not in this category
at all.

Read the codec and the container as two separate answers. The codec says
whether anything can decode the file; the container says whether anything can
open it, and those come apart. On stable CrossOver 26.3 the same VP9 plays in
an `.mp4` and does not in a `.webm`, because that build ships no `matroska`
plugin — 355 `.webm` files and 61 VP9 `.mp4` files are the same codec and
different outcomes. Which build opens what is set out in
[Games](Games.md).

Given a whole library instead of one game it surveys everything under it, which
helps when hunting for a title worth working on. The wiki still only covers
games we deliberately took on.

## A bottle is locked to one CrossOver version

Before anything else, check which engine a bottle belongs to:

    grep '"Version"' ~/Library/Application\ Support/CrossOver/Bottles/<name>/cxbottle.conf

**A bottle recorded for an older CrossOver will not run under a newer one, and
says nothing about it.** `wine --bottle X --cx-app ...` exits 0, prints not one
line, and starts no process. There is no error, no dialog and no log entry. It
reads exactly like a broken launcher script, and the temptation is to debug the
script.

So a measurement "on Preview" needs a bottle that records Preview's version, not
a 26.3 bottle launched with Preview's `wine`. Bottles are otherwise shared
between installs -- the engine supplies the runtime and the bottle supplies the
prefix -- which is what makes `diagnostics/launch-with.sh` useful, and it is easy
to over-generalise that into believing any bottle runs under any engine. It does
not: it runs under its own version and newer-refuses-older is silent.

Two consequences worth carrying:

- **Keep one bottle per engine version** for anything that has to be measured on
  both, and say which is which. Comparing "26.3 versus Preview" means two
  bottles, and the game installed where both can see it -- a Steam library on a
  shared volume does this without a second copy.
**The recorded version is not stable while a CrossOver app is open.** The
`"Version"` line gets rewritten as bottles are touched, so a value read while
CrossOver or CrossOver Preview is running can be stale within the minute. An
afternoon went into correcting `GST_PLUGIN_PATH` entries three times before this
was noticed: each correction was right when made and wrong shortly after, because
the version it was keyed to had changed underneath. Quit every CrossOver app,
confirm no `wineserver` remains, and only then read the file and act on it.

- **A bottle's `GST_PLUGIN_PATH` must match the bottle's own version.** The
  staged codec symlinks into one specific CrossOver's GStreamer, so pointing a
  26.3 bottle at a Preview-built tree puts two GStreamer cores in one process.
  That failure is loud where this one is silent -- `objc: Class
  GstCocoaApplicationDelegate is implemented in both ...` -- but the audit is
  cheap: compare each bottle's `"Version"` against the third field of
  `gst-codecs/.map`.

## 2. Does the crash actually apply to this build?

```
runtime/pe.py imports "/path/to/Game-Win64-Shipping.exe" --dlls
```

That answers the cheaper half: whether the game reaches Media Foundation at
all. `pe.py` has four modes — `exports`, `exports --ordinals`, `imports`,
`imports --dlls` — and prints nothing else.

The half that settles the crash is a scan for Electra's D3D12 version check.
The pattern is

```
cmp dword [rbp+disp], 12000     ; 81 7D ?? E0 2E 00 00
jl  <cpu path>                  ; 7C ??   or   0F 8C ?? ?? ?? ??
```

**No static scanner for it ships here yet.** The code that knows the pattern is
the runtime patch itself, which finds and rewrites the sites inside the running
process and reports how many it found, so the count arrives once the fix is
installed rather than before.

Mortal Shell 2 has four. A UE4 title has none, because the buffer pool that
crashes does not exist in that engine version. A count of zero means this
particular bug is not present, whatever else may be wrong.

## 3. What is the game asking for, and what is it getting?

For the silent failure — cutscene reached, screen black, no crash, no log.

```
diagnostics/build-probe.sh "/path/to/game/<carrier>.dll"
```

`mf-probe` replaces entries in the game's import address table and logs each
call with its result: `MFStartup`, `MFCreateFile`, `MFTEnumEx`,
`MFCreateSourceReaderFromByteStream`, `CoCreateInstance` failures, and
`CreateFileW` on video paths. Every hook calls through and returns the real
result unchanged — it watches, it does not intervene.

Hooking the import table beats scanning for byte patterns here: the IAT is a
documented structure, so nothing depends on which compiler built the game or on
things moving between updates.

**Picking a carrier.** The game has no plugin hook, so the probe rides in on a
DLL the game already loads. It must be imported directly, load early, and have
nothing to do with rendering. A shipped third-party library is ideal. The
three in use here:

- `libogg_64.dll` — any Unreal title.
- `libxess.dll` — DYNASTY WARRIORS: ORIGINS.
- `amd_ags_x64.dll` — Persona 5 Strikers. A vendor library the game imports
  and barely calls under CrossOver, which is the shape to look for when a game
  ships neither of the other two.

**Never `steam_api64.dll`.**

`build-probe.sh` reads the carrier's export table and generates PE forwarders
for every symbol, so the real library still answers every call. Install by
renaming the game's copy to `<stem>_real.dll` and dropping the probe in its
place. The log goes to the bottle's `C:\mf-probe.log`.

Read an empty log carefully. Two things produce one that has nothing to do with
what the game asked for.

**Imports by name only.** If the probe hooked the import table and not
`GetProcAddress`, a call resolved at runtime went somewhere nobody was
watching.

**Imports by ordinal.** An import with no name is skipped outright by a
name-walking hook, so the hook installs, reports itself installed, and never
fires. DYNASTY WARRIORS brings in `D3D12CreateDevice` from `d3d12.dll` as
ordinal 101 with no name, which is not unusual, and this has cost a wasted run
more than once here. `hook_import_ordinal` in `diagnostics/mf-probe.c` exists
for exactly that.

An empty log means "the game did not ask" only once both are ruled out.

## 4. Everything, if the game has no usable carrier

```
diagnostics/capture-mf-trace.sh <bottle> 'Z:\path\to\game.exe'
```

Launches through Wine with the Media Foundation channels on and reduces the
trace to the media types and handlers that were refused. Slower, far noisier,
and needs the game launched by hand — but it needs nothing installed into the
game folder.

## The rest, outside the sequence

```
diagnostics/find-callsites.py <file.exe> <symbol> [symbol...]
```

Finds where a PE calls an imported function without running it, by resolving
every `call qword ptr [rip+disp32]` against the import table. On DYNASTY
WARRIORS it found in one second what four launches of guesswork had not — two
`MFStartup` call sites, only one of them the video player.

```
diagnostics/launch-and-capture.sh <bottle> [engine]
```

**Repository only — this one is not in any release.** It keeps the stderr a
Steam or CrossOver-interface launch throws away. Wine, D3DMetal and DXMT write
their diagnostics there, and for a game that hangs on a
black screen with no crash report that is often the only statement of what is
wrong. One trap worth knowing before trusting a quiet capture: launching Steam
captures nothing, because Steam forks and returns, so the command finishes
before the game starts and the game's output belongs to another process.

There is also `diagnostics/registry/apply-webm-handler.sh`, which adds or
removes the `.webm` byte-stream handler mapping in a bottle. It was written to
test one question — whether that single mapping is the whole of what winevideo
provides for a WebM/VP9 game on a build that already carries the demuxer.

## A warning

Hooking and patching a running process is what anti-cheat exists to stop. **Do
not point any of this at a game with anti-cheat.** Everything here is for
single-player titles whose cutscenes do not play.

---

Back to [the games table](Games.md) ·
[Findings](Findings.md), what the six have in common
