# MacGameVideoFix

Makes Windows games show their cutscenes under CrossOver on Apple Silicon.

Six games so far, failing for reasons that have almost nothing in common.
They install the same way: open the app, pick the game from the list, drop its
folder on it, press Apply.
| Game | Symptom | Fix | Backend | DX |
| --- | --- | --- | --- | --- |
| [**Mortal Shell 2**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Mortal-Shell-2) | Crash on the first cutscene | Runtime patch | D3DMetal | 12 |
| [**Life is Strange: Reunion**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) | Runs, then freezes after a while | Runtime patch | D3DMetal | 12 |
| [**Life is Strange: Double Exposure**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure) | Runs, then freezes after a while | Runtime patch | D3DMetal | 12 |
| [**Beast of Reincarnation**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Beast-of-Reincarnation) | Startup video plays with sound, no picture | NV12 restored, Electra forced to software | D3DMetal | 12 |
| [**Persona 5 Strikers**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Persona-5-Strikers) | Video never starts; sound only | Staged VC-1 codec, D3D9 → D3D11 bridge | **DXMT** | 11 |
| [**DYNASTY WARRIORS: ORIGINS**](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Dynasty-Warriors-Origins) | Cutscene plays with sound, picture black | Video bridge | D3DMetal | 12 |

Each row links to a page in the [wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki) with that game's findings and fix.


with it, same version, differing only in the GStreamer plugins.
`.webm` byte-stream handler registered. It was expected to fail there and did
not; how the WebM is opened under those conditions is **not yet explained**, so
this is a measurement with an open question behind it.

One DLL carries three unrelated repairs, and each is asked for by executable
name rather than applied wherever its pattern happens to match: the Electra
buffer path that crashes Mortal Shell, the adapter-node walk that freezes both
Life is Strange titles, and the H.264 output negotiation that leaves Beast of
Reincarnation silent-but-blank. They share one file because they share one
carrier DLL — separate files would mean a title could only ever have one.

Tested on an M4 Max, macOS 27, CrossOver 26.2 with Game Porting Toolkit 4.0b2.

---

## Quick start

1. Download `MacGameVideoFix.app` from
   [Releases](../../releases), or build it yourself with `app/build-app.sh`.
2. Open the app, **pick your game from the list**, drop its folder on it, and
   press **Apply Fix**.

Picking the game first is what tells the app which folder to ask for, and it
says so on the drop zone. It also checks the game's shipping executable is
really under the folder you dropped, so pointing Double Exposure at Reunion's
folder is caught rather than half-applied.

For DYNASTY WARRIORS that warning is fatal — nothing will decode. For the
Unreal titles it is advice.

**Revert** puts everything back.

### Which folder to pick

For **DYNASTY WARRIORS: ORIGINS**, the folder holding `DWORIGINS.exe` —
usually `steamapps/common/DWORIGINS`.

For an **Unreal title**, the game's own folder — the one with `Engine` in it:

```
…/steamapps/common/Sparta                   ← drop this one
├── Engine/
│   └── Binaries/ThirdParty/Ogg/Win64/      ← the fix rides in here
└── MortalShell2/
    ├── Binaries/Win64/                     ← the shipping executable
    └── Content/
```

The tell is `Engine/Binaries/ThirdParty/Ogg/Win64`. That is where the proxy
DLL goes, and a title that ships no `libogg` cannot take this fix at all — the
app says so rather than guessing.

Dropping the Steam library folder works too; the app looks one level down.

Note that Steam names install directories after the project, not the game.
Mortal Shell 2 lives under `Sparta`, Reunion under `LifeisStrangeReunion`, and
their executables are `MortalShell2-Win64-Shipping.exe` and
`Iris-Win64-Shipping.exe` respectively. Browse by path rather than by the name
on the store page — and since the app checks the executable against the title
you picked, it will tell you when the two disagree.

### While it runs

