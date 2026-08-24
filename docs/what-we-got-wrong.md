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

## Process

- **`git add -A` swept a half-rewritten script into a commit.** Files are listed
  explicitly now.
- **Editing a generated page instead of its generator.** `wiki/games.py` writes
  the games table into several pages; editing the pages means the next run puts
  the withdrawn claim back.
