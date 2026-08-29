# What ships, and where it goes

Every binary this project installs, what it is, and which of five places it
belongs to. Written because "which files does a launcher have to copy" was asked
and could not be answered from one page.

Asking it has twice turned up something the page did not know: a category
nothing copied at all, and then a binary listed here as though it shipped while
no installer named it. Both are recorded below rather than quietly corrected,
because the second one was found by counting the files against a published
release instead of against this table.

## 1. Built here, from sources in this repository

Twelve are built here and **eleven of them travel**. Each rides on a library
the game already loads, forwards every export to the renamed original, and gets
`DllMain` — that is the whole mechanism. `check-builds.sh` rebuilds every one of
them from the source named below and refuses a release where the shipped copy
differs.

| DLL | source | titles |
| --- | --- | --- |
| `GfeSDK.dll` | `p5s-video-bridge.c` | Nioh, Nioh 2 |
| `amd_ags_x64.dll` | `p5s-video-bridge.c` | Persona 5 Strikers |
| `amd_ags_x64-nioh3.dll` | `dwo-video-bridge.c` | Nioh 3 |
| `libxess.dll` | `dwo-video-bridge.c` | Dynasty Warriors: Origins, Wo Long |
| `dinput8-kh.dll` | `dwo-video-bridge.c` | Kingdom Hearts (both) |
| `dinput8-nier.dll` | `dwo-video-bridge.c` | NieR Replicant |
| `libogg_64.dll` | `ue5-media-fix.c` | Mortal Shell 2, Beast of Reincarnation, Life is Strange (both) |
| `libogg_64_electra.dll` | `electra-h264-fix.c` | **none — superseded, does not travel** |
| `dstorage-ng4.dll` | `ng4-observe.c` | NINJA GAIDEN 4 |
| `fmod-tmnt.dll` | `d3d12-guards.c` | TMNT: Splintered Fate |
| `OpenColorIO_2_3-tormented.dll` | `d3d12-guards.c` | Tormented Souls 2 |
| `NvCloth_x64-resonance.dll` | `shader-floor-fix.c` | RESONANCE: A Plague Tale Legacy |

**Where they go:** into the game's own folder, beside the executable, with the
game's original renamed to `<name>_real.dll`. The installer for each title
decides the exact folder. Nothing outside the game folder is touched.

**The twelfth is `libogg_64_electra.dll`, and nothing uses it.** No installer
names it and it is in no bundle. Beast of Reincarnation, the title it was made
for, is served by `install-runtime-fix.sh` and the shipped `libogg_64.dll`,
which carries the current fix: `ue5-media-fix.c` holds
`CVAR_NAME[] = L"Electra.Win.H264UseOldOutputPath"`, the console variable that
puts Electra on its CPU path. The variant is kept because it builds and
`check-builds.sh` still verifies it, not because anything installs it.

## 2. Other people's binaries, redistributed

Four, all for NINJA GAIDEN 3: Razor's Edge. They cannot be rebuilt from anything
here, so `check-builds.sh` verifies them against recorded sha256s in
`ng3-THIRD-PARTY-LICENCES.md` instead.

| DLL | what it is | licence |
| --- | --- | --- |
| `ng3-d3d9.dll` | d9vk, `Sikarugir-App/d9vk` `v1.10.3-20250511` | zlib, as DXVK |
| `ng3-qasf.dll` | Wine's DirectShow ASF Reader, patched by winevideo | LGPL-2.1-or-later |
| `ng3-quartz.dll` | Wine's filter graph, patched by winevideo | LGPL-2.1-or-later |
| `ng3-winegstreamer.dll` | Wine's GStreamer bridge, patched by winevideo | LGPL-2.1-or-later |

**Where they go:** into the **bottle's** `drive_c/windows/system32`, not the game
folder, activated for one executable through `AppDefaults`. This is the only
fix here with `"scope": "bottle"`.

## 3. Staged into the engine

