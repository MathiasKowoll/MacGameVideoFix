# MortalShell2MacFix

Restores the VP9 cutscenes in Unreal Engine 5 games running under CrossOver on
Apple Silicon, without giving up D3D12.

Built and tested against **Mortal Shell 2** (UE 5.6.1) on CrossOver 26.2 with
Game Porting Toolkit 4.0b2, on an M4 Max running macOS 27. The same bug affects
any UE5 title whose cutscenes are VP9, so the tooling is written to be generic.

---

## The crash

A few seconds after launch, the game dies with:

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000000
...!FElectraMediaDecoderOutputBufferPoolBlock_DX12::AllocateBuffer()
   [Engine\Plugins\Media\ElectraCodecs\...\WindowsElectraDecoderGPUBufferHelpers.h:276]
...!FVideoDecoderVPxElectra::ConvertDecodedImageToNV12orP010()
Crash in runnable thread ElectraPlayer::Video decoder
```

If your crash names a different function, this tool will not help you.

## Root cause

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

### Why only VP9, and only D3D12

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

## The fix

Move the cutscenes onto the guarded path: transcode them to H.264, then remove
the originals from the `.pak` index so the engine reads your transcodes instead.

D3D12 stays active, so you keep PSO precompilation. (`-dx11` also dodges the
crash, but Unreal does not precompile PSOs on the D3D11 RHI, which means
permanent shader-compilation stutter.)

---

## Requirements

- Apple Silicon Mac, macOS 14 or later
- CrossOver 26.2 / 26.3, patched with [winevideo](https://github.com/Jfishin/winevideo)
- [ffmpeg](https://ffmpeg.org) — `brew install ffmpeg`
- Roughly 1 GB of free space for the backup and the transcodes

winevideo is not optional. Its patches 0005–0007 are what make Electra's H.264
Media Foundation path work on macOS at all — without them you would be moving
the cutscenes onto a path that is equally broken, just in a different way.

## Quick start

1. Download `MortalShell2MacFix.app` from
   [Releases](../../releases), or build it yourself with `app/build-app.sh`.
2. Create the user `Engine.ini` described below.
3. Open the app, drop the game folder on it, and press **Apply Fix**.

### Which folder to pick

Pick the folder that **contains `Content`** — not `Content/Movies`, and not
your Steam library root.

For Mortal Shell 2 that is `MortalShell2`, the folder inside Steam's `Sparta`
directory:

```
…/steamapps/common/Sparta/MortalShell2      ← drop this one
├── Binaries/
└── Content/
    ├── Movies/          ← the cutscenes
    │   ├── Movie_MortalShellII_OpeningCutscene.mp4
    │   ├── Shells/
    │   └── Tutorials/
    └── Paks/            ← pakchunk0-Windows.pak lives here
```

The tell is simple: the folder you choose must have **both `Content/Movies` and
`Content/Paks`** underneath it. The app needs Movies to transcode and Paks to
patch, so either one alone is not enough.

You can also drop `Content` itself, or the folder one level above — the app
looks one level down for a `Content` directory. What it cannot do is guess from
`Movies` alone, and it will tell you so rather than touch anything:

> That folder has no Content/Movies and Content/Paks inside.

Note that Steam names the install directory after the project, not the game.
Mortal Shell 2 ships under `Sparta`, so browse by path rather than by the name
on the store page.

### While it runs

The app backs everything up first and has a **Revert** button. It shows a
progress bar and streams the underlying scripts' output live, so you can see
which file it is working on rather than staring at a frozen window.

Once the fix is applied, **Apply Fix** is disabled until you revert. Running it
twice would transcode already-transcoded files and overwrite the backup with
H.264 instead of the originals.

Because the app is signed ad-hoc rather than notarised, macOS will refuse the
first launch. Right click it and choose **Open**, then confirm.

### The Engine.ini

At `~/Library/Application Support/CrossOver/Bottles/<BOTTLE>/drive_c/users/crossover/AppData/Local/<GAME>/Saved/Config/Windows/Engine.ini`:

```ini
[SystemSettings]
Electra.Win.H264UseOldOutputPath=1
Electra.Win.H265UseOldOutputPath=1
```

Make it read-only afterwards — Unreal rewrites it:

```bash
chmod 444 ".../Saved/Config/Windows/Engine.ini"
```

## Using the scripts directly

Both scripts are standalone and reversible.

```bash
# 1. Transcode. Originals are copied to Movies_VP9_backup/ first.
scripts/transcode-movies.sh "/path/to/<Game>/Content"

