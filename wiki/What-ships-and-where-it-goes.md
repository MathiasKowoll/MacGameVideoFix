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

## 4. Replaced in the engine by hand — and this category ships nowhere

Two files in the engine have been swapped on the machine this work was done on,
and neither travels in any bundle:

| file | live here | the engine's own |
| --- | --- | --- |
| `d3d9.dll` | 1,576,960 bytes | 192,080 bytes |
| `winegstreamer.dll` | 2,330,624 bytes | 424,000 bytes |

The `d3d9.dll` was built in this project with winevideo's two video-bridge
patches, for the Nioh work. A `.stock` copy of each original sits beside it,
which is how they can be told apart at all.

**This is a gap, not a category.** Nioh and Persona 5 Strikers are published as
fixed, and both need a D3D9-to-D3D11 bridge. If that bridge lives partly in an
engine file no user receives, then what is published is a fix verified only
where it was made. It has not been measured either way: restoring the stock
`d3d9.dll` and launching Nioh would settle it in one run, and until that is done
this page should not be read as saying those titles are safe.

This project already has this failure recorded once under another name —
`dwo-video-bridge.c` shipped keyed on an environment variable nothing ever set,
so it answered "no" on every machine but the one it was measured on.

## What a launcher has to copy

- Everything in categories 1 and 2 arrives in `fixes-v<version>.tar.gz`, named
  per title in `manifest.json` under `files`, with the installer named in
  `script` and its argument's meaning in `scope`.
- Category 3 is not per-title: `stage-codecs.sh` travels in the same bundle and
  is run against the engine, not a game. The manifest's `codec` field says which
  titles need it.
- Category 4 is the open question above.
