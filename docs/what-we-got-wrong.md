# What we got wrong

Internal. This is the record of mistakes made while producing the results in
[Findings](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings) — it
is kept out of that page deliberately, because that page is addressed to whoever
maintains Wine, D3DMetal, DXMT or CrossOver's packaging, and none of this helps
them.

It is kept because most of these cost a run, several cost a wrong claim that
reached the wiki, and the same shapes keep recurring.

## Claims that reached the wiki and were withdrawn

- **That the H.264 half explains the Life is Strange crash on 26.3.** The policy
  table arms that half for Beast of Reincarnation alone, and Beast works on
  26.3. Published across the README, both title pages, Findings and the table
  generator before a reviewer flagged it.
- **That DYNASTY WARRIORS needs a VP9 decoder, or needs winevideo.** Both builds
  decode VP9 identically; what stable lacks is a WebM demuxer.
- **That the DYNASTY WARRIORS handle problem was blocked upstream, and that
  winevideo had already solved it.** Neither was true.
- **That the winevideo paired run covered more than one title.** It covered
  Mortal Shell 2 and nothing else.
- **That DYNASTY WARRIORS was never launched on 26.3.** It had been, and it
  crashed. A measurement written up as an absence, in four places including the
  generator.
- **That both Life is Strange titles freeze while every other title crashes.**
  One edit changed the symptom word on every row at once when only two titles
  froze.
- **That `image_presenter` is a stub in Wine.** Only `StartPresenting` and
  `StopPresenting` are; `PresentImage`, `allocator_AllocateSurface` and
  `VMR9_SurfaceAllocator_InitializeDevice` are implemented.
- **That DXMT does not support Nioh.** The backend was not the variable; with
  the codec fixed, DXMT reached the same point and failed the same way.

The pattern in the first four: a claim restated in parallel on separate pages
survives correction, because fixing one copy leaves the others. That is why the
games table is generated from `wiki/games.py` and not maintained by hand.

## Mistakes that cost a run

- **Renaming `dstoragecoreeeee.dll` back to `dstoragecore.dll`.** Read as damage
  and repaired. It was the working configuration — four added letters is how a
  person disables a DLL — and undoing it introduced the entire DirectStorage
  crash that followed.
- **Publishing a vtable slot before saving the original.** A vtable is shared by
  every instance and read by every thread, so the replacement is live the
  instant the slot is written. Two crashes.
- **Logging `DSTORAGE_QUEUE_DESC.Name` from the wrong offset.** Assumed +8; it
  is at +16, so `vsnprintf` walked `0x2000` as a string. Two runs lost.
- **Moving the NV12-to-BGRA converter without the call that fills its clamp
  table.** A zeroed table turns good input into a uniformly black frame, which
  looks exactly like a bridge that is not working.
- **Hooking one half of a two-half bridge.** The invented share handle is
  meaningful only to the hook that reads it back; handing it out without that
  hook turned a clean failure into `0xC0000005`.
- **Replacing an installed DLL after the user had already launched the game.**
  The run measured the previous binary.
- **Building a proxy against an already-renamed original**, producing forwarders
  to `amd_ags_x64_real_real.dll`.
- **Passing a game path where the script expected an architecture**, creating a
  staging directory named after the CrossOver app.

## Traps we had already written down and then walked into

- **Imports by ordinal.** Findings has said for months that an entry in an
  import table is either a name or an ordinal, that a hook walking names skips
  the ordinals, and that `d3d12.dll` exports `D3D12CreateDevice` as ordinal 101.
  The probe implements the countermeasure. The bridge never did — it had no
  ordinal hooking at all — and nobody noticed for as long as the bridge existed,
  because DYNASTY WARRIORS reaches that function through Streamline at runtime
  and the `GetProcAddress` substitution caught it. Wo Long calls it straight
  through its own table and the bridge armed nothing.

  Writing a trap down is not the same as defending against it, and the two
  places that needed the defence had diverged.

## Reading errors, which are worse than crashes

These produced confident wrong conclusions rather than obvious failures.

- **`grep` without `-a`.** The runtime log contains bytes invalid as UTF-8, so
  grep treated it as binary and printed nothing. Read as "the call never
  happened". It had happened every time.
