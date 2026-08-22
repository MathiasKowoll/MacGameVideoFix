# Nioh 3

The ninth title, and the one that needed no new bridge code at all — it is
fixed by the [DYNASTY WARRIORS](Dynasty-Warriors-Origins.md) bridge, not by the
one its two predecessors use.

| | |
| --- | --- |
| Video | NV12, 2560×1440, 29.97 fps, inside the game's own `.fdata` packages |
| Played by | `IMFSourceReader` on Media Foundation, presented through the D3D11 video processor |
| Symptom | `Failed to play movie.` |
| Fix | The DYNASTY WARRIORS video bridge, on `amd_ags_x64.dll` |
| Backend | **D3DMetal**, D3D12 — the opposite of Nioh and Nioh 2 |
| CrossOver | `crossover-preview-arm64-20260821`. Not tried on 26.3 |

## Not the same fault as Nioh or Nioh 2

The family name is misleading. Nioh and Nioh 2 are DXMT titles whose D3D9
cutscene surfaces come back with a null share handle. Nioh 3 is D3D12 on
D3DMetal and never touches D3D9. What it does instead:

    MFCreateSourceReaderFromByteStream
      MF_SOURCE_READER_D3D_MANAGER: SET          <- asking for D3D-backed decode
      MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS: 1

It asks Media Foundation to decode into D3D video textures. Under D3DMetal
there is nothing to decode into: `ID3D11Device::QueryInterface(ID3D11VideoDevice)`
returns `E_NOINTERFACE`, and so does the video context. The bridge answers by
passing a **copy** of the attributes with those two removed — never the game's
own store, which breaks the negotiation outright — and Media Foundation
resolves in software instead.

That is only the first half. The game then drives the D3D11 video processor
itself to present each frame: `CreateVideoProcessorEnumerator`,
`GetVideoProcessorCaps`, `CreateVideoProcessor`, the input and output views,
and eleven `VideoProcessorSet*` calls. Every one of those is a method on an
interface D3DMetal does not provide, and every one of them is stubbed.

## Streamline, and why the first attempt saw nothing

Nioh 3 ships NVIDIA Streamline. It imports `D3D11CreateDevice` and
`D3D12CreateDevice` from `sl.interposer.dll` — a drop-in replacement exporting
the same names — and never names `d3d11.dll` or `d3d12.dll` at all.

A hook placed against `"d3d11.dll"` therefore finds nothing. The first run
reported `d3d11 not imported` and the game failed exactly as it had before the
bridge existed. The fix is one more hook against the interposer's name, and the
lesson generalises: **the module a function lives in is not the module a game
asks for it from.**

## The latent bug this title exposed

Hooking only the D3D11 half made things worse rather than better: the game
crashed with `0xC0000005`.

The bridge has two halves that only work together. `GetSharedHandle` fails
under D3DMetal, so one half hands the game an invented handle; the other half
recognises that handle when the D3D12 renderer brings it back. With only D3D11
hooked, the first half was handing out `0xD3D12B21D` and the second half did
not exist — so the invented value reached a real D3D12 device.

Two things came out of that:

- `D3D12CreateDevice` is hooked against the interposer as well. DYNASTY
  WARRIORS resolves it through `GetProcAddress`, which is why this never
  mattered before.
- **The bridge no longer hands out an invented handle unless the hook that can
  read it is armed.** That guard was missing from the beginning and DYNASTY
  WARRIORS never hit it, because its D3D12 always arrived through a path the
  bridge watched. A game that cannot share surfaces and is told so fails
  cleanly and says `Failed to play movie`; a game that is lied to dies.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on Preview only, and on D3DMetal only. 26.3 has not been tried.

The carrier is `amd_ags_x64.dll`, which is also Persona 5 Strikers' carrier —
but with a different bridge inside and a different export set. The shipped
proxy is kept under a distinct name for that reason, and the installer's export
check refuses one game's proxy in the other's folder.

## What it cost

Four runs, and only the first was a diagnosis. A probe established that the
game reaches its video through Media Foundation and dies asking for D3D-backed
decode. The second and third were the two halves of the Streamline hook, one of
which crashed the game. The fourth played.

No bridge logic was written for this title. What changed was two hook targets
and one safety check.
