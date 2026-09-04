# Persona 4 Golden — what the rest of the world reports

Internal notes, companion to [persona-4-golden.md](persona-4-golden.md).
Compiled 2026-09-02 from six parallel web sweeps (ProtonDB, AppleGamingWiki,
CodeWeavers, Whisky, GitHub issues, Steam and Reddit threads); 61 claims were
collected and the three most load-bearing of each sweep were re-fetched and
checked against their source. Claims marked *unverified* below were read from
the source once but not re-checked; nothing here was invented, and where the
reports are silent that is said.

The question was the one the user asked: other Denuvo titles do run on Apple
silicon, so what do they have that we do not, and what happened to this title
elsewhere.

## Persona 4 Golden elsewhere

LINUX / PROTON (x86): the same Denuvo build runs at scale.
- ProtonDB summary for app 1113000 (verified 2026-09-02): tier Gold, 487 reports, score 0.72, confidence strong, best-reported and trending Platinum. https://www.protondb.com/api/v1/reports/summaries/1113000.json
- Valve's Deck compatibility endpoint (verified): resolved_category 3 = Verified; the only informational note is "first-time setup requires an active internet connection". https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=1113000
- The 40 newest ProtonDB reports (verified, with correction): late 2025 to Aug 2026, mostly Proton 10.0-3 (also 9.0-4, 8.0-5, 10.0-4, Experimental, GE-Proton10-34, Proton-CachyOS, Proton-EM-10.0-30). Large majority: runs with no tinkering. Dominant complaint is fullscreen/alt-tab (~14 of 40). Smaller theme is cutscenes: one user needs PROTON_USE_WINED3D=1 for FMVs, one Lutris user had a black screen after the logos fixed with winetricks (vcrun2017, d3dcompiler_43, xact, quartz, d3dx11_43, wmp11) plus K-Lite, one Intel UHD 620 user had crackling cutscene audio. Two self-described pirates: one's pirated copy did not launch while the bought copy worked; the other's pirated copy "crashes instantly after company logos" on GE-Proton until switching to Proton-EM-10.0-30. https://www.protondb.com/app/1113000
- Proton issue #3982 (thread-level summary unverified; individual comments verified where noted): the 2020-2022 "logos then black screen/crash" was the WMV/ASF intro video (quartz/MF), fixed with wmp9/quartz/devenum/LAV Filters/GE-Proton; after Atlus's 2023-01-19 64-bit update the videos are VP9 and GloriousEggroll reported no protonfixes needed (2023-02-17). https://github.com/ValveSoftware/Proton/issues/3982
- Unverified but specific: on the 2023 build under Proton Experimental (2023-02-17), an "Application has crashed" after the logos was traced to wine's stub d3dx11_43.dll (unimplemented D3DX11CreateThreadPump) because the Steamworks redist failed to install into the prefix; copying the native ~270 KB d3dx11_43.dll fixed the launch. https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-1435071521
- Denuvo failures on Linux are visible, not silent. Verified: GAumala (2021-01-08, 100 hours on Linux) once got a popup like "failed to check Steam ID", then a generic SEGA support popup, and was locked out for a few days; "nothing to do with my DLLs". https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-756887898 . Verified with correction: nolbap (Proton 7.0-3, 2022-08-03) got "timed out" and a redirect to a "couldn't verify my purchase" page, after which every launch passed the Atlus/P-Studio logos and died with an "Application has crashed" box; spiffeeroo's 2022-08-05 reply attributes the validation page to Denuvo's 5-machines-per-24h limit (each Proton switch counts), but explicitly attributes the post-logo crash to a stale prefix after switching GE-Proton to Steam Proton (crashing as the intro video starts; fix: delete compatdata/1113000). https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-1204252450
- Unverified: a Dec 2022 Steam Deck thread had one poster blame "the anti piracy DRM they added to an update" for Linux breakage; others ran it untouched, and the OP resolved the post-logo crash with GE-Proton 7-42 plus waiting ~10 minutes on first launch. https://steamcommunity.com/app/1113000/discussions/0/3716062978734696491/
- Unverified: P4G CEP Steam Deck troubleshooting page says "Sorry, something went wrong" is Denuvo's temporary activation limit, that changing Proton version or modifying the prefix makes Denuvo think it is a new machine, and the only fix is waiting 24-48 h. https://p4g-deck.cep.one/appendix/troubleshooting

