#!/usr/bin/env bash
#
# Publish a release, with the app in it.
#
#     app/release.sh <version>          publish, e.g. app/release.sh 4.8.1
#     app/release.sh <version> --check  say what it would do, change nothing
#
# WHY THIS EXISTS. Four releases went out in one afternoon with no app
# attached. Each one was correct, tagged, described at length -- and useless to
# anyone who wanted to run it, because the thing being released was not in it.
# Nothing was wrong except that a step lived in somebody's habit instead of in a
# file.
#
# It also refuses to publish over a repository that does not check out. The two
# guards below exist precisely because this project has shipped a binary that
# did not match its source and a table that did not match its rows; running them
# after the release is running them too late.
#
# The zip is built with ditto, which is what preserves a bundle's symlinks and
# its ad-hoc signature. A plain `zip` does not, and an app that arrives with a
# broken signature is one Gatekeeper refuses for a reason the user cannot act on.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
VERSION="${1:-}"
DRY=0
[ "${2:-}" = "--check" ] && DRY=1

[ -n "$VERSION" ] || { sed -n '3,8p' "$0" >&2; exit 1; }
case "$VERSION" in v*) VERSION="${VERSION#v}" ;; esac
TAG="v$VERSION"

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

echo "[1/6] the app says what the tag says"
APP="$ROOT/app/MacGameVideoFix.app"
[ -d "$APP" ] || die "no app bundle at $APP -- run app/build-app.sh"
built="$(defaults read "$APP/Contents/Info" CFBundleShortVersionString 2>/dev/null)"
[ "$built" = "$VERSION" ] || die "the built app is $built, not $VERSION. Bump build-app.sh and run it."
say "app $built"

echo "[2/6] nothing shipped has drifted from its source"
"$ROOT/runtime/check-builds.sh" >/dev/null 2>&1 \
  || die "runtime/check-builds.sh reports drift. Run it to see what, and fix it first."
say "every DLL rebuilds from the source it names; the bundle matches runtime/"

echo "[3/6] the tables and the prose agree"
( cd "$ROOT/wiki" && python3 games.py --check >/dev/null 2>&1 ) \
  || die "wiki/games.py --check fails. Run it to see what, and fix it first."
say "generated tables current, no hand-written counts"

echo "[4/6] packaging"
# ditto, not zip: it keeps the symlinks and the signature.
OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
ZIP="$OUT/MacGameVideoFix-$VERSION.zip"
codesign --verify "$APP" 2>/dev/null || die "the bundle's signature does not verify; not shipping it"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP" || die "could not package the app"
say "MacGameVideoFix-$VERSION.zip  $(( $(stat -f%z "$ZIP") / 1024 )) KB"


if [ "$DRY" = 1 ]; then
  echo
  say "--check: nothing was tagged, pushed or uploaded."
  exit 0
fi

echo "[5/6] tag, push, upload"
git -C "$ROOT" tag -a "$TAG" -m "$TAG" 2>/dev/null || say "$TAG already exists, reusing it"
git -C "$ROOT" push -q origin "$TAG" || die "could not push $TAG"
gh release view "$TAG" >/dev/null 2>&1 \
  || gh release create "$TAG" --title "MacGameVideoFix $VERSION" --notes "See the commit for what changed." >/dev/null \
  || die "could not create the release"
gh release upload "$TAG" "$ZIP" --clobber >/dev/null || die "could not upload the app"

# Built AFTER the tag, not before.
#
# make-fixes-bundle.sh names its output from git describe, so packing it first
# produced fixes-v4.11.1-8-g7a263a7.tar.gz on a release meant to be 4.11.2. That
# was patched once by making this script FIND the tarball instead of assuming
# its name, which removed the error and left the wrong name -- the asset was
# then renamed by hand on two releases, and renaming things by hand is what this
# file exists to stop. The consumer matches on the fixes- prefix so nothing was
# broken; it was just wrong, twice, in a way somebody would eventually rely on.
#
# This step used to live in somebody's habit, exactly like the app once did.
# 4.11.0 went out with the app attached and no bundle, because the guard added
# after four appless releases was written to check for the app and nothing else.
# A checklist that only remembers the last thing that went wrong is a checklist
# with one entry.
"$ROOT/runtime/make-fixes-bundle.sh" "$OUT" >/dev/null \
  || die "could not build the fixes bundle"
# Found rather than named: make-fixes-bundle.sh names its output from git
# describe, so before the tag exists it comes out as fixes-v4.11.0-2-gabc1234
# and an exact name misses it. The tag is written in the next step, not this
# one, so this will always be the case on a first publish.
BUNDLE="$(ls "$OUT"/fixes-*.tar.gz 2>/dev/null | head -1)"
[ -n "$BUNDLE" ] && [ -f "$BUNDLE" ] || die "make-fixes-bundle.sh produced no tarball in $OUT"
[ -f "$BUNDLE.sha256" ] || die "the fixes bundle has no checksum beside it"
say "fixes-v$VERSION.tar.gz  $(( $(stat -f%z "$BUNDLE") / 1024 )) KB"
gh release upload "$TAG" "$BUNDLE" "$BUNDLE.sha256" --clobber >/dev/null \
  || die "could not upload the fixes bundle"
say "$TAG published with the app, the fixes bundle and its checksum"

echo "[6/6] what was published, not what was built"
# Every guard above this line asks "is what I am about to build correct". None of
# them asked "is what I just published correct", and on 5.0.1 that difference
# cost a file: the tag was cut between the commit that put NINJA GAIDEN 3 in the
# app and the commit that put manifest.json there, so the published bundle was a
# subset and nothing said so. check-builds.sh had already passed -- on the tree.
#
# This is the same argument as the read-back in make-engine-copy.sh. A write
# without a read-back rots, and a release process that only writes is a write
# without a read-back with a version number on it.
#
# Read from the uploaded asset, downloaded again, so this cannot pass by looking
# at the zip still sitting in the temp directory it was made in.
VERIFY="$(mktemp -d)"; trap 'rm -rf "$OUT" "$VERIFY"' EXIT
if gh release download "$TAG" -p "MacGameVideoFix-$VERSION.zip" -D "$VERIFY" --clobber >/dev/null 2>&1 \
   && ditto -x -k "$VERIFY/MacGameVideoFix-$VERSION.zip" "$VERIFY/x" 2>/dev/null; then
  # The real bundle. A plain find also matches __MACOSX, the zip's metadata
  # folder, which answers a question nobody asked and answers it wrongly.
  PUB="$VERIFY/x/MacGameVideoFix.app/Contents/Resources"
  if [ -d "$PUB" ]; then
    lost=$(comm -23 \
      <(tar tzf "$BUNDLE" | sed 's|.*/||' | grep -v '^$' | sort -u) \
      <(ls "$PUB" | sort -u))
    if [ -z "$lost" ]; then
      say "the published app carries everything the published fixes bundle does"
    else
      say "PUBLISHED APP IS A SUBSET of the published fixes bundle. Missing:"
      printf '%s\n' "$lost" | sed 's/^/    /'
      say "The release exists and is wrong. Fix and publish again; do not leave it."
      exit 1
    fi
  else
    say "could not find the app inside the published zip -- verify by hand"
  fi
else
  say "could not download the published asset to check it -- verify by hand"
fi
echo
say "Write the notes with: gh release edit $TAG --notes-file <file>"