- **Only failures were being logged.** The probe recorded `CreateFileW` when it
  failed and not when it succeeded. The absence of any movie being opened was
  about to be reported as evidence that none was.
- **Reading a shared log as one game's.** The runtime log is per bottle, not per
  title, so lines from Beast of Reincarnation were attributed to Reunion. The
  project's own support-bundle design documents this trap. Log lines now carry
  the executable name.
- **`DisableGpuDecompression=0` read as "already off".** The field is named
  `Disable*`, so `0` means the feature is enabled.
- **`Direct3DCreate9Ex -> 0x00000000` read as a null return.** It returns an
  `HRESULT`; that value is `S_OK`.
- **Attributing a crash without the comparison that would settle it.** The first
  Nioh run with the bridge installed crashed, and no earlier dump had been kept
  to compare addresses against. The attribution was made by reasoning from the
  log instead — correctly, but the stronger test had been available earlier and
  was not taken.

## Hypotheses that did not survive

Recorded because the reasoning was sound and the answer was still no.

- **That Nioh 3 checks NV12 support and gives up.** winevideo carries a patch
  whose stated reason is that D3DMetal cannot create NV12 textures, and Nioh 3's
  silence between `MFStartup` and `MFShutdown` fitted a capability check
  perfectly. A probe on `CheckFormatSupport` showed the game never asks about
  NV12 at all.
- **That the staged codec was what made Persona 5 Strikers play.** It played
  while the plugin was never loading. What decodes its VC-1 is still unknown.

## Shipped depending on something nothing sets

- **NieR Replicant's fix never worked outside this machine.** Its delivery path
  only runs if the bridge answers yes to `ID3D11VideoDevice`, and that answer
  was gated on the D3D12 side being armed — which NieR never is. What kept it
  working during development was `MGVF_VIDEO_DEVICE`, an environment variable
  written for one experiment. Nothing in the installer, the app, the bottle or
  any script has ever set it. So the title was measured under a lever, shipped
  without it, and played its video to a black screen for every user of 4.3.x.
  Found only because a rebuild happened to run in a session where the variable
  was no longer set.

  The gate itself had been correct when written: the D3D11 delivery did not
  exist yet, so answering yes with nothing behind it really was a black screen.
  The delivery was then implemented and the gate was not removed — the variable
  was used to step around it instead. That is the actual failure: a workaround
  applied to our own stale guard, in a place where deleting the guard was the
  whole fix.

  This is the same shape as *four levers left forced on*, one entry down, and it
  is worse, because that one only mismeasured runs. This one shipped.

## Checks that were wrong about writes that had succeeded

Twice in one session, and the same shape both times: a verification that
consulted the wrong thing and reported failure for work that had been done.

- **Reading `user.reg` to confirm a registry override.** wineserver flushes
  that file on its own schedule, so a key written a moment earlier is usually
  not on disk. The installer treated that as a failed write and aborted a
  KINGDOM HEARTS HD 1.5+2.5 install that had in fact succeeded — after copying
  the files it would then have to roll back. It asks the registry now.
- **Driving a Preview bottle with the release CrossOver.** The bottle belongs
  to CrossOver Preview; running `reg.exe` against it from
  `/Applications/CrossOver.app` makes Wine try to update the environment,
  which fails with `failed to load start.exe: c000000d` and takes the command
  with it. A verification sweep then reported that none of eight executables
  had its override — minutes after one of those executables had demonstrably
  loaded the bridge and played a video. The installer gets this right, trying
  Preview first; the ad-hoc commands typed alongside it did not.

The lesson is not "check more carefully". It is that **a check contradicting an
observed fact is a broken check until proven otherwise**, and the correct first
move is to doubt the instrument. Both times the observed fact — an install that
worked, a video that played — was sitting right there in the same session.

## Mistakes on Kingdom Hearts

- **Read data directory 0 instead of 1.** Index 0 is the export table; imports
  are index 1. The parser dutifully printed the executable's own name as a
  module and a page of mangled Steamworks symbols as its imports, twice, before
  the off-by-one was obvious.
- **`find_bottle` returned the first bottle with a `dinput8.dll`.** On a Mac
  with eight bottles that was Battle.net, so the registry override went into a
  bottle that will never run the game, and the "original" `dinput8` copied
  beside the game came from a different CrossOver build than the one that runs
  it — a genuinely different file, confirmed by hash. Shipped in the NieR
  installer since the day it was written; it had worked only because the
  alphabetically first bottle happened to be the right one at the time.