The app backs everything up first and has a **Revert** button. It shows a
progress bar and streams the underlying scripts' output live, so you can see
which file it is working on rather than staring at a frozen window.

Once the fix is applied, **Apply Fix** is disabled until you revert. Applying
twice would move the proxy DLL aside as though it were the game's own and lose
the original.

Because the app is signed ad-hoc rather than notarised, macOS will refuse the
first launch. Right click it and choose **Open**, then confirm.

## Requirements

- Apple Silicon Mac, macOS 14 or later
- CrossOver 26.2 / 26.3

For **DYNASTY WARRIORS: ORIGINS**, additionally:


Not optional there. Without it `MFCreateSourceReaderFromByteStream` on a
`.webm` fails outright and no frame is ever decoded for the bridge to carry.

than assumed. Mortal Shell 2 and Life is Strange: Reunion were both played on
same version, differing only in the GStreamer plugins — and the fix worked
either way.

The mechanism agrees: VP9 never goes through Media Foundation in Mortal Shell
(Electra decodes it with its own bundled libvpx, and only the *output*
conversion was broken, which is what the patch reroutes), and the Life is
Strange freeze is in DXGI, nowhere near video.

`diagnostics/launch-with.sh` is what makes that comparison possible — bottles
present or absent without reinstalling anything.

## DYNASTY WARRIORS: ORIGINS

The game decodes VP9 with Media Foundation on a D3D11 device kept only for
video, and draws with a D3D12 renderer. Five things stop that under D3DMetal,
and each hides the next:

1. `ID3D11VideoDevice` and `ID3D11VideoContext` are not implemented, and the
   player refuses to start without both.
2. It asks the source reader to decode into D3D video textures, which
   D3DMetal cannot produce.
3. It requires a video processor advertising `DEINTERLACE_BOB`, and returns
   `E_FAIL` when none does.
4. It can only present a sample backed by a D3D texture — it queries the
   buffer for `IMFDXGIBuffer` and has no path for anything else.
5. It hands that texture to its D3D12 renderer by shared handle, and
   `IDXGIResource::GetSharedHandle` returns **`E_NOTIMPL`**. Nothing written
   on the D3D11 side can be seen by D3D12, so the video quad samples a texture
   nobody ever wrote.

The fix supplies the interfaces, moves decoding to software, and carries the
frame across the gap itself: a handle of ours where D3DMetal refuses,
recognised again at `ID3D12Device::OpenSharedHandle`, answered with a texture
created on the game's own D3D12 device and filled each frame over a copy queue
with a fence wait.

It never modifies the game's code. One DLL is renamed and one is added:

```
libxess.dll       <- the bridge, forwards all 27 exports to the original
libxess_real.dll  <- the game's own, untouched
```

`libxess` is Intel's XeSS upscaler. It carries the fix because the game loads
it directly and it has nothing to do with video, so a proxy in front of it
cannot disturb what it does.

```bash
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS"
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --restore
```

The [wiki](../../wiki/Games) has the full account, including the eight
hypotheses that were wrong on the way there.

---

## Mortal Shell 2: the cutscene crash

### The crash

A few seconds after launch, the game dies with:

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000000
...!FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer()
   [Engine\Plugins\Media\ElectraCodecs\...\WindowsElectraDecoderGPUBufferHelpers.h:276]
...!FVideoDecoderVPxElectra::ConvertDecodedImageToNV12orP010()
Crash in runnable thread ElectraPlayer::Video decoder
```

If your crash names a different function, this tool will not help you.

### Root cause

Apple's D3DMetal does not implement `ID3DDestructionNotifier`. Unreal asks for
it and uses the result without checking the HRESULT — line 276 of that header is:

```cpp
TRefCountPtr<ID3DDestructionNotifier> Notifier;
Res = Resource->QueryInterface(__uuidof(ID3DDestructionNotifier), ...);
check(SUCCEEDED(Res));                              // compiled out in Shipping
Res = Notifier->RegisterDestructionCallback(...);   // null vtable deref
```

`QueryInterface` returns `E_NOINTERFACE`, `Notifier` stays null, the `check()`
does not exist in a shipping build, and the next line reads address 0.

#### Why only VP9, and only D3D12

Every Electra decoder gates the D3D12 output buffer pool on the same condition.
The difference is who guards it:

| Decoder | Guard | On D3D12 |
|---|---|---|
| H.264 / H.265 | `if (… && CVarElectraWindowsH264UseOldOutputPath == 0)` | set the CVar to `1` and it never touches the pool |
| VPx (VP8/VP9) | none — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)` | always enters it → crash |

