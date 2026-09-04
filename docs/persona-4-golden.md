# Persona 4 Golden — twenty-seven runs, and where they stop

Internal notes. No wiki entry and no fix: what stops this game is Denuvo
Anti-Tamper deciding against this machine (the executable names it: `DODENUVO`,
`denuvo_atd`, `srv03.antitamper.net`), and the one thing that would "fix" it is
out of scope. Written down so the next attempt starts from here.

**Preview is no longer supported.** As of 2026-08-31 the only supported engine
is stable CrossOver 26.3, and CrossOver Preview was dropped. The Preview
measurements below are kept as a record of what was measured, not as guidance
about where to run anything.

**Symptom.** Opens, shows a window with an FPS counter in the title bar for a
second or two, and closes. (2026-09-02: on stable it reaches the loading
screen and stops there at 12-14 s; see runs eighteen onward.)

## What the engine decides

The single sharpest result is that the CrossOver build decides whether the game
kills itself.

| Engine + backend | What happens |
| --- | --- |
| Preview 20260821, D3DMetal | Self-terminates, deterministically |
| Preview 20260821, DXMT | Dies immediately |
| **Stable (2026-07-15), D3DMetal** | **Does not self-terminate.** Window, FPS, then hangs -- run eighteen below: it raises the same fault, and the hang is where wine's dispatch of it ends |

Same executable, same bottle, same machine.

## The self-termination, measured

Under Preview the fault is identical every time -- not merely the same address,
but the same sixteen register values, with and without any instrumentation:

    ACCESS_VIOLATION at 00000001669D745B   writing to FFFFFFF22D819090
    rax=0000000080000002 rbx=0000000000000000 rcx=000000015e4aface ...

The instruction at that address, read from inside the process because the
executable is packed and cannot be disassembled from disk:

    a2 90 90 81 2d f2 ff ff ff      mov byte ptr [0xFFFFFFF22D819090], al

The impossible address is not computed. It is written into the instruction.
Nothing arithmetic went wrong, because there is no arithmetic -- an afternoon
was spent looking for a mispredicted pointer that does not exist.

The 32 bytes before decode as a function epilogue (`add rsp,0x28 ; ret`) and
alignment padding, so this is real code, not data being executed. The faulting
instruction begins one byte inside what a linear disassembler reads as `cpuid`
-- overlapping instructions, which is deliberate obfuscation. Every stack frame
that resolves to a module is `P4G.exe`: no `d3d11`, no `dxgi`, nothing from the
probe. `rax` holds `0x80000002`, `E_OUTOFMEMORY` in its old OLE form.

A store to a fixed impossible address, reached only from the game's own code, is
what anti-tamper protection does when a check fails. **Defeating that check is
circumvention and this project does not do it.** That is the reason work stopped
here, not a lack of leads.

## Ruled out, each by measurement

- **Our own watching.** A control build with every hook disabled -- no vtable
  patched, no import redirected -- faults at the same address with the same
  sixteen registers. Nine earlier runs stand.
- **The graphics layer.** Across a thousand logged calls D3DMetal refuses
  exactly two things: `EnumAdapters(1)` and `EnumOutputs(1)`, which mean "there
  is only one" and are correct. Nothing else is refused. There is no bad answer
  here to answer better, which is what this project's whole method depends on.
- **Adapter memory figures.** Rewritten from 38338/38338/38338 MB to a
  believable 4096/0/8192; crash unmoved, resource counts identical.
- **The vendor id, on D3DMetal.** Rewritten from 0x10de to 0x1002; crash
  unmoved. (On DXMT it does change behaviour -- see below.)
- **`Map` returning a bad pointer.** 961 calls, all successful, every returned
  pointer a normal user-space address. The theory that a mapped pointer produced
  the fault was good and wrong.
- **Missing dependencies.** Every DLL the game imports resolves, including
  `d3dx11_43`, `vcomp140` and the 64-bit `MSVCP110`/`MSVCR110`.
- **Wine's `d3dx11_43`.** The hang under stable puts frames in it, so the native
  Microsoft build (276 KB, present in the bottle) was overridden in for
  `P4G.exe` in place of Wine's (102 KB). Confirmed loaded from `system32`; the
  hang is unchanged.

## It is not a hang, and it does load its archives

Two things written above were wrong, and both were wrong the same way: a probe
was pointed at one door, saw nothing, and the silence was written down as a fact
about the game.

