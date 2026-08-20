#!/usr/bin/env bash
# Switch which set of movie files the game sees in Content/Movies.
#
# Pairs with pak-hide-videos.py: once the pak no longer serves the videos, the
# engine reads whatever is on disk, so swapping these folders swaps the codec
# without touching the pak again.
#
#   switch-movies.sh <Content dir> h264     transcoded H.264   (the working fix)
#   switch-movies.sh <Content dir> webm     VP9 remuxed to WebM
#   switch-movies.sh <Content dir> original stock VP9 in .mp4  (needs the pak restored)
#   switch-movies.sh <Content dir> none     nothing            (the game hangs; diagnostic only)
#
# The variant folders live next to Movies/ as Movies_H264, Movies_WEBM and
# Movies_VP9_backup.

set -euo pipefail

usage() { sed -n '4,12p' "$0" >&2; exit 1; }
[ $# -eq 2 ] || usage
CONTENT="$1"
WHICH="$2"
MOVIES="$CONTENT/Movies"

[ -d "$MOVIES" ] || { echo "error: no encuentro $MOVIES" >&2; exit 1; }

case "$WHICH" in
  h264)     SRC="$CONTENT/Movies_H264" ;;
  webm)     SRC="$CONTENT/Movies_WEBM" ;;
  original) SRC="$CONTENT/Movies_VP9_backup" ;;
  none)     SRC="" ;;
  *) echo "error: variante desconocida '$WHICH'" >&2; exit 1 ;;
esac

if [ -n "$SRC" ] && [ ! -d "$SRC" ]; then
  echo "error: falta $SRC" >&2; exit 1
fi

find "$MOVIES" \( -name '*.mp4' -o -name '*.webm' \) -delete

if [ -n "$SRC" ]; then
  ( cd "$SRC" && find . \( -name '*.mp4' -o -name '*.webm' \) -print0 |
    while IFS= read -r -d '' f; do
      mkdir -p "$MOVIES/$(dirname "$f")"
      cp "$f" "$MOVIES/$f"
    done )
fi

printf 'estado: %s\n  mp4 : %s\n  webm: %s\n' "$WHICH" \
  "$(find "$MOVIES" -name '*.mp4'  | wc -l | tr -d ' ')" \
  "$(find "$MOVIES" -name '*.webm' | wc -l | tr -d ' ')"
