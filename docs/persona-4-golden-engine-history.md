# Persona 4 Golden — the engine between CrossOver 23 and 26.3, and what this host answers

Internal notes, companion to [persona-4-golden.md](persona-4-golden.md) and
[what the rest of the world reports](persona-4-golden-community.md).
Compiled 2026-09-02 from five parallel sweeps (Wine's git history, CodeWeavers'
statements and sources, Apple's Rosetta documentation and release notes, other
Wine forks' Denuvo work, and a read-only survey of the installed 26.3 binaries),
the key claims re-fetched, plus two probe programs run in the game's own bottle
on this machine. *Unverified* marks a claim read once and not re-checked.

## Wine base per CrossOver version

Base Wine per CrossOver major (CodeWeavers changelog, re-fetched 2026-09-02 and confirmed verbatim): 23.0.0 (2023-08-16) "includes Wine 8.0.1 ... and selected patches from recent Wine"; 23.5.0 (2023-09-27), 23.6.0, 23.7.0 (2023-11-27, "MSync included"), 23.7.1 are point releases on that same 8.0.1 base; 24.0.0 (2024-02-22) = Wine 9.0; 25.0.0 (2025-03-11) = Wine 10.0; 26.0.0 (2026-02-10) = Wine 11.0 ("with over 6,000 improvements"); 26.1.0 (2026-04-09), 26.2.0 (2026-06-09), 26.3.0 are point releases on 11.0. The web changelog dates 26.3.0 "July 21, 2026" (its tarball Last-Modified is also 2026-07-21 14:56 UTC); the README inside the installed app says "July 13, 2026" and the code signature is timestamped 2026-07-15 -- so "26.3 (2026-07-13)" in our ground truth is the internal build date, not the public release date. The word "Denuvo" appears exactly once in the whole changelog (23.5.0, "Denuvo games run on Sonoma"); "Rosetta" only in an 11.x-era Rosetta Stone entry; nothing from 24.0 onward mentions Denuvo, Rosetta, Sequoia or macOS 27. Only "UI updates for macOS Tahoe" (26.0.0) and "Fix for Intel Tahoe" (25.1.1) touch the OS.

What the local binaries say (stock /Applications/CrossOver.app 26.3.0.39832, direct inspection): lib/wine/x86_64-unix/ntdll.so embeds "11.0" and the build id "wine-11.0-8726-g2e2f5fca349" (Wine 11.0 plus 8,726 CodeWeavers commits, head 2e2f5fca349); wineserver embeds "Wine 11.0"; bin/wine is a 42 KB Perl wrapper printing "Public Version: 26.3.0"; bin/wineloader is a 33 KB hardened-runtime x86_64 executable linking only libSystem; the real loader is lib/wine/x86_64-unix/wine. Every engine Mach-O is x86_64-only (no arm64 slice, no wine-preloader), LC_BUILD_VERSION minos 10.15 / SDK 15.1 / ld 1115.7.3 (Xcode 16.1), Info.plist BuildMachineOSBuild 23J220 (Sonoma) -- nothing was built or linked against a macOS 26/27 SDK. ntdll.dll's PE timestamp is 2026-07-15 10:43:58 UTC. The stock and MGVF-patched copies hash identical for ntdll.so, ntdll.dll, wine, wineloader, wineserver, win32u.so, winemac.so, kernel32.dll, kernelbase.dll; the only differing files are winegstreamer.so/.dll (plus our lib64 additions), so the whole syscall/SEH path is byte-identical in both -- consistent with stock failing identically. Caveat on the control: the "stock" app is not pristine (its winegstreamer pair was restored by us on Aug 29 and it carries our lib64/apple_gptk_4 payload); a fresh 26.3 download would be a cleaner control, though the ground truth already eliminates graphics and codecs.

Strings compiled into 26.3's ntdll.so (direct): sigsys_handler, "SIGSYS, rax %#llx, rip %#llx.", "sysctl.proc_translated", "Setting debug registers is not supported under Rosetta, faking success" (a CodeWeavers wording; upstream's WARN has no "faking success"), "gsbase %016lx teb %p at instr %p, fixing up" (a Wine 11.0-era string, absent in 10.5), libunwind_virtual_unwind, dwarf_virtual_unwind, "HACK: exec fault on executable page, addr %p", "HACK: write fault on a w|x page, addr %p", cxcompatdb.so hook, CX_APPLEGPTK_LIBD3DSHARED_PATH. Imports include _unw_init_local/_unw_step/_unw_get_proc_info/_unw_set_reg/_unw_getcontext and __Unwind_Find_FDE from libSystem; there is no _thread_set_tsd_base import and no "mov $0x3000003,%eax" byte pattern. Nothing in the binaries or changelog names Denuvo, macOS 27 or an M4.

The local CrossOver 26.3 source tarball (/Users/mathias/Development/crossover-sources-26.3.0.tar.gz, unpacked in /Users/mathias/Development/sources; wine/VERSION "Wine version 11.0", configure.ac "Crossover hacks") is available for grep; other trees under ~/Development (mgvf-winegstreamer-*/wine-src, ~/.local/winevideo/src/wine) are stock Wine 11.0, not CrossOver.

## What changed in the files a protector exercises

All commits below are in wine-mirror/wine unless noted; "verified" means the second pass re-fetched the commit/MR and confirmed; unverified items are flagged.