- **`find_bottle() { find_bottles | head -1; }` under `set -o pipefail`.**
  `head` closes the pipe, the writer takes SIGPIPE, and the pipeline reports
  141. The installer read that as "no bottle found" and aborted — after the
  `--restore` half had already removed the DLLs. The user launched a game with
  no bridge in it because of a shell idiom.
- **Attributed the shared handle to the wrong texture.** The log line naming a
  2048x1080 share target sits next to the `GetSharedHandle` call, so it looked
  like the game was sharing its own surface at that size. It is not: the handle
  belongs to the texture this bridge published, and the 2048-wide one is the
  game's own, logged because it was the first shared texture seen. An entire
  experiment — rebuilding the bridge texture at 2048x1080 — was designed on
  that misreading and changed nothing, which is the only reason it was caught.
- **Called a result worse when the user had called it better.** Refusing
  `IMFDXGIBuffer` turned a green screen into a black one. Read from the log
  alone that looked like a regression; the person watching the screen said it
  was an improvement. Both were true about different things, and only one of us
  could see the screen.
- **The monitor's filter did not include the line it was waiting for.** Spent
  two turns waiting for a luma measurement that the grep was dropping.
- **Framed a working thing as broken.** Said 0.2 Birth by Sleep was "out of
  reach for this approach" on the strength of it never touching Media
  Foundation. It plays fine — software MPEG-1 through CriWare is exactly the
  case that was never going to fail. The measurement was right and the sentence
  built on it answered a question nobody had asked.
- **Reported an inferred name as measured.** Said the `.usm` video was
  "Sofdec.Prime". The measured fact is MPEG-1 syntax and `mpeg_codec = 1`; the
  marketing name for that enum value is an inference and was stated as though it
  had been read off the bytes.

## Mistakes on Tormented Souls 2

**Four runs into DXGI when the import table said GDI.** The first hypothesis
was that DXGI was reporting something wrong, and four launches went into
proving it before anything about the executable was read. The import table
named `EnumDisplaySettingsW` — a GDI entry point, not a DXGI one — and could
have been dumped without launching the game once. The fix did turn out to live
in DXGI, which does not make the four runs less wasted: they were spent on a
guess that a two-second read would have redirected.

**A `DXGI_MODE_DESC` stride of 20 bytes instead of 28.** The structure is
Width, Height, a `DXGI_RATIONAL` of two `UINT`s, Format, ScanlineOrdering,
Scaling — 28 bytes. Walking the array at 20 read each entry starting partway
into the last one and produced resolutions nobody had offered, including a
confident claim that the largest available mode was 730 pixels tall. Two
hypotheses were built on that number before the stride was checked. The real
list had 26 modes and topped out at exactly the panel size. A wrong stride does
not look like an error — it looks like data.

**A hook installed in the import table for a function resolved at runtime.**
The guard for `D3D11CreateDevice` was written into the game's import table,
which is empty of it: the game calls `GetProcAddress` for it. The guard
therefore never installed, and its silence was briefly read as evidence about
the game rather than about the hook. This is the same shape as an earlier
mistake with a carrier DLL, already written down in this file, and it was not
recognised in time.

**Twice called a fix verified by a run that never reached it.** The game
started, the user confirmed it, and it was written down as working. The log said
otherwise both times: the mode-list guard was installed and never entered, so
whatever made the game start, it was not the code under test. The cause is that
Tormented Souls 2 caches its filtered resolution list in
`Saved/SaveGames/Settings.sav` and reads it back rather than enumerating again —
one good launch bakes the result in, and every later launch works with or
without the fix. The verification that counted required deleting that file.

The general form is worth keeping: **a guard that is installed is not a guard
that ran, and a game that starts is not evidence that the thing under test did
anything.** The log line that proves a guard fired is cheap to check and it was
skipped twice. It is the same error as reading a call's success as proof of the
mechanism behind it, which appears twice more in this file.

A related one, from the same game: the `1_0_CORE` retry fires twice on every
launch of it, and for a while that looked like the repair. It is not — the game
went on crashing at the same address while both retries were succeeding. A guard
firing says the fault it watches for is present, and nothing at all about
whether it is the fault that mattered.

