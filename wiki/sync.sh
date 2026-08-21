#!/usr/bin/env bash
#
# Push these pages to the GitHub wiki.
#
# The wiki is a separate repository, so the pages live here -- versioned and
# reviewable next to the tooling they describe -- and get mirrored across.
# Edit the files in this folder, not the wiki, or the next sync overwrites you.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REMOTE="${WIKI_REMOTE:-https://github.com/MathiasKowoll/MacGameVideoFix.wiki.git}"
MESSAGE="${1:-sync from the repository}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone -q "$REMOTE" "$WORK" || {
  echo "error: could not clone $REMOTE" >&2
  echo "       A wiki with no pages does not exist as a repository yet." >&2
  echo "       Create the first page from the web once, then run this again." >&2
  exit 1
}

# README.md documents this folder; the wiki has no use for it.
#
# Links are written as [Games](Games.md) so they work when the pages are read
# in the repository. The wiki resolves that to the raw file instead of the
# page, so strip the extension on the way across -- but only for links to a
# bare filename, never for anything with a slash in it.
for page in "$HERE"/*.md; do
  [ "$(basename "$page")" = "README.md" ] && continue
  sed -E 's/\]\(([^)/]+)\.md\)/](\1)/g' "$page" > "$WORK/$(basename "$page")"
done

cd "$WORK"
if git diff --quiet && git diff --cached --quiet && [ -z "$(git status --porcelain)" ]; then
  echo "already up to date"
  exit 0
fi

git add -A
git commit -q -m "$MESSAGE"
git push -q origin HEAD
echo "pushed: $(git log --oneline -1)"
