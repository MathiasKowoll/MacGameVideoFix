# What ships, and where it goes

Every binary this project installs, what it is, and which of four places it
belongs to. Written because "which files does a launcher have to copy" was asked
and could not be answered from one page — and because answering it turned up one
category nothing copies at all.

## 1. Built here, from sources in this repository

Twelve proxy DLLs. Each rides on a library the game already loads, forwards
every export to the renamed original, and gets `DllMain` — that is the whole
mechanism. `check-builds.sh` rebuilds every one of them from the source named
below and refuses a release where the shipped copy differs.

| DLL | source | titles |
| --- | --- | --- |
| `GfeSDK.dll` | `p5s-video-bridge.c` | Nioh, Nioh 2 |
| `amd_ags_x64.dll` | `p5s-video-bridge.c` | Persona 5 Strikers |
| `amd_ags_x64-nioh3.dll` | `dwo-video-bridge.c` | Nioh 3 |
| `libxess.dll` | `dwo-video-bridge.c` | Dynasty Warriors: Origins, Wo Long |
| `dinput8-kh.dll` | `dwo-video-bridge.c` | Kingdom Hearts (both) |
| `dinput8-nier.dll` | `dwo-video-bridge.c` | NieR Replicant |
| `libogg_64.dll` | `ue5-media-fix.c` | Mortal Shell 2, Beast of Reincarnation, Life is Strange (both) |
| `libogg_64_electra.dll` | `electra-h264-fix.c` | the Electra variant |
| `dstorage-ng4.dll` | `ng4-observe.c` | NINJA GAIDEN 4 |
| `fmod-tmnt.dll` | `d3d12-guards.c` | TMNT: Splintered Fate |
| `OpenColorIO_2_3-tormented.dll` | `d3d12-guards.c` | Tormented Souls 2 |
| `NvCloth_x64-resonance.dll` | `shader-floor-fix.c` | RESONANCE: A Plague Tale Legacy |

**Where they go:** into the game's own folder, beside the executable, with the
game's original renamed to `<name>_real.dll`. The installer for each title
decides the exact folder. Nothing outside the game folder is touched.

## 2. Other people's binaries, redistributed

Four, all for NINJA GAIDEN 3: Razor's Edge. They cannot be rebuilt from anything
here, so `check-builds.sh` verifies them against recorded sha256s in
`ng3-THIRD-PARTY-LICENCES.md` instead.

| DLL | what it is | licence |
| --- | --- | --- |
| `ng3-d3d9.dll` | d9vk, `Sikarugir-App/d9vk` `v1.10.3-20250511` | zlib, as DXVK |
| `ng3-qasf.dll` | Wine's DirectShow ASF Reader, patched by winevideo | LGPL-2.1-or-later |
| `ng3-quartz.dll` | Wine's filter graph, patched by winevideo | LGPL-2.1-or-later |
| `ng3-winegstreamer.dll` | Wine's GStreamer bridge, patched by winevideo | LGPL-2.1-or-later |

**Where they go:** into the **bottle's** `drive_c/windows/system32`, not the game
folder, activated for one executable through `AppDefaults`. This is the only
fix here with `"scope": "bottle"`.

## 3. Staged into the engine

Two GStreamer plugins, copied into CrossOver itself by `stage-codecs.sh`:
`libgstlibav.dylib` and `libgstmatroska.dylib`. The titles that need one are the
ones whose `codec` field in the manifest names it; for some of them it is the
entire fix and no DLL is installed beside the game at all.

**Where they go:** into the CrossOver application's own
`lib64/gstreamer-1.0`. They are shared by every bottle that engine runs.

## 4. Built here, installed into the engine, and packaged nowhere

Three files, all produced by this project, all sitting in the engine on this
machine, none of them in any bundle. `scripts/build-winegstreamer.sh` builds
them from a wine tree under `~/Development/mgvf-winegstreamer-build`, and
`built-for.json` beside them records what they were built against:

    engine 26.3.0.39832, wine-11.0-8726-g2e2f5fca349, patches 0002 0003 0006 0008

| file | size | what it is |
| --- | --- | --- |
| `winegstreamer.dll` | 2,330,624 | Wine's GStreamer bridge, PE half |
| `winegstreamer.so` | 222,376 | the same bridge, Unix half |
| `d3d9.dll` | 1,576,960 | Wine's d3d9 with winevideo's video-bridge patches |

**The winegstreamer pair is load-bearing and travels nowhere.** It was built
because codecs that were present would not show. Both halves have to match each
other and the engine they were built against, which is why `built-for.json`
exists and why neither half can be shipped without the other.

**The d3d9 is not.** It was built with winevideo's `0008` and `0009` patches for
the Nioh work and it did not fix Nioh — recorded in `docs/codecs-inside-the-engine.md`
at the time: *"with the complete bridge in the engine the title still dies with
zero calls to it"*. The bridge those titles actually use is
`runtime/p5s-video-bridge.c`, which ships as `GfeSDK.dll` and `amd_ags_x64.dll`
in the game folder and intercepts `OpenSharedResource` itself. Worse, this build
displaces something: RaccoonBot's patcher installs **d9vk** as the engine's
`d3d9.dll`, 3,848,151 bytes, and ours is sitting on top of it. It should go back.

**What this means for a user elsewhere.** The titles whose fix names the
D3D9-to-D3D11 bridge get that bridge from the game folder, so they are not
waiting on the d3d9; every other title never touches D3D9 at all. What they and every codec-dependent title may be
waiting on is the winegstreamer pair, and that has never been tested against an
engine without it.

This page has been wrong about these files twice, in opposite directions, before
anyone looked for the build tree that `docs/codecs-inside-the-engine.md` names.
Both claims were made from file sizes.

## What a launcher has to copy

- Everything in categories 1 and 2 arrives in `fixes-v<version>.tar.gz`, named
  per title in `manifest.json` under `files`, with the installer named in
  `script` and its argument's meaning in `scope`.
- Category 3 is not per-title: `stage-codecs.sh` travels in the same bundle and
  is run against the engine, not a game. The manifest's `codec` field says which
  titles need it.
- Category 4 is the open question above.
