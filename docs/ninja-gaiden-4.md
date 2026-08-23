# Ninja Gaiden 4 — where it actually stops

Not fixed. Blocked by DirectStorage rather than by video, which is not where
anyone expected. Written down because the reason is precise, and because more
than one earlier claim in this repository about it was wrong -- including one
made and withdrawn on the same day.

## The conclusion, 22 August 2026

Two gates were known. A third was found between them, and the first turned out
to be crossable.

**Gate 1 is crossable, and this was measured.** The game asks
`MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, {Video, VP90})` and gets nothing.
Answering by repeating the query for H.264 -- which is registered, so what comes
back is a real list of real objects with real lifetimes -- is enough: the game
counts them and carries on. It never activates them. That question had been open
and is now closed.

**DirectStorage is the wall.** Past gate 1 the title reaches
`IDStorageFactory::CreateQueue`, which returns `DXGI_ERROR_UNSUPPORTED` and a
null queue that the game stores without checking and calls through. That is with
every capability question granted, with GPU decompression forced off through
`DSTORAGE_CONFIGURATION1`, and with the staging buffer at the size the game
asked for. Removing `dstoragecore.dll` avoids the crash and replaces it with a
stall: 76 threads, none inside DirectStorage, Media Foundation, D3D12 or DXGI,
the main one waiting on a C++ condition variable.

**And the video was never reached.** `MFCreateSourceReaderFromURL` did not
appear in any of eight runs. Whatever this title's video needs, it is not what
stops it first.

**What is not the reason, and was written here as one.** This page said for a
while that the title was out of reach because a decoder MFT has to live inside
the process that owns Media Foundation and this project cannot put one there.
The first half is true; the second is not. The MFT can be ours.

`MFTEnumEx` is already answered from a proxy, and `IMFActivate::ActivateObject`
is already hooked -- it observes today, but the same hook can return an
`IMFTransform` of our own instead of whatever the enumeration pointed at. What
that transform then needs is a VP9 decoder, and there is precedent in this
repository for exactly that: Mortal Shell 2's Electra decodes VP9 in-process
with its own libvpx, which is portable C. Registration is not involved --
nothing has to be written to the registry, because the two calls that decide
which transform gets used are both intercepted.

That is real work -- an `IMFTransform` is about twenty methods, most of them
trivial, and a decoder has to be built into the carrier -- but it is the same
kind of work as everything else here, not a different kind.

**What actually stops this title today** is the paragraph above about
DirectStorage, which comes first. A working VP9 transform would change nothing
for Ninja Gaiden 4 until `CreateQueue` succeeds. It would matter for any title
whose only missing piece is the decoder.

The owner of this machine reports that the only configuration in which they have
played the title is with winevideo installed.

## The wrong claim, corrected

It was called out of reach because "we patch a running process and its .text is
encrypted". That conflated two different things:

- **Patching the game's own code.** Beast of Reincarnation needed this — three
  bytes at fixed offsets inside the executable. Anti-tamper defeats it.
- **Everything else.** A proxy DLL beside the game is what every game does with
  every DLL it ships. Hooking a COM vtable writes into `mfplat.dll` or
  `d3d11.dll`, which are not protected. DYNASTY WARRIORS and Persona 5 Strikers
  were fixed almost entirely that way.

Ninja Gaiden 4 needs nothing inside the game image. Its frame path is the
simplest of any title here:

    GetBufferCount → GetBufferByIndex(0) → IMFMediaBuffer::Lock → memcpy → Unlock

It locks a system-memory buffer and copies it. No `IMFDXGIBuffer`, no shared
handle, no `ID3D11VideoDevice` — none of the five obstacles that made the
DYNASTY WARRIORS bridge necessary.

## Where it stops

Two gates, in order. Measured on this machine with the game running.

**1. `MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, 0x3F, {Video, VP90})`.** The game
asks whether a VP9 decoder exists before doing anything else. In a stock
CrossOver bottle:

    entries under MFT_CATEGORY_VIDEO_DECODER:  0
    mentions of VP9 anywhere in the registry:  0

