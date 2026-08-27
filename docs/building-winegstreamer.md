# Building winegstreamer with the patches we need

The plan, written after measuring what is on this machine rather than from
memory. The goal is narrow: produce a `winegstreamer` pair -- the PE `.dll` and
the unix `.so` -- carrying the patches an Electra title needs, and **not** the
ones that break other titles.

## Why build rather than copy

Copying winevideo's pair works and was measured working: Beast of Reincarnation
plays 421 frames with it. It also breaks Devil May Cry 5, which crashes on its
video with that binary and plays with the stock one. Bisected the same night,
one variable at a time, with the plugins present in both runs.

That file carries thirty-five patches. Beast needs a handful of them. One of the
rest is enough to take DMC5 down, and copying is all-or-nothing.

A second reason, independent of the conflict: the two engines differ outside
video as well. The fork carries its own DXMT, DXVK and MoltenVK, and winevideo
carries different builds of all three. Adopting either engine wholesale trades
one project's work for the other's. Building the media half is what lets both
stand.

## What is here, measured

Everything, as it turns out. The plan first said two downloads were missing;
both were already on the machine and the second was never needed.

| | |
| --- | --- |
| CrossOver sources | present, `/Users/mathias/Development/sources` — **Wine 11.0**, which is what the engine reports (`wine-11.0-8726-g2e2f5fca349`) |
| `dlls/winegstreamer` | present, with `wg_transform.c`, `video_decoder.c`, `mfplat.c` |
| GStreamer sources | present in the same tree, **1.24.4** — the engine's own core version, measured |
| GStreamer headers | present there: `gst.h`, `video.h`, `audio.h`, `tag.h`, `pbutils.h` |
| GStreamer libraries to link against | in the engine's `lib64`, also 1.24.4 — an exact match rather than the 1.24.13 framework |
| llvm-mingw (PE i386 and x86_64) | present |
| macOS SDK | 26.5 |

`crossover-sources-26.2.0.tar.gz` is genuinely not needed: the tree here is the
right Wine and the patches land on it.

**The GStreamer development framework is needed after all**, and this note said
otherwise for a while. Headers being present in the source tree is not the same
as being able to build against it: `gstconfig.h` and `glibconfig.h` are
*generated* at configure time and do not exist here, and there are no `.pc`
files, which is what Wine's `WINE_PACKAGE_FLAGS(GSTREAMER, ...)` looks for.

Two ways out, and the first is better:

- **Download the GStreamer 1.24.x development framework.** Headers and `.pc`
  files, ready. It is what winevideo's own documented build uses, which removes
  a class of doubt about whether ours was generated the same way.
- Configure glib and gstreamer with meson in the source tree to generate them.
  meson 1.12 and ninja 1.13 are installed, so it is possible -- but it means
  building two projects to obtain headers, and what comes out may not match what
  the engine ships.

Either way the libraries to link against should be the engine's own `lib64`, at
1.24.4, rather than the 1.24.13 framework.

### Do the patches apply to this tree?

winevideo builds against 26.2 and this is 26.3, so they were expected not to.
Measured, they mostly do:

    0002   3 hunks   applies cleanly
    0006   4 hunks   applies cleanly
    0003   5 hunks   fails strict, applies with fuzz, no hunk lost
    0008   4 hunks   fails strict, applies with fuzz, no hunk lost

All four land. The two that need fuzz have shifted context and their hunks
should be read after applying rather than trusted -- fuzz means the patch found
somewhere plausible, not somewhere correct.

## The patch subset, which is the point of doing this

Do not replay all thirty-five. The ones on the path an Electra title uses are:

    0002  VP9/AV1 caps mapping, decoder input types
    0003  a real VP9 decoder MFT, advertised via MFTEnumEx
    0006  drop D3D awareness on macOS so the MFT can be chosen
    0008  always provide 2D-capable output samples

`0008` is the candidate for what actually fixes Beast: its log records
`dwFlags=0x7` on stock, without `PROVIDES_SAMPLES`, so the caller allocates every
frame -- the decoder cannot emit until Electra hands it a buffer, and Electra
will not hand over another until it receives pictures. That is the stand-off, and
it is the one thing a shim beside the game cannot change.

Explicitly not wanted, unless something later shows they are needed: `0018` and
`0019`, which cancel each other out and touch the source reader; and the
per-title patches for Soulcalibur, Kingdom Hearts and Mortal Shell II, which
duplicate fixes this project already ships as DLLs.

## Steps

1. Fetch both inputs. Check the tarball against the SHA-256 above.
2. Extract, `git init` and commit the pristine tree, so every later step is a
   diff against something known.
3. Apply the subset, one patch at a time, committing each. A patch that does not
   apply is information: it means the 26.2 tree it was written against differs
   from what was fetched.
4. Configure, and build only `dlls/winegstreamer`. The whole of CrossOver is not
   needed and building it would be a much larger undertaking.
5. Place the resulting pair in a copy of the engine, never the original.

## Verification, in this order

1. `diagnostics/check-engine-media.py` on the patched copy. It reads the built
   binary rather than trusting that a patch number was applied. Note what it
   actually detects: strings that distinguish the builds, not proof of any
   particular patch -- that distinction was got wrong once already.
2. **Beast of Reincarnation** must play past two frames. The number to beat is
   421.
3. **Devil May Cry 5** must still play. It is the title that fails with the full
   transplant, so it is the one that says whether the subset avoided the harm.
