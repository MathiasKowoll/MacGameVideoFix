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
echo "applying with $ENGINE ..."
"$APP/Contents/SharedSupport/CrossOver/bin/wine" --bottle "$BOTTLE" \
  --cx-app regedit.exe "$HERE/webm-bytestream-handler.reg" >/dev/null 2>&1
sleep 2
report
