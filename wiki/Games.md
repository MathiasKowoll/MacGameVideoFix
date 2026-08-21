Every row here was measured on an installed copy, not recalled. The method is
in [Diagnosing a new game](Diagnosing-a-new-game.md); where a claim comes from a static scan rather
than from playing the game, it says so.

Tested on an M4 Max, macOS 27, CrossOver 26.2 patched with
[winevideo](https://github.com/Jfishin/winevideo), GPTK 4.0b2.

## Fixed

### Mortal Shell 2 — Unreal Engine 5.6.1

| | |
| --- | --- |
| Cutscenes | 61 × VP9 in `.mp4`, inside `pakchunk0-Windows.pak` and loose |
| Played by | Electra, decoding VP9 with its own libvpx |
| Symptom | `EXCEPTION_ACCESS_VIOLATION` reading `0x0` in `FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer` |
| Fix | Runtime patch — **4 patch sites**, all four confirmed at runtime |

Both modes work. The runtime patch leaves the original VP9 files untouched;
re-encoding is the fallback.

## Under investigation

### DYNASTY WARRIORS: ORIGINS — Koei Tecmo, in-house engine

| | |
| --- | --- |
| Cutscenes | 355 × VP9 Profile 0, `.webm`, 960×540, **no audio track** |
| Played by | `IMFSourceReader`, created from a byte stream — the exe imports exactly one function from `MFReadWrite.dll`, `MFCreateSourceReaderFromByteStream` |
| Symptom | Black screen. Game runs and is playable; logo appears; no crash |

What the probe found so far:

- Media Foundation initialises correctly — **1738 `MFStartup` calls, every one
  `S_OK`**, each followed by `MFShutdown`. The game retries in a loop.
- Between them, **nothing**. Not `MFCreateFile`, not `MFTEnumEx`, not
  `MFCreateSourceReaderFromByteStream`, not even `MFCreateAttributes`.
- No `GetProcAddress` for any `MF*` symbol, so it is not resolving them
  dynamically behind our back.

So whatever fails is **not a Media Foundation call**, and winevideo's VP9 stack
is not the problem — it is never reached. The bottle has all of it in place:
the `.webm` byte-stream handler, the VP9 decoder MFT backed by real code in
`winegstreamer.dll`, `libgstvpx`, `libgstmatroska`, and `msvproc.dll`.

Next: hooks on `CoCreateInstance` (a decoder asked for by exact CLSID that Wine
does not register would fail exactly like this) and on `CreateFileW` for
`.webm` paths.

## Measured, not affected

### Returnal — Unreal Engine 4

95 × VP9 in `.mp4`, decoded by a bundled decoder — the executable imports no
media API at all. A static scan finds **0 instances** of Electra's D3D12
version check, so the crash described on [Home](Home.md) cannot occur here. UE4 has no
D3D12 output buffer pool.

Not played through to confirm.

### Ghost of Tsushima Director's Cut — in-house engine (Nixxes port)

252 × VP9 in `.webm`, bundled decoder, no media API imported. Untested.

## Everything else surveyed

73 titles were scanned. Grouped by how they play video, because that is what
decides the failure mode:

| How | Titles |
| --- | --- |
| **Bink** — own decoder, never touches Media Foundation or D3D video, unaffected by any of this | God of War Ragnarök (228), F1 23 (204), Mortal Kombat 1 (204), Jedi Survivor (134), Horizon Zero Dawn Remastered (102), Batman Arkham Asylum GOTY (45), SILENT HILL 2 (10), Life is Strange Reunion (3), Rayman Legends (2), Crash Bandicoot N. Sane Trilogy (2) |
| **H.264 through Media Foundation** — needs winevideo, otherwise normal | Mortal Kombat 1 (11), Revenge of the Savage Planet (100), Crash Bandicoot 4 (54) |
| **VP9** — the interesting class | Mortal Shell 2, DYNASTY WARRIORS: ORIGINS, Ghost of Tsushima DC, Returnal |

Titles that reported zero videos — Beast of Reincarnation, DRAGON BALL
Sparking! ZERO, NINJA GAIDEN 4, Stellar Blade — pack their movies somewhere the
survey cannot read. That is a gap in the tool, not a statement about the game.

## Adding a row

Run the survey, and if the game misbehaves, the probe. Both are in
`diagnostics/`. Paste what they print — measurements are the point of this
page, and a row without one is worse than no row.
