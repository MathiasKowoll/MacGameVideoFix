#!/bin/bash
#
# Build the two PE halves that let a Windows client learn which bus a controller
# is on: winebus.sys (mgvf-0002), setupapi.dll (mgvf-0003) and ntoskrnl.exe (mgvf-0004).
#
#     scripts/build-controller-bus.sh
#
# WHY THIS EXISTS. A DualSense rumbles over USB and never over Bluetooth in every
# Steam game under CrossOver on macOS. Measured on 2026-09-08: over Bluetooth the
# pad wants report 0x31 with a CRC, and no client under wine ever sends one,
# because hidapi decides USB against Bluetooth by asking the HID device's parent
# devnode for a BTHENUM compatible id -- and under wine CM_Get_Parent is a stub
# and winebus reports no bus at all. Steam's own log says "bluetooth 0" for a pad
# on Bluetooth. The two patches make the truth reachable; this builds them.
#
# It reuses build-winegstreamer.sh's tree, on purpose: same sources, same
# configure, same toolchain, same engine stamp. Run that first. The patches touch
# only the PE side (winebus's unixlib interface is unchanged), so only the two
# PE files are produced and installed.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

OUT="${MGVF_BUILD_OUT:-$HOME/Development/mgvf-winegstreamer-build}"
OWNPATCHES="$(cd "$(dirname "$0")/.." && pwd)/source-patches"
ENGINE="${MGVF_ENGINE:-$HOME/Applications/Crossover_patched.app/Contents/SharedSupport/CrossOver}"
TREE="$OUT/wine-src"

export PATH="/opt/homebrew/opt/bison/bin:$HOME/.local/cxge/toolchains/llvm-mingw/bin:$PATH"
export SDKROOT="$(xcrun --show-sdk-path)"

say() { printf '  %s\n' "$*"; }

[ -f "$OUT/wine-build/config.status" ] || { say "no configured tree in $OUT -- run scripts/build-winegstreamer.sh first"; exit 1; }
[ -d "$TREE/dlls/winebus.sys" ] || { say "no wine sources in $TREE"; exit 1; }
command -v x86_64-w64-mingw32-clang >/dev/null 2>&1 || { say "no llvm-mingw on PATH"; exit 1; }

# ---- 1. our two patches, on top of whatever the winegstreamer build applied ---
for p in mgvf-0002 mgvf-0003 mgvf-0004; do
  f=$(ls "$OWNPATCHES/$p"-*.patch 2>/dev/null | head -1)
  [ -n "$f" ] || { say "no patch numbered $p in $OWNPATCHES"; exit 1; }
  # Already applied is told by the reverse dry run applying cleanly: macOS's
  # BSD patch and GNU patch word the "previously applied" message differently.
  if ( cd "$TREE" && patch -p1 -R -l -F3 --dry-run <"$f" >/dev/null 2>&1 ); then
    say "already applied $(basename "$f")"
  elif ( cd "$TREE" && patch -p1 --forward -l -F3 <"$f" >/dev/null 2>&1 ); then
    say "applied $(basename "$f")"
  else
    say "FAILED to apply $(basename "$f")"; exit 1
  fi
done

# ---- 2. just the two PE files -------------------------------------------------
( cd "$OUT/wine-build" && make -j8 dlls/winebus.sys/x86_64-windows/winebus.sys \
                                  dlls/setupapi/x86_64-windows/setupapi.dll \
                                  dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe >"$OUT/make-controller.log" 2>&1 ) \
  || { say "make failed, see $OUT/make-controller.log"; exit 1; }

SYS="$OUT/wine-build/dlls/winebus.sys/x86_64-windows/winebus.sys"
DLL="$OUT/wine-build/dlls/setupapi/x86_64-windows/setupapi.dll"
KRN="$OUT/wine-build/dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe"
[ -f "$SYS" ] && [ -f "$DLL" ] && [ -f "$KRN" ] || { say "build produced no output"; exit 1; }
cp "$SYS" "$OUT/winebus.sys"; cp "$DLL" "$OUT/setupapi.dll"; cp "$KRN" "$OUT/ntoskrnl.exe"

# ---- 3. the stamp the installer matches on, same fields as built-for.json ----
cat > "$OUT/controller-built-for.json" <<JSON
{
  "engine_app": "$(basename "$(cd "$ENGINE/../../.." && pwd)")",
  "engine_version": "$(defaults read "$ENGINE/../../Info.plist" CFBundleVersion 2>/dev/null || echo unknown)",
  "wine_build": "$(strings -a "$ENGINE/lib/wine/x86_64-unix/ntdll.so" | grep -oE 'wine-[0-9]+\.[0-9]+[^ ]*' | head -1)",
  "patches": "mgvf-0002 mgvf-0003 mgvf-0004"
}
JSON
say "built for: $(sed -n 's/.*"engine_app": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json") / $(sed -n 's/.*"wine_build": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json")"
say "built:  $OUT/winebus.sys  ($(stat -f %z "$OUT/winebus.sys") bytes)"
say "        $OUT/setupapi.dll ($(stat -f %z "$OUT/setupapi.dll") bytes)"
say "        $OUT/ntoskrnl.exe ($(stat -f %z "$OUT/ntoskrnl.exe") bytes)"
