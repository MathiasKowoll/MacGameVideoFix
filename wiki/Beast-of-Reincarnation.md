# Beast of Reincarnation

Unreal Engine 5. The startup video plays its sound and shows nothing.

| | |
| --- | --- |
| Video | H.264, 1920x1080 at 60 fps, measured by FourCC and by frame timestamps |
| Played by | Electra, through a Media Foundation decoder MFT |
| Symptom | Sound plays, picture never appears. No crash |
| Fix | One console variable, found by its own name in the binary |
| CrossOver | 26.3 **with winevideo**. Plain 26.3 and Preview 27 stall after two frames |
| winevideo | **Required**, since the game update of 2026-08-24 |

## The game updated, and this became a winevideo title

On 2026-08-24 the game shipped a new build, and the fix below stopped working.
Everything on this page was still true of the old binary; none of it was enough
for the new one.

**Every address moved.** The two call sites this fix had written down landed
mid-instruction in the new executable, so the fix verified them, declined and
did nothing — which is the designed behaviour and is also useless. The sites
were found again by what they are rather than where they were, and the console
variable is now located from its own name: there is exactly one UTF-16
`Electra.Win.H264UseOldOutputPath` in the image and exactly one RIP-relative
reference to it in 128 MB of code, and the store that follows is the pointer.
That is the only thing this fix now remembers between builds, and it is
Electra's own API rather than a number read off a disassembly.

**And then it still did not play**, on any stock engine. What the log finally
said, once its counters were fixed:

```
ProcessInput      8      accepted 8
ProcessOutput    26      frames out 2, needs more in 23, stream change 1
END_OF_STREAM: drained the decoder and got 6 more frame(s)
```

Eight access units in — one GOP, 150 ms of a 60 fps video — two pictures out,
six still inside, and Electra ending the video 95 ms later. Neither side is
wrong: an H.264 decoder holds frames until it knows nothing earlier is coming,
and a player waits for pictures before sending more. `MF_LOW_LATENCY` is the
documented way to break that, and it returned S_OK and changed nothing.

The same DLL, the same game build, four engines:

| engine | fed | frames out | |
| --- | --- | --- | --- |
| CrossOver 26.3 with winevideo 0.5.0 | 480 | 475 | plays through |
| CrossOver 26.3 | 7 | 1 | no picture |
| CrossOver Preview 27 | 8 | 2 | no picture |
| The Procyon fork's engine | 8 | 2 | no picture |

The three that fail differ in the exact counts and not in the shape: a GOP goes
in, one or two pictures come out, the rest stay inside, and the video ends.

Which engine you have can be answered without launching anything, because the
difference is four strings in one binary:

```
diagnostics/check-engine-media.py

  engine                        queue bounds  demux bound   Electra
  CrossOver.app                 ABSENT        ABSENT        stalls after a GOP
  CrossOver Preview.app         ABSENT        ABSENT        stalls after a GOP
  Crossover_patched.app         ABSENT        ABSENT        stalls after a GOP
  CrossOver-winevideo-0.5.app   set           set           plays
```

That is also how to check a port rather than trust it: whoever carries 0018 and
0019 into another engine can ask the built binary whether they arrived.

**winevideo dissolves it.** In its bottle the same build plays the cutscene
whole: 475 frames, one every 16.7 ms of wall clock, ending on its own. Its
patches `0018-winegstreamer-remove-compressed-queue-time-bound` and
`0019-lift-decodebin-demux-time-bound` are the plausible reason — the bound is
on the transform's own queue, not on the source reader, which is why this looked
at first like a path the title does not use.

### The patches can be carried, when the Wine build matches

Measured the same night. An engine does not have to *be* winevideo to play this
title -- it has to carry winevideo's `winegstreamer`, and that pair of files
transplants cleanly **into an engine built from the same Wine**:

    winegstreamer.so   and   winegstreamer.dll

Both engines here report `wine-11.0-8726-g2e2f5fca349`, which is what CrossOver
26.3 carries. While the target was based on Preview 27
(`wine-11.15-8895-g32f409fef6a`) the transplant was not possible and was not
attempted: the unix-side `.so` binds to the internals of the Wine it was built
with. Moving the target to a 26.3 base is what made it possible, not any change
to the files.

Checked before copying, so this is not luck: the PE side imports only `ole32`,
`msdmo` and `ntdll` -- nothing of winevideo's own, so it does not drag in that
project's compat module -- and the unix side links only against GStreamer
libraries the target already had. The dependency graph closes inside the bundle.

Result, in the launcher's own bottle rather than winevideo's:

```
offers type 0: 'NV12'          <- the engine offers it now; the relabel is inert
ProcessInput   427   accepted 427
frames out     421
```

One frame every 16.7 ms of wall clock to the end. The three faults below are
answered by the engine, and what remains of this fix is one console variable.

Codecs travel in the same lot: `libgstlibav`, `libgstmatroska` and `libgstvpx`
with their libraries, into `lib64/gstreamer-1.0` and `lib64`. Note that VP9 also
needs winevideo's `vp9-mft.reg` in the bottle -- the transplanted binary names
VP9 but does not register the MFT by itself.

### What is left of the fix there, measured one switch at a time