So VP9 always hits it on D3D12. Titles that run on D3D11 (Persona 5 Strikers,
for one) never do: the same bug is there, just unreachable.

**There is no VPx CVar.** Extracting every string from the shipped executable
turns up only `Electra.Win.H264UseOldOutputPath` and
`Electra.Win.H265UseOldOutputPath`. VP9 on D3D12 has no configuration escape.

### The fix

A small proxy DLL patches Electra in memory as the game starts, so its VPx
decoder takes the same CPU output path that every D3D11 machine already uses.
Your original VP9 cutscenes play, untouched: nothing the game ships is edited,
it costs 74 KB and a second to apply, and D3D12 stays active so you keep PSO
precompilation. (`-dx11` also dodges the crash, but Unreal does not precompile
PSOs on the D3D11 RHI, which means permanent shader-compilation stutter.)

It does not survive Steam's **verify integrity of game files** — that puts
every original back, including the DLL we moved aside. Re-apply afterwards.

#### The re-encode mode is gone

Earlier releases could transcode the cutscenes to H.264 and drop the VP9
originals from the `.pak` index. The runtime patch replaces it completely and
is better on every axis, so that mode has been removed rather than left as a
trap: it took twenty minutes, needed ffmpeg and a gigabyte, softened the
picture, and edited files the game shipped.

**If you applied it with an older release, the app still detects it and offers
to undo it**, because a patched pak index and a `Movies_VP9_backup` folder
cannot be unwound any other way short of letting Steam re-download the game.
Undo it, then apply the runtime patch.

---

## Life is Strange: Reunion and Double Exposure

A different fault entirely, in the same DLL. The game runs correctly and then
freezes — after a while, anywhere, with no crash and nothing in any log.

**It is not a deadlock.** A spindump taken while it was stuck shows the
GameThread burning 1.27 seconds of CPU across 128 samples while RenderThread 0
used four milliseconds. One thread pinned, everything else starving behind it.

Unreal walks the adapter's memory nodes through
`IDXGIAdapter3::QueryVideoMemoryInfo`, accumulating across them, and ends the
walk when the call fails. On Windows it fails once the index passes the number
of nodes. **D3DMetal answers `S_OK` for every index**, so the counter climbs
forever — two hundred million iterations a second, measured.

Refusing node 1 with `DXGI_ERROR_INVALID_CALL` ends it. The refusal fires
**once** per session: Unreal takes the node count from that answer and never
asks again, and the polling rate settles at around 2,500 a second.

The full write-up, including the disassembly of the loop and the three wrong
turns that came first, is on the wiki:
[Reunion](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Reunion) ·
[Double Exposure](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Life-is-Strange-Double-Exposure).

**This one is not about these games.** Any UE5 title on the D3D12 RHI makes
that walk, so the guard is worth trying on any Unreal game that freezes this
way. It was kept as a per-game fix rather than a CrossOver-wide `dxgi.dll`
override deliberately — patching one game's process is a much smaller blast
radius than replacing a DLL every bottle loads.

A separate win came out of the same work: serving repeat queries from a 100 ms
cache was reported as noticeably better frame rates, independent of the freeze.
Thousands of crossings a second into Wine's unix side cost more than their wall
time, which is contention rather than cycles.

---

## Using the scripts directly

Every script is standalone and reversible.

### DYNASTY WARRIORS: ORIGINS