APPLE SILICON: the community reaches gameplay under CrossOver and Whisky; the known failure is mid-game.
- AppleGamingWiki (verified; page last edited 2025-08-04): CrossOver "Runs" with note "Generally runs well but crashes when entering a dungeon. (GPTK 2, MacBook Pro M2 Pro)"; Wine "Unplayable - Does not boot." (no device, date or reporter on either row); Parallels "Runs"; Windows 10 ARM "Playable" under VMware Fusion. Revision history (verified): the CrossOver note replaced a 2023-11-22 note "Runs perfect using crossover 23.6 and DXVK and high res (m2 MacBook Air 8gb ram)" in revision 9200 of 2024-06-17, which also set the Wine row to Unplayable. https://www.applegamingwiki.com/wiki/Persona_4_Golden
- Whisky game-support entry (verified; commits 2024-11-01, repo archived 2026-03-30): status Garbage, installs Yes, opens Yes; sole warning is that loading while in a dungeon crashes and loading a dungeon via "continue" loses the quick save. https://github.com/Whisky-App/whisky-book/blob/main/src/game-support/persona-4-golden.md and https://docs.getwhisky.app/game-support/persona-4-golden.html
- Whisky issue #1045 (verified): opened 2024-06-27 (Whisky 2.3.2, macOS 14.5, Wine 7.7.0, DXVK on; hardware not stated); reporter got into the game but it crashed on every new-area load; Parallels and CrossOver "hadn't worked" for him (no detail). Org member hahayupgit, 2024-09-29: P4G "does not work through Whisky, or any version of Wine as far as I'm aware"; after extensive testing "the issue stems from dungeons" - entering with party members crashes, entering alone does not. https://github.com/Whisky-App/Whisky/issues/1045
- MacGamingDB (unverified): three CrossOver reports, all Barely Playable: v25.0 DXVK M3 Pro "crashes when you enter the dungeons and the music is choppy"; v25.0.1 DXVK M4 Air "Crashes randomly"; v26.0 D3DMetal M1 Air 8 GB, 60 fps High, "very very high probability of crash in every loading section", recommends Ryujinx. A VMware Fusion 25H2 report on M3 is Excellent. https://macgamingdb.app/games/1113000
- CodeWeavers compat page (verified with correction, Wayback 2026-05-03): Mac rating "Limited Functionality" (3/5), Last Tested 26.1.0 with a single vote, Linux unrated; per-version breakdown has 16 entries from 20.0.4 to 26.1.0 (ratings 2 to 5 stars), no reason given. Unverified: earlier captures show "Installs, Will Not Run" at 20.0.4 (2021), "Limited Functionality" at 23.7.1 and 25.0.0, and a tester screenshot "macOS 14 (Sonoma) CrossOver 24.0.1 TV entrance". http://web.archive.org/web/20260503075610/https://www.codeweavers.com/compatibility/crossover/persona-4-golden
- Proton issue #9354 (unverified, 2025-12-31): a CrossOver user on an M1 Pro says "Other games work fine (Persona 3 Reload, Persona 4 Golden)" while P5R shows the loading logo and closes with no error. https://github.com/ValveSoftware/Proton/issues/9354
- Parallels ARM Windows 11 on M1 Pro/M2 Pro (unverified, 2023-10 to 2025-06): starts, menus fine, no 3D objects/textures after loading a save; same setup works in VMware Fusion. https://forum.parallels.com/threads/persona-4-golden-missing-objects-textures-on-m1-pro.361817/
- applesilicongames.com (unverified, undated): one "not playable via CrossOver/Steam" on Mac mini M1 with no symptom; playable at 15 fps in Parallels on M1 Air. https://applesilicongames.com/games/WuhKUdxZpsvWEYmwa9i3mB/persona-4-golden

SILENCE: no source in any of the six angles describes P4G exiting 12-14 s into the first loading screen, an access violation from .arch, rax=0x80000002, c0000409, garry.sgaas.net, or the "VirtualApple" brand string. The CodeWeavers forum thread "Persona 4 Golden crash" (msg=328802; search snippets say May 2025, Windows 10 bottle, D3DMetal, crash after the loading screen with GStreamer-CRITICAL lines) could not be fetched by any angle because of a Cloudflare challenge, so its content is not established.

