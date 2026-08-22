#!/usr/bin/env bash
#
# Add (or remove) the .webm byte-stream handler mapping in a bottle.
#
#     apply-webm-handler.sh <bottle> [engine]      add it
#     apply-webm-handler.sh <bottle> [engine] --check   report only
#
# Written to test one question: on CrossOver Preview, is this single mapping
# the whole of what winevideo provides for a WebM/VP9 game? Preview already
# carries the demuxer and a hardware VP9 decoder; a clean bottle has zero
# extensions mapped to the handler, while a bottle winevideo has touched has
# three (.mkv, .msd, .webm).
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u
BOTTLE="${1:-}"
ENGINE="${2:-CrossOver Preview}"
[ -n "$BOTTLE" ] || { sed -n '3,16p' "$0" >&2; exit 1; }

B="$HOME/Library/Application Support/CrossOver/Bottles/$BOTTLE"
[ -d "$B" ] || { echo "error: no bottle named $BOTTLE" >&2; exit 1; }

report() {
  if grep -qi 'ByteStreamHandlers\\\\\.webm' "$B/system.reg" 2>/dev/null; then
    echo "  .webm handler: PRESENTE"
  else
    echo "  .webm handler: ausente"
  fi
}

echo "bottle: $BOTTLE"
report
[ "${3:-}" = "--check" ] && exit 0

APP=""
for root in /Applications "$HOME/Applications"; do
  [ -d "$root/$ENGINE.app" ] && APP="$root/$ENGINE.app" && break
done
[ -n "$APP" ] || { echo "error: no $ENGINE.app" >&2; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"

# regedit needs two things this did not give it, and gave no sign of missing:
# a Windows path, and /s. Without /s it opens an import dialog nobody is there
# to click, and a Unix path is not something it resolves. The bottle maps z: to
# the filesystem root, so the file is reachable as Z:\...\ with backslashes.
WINPATH="Z:$(printf '%s' "$HERE/webm-bytestream-handler.reg" | tr '/' '\\')"
echo "applying with $ENGINE ..."
echo "  importing $WINPATH"
"$APP/Contents/SharedSupport/CrossOver/bin/wine" --bottle "$BOTTLE" \
  --cx-app 'C:\windows\regedit.exe' /s "$WINPATH" 2>&1 | grep -viE 'msync|^$' | sed 's/^/  /'

# The bottle writes its registry lazily; a running wineserver may not have
# flushed yet, and reporting before it does would say "absent" about something
# that landed.
for _ in 1 2 3 4 5 6; do
  grep -qi 'ByteStreamHandlers\\\\\.webm' "$B/system.reg" 2>/dev/null && break
  sleep 2
done
report
