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
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROXY="${PROXY_DLL:-$HERE/libogg_64.dll}"
EXPORTS="$HERE/pe.py"

# The proxy writes this path at runtime; finding it inside a DLL is how we tell
# our file apart from the game's. Releases up to 3.2 wrote the other name, and
# both have to be recognised: mistaking our own proxy for the game's original
# would move it aside and destroy the real one.
MARKER='ue5-runtime-fix.log'
LEGACY_MARKER='ue5-vpx-cpupath.log'
# Diagnostic builds ride on the same carrier and are just as much ours. A probe
# is a valid PE with readable exports, so the damaged-file check does not catch
# it: without this it would read as the game's own DLL and be moved over the
# saved original.
PROBE_MARKER='electra-probe.log'
# The Electra H.264 fix rides on the same carrier as the Unreal fix, so the
# installer has to recognise it as ours too.
ELECTRA_MARKER='electra-h264-fix.log'
# The merged fix: all three halves in one file, since they share a carrier.
MERGED_MARKER='ue5-media-fix.log'

usage() { sed -n '3,25p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

CONTENT="$1"
MODE="${2:---install}"

# Accepts either the game folder or something below it, usually Content. Walk
# up rather than assume a depth, because not every title nests the same way --
# and test the folder we were handed before ascending, or being given the game
# folder itself, the obvious thing to pass, fails.
ROOT=""
probe="$CONTENT"
for _ in 1 2 3 4 5; do
  if [ -d "$probe/Engine/Binaries/ThirdParty/Ogg/Win64" ]; then ROOT="$probe"; break; fi
  probe="$(dirname "$probe")"
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
REAL="$OGG/libogg_64_real.dll"
# Releases up to 3.0 used this name. Restore has to know about it, or an
# upgrade would leave the original stranded under a name nothing looks for.
LEGACY_REAL="$OGG/libogg_real.dll"

is_ours() {
  [ -f "$1" ] || return 1
  LC_ALL=C grep -qa "$MARKER" "$1" \
    || LC_ALL=C grep -qa "$LEGACY_MARKER" "$1" \
    || LC_ALL=C grep -qa "$PROBE_MARKER" "$1" \
    || LC_ALL=C grep -qa "$ELECTRA_MARKER" "$1" \
    || LC_ALL=C grep -qa "$MERGED_MARKER" "$1"
}

status() {
  if is_ours "$LIVE" && { [ -f "$REAL" ] || [ -f "$LEGACY_REAL" ]; }; then echo installed
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
  if [ -f "$REAL" ]; then mv -f "$REAL" "$LIVE"; else mv -f "$LEGACY_REAL" "$LIVE"; fi
  rm -f "$OGG/libogg_64.dll.orig" "$LEGACY_REAL" "$REAL"
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
  #
  # Read each side separately and check both succeeded. Piping straight into
  # comm hides a failure: comm -23 with an unreadable left side reports nothing
  # missing, which reads exactly like "everything is forwarded" -- and the next
  # step then moves that unreadable file over the saved original. Whatever
  # $LIVE is, if its exports cannot be read it is not a DLL worth keeping.
  if ! live_exports="$(python3 "$EXPORTS" exports "$LIVE" 2>&1)"; then
    echo "error: cannot read the exports of $LIVE" >&2
    echo "$live_exports" | sed 's/^/       /' >&2
    if [ -f "$REAL" ] || [ -f "$LEGACY_REAL" ]; then
      echo "       That file is damaged, and your original is still beside it." >&2
      echo "       Restore it before doing anything else:" >&2
      echo "         $0 \"$CONTENT\" --restore" >&2
    else
      echo "       Verify the game files in Steam, then run this again." >&2
    fi
    exit 1
  fi
  if ! proxy_exports="$(python3 "$EXPORTS" exports "$PROXY" 2>&1)"; then
    echo "error: cannot read the exports of the shipped proxy $PROXY" >&2
    echo "$proxy_exports" | sed 's/^/       /' >&2
    exit 1
  fi
  missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) \
                      <(printf '%s\n' "$proxy_exports" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this game's libogg exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    echo "       Rebuild it: runtime/build-proxy.sh \"$LIVE\"" >&2
    exit 1
  fi

  echo "[3/4] moving the original aside as $(basename "$REAL")"
  mv -f "$LIVE" "$REAL"

  echo "[4/4] installing the proxy"
  cp "$PROXY" "$LIVE"

  echo
  echo "installed"
  echo "  VP9 cutscenes play as shipped, on titles that crashed on them"
  echo "  the adapter-node walk is guarded, on titles that froze after a while"
  echo "each half is inert where its fault is absent"
  echo "a log is written to the bottle's C:\\${MARKER} on each launch"
  ;;

*)
  usage
  ;;
esac