Two GStreamer plugins, copied into CrossOver itself by `stage-codecs.sh`:
`libgstlibav.dylib` and `libgstmatroska.dylib`. The titles that need one are the
ones whose `codec` field in the manifest names it; for some of them it is the
entire fix and no DLL is installed beside the game at all.

**Where they go:** into the CrossOver application's own
`lib64/gstreamer-1.0`. They are shared by every bottle that engine runs.

## 4. Built here, installed into the engine

Three files, all produced by this project, all sitting in the engine on this
machine. `scripts/build-winegstreamer.sh` builds them from a wine tree under
`~/Development/mgvf-winegstreamer-build`, and `built-for.json` beside them
records what they were built against:

    engine 26.3.0.39832, wine-11.0-8726-g2e2f5fca349, patches 0002 0003 0006 0008

| file | size | what it is | packaged |
| --- | --- | --- | --- |
| `winegstreamer.dll` | 2,330,624 | Wine's GStreamer bridge, PE half | yes |
| `winegstreamer.so` | 222,376 | the same bridge, Unix half | yes |
| `d3d9.dll` | 1,576,960 | Wine's d3d9 with winevideo's video-bridge patches | **no, deliberately** |

**The winegstreamer pair is load-bearing.** It was built because codecs that were
present would not show. Both halves have to match each other and the engine they
were built against, which is why `built-for.json` exists and why neither half can
be shipped without the other.

**The d3d9 is not, and is left out on purpose.** It was built with winevideo's
`0008` and `0009` patches for the Nioh work and it did not fix Nioh — recorded in
`docs/codecs-inside-the-engine.md` at the time: *"with the complete bridge in the
engine the title still dies with zero calls to it"*. The bridge those titles
actually use is `runtime/p5s-video-bridge.c`, which ships as `GfeSDK.dll` and
`amd_ags_x64.dll` in the game folder and intercepts `OpenSharedResource` itself.
And it occupies a path something else wants. RaccoonBot's patcher writes **d9vk**
to `lib/wine/x86_64-windows/d3d9.dll`, which is exactly where this one sits.

Measured on this machine rather than assumed, because the first version of this
paragraph said ours was sitting on top of d9vk and that was wrong:

| | x86_64-windows | i386-windows |
| --- | --- | --- |
| in the engine now | 1,576,960 — ours | 185,424 — wine's own |
| `d3d9.dll.stock` beside it | 192,080 — wine's own | — |
| what d9vk would be | 3,848,151 | 4,050,110 |

**d9vk is not in the engine at all**, in either architecture. So ours did not
displace it; the legible sequence is that wine's original was backed up, ours
was written over it, and d9vk was either never applied or applied and then
overwritten. Ours should still come out — but taking it out is not by itself
enough, and the honest repair is to re-patch the engine from the application
rather than to copy a file over by hand.

That gap may also be a lead. NINJA GAIDEN Sigma is a D3D9 title that fails with
`DxvkAdapter: Failed to create device`, MoltenVK reporting `transformFeedback`,
`bufferDeviceAddress`, `timelineSemaphore` and `depthClipEnable` all zero —
which is the same diagnosis NINJA GAIDEN 3's page gives. A properly installed
d9vk is the other road for that title, and nobody has been able to try it,
because the engine has not had d9vk in it.

**None of this is the NINJA GAIDEN 3 fix**, which is a separate `d3d9.dll` that
happens to share the name. That one is d9vk (3,832,944 bytes, third-party, in
category 2 above), and it goes into the **bottle's** `drive_c/windows/system32`
with per-executable registry overrides — never into the engine, so it reaches
one title without changing D3D9 for anything else on the machine. Two files, the
same filename, different origins and different destinations.

This page has been wrong about these files twice, in opposite directions, before
anyone looked for the build tree that `docs/codecs-inside-the-engine.md` names.
Both claims were made from file sizes. **Do not decide whose a binary is by
comparing sizes.**

### The payload folder