So the answer is no, and the game never gets further. Watching the running
process confirms it: `mfplat.dll` and `mfreadwrite.dll` load, and **not one
GStreamer plugin ever does**. No pipeline is built because none is asked for.

**2. `MFCreateSourceReaderFromURL`.** If that fails the game calls `exit(-1)` —
which is why it leaves a black screen and no crash report. It does not hang, it
gives up.

`FromURL` resolves by file extension through the registry, so it needs a
`.webm` byte-stream handler. That is why DYNASTY WARRIORS works in a bottle
where this does not: it uses `FromByteStream`, which resolves by content.
Registering the handler (`diagnostics/registry/apply-webm-handler.sh`) is
necessary and, on its own, changes nothing — gate 1 comes first.

## What the videos are

400 files in `Assets/Movies/`, all of them checked rather than sampled:

- **399 WebM with VP9**, five of those carrying Opus; the rest are video-only,
  the audio coming from Wwise.
- **One is not.** An 86 MB `.msd` holding **H.264 and AAC in MP4**. Since a
  source-reader failure is fatal here, that single file can turn a working fix
  into a hard exit, and it has to be handled before this could be called done.

An earlier sampled scan found `USM` in the asset archives and raised CRI Sofdec
as a possibility. It is not Sofdec.

## What would be needed

CrossOver ships no VP9 decoder MFT. winevideo adds one, inside winegstreamer
(its patch 0003), which is exactly the kind of change this project cannot make.

But the capability is there: `crossover-preview-arm64-20260821` decodes VP9
profile 0 and 2 on its own through `vp9parse → vtdec_hw`. What is missing is
the declaration, not the decoder — nothing in the registry says an MFT exists.

So the open question is whether gate 1 is a capability check the game makes
before using a path that does not involve that MFT at all. If it is, answering
`MFTEnumEx` from a proxy would be enough and the source reader would do the
rest. That is untested.

## What was tried, and what it changed

**The MFT gate was answered.** When the game asks for a VP9 decoder and gets
none, the query is repeated for H.264 -- a format that really is registered --
and the resulting list handed back. The game only counts these; the
disassembly shows `count > 0` leading to a single `mov byte [...], 1`, and it
never activates them. Measured:

    VP90 had no decoder; asked again for H264 and got 1
    MFTEnumEx flags=0x3f -> 0x00000000, 1 decoder(s) offered

That gate is genuinely passed now, where it returned zero before.

**And the screen is still black.** `MFCreateSourceReaderFromURL` is still never
called, so something between the gate and opening a file stops it — or the
black screen was never about video at all.

The unexamined lead is D3D12. CrossOver's console prints a line beginning
`D3DMetal ID3DDestructionNot…` — D3DMetal reporting a query for
`ID3DDestructionNotifier`, which it does not implement, using the GUID name
table in GPTK's `d3d12.dll`. The game ships its own `D3D12Core.dll` (the
Agility SDK), and that binary does contain the interface's GUID and name. That
is the same interface behind Mortal Shell 2's crash, and winevideo implements
it in vkd3d in its patch 0010.

Whether it is fatal here is unknown. Settling it needs the full console line,
which only exists if the bottle is launched from a terminal
(`diagnostics/launch-and-capture.sh`) -- started from Steam or the CrossOver
interface, that output is discarded.

**A method note worth keeping.** The game was twice reported here as having
"terminated" when it had been closed by hand. A process disappearing is not the
same as a program giving up, and treating one as the other produced a confident
account of two independent faults that the evidence did not support.

## What was ruled out, in order

Each of these was a reasonable hypothesis and each is now measured rather than
argued. None of them is the black screen.

