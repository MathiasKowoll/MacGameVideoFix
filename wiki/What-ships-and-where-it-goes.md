# What ships, and where it goes

Every binary this project installs, what it is, and where it goes. Written
because "which files does a launcher have to copy" was asked and could not be
answered from one page. It used to promise five destinations; that number went
stale, so it is not promised any more.

Asking it has twice turned up something the page did not know: a category
nothing copied at all, and then a binary listed here as though it shipped while
no installer named it. Both are recorded below rather than quietly corrected,
because the second one was found by counting the files against a published
release instead of against this table.

## 1. Built here, from sources in this repository

Twelve DLLs are built here from the C sources beside them, and **eleven of them
travel**. Each rides on a library the game already loads, forwards every export
to the renamed original, and gets `DllMain` — that is the whole mechanism.
`check-builds.sh` rebuilds every one of them from the source named below and
refuses a release where the shipped copy differs.

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

## 3. The GStreamer codec plugins

The plugins CrossOver does not ship: `libgstlibav.dylib`, the decoders, and
`libgstmatroska.dylib`, the demuxer — with `libgstvpx.dylib` as well on the
engine route below. The titles that need one are the ones whose `codec` field in
the manifest names it; for some of them it is the entire fix and no DLL is
installed beside the game at all.

**Where they go depends on whose engine it is**, and the two routes are mutually
exclusive by design.

*Staged beside a bottle, for a CrossOver the user owns.* `stage-codecs.sh`
builds a directory of its own under
`~/Library/Application Support/MacGameVideoFix/gst-codecs/<engine>/<arch>/`,
with the two plugins in `gstreamer-1.0/` and the libraries they need in `lib/`
beside them, taken from the user's own GStreamer framework. A bottle reaches
them through one `GST_PLUGIN_PATH` line. **Nothing is written into CrossOver.**

*Carried inside the engine, for a copy this project made.* New in release 5.
`scripts/make-engine-copy.sh` copies all three plugins into the copy's own
`lib64/gstreamer-1.0`, beside CrossOver's own, and the nine support libraries
they need into `lib64` — from `runtime/engine-payload/lib64`, not from the
user's framework. No bottle on that engine needs `GST_PLUGIN_PATH` at all.

Applying both to one engine puts a second copy of each plugin on the search
path, which is two GStreamer cores in one process.
[How the codec staging works](How-the-codec-staging-works.md) is the whole
mechanism, both routes, and the state of the check that is meant to keep them
apart.

## 4. Built here, installed into the engine

Three files, all produced by this project, all sitting in the engine on this
machine. `scripts/build-winegstreamer.sh` builds them from the wine tree under
`~/Development/sources/wine`, into a build directory of its own, and an
`engine-built-for*.json` beside them records what they were built against:

    engine 26.3.0.39832, wine-11.0-8726-g2e2f5fca349,
    patches 0002 0003 0006 0008 mgvf-0001