**It is not frozen.** The window renders the game's own loading indicator at
24-29 fps with the Metal HUD confirming GPU work, and its frame limiter -- read
in the disassembly at +0x609c29: QueryPerformanceCounter, elapsed microseconds,
Sleep -- runs normally. That only became visible when the Metal HUD was turned
on, at the user's suggestion, after an afternoon of measuring CPU from outside.
Removing the cross-engine GStreamer contamination described below took it from
24 fps to 61.

**It does open its data.** A file probe reported "167 files opened, and not one
archive", which was repeated here as the headline finding. The arithmetic kills
it: P4G.ini is 1374 bytes and was opened 163 times, which is 218 KB, while the
same log counted 192 reads totalling 20 MB. Roughly 19.8 MB came from handles
the probe never saw -- because it hooked `CreateFileW` and the game imports
`CreateFileA` as well. The archives were being opened the whole time.

The same error, twice more: a wait probe hooked only `WaitForSingleObjectEx` and
recorded one wait while forty-six threads sat in waits, which was written up as
"the protected binary bypasses kernel32". It does not; most code calls the plain
form, which was not hooked. And a control build meant to prove the probes were
innocent had its early return placed above `AddVectoredExceptionHandler`, so it
ran with no fault handler at all and its empty log was very nearly read as "no
crash happened".

## Cross-engine GStreamer, which was ours

Every run made with the stable engine had two copies of libgstreamer in the
process, and the terminal said so from the first one:

    Class GstCocoaApplicationDelegate is implemented in both
      /Applications/CrossOver.app/.../libgstreamer-1.0.0.dylib
      /Applications/CrossOver Preview.app/.../libgstreamer-1.0.0.dylib

The bottle was wired for Preview, correctly, and the runs were launched with
stable. A staged codec reaches its libraries through symlinks into one
CrossOver's bundle, so loading it under another drags that engine's core in
beside the running one -- the exact crash `stage-codecs.sh` was written to avoid,
arrived at from the one direction it did not cover.

It is not the cause: removing it changed the frame rate and nothing else, and on
Preview -- where there is no duplicate, because the bottle points at Preview's
own staging -- the game fails identically with `GST_PLUGIN_PATH` emptied. But it
was underneath every stable-engine measurement taken before it was noticed, so
those are worth less than they were credited with at the time.

Two repairs came out of it, in `diagnostics/launch-with.sh` and
`stage-codecs.sh`, and they are written up in
[what we got wrong](what-we-got-wrong.md).

## What was eliminated on the loading screen

- **A controller.** A DualSense Edge was connected and `dinput8` frames jumped
  from 2 to 24 between two captures. With it fully powered off, verified at zero
  HID entries, the stall is identical.
- **Disk.** No failed open, no failed read, no short read, no read outstanding.
- **Audio.** `mmdevapi`, `winecoreaudio` and CoreAudio all load. The empty
  `SoundDeviceID` in P4G.ini is the game's own shipped default.
- **Steam.** `steam.exe` runs with `steam://run/1113000`; the process holds
  `steam_api64.dll` and `steamclient64.dll`.
- **Wine's partial `d3dx11_43`.** The native Microsoft build was overridden in
  and confirmed loaded from `system32`; unchanged.
- **The staged codec's version.** The plugin is gstreamer-libav 1.24.14 with
  FFmpeg 6.0; Preview's core is 1.28.5. That mismatch is real and worth closing
  on its own account, but emptying `GST_PLUGIN_PATH` on Preview changes nothing
  here.

## Run eighteen -- 2026-09-02: the stable hang is the same exception, lost in the unwinder

Stable 26.3 (the patched copy), D3DMetal 4.0b2 with Metal 4 off, launched through
RaccoonBot with `CX_DEBUGMSG=+loaddll,+mfplat,+mfreadwrite,+winegstreamer,+xaudio2,+mmdevapi,+quartz,+seh`
in the game's environment field. First stable run whose log reaches the fault.
The main thread, in order:

- 171 `NtQueryInformationProcess` calls (ProcessBasicInformation and
  ProcessWow64Information) over eleven seconds, then the game's own
  `OutputDebugString`: "Received stats and achievements from Steam".
- 1.5 s later, the fault the Preview runs recorded, byte for byte: `c0000005`
  writing `FFFFFFF22D819090` from `1669D745B`, `rax=80000002`. **Stable raises
  it too.** The table above described the outcome, not the cause.