4. **NINJA GAIDEN 4** and **Persona 5 Strikers**, because both go through paths
   these patches touch and both are known-good today.

Adding patches one at a time and running that list is slower than replaying all
thirty-five, and it is the only way to end up knowing which patch does what --
which is precisely what was not known when the whole file was copied.

## It worked, and what it took

Built with `0002 0003 0006 0008` and installed in the launcher's own engine:

| winegstreamer | Beast of Reincarnation | Devil May Cry 5 |
| --- | --- | --- |
| stock CrossOver 26.3 | stalls after a GOP | plays |
| winevideo's, all 35 patches | plays | **crashes** |
| **ours, those four** | **plays** | **plays** |

So the thirty-one patches that were not wanted were also the ones doing harm.
This is a combination neither project publishes: the fork's graphics stack with
winevideo's video fixes and without the collateral.

Three things cost a run each and are now in the script rather than in memory:

- macOS ships **bison 2.3** and GStreamer wants 2.4. Homebrew's goes first on
  PATH, or meson stops before generating the enum headers.
- Declaring `--host` puts autoconf in cross mode and it **loses the SDK**. The
  symptom is "C compiler cannot create executables" while the same command
  works by hand. `SDKROOT` has to be explicit.
- The engine's dylibs carry an install_name of **`/opt/cxoffice/lib64/...`**,
  CodeWeavers' build prefix, which exists on no Mac. Link against them and the
  linker copies that path into the result, which then cannot load its own
  dependencies. The stock binary uses `@rpath` plus a second rpath three levels
  up; both have to be put back with `install_name_tool` afterwards. This one was
  found by building something that compiled, linked, installed, and crashed the
  game -- which is exactly what the "reproduce before patching" step exists to
  catch, and did.

And one that is not about Wine at all: **the build path must not contain a
space.** Under `Application Support` the failure reads as clang being handed
half a filename.

### Two things the tooling gets wrong here, deliberately recorded

`check-engine-media.py` reports this build as `ABSENT / stalls`. It is looking
for strings that come from patch `0030`, which we did not apply. It still
separates winevideo's build from stock, which is what it was written for, but it
cannot see our subset. The verdict came from the games.

And `--patches` order is the `series` order, not ours to choose: these four were
taken because they touch the paths an Electra title uses, not because anyone
proved which one does the work. `0008` remains the candidate.

## The regression pass

| title | path it exercises | result |
| --- | --- | --- |
| Beast of Reincarnation | Electra through the MFT -- `0006`, `0008` | **plays**, 240+ frames at 60 fps |
| Devil May Cry 5 | codec only, no DLL of ours in the process | **plays** |
| Persona 5 Strikers | source reader, libav, and our D3D9 bridge | **plays**, `has picture` to frame 180 |
| NINJA GAIDEN 4 | VP9 -- `0002`, `0003` | **video plays**, then crashes; see below |
| Nioh 2 | same bridge and codec as Persona | **plays**, `has picture` to frame 360 |
| Nioh | same again | not run -- it crashes on this engine before any video, older than this work |
| Mortal Shell 2 | **not** the MFT path, as expected -- it takes the VPx route our own DLL patches | **plays**; `VPx version checks: 4 found, 4 patched` |
| NieR Replicant | WMV through the source reader | **plays**, 300 samples, 200 frames blitted, luma 14..238 |
| DYNASTY WARRIORS: ORIGINS | matroska plus the D3D11-to-D3D12 bridge | **plays**, NV12 at 2560x1440, 300 samples |
| Wo Long: Fallen Dynasty | the D3D bridge -- its samples are asked for `IMFDXGIBuffer` | **plays**, luma 15..171 to frame 120 |
| Nioh 3 | the DYNASTY WARRIORS bridge, second title on that path | **plays**, luma 19..209, 300 samples |
| TMNT: Splintered Fate | started, and its own half reported itself inert: "the 16:9 mode guard stays out of TMNTSF.exe" -- worth a look, not counted either way |
| Both Life is Strange | node guard, not the media path | both **start** on the rebuilt engine, but neither has our fix installed, so this tests the engine and not our work. Their fix answers a freeze that appears after a while of play, which a short launch does not exercise either way |

**NINJA GAIDEN 4 crashes without any DLL of ours in the process.** Run with the
fix uninstalled -- the game back to its own `dstorage.dll`, nothing of ours
loaded, no log written -- and it still dies. So the crash is the title or the
engine, and our DLL is not in it. That is the cleanest thing learned about NG4
all night, and it took removing our own code to learn it.

**And it is not a regression.** It opens its `.msd` container, is offered
a VP9 decoder, binds a device and plays the video -- which validates `0002` and
`0003`, the two patches no other title here exercises -- and then the process
dies with no crash report and no further log. It behaved identically two hours
earlier, with winevideo's transplanted binary and before anything was compiled
here, and identically again with the stock one. Of the three runs recorded in its
log, this is the only one that reached the video at all.

So it is an open defect of that title on that engine, older than this work, and
it is the one place where these four patches made a title get *further* without
making it finish.

Three of those titles -- DYNASTY WARRIORS, NieR and Wo Long -- write into one
log, because their carriers are built from one source. Read the process tag, not
the filename: a count taken while the file was still growing credited all of
NieR's frames to DYNASTY WARRIORS.

A title that does regress names the patch to drop: the script takes patch
numbers, so isolating one is a rebuild and a launch.
