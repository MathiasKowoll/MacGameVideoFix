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

## Diagnosed, not yet fixed

### DYNASTY WARRIORS: ORIGINS — Koei Tecmo, in-house engine

| | |
| --- | --- |
| Cutscenes | 355 `.webm`, VP9 Profile 0. 42 of them 2560x1440 and up to four minutes; the rest 960x540 interface clips |
| Played by | `IMFSourceReader`, on a **separate D3D11 device** created only for video |
| Symptom | Black screen. Game runs and is playable; no crash |

**Cause.** The video player queries the D3D11 device for `ID3D11VideoDevice`
and its context for `ID3D11VideoContext`. Both return `E_NOINTERFACE`, and the
player gives up before opening anything:

```asm
QueryInterface(ID3D11VideoDevice)    ; 0x80004002
QueryInterface(ID3D11VideoContext)   ; 0x80004002
js  <exit>                           ; whole subsystem off
```

It retries once a frame, forever, which is why nothing appears and nothing
crashes.

**Why the interfaces are missing.** With `CX_GRAPHICS_BACKEND=d3dmetal`,
CrossOver puts Apple's Game Porting Toolkit ahead of Wine in the search path.
GPTK ships a 136 KB `d3d11.dll` that is a thin shim over D3DMetal and carries
neither video interface. Wine's own `d3d11.dll` — 425 KB, over wined3d —
implements `ID3D11VideoDevice1` with 24 real methods and a working decoder,
and its `QueryInterface` hands it out:

```c
else if (IsEqualGUID(riid, &IID_ID3D11VideoDevice) || IsEqualGUID(riid, &IID_ID3D11VideoDevice1))
    *out = &device->ID3D11VideoDevice1_iface;
```

So the implementation exists; the game is just not getting it. D3DMetal's own
binary carries `ID3D11Device` and `ID3D12Device` but neither video interface —
nor `ID3DDestructionNotifier`, which independently confirms the Mortal Shell 2
cause from Apple's binary.

**Why it is not a one-line fix.** The game renders with D3D12 through D3DMetal
and wants a D3D11 device only for video, so in principle the two backends could
coexist. But Wine's `d3d11` builds its device from a DXGI adapter through
Wine-private interfaces, which GPTK's `dxgi.dll` does not implement — and even
past that, frames decoded on a wined3d device would still have to reach a
D3DMetal renderer.

**What was measured, in order.** The path here is worth recording because four
launches went into guesses that were all wrong:

1. Media Foundation initialises fine — 2209 `MFStartup` calls, every one
   `S_OK`. Not the problem.
2. No `CoCreateInstance` failed, and the game never opens a `.webm`. Not a
   codec registration, not the file.
3. `find-callsites.py` located the video player statically, without a run, by
   resolving `call qword ptr [rip+disp]` against the import table.
4. Disassembling it gave the exact gate, which a probe then confirmed at
   runtime by asking the same two questions on the same objects.

## Adding a row

This page covers titles we have deliberately taken on, not everything that
happens to be installed on someone's machine. A game gets a row when there is a
reason to work on it.

To add one: run the survey on that game's folder, and if it misbehaves, the
probe. Both are in `diagnostics/`, and both are described in
[Diagnosing a new game](Diagnosing-a-new-game.md). Paste what they print —
measurements are the point of this page, and a row without one is worse than no
row.