- Wine's dispatcher walks the stack. `1669D745B` lies in `.arch`, the packer's
  422 MB executable section, and has no `RUNTIME_FUNCTION` (the table holds
  37 524 entries, 1 945 of them in `.arch`, none covering it), so
  `RtlVirtualUnwind2` treats it as a leaf and reads the return address off the
  stack. What it reads is not code: `16421ABD8`, then `A000800000000000`, `0`,
  `2081C0000`, `10EDC8`, `0`, `10EDC0` -- stack addresses and garbage, because
  the protected code does not keep a Windows-shaped stack. No module owns those
  addresses, so `virtual_unwind` asks the host unwinder (`unwind_builtin_dll` ->
  `libunwind_virtual_unwind`), and for frame `10EDD0` that returns a handler at
  `7FF819B27DA8`, inside `/usr/lib/system/libunwind.dylib`.
- Wine calls it as an SEH handler. It faults at `unwind_phase2+611` reading
  `NULL+0x48`; wine's nested-exception handler returns 2, "nested exception",
  and dispatch restarts at the same frame. 247 times in eight seconds, then
  `libunwind_virtual_unwind: last frame`, and the main thread never logs
  another line. Three minutes on it was still parked -- 3 % CPU, the audio
  thread feeding silence 64 to 256 frames a call -- when the run was closed.

So "Preview terminates, stable hangs" is one event with two endings. The store
to an impossible address is still the protector's own decision -- nothing
precedes it but Steam stats and process queries, and no vectored handler of the
game's is registered in this process (`+seh` prints every vectored call; there
were none) -- and the reading above stands: why the check fails is the game's
business. What the engine decides is only how wine's dispatch ends when the
stack under a protected frame cannot be walked: this stable build wanders into
the host unwinder and stays there, Preview evidently reached process death.
That second half is a wine defect of a kind -- calling a personality routine
found through libunwind for a Windows exception, in the branch where stock wine
prints "calling personality routine in system library not supported yet" and
clears it -- but repairing it would only turn the hang back into the crash.

Also measured on the way:

- **No codec question.** The game's own process loaded no `mfplat`,
  `mfreadwrite`, `winegstreamer`, GStreamer or libvpx at any point; the three
  `mfplat` loads in the same log belong to Steam's web helper. The 80 movies are
  CRI Sofdec2 with VP9 inside (`usm_vp9`, IVF chunks) and the executable
  carries its own libvpx. "Like Strikers" was the wrong prior: Strikers needed
  VC-1 over DirectShow plus a D3D9 bridge.
- `nvngx.dll` and `nvapi64.dll` load as wine builtins on D3DMetal as well; the
  NVIDIA vendor id is not a DXMT-only story.
- Steam's `gameoverlayrenderer64.dll` is in the process and patches code in
  place. Not eliminated. The two launches still untried that are not
  circumvention: the overlay off in Steam's own settings, and `advertiseAVX`
  off in RaccoonBot, since the faulting instruction sits one byte inside a
  `cpuid`.

## Runs nineteen to twenty-two -- 2026-09-02: four endings, one verdict

Four more launches on stable 26.3, each changing one thing that is
configuration rather than tampering, each with `+seh` logged. The protector's
fault fired in every one, at the same address with the same registers, 12 to
14 seconds in and 1.5 s after "Received stats and achievements from Steam".
What changed was only the ending:

| Run | Change | Fault | How wine's dispatch ended |
| --- | --- | --- | --- |
| 19 | `advertiseAVX` off (Rosetta stops advertising AVX) | 14.2 s | 273 calls into the host libunwind handler, then parked. Hang. |
| 20 | DXMT instead of D3DMetal | 12.9 s | 241 calls into the same handler, then the process vanished 7 s later with no wine exit trace and no macOS crash report. |
| 21 | DXMT, NVEXT off, AMD identity (no wine log: the env field had been replaced instead of extended) | -- | Steam: 16 s alive. |
| 22 | DXMT, `DXMT_ENABLE_NVEXT=0`, `DXMT_CONFIG_FILE` with DXMT's own Genshin profile (vendor 1002, device 7340, "AMD Radeon Pro 5300M"); DXMT confirmed reading it; `nvapi64`/`nvngx` no longer loaded | 12.4 s | **The game's own handler ran.** DXMT's two vectored handlers (in its `d3d11` and `dxgi`) returned 0, the walk took 65 leaf steps through the same garbage without ever consulting the host unwinder, reached a real frame at `10f1f0` and called the handler at `P4G.exe+0x823E28`. That handler did not return: it raised `c0000409` (fast-fail) from `P4G.exe+0x823584`, noncontinuable, and the process ended cleanly. 14 s alive. |