**The patch line lives in this repository now**, in `source-patches/`, with a
`README.md` saying what each patch is for and whose it is. The build used to
point at another project's patch directory, so the pair could only be rebuilt
on a machine that had that project installed. `mgvf-0001` is ours, and
it is the whole of the METAL GEAR SOLID: Peace Walker fix. `0002`, `0003`,
`0006` and `0008` are carried unchanged from
**[winevideo](https://github.com/Jfishin/winevideo)**, each with a header saying
it is not ours; `0003` does not apply without `0002`, and `0008` needs its
predecessors.

| file | size | what it is | packaged |
| --- | --- | --- | --- |
| `winegstreamer.dll` | 2,330,624 | Wine's GStreamer bridge, PE half | yes |
| `winegstreamer.so` | 222,376 | the same bridge, Unix half | yes |
| `d3d9.dll` | 1,576,960 | Wine's d3d9 with winevideo's video-bridge patches | **no, deliberately** |

**The pair ships twice, once per target engine.** `runtime/` holds
`engine-winegstreamer.dll`, `engine-winegstreamer.so` and
`engine-built-for.json` for the patched fork, and
`engine-winegstreamer-stock.dll`, `engine-winegstreamer-stock.so` and
`engine-built-for-stock.json` for stock `CrossOver.app`. A set is three files
sharing a suffix, and the empty suffix is a set like any other, so a bundle
carrying only one behaves exactly as it always did.
`install-engine-media.sh` matches `engine_app` in each JSON against the target
engine's identity and refuses when none matches.

**The winegstreamer pair is load-bearing.** It was built because codecs that were
present would not show. Both halves have to match each other and the engine they
were built against, which is why an `engine-built-for*.json` travels with every
set and why neither half can be shipped without the other.

**Both sets were built against stable CrossOver 26.3**, `26.3.0.39832`, and
`install-engine-media.sh` refuses any other version. That is what makes stable
26.3 the one supported engine here rather than one option among several.

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

`runtime/engine-payload/` holds the two load-bearing halves, and the codec
plugins beside them, laid out the way a CrossOver engine is, so a patcher
building its own copy can overlay the tree without a mapping table of its own:

    wine/x86_64-windows/winegstreamer.dll   ->  <CX>/lib/wine/x86_64-windows/winegstreamer.dll
    wine/x86_64-unix/winegstreamer.so       ->  <CX>/lib/wine/x86_64-unix/winegstreamer.so
    lib64/gstreamer-1.0/*.dylib             ->  <CX>/lib64/gstreamer-1.0/
    lib64/*.dylib                           ->  <CX>/lib64/
    built-for.json                          ->  not copied; read it first
    CODEC-LICENCES.md                       ->  not copied; the notices and the
                                                sha256 of every file above

The `lib64` half of that map is a 26.3 layout. 27 keeps its plugins somewhere
else, and the paragraph on destinations below says where and why guessing does
damage.

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

**A copy this project made answers to a name nothing was built for**, so the
name check on its own would refuse it. `mgvf-origin.json`, in the copy's
`Contents/SharedSupport/CrossOver`, is what resolves that:
`install-engine-media.sh` reads `copied_from` out of it and uses the set built
for the original. The marker is the identity, not the name — a copy can be
called anything. Nothing else is accepted: an engine with no marker and no
matching name is still refused.

### Nothing ships for METAL GEAR SOLID: Peace Walker

Worth an entry here precisely because it is empty. No installer names the title,
no manifest row carries it, and nothing is placed in its game folder. Its fix is
the engine patch `mgvf-0001`, so anyone looking for a DLL to copy will not find
one and should stop looking.

Before, the game died the moment a pre-rendered cutscene started. With the patch
it plays: measured over about eleven minutes, two Media Foundation sessions, two
`.xmx` files, zero exceptions. Measured on CrossOver 26.3 and on no other
engine.

A green band along the bottom of the frame remains in **some** cutscenes, not
all. It is not caused by this patch — the buffer's current, maximum and
contiguous lengths all agree, so the band is already in the frame when it
arrives — and beyond that it is unexplained.
[The title's own page](Metal-Gear-Solid-Peace-Walker.md) has the measurement and
what would settle it.

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

## The copy, which is where categories 3 and 4 now go

Release 5 does not patch the CrossOver a person bought.
`scripts/make-engine-copy.sh` makes a copy, at the user's direction, to
`~/Applications/<Name>.app`, and everything this project changes in an engine is
changed there instead. The reason is worth stating plainly: so that nobody takes
a support complaint to CodeWeavers about a CrossOver we modified, and so the
original is always there to reproduce against.

**The order is the whole trick:** copy → every change → `codesign` →
`xattr -cr`. Signing before the last change leaves a seal the next change
breaks, and Finder then says the bundle is damaged and names an unknown download
date. That is Gatekeeper's wording for a signature that does not validate. It is
not about the date.

What goes in, in the order the script does it:

| step | what | where in the copy |
| --- | --- | --- |
| provenance | `mgvf-origin.json` | `Contents/SharedSupport/CrossOver/` |
| category 4 | the winegstreamer pair, through `install-engine-media.sh` | `lib/wine/x86_64-windows` and `lib/wine/x86_64-unix` |
| toolkit | only when `--gptk` names a directory already on the machine | `lib64/apple_gptk` |
| category 3 | the three plugins, and the nine libraries behind them | `lib64/gstreamer-1.0` and `lib64` |

`mgvf-origin.json` is written before anything is changed and records who made
the copy, when, what it was copied from, that engine's version, why a copy
exists at all and the order above. It is how our installers recognise one, and
it is the identity a copy has — the name is not, because a copy can be called
anything.

**The pair's originals are kept.** `install-engine-media.sh` sets CrossOver's
own halves aside as `winegstreamer.dll.mgvf-stock` and
`winegstreamer.so.mgvf-stock`, and refuses to take a backup whose contents are
already our own build — a backup that is the patch makes `--restore` reinstall
the patch and report success.

**The toolkit is not ours to carry.** `--gptk` names a directory already on the
machine; without it the copy keeps the toolkit CrossOver shipped. When it is
given, the engine's own `lib64/apple_gptk` is moved aside **whole** to
`lib64/apple_gptk_bak` — the scheme the app's Restore button already uses, so
that reverting really reverts, and so that both halves stay together by
construction rather than by care.

**Three checks have to pass before a copy is handed to anybody:** a valid ad-hoc
signature, no quarantine attribute left anywhere in the bundle, and
`wineloader --version` producing output. The last catches a copy that is being
killed on launch, which the first two cannot see.

A copy is a copy. It is not a product, nobody can go and get one, and the
CrossOver it was made from is CodeWeavers' and stays exactly as they shipped it.

## The GStreamer prerequisite, and how it stopped applying to everyone

**It applied to every route until 2026-08-29**, because nothing in this project
and nothing in RaccoonBot redistributed a decoder: the routes differed in where
the plugins were *put*, never in where they *came from*, and both ends took them
from a framework the user had to install.

That is no longer the whole story, and the payload rows below are why. The
section keeps the history because the reasoning is what justifies the change,
and because this page has already been wrong about it twice.

| | where the plugins are placed | where they come from | user must install GStreamer |
| --- | --- | --- | --- |
| **Our installers, staging** — category 3 | a staged directory per engine, `GST_PLUGIN_PATH` | the user's framework | **yes** |
| **Our own engine copy** — category 3 | inside the copy's `lib64/gstreamer-1.0` | `runtime/engine-payload/lib64` | **no** |
| **A patcher, before this change** | inside the engine it builds | the user's framework | **yes** |
| **A patcher, from the payload** | inside the engine it builds | `runtime/engine-payload/lib64` | **no** |

**The rows that take the plugins from the payload are the decision taken on
2026-08-29**, and the reason the rest of this section is worth reading rather
than skipping: a dependency on the user having installed exactly 1.24.13 fails
silently in both directions. Absent gives a black cutscene; a different 1.24.x
is worse, because it looks installed. The payload pins it.

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

**All three surfaces do announce it, and that was worth checking rather than
assuming.** `stage-codecs.sh` refuses outright when the framework is absent, and
otherwise prints the version it found and compares its series against the engine
core it is staging for. The app carries a staging step of its own and says per
title — *"Also needs the VC-1 codec staged."* RaccoonBot reads `GStreamerStatus`
into its Options screen and, for someone who installed the wrong release, names
both versions in one sentence: *"GStreamer 1.24.14 is installed, but 1.24.13 is
the one…"*, with an Install button when it is missing entirely.

An earlier version of this paragraph said nothing announced it. That was written
without opening any of the three, and each one disproves it.

**The failure it still produces is worth knowing.** Ignore all of that and the
engine ends up as stock 26.3 is — seventeen plugins, no `libgstlibav`, no
`libgstmatroska`, no `libgstvpx` — and the first sign is a black cutscene in a
title whose fix is installed and reporting healthy. The fix *is* healthy; it has
nothing to decode.

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
half-filled `lib64` hides the engine's own nineteen plugins.

### Where the engine's codecs actually came from: winevideo

Answered by hashing every CrossOver on the machine instead of reasoning about
sizes, after two of us concluded they came from "a GStreamer that is gone".

They come from **[winevideo](https://github.com/Jfishin/winevideo)**, whose 0.5
release carries the identical files. All eight, byte for byte:

| file | bytes | same as winevideo 0.5 |
| --- | --- | --- |
| `libgstlibav.dylib` | 267,696 | yes |
| `libgstmatroska.dylib` | 366,768 | yes |
| `libgstvpx.dylib` | 110,416 | yes |
| `libavcodec.60.dylib` | 13,607,312 | yes |
| `libavformat.60.dylib` | 1,995,760 | yes |
| `libavutil.58.dylib` | 750,560 | yes |
| `libswresample.4.dylib` | 172,000 | yes |
| `libavfilter.9.dylib` | 153,664 | yes |

Both engines carry twenty plugins where stock 26.3 carries seventeen — counted
as `.dylib` files, because the directory also holds an `include` directory and
an earlier count here took that for a plugin. Stock CrossOver has no
`libgstlibav` at all, and CrossOver Preview has none either — so the only build
on this machine that ships these decoders is winevideo's, and on 26 August they
were copied from it into the patched engine.

**So there is a third source, and it is the one that was actually used.** The
table above describes the two routes that take plugins from the user's own
GStreamer framework. Neither is where these came from. winevideo redistributes
the decoders inside its CrossOver build, and this engine inherited them.

This matters for the bundling question. If the decoders that are demonstrably
working came out of a CrossOver build rather than a framework install, then
"the user must install GStreamer 1.24.13" is the prerequisite for the *staging*
and *patcher* routes, and not a description of how this machine got working
video. It also means the files are already being redistributed by somebody, in
a build this project uses.

**And the argument against re-patching disappears entirely.** It was that
re-patching would swap a working decoder for a framework copy that had never run
here. It would not: the payload was taken from the same winevideo build the
engine's own copies came from, so a re-patch installs the identical bytes.
Measured rather than reasoned — all twelve files in the engine being played on
compare equal to the twelve in `runtime/engine-payload/lib64`.

So the three reasons to re-patch stand undiminished — d9vk missing in both
architectures, our `d3d9` occupying its path, codecs arriving by a route nobody
recorded — and the cost that was set against them is zero for the codecs.

The hashes are recorded below anyway, because the cheapest moment to write down
what works is before changing it, and because "it was identical when we checked"
is only worth something if somebody wrote down what it was identical to.

### So they travel now

Since they are winevideo's and identifiable, they can be carried rather than
borrowed. Twelve files, 21,632,848 bytes, are in `runtime/engine-payload/lib64`
with their sha256s in `CODEC-LICENCES.md`, and `check-builds.sh` fails when one
of them drifts — verified by altering a byte and watching it fail, because a
check that has never failed is not known to work.

The list is the **transitive closure** of what the three plugins need and stock
CrossOver does not already carry, walked with `otool -L`: that is why `liborc`,
`libvpx`, `libz`, `libbz2` and `libswresample` are in it, and it is what makes a
copy of the tree leave no dangling `@rpath`. Adding a plugin later means walking
it again, not appending a filename.

They are LGPL and BSD, dynamically linked and each replaceable on its own, which
is the mechanism the licence asks for. `CODEC-LICENCES.md` carries the notices
and the corresponding-source offer.

**This does not retire the framework route.** A user who keeps their own
CrossOver is still staged from their own GStreamer, because this project does
not modify an engine somebody else owns. What has changed is that "whoever
builds the engine" now includes us: the copy described above takes its plugins
from the payload. The reason for the two routes is no longer "we do not modify
an engine" but "we do not modify *the user's* engine".

### And it plays

Devil May Cry 5 was played on 2026-08-29 and its video runs. That title is the
cleanest possible subject: codec-only, no proxy, no bridge, nothing of this
project inside the process. The engine's own plugin is the only thing that can
have decoded it.

So the arrangement is proven — plugins placed inside an engine reach a game —
and now the plugins are identified as well. The set that works, recorded because
the cheapest moment to write down a working configuration is before changing it:

| plugin | bytes | sha256 |
| --- | --- | --- |
| `libgstlibav.dylib` | 267,696 | `b748843c176a4715d111036674cf6859d8f43fc6b4e98a3abaa5750d57233ac9` |
| `libgstmatroska.dylib` | 366,768 | `9e7d08da9252f30113732981c214323faa13f12648cf3a6bbb48ee88bce0c1b2` |
| `libgstvpx.dylib` | 110,416 | `2afef0cee64b0bd606660aaf2294dae7d68049e235814fa99dbd3fd1f1b7c14c` |

**It proves nothing about portability.** A title working here says the placement
works, on a machine that has both winevideo's build and GStreamer 1.24.13. A
user who has neither still gets seventeen plugins and no `libav`, and that is the
question the bundling proposal answers and this test does not touch.

## What a launcher has to copy

- Everything in categories 1 and 2 arrives in `fixes-v<version>.tar.gz`, named
  per title in `manifest.json` under `files`, with the installer named in
  `script` and its argument's meaning in `scope`.
- Category 3 is not per-title: `stage-codecs.sh` travels in the same bundle and
  is run against the engine, not a game. The manifest's `codec` field says which
  titles need it. The plugins themselves travel too — all twelve dylibs, flat,
  with `CODEC-LICENCES.md` beside them — for a launcher that places them inside
  an engine of its own rather than staging them.
- Category 4 travels too, and is **not** under `games` — nothing about it is
  per-title, and a nameless row in a list keyed by title is a row someone will
  try to match against a game folder. It has its own top-level block, which says
  where each file goes and what it was built for:

```json
"schema": 3,
"engine": {
  "script": "install-engine-media.sh",
  "scope": "engine",
  "files":  ["engine-winegstreamer.dll", "engine-winegstreamer.so", "engine-built-for.json"],
  "install": [
    {"file": "engine-winegstreamer.dll", "dest": "lib/wine/x86_64-windows/winegstreamer.dll"},
    {"file": "engine-winegstreamer.so",  "dest": "lib/wine/x86_64-unix/winegstreamer.so"}
  ],
  "codecs": {
    "licences": "CODEC-LICENCES.md",
    "why": "…",
    "install": [
      {"file": "libgstlibav.dylib", "dest": "lib64/gstreamer-1.0/libgstlibav.dylib"},
      {"file": "libavcodec.60.dylib", "dest": "lib64/libavcodec.60.dylib"}
    ]
  },
  "builtFor": {"app": "Crossover_patched.app", "version": "26.3.0.39832",
               "wine": "wine-11.0-8726-g2e2f5fca349"},
  "why": "…"
},
"engineSets": [
  {"app": "CrossOver.app", "version": "26.3.0.39832",
   "wine": "wine-11.0-8726-g2e2f5fca349", "patches": "0002 0003 0006 0008 mgvf-0001",
   "files": ["engine-winegstreamer-stock.dll", "engine-winegstreamer-stock.so",
             "engine-built-for-stock.json"]},
  {"app": "Crossover_patched.app", "…": "…"}
]
```

  The bundle is flat, so `files` are flat names and `install` carries the
  destinations. A patcher that would rather overlay a tree than read a mapping
  has `runtime/engine-payload/` for exactly that; the two are the same bytes.
- **`engine.codecs.install` is not installed by the engine installer.** Two of
  its pairs are shown above; there is one per file in `CODEC-LICENCES.md`.
  `install-engine-media.sh` installs the pair and nothing else, so the codecs
  are the launcher's to place — or ours, through `make-engine-copy.sh`. A reader
  who assumes the engine installer carries them ends up with a bridge and
  nothing to decode, which is the failure this section closes on.
- **`engineSets` is one entry per set the bundle can serve**, and the set for
  stock CrossOver sorts first. `engine` above still names the unsuffixed set and
  has not moved, so a reader that already knows this manifest keeps working; it
  is not enough on its own, because a bundle carrying only the set for a patched
  fork is useless to somebody holding stock CrossOver and the manifest gave no
  way to see that a second set was sitting right there.

**Decoding takes two things, and category 4 is only one of them.** The
`winegstreamer` pair is the bridge from a Windows process to GStreamer; the
plugins in the table above are what actually decode. A machine with the bridge
and no `libgstlibav` decodes nothing, and a machine with the plugins and a
mismatched bridge is worse off still, because that failure looks exactly like
the fault the bridge was built to repair.

So the checklist for a machine elsewhere is both rows: the pair from category 4,
installed against an engine whose app name and version match the set's
`engine-built-for*.json` — or which records itself, in `mgvf-origin.json`, as a
copy of one that does — and the plugins by whichever of the routes applies:
staged from the user's own GStreamer, or placed inside an engine that is built
or copied. Neither half has been tested without the other.

## What a launcher has to pass

Copying the files is half of it. These four environment variables are the
interface between the bundle and whatever drives it, and three of them were
added the night a launcher's install wrote a KINGDOM HEARTS override into the
Battle.net bottle.

| variable | what it does |
| --- | --- |
| `MGVF_BOTTLE` | **The one bottle to touch.** Absolute path to the bottle *directory*, the one holding `drive_c`. A trailing slash is stripped. An invalid value is an error, never a fallback. |
| `MGVF_FRONTEND` | Any non-empty value means *a program is driving this, not a person*. With it set, an unset `MGVF_BOTTLE` is an **error** rather than a scan. |
| `MGVF_STATUS_ONLY` | `1` forces `--status` whatever argument was given. Makes a survey pass structurally read-only. |
| `MGVF_BOTTLES` | **Plural, and older.** *Adds* a root to search. One character from `MGVF_BOTTLE` and the opposite meaning. |

**`MGVF_` names this bundle, not an application.** All four are read by the
scripts, which travel in the tarball; the app reads none of them and only sets
two. Somebody who has only a launcher and never installs MacGameVideoFix has the
scripts and the contract works unchanged.

**Why the frontend flag exists.** Unset used to mean "scan", for everybody. That
is fine for a person at a terminal and is exactly how a launcher build that
forgets to pass a bottle fails: silently, writing where it was never told to.
A person with neither variable still gets the scan.

**Verifying an install.** `--status` answers `installed`, `half`, `broken` or
`absent`, and it already checks **every** executable the fix covers — six for
KINGDOM HEARTS 1.5+2.5, two for 2.8 — so a launcher that reads it gets the whole
answer without reading `overrides` itself.

**What `--status` does not answer** is whether the game works. It reports files
and registry keys. KINGDOM HEARTS 2.8 answers `installed` and does not play; the
manifest carries no field to distinguish that, and anything rendering a green
tick from `--status` alone will eventually render a true statement about the
files and a false one about the game.