`runtime/engine-payload/` holds the two load-bearing halves laid out the way a
CrossOver engine is, so a patcher building its own copy can overlay the tree
without a mapping table of its own:

    wine/x86_64-windows/winegstreamer.dll   ->  <CX>/lib/wine/x86_64-windows/winegstreamer.dll
    wine/x86_64-unix/winegstreamer.so       ->  <CX>/lib/wine/x86_64-unix/winegstreamer.so
    built-for.json                          ->  not copied; read it first

This is the shape RaccoonBot's patcher already consumes for d9vk, so the overlay
belongs in the same pass that produces the patched CrossOver — not in a later
step against an engine that is already built and possibly already running.

**The guard is not paperwork.** The two halves speak an interface that changes
between wine revisions; onto a different wine they do not degrade, they stop
loading media — failing in the exact shape of the fault they were built to
repair. And the version alone does not identify an engine: the patched fork and
a stock CrossOver both report `26.3.0.39832`, which is how this project once
wrote into a stock install during a test. The app name is the part that tells
them apart. `install-engine-media.sh` checks both, and any patcher consuming the
payload directly should check both too.

## 5. Built here, installed into the engine, and not part of any fix

One more binary exists and belongs in this inventory even though nothing here
depends on it: `crossover/dxgi.dll`, 67,072 bytes, installed by
`crossover/install-node-guard.sh`.

It is the DXGI node guard — the same correction Life is Strange needs, which
answers `QueryVideoMemoryInfo` for adapter nodes that do not exist so Unreal's
node walk terminates — but applied to the whole CrossOver instead of one game.
**No title requires it.** Every title that needs the guard gets it from its own
game-folder proxy, and this is the engine-wide alternative that
`docs/upstreaming.md` keeps as a direction for taking the work upstream.

It is deliberately outside the bundle: `make-fixes-bundle.sh` globs
`runtime/install-*.sh`, and this one lives in `crossover/`. Two consequences
worth stating rather than leaving to be discovered.

- **It does not declare the manifest schema.** Every other installer carries
  `MGVF-SCOPE` and answers `--status`; this one predates that rule and has
  neither. Anything that surveys the installers will not see it.
- **A launcher cannot offer it, and should not want to.** It modifies a shared
  CrossOver for every bottle and every game, and invalidates the code signature
  — a decision for a person, not something to apply on a title's behalf.

## The GStreamer prerequisite, which applies to everyone

**Every route needs GStreamer installed on the user's machine.** Nothing in this
project, and nothing in RaccoonBot, redistributes a decoder. The routes differ in
*where the plugins are put*, not in *where they come from*.

| | where the plugins are placed | where they come from | user must install GStreamer |
| --- | --- | --- | --- |
| **Our installers** — category 3 | a staged directory per engine, `GST_PLUGIN_PATH` | the user's framework | **yes** |
| **RaccoonBot** | inside the engine it builds | the user's framework | **yes** |

This page said the opposite for a few hours, and then said something different
and also wrong, so the measurement is written down rather than the conclusion.
`RaccoonBot.app` contains **zero** files named `libgst*`, `libav*` or `libsw*`.
Its `Resources` carry 153 MB of other people's toolkits — d3dMetal 3 and 4,
d9vk, dxvk, three MoltenVK builds, mesa, Rosetta x87, wine — and not one codec.
Its `EngineCodecs` copies from
`/Library/Frameworks/GStreamer.framework/Versions/1.0/lib` and reports
`frameworkMissing` when it is absent.

The first version of this section made the framework a flat prerequisite, which
was right. The second removed it for launchers that own their engine, on the
strength of `docs/codecs-inside-the-engine.md` saying RaccoonBot had "moved to
this arrangement" — but that document describes where the files are *placed*,
and I read it as saying where they *come from*. Owning the engine changes the
destination, not the source.

**Install 1.24.13, not a later 1.24.** It is what winevideo names, what is
installed and working here (`pkgutil` says so for every GStreamer package on
this machine), and what RaccoonBot requires *exactly*. Our own `stage-codecs.sh`
accepts the 1.24 series, because a plugin only has to be ABI-compatible with the
core it is re-homed onto — but its instructions used to name 1.24.14, which
would have sent a user to a release the other route on the same machine refuses.
Accepting a range and advising a version are different jobs.