## Denuvo on Apple silicon

WHAT WORKS: CrossOver, and Windows-on-ARM VMs.
- CodeWeavers declared Denuvo playable in CrossOver 23.5 (verified, 2023-09-27): blog says "Denuvo games are now playable", footnote "macOS Sonoma is required for D3DMetal option and Denuvo games"; changelog 23.5.0 says "Denuvo games run on Sonoma." Neither document says what changed or why Sonoma is needed. http://web.archive.org/web/20260807231143/https://www.codeweavers.com/blog/mjohnson/2023/9/27/crossover-235-is-a-real-game-changer
- Denuvo titles reported running on CrossOver on Apple silicon (unverified unless noted): Persona 5 Royal "Perfect" on CrossOver 24.0.5 (AGW 2024-12-22, M3 Pro, macOS 15.2) https://www.applegamingwiki.com/wiki/Persona_5_Royal ; Persona 3 Reload "Perfect" on both CrossOver and Whisky https://www.applegamingwiki.com/wiki/Persona_3_Reload ; Metaphor: ReFantazio "Runs Well" (CodeWeavers, last tested 25.0.1) https://www.codeweavers.com/compatibility/crossover/metaphor-refantazio ; Tekken 8 "Perfect" on CrossOver 24.0.4 but "Unplayable - Stuck on a black screen" via Wine 11.3/Heroic (2026-02-22) https://www.applegamingwiki.com/wiki/Tekken_8 ; Hogwarts Legacy "Perfect" on CrossOver but "Unplayable - DRM protection error" in Parallels (same AGW angle, unverified).
- Inside Windows-on-ARM VMs (Parallels, VMware Fusion) P4G and P5R activate and run (AGW verifications 2024-11-08 and 2025-08-03, verified page; MacGamingDB VMware "Excellent", unverified). So the Mac hardware itself is not refused; only the Rosetta-plus-Wine path is in question. https://www.applegamingwiki.com/wiki/Persona_4_Golden

