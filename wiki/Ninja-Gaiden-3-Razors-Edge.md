# NINJA GAIDEN 3: Razor's Edge

The game would not start at all, in any backend. It does now, at 60 fps, and
its in-game cutscenes play. The boot movie freezes on its first frame and one
click passes it; that part is not solved.

`install-ng3-fix.sh` applies it. Unlike every other installer here it takes a
**bottle**, not a game folder, because nothing it does is inside the game
folder — four DLLs go into the bottle's `system32` and the override is a
registry key. Nothing the game ships is touched.

**Why it is the odd one of the Master Collection.** SIGMA and SIGMA 2 import
`d3d11.dll` and start fine. Razor's Edge is **Direct3D 9 only** — `d3d9.dll`
and `d3dx9_43.dll`, no d3d11 anywhere — and that is the whole reason the other
two work under DXVK and this one would not start at all.

**The message it shows is a red herring.** Under CrossOver's own DXVK it says
*"Insufficient VRAM. Please close all running applications."* Its own log says
what really happened, eleven times: `DxvkAdapter: Failed to create device`,
after reporting `transformFeedback: 0` and `timelineSemaphore: 0`. MoltenVK does
not offer those and that DXVK requires them. Nothing to do with memory — DXVK
reports the machine's full 49152 MiB two lines earlier.

**Its videos are not Media Foundation.** 24 files in `databin/movie`, all ASF
containers, **WMV3** video and **WMAv2** audio at 1280x720, played through
**DirectShow** — `quartz` and `qasf`. An afternoon went into instrumenting
`MFTEnumEx`, both source readers and `MFCreateFile` before the winevideo
developer said where to look. The codec was measured with ffprobe; the path
was not, and the difference cost hours.

**The recipe.** Four overrides, all per-executable under
`HKCU\Software\Wine\AppDefaults\NINJA GAIDEN 3 Razor's Edge.exe\DllOverrides`,
so no other title is affected, with the DLLs placed in
`drive_c/windows/system32`:

| value | `native,builtin` | where it comes from |
| --- | --- | --- |
| `*d3d9` | d9vk | `Sikarugir-App/d9vk`, `d9vk-macOS-async-v1.10.3-20250511` |
| `*qasf` | patched | winevideo 0.5 payload |
| `*quartz` | patched | winevideo 0.5 payload |
| `*winegstreamer` | patched | winevideo 0.5 payload |

That d9vk is the **same DXVK version** CrossOver ships, 1.10.3, built
differently for macOS. "DXVK 1.10.3 does not work on Apple silicon" was written
here first and was wrong: CrossOver's build does not, this one does.

**Result.** The game starts, renders at 60 fps, reaches its menu, and **its
in-game cutscenes play**. The boot movie decodes its first frame and freezes;
one click skips it and the game carries on. Nioh was re-run afterwards and is
unaffected, which matters because these DLLs sit in a shared `system32` and only
the per-app override keeps them out of everything else.

**What is left, and why it may stay left.** Adding winevideo's `winegstreamer`
changed nothing observable. That fits: it is split into a PE half and a Unix
`.so`, and only the PE half can be overridden per application, so the two halves
no longer match. Chasing the boot movie means transplanting more of another
engine into this one. **winevideo 0.5 runs this title properly**, because there
all of these are one coherent build — that is the honest answer for anyone who
wants the intro as well.

## The binaries, and whose they are

<!-- count-ok --> Three of the four are other people's work and ship with the fix, because one
that only works for people who already installed something else is not a fix.
d9vk is DXVK, zlib. `qasf`, `quartz` and `winegstreamer` are Wine,
LGPL-2.1-or-later, patched by winevideo's author. `ng3-THIRD-PARTY-LICENCES.md`
travels beside them with the sha256 of each file, and `check-builds.sh` verifies
them against it — they cannot be rebuilt from a source in this repository, so a
swapped binary is caught by its hash or not at all.

The diagnosis is winevideo's author's, and it is the part that mattered: Wine's
DirectShow ASF Reader delivered a video sample on the WMReader callback, the
downstream video pin blocked in `Receive()`, audio needed that same callback to
preroll and never got it, and the graph sat in `VFW_S_STATE_INTERMEDIATE`
instead of reaching Running. Their build moves blocking video delivery onto a
serialized worker so audio can preroll. This project measured the codec, the
renderer, and why CrossOver's DXVK cannot create a device.

## Registry, and why it does not need a quiet bottle

The overrides are written through `reg.exe` inside the bottle and asked back
afterwards, the way the Kingdom Hearts and NieR installers already did here. The
first version of this script appended them to `user.reg` with a text editor and
therefore had to refuse to run while a wineserver was alive — which for a
launcher means quitting Steam and ending the prefix before it can even tell a
user whether their game needs the fix. That requirement was an artefact of how
four values were written, and it is gone.
