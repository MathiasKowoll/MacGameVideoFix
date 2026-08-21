#!/usr/bin/env bash
#
# Install (or remove) the DXGI node guard in a CrossOver build.
#
# This one is not per-game. It replaces Apple's dxgi.dll inside CrossOver with
# a proxy that forwards everything and corrects one call:
# IDXGIAdapter3::QueryVideoMemoryInfo answers S_OK for adapter nodes that do
# not exist, and Unreal's D3D12 renderer walks those nodes until the call
# fails. It never does, so the walk never ends -- one thread pinned and the
# game frozen after a while, wherever it happens to be.
#
#   install-node-guard.sh <CrossOver.app>            install
#   install-node-guard.sh <CrossOver.app> --restore  remove
#   install-node-guard.sh <CrossOver.app> --status   report
#
# Read this before installing:
#
#   * It affects every game in every bottle that uses this CrossOver, not one
#     title. That is the point of it, and also the risk.
#   * It does not need winevideo. It has nothing to do with video decoding.
#   * Modifying the bundle invalidates its code signature, as any CrossOver
#     patch does.
#   * Point it at a copy if you would rather not touch the build you rely on.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/dxgi.dll}"
MARKER='dxgi_real.dll'      # our proxy forwards to this; Apple's does not mention it

usage() { sed -n '3,28p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

APP="${1%/}"
MODE="${2:---install}"

DIR="$APP/Contents/SharedSupport/CrossOver/lib64/apple_gptk/wine/x86_64-windows"
[ -d "$DIR" ] || {
  echo "error: no Game Porting Toolkit DLLs in $APP" >&2
  echo "       Expected $DIR" >&2
  exit 1
}

LIVE="$DIR/dxgi.dll"
REAL="$DIR/dxgi_real.dll"

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

status() {
  if is_ours "$LIVE" && [ -f "$REAL" ]; then echo installed
  elif is_ours "$LIVE"; then echo broken
  else echo absent
  fi
}

case "$MODE" in
--status)
  echo "$(status) $APP"
  exit 0
  ;;

--restore)
  state="$(status)"
  if [ "$state" = absent ]; then echo "not installed, nothing to do"; exit 0; fi
  if [ "$state" = broken ]; then
    echo "error: $LIVE is our proxy but $REAL is missing." >&2
    echo "       Reinstall CrossOver to get Apple's back." >&2
    exit 1
  fi
  echo "[1/2] restoring Apple's dxgi.dll"
  mv -f "$REAL" "$LIVE"
  echo "[2/2] done"
  ;;

--install|"")
  [ -f "$PROXY" ] || { echo "error: proxy not found at $PROXY" >&2; exit 1; }
  if [ "$(status)" = installed ]; then echo "already installed, nothing to do"; exit 0; fi
  if is_ours "$LIVE"; then
    echo "error: $LIVE is already a proxy but $REAL is gone." >&2
    exit 1
  fi

  echo "[1/3] checking the proxy exports everything Apple's does"
  missing="$(comm -23 <(python3 "$HERE/../runtime/pe.py" exports "$LIVE" | sort) \
                      <(python3 "$HERE/../runtime/pe.py" exports "$PROXY" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this GPTK's dxgi exports symbols the proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    exit 1
  fi

  echo "[2/3] moving Apple's aside as dxgi_real.dll"
  mv -f "$LIVE" "$REAL"

  echo "[3/3] installing the guard"
  cp "$PROXY" "$LIVE"

  echo
  echo "installed in $(basename "$APP")"
  echo "every bottle using this CrossOver is affected, which is the point"
  ;;

*)
  usage
  ;;
esac
