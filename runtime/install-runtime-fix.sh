#!/usr/bin/env bash
#
# Install (or remove) the runtime fix: a proxy libogg_64.dll that patches
# Electra's VPx decoder onto its CPU output path as the game starts.
#
# Nothing the game ships is edited. One DLL is renamed and one is added:
#
#   libogg_64.dll   <- our proxy, forwards every symbol to libogg_real.dll
#   libogg_real.dll <- the game's original, untouched
#
# The movies and the .pak are left exactly as Steam delivered them, so the
# original VP9 cutscenes are what actually play.
#
#   install-runtime-fix.sh <Content dir>            install
#   install-runtime-fix.sh <Content dir> --restore  remove
#   install-runtime-fix.sh <Content dir> --status   report what is in place
#
# <Content dir> is the folder containing Movies/, e.g.
#   .../steamapps/common/Sparta/MortalShell2/Content
#
# Part of MortalShell2MacFix — https://github.com/MathiasKowoll/MortalShell2MacFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/libogg_64.dll}"
EXPORTS="$HERE/pe.py"

# The proxy writes this path at runtime; finding it inside a DLL is how we tell
# our file apart from the game's.
MARKER='ue5-vpx-cpupath.log'

usage() { sed -n '3,25p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

CONTENT="$1"
MODE="${2:---install}"

# Content is .../<Game>/Content; Engine/ sits beside <Game>. Walk up rather
# than assume a depth, because not every title nests the same way.
ROOT=""
probe="$CONTENT"
for _ in 1 2 3 4; do
  probe="$(dirname "$probe")"
  if [ -d "$probe/Engine/Binaries/ThirdParty/Ogg/Win64" ]; then ROOT="$probe"; break; fi
done
[ -n "$ROOT" ] || {
  echo "error: could not find Engine/Binaries/ThirdParty/Ogg/Win64 above" >&2
  echo "       $CONTENT" >&2
  echo "       This game does not ship libogg, so the runtime fix has no way in." >&2
  exit 1
}

# The VS20xx subfolder name changes between engine versions.
OGG=""
for d in "$ROOT"/Engine/Binaries/ThirdParty/Ogg/Win64/*/; do
  [ -f "$d/libogg_64.dll" ] && { OGG="${d%/}"; break; }
done
[ -n "$OGG" ] || { echo "error: no libogg_64.dll under $ROOT/Engine/Binaries/ThirdParty/Ogg/Win64" >&2; exit 1; }

LIVE="$OGG/libogg_64.dll"
REAL="$OGG/libogg_real.dll"

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

status() {
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken       # proxy present, original missing
  else echo absent
  fi
}

case "$MODE" in
--status)
  echo "$(status) $OGG"
  exit 0
  ;;

--restore)
  state="$(status)"
  if [ "$state" = absent ]; then
    echo "the runtime fix is not installed, nothing to do"
    exit 0
  fi
  if [ "$state" = broken ]; then
    echo "error: $LIVE is our proxy but $REAL is missing." >&2
    echo "       Verify the game files in Steam to get the original back." >&2
    exit 1
  fi
  echo "[1/2] restoring the original libogg_64.dll"
  mv -f "$REAL" "$LIVE"
  rm -f "$OGG/libogg_64.dll.orig"
  echo "[2/2] done — the game is back to stock"
  ;;

--install|"")
  [ -f "$PROXY" ] || { echo "error: proxy DLL not found at $PROXY" >&2; exit 1; }

  if [ "$(status)" = installed ]; then
    echo "the runtime fix is already installed, nothing to do"
    exit 0
  fi

  echo "[1/4] checking $(basename "$LIVE")"
  # A game update or a Steam verification puts the stock DLL back, which means
  # any libogg_real.dll left over is stale. Refresh it from whatever is
  # genuinely original right now -- same reasoning as the movie backup.
  if is_ours "$LIVE"; then
    echo "error: $LIVE is already a proxy but $REAL is gone." >&2
    echo "       Verify the game files in Steam, then run this again." >&2
    exit 1
  fi

  echo "[2/4] checking the proxy exports everything the game imports"
  # If the game ships a libogg we do not fully forward, the process would fail
  # to start with a missing-entry-point error. Better to refuse now.
  missing="$(comm -23 <(python3 "$EXPORTS" exports "$LIVE" | sort) \
                      <(python3 "$EXPORTS" exports "$PROXY" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this game's libogg exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    echo "       Rebuild it: runtime/build-proxy.sh \"$LIVE\"" >&2
    exit 1
  fi

  echo "[3/4] moving the original aside as libogg_real.dll"
  mv -f "$LIVE" "$REAL"

  echo "[4/4] installing the proxy"
  cp "$PROXY" "$LIVE"

  echo
  echo "installed — the original VP9 cutscenes will play as shipped"
  echo "a log is written to the bottle's C:\\${MARKER} on each launch"
  ;;

*)
  usage
  ;;
esac
