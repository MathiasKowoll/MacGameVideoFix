#!/usr/bin/env bash
#
# Put a built winegstreamer into an engine, and refuse if it was built for
# another one.
#
#     scripts/install-winegstreamer.sh [<built dir>] [<engine>]
#     scripts/install-winegstreamer.sh --restore [<engine>]
#
# The unix half links against Wine's own internals, which are not stable
# between releases. A pair built for one CrossOver dropped onto the next does
# not announce itself: it fails at load, and what a person sees is a game that
# does nothing. So this compares what the binary was built for against what is
# actually there, and stops.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
BUILT="${1:-$HOME/Development/mgvf-winegstreamer-build}"
ENGINE="${2:-$HOME/Applications/Crossover_patched.app/Contents/SharedSupport/CrossOver}"
[ "$BUILT" = "--restore" ] && { ENGINE="${2:-$ENGINE}"; RESTORE=1; } || RESTORE=0
U="$ENGINE/lib/wine/x86_64-unix/winegstreamer.so"
W="$ENGINE/lib/wine/x86_64-windows/winegstreamer.dll"

if [ "$RESTORE" = 1 ]; then
  for f in "$U" "$W"; do
    [ -f "$f.stock" ] || { echo "no $f.stock to restore" >&2; exit 1; }
    cp "$f.stock" "$f" && echo "  restored $(basename "$f")"
  done
  exit 0
fi

for f in "$BUILT/winegstreamer.so" "$BUILT/winegstreamer.dll" "$BUILT/built-for.json"; do
  [ -f "$f" ] || { echo "missing $f -- run build-winegstreamer.sh first" >&2; exit 1; }
done

app="$(cd "$ENGINE/../../.." && pwd)"
here_wine="$(strings -a "$ENGINE/lib/wine/x86_64-unix/ntdll.so" | grep -oE 'wine-[0-9]+\.[0-9]+[^ ]*' | head -1)"
built_wine="$(sed -n 's/.*"wine_build": "\(.*\)".*/\1/p' "$BUILT/built-for.json")"
if [ "$here_wine" != "$built_wine" ]; then
  echo "refusing: built for $built_wine, this engine is $here_wine" >&2
  echo "          rebuild against this engine's sources first" >&2
  exit 1
fi

for f in "$U" "$W"; do [ -f "$f.stock" ] || cp "$f" "$f.stock"; done
cp "$BUILT/winegstreamer.so" "$U"
cp "$BUILT/winegstreamer.dll" "$W"
echo "  installed into $(basename "$app"), built for $built_wine"
echo "  the originals are beside them as .stock"
