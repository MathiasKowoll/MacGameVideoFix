#!/bin/bash
#
# Put the controller-bus set (winebus.sys, setupapi.dll, ntoskrnl.exe) into a CrossOver
# engine, or take it back out.
#
#     runtime/install-engine-controller.sh <CrossOver.app> install
#     runtime/install-engine-controller.sh <CrossOver.app> --status
#     runtime/install-engine-controller.sh <CrossOver.app> --restore
#
# Same shape as install-engine-media.sh, same rules: refuse an engine these were
# not built for (name AND version -- a patched fork and stock CrossOver report
# the same version), keep the original beside each file as .mgvf-stock, never
# let a backup be our own build, and replace by rename so a process holding the
# old file keeps the old file. Both files are read at bottle boot / process
# start, so close Steam and let its bottle shut down before installing.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${1:-}"; ACTION="${2:-}"
usage() { echo "usage: $0 <CrossOver.app> install|--status|--restore" >&2; exit 2; }
[ -n "$APP" ] && [ -n "$ACTION" ] || usage
CX="$APP/Contents/SharedSupport/CrossOver"
[ -d "$CX" ] || { echo "error: not a CrossOver app: $APP" >&2; exit 1; }

BUILD="${MGVF_BUILD_OUT:-$HOME/Development/mgvf-winegstreamer-build}"
SYS_SRC="${MGVF_CONTROLLER_SYS:-$BUILD/winebus.sys}"
DLL_SRC="${MGVF_CONTROLLER_DLL:-$BUILD/setupapi.dll}"
KRN_SRC="${MGVF_CONTROLLER_KRN:-$BUILD/ntoskrnl.exe}"
BUILTFOR="${MGVF_CONTROLLER_BUILTFOR:-$BUILD/controller-built-for.json}"
SYS_DEST="$CX/lib/wine/x86_64-windows/winebus.sys"
DLL_DEST="$CX/lib/wine/x86_64-windows/setupapi.dll"
KRN_DEST="$CX/lib/wine/x86_64-windows/ntoskrnl.exe"

status() {
  if [ -f "$SYS_DEST.mgvf-stock" ] && [ -f "$DLL_DEST.mgvf-stock" ] && [ -f "$KRN_DEST.mgvf-stock" ]; then echo installed
  elif [ -f "$SYS_DEST.mgvf-stock" ] || [ -f "$DLL_DEST.mgvf-stock" ] || [ -f "$KRN_DEST.mgvf-stock" ]; then echo broken
  else echo absent; fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      n=0
      for d in "$SYS_DEST" "$DLL_DEST" "$KRN_DEST"; do
        if [ -f "$d.mgvf-stock" ]; then mv -f "$d.mgvf-stock" "$d"; n=$((n+1)); fi
      done
      if [ "$n" -gt 0 ]; then
        /usr/bin/codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
        /usr/bin/xattr -cr "$APP" 2>/dev/null || true
        echo "restored $n of 3"
      else echo "nothing to restore"; fi
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

for f in "$SYS_SRC" "$DLL_SRC" "$KRN_SRC" "$BUILTFOR"; do
  [ -f "$f" ] || { echo "error: $f is missing -- run scripts/build-controller-bus.sh first" >&2; exit 1; }
done

want_app="$(/usr/bin/sed -n 's/.*"engine_app": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
want_engine="$(/usr/bin/sed -n 's/.*"engine_version": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
have_app="$(basename "$APP")"
have_engine="$(/usr/bin/defaults read "$APP/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo "")"
[ "$want_app" = "$have_app" ] || { echo "error: these were built for $want_app and this is $have_app." >&2; exit 1; }
[ "$want_engine" = "$have_engine" ] || { echo "error: these were built for engine $want_engine and this is $have_engine." >&2; exit 1; }

# A bottle that is up has winebus.sys loaded in its winedevice.exe.
if /usr/bin/pgrep -f "winedevice.exe" >/dev/null 2>&1; then
  echo "error: a wine bottle is running (winedevice.exe). Close Steam, let the bottle shut down, and re-run." >&2; exit 1
fi

put() {
  local src="$1" dest="$2"
  [ -f "$dest" ] || { echo "error: no $dest to replace" >&2; exit 1; }
  if [ -f "$dest.mgvf-stock" ]; then
    if cmp -s "$src" "$dest.mgvf-stock"; then
      echo "error: $dest.mgvf-stock is this same build, not the original; refusing." >&2; exit 1
    fi
  elif cmp -s "$src" "$dest"; then
    echo "  note: $(basename "$dest") is already this build; no backup taken" >&2
  else
    cp -p "$dest" "$dest.mgvf-stock"
  fi
  cp "$src" "$dest.mgvf-new" && mv -f "$dest.mgvf-new" "$dest"
  echo "  $(basename "$dest")  <- $(basename "$src")"
}
put "$SYS_SRC" "$SYS_DEST"
put "$DLL_SRC" "$DLL_DEST"
put "$KRN_SRC" "$KRN_DEST"
# The two files are sealed resources of the bundle: replacing them breaks the
# signature, and a broken seal is what Finder calls "damaged". Sign, then clear
# attributes, in that order (see patching-a-crossover-copy).
/usr/bin/codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || echo "  warning: re-signing failed; the app may be reported as damaged" >&2
/usr/bin/xattr -cr "$APP" 2>/dev/null || true
echo "installed into $have_app ($have_engine)"