| Hypothesis | Result |
| --- | --- |
| Videos are CRI Sofdec, not Media Foundation | **no** — 399 of 400 are WebM/VP9 |
| The graphics backend was wrong (DXMT) | **no** — the same on D3DMetal |
| No VP9 decoder MFT, so the gate fails | **real, and answered** — see below |
| No `.webm` byte-stream handler | **real, and registered** — never reached |
| The DXGI device manager binds no device | **no** — `ResetDevice` returns S_OK |
| `ID3DDestructionNotifier` is refused | **not observed** — nothing asks for it |
| D3D12 device creation fails | **no** — succeeds at feature level 11_0 |

So Media Foundation initialises completely, D3D12 comes up, committed
resources are created, and the screen is black. Whatever stops it is past all
of that, and `MFCreateSourceReaderFromURL` is still never called even though it
is in the binary.

**The MFT gate is genuinely fixed.** When the game asks for a VP9 decoder and
gets none, the query is repeated for H.264 -- registered, real -- and that list
handed back. The game only counts them; the disassembly shows `count > 0`
setting a single bit, and it never activates them. It went from zero to one and
stayed there. Necessary, evidently not sufficient.

**Two traps cost runs here, both of them already documented in this
repository.** The game imports `D3D12CreateDevice` from `d3d12.dll` **by
ordinal 101**, with no name, and `hook_import` walks names only -- it skips
ordinal entries with an explicit `continue`. The hook installed, reported
itself installed, and was never called, so the log showed no D3D12 device
because nothing watched the door it came through. DYNASTY WARRIORS did exactly
this and `hook_import_ordinal` was written for it months ago. This probe was
derived from the Media Foundation probe, which never needed ordinals.

And a terminal capture produced two lines of `msync` and nothing else for three
attempts, because launching Steam captures nothing: Steam forks and returns, so
the command finishes before the game starts and the game's output belongs to
another process.

## Risk, since the question comes up

Assessed rather than assumed. The protection is a whole-section code-encryption
packer that protects its own image and nothing else: four imports, no TEB or
PEB reads anywhere in the 2 MB stub, so it cannot enumerate modules or resolve
APIs by hash, and its one persistent hook is removed after unpacking. It
matches no marker for Denuvo, VMProtect, Themida, Arxan or SteamStub.

The game does enumerate modules once, at Steam init — but it is looking for a
Steam **emulator**, comparing against `SteamAPI_Init` and friends, and its
worst outcome is a localised dialog. Nothing exits, nothing is persisted.

There is no DRM lockout, no ban risk (single-player, no anti-cheat, no `.sys`
in the install), and no way to corrupt the install. The one real cost of a
proxy would be that `dstorage.dll` is Steam-managed, so *verify integrity of
game files* would silently undo it.

**One rule if a bridge is ever built here:** do not name it anything
Steam-related and do not re-export any Steamworks entry point. That is the only
way it could plausibly trip the check above.

## Measured again, 22 August 2026

An evening of instrumentation, most of it spent on a problem that was not there.
Written down in full because the wrong turn is as instructive as the finding.

### The blocker, now confirmed in the binary

Gate 1 was described above from the registry's side: no VP9 decoder is
registered. The stronger version is that there is nothing to register.
`MFVideoFormat_VP90` — the GUID `30395056-0000-0010-8000-00AA00389B71` — does
not appear anywhere inside `crossover-preview-arm64-20260821`'s
`winegstreamer.so`. The `VP90` that turns up in its strings is one entry in a
table of fourcc names, beside `qVP10`, `qY210` and the rest.

So this is not a missing registration. The decoder does not exist in the
build, and no amount of registry work conjures one. Host GStreamer decodes VP9
perfectly well — seven elements offer it — but nothing bridges that to Media
Foundation. That bridge is precisely what winevideo adds, and it is why this
title works there and not here.

Confirmed live: the game asks `MFTEnumEx` for VP90, is told zero, asks again for
H264 and is told one, and then exits. Every other title in this repository is
served by a decoder Preview already has.

### The DirectStorage detour, and a rename that was right

The game folder contained `DirectStorage/dstoragecoreeeee.dll` — four letters
added to a filename, which is how a person disables a DLL. It was read here as
damage and restored to `dstoragecore.dll`. That was wrong; it was the working
configuration, and restoring it introduced a hard crash that had never been part
of this title's problem.

