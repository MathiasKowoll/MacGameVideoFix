# Beast of Reincarnation

Unreal Engine 5. The startup video plays its sound and shows nothing.

| | |
| --- | --- |
| Video | H.264, measured by FourCC rather than assumed |
| Played by | Electra, through a Media Foundation decoder MFT |
| Symptom | Sound plays, picture never appears. No crash |
| Fix | NV12 put back on the menu, and Electra forced onto its software path |
| CrossOver | 26.3 and `crossover-preview-arm64-20260821`, played through on both |
| winevideo | Not required — every run was in a bottle it never touched |

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
macOS". This is that effect from inside the process: one game, reversible, and
nothing outside the game folder touched.

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
