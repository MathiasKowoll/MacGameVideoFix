Source for the project wiki, kept in the repository so it is versioned and
reviewable alongside the tooling it documents.

GitHub will not accept a push to a wiki that has never had a page, so the
first one has to be created once from the web — **Wiki → Create the first
page** — after which these files mirror to it:

```bash
git clone https://github.com/MathiasKowoll/MortalShell2MacFix.wiki.git /tmp/wiki
cp wiki/Home.md wiki/Games.md wiki/Diagnosing-a-new-game.md /tmp/wiki/
cd /tmp/wiki && git add -A && git commit -m "sync from repo" && git push
```

`README.md` stays here; the wiki does not need it.