**A version-specific address nearly shipped.** The working diagnosis came from
patching a vtable slot at a hardcoded RVA, valid for exactly this build of this
game. It confirmed the cause and it could not travel in a published fix — a
patch aimed at a fixed offset in someone else's build writes into whatever
happens to be there. It was stripped before packaging, and the released guard
touches only interfaces it looks up at runtime. Worth stating because the
temptation was real: it worked.

## Running a bottle under an engine it was not wired for

An afternoon on a game that stalls at its loading screen was spent with two
copies of libgstreamer in the process, and the terminal had been saying so from
the first run:

    Class GstCocoaApplicationDelegate is implemented in both
      /Applications/CrossOver.app/.../libgstreamer-1.0.0.dylib
      /Applications/CrossOver Preview.app/.../libgstreamer-1.0.0.dylib
    This may cause spurious casting failures and mysterious crashes.

The bottle was wired for Preview, correctly. The runs were launched with the
stable engine, to see whether the engine made a difference. A staged codec tree
reaches its libraries through symlinks into one CrossOver's bundle, so loading
the plugin under a different engine drags that engine's libgstreamer in beside
the running one. Removing the variable took the game from 24 fps to 61.

It was not the cause of the stall. It was underneath every measurement taken
before it was noticed, which is worse in its way -- an unclean run that still
fails teaches nothing, and several conclusions were drawn from those runs.

Two things about how this was diagnosed are worth keeping.

**The app was blamed before it was read.** This was written up here, out loud, as
"a published defect in this project": a bottle-wide variable pinned to one
engine. Reading the code afterwards showed the app already resolves the staged
path from the engine the bottle records, re-points it when the bottle migrates,
and carries a `.map` of version to path for exactly this. The comment beside it
even describes the failure -- "a bottle migrated to another CrossOver kept
pointing at the previous engine's directory for good". The defect was diagnosed
from the symptom and attributed without checking whether the thing being blamed
already handled it.

**The real gap was one level up.** `diagnostics/launch-with.sh` exists precisely
to run a bottle under a different engine, and said nothing about the codec path.
It now looks the engine up in `.map`, says whether the staged codec matches, and
overrides `GST_PLUGIN_PATH` for that run when it does not. The tool built for
crossing engines is the one that has to know what crossing them costs.

## Reading a log while it was still being written

Twice in one day, and the second time it nearly threw away a correct answer.

Persona 4 Golden: two runs were diffed to see whether the fault was
deterministic. The second file was still being written; the diff showed the
crash and the totals missing and was read as "this run behaved differently". It
had not. Waiting five seconds gave two byte-identical logs.

Devil May Cry 5, the same day: a GStreamer trace was opened at ten lines,
showed `h264parse` and `vtdec_hw`, and the VC-1 hypothesis was withdrawn out
loud as "incorrect, and I built a story on it". The log later reached 10,556
lines and contained `asfdemux`, `avdec_vc1`, `format=(string)WVC1` and
`Using libavcodec version 60.3.100` — the hypothesis, confirmed by name, in the
part that had not been written yet. The game plays H.264 as well; the first ten
lines were true and were not the whole truth.

Withdrawing a claim is the right move when it is wrong. Withdrawing it from a
partial read is the same error as making it from one, and it costs more, because
the retraction sounds like rigour.

The rule is cheap: a log that is still growing is not a measurement. Check the
size twice before drawing anything from it.

## Process

- **`git add -A` swept a half-rewritten script into a commit.** Files are listed
  explicitly now.
- **Editing a generated page instead of its generator.** `wiki/games.py` writes
  the games table into several pages; editing the pages means the next run puts
  the withdrawn claim back.

## Open, and not explained yet