# 2. Hide the pak's video entries so the engine reads the loose files.
scripts/pak-hide-videos.py "/path/to/<Game>/Content/Paks/pakchunk0-Windows.pak"          # list only
scripts/pak-hide-videos.py "/path/to/<Game>/Content/Paks/pakchunk0-Windows.pak" --apply

# Undo, in either order
scripts/pak-hide-videos.py ".../pakchunk0-Windows.pak" --restore
scripts/transcode-movies.sh "/path/to/<Game>/Content" --restore
```

`pak-hide-videos.py` is pure Python 3 with no dependencies and runs on the
interpreter macOS already ships.

---

## How it works

### Why the loose files are not enough

The files under `Content/Movies` are **not** what the game reads. The real
videos live inside `pakchunk0-Windows.pak`, and the pak takes priority over
disk. Transcoding the loose copies alone changes nothing.

Unreal has a `-LookLooseFirst` switch for exactly this, but it is not compiled
into UE 5.6 shipping builds. So the pak index has to be edited.

### What the pak patch does

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

### Why the transcode settings look odd

```
-tune fastdecode -crf 21 -maxrate 6M -bufsize 12M -refs 2 -bf 0 -write_tmcd 0
```

Electra's H.264 decoder runs **in software** here: winevideo's patch 0005 makes
the MFT report no D3D awareness, because no macOS backend can create NV12 D3D11
textures. So decode cost matters more than it normally would, hence
`fastdecode`, no B-frames and a bitrate ceiling.

`-write_tmcd 0` is the subtle one. The mp4 muxer re-creates the source's
timecode track by default, and a third track is enough to stop Electra from
presenting video — audio keeps playing, the picture stays black. Every video
that works has exactly two tracks.

## Troubleshooting

**Steam's "verify integrity of game files" undoes this.** It restores the
original VP9 pak and the crash returns. Same after a game patch. Just run the
fix again.

**Black screen, no audio, game hangs** — the engine found no video files at all.
With nothing to play, the startup movie player waits forever rather than
skipping. Check that `Content/Movies` has the H.264 files under the same names
and subfolder layout as the originals.

**Audio plays, picture stays black** — that one file still has a third track, or
its bitrate is too high for the software decoder. Re-transcode it.

**Still crashing in `AllocateBuffer`** — the pak is still serving VP9:

```bash
python3 -c "d=open('pakchunk0-Windows.pak','rb').read(); print('vp09:',d.count(b'vp09'),'avc1:',d.count(b'avc1'))"
```

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

Needs Xcode's Swift toolchain. Produces `app/MortalShell2MacFix.app`, ad-hoc
signed, targeting arm64 macOS 14+.

## License

[GPL-3.0-or-later](LICENSE). The tooling here exists because Wine, vkd3d and
DXMT are free software that can be read and modified — copyleft keeps any
derivative of this work equally available.

## Credits

- [CrossOver](https://www.codeweavers.com/crossover) by CodeWeavers, and
  [Wine](https://www.winehq.org/) underneath it.
- [winevideo](https://github.com/Jfishin/winevideo) by Jfishin, whose Media
  Foundation patches make the H.264 path work on macOS. This project depends on
  it and does not duplicate it.
- [DXMT](https://github.com/3Shain/dxmt) and
  [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), whose
  source made the root cause legible.

## Disclaimer

Unofficial community tooling, provided as-is. It modifies files in your game
installation; everything is backed up and reversible, but back up anything you
care about first. Not affiliated with or endorsed by CodeWeavers, Apple, Epic
Games, Cold Symmetry or Playstack.