Run 22 is the Windows-shaped ending, and it settles the question the other
runs left open: when dispatch does reach the game, the game aborts itself.
The hang (18, 19), the silent death (20) and the clean abort (22) are the same
verdict delivered three ways, and which one a launch gets depends on what
garbage lies on the stack under the protected frame and whether the host
unwinder happens to claim one of those addresses (run 18: `2081c0000` was
claimed; run 22: `480360`, `aed8b0`, `7fe61d80b200` were not). NVIDIA
identity, NVEXT, AVX advertising and the graphics backend do not move the
fault by a byte. They are eliminated.

Not eliminated, for the record: the Steam overlay. Its per-game switch was
turned off before runs 20 to 22 and `gameoverlayrenderer64.dll` loaded in all
of them (its per-frame scan for `pbcl`/`pbsv`/`sl.interposer`/`xinput` is the
49-fold `LdrGetDllHandleEx` burst in the log), so the switch never reached
this bottle's Steam. Low expectation: the overlay was also present on the
Preview runs that self-terminated and on every run that hangs.

## Run twenty-three -- 2026-09-02: the activation round-trip happens

Same launch as run 22 with `+winsock,+wininet,+winhttp,+dnsapi,+secur32`
added. `lsof -i` on the process at 1, 4, 7 and 11 s saw no socket, because
the whole exchange fits inside one second. Relative to the game's first log
line, on one network thread:

    3.57  connect 127.0.0.1:50186                (the Steam client)
    3.58  getaddrinfo "garry.sgaas.net" 443  -> 52.30.36.110, 99.80.129.80
    3.65  connect 52.30.36.110:443           (non-blocking, WSAEWOULDBLOCK)
    3.90  TLS handshake begins (schannel)
    4.39  handshake completed; request encrypted and sent
    4.64  reply received, decrypted: 399 bytes; socket closed
   12.47  Denuvo's fault, 1.5 s after "Received stats and achievements"

`garry.sgaas.net` is SEGA's, on Amazon in Ireland, and it is the host SEGA's
Denuvo titles use for product activation. So the token request goes out,
TLS works, and an answer of 399 bytes comes back eight seconds before the
verdict. What the answer says is inside TLS and not ours to read; a token
is normally larger than 399 bytes, a refusal is not. No second connection
follows. `wininet` was loaded but never used (61 `DllMain` lines, no request);
the game speaks winsock and schannel directly.

## Run twenty-four -- 2026-09-02: the same, with the network off

Wi-Fi down after Steam was up and logged in (the host had no default route
when the game started). `getaddrinfo("garry.sgaas.net")` at 3.70 s returned
nothing, no connection to port 443, no TLS handshake, and "Received stats and
achievements from Steam" never printed. Denuvo's fault came anyway at 11.7 s,
the game's handler fast-failed as in runs 22 and 23, no dialog, no message.
Steam: 14 s alive.

What that does and does not say. It does not separate "the server refused"
from "a local check failed": a first activation needs the server, this
machine has never held a token, so offline was always going to fail too.
It does say that the game's failure path is the same whether the 399-byte
answer arrives or not, that Denuvo shows no UI in either case, and that
nothing later than the exchange -- Steam stats, a second connection --
is part of the decision. The verdict is taken with or without the network,
at the same point in the loading screen.

For the record, the only files the game wrote across the day's runs were
its own `AppData\Local\SEGA\P4G\P4G.ini`, `crashdat\bd\settings.dat`
(Crashpad); the one temp file in the window is a JPEG thumbnail of Steam's
own; nothing that looks like a cached token was ever created.

## Run twenty-two, revisited -- the protector's raw syscalls, and a token that stays

Two things in the same logs that only made sense after reading what other
people had found (see [what the rest of the world reports](persona-4-golden-community.md)).

**Raw syscalls.** Every run carries 66 lines of `trace:seh:sigsys_handler
SIGSYS, rax 0x36` from the game's main thread, between 2.7 s and 11.8 s --
the last one 0.7 s before the fault -- from 32 different sites, all inside
`.arch`. `0x36` is Windows 10's number for `NtQuerySystemInformation`: the
protector bypasses `ntdll` and executes `syscall` itself, wine's SIGSYS
handler catches it and dispatches it. One of those queries lands on a class
wine only half-implements (`fixme:ntdll:NtQuerySystemInformation info_class
SYSTEM_PERFORMANCE_INFORMATION`, 3.56 s, main thread). The nearest public
analogue is Sikarugir issue #233 (Age of Empires II: DE, 2026-06): a
deterministic access violation inside Denuvo code under Rosetta, traced to a
raw `syscall` that wine's SIGSYS path answered wrongly. Ours is a different
instruction -- a store to an impossible address, not a null dereference --
but the shape is the same: protector code, raw syscalls, wine's emulation of
them, then the fault. Which classes the 66 queries ask and what wine answers
is the next measurement, and it is a measurement of the engine
(`+ntdll,-unwind` on the next run; the `ntdll` channel prints the class).