- **Devil May Cry 5 and Beast of Reincarnation want different winegstreamers.**
  Bisected the same night, one variable at a time, with winevideo's plugins
  inside the engine for both runs:

  | winegstreamer | Beast of Reincarnation | Devil May Cry 5 |
  | --- | --- | --- |
  | winevideo's | plays, 421 frames | crashes on the video |
  | stock CrossOver 26.3 | stalls after a GOP | plays |

  So the codecs are exonerated -- they were present either way -- and the
  conflict is the binary. winevideo's `winegstreamer` carries some thirty-five
  patches; Beast needs two of them (`0018`, `0019`, the queue time bounds) and
  one of the others is enough to take DMC5 down.

  That is an argument for porting the two patches rather than transplanting the
  whole file, which was the shortcut taken because the Wine builds matched. It
  also means an engine cannot currently serve both titles, and choosing per
  title is the only thing that works today.

  Two hypotheses were spent getting here and both were wrong, which is the part
  worth keeping. The first was that the four things changed at once could not be
  separated; they could, with one launch each. The second was the plugin
  scanner: no CrossOver ships one -- the path compiled into the core is
  `/opt/cxoffice/libexec/...`, which exists on no Mac, so every engine scans
  plugins in-process and a plugin that faults on load takes the game with it.
  True, measured, and not this: the scan completed and wrote a full registry,
  and DMC5 crashed afterwards, on the video.

- **What the engine change looked like before it was bisected, 2026-08-26.** It had
  played twice that evening: once on the 27-based engine with the codecs staged
  per bottle, and again with the same plugins placed inside that engine and the
  staging directory moved aside, which was the measurement that validated
  placement at all. After the engine was rebased on CrossOver 26.3 and
  winevideo's `winegstreamer` pair was transplanted into it, it stopped.

  Four things changed between the run that worked and the run that did not: the
  engine's base, the transplanted `winegstreamer`, the plugin set (winevideo's
  builds rather than ours), and the removal of `GST_PLUGIN_PATH` from the bottle.
  Nothing here separates them, so nothing here says which.

  The title leaves no trace to read: it carries no DLL of ours, so there is no
  log, and it writes no crash report of the kind Unreal titles do. What is known
  is only what the plugin cache says, which is that the engine's own plugins are
  the ones registered and `avdec_vc1` and `avdec_wmv3` are among them.

  The cheap bisect, when someone picks this up: the stock `winegstreamer.so` and
  `.dll` were kept beside the transplanted ones as `.stock`. Putting those two
  back separates the transplant from the engine change, at the cost of Beast
  stalling again while the test runs.

- **NINJA GAIDEN 4: the trail ended where our own view did.** Its log stopped
  immediately after `MFCreateSourceReaderFromURL` returned, and that was read
  as the game dying there. It was not. The three reader hooks --
  `GetNativeMediaType`, `SetCurrentMediaType`, `ReadSample` -- were patched
  only in `MFCreateSourceReaderFromByteStream`, and this title creates its
  reader from a URL. Nothing after that point was ever going to be logged.

  Hooked on both paths since. What the runs then showed is worth keeping: the
  reader is created, all three slots are patched, and **not one of them is ever
  called** -- no native type asked for, none set, no sample read. The process
  dies between creating the reader and using it, with 33 lines written against
  a cap of 300, so nothing was truncated.

  And it is not deterministic. One run reached the video and played it; the next
  died before touching it. A media fault behaves the same way every time. This
  looks more like a race or a resource that is sometimes there.

  Then a second guess, also wrong: that the hook fired only for the first reader
  and the game opened one per movie. Hooked unconditionally, and the run is
  identical -- one reader, three slots patched, no calls.

  So the standing fact is stranger than either guess. The player reports the
  first video **starting and cutting off**, while the reader is created and never
  read from, and the decoder the title enumerated is never fed either: no
  ProcessInput, no ProcessOutput. Whatever draws those frames does not pass
  through any hook this DLL sets. That is where the instrumentation ends and
  where a later session would start -- probably by finding what the title calls
  between `MFCreateMediaType` and the picture appearing.

  Third time tonight that an absence in a log was read as evidence, after the
  frame counter that could only reach three and the ProcessInput failures logged
  on call 1 and every 200th. The rule keeps earning its place: an absence is
  only evidence if the thing that writes it was running. Twice tonight it was
  not, and the third time it genuinely was -- which is the only reason the last
  paragraph can be written at all.

- **NINJA GAIDEN 4 on the same engine.** Its enumeration reported a VP9 decoder
  for the first time -- the gate its fix exists to answer -- so the fix now
  stands its workaround down when the engine offers one. Run since, and the
  conditional took the new branch:

      MFCreateDXGIDeviceManager -- ALLOWED after all: this engine offered a decoder
      MFCreateDXGIDeviceManager -> 0x00000000
      IMFDXGIDeviceManager::ResetDevice(device=...) -> 0x00000000   << a device is bound

  and the intro video played, which it had not on that engine before.

  The run then ended in a crash dialog, and that crash was **not the title's**:
  another session killed the launcher while the game was running. It was minutes
  from being investigated as a defect of NINJA GAIDEN 4. Two people driving one
  machine is a measurement hazard of its own, and the cost is not the killed run
  -- it is the hours that would have gone into explaining it.