WHAT DOES NOT WORK: GPTK/upstream Wine paths, and reasons reported.
- No GPTK/Whisky Denuvo support is claimed: AGW's GPTK page says DRM like Denuvo is "long known to be incompatible with wine" and the only recourse is to wait for the DRM to be removed; a Whisky maintainer wrote "This game has Denuvo, so I can't promise I can properly provide support for it" (Watch Dogs 2, 2023-07-12). Unverified. https://www.applegamingwiki.com/wiki/Game_Porting_Toolkit
- P5R history (unverified): Isaac Marovitz (CodeWeavers forum, 2022-10-31) attributed P5R not opening at all on pre-23.5 CrossOver to "Lack of Denuvo support" plus transacted-file Win32 APIs; Trey Boyer (2023-02-05) added that on Apple silicon it "supposedly requires AVX support on the CPU, which is explicitly not supported by Rosetta 2", a problem shared by other SEGA titles. http://web.archive.org/web/20240205152101/https://www.codeweavers.com/compatibility/crossover/forum/persona-5-royal?msg=270341
- Engine-side mechanisms named in public reports (all unverified):
  (a) Rosetta debug registers: wine commit 93fde56b49 (Brendan Shanks, 2023-02-08, WineHQ bug 54367) fakes zero DR0-DR7 and fails SetThreadContext with CONTEXT_DEBUG_REGISTERS under Rosetta; the log line "thread_get_state failed on Apple Silicon - faking zero debug registers" appears in Whisky crash reports for RE2/RE3 (#270) and Dying Light 2 (#851), Denuvo-shipping titles, though those reports do not name Denuvo. https://github.com/wine-mirror/wine/commit/93fde56b49
  (b) Raw syscalls under Rosetta: Sikarugir issue #233 (2026-06-26) traces a deterministic access violation inside Denuvo integrity code in Age of Empires II: DE to a raw "syscall" instruction (SSN 0x18, NtAllocateVirtualMemory) that Wine's SIGSYS-based direct-syscall dispatch faults on under Rosetta, returning c0000005 so the protector dereferences a NULL allocation; reproduced on stock WhiskyWine; began with the 2026-06-02 update carrying a newer Denuvo. https://github.com/Sikarugir-App/Sikarugir/issues/233
  (c) x86 fidelity in other emulators: FEX added SGDT (2022-11, Team Sonic Racing), SIDT/LSL (2025-02, "used by recent Denuvo"), a CPUID option hiding hybrid cores (2026-02, "Required for Denuvo"), and has an open issue on inline self-modifying code for Denuvo. https://github.com/FEX-Emu/FEX/pull/4377 . box64: a Denuvo MGSV build loads a few seconds then dies with unhandled c0000409; maintainer says it needs BOX64_DYNAREC_SAFEFLAGS=2 (stricter flags emulation), Windows syscall emulation and a 48-bit address space. https://github.com/ptitSeb/box64/issues/3659
  (d) On x86 Linux, Yakuza: Like a Dragon (Nov 2020) raised an illegal-instruction exception inside the exe plus "virtual_unwind exception data not found"; Paul Gofman called it "an issue with newer Denuvo" and shipped a wine patch; playable in Proton 5.13-3. https://github.com/ValveSoftware/Proton/issues/4363
- Vendor statements (verified): Irdeto's product page says Denuvo supports Windows games under Proton, runs entirely in user mode, and "Detects virtualized and emulated execution environments" (also lists "Hypervisor detection"). https://irdeto.com/video-games/denuvo-anti-cheat/anti-tamper . Unverified: Irdeto blog 2026-08-27 says Proton-version switching previously created activation edge cases and Denuvo "has since addressed this behavior"; nothing about Wine on macOS, VMs or CPU identity. https://irdeto.com/blog/denuvo-anti-piracy-proton-linux-steam-deck
- Activation limits (verified): kisak-valve, Proton issue #7397 (Persona 3 Portable, 2024-01-10): 5 activation tickets per day; each Proton version change looks like a different computer; symptom is a popup with a link to a Denuvo support page; remedy is wait 24 h. https://github.com/ValveSoftware/Proton/issues/7397 . Steam thread (verified with correction, July 2020): "Currently your game purchase cannot be re-validated successfully, please wait 24 hours and try again" opened as a codefusion page after repeated reinstalls in a VM plus a move to a new laptop; posters blamed >5 installs or the VM presenting as a new machine; OP later said refund-and-rebuy "worked for me" 13 days later, which cannot be separated from the 24 h simply elapsing. https://steamcommunity.com/app/1113000/discussions/0/2640748842582637663/
- Machine identity details (unverified): GE-Proton 10-11's Denuvo patch is a one-line wine.inf change that stops rewriting HKLM\...\Windows NT\CurrentVersion\DigitalProductId on each prefix update. https://github.com/Etaash-mathamsetty/wine-valve/commit/cd3efb2ade393776b40a5731eb8a81dec46e8b30 . Tekken 7 issue: the activation token lives in the prefix at users/steamuser/Local Settings/Application Data/Steam/userdata/<steamid>/<appid>/<number> and the game deletes it when invalid. https://github.com/ValveSoftware/Proton/issues/3267

Not found by any angle: any report of a Denuvo title failing on CrossOver 26.x specifically, on macOS 26/27, or on M4-class hardware; any statement linking Denuvo to the "VirtualApple" brand string or hypervisor detection under Rosetta.

## SEGA and Atlus titles

All of the following are from the sega-atlus-mac and denuvo-apple-silicon angles; only the P4G rows above and the CrossOver 23.5 announcement were verified, the rest are unverified.
- Persona 4 Golden: see p4g_elsewhere. Verified: AGW CrossOver "Runs" (dungeon crash), Wine "Does not boot"; Whisky Garbage/installs/opens with dungeon-load crash; CodeWeavers Limited Functionality at 26.1.0.
- Persona 5 Royal: pre-23.5 CrossOver "will not open at all", attributed to Denuvo and transacted-file APIs (2022-10-31), later to AVX (2023-02-05). Now AGW "Perfect" on CrossOver 24.0.5 (M3 Pro, macOS 15.2, 2024-12-22) but "Unplayable - Game doesn't even boot" via Whisky 2.3.4 on the same machine. CodeWeavers: "Installs, Will Not Run" at 23.7.1, "Runs Great" at 24.0.5 and 25.0.1, then "Limited Functionality" at 26.1.0 (2 ratings, unexplained). MacGamingDB has CrossOver 26.0/26.1 DXMT "Excellent" on M1 Pro and M4 Pro. A Steam thread (2026-03-06, M1 Air, CrossOver 26.0 DXMT) says "Game crashes on system data creation" until upgrading to macOS Sequoia. Proton issue #9354 (2025-12-31): on an M1 Pro under CrossOver P5R shows the loading logo and closes silently while P4G and P3R work; had worked on another MacBook. https://www.applegamingwiki.com/wiki/Persona_5_Royal ; https://github.com/ValveSoftware/Proton/issues/9354
- Persona 3 Reload: AGW "Perfect" on CrossOver and Whisky; the Tartarus loading crash was fixed by the game's own update 1.04; Whisky docs "Platinum"; CodeWeavers "Runs Well" (26.1.0); MacGamingDB 12 CrossOver reports incl. v26.0 on M1 Pro, M4, M5 Pro. https://www.applegamingwiki.com/wiki/Persona_3_Reload
- Metaphor: ReFantazio: CodeWeavers "Runs Well" (25.0.1), tester "Got to an early game save area without issue, just sound cutting out"; audio dropouts are the only Mac complaint. https://www.codeweavers.com/compatibility/crossover/metaphor-refantazio
- Shin Megami Tensei V: Vengeance: single MacGamingDB report, CrossOver 26.0 D3DMetal M4 Pro, "GOOD - Has scaling issues".
- Persona 3 Portable: Denuvo activation-limit popup under Proton (verified, issue #7397).
- Sonic Frontiers: AGW Wine "Game won't start up (AVX is not supported by apple silicon)" (2023-06-28, M2 Max) but "Playable" on CrossOver; CodeWeavers "Runs Well" (25.0.0). https://www.applegamingwiki.com/wiki/Sonic_Frontiers
- Like a Dragon: Infinite Wealth: CodeWeavers "Installs, Will Not Run" (24.0.4); a 2024-10-09 tutorial runs it on CrossOver with CXPatcher "fixing the AVX issue" on Sequoia.
- Sonic Origins (CrossOver 22 "Crashes on launch", Parallels 19 "Does not launch", 2023-08-27) and Persona 5 Strikers (CrossOver 22 "crashes when you try to run it", 2022-10-05): 2022-2023-era failures with no later data.
- Yakuza: Like a Dragon (x86 Linux): the Nov 2020 "newer Denuvo" illegal-instruction crash fixed by a wine patch. https://github.com/ValveSoftware/Proton/issues/4363
- Team Sonic Racing: FEX added SGDT for its Denuvo in 2022-11.
- Non-Denuvo controls: Yakuza Kiwami 2 and Yakuza 3 Remastered need ROSETTA_ADVERTISE_AVX=1 on AGW; PCGamingWiki confirms no Denuvo for those two and Denuvo for P4G, P5R, P3R, Metaphor, Sonic Frontiers, Sonic Origins, SMT V: V, LaD IW, P5 Strikers.
Pattern across SEGA/Atlus: every documented launch-time failure on Apple silicon was AVX (fixed by Sequoia's Rosetta, GPTK 2, CXPatcher or ROSETTA_ADVERTISE_AVX=1), transacted-file APIs (P5R 2022), a game bug patched by the publisher (P3R 1.04), or an old CrossOver 22 engine. None is described as loading screen, activation exchange, then exit.

## What differs from our failure

WHAT THE WORKING CASES HAVE THAT WE DO NOT
1. Host OS and engine generation. Every Apple silicon P4G success is on macOS 14-15 with CrossOver 23.6 through 26.0/26.1 (AGW 2023-11-22 note on 23.6, verified; AGW GPTK 2 M2 Pro note, verified; MacGamingDB 25.0/25.0.1/26.0, unverified; CodeWeavers tester on 24.0.1 at the TV entrance, unverified). No report of any Denuvo title on macOS 27 or an M4 Max was found. CodeWeavers tied Denuvo support to the host OS ("Denuvo games run on Sonoma", verified) without saying why, so a host-OS-dependent mechanism is plausible but undocumented. The P5R downgrade to "Limited Functionality" at 26.1.0 on codeweavers.com (unverified) and the silent post-logo P5R exit on an M1 Pro under CrossOver in Proton issue #9354 (unverified, 2025-12-31) are the only hints that something on the 26.x line regressed for an Atlus Denuvo title, and neither gives a cause.
2. A stock launcher. Working Mac reports use stock CrossOver or Whisky launching the Steam client; ours runs through the RaccoonBot launcher with engine patches and hooks. On Windows and Proton, injected code is the leading reported cause of P4G closing at or right after start (RTSS, MSI Afterburner, Discord overlay, Steam Overlay, MangoHud MANGOHUD=0 on Proton-GE; all unverified). https://www.pcgamingwiki.com/wiki/Persona_4_Golden
3. Their failures come after gameplay starts. The Mac community's crash is in-game (dungeons, area loads), i.e. the protector's startup and activation have passed. Ours never leaves the first loading screen.

WHAT THE FAILING CASES SHARE WITH US
1. Access violation from inside protector code on Apple silicon under Wine, deterministic, after a Denuvo version change: Sikarugir #233 (AoE2 DE, unverified) is the closest analogue, and its cause was engine-side (raw syscall under Rosetta faulting in Wine's SIGSYS path, returning c0000005), not activation or CPUID. Our fault (write to an impossible address, rax=0x80000002, from .arch) is a different instruction pattern, but the shape (protector code, Rosetta, Wine dispatch) matches.
2. c0000409 fast-fail a few seconds after start from a Denuvo title under an x86 emulator: box64/MGSV (unverified), attributed to emulation fidelity (flags).
3. Hardware exception from inside the protected image on Wine fixed by an ntdll-level wine change: Yakuza LaD 2020 (unverified). Note our observation that the dispatch sometimes gets lost in the host unwinder resembles the "virtual_unwind exception data not found" reported there.
4. Rosetta-specific divergences the protector can observe: faked debug registers (wine commit 93fde56b49, unverified), SGDT/SIDT/LSL/CPUID topology (FEX, unverified), the vendor's own "Detects virtualized and emulated execution environments" (verified). Whether P4G's protector reacts to any of these is not reported anywhere.

WHAT DOES NOT MATCH
- Every reported Denuvo refusal on this title is user-visible and time-based: a "failed to check Steam ID" popup then a SEGA support popup (verified), a codefusion "cannot be re-validated ... wait 24 hours" browser page (verified), a popup with a Denuvo support link (verified, P3P). We saw a 399-byte reply, no page, no dialog, then a fault. An activation-limit lockout is not what the sources describe for our symptom, although the sources cannot exclude that a refused ticket is consumed silently on this code path.
- P4G's classic post-logo crash on Wine was video decoding (ASF/WMV, then d3dx11_43 stub); our earlier measurement (no MF/GStreamer/libvpx in the process at the loading screen) already rules that out for the 2023 build.
- AVX: our fault is unchanged with AVX advertising off, and the SEGA AVX failures are instant non-starts, not a 12 s loading screen.
- Graphics backend and GPU identity: public Mac reports reach gameplay on DXVK, D3DMetal and DXMT alike; our fault is invariant across D3DMetal/DXMT and AMD/NVIDIA identity, consistent with the backend being irrelevant.

## Worth doing next, none of it circumvention

- Run once with every launcher instrument, engine patch, D3D hook and the Steam Overlay disabled, on a stock CrossOver 26.3 bottle launched from stock CrossOver, and see whether the 12-14 s fault survives. Motivated by the overlay/injection reports on PCGamingWiki and Atlus's bug thread (RTSS, Afterburner, Discord, Steam Overlay, MangoHud; unverified) https://www.pcgamingwiki.com/wiki/Persona_4_Golden and by the fact that every working Mac report uses a stock launcher (AGW, Whisky docs, verified).
- Count activations. Denuvo grants 5 tickets per game per 24 h and each Proton/engine change reads as a new machine (kisak-valve, verified, https://github.com/ValveSoftware/Proton/issues/7397). Our D3DMetal/DXMT/AMD/NVIDIA/AVX variants may each have consumed one. Pick one fixed configuration, wait 24-48 h without launching (CEP troubleshooting page, unverified, https://p4g-deck.cep.one/appendix/troubleshooting), then launch exactly once and record whether the fault and the garry.sgaas.net exchange change.
- Check whether an activation token persists in the bottle after the 399-byte reply: look under users/<user>/Local Settings/Application Data/Steam/userdata/<steamid>/1113000/ (and AppData/Local equivalents) before and after the fault; Denuvo deletes the token when it judges it invalid (Tekken 7 issue, unverified, https://github.com/ValveSoftware/Proton/issues/3267). A token that appears and vanishes points at machine rejection; none at all points at an earlier failure.
- Verify that HKLM\Software\Microsoft\Windows NT\CurrentVersion\DigitalProductId is stable across our launches and bottle/engine updates; GE-Proton's Denuvo fix was solely to stop wine.inf from clobbering it (unverified, https://github.com/Etaash-mathamsetty/wine-valve/commit/cd3efb2ade393776b40a5731eb8a81dec46e8b30). Our memory note says stock CrossOver bottle updates rewrite system32, so a rewrite of this key is plausible.
- Look in our trace for a SIGSYS / raw syscall instruction, a GetThreadContext/SetThreadContext with CONTEXT_DEBUG_REGISTERS, or SGDT/SIDT/LSL immediately before the .arch fault. Motivated by Sikarugir #233 (raw syscall under Rosetta -> c0000005 inside Denuvo, unverified, https://github.com/Sikarugir-App/Sikarugir/issues/233), wine commit 93fde56b49 (faked DRs under Rosetta, unverified, https://github.com/wine-mirror/wine/commit/93fde56b49) and FEX PR 4377 (unverified). This is measurement of the engine, not circumvention.
- Try the same bottle on a macOS 15 (Sequoia) host or, failing that, CrossOver 25.x/26.0-26.1 on this machine, keeping the bottle and DigitalProductId constant. Every reported Mac success is on macOS 14-15 and CrossOver 23.6-26.1 (AGW verified; MacGamingDB unverified), CodeWeavers tied Denuvo support to the host OS version without explanation (verified), and no report exists for macOS 27 or M4 Max.
- Confirm the bottle's system32 carries the native d3dx11_43.dll (~277 KB) rather than the ~81 KB wine stub, and that the Steamworks redist installed; the 2023 build's post-logo 'Application has crashed' under Proton was this (unverified, https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-1435071521). Cheap to check even though our fault site is in .arch.
- Compare the exception-dispatch path: our dispatch sometimes gets lost in the host unwinder; Yakuza LaD's Denuvo crash on Proton logged 'virtual_unwind exception data not found' and was fixed by a wine ntdll change from Paul Gofman (unverified, https://github.com/ValveSoftware/Proton/issues/4363). Diffing how CrossOver 26.3's ntdll handles exceptions raised from a section with no unwind data against 26.0/26.1 is a legitimate engine investigation.
- Fetch the CodeWeavers forum thread 'Persona 4 Golden crash' (support/forums/general?t=27;msg=328802) and the P5R/SF6 threads manually in a real browser; every automated route hit a Cloudflare challenge. Search snippets suggest it is a May 2025 D3DMetal crash after the loading screen, the nearest public description of our symptom, but its content is unestablished.
- Read Whisky issues #270 (RE2/RE3) and #851 (Dying Light 2) crash logs for how a Denuvo title fails right after 'faking zero debug registers' under Rosetta (unverified pointers from the denuvo-apple-silicon angle) to see whether the fault shape resembles ours.

## Not worth doing

- Codec/media work: P4G's post-logo failures on Wine were ASF/WMV decoding on the 2020 build and became moot with the 2023 VP9/IVF update (Proton #3982, GloriousEggroll 2023-02-17, unverified); we already measured no MF/GStreamer/libvpx in the process at the fault. Installing quartz, wmp, LAV, K-Lite or GStreamer plugins will not touch this fault.
- AVX advertising: the SEGA AVX failures are instant non-starts under Rosetta (AGW Sonic Frontiers, P5R forum, unverified), and we measured the fault unchanged with AVX advertising off.
- Graphics backend and GPU vendor identity: public Mac reports reach gameplay on DXVK, D3DMetal and DXMT (AGW verified; MacGamingDB unverified); our fault is invariant across D3DMetal/DXMT and AMD/NVIDIA identity.
- The AMD RX 6000 driver crashes (2022-2023 Windows, r/persona4golden PSA, unverified): in-game area transitions on real AMD drivers, unrelated to a 12 s loading-screen exit under Wine.
- Windowed mode or waiting through a 10-minute first launch (Steam Deck thread, unverified): our process faults deterministically at 12-14 s, it is not slow.
- Expecting the known Mac dungeon crash to explain us: Whisky (verified) and AGW (verified) document a mid-game crash with party members; it happens hours after the point where we die.
- Attributing this to Denuvo-on-Apple-silicon in general: P4G, P5R, P3R, Metaphor and SMT V:V all have Apple silicon CrossOver reports reaching gameplay (verified for P4G, unverified for the rest), and Denuvo activates inside ARM Windows VMs on the same hardware (AGW verified).
- Refund-and-rebuy to reset activations: the only report (Steam July 2020, verified with correction) cannot separate the rebuy from the 24 h lock simply expiring, and a poster in the thread doubted it would help.
- Pirated-copy comparisons on ProtonDB (verified): those launches fail for lack of a legitimate ticket and say nothing about a licensed copy's behaviour under Rosetta.
- Any circumvention of the protector: out of scope by the user's own framing, and no legitimate source reports one.

## Sources

- <https://www.protondb.com/api/v1/reports/summaries/1113000.json>
- <https://www.protondb.com/app/1113000>
- <https://store.steampowered.com/saleaction/ajaxgetdeckappcompatibilityreport?nAppID=1113000>
- <https://github.com/ValveSoftware/Proton/issues/3982>
- <https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-1204252450>
- <https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-756887898>
- <https://github.com/ValveSoftware/Proton/issues/3982#issuecomment-1435071521>
- <https://github.com/ValveSoftware/Proton/issues/7397>
- <https://github.com/ValveSoftware/Proton/issues/4363>
- <https://github.com/ValveSoftware/Proton/issues/3267>
- <https://github.com/ValveSoftware/Proton/issues/9354>
- <https://p4g-deck.cep.one/appendix/troubleshooting>
- <https://steamcommunity.com/app/1113000/discussions/0/2640748842582637663/>
- <https://steamcommunity.com/app/1113000/discussions/0/3716062978734696491/>
- <https://steamcommunity.com/app/1113000/discussions/0/3269060963457280800/>
- <https://steamcommunity.com/app/1113000/discussions/0/2441461920666944482/>
- <https://steamcommunity.com/app/1113000/discussions/0/6117591738154364655/>
- <https://www.applegamingwiki.com/wiki/Persona_4_Golden>
- <https://www.applegamingwiki.com/wiki/Persona_5_Royal>
- <https://www.applegamingwiki.com/wiki/Persona_3_Reload>
- <https://www.applegamingwiki.com/wiki/Sonic_Frontiers>
- <https://www.applegamingwiki.com/wiki/Tekken_8>
- <https://www.applegamingwiki.com/wiki/Game_Porting_Toolkit>
- <https://github.com/Whisky-App/Whisky/issues/1045>
- <https://github.com/Whisky-App/whisky-book/blob/main/src/game-support/persona-4-golden.md>
- <https://docs.getwhisky.app/game-support/persona-4-golden.html>
- <https://macgamingdb.app/games/1113000>
- <https://applesilicongames.com/games/WuhKUdxZpsvWEYmwa9i3mB/persona-4-golden>
- <https://forum.parallels.com/threads/persona-4-golden-missing-objects-textures-on-m1-pro.361817/>
- <http://web.archive.org/web/20260503075610/https://www.codeweavers.com/compatibility/crossover/persona-4-golden>
- <http://web.archive.org/web/20260807231143/https://www.codeweavers.com/blog/mjohnson/2023/9/27/crossover-235-is-a-real-game-changer>
- <http://web.archive.org/web/20240205152101/https://www.codeweavers.com/compatibility/crossover/forum/persona-5-royal?msg=270341>
- <https://www.codeweavers.com/compatibility/crossover/metaphor-refantazio>
- <https://github.com/wine-mirror/wine/commit/93fde56b49>
- <https://github.com/Sikarugir-App/Sikarugir/issues/233>
- <https://github.com/FEX-Emu/FEX/pull/4377>
- <https://github.com/ptitSeb/box64/issues/3659>
- <https://github.com/Etaash-mathamsetty/wine-valve/commit/cd3efb2ade393776b40a5731eb8a81dec46e8b30>
- <https://irdeto.com/video-games/denuvo-anti-cheat/anti-tamper>
- <https://irdeto.com/blog/denuvo-anti-piracy-proton-linux-steam-deck>
- <https://www.pcgamingwiki.com/wiki/Persona_4_Golden>
- <https://www.reddit.com/r/persona4golden/comments/vvnne3/psa_persona_4_golden_crashing_solution_for_amd/>