With DirectStorage live, the game builds its `"Graphics DSQueue"` and dies:

    141021b9f:  call [rax+0x18]      ; IDStorageFactory::CreateQueue
    141021ba7:  test eax,eax
    141021ba9:  cmovs rcx,rbx        ; a failed HRESULT becomes NULL
    ...
    14102f729:  mov [rbx],rax        ; stored without a check
    14102f72f:  mov rdx,[rax]        ; faults, rax = 0
    14102f732:  call [rdx+0x48]      ; queue->GetErrorEvent()

`CreateQueue` returns `DXGI_ERROR_UNSUPPORTED` for the one queue that carries an
`ID3D12Device`; the sibling `"Asset DSQueue"` passes `Device = NULL` through the
same code and is fine. The game never checks, so the refusal lands as a null
dereference on the main thread during renderer bring-up.

Why it refuses is not reachable from a proxy DLL. Everything DirectStorage asks
the device is granted — `ID3D12Device5`, Shader Model 6.5, `WaveOps`, `Int64`,
`Native16Bit`, `ExpandedComputeResourceStates`, a COPY command queue, a 32 MB
buffer — and it builds no pipeline and makes no further call before giving up.
Ruled out by measurement, each costing a launch: GPU decompression, the
compatibility mapping layer, the 256 MB staging size, `D3D12_OPTIONS17` (answered
yes to it and nothing changed), and our own instrumentation. The likeliest
remaining explanation is off the device entirely: this game ships an Agility SDK
in `D3D12/D3D12Core.dll` and Wine never loads it, so DirectStorage is talking to
a device that is not backed by the runtime it expects.

**So `dstoragecore.dll` stays disabled.** Refusing the factory from a proxy has
the same effect and is reversible — the game's backend dispatcher chooses
DirectStorage on nothing more than a null test of the factory pointer — but the
rename is what was already there and it needs no DLL to keep working.

### What would actually fix this title

A VP9 decoder reachable from Media Foundation. Three ways, none small:

1. **winevideo**, which is where this works today.
2. **Ship a VP9 decoder MFT of our own**, registered under
   `MFT_CATEGORY_VIDEO_DECODER` and backed by something that can decode — host
   GStreamer, libvpx, or VideoToolbox. Our proxy already hooks `MFTEnumEx`, so
   handing back an activate object for VP90 is the small end of this; a correct
   `IMFTransform` is not.
3. **Re-encode the 400 files.** Deliberately removed from this project, and it
   would still have to handle the one `.msd` that is H.264/AAC in MP4.

Until one of those exists, the honest status is unchanged: not fixed, and the
reason is a codec, not anti-tamper.

### The DirectStorage refusal, named

Disassembling both binaries afterwards answered the question the live probes
could not, and the answer is in D3DMetal rather than in DirectStorage.

`D3D12Device::EnumerateMetaCommands` decides its return value from a per-title
override table — 49 records matched against the executable's basename. One byte
of the matched record selects the answer:

    cmpb  $0x0, 0x24(%rax)        ; the matched title's record
    movl  $0x887a0004, %eax       ; DXGI_ERROR_UNSUPPORTED
    cmovnel %ecx, %eax            ; non-zero -> S_OK

Exactly one of the 49 has that byte set: `Wreckfest2.exe`. Anything unlisted
gets `_UnknownApp`, whose byte is zero, and therefore the error — deterministically,
every launch.

DirectStorage calls it unguarded, immediately after the `D3D12_OPTIONS4` query
that ends our trace, and rethrows the HRESULT verbatim out of `CreateQueue`.
The asymmetry is the whole defect: *no metacommand available* is handled
gracefully — the code falls through to an HLSL compute path that this device's
capabilities do support — but a *failing HRESULT* is not caught. Returning
`S_OK` with a count of zero would have been the answer that let this work.

So the refusal is not a capability this Mac lacks. It is an unimplemented call
answering with an error where an empty result was expected, and it is worth
reporting upstream on those terms.
