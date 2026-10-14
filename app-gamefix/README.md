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
| Nioh 3 | "Failed to play movie" | D3DMetal |
| RESONANCE: A Plague Tale Legacy | "Shader Model 6.7 is not supported" | D3DMetal |
| TMNT: Splintered Fate | Opens a window, then closes | D3DMetal |
| The Witcher 3: Wild Hunt | Black screen at start; DLSS said to be unsupported | D3DMetal |
| Tormented Souls 2 | Fatal error before the first frame | D3DMetal |
| Wo Long: Fallen Dynasty | Cutscene with sound and a black picture | D3DMetal |

The list is not written by hand. The build takes every installer in
MacGameVideoFix that declares a game-folder fix, and keeps the games for which
MacGameVideoFix's own table records no codec and no engine requirement.

**Measured, and what was not.** Every fix here was measured on a CrossOver 26.3
that carried MacGameVideoFix's engine patches, which do not touch these fixes;
Spider-Man 2 and God of War on CrossOver Preview 27 through NotProton.
For Wo Long, Nioh 3 and KINGDOM HEARTS Dream Drop Distance, that the fix needs
nothing of the engine is read off how it works, not established by a run on an
untouched CrossOver.

**Game Porting Toolkit 4.0b3.** On 2026-10-13, with D3DMetal 4.0b3 and every
fix removed, CONTROL Resonant drew its windows correctly and RESONANCE started
and played: neither needs its fix there. Life is Strange: Reunion also played
without its fix, but its freeze was never reproduced on demand, so that says
little. Spider-Man 2, The Witcher 3, Tormented Souls 2 and TMNT still needed
theirs, and God of War runs on DXMT, which the toolkit does not change. Every
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
