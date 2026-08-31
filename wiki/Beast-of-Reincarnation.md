# Beast of Reincarnation

Unreal Engine 5. The startup video plays its sound and shows nothing.

| | |
| --- | --- |
| Video | H.264, 1920x1080 at 60 fps, measured by FourCC and by frame timestamps |
| Played by | Electra, through a Media Foundation decoder MFT |
| Symptom | Sound plays, picture never appears. No crash on a stock engine |
| Fix | NV12 put back on the menu, two `IsSoftware` call sites patched by address, one console variable found by its own name |
| CrossOver | Stable 26.3, on an engine carrying **winevideo**'s `winegstreamer`. A plain 26.3 stalls after two frames |
| winevideo | **Required**, since the game update of 2026-08-24 |

## The game updated again, and everything moved by 0x4070

On 2026-08-31 the game shipped another build — `++aibou+mainline-CL-407366`,
engine 5.4.4-407366, a 176.6 MB executable dated that day — and the fix stopped
working for the second time. The first move was a week earlier and is the
section below.

**The guard refused, and that is the design working.** It read what was at the
two addresses it had written down, did not recognise it, patched nothing and
said so. Nothing was written into a build it did not recognise, and the title
simply stopped working, cleanly, until somebody looked. It also stops there and
goes no further: the console variable is only reached once those two sites
verify, so a run that declines applies no half of the fix at all.

**Every address moved by exactly 0x4070** — both call sites and the function
they call, all three by the same amount:

| | the 2026-08-24 build | the 2026-08-31 build |
| --- | --- | --- |
| Site A | `0x0636CB3E` | `0x06370BAE` |
| Site B | `0x06370B44` | `0x06374BB4` |
| The function both call | `0x0636B8B0` | `0x0636F920` |

The distance between the two sites is `0x4006` in both builds. That is worth
recording and is not a signature to search by: last time, a search by the
distance between the old pair found a different pair entirely. A distance is not
a name.

**The anchor came from the game's own crash report**, not from a search.
`Saved/Crashes` listed a `PCallStack`, and three of its addresses said where to
look:

    636f6c9   where it died
    636fa19   inside the function the two sites call
    6370bb3   the return address of site A's call — so site A is 6370bae

A `call rel32` is five bytes, so the return address is the site plus five. From
there the pair is confirmed rather than guessed: in the whole 128 MB of `.text`
exactly two direct calls reach `0636F920`, and both are followed by
`testb %al,%al`. Both have the identical shape:

    lea r8,[rsp+X] ; mov rcx,[reg+0x98] ; lea rdx,[reg+0xc8] ; call ; testb al,al ; je

The console variable moved with everything else — its pointer is at `0x0AA78428`
in this build and was at `0x0AA29110` in the last — but that one is found from
the variable's own name, and the written-down address is only a fallback.

**What the crash actually is**, since it says what the patch is for. At
`636f6c9` the game does `mov rax,[rbx]` with `rbx` zero. `rbx` came from the
stack slot handed to the preceding virtual call — vtable slot 0,
`QueryInterface` — asking a D3D object for
`{A06EB39A-50DA-425B-8C31-4EECD6C270F3}`, which is
`IID_ID3DDestructionNotifier`. The slot is zeroed before the call and the result
is never checked, so an interface that is not implemented is a null dereference
one instruction later: `EXCEPTION_ACCESS_VIOLATION` reading address 0x0. That is
the D3D path, and this patch exists so that the path is never taken.

**It failed identically on an unmodified environment**, which is what says the
game update caused this rather than anything of ours.

**Expect a third, and expect the same method to find it.** The fix prints the
method itself when it declines: crash it, read `Saved/Crashes` for the call
stack, disassemble at the anchor for a bool-returning call followed by
`testb %al,%al`, then find every direct call to that same function. Two moves
inside a week; the addresses on this page are for the build named above and for
nothing else.

## The game updated, and this became a winevideo title

On 2026-08-24 the game shipped a new build, and the fix below stopped working.
Everything on this page was still true of the old binary; none of it was enough
for the new one.

