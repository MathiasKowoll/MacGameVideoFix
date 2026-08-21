#!/usr/bin/env bash
#
# Does this game actually use winevideo?
#
# Media Foundation is delay-loaded by most Unreal titles, so the import table
# only says the code *can* reach it, never that it does. This looks at a
# running process instead: if winegstreamer and the GStreamer VP9 plugins were
# never mapped in while a cutscene played, then winevideo is not on that game's
# path and the fix does not depend on it.
#
# Run the game, get to a cutscene, and then:
#
#     diagnostics/check-winevideo-use.sh MortalShell2
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -u
PAT="${1:-Win64-Shipping.exe}"
PID=$(pgrep -f "$PAT" | head -1)
[ -z "$PID" ] && { echo "no encuentro un proceso que coincida con '$PAT'."; echo "procesos wine vivos:"; pgrep -fl '\.exe' | head; exit 1; }

echo "pid $PID  ($(ps -o comm= -p "$PID" | xargs basename 2>/dev/null))"
echo

MAPS=$(lsof -p "$PID" 2>/dev/null)

report() {
  local label="$1" pattern="$2"
  local hits
  hits=$(printf '%s\n' "$MAPS" | grep -icE "$pattern")
  if [ "$hits" -gt 0 ]; then
    printf '  %-28s CARGADO (%s)\n' "$label" "$hits"
    printf '%s\n' "$MAPS" | grep -ioE "/[^ ]*($pattern)[^ ]*" | sort -u | sed 's/^/      /'
  else
    printf '  %-28s no cargado\n' "$label"
  fi
}

echo "componentes de winevideo:"
report "winegstreamer"    'winegstreamer'
report "plugins gstreamer" 'libgstvpx|libgstmatroska|libgstreamer'
echo
echo "media foundation:"
report "mfplat"           'mfplat'
report "mfreadwrite"      'mfreadwrite'

echo
if printf '%s\n' "$MAPS" | grep -qiE 'winegstreamer|libgstvpx'; then
  echo "=> este juego SI pasa por winevideo. El arreglo lo necesita."
else
  echo "=> nada de winevideo esta mapeado en este proceso."
  echo "   Si acabas de ver una cinematica, el arreglo NO depende de winevideo."
  echo "   (Comprueba que la cinematica ya haya reproducido antes de fiarte)."
fi