1. Raw-syscall (SIGSYS) emulation -- dlls/ntdll/unix/signal_x86_64.c. Verified: upstream Wine 8.0.1 (tag 2023-04-20) and 9.0 contain no SIGSYS code at all (grep count 0). The handler is a88a0736ea1197b9856dc7322b1c5b334f0a5de0 "ntdll: Add SIGSYS handler to support syscall emulation on macOS Sonoma and later" by Brendan Shanks (CodeWeavers), authored 2023-06-16, merged 2024-11-06 via MR !6777 (https://gitlab.winehq.org/wine/wine/-/merge_requests/6777, with a companion test commit 1553d482), first released in Wine 9.21; present in 10.0 and 11.0. MR text: valid macOS syscall numbers carry a class in the top 24 bits (start at 0x2000000), so Windows SSNs are invalid and XNU raises SIGSYS; "macOS 13 and earlier have a kernel bug which prevents SIGSYS from being delivered"; "adapted from Apple's Game Porting Toolkit Wine patch, and from the 'ntdll-Syscall_Emulation' wine-staging patchset" (Paul Gofman, 2020-07-14, seccomp, bug 48291). The handler only routes: frame->rip = RIP+0xb, frame->rcx = RIP, R11 = eflags, RCX = frame, RIP -> __wine_syscall_dispatcher_prolog_end_ptr; the +0xb is undone by the pre-existing "subq $0xb,0x70(%rcx)" (Legends of Runeterra hook, already in 8.0.1). It never inspects arguments or results. The only later change to the macOS path through 11.0 is d2085b768b65 (Paul Gofman, authored 2025-03-12/13, Wine 10.4): if TF was set, clear it and mark CONTEXT_CONTROL in restore_flags. After 11.0 (not in CrossOver 26.x): cbb9906d7534 Linux Syscall User Dispatch (Elizabeth Figura, 2026-03-16, Wine 11.5) widened the #if guard; b90cdc04d775 (11.5) added a Linux-only early return. Inference, NOT in any source: that CrossOver 23.5's "Denuvo games run on Sonoma" is this patch shipped out of tree (the author is CodeWeavers staff and authored it two months before 23.0; upstream 8.0.1 has none). Settling it needs crossover-sources-23.5.0.tar.gz (134.9 MB) -- not downloaded. In the 26.3 source tree (direct read, unverified by second pass) sigsys_handler is upstream's plus "CW Hack 24265": on M3, Rosetta restores mxcsr from the sigcontext even after it is patched, so the handler rewrites the FPU area and redirects rip through a __restore_mxcsr_thunk before the dispatcher. On this host a raw syscall 0x36 still arrives as SIGSYS (trapno 133, err 0x36) -- measured.

2. Syscall numbering -- dlls/ntdll/ntdll.spec. Verified: c8c0a023ff5dbb9adec1621b4558a88306da7709 "ntdll: Add explicit ids to a number of syscalls" (Alexandre Julliard, 2025-06-05, first in Wine 10.10, first stable 11.0; companion winebuild 731cd0a769) gives NtQuerySystemInformation -syscall=0x0036 and NtQueryInformationProcess -syscall=0x0019 from j00ru's table. Before it, id 0x36 dispatched NtCreatePort (8.0.1), NtDeleteAtom (9.0), NtDebugActiveProcess (10.0). Since the title reached gameplay on CrossOver 23.6-25 (Wine 8.0.1-10.0), its protector must read SSNs out of ntdll's thunks rather than hard-code 0x36 -- inference, but the only reading consistent with the reports.

3. GSBASE / TEB access -- verified: MR !6866 (six commits, merged 2025-04-02, first in Wine 10.5, "%GS register swapping on macOS" in the 10.5 announcement): 3a16aabbf55b "On macOS x86_64, swap GSBASE between the TEB and macOS TSD when entering/leaving PE code" and 90a9078eae15 "Remove x86_64 Mac-specific TEB access workarounds" (Brendan Shanks, authored 2024-08-15). Before: upstream mirrored only TEB 0x30 (Self) and 0x58 (TLS pointer) into the macOS TSD (wine-8.0.1 call_init_thunk, ".byte 0x65 movq", pthread_teb in Reserved5[0]); the MR notes CrossOver additionally hacked 0x60/PEB and binary-patched CEF binaries for 0x8/StackBase, and that Apple's libd3dshared could enable a Rosetta "special mode" using the Windows TEB for %gs in certain regions. After: %gs = real TEB in PE code; init_handler sets TSD, leave_handler restores TEB; dispatchers inline "movl $0x3000003,%eax; syscall". Listed drawbacks: direct PE-to-Unix jumps crash ("Notable examples are D3DMetal and DXMT"); a direct `syscall` from Windows code "likely needs a valid stack pointer ... something anticheat code might do". The MR also says Rosetta 2 does not correctly implement GS.base in the full thread state (set to 0 on entry, not read on exit) -- given as a reason for rejecting an alternate design. Local observation: CrossOver 26.3's ntdll.so has no _thread_set_tsd_base import and zero 0x3000003 syscall byte sequences, so CodeWeavers apparently did not adopt the swap (plausibly because they ship D3DMetal) and still runs the pre-10.5 TSD scheme -- a standing divergence: PE reads of TEB fields other than 0x30/0x58/0x60 through %gs get macOS TSD contents. Follow-ups: 3aa28e452018 (10.9), 86b886788ba2 "%cs in sigcontext" (10.13, Intel only), 94447cee6194 check_invalid_gsbase in segv_handler (10.14, bug 57444) with a3e3c0a11724 (10.15) and fd6bdbeda50c (10.17, bug 58755) -- the "fixing up" string is in 26.3's binary, so at least this fix-up is present: a SIGSEGV at a %gs-prefixed instruction in PE code is silently resumed with GSBASE reset instead of being raised (unverified by second pass, from direct source read). 6f6f66ee05 (2026-02-03, after 11.0) stops fixing gsbase on execution faults (D2R loop on Intel) -- unverified.

4. Debug registers under Rosetta -- verified: 93fde56b494151f5e4bdfc560f930867bda52514 "server: On macOS, fake debug registers when running under Rosetta" (Brendan Shanks, based on Tim Clem, committed 2023-02-17, Wine 8.2, bug 54367): get_thread_context returns all-zero DR0-DR7 for translated processes; set_thread_context returns STATUS_UNSUCCESSFUL; 39655dade3c8 adds the ntdll WARN. CrossOver 26.3's ntdll.so instead says "...faking success" and its ntdll (not only wineserver) reads sysctl.proc_translated -- so CrossOver reports DR writes as successful, upstream reports failure; both read back zeros. Same on every Apple Silicon Mac including the ones where the game worked.

5. Exception dispatch and unwinding -- verified with corrections: leaf-function handling moved into RtlVirtualUnwind in 654c03d1317f (Julliard, 2024-02-22, first in Wine 9.3); libunwind removed by e7439f1be35f "configure: Stop using libunwind" (Julliard, 2026-03-30, "This was only useful for macOS .so dlls that we no longer support", first release without it Wine 11.6; 11.5 still has it). In Wine 11.0 through 11.5 (and CrossOver 26.3's ntdll.so, which imports the unw_* symbols) PE-side virtual_unwind hands a frame with no unwind entry and no owning PE module to unwind_builtin_dll, which after _Unwind_Find_FDE falls back to host libunwind (libSystem 1351 on this host). This explains the measured hang-vs-fast-fail split; it is aftermath of the AV, not its cause, and no commit in the window changes how a handler-less fault inside a module is delivered to the game's SEH handler. segv_handler's PROTFLT/PAGEFLT mapping is identical in upstream master and CrossOver 26.3 sources (verified by whitespace-insensitive diff): trapno 14 -> ACCESS_VIOLATION {(err>>1)&9, si_addr}; trapno 13 -> {0, 0xffffffffffffffff unless selector error}.

6. Fault classification under Rosetta (measured here, verified reproduction): a store to a non-canonical address (0x8000000000000000, 0xdead00000000dead) arrives as SIGSEGV trapno 14, err 6, faultvaddr = the address, indistinguishable from an ordinary unmapped store; real x86-64/Windows raise #GP and Windows reports ExceptionInformation {0, -1} (Mozilla bug 1493342: "Linux and OS X report the address as 0; Windows reports it as -1"). So for a non-canonical store Wine-on-Rosetta delivers {1 (write), addr} where Windows delivers {0, 0xffffffffffffffff}. Also measured: `hlt` -> trapno 13; `mov %cr3,%rbx` and `ud2` -> SIGILL trapno 6 (Windows: PRIV_INSTRUCTION for cr3, ILLEGAL_INSTRUCTION for ud2 -- the Endfield_FineWine project fixed this in ~40 lines of signal_x86_64.c on macOS 26.5/CrossOver 26.2; its second claim, that Rosetta faults on 0F 1F multi-byte NOPs, did not reproduce here). Whether the non-canonical classification is new in 26/27 or long-standing is unverified (no macOS 14/15 machine available).

7. CrossOver-only Rosetta hacks in 26.3's signal_x86_64.c (direct source read, unverified by second pass): CW Hack 24256 (mxcsr in every signal context is wrong under Rosetta; handler reads it with stmxcsr), CW Hack 24265 (M3 mxcsr restore thunk, see 1), CW Hack 23427 (emulate_xgetbv returns XCR0 = 0xe7 -- fpu/sse/avx/full AVX-512 -- when __builtin_available(macOS 15.0), else 0x07; "Arguably we should only claim AVX support if ROSETTA_ADVERTISE_AVX is set"), CW HACK 20186 (CET nop, Big Sur), runtime is_rosetta2 via sysctl.proc_translated, "faking success" for debug registers. None are upstream; none carry a changelog line.

8. Address space and memory manager -- unverified (single angle, not re-fetched): 8c437ef80c82 dynamic host address-space probe (9.17); 8ad60112690b "Hard code the host address space limit on macOS" (Brendan Shanks, merged 2026-07-16, Wine 11.14, MR !11402): the probe returns 0x7fffffff0000 but XNU's real limit is 0x7ffffe000000 on ARM64 (applies to Rosetta processes), so Wine 11.0 advertises 32 MB at the top that do not exist; bug 48291 comment 59 records a protector doing its syscall from a MEM_TOP_DOWN allocation. SystemBasicInformation's payload (virtual_get_system_info) is content-identical 8.0.1 -> 11.0 except a macOS hw.memsize branch (c6b2bccfe7b9, 2023-11-14). Rosetta memory workarounds: 1b310a5aba10 (8.11, misreported faults for lock cmpxchg8b break write-watch), a1627e3c40a7 (10.0-rc, W^X in write_process_memory; Rosetta can drop VM_PROT_WRITE), 8fd49c4d8e9c (10.5), b6e4530a19d6/7c88a9334f37/ea68c902ddbc (10.17-10.19, PROT_EXEC mapping), 49ea133b7b22 (9.15, 16K host page size for free-RAM accounting only). Upstream has zero commits mentioning Tahoe, macOS 26 or macOS 27 and zero mentioning Denuvo; the 11 "Rosetta" commits are all listed above.

9. Low weight, unverified: 1c349a9a600e (2025-12-12, in 11.0) 10 ms CPU tick on macOS; the May-2025 series (6b65ae4e9bab, 837358a52474, 700fe8134628, 4a5fade67395; 10.9) deriving CPU feature bits/XState layout from user shared data.

Upstream after wine-11.0 (2026-01-13) that CrossOver 26.x cannot contain unless backported: cbb9906d/b90cdc04 (11.5), e7439f1be35f (11.6), 12db5bd2eadc single-step to SIGTRAP (2026-05-11), 74144ade25c3 HW-breakpoint traps inside signal stack (2026-06-22), 8ad60112690b (11.14), 4ac0555e55c8 (2026-08-17), 67a26bddad05 (2026-08-24) -- listed by one angle, not re-verified; none changes sigsys_handler's macOS semantics.

## Rosetta in macOS 26 and 27

Apple statements (quoted by one angle; not re-fetched by the second pass, so treat wording as unverified but the URLs are real):
- "About the Rosetta translation environment" (https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment): Rosetta "will be available through macOS 27 -- as a general-purpose tool for Intel apps"; "Beyond this timeframe, we will keep a subset of Rosetta functionality aimed at supporting older unmaintained gaming titles"; "macOS 27 directly integrates support for Intel binary translation, without needing to install Rosetta. This enables support for Intel Linux binaries running in ARM virtual machines (VMs) as well as Intel Linux containers." Also (verified verbatim): "Rosetta translates all x86_64 instructions, including ones from the AVX and AVX2 instruction set, but it doesn't support the execution of AVX512 vector instructions." Apple documents nothing about exception delivery, SIGSYS, debug registers, CPUID or self-modifying code.
- Apple Developer News 2026-09-01 (https://developer.apple.com/news/?id=w5ngl9k2): macOS 27 is the final release supporting Rosetta for Intel-only apps; gaming-oriented functionality continues.
- macOS 27 (Golden Gate) Beta 8 release notes (https://developer.apple.com/documentation/macos-release-notes/macos-27-release-notes): apps previously set to "Open using Rosetta" now launch natively; "Any compatibility issues requiring Rosetta from the past should be re-assessed on macOS 27" (168097174); Rosetta is not automatically restored after upgrading to 27.0 (163213094); a beta-only `sudo game-test-tool enable` switches Intel games to "the new underlying system behavior" and disables Rosetta (166398727); Intel apps may appear erroneously as "Steam Game" in the unsupported list (177192993).
- macOS 26.4 notes (https://developer.apple.com/documentation/macos-release-notes/macos-26_4-release-notes): Rosetta "usage awareness" notifications at launch of translated apps, accelerated cadence during betas, MDM key allowRosettaUsageAwareness (169228455). macOS 26 notes: boot-arg nox86exec=1 crashes any Rosetta process at launch (136764433).
- macOS 26.5: CodeWeavers' 2026-05-18 blog says Diablo IV/Overwatch needed "both Wine changes in CrossOver 26.1 in addition to Rosetta changes in macOS 26.5" (https://www.codeweavers.com/blog/mjohnson/2026/5/18/finally-diablo-iv-and-overwatch-are-playable-with-crossover-261-macos-265); Apple's 26.5 notes carry no Rosetta entry. The underlying defect: a Rosetta 2 deadlock (thread parked on os_sync_wait_on_address inside libd3dshared) when Blizzard's anti-cheat reads its own translated x86_64 code pages, on M1-M5, macOS 15.7.4 and 26.2; Apple DTS acknowledged FB15880492/FB21763885/FB21838832 with no timeline (https://developer.apple.com/forums/thread/814383; https://github.com/MichaelLod/D4Mac). This is the 2026 precedent for a silent Rosetta change affecting protector-style code, and for a fix needing both sides.
- Independent: Eclectic Light (2026-06-10 post, comment 2026-07-07) notes Rosetta now lives in its own cryptex, /System/Volumes/Preboot/Cryptexes/Rosetta, on 27.

Measured on this host (2026-09-02; sw_vers macOS 27.0 build 26A5425a, a seed build): game-test-tool reports "Configured: disabled / Active: Game Test Mode inactive"; nvram boot-args not set; the Rosetta cryptex RestoreVersion.plist says IsSeed true, RestoreVersion 26.1.425.5.1; oahd binaries dated 2026-08-27, libRosettaRuntime dated 2026-08-08. Rosetta's CPUID here: vendor GenuineIntel, brand "VirtualApple @ 2.50GHz", leaf-1 EAX 0x000206C0, ECX 0x0298220f, EDX 0x0f8b8b15, no AVX/OSXSAVE/FMA/F16C, leaf-7 EBX/ECX/EDX zero; with ROSETTA_ADVERTISE_AVX=1: AVX, AVX2, BMI1/2, FMA, F16C, OSXSAVE on, AVX512F off (verified reproduction; no earlier baseline exists locally, so "unchanged from 14/15" is not established, though it matches the widely reported identity). SIGSYS delivery for a raw syscall 0x36 works (sig 12, trapno 133, err 0x36). Non-canonical stores are classified as #PF (trapno 14) with the address, `mov cr3` as SIGILL (see what_changed item 6).

Base rates for Rosetta regressions in betas/point releases (single angle, unverified): RPCS3 blocked macOS 14.0-14.2 for a mistranslation until 14.3; a divl/cwtd wrong-RDX bug (FB13503265) fixed in 14.4 beta; the macOS 26 beta forced libSystem linking and broke wine-preloader engines only during the beta (Sikarugir #130). Apple has fixed such things without documenting them.

Silence: no Apple document, CodeWeavers page or public report covers Denuvo, this title, or any SIGSYS/exception behaviour on macOS 27 or an M4 Max. CodeWeavers' support matrix tops out at Tahoe 26.0 and says "Beta or unreleased versions of macOS are not supported"; its 2026-07-31 ARM64 preview post says systems below 26.5 run "Intel Wine and Rosetta" while 26.5+ can use ARM64 Wine + FEX.

## What other Wine forks did about Denuvo

Apple GPTK (verified via the homebrew-apple formula, single angle): built on crossover-sources-22.1.1 with an embedded patch containing the original sigsys_handler variant, which pushes RIP+0xb as a return address on the user stack ("*rsp -= 1; **rsp = RIP+0xb") and jumps to __wine_syscall_dispatcher -- i.e. it touches the guest stack below rsp, unlike the upstream form. Gcenx/game-porting-toolkit mirrors it. The local /Users/mathias/Downloads/gptk4b2 contains only libd3dshared shims, no ntdll, so Apple's current handler could not be compared.

Whisky-App/wine is a CrossOver-derived tree (branch 7.7, last pushed 2025-03-03) whose local changes are CX-hack add/removals (e.g. "CW HACK 23560 - Diablo IV"); Sikarugir-App/wine is a plain wine-mirror fork; GitHub code search finds no "sigsys" in either org. Sikarugir issue #233 (open, June 2026) is an Age of Empires II DE Denuvo crash on Wine 10.0 whose reporter hypothesises that Wine's EtwRegisterTraceGuidsW stubs never invoke callbacks, leaving a Denuvo runtime pointer NULL -- unverified, concerns a June-2026 Denuvo build (P4G's last patch is v1.05, 2023-04-20), no patch exists. Issue #239 (new-wow64 + Rosetta syscall fault) is deleted (HTTP 410), no archive.

Proton/Linux lineage (verified for bug 48291, Wine 11.5 and c8c0a023ff; Proton commit details single-angle): Paul Gofman's seccomp-based emulation (wine-staging ntdll-Syscall_Emulation, 2020-07-14, "Detroit: Become Human"); Proton's sigsys_handler_rdr2 (ValveSoftware/wine b620abe54c, 2020-08-05) translating Windows 10 1809 SSNs (0x19 NtQueryInformationProcess, 0x36 NtQuerySystemInformation, 0xec NtGetContextThread, 0x55 NtCreateFile) to Wine's numbers -- showing exactly which calls protectors issue raw, matching our 0x36; Elizabeth Figura's Syscall User Dispatch (cbb9906d, Wine 11.5, closing bug 48291). All are routing fixes; none changes what NtQuerySystemInformation returns. Gofman's 2020 Yakuza: Like a Dragon patch ("an issue with newer Denuvo", Proton #4363) was never opened (attachment); the same day users ran the game with PROTON_USE_SECCOMP=1 alone, and the "warn:seh:virtual_unwind exception data not found" line was the ordinary no-unwind-info warning, not the defect; the only Gofman ntdll commit on proton_5.13 in that window fixes SMT flag reporting (count_bits popcount) -- attribution is inference. GE-Proton 10-11 (2025-08, Etaash-mathamsetty/wine-valve cd3efb2) stops wine.inf clobbering DigitalProductId ("may help with Denuvo being triggered when changing proton versions"), building on Gofman's 2021 CW-Bug-Id #19702 wineboot create_digitalproductid and Win10-style ProductId; the 26.3 tree has the same create_digitalproductid -- machine-identity class, unlikely ours since activation succeeds and the fault reproduces offline.

macOS community fixes for protectors on Rosetta (single angle): Endfield_FineWine (July 2026, M3, macOS 26.5, CrossOver 26.2) reclassifies `mov rbx,cr3` from ILLEGAL_INSTRUCTION to PRIV_INSTRUCTION and skips 0F 1F NOPs Rosetta allegedly faults on, ~40 lines in dlls/ntdll/unix/signal_x86_64.c, for ACE/VMProtect (the cr3 classification reproduces on our 27 beta; the NOP fault does not). D4Mac documents the Blizzard/Rosetta deadlock above. No fork anywhere carries a Denuvo-specific engine change beyond raw-syscall routing; there is nothing to import.

## Divergences from Windows, ranked by the sweep

- 1. The host: a seed Rosetta on an unreleased macOS 27 that nobody has run this title on. Every public success is macOS 14-15 with CrossOver 23.6-26.1; the 26.x engine is the same Wine 11.0 base and 26.3's core binaries are byte-identical between our copy and stock, so the one variable no report covers is the translator. Apple says 27 re-integrates Intel translation (cryptex-delivered, IsSeed here) and tells developers past Rosetta issues 'should be re-assessed on macOS 27'; 2026 precedent (Blizzard anti-cheat deadlock, fixed silently in 26.5) shows Rosetta changes that only protector-style code notices. Test legitimately: (a) run stock CrossOver 26.1 and 25.1.1 in fresh bottles on this same host -- if 26.1 also fails on 27 while it is reported working on 15, the host is implicated; if 26.1 works here, the delta is inside the 26.1->26.3 CX overlay (small tarball diff); (b) run the identical 26.3 bottle on a macOS 26.5 volume or VM; (c) differential run under CodeWeavers' ARM64 preview (FEX instead of Rosetta; needs 26.5+, which 27 satisfies) -- Steam may not launch there, per their limitations note.
- 2. Exception-record fidelity for the terminal fault (measured, verified): under Rosetta a store to a non-canonical address is delivered as a page fault with the address, so Wine hands the game's SEH handler EXCEPTION_ACCESS_VIOLATION {1 (write), <address>} where Windows would deliver {0, 0xffffffffffffffff} (#GP). segv_handler's mapping is identical in upstream and CrossOver 26.3 and has no non-canonical special case. Test: decode the faulting instruction's immediate address in our trace; if it is non-canonical, write a test program that raises the same store under __try and prints the record, compare with the Windows-documented values; a Windows-faithful engine fix is to treat trapno 14 with a non-canonical si_addr as TRAP_x86_PROTFLT. Caveat: the AV is probably the protector's reaction to an earlier failed check (it then fast-fails), so fixing the record may only change how it dies; and whether this classification is new in 27 is unverified.
- 3. Register/stack state across the 66 raw syscalls (partly verified). Denuvo's documented checks include integrity scans of its own SYSCALL/CPUID handlers and stashing values in 'unused' stack space (connorjaydunn analysis, verified quotes). Wine's SIGSYS path is a signal delivery plus dispatcher re-entry that Windows never performs; CrossOver adds CW Hack 24256 (mxcsr in signal contexts is wrong under Rosetta) and 24265 (M3 restores mxcsr from the sigcontext regardless; handler detours through a restore thunk) -- per-chip workarounds nobody has validated on M4 or 27. rcx/r11/results are already measured correct. Test: extend the raw-syscall test program to (a) fill a pattern below rsp (red zone and further) and verify it survives the syscall, (b) read mxcsr, x87 control word, and XMM/YMM contents before and after, (c) check rflags beyond r11, (d) single-step across the syscall (TF path changed in Wine 10.4); log direct mxcsr vs fpu.MxCsr inside the 66 events via a +seh-style trace.
- 4. XGETBV vs CPUID inconsistency (source read, unverified by second pass): CW Hack 23427 makes emulate_xgetbv answer XCR0 = 0xe7 (claims AVX and full AVX-512 state) on macOS 15+, while CPUID reports no OSXSAVE/AVX and Rosetta cannot execute AVX-512; Denuvo's documented surface includes XGETBV. On real hardware XGETBV with OSXSAVE=0 raises #UD. Present on macOS 15 too (where the game worked), so weak as a 27-only explanation, but it is a documented protector input and one probe away: execute xgetbv(0) in the test program under 26.3 and compare with the CPUID-implied answer.
- 5. Debug-register semantics (verified): NtSetContextThread with CONTEXT_DEBUG_REGISTERS is faked as success in CrossOver (STATUS_UNSUCCESSFUL upstream) and reads back all zeros, whereas Windows honours DR0-DR7. Identical on M1-M3 where the title worked, so low weight; cheap check: grep the +seh log for 'Setting debug registers is not supported under Rosetta, faking success' and confirm whether the protector sets/reads DRs at all (NtGet/SetContextThread via ntdll exports would not show in the SIGSYS trace).
- 6. TEB/GSBASE model (verified for upstream; local observation for CrossOver): CrossOver 26.3 did not adopt Wine 10.5's GSBASE swap (no _thread_set_tsd_base, no 0x3000003 syscall bytes), so PE code reading TEB fields through %gs other than Self/TLS/PEB (e.g. gs:8 StackBase, gs:0x10 StackLimit) sees macOS TSD contents; the check_invalid_gsbase fix-up (10.14+, present in 26.x, absent in 25.x) silently resumes %gs-prefixed faults instead of raising them. Standing on 14/15 as well. Test: a program that reads NtCurrentTeb()->StackBase/StackLimit through gs:8/gs:0x10 and compares with the values from the TEB pointer; and count 'fixing up' TRACE lines in a +seh run of the game.
- 7. Advertised vs real top of address space (single angle, unverified): Wine 11.0 reports HighestUserAddress = 0x7fffffff0000 while XNU's ARM64 limit for Rosetta processes is 0x7ffffe000000 (fixed upstream only in 11.14, after 26.3); protectors are documented to use MEM_TOP_DOWN allocations. Same on 14/15 unless the kernel constant changed. Test: VirtualAlloc(MEM_TOP_DOWN) and NtAllocateVirtualMemory near the advertised top under 26.3 on this host and compare the returned address and any failures with SystemBasicInformation's HighestUserAddress.
- 8. Rosetta page-permission semantics (unverified): W^X, write-watch and PROT_EXEC mapping are patched around case by case (1b310a5aba10, a1627e3c40a7, 10.17-10.19 commits; CrossOver's own 'HACK: exec fault on executable page' / 'HACK: write fault on a w|x page' strings), so a self-modifying or page-guarding protector is exposed to Rosetta, not Windows, behaviour. Test: a program that writes to, then executes, then re-protects a page with VirtualProtect/PAGE_GUARD and checks the exception records against Windows; run with +virtual to see whether the HACK paths fire during the game's first 13 s.
- 9. Unwind path (verified): the hang branch is Wine 11.0-11.5's unwind_builtin_dll falling into host libunwind (macOS 27's libSystem) for a garbage return address; upstream 11.6 removed it. This explains hang-vs-fast-fail, not the fault; not a cause. Timing (10 ms CPU tick, 1c349a9a600e) is listed for completeness only.

## What this host actually answers (fidelity probe, 2026-09-02)

`fidelity-probe.c` (llvm-mingw, 95 KB), run with the 26.3 engine in the Steam
bottle, raises each fault a protector can raise and prints the record wine
delivers next to the Windows-documented one:

```
store to FFFFFFF22D819090 (canonical)  -> fault code 0xc0000005 info[2] {0x1, 0xfffffff22d819090}   Windows: c0000005 {1, FFFFFFF22D819090}
store to 8000000000000000 (non-canonical) -> fault code 0xc0000005 info[2] {0x1, 0x8000000000000000}   Windows: c0000005 {0, FFFFFFFFFFFFFFFF}
store to NULL+0x48                     -> fault code 0xc0000005 info[2] {0x1, 0x48}   Windows: c0000005 {1, 48}
mov cr3, rbx                           -> fault code 0xc000001d info[0] {0x0, 0x0}   Windows: c0000096 PRIV_INSTRUCTION {0,0}
ud2                                    -> fault code 0xc000001d info[0] {0x0, 0x0}   Windows: c000001d ILLEGAL_INSTRUCTION {0,0}
hlt                                    -> fault code 0xc0000096 info[0] {0x0, 0x0}   Windows: c0000096 PRIV_INSTRUCTION {0,0}
int3                                   -> fault code 0x80000003 info[1] {0x0, 0x0}   Windows: 80000003 BREAKPOINT {0,0}
TF single-step                         -> fault code 0x80000004 info[0] {0x0, 0x0}   Windows: 80000004 SINGLE_STEP {0,0}
   bytes changed below rsp (offset from rsp: value): [-8]=5e [-7]=18 [-6]=00 [-5]=40 [-4]=01 [-3]=00 [-2]=00 [-1]=00
   qword at rsp-8 = 0x14000185e (main=0000000140001440)
raw syscall 0x36: status 0x0; bytes below rsp changed: 8/512 (Windows: 0); mxcsr 0x1f80->0x1f80 x87cw 0x37f->0x37f rflags 0x287->0x287 xmm7 preserved   Windows: all preserved
CPUID brand 'VirtualApple @ 2.50GHz' OSXSAVE 1 AVX 1 AVX2 1 AVX512F 0 | xgetbv(0): ok XCR0 0x7   Windows: XCR0 bits agree with CPUID; #UD if OSXSAVE=0
DR write bp: SetThreadContext ok (err 0); write to victim -> no fault code 0x00000000; GetThreadContext ok Dr0 0 Dr7 0   Windows: Set ok, 80000004 fires, Dr0/Dr7 read back
TEB via gs: gs:8 0x110000 gs:10 0x12000 gs:30 0x7ffc0000 gs:60 0x7ffd0000 | TEB 000000007FFC0000 StackBase 0000000000110000 StackLimit 0000000000012000 PEB 000000007FFD0000   Windows: gs:8==StackBase gs:10==StackLimit gs:30==TEB gs:60==PEB -> MATCH
MEM_TOP_DOWN -> 00007FF001DA0000 (max app addr 00007FFFFFFEFFFF); fixed alloc at top -> FAILED (err 8)   Windows: top-down lands just under max; fixed at top succeeds
write to PAGE_GUARD page               -> fault code 0x80000001 info[2] {0x1, 0x8e0000}   Windows: 80000001 GUARD_PAGE {1, addr}
second write (guard cleared)           -> no fault code 0x00000000 info[0] {0x0, 0x0}   Windows: no fault
RWX page write-then-execute -> 42   Windows: 42
RWX page modify-then-execute -> 43   Windows: 43
write to PAGE_EXECUTE_READ page        -> fault code 0xc0000005 info[2] {0x1, 0x8f0000}   Windows: c0000005 {1, addr}
read from PAGE_NOACCESS page           -> fault code 0xc0000005 info[2] {0x0, 0x8e0000}   Windows: c0000005 {0, addr}
execute PAGE_NOACCESS page             -> fault code 0xc0000005 info[2] {0x8, 0x8e0000}   Windows: c0000005 {8, addr} (DEP)
```

Read against the ranked list above:

- The protector's own store (`FFFFFFF22D819090`, canonical kernel half) is
  reported exactly as Windows would: `c0000005 {1, address}`. Divergence 2 does
  not apply to this title's fault; the non-canonical case does diverge
  (`{1, addr}` here, `{0, -1}` on Windows) but nothing here raises it.
- **A raw `syscall` writes eight bytes below the caller's stack pointer**: the
  return address, `RIP+0xb`. That is the dispatcher's way home,
  `pushq 0x70(%rcx); ret` at the end of `__wine_syscall_dispatcher` in
  `dlls/ntdll/unix/signal_x86_64.c` -- the same epilogue in CrossOver 26.3 and
  in upstream Wine 11.0, and old: every syscall through the dispatcher returns
  this way, and GPTK's original SIGSYS handler pushed the address explicitly.
  Windows' `syscall`/`sysret` never touch user memory. A Windows-fidelity
  defect, present on every version this title ran on elsewhere, so not the
  differentiator -- unless the protector keeps state below `rsp` across its
  66 raw calls, which the community's successes on the same engines argue
  against.
- `mov cr3` is `ILLEGAL_INSTRUCTION` here where Windows says
  `PRIV_INSTRUCTION` (the Endfield_FineWine report reproduces on macOS 27);
  `hlt`, `ud2`, `int3`, single-step, guard pages, W^X, DEP and NOACCESS all
  match Windows.
- Debug registers: `SetThreadContext` succeeds, the hardware breakpoint never
  fires, `DR0`/`DR7` read back zero. Rosetta has no debug registers; CrossOver
  fakes success (upstream fails the call). Same on every Apple silicon Mac
  where the title worked.
- `XGETBV` answers `XCR0 = 7`, consistent with CPUID (AVX on, AVX-512 off);
  CW Hack 23427's `0xe7` branch is not what this host returns. Excluded.
- The TEB through `%gs` (`gs:8`, `gs:10`, `gs:30`, `gs:60`) matches the TEB
  pointer's fields. Excluded on this host.
- `MEM_TOP_DOWN` lands 256 MB under the advertised top and a fixed allocation
  at the top fails; Wine 11.14 fixed the advertised limit after 26.3. A
  standing divergence, not new.
- Registers, `mxcsr`, x87 control word, flags and XMM survive the raw syscall.

Net: the probe finds real divergences, and none of them is unique to this
host; the ones that could matter to a protector (stack write below `rsp`,
faked debug registers, `cr3`) were already there on the Macs where the game
ran. What no probe here can compare is Rosetta itself, because there is no
macOS 14/15 baseline on this machine.

## The 26.1 -> 26.3 source diff, done -- 2026-09-02

`crossover-sources-26.1.0.tar.gz` (149 051 164 bytes, media.codeweavers.com,
last modified 2026-04-09) unpacked to `~/Development/sources-26.1` beside the
26.3 tree. Both carry Wine 11.0. The whole Wine delta between them is twelve
files, none in the engine core:

| File | Change |
| --- | --- |
| `dlls/advapi32/security.c` | CW HACK 27245: the Battle.net ACL/owner rewrite now also applies to `EpicGamesLauncher.exe` |
| `dlls/kernelbase/file.c`, `kernelbase.spec`, `kernel32.spec`, `include/fileapi.h` | new `FindNextFileNameW` stub export (GOG Galaxy) |
| `dlls/msctf/msctf.c`, `threadmgr.c`, `msctf_internal.h`, `include/msctf.idl`, new `include/ctffunc.idl` | text-services thread manager rework, `TF_GetThreadMgr` moved, an `ITfFunctionProvider` stub |
| `dlls/winebus.sys/Makefile.in`, `include/Makefile.in` | build lists |

No change under `dlls/ntdll`, `server`, `loader`, `dlls/win32u`,
`dlls/winemac.drv` or `dlls/kernelbase` beyond the stub. Outside Wine the
tarballs differ only in GStreamer/GLib subproject wrap locks. Persona 4
Golden does load `msctf.dll`, as any window-owning process does through
`imm32`, but the rework touches thread-manager creation and a function-provider
stub, nothing the protector's code path exercises; it imports no
`FindNextFileNameW`, and the ACL hack keys on the Epic executable's name. So the engine that the community's
26.x successes ran on and the engine that fails here are, in every path a
protector exercises, the same source. Together with stock 26.3 failing in its
own bottle, that leaves the host: macOS 27's seed Rosetta on this M4 Max.

One honesty note on the "26.x works elsewhere" premise. For this title the
26.x successes are MacGamingDB entries (26.0 D3DMetal on an M1 Air, "crash
in every loading section" but playing), unverified; CodeWeavers' own 26.1.0
rating for it is "Limited Functionality" from one vote with no reason given,
which could be this very failure. For Persona 5 Royal, same protector family,
MacGamingDB has 26.0/26.1 DXMT "Excellent" on an M1 Pro and an M4 Pro, also
unverified, and a Steam thread where it crashed on 26.0 until the host was
upgraded to Sequoia. The macOS version is the variable those reports keep
pointing at.

The one translator differential available on this machine without another
Mac: CodeWeavers' ARM64 preview (2026-07-31) runs ARM64 Wine on FEX instead
of Rosetta on macOS 26.5 and later. If the protector's fault moves under FEX,
Rosetta is implicated; if it stays, it is not Rosetta. Steam may not launch
in that preview, per their note, and there is no D3DMetal, so the value is
only whether the 12 s fault moves.

## Recommended actions from the sweep

- Bisect the engine on this host first: install stock CrossOver 26.1.0 (and 25.1.1, Wine 10.0, which predates the explicit 0x36 syscall ids and the gsbase fix-ups) each in a fresh bottle -- bottles are engine-locked -- and run Persona 4 Golden. 26.1 is the newest version with public success reports (macOS 14-15). Same failure on 26.1 and 25.1.1 here points at macOS 27's Rosetta; success on 26.1 narrows it to the 26.1->26.3 CodeWeavers delta.
- Bisect the host second: boot the same 26.3 bottle on a macOS 26.5 volume (external drive or a macOS VM with Rosetta) on this M4 Max. If it reaches gameplay there, file with Apple (Feedback Assistant, macOS 27 beta, Rosetta) with the two probe programs and the SIGSYS/AV trace, and tell CodeWeavers.
- Source diff worth doing now, cheaply: crossover-sources-26.1.0.tar.gz (149,051,164 bytes) against the already-unpacked 26.3.0 (149,054,023 bytes; /Users/mathias/Development/sources) restricted to dlls/ntdll/unix/signal_x86_64.c, dlls/ntdll/signal_x86_64.c, dlls/ntdll/unix/virtual.c, dlls/ntdll/unix/system.c, dlls/ntdll/unix/loader.c, server/mach.c, server/ptrace.c, loader/. Expect a small delta; that is the whole engine difference between the last reported-working version and ours. The 23.7.1 (134,921,040 bytes) vs 26.3.0 diff is large and its answer is already known (SIGSYS handler present in both, GSBASE model unchanged, syscall ids changed in 11.0); download 23.5.0 (134,888,629 bytes) only if you want to settle whether 23.5 carried the SIGSYS handler -- low priority.
- Extend the existing raw-syscall test program into a Windows-fidelity probe run under 26.3 on this host (and later on 26.5/another Mac): (1) non-canonical store under __try, print ExceptionInformation; (2) `mov rbx,cr3` and ud2 codes; (3) stack pattern below rsp and mxcsr/x87 CW/XMM/YMM/rflags before and after a raw syscall 0x36, plus a single-step across it; (4) xgetbv(0) vs CPUID OSXSAVE/AVX bits; (5) DR7 set-then-read via Get/SetThreadContext and whether a HW breakpoint fires; (6) TEB StackBase/StackLimit via gs:8/gs:0x10 vs the TEB pointer; (7) MEM_TOP_DOWN allocation at the advertised top vs SystemBasicInformation.HighestUserAddress; (8) VirtualProtect w|x, PAGE_GUARD and write-watch records. Compare against Windows-documented values, not against the engine's own answers. Any mismatch is a legitimate engine/host fidelity defect to report; it is not evidence of what the protector checks.
- Instrument, do not patch: run the game once with WINEDEBUG=+seh,+virtual and grep for 'faking success' (debug registers), 'fixing up' (gsbase), 'HACK: exec fault' / 'HACK: write fault', 'ignoring trap in syscall', and count them before the 12.5 s fault. Log direct mxcsr vs sigcontext mxcsr in the 66 SIGSYS events if a local build is available.
- Optional differential on translator only: CodeWeavers' ARM64 preview (2026-07-31; universal build, FEX on macOS 26.5+, so on 27 it will use FEX) with a fresh bottle. Steam/launchers may not work in that preview and there is no D3DMetal; the value is purely whether the protector fault moves.
- Report to CodeWeavers (support ticket, not the public forum, since the forum is Cloudflare-gated for tooling): Persona 4 Golden (Steam 1113000, Denuvo) on M4 Max, macOS 27.0 beta 26A5425a (Rosetta cryptex 26.1.425.5.1 seed), stock CrossOver 26.3.0.39832 fresh bottle; loading screen reached; 66 SIGSYS dispatches of NtQuerySystemInformation(SystemBasicInformation) between 2.8 and 12.1 s all handled; access violation from the protector's .arch section at 12.5-12.8 s -> c0000409 fast-fail, or a hang when dispatch reaches libunwind_virtual_unwind; reproduces offline; graphics backend, AVX advertising, overlay and codecs excluded; ask whether FB15880492/FB21763885/FB21838832 or their 26.5 Rosetta work overlap, whether 26.3 has been run on any 27 seed, and note the measured non-canonical-store classification (trapno 14 with address vs Windows #GP/-1). Also report the outcome of the 26.1/25.1.1 and 26.5 bisections when you have them; that is the datum they lack.
- Housekeeping for the control: for any further 'stock fails identically' claim use a freshly downloaded CrossOver 26.3 (the /Applications copy has our restored winegstreamer pair and lib64/apple_gptk_4 beside it), and keep msync and the GPTK toggle identical across runs (msync is a Mach-semaphore performance option with no documented Denuvo relation).

## Sources

- <https://gitlab.winehq.org/wine/wine/-/merge_requests/6777 (SIGSYS handler MR; read via GitLab API)>
- <https://github.com/wine-mirror/wine/commit/a88a0736ea1197b9856dc7322b1c5b334f0a5de0>
- <https://github.com/wine-mirror/wine/commit/d2085b768b65>
- <https://gitlab.winehq.org/wine/wine/-/merge_requests/6866 (GSBASE swap; read via GitLab API)>
- <https://github.com/wine-mirror/wine/commit/3a16aabbf55be5e0416c53b498ed1d085b8d410d>
- <https://github.com/wine-mirror/wine/commit/93fde56b494151f5e4bdfc560f930867bda52514>
- <https://github.com/wine-mirror/wine/commit/39655dade3c802557754439451279c5b59b31ce8>
- <https://github.com/wine-mirror/wine/commit/c8c0a023ff5dbb9adec1621b4558a88306da7709>
- <https://github.com/wine-mirror/wine/commit/654c03d1317f4b22294109fee7ded2dbbe39645e>
- <https://github.com/wine-mirror/wine/commit/e7439f1be35f005cad6de8d84f19d2ab8f6e85b8>
- <https://gitlab.winehq.org/wine/wine/-/merge_requests/7064 (check_invalid_gsbase; unverified)>
- <https://gitlab.winehq.org/wine/wine/-/merge_requests/11402 (macOS host address limit; unverified)>
- <https://github.com/wine-mirror/wine/commit/1b310a5aba10 (unverified)>
- <https://github.com/wine-mirror/wine/commit/1c349a9a600e (unverified)>
- <https://github.com/wine-mirror/wine/commits/master/dlls/ntdll/unix/signal_x86_64.c>
- <https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/unix/signal_x86_64.c>
- <https://bugs.winehq.org/show_bug.cgi?id=48291>
- <https://bugs.winehq.org/show_bug.cgi?id=54367>
- <https://bugs.winehq.org/show_bug.cgi?id=45083 (unverified)>
- <https://www.codeweavers.com/crossover/changelog>
- <https://www.codeweavers.com/crossover (system requirements; unverified)>
- <https://www.codeweavers.com/blog/mjohnson/2023/9/27/crossover-235-is-a-real-game-changer (via Wayback; unverified)>
- <https://www.codeweavers.com/blog/mjohnson/2026/5/18/finally-diablo-iv-and-overwatch-are-playable-with-crossover-261-macos-265 (via Wayback; unverified)>
- <https://www.codeweavers.com/blog/mjohnson/2026/6/11/whats-in-and-whats-out-for-crossover-27 (unverified)>
- <https://www.codeweavers.com/blog/mjohnson/2026/7/31/crossover-preview-the-right-to-bear-arm64-on-mac (unverified)>
- <https://www.codeweavers.com/compatibility/crossover/persona-4-golden (Wayback 2026-05-03; unverified)>
- <https://support.codeweavers.com/advanced-settings-in-crossover-235 (msync; unverified)>
- <https://media.codeweavers.com/pub/crossover/source/crossover-sources-26.3.0.tar.gz (HEAD sizes for 23.0.0..26.3.0; unverified)>
- <https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment>
- <https://developer.apple.com/documentation/macos-release-notes/macos-27-release-notes (Beta 8; unverified)>
- <https://developer.apple.com/documentation/macos-release-notes/macos-26_4-release-notes (unverified)>
- <https://developer.apple.com/documentation/macos-release-notes/macos-26-release-notes (unverified)>
- <https://developer.apple.com/news/?id=w5ngl9k2 (unverified)>
- <https://developer.apple.com/forums/thread/814383 (Rosetta deadlock; unverified)>
- <https://developer.apple.com/forums/thread/769486 (BMI/F16C, forum user statement)>
- <https://developer.apple.com/forums/thread/743838 (Rosetta divl/cwtd bug; unverified)>
- <https://github.com/apple/homebrew-apple/blob/master/Formula/game-porting-toolkit.rb (unverified)>
- <https://github.com/MichaelLod/D4Mac (unverified)>
- <https://github.com/stoicswe/Endfield_FineWine (unverified)>
- <https://github.com/Sikarugir-App/Sikarugir/issues/233 (unverified)>
- <https://github.com/Sikarugir-App/Sikarugir/issues/130 (unverified)>
- <https://github.com/ValveSoftware/Proton/issues/4363 (unverified)>
- <https://github.com/Etaash-mathamsetty/wine-valve/commit/cd3efb2ade393776b40a5731eb8a81dec46e8b30 (unverified)>
- <https://github.com/RPCS3/rpcs3/pull/15237 (unverified)>
- <https://connorjaydunn.github.io/blog/posts/denuvo-analysis/>
- <https://bugzilla.mozilla.org/show_bug.cgi?id=1493342>
- <https://eclecticlight.co/2026/06/10/crossing-the-golden-gate-intel-support-and-an-update-to-systhist/ (unverified)>
- <Local: /Applications/CrossOver.app/Contents/SharedSupport/CrossOver (ntdll.so, wineserver, wineloader, changelog.html, README, Info.plist)>
- <Local: /Users/mathias/Applications/Crossover_MGVF.app/Contents/SharedSupport/CrossOver>
- <Local: /Users/mathias/Development/crossover-sources-26.3.0.tar.gz and /Users/mathias/Development/sources/wine/dlls/ntdll/unix/signal_x86_64.c>
- <Local probes: /private/tmp/claude-501/-Users-mathias-Development-Vp9/97fd9a0d-30a4-4bf8-960d-c2ef8a7a931d/scratchpad/fault.c and cpuid.c (run on macOS 27.0 26A5425a, M4 Max, 2026-09-02)>