**Every address moved**, by 0x1E930. The two call sites this fix had written
down landed mid-instruction in the new executable, so the fix verified them,
declined and did nothing — which is the designed behaviour and is also useless.
The console variable is now located from its own name: there is exactly one
UTF-16 `Electra.Win.H264UseOldOutputPath` in the image and exactly one
RIP-relative reference to it in 128 MB of code, and the store that follows is
the pointer.
That is the only thing found by name, and it is Electra's own API rather than a
number read off a disassembly. The two call sites are still addresses written
down per build; what verifies them is not a byte pattern but where they call —
both must reach the same function, with `testb %al,%al` behind the call. That is
why a build they do not fit is refused rather than guessed at.

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

**CrossOver Preview is no longer a supported engine here.** The supported engine
is stable 26.3. Preview appears on this page -- in the table above, and in what
was read out of both builds' binaries below -- as a record of what was measured
on it at the time, not as a build to run this title on.

Which engine you have could not, in the end, be answered without launching one.
A check that read four strings out of `winegstreamer` separated winevideo's build
from stock across five engines and was written up as the way to verify a port.
Then this project built its own binary carrying none of those four strings, and
the title plays with it. Four points of correlation, no mechanism. The check has
been withdrawn; the games are the test.

**winevideo dissolves it.** In its bottle the same build plays the cutscene
whole: 475 frames, one every 16.7 ms of wall clock, ending on its own. Which of
its patches is the reason was got wrong twice: `0018` and `0019` were named, and
reading them showed `0019` reverting `0018` and both touching the source reader,
which this title does not use. The candidate now is `0008`, "always provide
2D-capable output samples" — the log below records `dwFlags=0x7` without
`PROVIDES_SAMPLES`, so on stock the caller allocates every frame, and that is
the stand-off. Candidate, not finding.

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
answered by the engine, and what remained of this fix that night was one console
variable.

Codecs travel in the same lot: `libgstlibav`, `libgstmatroska` and `libgstvpx`
with their libraries, into `lib64/gstreamer-1.0` and `lib64`. Note that VP9 also
needs winevideo's `vp9-mft.reg` in the bottle -- the transplanted binary names
VP9 but does not register the MFT by itself.

### What is left of the fix there, measured one switch at a time

| | on an engine carrying winevideo's `winegstreamer`, that night |
| --- | --- |
| Console variable set to 1 | **required** — without it, one frame and a null dereference |
| The two `IsSoftware` patches | inert: the engine no longer advertises `MF_SA_D3D_AWARE` — **contradicted since; see below** |
| NV12 put back on the menu | inert: the engine offers NV12 itself |
| An `IMF2DBuffer2` over the caller's buffer | inert: the engine provides 2D-capable samples |

So on an engine carrying winevideo's `winegstreamer`, that night, this title
needed one console variable and nothing else, and the three faults described
below were all answered by the engine. They are kept in the DLL because they are
what carries the title on a stock CrossOver — where, as of the new build, the
video does not play at all.

That was one measurement on one engine on one night. On 2026-08-31 the fix
declined entirely and the title stopped working, which is not a re-measurement
of the rows above: when the call sites do not verify, the console variable is
never reached either, so that run does not say which half the title was missing.

**And the `IsSoftware` row is contradicted.** The row says the two patches were
inert. On 2026-08-31 the addresses moved, the guard refused to patch, and the
title stopped working until the two sites were followed into the new build —
which is the behaviour of a patch the title depends on, not of an inert one.
That is a different run from the decline described above: it is what happened
once the addresses were followed, not what the decline itself showed. The two
observations disagree, and the one from 2026-08-31 is the more recent. Both are
kept as they were made. No reason is offered for the disagreement, because none
has been measured.

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
title works on stable; the Preview half of it is a record of a measurement and
not a route.

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
is no longer enough on its own, and the addresses have moved again since; see
the two sections at the top of this page.

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
  checked first — each site must be a direct call reaching the same function,
  with `testb %al,%al` behind it — so a game update makes the fix do nothing,
  say so, and say how to find them again, rather than corrupt what it lands in.

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
