# MacGameFix

A small macOS application that installs, checks and removes the **game-folder
fixes** of [MacGameVideoFix](https://github.com/MathiasKowoll/MacGameVideoFix):
the ones that live entirely inside a game's own folder.

None of them changes CrossOver. They work with CrossOver as CodeWeavers ships
it, with CrossOver Preview, and with Steam Play tools for macOS such as
[NotProton](https://github.com/NotProtonNot/NotProton). That is the whole point
of this app: the fixes that need a patched engine — a staged GStreamer codec, a
`winegstreamer` carrying winevideo's patches or ours — stay in MacGameVideoFix,
and nothing here depends on them.

It is published as its own repository, where the built application and its
release notes live:

> <https://github.com/MathiasKowoll/MacGameFix>

## What it does

- **Finds the games.** It reads the libraries of the native macOS Steam (where a
  Steam Play tool installs Windows games), the libraries of the Steam inside
  each CrossOver bottle, and any folder you add. Only the supported games below
  are ever shown; nothing else in your libraries is named.
- **Says what is installed**, for each game, by asking that game's installer.
- **Installs or removes** the fix for one game or several at once. Removing puts
  the game's own files back.

Quit a game before installing or removing its fix.

## The games

| Game | What the fix is for | Needs |
| --- | --- | --- |
| CONTROL Resonant | Lit rooms behind windows drawn in flat colours | D3DMetal |
| God of War | White specks on characters in dim light | DXMT |
| KINGDOM HEARTS Dream Drop Distance | Cutscenes with a solid green picture; a crash dialog on exit | D3DMetal, a prefix |
| KINGDOM HEARTS HD 1.5+2.5 ReMIX | The same, for the six titles of the package | D3DMetal, a prefix |
| Life is Strange: Double Exposure | Freezes after a while | D3DMetal 4 |
| Life is Strange: Reunion | Freezes after a while | D3DMetal 4 |
| Marvel's Spider-Man 2 | T-pose, a broken symbiote, black blocks in the sandstorm | D3DMetal |
| Mortal Shell 2 | Crash on the first cutscene | D3DMetal |
| NINJA GAIDEN 4 | Says the VP9 codec is missing, then exits; on D3DMetal 4, nothing is ever drawn | D3DMetal 3 or 4.0b3, CrossOver Preview 27 |
| Nioh 3 | "Failed to play movie" | D3DMetal |
| Persona 5 Strikers | Video never starts; sound only | DXMT |
| RESONANCE: A Plague Tale Legacy | "Shader Model 6.7 is not supported" | D3DMetal |
| TMNT: Splintered Fate | Opens a window, then closes | D3DMetal |
| The Witcher 3: Wild Hunt | Black screen at start; DLSS said to be unsupported | D3DMetal |
| Tormented Souls 2 | Fatal error before the first frame | D3DMetal |
| Wo Long: Fallen Dynasty | Cutscene with sound and a black picture | D3DMetal |

The list is not written by hand. The build takes every installer in
MacGameVideoFix that declares a game-folder fix, and keeps the games for which
MacGameVideoFix's own table records no engine requirement, and either no codec
or a CrossOver that ships that codec as CodeWeavers distributes it.

**Measured, and what was not.** Every fix here was measured on a CrossOver 26.3
that carried MacGameVideoFix's engine patches, which do not touch these fixes;
Spider-Man 2 and God of War on CrossOver Preview 27 through NotProton.
For Wo Long, Nioh 3 and KINGDOM HEARTS Dream Drop Distance, that the fix needs
nothing of the engine is read off how it works, not established by a run on an
untouched CrossOver.

**Game Porting Toolkit 4.0b3.** On 2026-10-13 and 14, with D3DMetal 4.0b3 and
every fix removed, CONTROL Resonant drew its windows correctly, RESONANCE
started and played, and both Life is Strange titles played without problems:
none of the four needs its fix there. (The Life is Strange freeze was never
reproduced on demand, even on 4.0b2, so for those two the sessions show that
nothing went wrong rather than a fault cured.) Spider-Man 2, The Witcher 3, Tormented Souls 2, TMNT and Wo Long still
needed theirs, and God of War runs on DXMT, which the toolkit does not change. Every
fix stays, for anyone on 4.0b2 and earlier.

### KINGDOM HEARTS needs a prefix

Its fix adds a DLL beside the game and also tells Wine, in the registry of that
game's prefix, to load it. The app finds the prefix itself:

- for a Steam Play tool, `steamapps/compatdata/<appid>/pfx` beside the library;
- for CrossOver, a bottle whose Steam declares the library the game is in.

When both exist it lets you choose. A Steam Play prefix is edited only while
nothing runs in it, and the installer refuses otherwise. The CrossOver route is
the one measured with the game. The Steam Play route was tested on a prefix --
install, check, remove, and Wine reading the key back -- but not yet with
KINGDOM HEARTS itself running from one.

### NINJA GAIDEN 4 needs CrossOver Preview

Its videos are VP9 in WebM. Stable CrossOver 26.3 cannot open a WebM, so there
the game reaches its menu with every video missing; MacGameVideoFix stages the
codec for it, which this app does not do. CrossOver Preview 27 ships what it
needs: its own GStreamer opens the WebM and VideoToolbox decodes the VP9.

The fix itself does three things, all in the game folder. It answers the game's
question about a VP9 decoder, which a Windows install answers with a codec pack.
It renames `dstoragecore.dll`, so DirectStorage is off, and removing the fix
renames it back. And it repairs two faults in how D3DMetal 4 handles D3D12
fences, without which the game runs with sound and never draws a frame.

Measured on 2026-10-14: on CrossOver Preview 27 through NotProton, with D3DMetal
4.0b3 and Metal 4 on, the intro video played and the menu came up. The same
repair, in a diagnostic build, brought the game up on 4.0b3 with Metal 4 on and
off, on a CrossOver carrying MacGameVideoFix's engine. D3DMetal 4.0b2 with the
repair was not run.

### Persona 5 Strikers decodes its own video

Its cutscenes are VC-1 with WMA Pro audio, which no CrossOver decodes. The fix
adds `mgvf-asf-reader.dll` beside the game: a video reader built on FFmpeg
(LGPL; its notice is installed with it), used only when CrossOver cannot open
the video. The game must run on **DXMT**. With a Steam Play tool such as
NotProton, set the launch option `CX_GRAPHICS_BACKEND=dxmt %command%`.

Measured on 2026-10-14 on CrossOver Preview 27 through NotProton, with nothing
else installed: the intro videos play with sound in sync. Stable CrossOver 26.3
without MacGameVideoFix's codec was not run.

### Spider-Man 2 downloads one thing

Its installer rebuilds the fix's four shaders from your own `Spider-Man2.exe`,
with Microsoft's shader tools taken from the release of
[SpiderMan2Fix](https://github.com/macprotips/SpiderMan2Fix). They are downloaded
once, cached in `~/Library/Caches/MacGameVideoFix`, and checked against the
verified result before anything is installed.

## Opening it the first time

The application is signed ad hoc, so macOS blocks the first launch of a
downloaded copy. **Right click > Open** once, and confirm.

## Building it

From a checkout of MacGameVideoFix:

```
app-gamefix/build-app.sh
```

It compiles `MacGameFix.swift`, chooses the games as described above, copies
the installers and every file they name from `runtime/` unchanged, and fails if
the bundle differs from `runtime/` in a single byte.

## Credits

- **[witcher3-crossover-fix](https://github.com/tholtman1-del/witcher3-crossover-fix)
  by tholtman1-del** (MIT): the diagnosis of The Witcher 3's black screen and
  the rule that refuses the pipeline.
- **[SpiderMan2Fix](https://github.com/macprotips/SpiderMan2Fix) by macprotips.**
  The Spider-Man 2 fix is theirs; this adds a carrier that needs no DLL override,
  and a barrier after the volumetric fog copy.
- **The God of War fix** comes from a r/macgaming post, "God Of War 2018 White
  Dot Fix (through Crossover)", whose author found the depth test behind the
  white specks.
- Everything else, and the findings behind every fix, is in
  [MacGameVideoFix](https://github.com/MathiasKowoll/MacGameVideoFix) and its
  [wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki).

## License

[GPL-3.0-or-later](https://github.com/MathiasKowoll/MacGameVideoFix/blob/main/LICENSE).