**The token.** `Program Files (x86)\Steam\userdata\77523073\1113000\13133033137`,
5 261 bytes of hex text, was created at 10:39:10 during the day's seventh
launch (run 19) and never modified or deleted since, through five more
faulting runs. That path is where Denuvo keeps its activation ticket in
another title (Tekken 7, Proton issue #3267, unverified), and that report says
the game deletes it when it judges it invalid. If the same holds here, the
server issued a ticket, the game kept it, and the fault at 12 s is a local
verdict taken after activation -- which is also what run 24 (network off,
same fault, same second) says from the other side.

Also checked, for the record: `DigitalProductId` in the bottle's registry is
all zeros in both keys (GE-Proton's Denuvo fix was only to stop wine.inf from
rewriting that key); `d3dx11_43.dll` in `system32` is the native 276 KB
build and loads as native. Thirteen launches on 2026-09-02 between 09:58 and
12:02 -- if Denuvo counts engine variants as machines, the five-per-day limit
was crossed, but every documented refusal is a popup or a browser page, and
we saw neither.

## Runs twenty-five to twenty-seven -- 2026-09-02: the syscalls answered, and stock CrossOver falls the same way

**Run 26, `+ntdll,-unwind`** (25 had no `CX_LOG`; the launcher's field replaces
rather than merges). All 66 raw syscalls are the same call:
`NtQuerySystemInformation(SystemBasicInformation, buf, 0x40, NULL)`, from
2.79 s to 12.14 s, the last one 0.61 s before the fault at 12.75 s. Wine's
SIGSYS handler dispatched every one to the real function (the `ntdll` trace
follows each `SIGSYS` line on the same thread). In the 0.6 s between the last
one and the fault the main thread made exactly one more `ntdll` call. The
verdict is taken inside the protector's own code, with nothing further asked
of wine. Other classes seen in the process, all from a second thread by the
normal path: `SystemCpuInformation` x8, `SystemDynamicTimeZoneInformation`
x6, `SystemInterruptInformation` x2, `SystemPerformanceInformation` x1 (wine's
fixme), `SystemWineVersionInformation` x1.

**The probe.** A 90 KB test program (`sbi-probe.c`, built with llvm-mingw) run
in the same bottle on the same engine asks `SystemBasicInformation` through
`ntdll` and through a raw `syscall` with SSN 0x36, and checks the register
convention afterwards. Both answers are identical and Windows-shaped: page
4096, granularity 65536, 48.0 GB in 12 582 911 pages, user range
`0x10000`-`0x7ffffffeffff`, 16 processors, affinity `0xffff`, status 0, 64
bytes written. After the raw syscall `rcx` holds the next instruction and
`r11` the flags, as Windows leaves them. The one field that is not what
Windows returns is `TimerResolution`, 0 here where Windows reports the clock
increment (156 250). Upstream wine never fills that field either, so the CrossOver builds the
community succeeded on returned the same 0; it is not the cause.

**Run 27, stock CrossOver 26.3** (the user's control: `/Applications/CrossOver.app`
26.3.0.39832, its own Steam bottle, no RaccoonBot, no engine patches, no
hooks; no wine log). Steam: launched 12:48:42, process removed 12:49:06. Same
symptom by the user's account: "se cae". That removes the launcher and our
patched copy from the suspects. What is left is what stock CrossOver 26.3 on
this host shares with our copy: the engine version, macOS 27 with its
Rosetta, and the machine itself. Every public success with this title on
Apple silicon is on CrossOver 23.6 to 26.1 and macOS 14 to 15
([community notes](persona-4-golden-community.md)).

**Where that leaves it.** Two launches would settle the remaining question,
both legitimate: the same bottle under CrossOver 26.1 or 26.0 on this Mac
(a regression in the 26.x line, which CodeWeavers would want to hear about,
since they support Denuvo officially), and one launch after a day without
any, in one fixed configuration, to retire the five-activations-a-day
reading even though no documented refusal is silent. A bug report to
CodeWeavers with the measured chain -- the fault, the dispatch, the
syscalls, the token, the network exchange -- is the useful output either
way. Circumventing the check remains out of scope.

## The engine's own answers, and the versions -- 2026-09-02

The user asked which Wine CrossOver 23 carries and what changed in the files
since. The answer, with the sweep and the probes behind it, is in
[the engine between CrossOver 23 and 26.3](persona-4-golden-engine-history.md).
The short form: 23.x is Wine 8.0.1 plus CodeWeavers' patches, 26.x is Wine
11.0 plus theirs, and the public successes with this title sit at both ends
(23.6 and 26.1), so the 8.0.1 -> 11.0 delta is not what breaks us. 26.3's core
binaries are byte-identical between the stock app and our copy. The 26.1 ->
26.3 delta is CodeWeavers' own overlay and the changelog lists only per-app
fixes; its source diff needs `crossover-sources-26.1.0.tar.gz` (149 MB),
not yet downloaded. A fidelity probe run in the bottle finds real divergences
from Windows -- a raw `syscall` writes the return address below `rsp`
(`pushq 0x70(%rcx); ret`, upstream and CrossOver alike), debug registers are
faked, `mov cr3` is classified as illegal rather than privileged, top-down
allocations stop 256 MB short -- and none is unique to this host. The one
variable no report covers and no local probe can baseline is macOS 27's seed
Rosetta on this M4 Max.

## Where it stands -- parked 2026-09-02

Twenty-seven measured runs, a community sweep, an engine-history sweep, two
probe programs and a source diff later, the case rests here:

- The stop is Denuvo's own verdict, taken at 12-14 s on the first loading
  screen, delivered as a store to an impossible address from `.arch` and, when
  wine's dispatch reaches the game's handler, a `c0000409` self-abort. Hang,
  silent death and clean abort are one event with three endings.
- Activation succeeds (TLS exchange with SEGA's host, a token that persists),
  the fault reproduces offline, and none of codecs, graphics backend, GPU
  identity, NVEXT, AVX advertising, the Steam overlay, our launcher or our
  patched engine changes a byte of it: stock CrossOver 26.3 fails the same.
- The engine core is source-identical from 26.1 to 26.3, and the fidelity
  probe finds no divergence unique to this host.
- What no report covers and no local probe can baseline is macOS 27's seed
  Rosetta on this M4 Max. The one translator differential available here,
  CodeWeavers' ARM64 preview on FEX, was tried and did not open at all.

Parked, not closed: the next data point is this same bottle on a macOS 26.5
host, or a fresh look when Apple ships a Rosetta update or CodeWeavers a
26.4. A CodeWeavers ticket with the measured chain is drafted on request.
Circumventing the check stays out of scope.

## Why it was not chased further

The game reaches a window and stops with 46 of 48 threads parked in `ntdll`,
memory flat, roughly a tenth of one core busy. Nobody is working and nobody is
being woken.

Two tools failed on it, both for the same reason:

- `winedbg` cannot unwind stacks under Rosetta. It returns one or two frames per
  thread, which is not enough to name a wait.
- Import-table hooks do not see this binary. A probe watching every untimed wait
  recorded **one** call while 46 threads sat in waits; the same probe watching
  `VirtualAlloc` recorded no refusals at all. The protected executable reaches
  `ntdll` directly rather than through `kernel32`, so the usual hooking sees
  almost nothing.

Hooking `ntdll` itself inside a protected process is fragile and is
indistinguishable from tampering with it. That is the wall.

## What is worth reporting upstream

**The adapter describes itself three ways at once.** This is a real defect in
the translation stack, independent of this game, and it is written up in
[Findings](../wiki/Findings.md).

    adapter 0 from GetDesc: "AMD Compatibility Mode"
      vendor 0x10de   device 0x66af

`0x66af` is AMD's Radeon VII. `0x10de` is NVIDIA. The name says AMD. Two of the
three say AMD and the vendor id says otherwise -- and it is the vendor id that
software branches on. DXMT demonstrably does: it prints

    info:  Vendor extension enabled: NVEXT

and loads `nvapi64.dll` and `nvngx.dll` on a machine with no NVIDIA hardware
anywhere near it.

**And the engine build alone decides whether the protector kills the process.**
Same binary, same bottle: Preview terminates, stable does not. That is a
reproducible-to-the-byte difference worth handing to CodeWeavers, and it is the
most useful thing this investigation produced.
