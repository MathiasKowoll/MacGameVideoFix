# Nioh 2

The third title fixed by the bridge written for
[Persona 5 Strikers](Persona-5-Strikers.md), and the one that cost nothing: no
code change, no rebuild, the same proxy binary already built for
[Nioh](Nioh.md).

| | |
| --- | --- |
| Video | WMV3 in ASF, 529 files |
| Played by | `IMFSourceReader` on Media Foundation, presented onto a shared D3D9 surface |
| Symptom | The cutscene refuses to play, then the game crashes |
| Fix | Stage the WMV3 decoder, then hand the game a share handle that exists |
| Backend | **DXMT only.** The sidecar needs `GetSharedHandle`, `E_NOTIMPL` under D3DMetal |
| CrossOver | Stable 26.3, on a copy of it carrying this project's `winegstreamer` (2026-08-31) |

## What it shares with Nioh, and what it does not

Same carrier — `GfeSDK.dll`, exporting the identical 16 symbols, so the proxy
built for Nioh installs here untouched. Same WMV3-in-ASF video. Same pair of
`d3d9` and `d3d11` imports, and the same fault underneath: a shared render
target that D3D9 creates, reports `S_OK` for, and hands back with a share handle
of zero.

What differs is everything above that line. Nioh builds a DirectShow graph;
Nioh 2 imports `MF`, `MFPlat`, `MFReadWrite` and `dxva2` and goes through Media
Foundation, which is Persona 5 Strikers' route rather than its own predecessor's.

That difference is visible in what the bridge caught:

    import table: 4 of 6 Media Foundation and D3D9 entries hooked
    MFStartup(version=0x20070, flags=0x0) -> S_OK
    MFCreateSourceReaderFromByteStream -> S_OK (reader 1)
      -> ASF header, so the container is what it should be
      stream 0 offers: 'I420'   stream 0 asked for: 'NV12'

Four hooks where Nioh landed two, and a source reader Nioh never created. The
bridge needed to know none of it: it watches the shared surface, not the player.

    CreateRenderTarget(1920x1080 fmt=22, SHARED requested) -> S_OK, handle 0
    sidecar: 1920x1080 texture, GetSharedHandle -> S_OK, handle 40000102
    StretchRect INTO it
    source luma [120]: average 72, range 18..230   << has picture

## The frame format differs and nothing had to care

Nioh's frames arrived in a four-byte format at a pitch of 7680. Nioh 2's arrive
as NV12 at a pitch of 1920. The NV12-to-BGRA converter that came across from
[DYNASTY WARRIORS](Dynasty-Warriors-Origins.md) handled the difference with no
adjustment — the second time that converter has moved between titles unchanged.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on DXMT only: on 2026-08-31 on stable 26.3 — on a copy of it this
project patched, carrying our `winegstreamer`, not a stock one. It was measured
on `crossover-preview-arm64-20260821` before that, which stands as a record
rather than a route, since Preview is no longer a supported engine here. Under
D3DMetal the sidecar has nothing to build on, since `GetSharedHandle` is
`E_NOTIMPL` there, so it is not expected to work — but that has not been run
either.

## What it cost

One run. The preparation was three read-only checks that could each have sent
this down a longer road and did not: the video is WMV3 in ASF like Nioh's, the
carrier exports the identical 16 symbols so no rebuild was needed, and the
executable imports both `d3d9` and `d3d11`.
