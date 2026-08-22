Source for the [project wiki](https://github.com/MathiasKowoll/MacGameVideoFix/wiki),
kept in the repository so it is versioned and reviewable alongside the tooling
it documents.

Edit the files here, not the wiki — `sync.sh` overwrites the wiki with these.

```bash
wiki/sync.sh "what changed"
```

`README.md` stays here; the wiki does not need it.

`sync.sh` strips the `.md` from a link to a bare filename, so `[Games](Games.md)`
works both here and on the published wiki. It strips nothing when an anchor
follows, so `[Findings](Findings.md#a-heading)` reaches the wiki unchanged and
resolves to the raw file rather than the page. Until the expression covers that
case, link to the page and name the section in the sentence instead.

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

The `CrossOver` column is generated too. Its cells say which builds a title was
measured on, never which it might work on, and "Preview" in them means
`crossover-preview-arm64-20260821` — the preamble on each page says so, and the
cells stay short on purpose.

One thing to keep when editing `NOTE`: it writes `--` where the hand-written
prose around it uses `—`, and the two render side by side on the published page.

This file is not synced to the wiki. It is a note for whoever edits the
repository, and nothing in it reaches a reader of the published pages.
