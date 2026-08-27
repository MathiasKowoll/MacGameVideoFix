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