- **That 0018 and 0019 are what Beast of Reincarnation needs.** Said to the
  other session, written into the wiki page, into two commit messages and into
  a diagnostic's header comment, on the strength of their titles and of four
  strings found in winevideo's binary. Then the patches were read.

  `0019` reverts `0018`: the first adds a bounded demux queue behind an
  environment variable, the second removes it entirely. Net effect on the source,
  nothing. And both touch `wg_parser.c`, which is the source reader -- the path
  an Electra title does not use, which is the one thing about this title that
  was established early and firmly.

  The `max-size-*` strings that survive in the binary come from `0030`, which
  reintroduces the queue gated on a per-process feature flag, still on the parser
  path.

  What plausibly fixes it is `0008`, "always provide 2D-capable output samples":
  Beast's own log records `dwFlags=0x7` on stock, without `PROVIDES_SAMPLES`, so
  the caller allocates every frame -- the decoder cannot emit until Electra hands
  it a buffer, and Electra will not hand over more until it receives pictures.
  That is the stand-off exactly, and it is the one thing the game-side shim could
  not change: it wrapped the caller's buffer, it did not change who allocates.

  Not proven either. It is a candidate, and it is labelled one.

  What survives unharmed is the measurement -- winevideo's winegstreamer plays
  the title and stock does not, on five engines -- and check-engine-media.py,
  which still separates them correctly because those strings do distinguish the
  builds. Only the explanation of *why* was invented.

- **WINEDLLPATH does not redirect a builtin, tried 2026-08-26.** The idea was
  good and would have solved the conflict cleanly: leave the engine stock, keep
  winevideo's `winegstreamer` pair in a directory outside it, and let each game
  raise the one it needs through the per-launch environment the launcher already
  sets. The variable is honoured by that engine's `wine` and `ntdll.so` -- seven
  and two occurrences -- so it looked reachable.

  It is not. Beast loaded the stock builtin anyway: its log shows `YV12` offered
  and our relabel engaging, which is the stock signature. WINEDLLPATH is from
  when builtins were loose `.so` files; a modern builtin is a PE paired with a
  unixlib, both resolved from the engine's own directory.

  So per-title selection is possible at the granularity of the **engine**, not
  the DLL -- two engine copies, chosen at launch, which is something the
  launcher already does. Recorded because the next person will have the same
  idea, and the variable's presence in the binary makes it look like it should
  work.

## Where NINJA GAIDEN 4 stands, and it is two concrete gaps

The title works on the complete winevideo engine and not on the fork's with only
`winegstreamer` replaced -- not with winevideo's transplanted binary, not with
the four-patch build, and not with our own DLL removed. Reading winevideo's own
page for the title turned that from a mystery into a list.

**Its documented fix is patches 0002 through 0004.** We built with 0002 and 0003
and left out **`0004-mfplat-fall-back-to-BGRA-when-D3D11-device-can-t-cre`**,
which their page describes as falling back to BGRA when the D3D11 device cannot
allocate the requested video texture format. Their root-cause note names exactly
that as a second failure mode beside the missing decoder: *"A negotiated video
texture can also fail when the D3D11 device cannot create the requested format."*

**And the bottle is missing its byte-stream registration.** Their page says the
prepared bottle receives the VP9 MFT *and* `.webm`, `.mkv` and `.msd`
byte-stream registration. Ours has the MFT -- four mentions in system.reg -- and
**zero** of the three extensions. The file this title opens is
`Assets/Movies/88f75716-....msd`.

That fits what was measured and could not be explained: the reader is created,
returns success, and is then never read from. A byte-stream handler is what lets
Media Foundation open a container by extension at all.

Both gaps are cheap to close, and the second needs nothing new:
`diagnostics/registry/webm-bytestream-handler.reg` in this repository already
maps an extension to the GStreamer byte-stream handler, and
`apply-webm-handler.sh` applies it. It was written months ago to answer a
different question about DYNASTY WARRIORS.

