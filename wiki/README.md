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

## Part of these pages is generated

Three blocks and one file belong to `games.py`, not to the pages that carry
them.
The games table sits between its own `games:begin` and `games:end` comment
markers in `Home.md` and `Games.md` — it appears twice and the two copies
drifted apart before, which is why it is injected rather than kept per page.
`Games.md` carries a second block for the stack table. The repository's own
`README.md` carries a third, the same rows with absolute links because it is
read on the front page rather than inside the wiki; it was kept by hand and fell
a row behind the wiki, so a title that had been fixed, shipped and documented
never reached the table most people see first. `process-features.json` at the
repository root is generated from the same rows as well. (The markers are not
written out in full here on purpose: `games.py` would treat this file as a page
to rewrite.)

```bash
wiki/games.py            # rewrite every page and the JSON from its constants
wiki/games.py --check    # fail if a page or the JSON has drifted
```

`--check` does a second job. It refuses a hand-written count of the title set
anywhere in these pages, in the repository's `README.md`, in
`app/MacGameVideoFix.swift`, or in `games.py` itself:
"six of the nineteen", "three titles need a codec". <!-- count-ok -->
Such a number lives in two places from the moment it is written and only one of
them is kept up to date. Write "the titles marked X in the table" and let the
table do the counting. A count of something local — executables in one package,
modes in one list — is fine: mark that line `<!-- count-ok -->`, so the next
person reading it can see the claim was deliberate. `app/release.sh` runs this
check and stops the release when it fails.

The rows are the `GAMES` list, the header row is `HEAD`, the prose beneath the
table is `NOTE`, and the stack table's header is `STACK_HEAD`. Editing any of
them by hand in a `.md` file works until the next run of `games.py`, which then
discards it.

The `CrossOver` column is generated too. Its cells say which builds a title was
measured on, never which it might work on; the cells stay short on purpose, and
the generated note beneath the table carries what they do not fit. The supported
engine is stable 26.3 and nothing else, so a cell naming any other build is the
record of a run that was made rather than an engine to send a reader to.

One thing to keep when editing `NOTE`: it writes `--` where the hand-written
prose around it uses `—`, and the two render side by side on the published page.

This file is not synced to the wiki. It is a note for whoever edits the
repository, and nothing in it reaches a reader of the published pages.