| | on winevideo |
| --- | --- |
| Console variable set to 1 | **required** — without it, one frame and a null dereference |
| The two `IsSoftware` patches | inert: the engine no longer advertises `MF_SA_D3D_AWARE` |
| NV12 put back on the menu | inert: the engine offers NV12 itself |
| An `IMF2DBuffer2` over the caller's buffer | inert: the engine provides 2D-capable samples |

So on winevideo this title needs one console variable and nothing else, and the
three faults described below are all answered by the engine. They are kept in
the DLL because they are what carries the title on a stock CrossOver — where,
as of the new build, the video does not play at all.

Each of those rows is a run with that half switched off, not a reading of the
code. The DLL reads `C:\mgvf-electra.txt` from the bottle for the words
`nocvar`, `noissw` and `no2d`; with no such file every half is armed, which is
what ships. It exists because each of these answers cost a person walking to a
cutscene, and a rebuild between every one of them wastes that.

## Three faults in a row

Each one hid the next, and none was where four earlier guesses put it.

### 1. CrossOver hides NV12, and Electra accepts nothing else

`transform_GetOutputAvailableType` in CrossOver's `winegstreamer` skips NV12
whenever it detects macOS. The strings sit adjacent in the shipping binary of
both `crossover-preview-arm64-20260821` and stock 26.3 — which is a binary read
rather than a run, and is worth keeping separate from the run below:

```
transform_GetOutputAvailableType / Skipping NV12 output format / Darwin
```

`is_macos()` is the only guard. The decoder then offers YV12, YV12, IYUV, I420
and YUY2. Electra's H.264 decoder walks that list looking for NV12, does not
find it, and destroys the decoder — so no frame ever existed.

The censoring is only in the getter. `SetOutputType` validates against an array
that still contains NV12 and carries no macOS check, so handing NV12 back by
name is honoured and the negotiation completes.

The fix was played through on both of those builds. That is a separate
statement from the string comparison above, and it is the one that says the
title works on stable.

**One divergence, and it is unexplained.** Both Life is Strange titles crash on
26.3 while this one plays there, and the three install the same DLL. What they
do not share is which half of it runs: the policy table arms this NV12 restore
for `BeastOfReincarnation-Win64-Shipping.exe` alone, and arms only the node
guard for those two. So the restore is measured working on 26.3 here and is
inert there, which rules it out as the direct cause of their crash and leaves
the question open — see [Findings](Findings.md), under *The open defect on
26.3*, and [Life is Strange: Reunion](Life-is-Strange-Reunion.md). Nothing in
this page should be read as saying the NV12 restore is safe on stable
everywhere; it says it is measured on this title.

### 2. Electra asks itself, not the decoder, whether it is in software

With frames finally decoding, the picture was still black. Electra decides
whether it is decoding in software by asking its **own** platform handle, never
the MFT — so withholding the D3D manager from the decoder, which is the obvious
move and was tried first, could never have worked.

Because `winegstreamer` still advertises `MF_SA_D3D_AWARE`, Electra builds
itself a D3D11 device, answers "not software", and takes a branch that requires
`IMFDXGIBuffer` on the output buffer. No system-memory buffer can satisfy that,
so every frame was dropped in silence.

Two calls to `IsSoftware()` are made to return true.

### 3. The same gate decides the frame height

Patch one of those two call sites and not the other and the outer gate stays
false: `DecodedHeight` is passed as the frame height instead of one and a half
times it, and the renderer is handed a luma-only picture. Both go, or neither.

## What this is

winevideo reaches the same place by patching `winegstreamer` itself — its patch
0005, whose stated effect is "UE ElectraPlayer takes its software decode path on
macOS". This was that effect from inside the process: one game, reversible, and
nothing outside the game folder touched. On the build shipped on 2026-08-24 that
is no longer enough on its own; see the section above.

The console variable `Electra.Win.H264UseOldOutputPath` selects the same
fallback, and the DLL sets it itself. **No `Engine.ini` is needed** — measured,
by playing with the file deleted. That removes the most fragile part of the old
arrangement: a path depending on the Unreal project name, inside a bottle that
had to be guessed. Its polarity is worth recording anyway, because it was
assumed backwards for an afternoon: a non-zero value takes the fallback.

## Caveats

- **Do not use this on a game with anti-cheat or anti-tamper.** It patches a
  running process, and part of it writes to the executable's own code.
- The addresses it writes to come from disassembling this exact build. They are
  checked against the bytes that should be there first, so a game update makes
  the fix do nothing and say so rather than corrupt what it lands in.

## How it was found, since four attempts first failed

The first four attempts all argued from resemblance to
[Mortal Shell 2](Mortal-Shell-2.md) — same engine, same carrier DLL, Media
Foundation delay-loaded, a crash on the first video. All four changed nothing,
because all four acted downstream of a decoder that never started.

What broke the deadlock was instrumenting the game and, more importantly,
making the instrument prove it was attached. An early probe reported that the
game created a decoder and never used it, which is a striking finding and was
false: the hooks were not in the chain. A canary on a method every caller
invokes turned the absence of evidence back into evidence, and from there each
fault named itself.

The lesson that cost the most time: three separate conclusions came from
reading a binary, or from a log's silence, and being reported as measurements.

---

Back to [the games table](Games.md) · [Diagnosing a new game](Diagnosing-a-new-game.md) · [Findings](Findings.md), what what they have in common