```bash
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS"            # install
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --status   # report
runtime/install-dwo-bridge.sh "/path/to/steamapps/common/DWORIGINS" --restore  # remove
```

It expects `libxess.dll` and `pe.py` beside it. To build your own:

```bash
runtime/build-proxy.sh "/path/to/DWORIGINS/libxess.dll" dwo-video-bridge.c
```

### Unreal: runtime patch

```bash
runtime/install-runtime-fix.sh "/path/to/<Game>/Content"            # install
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --status   # report
runtime/install-runtime-fix.sh "/path/to/<Game>/Content" --restore  # remove
```

It expects `libogg_64.dll` and `pe.py` beside it. The release ships a
prebuilt DLL; to build your own you need
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases):

```bash
runtime/build-proxy.sh "/path/to/<Game>/Engine/Binaries/ThirdParty/Ogg/Win64/VS2015/libogg_64.dll"
```

### Unreal: undoing an older release's re-encode

Only for a copy an earlier version transcoded. Both steps are no-ops on a game
that never had it applied.

```bash
scripts/pak-hide-videos.py ".../Content/Paks/pakchunk0-Windows.pak" --restore
scripts/transcode-movies.sh "/path/to/<Game>/Content" --restore
```

`pak-hide-videos.py` is pure Python 3 with no dependencies and runs on the
interpreter macOS already ships. The forward direction of both scripts is no
longer offered by the app.

---

## How it works

### The runtime patch

Electra decides whether to use the D3D12 buffer pool by comparing the D3D
version against 12000 — `bUseGPUBuffers = (PlatformDevice && PlatformDeviceVersion >= 12000)`.
The compiler turns that into:

```asm
cmp dword [rbp+disp], 12000     ; 81 7D xx E0 2E 00 00
jl  <cpu path>                  ; 7C xx   or   0F 8C xx xx xx xx
```

We raise the 12000 to `INT_MAX`, so the comparison always takes the CPU branch.
Four bytes per site, four sites in this build. Nothing else is touched: the
decoder still decodes VP9 with its own libvpx, and Unreal presents the frames
the way it does on any D3D11 machine.

The compare has to be against a **stack slot** (`81 7D` / `81 BD`). The H.264
and H.265 decoders compare a register instead, and they already have a way out
through `Electra.Win.H264UseOldOutputPath` — leaving them alone keeps this
change to the one decoder that has no other option.

It is a pattern scan, not a table of offsets, because a game update moves
everything: between two builds of Mortal Shell 2 the crash site alone shifted by
`0x2C70`. If the pattern does not match, nothing is written and the log says so.

### Getting the patch into the process

The game has no plugin hook, so the patch rides in on a DLL the engine already
loads. `libogg_64.dll` is a good carrier: every Unreal title ships it, it loads
before any cutscene, its ABI has been frozen for years, and it has nothing to do
with rendering — so a proxy in front of it cannot disturb the renderer.

```
libogg_64.dll    <- our proxy, 72 KB
libogg_real.dll  <- the game's original, renamed, untouched
```

The proxy re-exports all 64 symbols as PE **forwarders** straight to
`libogg_real`, so the Windows loader resolves them on demand and no thunk code
of ours ever runs. The only thing we get is `DllMain`, which starts a thread and
applies the patch.

The installer refuses to run if the game's `libogg` exports anything the shipped
proxy does not forward — a missing entry point would stop the game from starting
at all, so it is better to fail early and ask for a rebuild.

### What the pak patch did

Kept because the undo path still relies on it, and because the format notes are
the only public write-up of this that we know of.

