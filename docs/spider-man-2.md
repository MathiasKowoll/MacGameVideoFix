# Marvel's Spider-Man 2 — what was eliminated, and what is left

Internal. Nothing here is fixed and nothing here is published: it is kept so
the next attempt does not repeat this one.

## The symptom, as reported and as seen

Characters render **deformed** — limbs stretched and bent wrongly, textures and
materials correct, the world correct. Sometimes they enter a true **bind pose**
instead. Community reports add that suit animations break constantly, facial
and cloth animation are unreliable, cycling suits in the menu sometimes clears
it, and opening the map three to five times can bring it back. The word used
is *random*, and that word is the most useful piece of evidence in the whole
investigation.

One further report, not verified here: the same game is said to run correctly
on a larger/newer Apple Silicon part. Treated below as a claim, not a
measurement, but it points the same way as everything else.

## What was measured

All of it with `diagnostics/gfx-observe.c` riding on `amd_ags_x64.dll`, which
the game imports statically, plus `diagnostics/cpu-selftest.c` run in the
bottle.

| Hypothesis | Verdict | Evidence |
| --- | --- | --- |
| A capability is missing and the game does not check | **No** | 39,000+ creation calls across resources, pipelines, heaps, root signatures and queues. **Zero refused.** Not one. |
| The wave width is misreported | **No** | `D3D12_OPTIONS1`: WaveOps 1, `WaveLaneCountMin` 32, `WaveLaneCountMax` 32 — correct for this GPU |
| The game uses AMD shader intrinsics that go nowhere | **No** | Its AGS imports are `PushMarker`, `PopMarker`, `SetMarker`, `CreateDevice`, `DestroyDevice` and two settings calls. Debug markers only. |
| Havok is missing a library | **No** | Havok is statically linked; `hkCompatFormats.dll` ships with no copy of the game and fails to load on Windows too |
| A CPU instruction is advertised but implemented wrongly | **No** | Rosetta advertises AVX, AVX2, FMA, F16C, BMI1, BMI2. Seven arithmetic checks, all correct, including an F16C round trip and a 4×4 transform against its scalar equivalent |
| DirectStorage corrupts streamed assets | **No** | `dstoragecore.dll` moved aside, game relaunched, deformation unchanged |
| The game aliases memory and the barriers are not honoured | **No** | 2,627,042 transition and 447,984 UAV barriers counted, **0 aliasing** — and the counter proves the hook was live, which is the distinction an earlier bad measurement could not make |

Capabilities reported, for the record: shader model 6.6, resource binding tier
3, resource heap tier 2, raytracing tier 1.1, 16-bit minimum precision, 64-bit
integer shader ops. The stack tells this game it can do everything it asks
about, and then does not refuse it anything.

## What is left

**Synchronisation that is issued correctly and not honoured.** It is the only
surviving explanation that fits every observation, and it fits all of them:

- *Randomness.* A mistranslated shader computes the same wrong number every
  frame on every machine. A race does not.
- *Changing with the map and the suit menu.* Those change allocation and
  scheduling patterns, which is what a race is sensitive to. They do not change
  arithmetic.
- *Machine dependence.* Same reason.
- *Nothing refused.* A barrier is never refused. It is honoured or it is not,
  and from outside the process the two are identical.
- *The volume.* 448,000 UAV barriers in ninety seconds. A UAV barrier exists
  for exactly one purpose: to make a previous dispatch's writes visible to the
  next one's reads. Bone matrices are written by one dispatch and read by the
  skinning pass. If a fraction of those guarantees does not hold, the skinning
  reads matrices that are half-written or stale, which draws a character whose
  limbs are in the wrong places.

### A mechanism, offered as a hypothesis and not measured

D3D12 barriers have no direct Metal equivalent. Metal tracks hazards per
resource automatically — except on heaps, which are commonly created untracked
for performance, leaving the application to place fences itself. This game
creates **32,113 placed resources across 3,748 heaps**, which is where that
distinction lives. A translation that relies on automatic tracking where there
is none, or that places fences at a coarser granularity than the barriers it
was given, produces reads that overtake writes — visible only when the timing
allows, which is to say sometimes, on some machines, and differently after
anything that perturbs allocation.

This is consistent with the numbers above. It has not been demonstrated, and
demonstrating it would need to be done from inside the translation layer.

## Why this project cannot fix it

Every fix here works the same way: the game asks a question, the answer it gets
is wrong or unusable, and a proxy DLL answers differently. That shape requires
a decision to intercept.

There is no decision here. The game issues correct barriers, in the right
places, and is refused nothing. A proxy DLL cannot insert a guarantee that the
layer beneath it does not provide.

## What would move it forward

- Confirming the machine dependence properly: the same build, the same
  CrossOver, two different Apple Silicon parts, same scene. If it is a race,
  that is where it shows.
- Someone inside D3DMetal comparing what a UAV barrier on a placed resource
  becomes in Metal against what the D3D12 specification requires of it.

## Tooling this produced

`diagnostics/gfx-observe.c` — general, and the first thing to put on any game
that misbehaves regardless of symptom. Logs refusals rather than calls,
capability answers rather than questions, and totals on a timer so a game that
is killed rather than closed still reports.

`diagnostics/cpu-selftest.c` — runs in a bottle with no game, asks CPUID what
is on offer and then checks the offer against arithmetic worked out on paper.

## Two measurements of ours that were wrong before they were right

Recorded because both were confidently reported before being caught, and both
had the same shape: checking an effect rather than asking the source.

- **Overlap detection by bookkeeping.** Sizes were taken only for buffers and
  left at zero for textures, so every texture placed at offset zero "overlapped"
  every other; and freed heaps were never removed from the table, so an
  allocator reusing an address looked like aliasing. Twelve confident ALIAS
  lines, all of them noise. Replaced by counting the barriers the game actually
  issues, which needs no size at all.
- **Totals only at process exit.** A game closed from Steam never runs
  `DLL_PROCESS_DETACH`, and the totals are the half of the log that says what
  did *not* happen. The first run lost exactly the number the run was for.
