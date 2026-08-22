# Ninja Gaiden 4 — where it actually stops

Not fixed. Written down because the reason is precise, and because an earlier
claim in this repository about it was wrong.

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

But the capability is there: CrossOver Preview decodes VP9 profile 0 and 2 on
its own through `vp9parse → vtdec_hw`. What is missing is the declaration, not
the decoder — nothing in the registry says an MFT exists.

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
