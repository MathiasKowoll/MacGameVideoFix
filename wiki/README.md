Source for the [project wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki),
kept in the repository so it is versioned and reviewable alongside the tooling
it documents.

Edit the files here, not the wiki — `sync.sh` overwrites the wiki with these.

```bash
wiki/sync.sh "what changed"
```

`README.md` stays here; the wiki does not need it.

## One part of these pages is generated

Everything between the `games:begin` and `games:end` comment markers in
`Home.md` and `Games.md` belongs to `games.py`, not to the pages. The table
appears twice and the two copies drifted apart before, which is why it is
injected rather than kept per page. (The markers are not written out here on
purpose: `games.py` would treat this file as a page to rewrite.)

```bash
wiki/games.py            # rewrite both pages from its constants
wiki/games.py --check    # fail if a page has drifted
```

The rows are the `GAMES` list, the header row is `HEAD`, and the prose beneath
the table is `NOTE`. Editing any of those three by hand in a `.md` file works
until the next run of `games.py`, which then discards it.

**`games.py` is unsafe to run until its constants are updated.** `Games.md` and
`Home.md` are ahead of it: both carry a `CrossOver` column the generator does
not emit, and a `NOTE` that has been corrected several times since. The
generator still holds the withdrawn version — that DYNASTY WARRIORS: ORIGINS
needs winevideo on a stable build, which measurement has overtaken (what stable
lacks is a WebM demuxer, not a VP9 decoder), and that CrossOver's own VP9 and
AAC decoding is established on stable, which it is not. `games.py --check`
fails today for exactly that reason.

The two injected blocks are byte-identical between the pages, so the repair is
a single copy: take the block from `Games.md` between the markers, and put its
header row into `HEAD`, its rows into `GAMES` and the prose beneath into
`NOTE`. Two things to keep while copying — the `CrossOver` column, and the em
dashes, since `NOTE` writes `--` where the hand-written prose around it uses
`—` and the two render side by side on the published page. Do that before
running `games.py`, or the run restores the withdrawn claims over the top of
the corrected ones.

Note also that this file is not synced to the wiki, so this warning reaches
nobody reading the published pages. It is a note for whoever edits the
repository.
