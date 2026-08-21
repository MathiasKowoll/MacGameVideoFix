Four tools, each answering a different question. Run them in this order — every
one narrows what the next has to look at.

## 1. What does this game ship, and what plays it?

```
diagnostics/survey-games.sh "/path/to/steamapps/common/<Game>"
```

One row: engine, how many videos, in what container, the codec of a sample, and
which media DLLs the main executable is linked against. Usually enough to say
which failure mode you are looking at, or that the game is not in this category
at all.

Given a whole library instead of one game it surveys everything under it, which
helps when hunting for a title worth working on. The wiki still only covers
games we deliberately took on.

## 2. Does the crash actually apply to this build?

```
runtime/pe.py imports "/path/to/Game-Win64-Shipping.exe" --dlls
```

For an Unreal title, a scan for Electra's D3D12 version check settles it
without launching anything. The pattern is

```
cmp dword [rbp+disp], 12000     ; 81 7D ?? E0 2E 00 00
jl  <cpu path>                  ; 7C ??   or   0F 8C ?? ?? ?? ??
```

Mortal Shell 2 has four. Returnal, on UE4, has none — the buffer pool that
crashes does not exist in that engine version. A count of zero means this
particular bug is not present, whatever else may be wrong.

## 3. What is the game asking for, and what is it getting?

For the silent failure — cutscene reached, screen black, no crash, no log.

```
diagnostics/build-probe.sh "/path/to/game/<carrier>.dll"
```

`mf-probe` replaces entries in the game's import address table and logs each
call with its result: `MFStartup`, `MFCreateFile`, `MFTEnumEx`,
`MFCreateSourceReaderFromByteStream`, `CoCreateInstance` failures, and
`CreateFileW` on video paths. Every hook calls through and returns the real
result unchanged — it watches, it does not intervene.

Hooking the import table beats scanning for byte patterns here: the IAT is a
documented structure, so nothing depends on which compiler built the game or on
things moving between updates.

**Picking a carrier.** The game has no plugin hook, so the probe rides in on a
DLL the game already loads. It must be imported directly, load early, and have
nothing to do with rendering. A shipped third-party library is ideal —
`libxess.dll` for DYNASTY WARRIORS: ORIGINS, `libogg_64.dll` for any Unreal
title. **Never `steam_api64.dll`.**

`build-probe.sh` reads the carrier's export table and generates PE forwarders
for every symbol, so the real library still answers every call. Install by
renaming the game's copy to `<stem>_real.dll` and dropping the probe in its
place. The log goes to the bottle's `C:\mf-probe.log`.

Read an empty log carefully. It means "the game did not ask" only if the probe
also hooked `GetProcAddress` — otherwise it may mean the calls went somewhere
you were not watching.

## 4. Everything, if the game has no usable carrier

```
diagnostics/capture-mf-trace.sh <bottle> 'Z:\path\to\game.exe'
```

Launches through Wine with the Media Foundation channels on and reduces the
trace to the media types and handlers that were refused. Slower, far noisier,
and needs the game launched by hand — but it needs nothing installed into the
game folder.

## A warning

Hooking and patching a running process is what anti-cheat exists to stop. **Do
not point any of this at a game with anti-cheat.** Everything here is for
single-player titles whose cutscenes do not play.