It never touches file data. It rewrites the `FullDirectoryIndex` without the
`.mp4` entries, sets `bReaderHasPathHashIndex = 0` so the engine consults only
the directory index (which avoids having to reimplement UE's path hash),
recomputes the SHA-1s, and **appends** the new index at the end of the file with
an updated footer. Undoing it is a plain truncate back to the original size,
recorded in a small JSON file beside the pak.

It validates the index hash before writing anything, refuses to run twice, and
rejects encrypted indexes and pak versions other than 11.

Once an entry is gone the engine falls through to disk, because
`FPakPlatformFile::IsNonPakFilenameAllowed` does not exclude `.mp4`.

## Other games

Neither fault is specific to the title it was found on.

The Unreal crash is in `ElectraMediaVPxDecoder`, which is engine code, so any
UE5 title with VP9 cutscenes on D3D12 hits it — same stack, same address,
different offsets.

The Dynasty Warriors fault is not about that game either: it is what happens
to *any* game that decodes video on a D3D11 device and presents it with a
D3D12 renderer, because `GetSharedHandle` is `E_NOTIMPL` under D3DMetal for
all of them.

What is specific in both cases is only the **carrier** — the DLL the fix rides
in on. `libogg_64.dll` for Unreal titles, `libxess.dll` for DYNASTY WARRIORS:
ORIGINS. Adding a game means finding a DLL it loads directly that has nothing
to do with rendering, and building a proxy for it:

```bash
runtime/build-proxy.sh "/path/to/game/<carrier>.dll" dwo-video-bridge.c
```

`diagnostics/survey-games.sh` reports what a game ships and which media API it
uses, which is usually enough to say which of the two faults you are looking
at, or that it is neither.

**Do not use any of this on a game with anti-cheat.** It patches a running
process, which is exactly the behaviour anti-cheat exists to stop.

## Troubleshooting

**Steam's "verify integrity of game files" undoes this.** It puts the game's
own `libogg_64.dll` back and the proxy is gone. Same after a game patch. Just
run the fix again.

**Still crashing in `AllocateBuffer`** — the proxy is not being loaded. Check
what the app reports, and look for `C:\ue5-runtime-fix.log` in the bottle's
`drive_c`: if it does not exist, the DLL never ran.

**Still freezing after a while** — check the same log. The node guard writes
one line the first time it refuses a node that does not exist, and that line
appearing is what says the fix took effect. If the log has the Electra lines
but not that one, the game is not making the adapter-node walk and the freeze
is something else; the [wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki/Diagnosing-a-new-game)
covers how to look.

**A game with no `libogg`** — the fix has no way in. Nothing in this repository
helps that title yet.

## Things that do not work

Documented so nobody spends an evening rediscovering them.

- **Registry keys or environment variables for D3DMetal.** D3DMetal reads no
  registry keys at all and has 27 `D3DM_*` environment variables, none relevant.
  The `IID_ID3DDestructionNotifier` GUID does not appear in the framework binary
  (control GUIDs such as `IID_ID3D12Device` do, so the test is sound). There is
  no switch because the code is not there.

- **GPTK 4.0 beta 2.** Does not fix it. Its `d3d12.dll` does contain the string
  `IID_ID3DDestructionNotifier`, which looks promising — but that DLL carries
  **698** `IID_*` strings (`IID_IAdviseSink`, `IID_IBindCtx`, …). It is a generic
  COM name table used for diagnostic logging, not an implementation. Tested
  in-game: identical crash, same address.

- **A proxy `d3d12.dll`.** On GPTK 3 it is structurally impossible: every export
  is a trampoline (`mov gGFXTDispatch+N, rax ; jmp rax`) into a table the
  D3DMetal core fills at runtime, and only in the module it recognises as
  `d3d12.dll`. GPTK 4 turned those into real functions (`.text` grew from 854 to
  7238 bytes) so a proxy becomes possible in principle — except D3DMetal refuses
  to initialise under any other module name, giving `ERROR_DLL_INIT_FAILED`.

- **WebMMedia.** Unreal ships a second VP8/VP9 player, wholly independent of
  Electra, and it is compiled into the game. Its factory accepts only the
  `.webm` extension while Unreal asks for `.mp4`, so it never gets a look.
  Remuxing every video to WebM losslessly: game boots, no crash, no video.

- **Patching vkd3d.** vkd3d-proton added `ID3DDestructionNotifier` in v2.14
  precisely for games that expect it, and backporting it onto CrossOver's
  vkd3d 1.18 builds and loads cleanly. It is simply not on the path — D3D12 is
  served by D3DMetal here. Useful only if your D3D12 goes through vkd3d.

- **DXMT.** Its D3D12 exists (`-Denable_d3d12=true`, off by default) but is
  early: 8787 lines against 18621 for its mature D3D11. Built against CrossOver,
  the game does not launch. Worth revisiting as it matures — being open source,
  it is the one path where this could be fixed properly for everyone rather than
  per title.

### For anyone building DLLs for CrossOver

Wine distinguishes builtin from native DLLs by a 32-byte signature at offset
`0x40` of the DOS stub — the string `Wine builtin DLL`, checked in
`dlls/ntdll/loader.c`. `winebuild` writes it; **llvm-mingw does not**. Without
it Wine will not load your DLL as a builtin, and you will chase phantom errors.
DXMT's meson handles it for you.

---

## Building the app

```bash
app/build-app.sh
```

Needs Xcode's Swift toolchain. Produces `app/MacGameVideoFix.app`, ad-hoc
signed, targeting arm64 macOS 14+.

## License

[GPL-3.0-or-later](LICENSE). The tooling here exists because Wine, vkd3d and
DXMT are free software that can be read and modified — copyleft keeps any
derivative of this work equally available.

## The staged codecs, and where they come from

Persona 5 Strikers needs a VC-1 decoder CrossOver does not ship. **We do not
distribute one.** `runtime/stage-codecs.sh` borrows it from the official
GStreamer you already have installed:

- **[GStreamer](https://gstreamer.freedesktop.org)** — install the *runtime*
  package for macOS. `libgstlibav` in it carries ffmpeg, which decodes VC-1,
  WMV3 and WMA.

The script copies that one plugin and ffmpeg into a directory of its own,
symlinks the GStreamer core to **CrossOver's** copy, and points the bottle at
it with `GST_PLUGIN_PATH`. Nothing is patched and nothing is redistributed.

**The idea is not ours.** [winevideo](https://github.com/Jfishin/winevideo)
does this too — its README describes importing WMV/VC-1 codecs from the user's
official GStreamer install, and it requires GStreamer 1.24.13 for exactly the
titles that need them. What differs is only the mechanism: winevideo patches
the CrossOver installation to make those plugins loadable, and this reaches the
same end with one staged folder and one line of bottle configuration, because
CrossOver's launcher never sets `GST_PLUGIN_PATH` and the bottle's environment
is applied first.

## Credits

- [CrossOver](https://www.codeweavers.com/crossover) by CodeWeavers, and
  [Wine](https://www.winehq.org/) underneath it.
- **[GStreamer](https://gstreamer.freedesktop.org)**, whose official macOS
  build supplies the VC-1 decoder Persona 5 Strikers needs. It is borrowed from
  an installation you already have, never redistributed here.
- **[winevideo](https://github.com/Jfishin/winevideo) by Jfishin.** None of
  this would exist without it. Its patches are where every one of these faults
  was first identified: that Electra will accept NV12 and nothing else, that
  CrossOver censors that format on macOS, that Electra decides in software by
  asking its own platform handle, that a D3D9 surface has to be bridged rather
  than shared. This project reaches several of the same places from inside the
  game process instead of by patching Wine, which is a different trade-off, not
  a better one — and it is only possible because winevideo had already worked
  out what was wrong. Where the two differ most: winevideo works outside the
  game, so it reaches titles protected against tampering, which nothing here
  can.
- [DXMT](https://github.com/3Shain/dxmt) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), whose
  source made the root cause legible.

## Disclaimer

Unofficial community tooling, provided as-is. It modifies files in your game
installation; everything is backed up and reversible, but back up anything you
care about first. Not affiliated with or endorsed by CodeWeavers, Apple, Epic
Games, Cold Symmetry or Playstack.
