#!/usr/bin/env bash
#
# Transcode a UE5 title's VP9 cutscenes to H.264 so Electra takes its guarded
# output path instead of the D3D12 buffer pool that crashes under D3DMetal.
#
# Originals are copied to Movies_VP9_backup/ before anything is written, so
# --restore puts them back.
#
#   transcode-movies.sh <Content dir>            transcode VP9 -> H.264
#   transcode-movies.sh <Content dir> --restore  put the originals back
#
# <Content dir> is the folder containing Movies/, e.g.
#   .../steamapps/common/Sparta/MortalShell2/Content
#
# Part of MortalShell2MacFix — https://github.com/<owner>/MortalShell2MacFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() { sed -n '3,15p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

CONTENT="$1"
RESTORE="${2:-}"
MOVIES="$CONTENT/Movies"
BACKUP="$CONTENT/Movies_VP9_backup"

[ -d "$MOVIES" ] || { echo "error: no Movies/ folder in $CONTENT" >&2; exit 1; }

if [ "$RESTORE" = "--restore" ]; then
  [ -d "$BACKUP" ] || { echo "error: no backup at $BACKUP" >&2; exit 1; }
  total=$(find "$BACKUP" -name '*.mp4' | wc -l | tr -d ' ')
  n=0
  while IFS= read -r -d '' f; do
    rel="${f#"$BACKUP"/}"
    n=$((n + 1))
    mkdir -p "$MOVIES/$(dirname "$rel")"
    cp "$f" "$MOVIES/$rel"
    echo "[$n/$total] restored $rel"
  done < <(find "$BACKUP" -name '*.mp4' -print0)
  echo "originals restored from Movies_VP9_backup"
  exit 0
fi

command -v ffmpeg  >/dev/null || { echo "error: ffmpeg not found. brew install ffmpeg" >&2; exit 1; }
command -v ffprobe >/dev/null || { echo "error: ffprobe not found. brew install ffmpeg" >&2; exit 1; }

# Back up once. If the backup exists we assume a previous run made it.
if [ ! -d "$BACKUP" ]; then
  echo "backing up originals to Movies_VP9_backup/ ..."
  cp -R "$MOVIES" "$BACKUP"
fi

total=$(find "$BACKUP" -name '*.mp4' | wc -l | tr -d ' ')
n=0
converted=0

while IFS= read -r -d '' f; do
  rel="${f#"$BACKUP"/}"
  n=$((n + 1))

  codec=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
                  -of csv=p=0 "$f" 2>/dev/null || true)
  if [ "$codec" != "vp9" ] && [ "$codec" != "vp8" ]; then
    echo "[$n/$total] skip (already $codec) $rel"
    continue
  fi

  mkdir -p "$MOVIES/$(dirname "$rel")"

  # A plain line (no [n/total] prefix) so the GUI shows activity while ffmpeg
  # works on a long file without advancing the bar early.
  echo "        encoding $rel"

  # -write_tmcd 0 matters: the mp4 muxer otherwise re-creates the timecode
  # track from the source, and a third track is enough to stop Electra from
  # presenting video (audio still plays, picture stays black).
  #
  # -tune fastdecode / -bf 0 / -refs 2 keep decoding cheap. Electra's H.264
  # decoder runs in software here, because winevideo's patches make the MFT
  # report no D3D awareness (no macOS backend can create NV12 D3D11 textures).
  ffmpeg -nostdin -v error -y -i "$f" \
    -map 0:v:0 -map "0:a?" -dn -sn -write_tmcd 0 \
    -c:v libx264 -tune fastdecode -pix_fmt yuv420p \
    -preset medium -crf 21 -maxrate 6M -bufsize 12M -refs 2 -bf 0 \
    -c:a copy -movflags +faststart "$MOVIES/$rel"

  out=$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name \
                -of csv=p=0 "$MOVIES/$rel" 2>/dev/null || true)
  if [ "$out" = "h264" ]; then
    echo "[$n/$total] ok   $rel"
    converted=$((converted + 1))
  else
    echo "[$n/$total] FAIL $rel (got '$out')" >&2
  fi
done < <(find "$BACKUP" -name '*.mp4' -print0)

echo
echo "transcoded $converted of $total files"
echo "next: run pak-hide-videos.py --apply so the engine reads these instead of the pak"