**The mapping was tried and was not enough.** `.webm`, `.mkv` and `.msd` added to
the bottle, the title run with no DLL of ours in it, and it crashed the same way.
Recorded as done rather than pending. One caveat on that run: with our fix
uninstalled nothing logged, so it says the crash survives the mapping and does
not say whether the mapping was used.

**And `0004` is not a winegstreamer patch.** It touches `dlls/mfplat/sample.c`,
one hunk. So testing it means building and replacing **mfplat.dll** as well --
the second of the five binaries deliberately left alone, and a new binary in the
engine rather than another patch in one already verified. `scripts/build-winegstreamer.sh`
builds `dlls/winegstreamer/all` only; it would need a second target.

Order for tomorrow, one change at a time as everything else tonight was done:

1. Build `dlls/mfplat/all` with `0004` applied, install it beside the
   winegstreamer pair, and run the title.
2. Run Devil May Cry 5 straight after, because it is the title that says what a
   new binary cost -- and mfplat sits on far more paths than winegstreamer does.
3. If NG4 still crashes, the remaining difference is d3d9, qasf, quartz, ntdll,
   winevideo_compat, or the bottle their patcher prepares. At that point
   measuring which is worth more than adding them all.

## The engine check was wrong, and it was mine

`diagnostics/check-engine-media.py` read four strings out of `winegstreamer` --
`max-size-time`, `max-size-buffers`, `max-size-bytes`, `decodebin_parser_init_gst`
-- and reported whether an engine could play an Electra title. It agreed with
every measurement it was tested against: five engines, the four that lacked them
stalled and the one that had them played.

Then this project built its own `winegstreamer` with four different patches. It
carries **none** of those strings, and Beast of Reincarnation plays with it.

So the check was four points of correlation dressed as a mechanism. Worse, it had
already been caught: when the four-patch build was installed it reported
`ABSENT / stalls` and that was written off in a message as "do not believe it
here". Believing a tool selectively is not a fix. It is withdrawn.

Two things it dragged with it. The claim that `0018` and `0019` were what
unblocked the title was corrected once already -- `0019` reverts `0018` -- and
this closes it for good: four patches that touch none of the queue bounds
unblock it. And the idea that a port could be verified by reading strings is
gone with it; whether a rebuild carries a fix is answered by running the games.

What survives untouched is every measurement: winevideo's binary plays the title
and stock does not, and ours plays it and keeps Devil May Cry 5 alive. Only the
shortcut for predicting that was invented.

## NINJA GAIDEN 4 dies in ntdll, and we can name the instruction

Found by giving up on other people's logging. The launcher sets
`WINEDEBUG=-all`, which silences Wine's own crash output, so the fix DLL was
given a vectored exception handler instead -- it is already inside the process,
and it sees every exception before anything else does.

    EXCEPTION 0xc0000005 at 00006FFFFFF78D55  in C:\windows\system32\ntdll.dll (+0x38d55)
        access violation writing address 0x0000000000000000

Disassembled at that offset, inside **`RtlVirtualUnwind2`**:

    170038d4d:  movq 0xe0(%rsp), %rax     ; an optional out-pointer from the stack
    170038d55:  movq $0x0, (%rax)         ; written through, unchecked

So a caller passes NULL for an optional output parameter during exception
unwinding -- ordinary in a running game -- and Wine's implementation writes to
it. The title does not die in its own code, nor in ours, nor in winegstreamer.

The same byte pattern across the engines here:

| engine | ntdll | that instruction |
| --- | --- | --- |
| CrossOver 26.3 | `9ca9870f039f` | present |
| the fork's | `9ca9870f039f` -- byte-identical | present |
| CrossOver Preview 27 | `89b839533701` | absent |
| winevideo 0.5 | `938c5fc80bf7` | absent |

Which fits the title working on winevideo and failing on the fork. It does not
yet fit the report that it works on a stock CrossOver: 26.3 carries the same
ntdll as the fork. Either that run was on Preview, or something else keeps the
path from being reached. That is the one question left before this is a
finding rather than a strong lead.

Worth noting what it cost to see: three probes that showed nothing, two of which
were blind spots of our own making, and the answer came from stopping the search
for a log and catching the exception in the process we were already inside.
