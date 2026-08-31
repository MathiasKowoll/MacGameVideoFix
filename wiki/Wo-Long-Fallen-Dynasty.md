# Wo Long: Fallen Dynasty

The eleventh title, fixed by the [DYNASTY WARRIORS](Dynasty-Warriors-Origins.md)
bridge on the same carrier — and the one that exposed a gap that had been in
that bridge since it was written.

| | |
| --- | --- |
| Video | Delivered as NV12, 2560×1440, 29.97 fps, from the game's own archives |
| Played by | `IMFSourceReader` on Media Foundation, D3D11 decode and a D3D12 renderer |
| Symptom | The cutscene runs with sound and no picture |
| Fix | The DYNASTY WARRIORS bridge, on `libxess.dll` |
| Backend | **D3DMetal**, D3D12 |
| CrossOver | Stable 26.3, on a copy of it carrying this project's `winegstreamer` (2026-08-31) |

## Nothing new was written for it

Wo Long imports `d3d11`, `d3d12`, `dxgi`, `MFPlat` and `MFReadWrite` directly,
and ships `libxess.dll` — Intel's XeSS upscaler, which does nothing under Metal.
That is DYNASTY WARRIORS' configuration exactly, and the shipped proxy already
covered all 24 exports this build has.

## What it exposed

The first run with the bridge produced sound, no picture, and this:

    SetCurrentMediaType asked 'NV12' 2560x1440 -> S_OK     × 7
    (no ReadSample, ever)

Seven negotiations, every one answered correctly, and not a single sample read.
The game was building a player, being told yes, and tearing it down again.

What was missing from the log said why: **`D3D12 device reached, bridge armed`
never appeared.** The bridge hooks `D3D12CreateDevice` two ways — through
`GetProcAddress`, which is how DYNASTY WARRIORS reaches it via NVIDIA
Streamline, and against `sl.interposer.dll`, which is how Nioh 3 imports it. Wo
Long imports it as **ordinal 101 of `d3d12.dll`** and calls it straight through
its own import table.

An import table entry is either a name or an ordinal, and a hook that walks
names skips the ordinals entirely: it installs nothing, reports nothing, and is
never called. The bridge had no ordinal hooking at all. The probe did, which is
why the probe played the video and the bridge did not.

DYNASTY WARRIORS hid this for as long as the bridge has existed. It has the same
ordinal import, but reaches the function through Streamline at runtime, so the
`GetProcAddress` substitution caught it and the missing half never showed.

## The guard that turned a crash into a diagnosis

With the D3D12 side unarmed, the bridge refused to hand out the share handle
that only that side can recognise — a guard added the day before, after Nioh 3
crashed with `0xC0000005` for handing one out with nothing to read it.

So Wo Long got a real failure from `GetSharedHandle`, learned it could not
share, and retried. Seven clean retries instead of an access violation, and a
log that named the cause. The guard was written as a precaution; this is where
it paid.

## Caveats

Do not use this on a game with anti-cheat. The fix patches a running process.

Measured on D3DMetal only: on 2026-08-31 on stable 26.3 — on a copy of it this
project patched, carrying our `winegstreamer`, not a stock one. The picture has
been read only on that patched copy. It was measured on
`crossover-preview-arm64-20260821` before that, which stands as a record rather
than a route, since Preview is no longer a supported engine here.

No staged codec is needed: the reader reports its native type as NV12 with
`MF_MT_COMPRESSED` at 0, so whatever decodes it does so upstream and the stored
format never became visible here.

## What it cost

Three runs. The first established the symptom and the seven retries, the second
was the probe playing the video and showing the bridge what it was missing, and
the third was the bridge with ordinal hooking.