**Without it, nothing announces the problem.** The engine ends up as stock 26.3
is — eighteen plugins, no `libgstlibav`, no `libgstmatroska`, no `libgstvpx` —
and the first sign is a black cutscene in a title whose fix is installed and
reporting healthy. A launcher should check for the framework and say so plainly
before a user reaches that point.

**`winegstreamer` is not what supplies them.** `otool -L` shows it linking
`libgstreamer-1.0`, `libgstvideo`, `libgstaudio`, `libgsttag` and `libglib`, and
no decoder at all. It is the bridge from a Windows process to whatever plugins
are present. Both halves are needed and neither substitutes for the other, which
makes "the engine has winegstreamer, so it has codecs" the one wrong turn to
design against.

**The destination differs by engine, and guessing it does damage.** 26.3 keeps
plugins in `lib64/gstreamer-1.0`; 27 keeps them in `lib/<arch>/gstreamer-1.0`.
Do not create a `lib64` on a 27-based engine — the block in its `wine` that sets
`GST_PLUGIN_SYSTEM_PATH` *replaces* the path rather than appending, so a
half-filled `lib64` hides the engine's own twenty plugins.

### The engine on this machine is running codecs from a GStreamer that is gone

Worth recording because it is invisible and will confuse the next person.

| | bytes | architectures |
| --- | --- | --- |
| framework's `libgstlibav.dylib` | 600,496 | x86_64 + arm64 |
| its x86_64 slice | 282,624 | x86_64 |
| **the engine's copy** | **267,696** | **x86_64, matching neither** |

The copying does no thinning and no relinking, so the engine's plugin is not a
processed version of what is installed today — it came from a GStreamer that is
no longer on this machine, on 26 August. Re-patching would replace it with the
1.24.13 ones. That is the third reason to re-patch, after the missing d9vk and
our `d3d9` sitting in its path.

## What a launcher has to copy

- Everything in categories 1 and 2 arrives in `fixes-v<version>.tar.gz`, named
  per title in `manifest.json` under `files`, with the installer named in
  `script` and its argument's meaning in `scope`.
- Category 3 is not per-title: `stage-codecs.sh` travels in the same bundle and
  is run against the engine, not a game. The manifest's `codec` field says which
  titles need it.
- Category 4 travels too, and is **not** under `games` — nothing about it is
  per-title, and a nameless row in a list keyed by title is a row someone will
  try to match against a game folder. It has its own top-level block, which says
  where each file goes and what it was built for:

```json
"engine": {
  "script": "install-engine-media.sh",
  "scope": "engine",
  "files":  ["engine-winegstreamer.dll", "engine-winegstreamer.so", "engine-built-for.json"],
  "install": [
    {"file": "engine-winegstreamer.dll", "dest": "lib/wine/x86_64-windows/winegstreamer.dll"},
    {"file": "engine-winegstreamer.so",  "dest": "lib/wine/x86_64-unix/winegstreamer.so"}
  ],
  "builtFor": {"app": "Crossover_patched.app", "version": "26.3.0.39832",
               "wine": "wine-11.0-8726-g2e2f5fca349"}
}
```

  The bundle is flat, so `files` are flat names and `install` carries the
  destinations. A patcher that would rather overlay a tree than read a mapping
  has `runtime/engine-payload/` for exactly that; the two are the same bytes.

**Decoding takes two things, and category 4 is only one of them.** The
`winegstreamer` pair is the bridge from a Windows process to GStreamer; the
plugins in the table above are what actually decode. A machine with the bridge
and no `libgstlibav` decodes nothing, and a machine with the plugins and a
mismatched bridge is worse off still, because that failure looks exactly like
the fault the bridge was built to repair.

So the checklist for a machine elsewhere is both rows: the pair from category 4,
installed against an engine whose app name and version match `built-for.json`,
and the plugins by whichever of the two routes applies — staged from the user's
own GStreamer, or placed inside an engine the launcher builds. Neither half has
been tested without the other.
