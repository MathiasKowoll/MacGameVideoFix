# Mirror's Edge Catalyst — measured, and not ours to fix

Internal notes. This game gets no fix in the app, because what stops it is not a
defect we can reach from a bottle: it is how Rosetta 2 and Wine's ntdll handle
self-modifying code, and neither is reconfigurable from here. Written down so the
next person does not spend the afternoon we spent, and because the trace is worth
reporting to CodeWeavers as-is.

**Symptom.** Launches from Steam through EA. The launcher shows "Launching
game…" and nothing else. No game window, no error. The user owns the game and
the licence is valid.

## What actually happens

The game relaunches itself in a loop and gives up. Watched from outside:

    14:41:41  MirrorsEdgeCatalyst.exe            21 s, one core at 99%, dies
    14:42:05  MirrorsEdgeCatalyst.exe /dbrv=1    21 s, 99%, dies
    14:42:27  MirrorsEdgeCatalyst.exe /dbrv=2    21 s, 99%, dies
    14:42:48  MirrorsEdgeCatalyst.exe /dbrv=3    21 s, 99%, dies
    14:43:06  MirrorsEdgeCatalyst.exe /dbrv=4    21 s, 99%, dies
    14:43:28  MirrorsEdgeCatalyst.exe /dbrv=5    21 s, 99%, dies, gives up

Each instance burns one core for ~21 seconds with **no disk I/O and no TCP
connection** (only a local UDP socket), then exits with code −6 (SIGABRT). EA's
own log records `Game session was too short: [25] seconds`. It never creates a
D3DMetal device — no shader cache is ever written for `MirrorsEdgeCatalyst.exe`,
so it dies before it draws anything.

## Why: self-modifying code under Rosetta

`MirrorsEdgeCatalyst.exe` does not contain the game. It is a Denuvo-style
anti-tamper wrapper: 16 sections with altered, space-padded names; `.srdata`
declares 25 MB with **zero bytes on disk**; the payload in `.sbss` is 76 MB at
8.0 bits/byte of entropy (perfectly encrypted); `Basereloc` size is 0; the only
import is `Core/Activation64.dll` ("EA DRM Helper" 4.11.01.297) by ordinal 100.
The real code is decrypted into memory at startup and executed there.

That is the pattern that fails. The wrapper writes x86 into a page, executes it,
then rewrites the same page with the next stage — classic self-modifying code
(SMC) on W+X pages. On x86 (Linux/Proton) this only needs
`VirtualProtect PAGE_EXECUTE_READWRITE` to be honoured, and it is: no
translation, the modified code runs directly. **That is why it boots on Linux.**

On Apple Silicon the same operation crosses two layers that both enforce macOS's
W^X policy (a page cannot be writable and executable at once):

1. **Rosetta 2** does not run the x86, it *translates* it, and keeps two caches:
   AOT (disk-mapped, signed — `TranslationCacheAot.cpp`) and JIT (runtime-generated
   — `TranslationCacheJit.cpp`). When translated x86 is rewritten, Rosetta must
   trap a jit write fault, `mprotect` the page, mark the fragment
   `CodeFragmentKind::InvalidJit`, and re-translate. Its literal failure modes:
   `mprotect failed on jit write fault` and
   `did not find a matching code fragment for an invalidated jit trap`.

2. **Wine's ntdll** (aarch64-unix side) *refuses by design* to keep a page W+X —
   `Disallowing WX permissions (%x->%x)` — and falls to a fault path,
   `HACK: exec fault on executable page`.

When the two disagree about whether a page may be written and executed, the
unpacker's thread spins in a fault → mprotect → re-translate loop that burns a
core and aborts. The 21 seconds and the SIGABRT are that loop.

The causal link between the loop and the Rosetta strings is inferred, not
traced — no Rosetta log was captured from the failing run — but the memory
pattern is static fact and the two enforcement layers are confirmed in the
binaries.

## What was ruled out, by measurement

Each cost a run; none is the cause. Listed so they are not tried again.

- **The activation / DRM licence.** It works. The Origin activation log records
  `License signature is good.` on every attempt — 44 times across the day.
  Exit code 170, which looked like a DRM refusal, was an artefact of launching
  the exe by hand from a terminal: EA's activation cuts off any start not made
  through the client. It has nothing to do with the failure.
- **The graphics backend / D3DMetal.** The game dies before creating a device;
  no shader cache is ever written. D3DMetal works in this same bottle for other
  titles the same day.
- **NVAPI / the NVIDIA path** (`NvCameraSDK64.dll` = Ansel). Disabled per-exe via
  `DllOverrides`; no change. Left disabled — it does no harm.
- **EA's dead servers.** `winter15.gosredirector.ea.com` was pointed at
  `127.0.0.1`; no change. The 21 seconds are not a network timeout — there are
  zero TCP connections during them.
- **EA and Steam overlays** (IGO hooks `activation64.dll` — a plausible
  self-verification trip). Both disabled; identical loop, no new IGO logs.
- **`WINE_SIMULATE_WRITECOPY=1`** — the closest existing knob, applied by
  CrossOver to Battle.net and others for page-protection clashes. No change.
- **`ROSETTA_DISABLE_AOT=1`** — forces pure-JIT translation, on the theory that
  the loop is born of the AOT/JIT cache disagreement. No change.
- **The bottle's Windows version.** Reports Windows 10 (build 19043) correctly;
  the `Qt: Untested Windows version 6.2` line is normal for a manifest-less
  process and is not the cause.
- **CPU / AVX detection.** No CPUID/AVX literals; such a check would fail in
  microseconds, not 21 seconds.

## What is still open, and where the fix lives

The fix is real but not in this project's reach. It lives in **Rosetta 2**
(Apple) — how it re-translates self-modified pages under W^X — and in **Wine's
ntdll under Rosetta** (CodeWeavers) — the interaction of its W^X `HACK` with
Rosetta's jit-write-fault path. CrossOver's compat database (189 rules, decrypted
and read in full) has no rule for this title and no *kind* of rule for
self-modifying code at all: its hack types are `steam_game_id`, `path_suffix`,
`graphics_backend`, `append_cmd_line`, `dll_overrides`, `env_vars`,
`replace_exe_path`. There is no vocabulary here for the thing that fails.

The reportable trace, for CodeWeavers: a Denuvo/EA-DRM wrapper (self-modifying
code, W+X pages, `Core/Activation64.dll` ordinal 100) launched under Rosetta 2
spins in Rosetta's jit-write-fault re-translation against ntdll's W^X refusal
(`Disallowing WX permissions`, `HACK: exec fault on executable page`); one core
at 99% for ~21 s, SIGABRT, relaunch `/dbrv=1..5`, never reaches device creation.
Works under Proton on native x86, which isolates the fault to translation of
self-modified code.

## The lesson worth keeping

Newer games translate better; the further back a game reaches, the further the
compatibility. See
[Compatibility runs backwards](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Findings#compatibility-runs-backwards).
Mirror's Edge Catalyst is a 2016 title with a 2016-era DRM wrapper, and it is the
wrapper's startup technique — not its graphics, not its licence — that the
translation layer cannot serve. Jedi Survivor (2023, the same publisher, the
same family of protection) reached the graphics stage on this machine; this one
does not get past the unpacker.
