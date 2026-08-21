Titles we have taken on, and what was measured on each. Every row comes from an
installed copy rather than from memory; where a claim comes from a static scan
rather than from playing the game, it says so.

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

## Adding a row

This page covers titles we have deliberately taken on, not everything that
happens to be installed on someone's machine. A game gets a row when there is a
reason to work on it.

To add one: run the survey on that game's folder, and if it misbehaves, the
probe. Both are in `diagnostics/`, and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md). Paste what they print —
measurements are the point of this page, and a row without one is worse than no
row.
