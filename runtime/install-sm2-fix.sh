#!/usr/bin/env bash
#
# Install the Spider-Man 2 T-pose fix.
#
# This is macprotips' fix (github.com/macprotips/SpiderMan2Fix, public domain),
# plus one improvement of ours: it needs no DLL override, and it adds a barrier
# that removed brief black blocks in the sandstorm stage.
#
#     install-sm2-fix.sh <game folder>            install, or rebuild after a game update
#     install-sm2-fix.sh <game folder> --status   report
#     install-sm2-fix.sh <game folder> --restore  undo
#
# On M1-M4 Macs D3DMetal returns 0 for typed buffer reads past element 2^28 and
# drops typed writes past it, and the game reads and writes its 2 GB character
# mesh pool exactly that way: the characters stored high in it stay in a T-pose,
# and the symbiote's triangles are lost. Found and fixed by macprotips
# (github.com/macprotips/SpiderMan2Fix, public domain); see sm2-tpose-fix.c.
#
# The fix rides on amd_ags_x64.dll, which the game imports, so no DLL override is
# needed under any launcher. Its two shaders are rebuilt here from the user's own
# Spider-Man2.exe with Microsoft's shader tools, which are fetched once from that
# project's release (pinned by sha256) and cached; nothing from the game ships
# with MacGameVideoFix. A rebuilt shader is used only if it hashes to the one
# verified in the game.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MGVF-SCOPE: folder
# MGVF-GAME: Marvel's Spider-Man 2 | Spider-Man2.exe |
# MGVF-WHY: Characters stay in a T-pose and the symbiote breaks on M1-M4 Macs: D3DMetal reads and writes typed buffers only to element 2^28.

set -euo pipefail

usage() { sed -n '3,7p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

GAME="$1"
MODE="${2:---install}"
# See install-p5s-bridge.sh: a read-only caller must never land on --install.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then MODE=--status; fi
HERE="$(cd "$(dirname "$0")" && pwd)"

EXE="$GAME/Spider-Man2.exe"
LIVE="$GAME/amd_ags_x64.dll"
REAL="$GAME/amd_ags_x64_real.dll"
PROXY="$HERE/amd_ags_x64-sm2.dll"
EXPORTS="$HERE/pe.pl"
SHADERTOOL="$HERE/sm2-shadertool"
INI="$GAME/sm2fix.ini"
MARKER='sm2-tpose-fix.log'

# The game's shader, whether the typed view it rewrites is read (srv) or written
# (uav), that view's space and register, and the hash of the rewritten shader
# macprotips verified in the game.
SHADERS="3d50acdb571dce22 srv 0 1 589ed5a9fddc545f
14902794dd7134e4 srv 0 0 ee434fbf42f24d08
9538ca33c087a84c uav 0 1 ce7b280115b2aa10
125a4957ab64d24d uav 0 1 b84c589e5ae4b5c5"
SHADER_LIST="3d50acdb571dce22 14902794dd7134e4 9538ca33c087a84c 125a4957ab64d24d"

# Microsoft's DirectX Shader Compiler (dxc, dxa, dxv), arm64, built at DXC commit
# 6e0fab36 -- the build the verified shaders came from, so ours come out identical.
TOOLS_URL="https://github.com/macprotips/SpiderMan2Fix/releases/download/v1.0.1/Spider-Man-2-T-Pose-Fix.zip"
TOOLS_SHA256="e837fd62332f239b1b1f7d83be313e4392b646ca640057495c6384a1cef287c0"
TOOLS_COMMIT="6e0fab36"
TOOLS="${MGVF_SM2_TOOLS:-$HOME/Library/Caches/MacGameVideoFix/sm2-dxc-$TOOLS_COMMIT}"

is_ours() { [ -f "$1" ] && LC_ALL=C grep -qa "$MARKER" "$1"; }

# Their own d3d12.dll, from the Spider-Man 2 T-Pose Fix app. It writes the same
# sm2fix.ini and shaders, so --restore must leave those alone while it is there.
theirs_present() { [ -f "$GAME/d3d12.dll" ] && LC_ALL=C grep -qa "Spider-Man 2 T-Pose Fix" "$GAME/d3d12.dll"; }

# PE timestamp and image size: the build the DLL checks before it acts.
game_build() {
  /usr/bin/perl -e '
    open(my $f, "<:raw", $ARGV[0]) or exit 1; read($f, my $h, 4096) == 4096 or exit 1;
    substr($h, 0, 2) eq "MZ" or exit 1; my $pe = unpack("V", substr($h, 0x3c, 4));
    substr($h, $pe, 4) eq "PE\0\0" or exit 1;
    printf("%08x-%08x\n", unpack("V", substr($h, $pe + 8, 4)), unpack("V", substr($h, $pe + 80, 4)));
  ' "$1"
}

ini_build() { sed -n 's/^game_build[[:space:]]*=[[:space:]]*//p' "$INI" 2>/dev/null | head -1; }
ini_shaders() { sed -n 's/^shaders[[:space:]]*=[[:space:]]*//p' "$INI" 2>/dev/null | head -1; }
# MGVF's own setting: a UAV barrier after the volumetric fog copy (see
# sm2-tpose-fix.c). On by default; a 0 someone wrote is kept across reinstalls.
ini_fog_barrier() { sed -n 's/^mgvf_fog_barrier[[:space:]]*=[[:space:]]*//p' "$INI" 2>/dev/null | head -1; }

# Current means: set up for this game build, with this fix's shader list, and the
# DLL in the folder is the one shipped here (an older one of ours is replaced).
is_current() {
  [ "$(ini_build)" = "$1" ] && [ "$(ini_shaders)" = "$SHADER_LIST" ] && [ -n "$(ini_fog_barrier)" ] \
    && cmp -s "$LIVE" "$PROXY"
}

[ -f "$EXE" ] || {
  echo "error: no Spider-Man2.exe in $GAME" >&2
  echo "       Pick the folder Marvel's Spider-Man 2 is installed in." >&2
  exit 1
}

case "$MODE" in
--status)
  if is_ours "$LIVE" && [ -f "$REAL" ]; then
    # After a game update the DLL stands down until the shaders are rebuilt for
    # the new build, and an install from an older version of this fix lacks
    # shaders. The app knows four words, and "broken" -- patched, not active,
    # apply again -- is exactly this; installing again brings it up to date.
    if is_current "$(game_build "$EXE")"; then echo installed
    else echo "the game or this fix changed since it was installed; install again" >&2; echo broken; fi
  elif is_ours "$LIVE"; then echo broken
  elif [ ! -f "$LIVE" ] && [ -f "$REAL" ]; then echo half
  else echo absent; fi
  exit 0
  ;;
--restore)
  if [ -f "$REAL" ]; then
    mv -f "$REAL" "$LIVE"
    echo "restored -- the game is back to its own amd_ags_x64.dll"
  else
    echo "nothing to restore"
  fi
  rm -f "$GAME/$MARKER"
  if theirs_present; then
    echo "  sm2fix.ini and the shaders stay: the Spider-Man 2 T-Pose Fix app's d3d12.dll uses them too"
  else
    rm -f "$INI" "$GAME"/sm2fix-pool-*.dxil
  fi
  exit 0
  ;;
--install) ;;
*) usage ;;
esac

[ "$(uname -m)" = arm64 ] || {
  echo "error: this fix is for Apple Silicon Macs (the shader tools are arm64)." >&2
  exit 1
}
build="$(game_build "$EXE")" || { echo "error: cannot read the game build from Spider-Man2.exe" >&2; exit 1; }

if is_ours "$LIVE" && [ -f "$REAL" ] && is_current "$build"; then
  echo "the fix is already installed for this game build, nothing to do"
  exit 0
fi
if is_ours "$LIVE" && [ ! -f "$REAL" ]; then
  echo "error: $LIVE is already the fix but $REAL is gone." >&2
  echo "       Verify the game files in Steam, then run this again." >&2
  exit 1
fi
# The saved original must never be overwritten: a live DLL that is not this fix,
# with an original already set aside, is some other proxy (an older build, or
# another tool's), and moving it over $REAL would lose the game's own file.
if ! is_ours "$LIVE" && [ -f "$REAL" ]; then
  echo "error: $REAL already exists and $LIVE is not this fix." >&2
  echo "       Run this with --restore first, or verify the game files in Steam." >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[1/4] Microsoft's shader tools"
tools_ok() {
  [ -x "$TOOLS/Helpers/dxc" ] && [ -x "$TOOLS/Helpers/dxa" ] && [ -x "$TOOLS/Helpers/dxv" ] \
    && [ -f "$TOOLS/Frameworks/libdxcompiler.dylib" ] \
    && "$TOOLS/Helpers/dxc" --version 2>/dev/null | grep -q "$TOOLS_COMMIT"
}
if tools_ok; then
  echo "  cached in $TOOLS"
else
  echo "  fetching them once (10 MB) from github.com/macprotips/SpiderMan2Fix"
  curl -fsSL --retry 2 -o "$WORK/tools.zip" "$TOOLS_URL" || {
    echo "error: could not download the shader tools; check the connection and try again" >&2
    exit 1
  }
  got="$(shasum -a 256 "$WORK/tools.zip" | cut -d' ' -f1)"
  [ "$got" = "$TOOLS_SHA256" ] || {
    echo "error: the downloaded tools are not the expected file (sha256 $got); nothing was changed" >&2
    exit 1
  }
  mkdir "$WORK/tools"
  ditto -x -k "$WORK/tools.zip" "$WORK/tools"
  app="$(find "$WORK/tools" -maxdepth 2 -name '*.app' -type d | head -1)"
  [ -n "$app" ] && [ -x "$app/Contents/Helpers/dxc" ] || {
    echo "error: the download does not hold the shader tools where expected; nothing was changed" >&2
    exit 1
  }
  rm -rf "$TOOLS.new"
  mkdir -p "$TOOLS.new/Helpers" "$TOOLS.new/Frameworks" "$TOOLS.new/Licenses"
  ditto "$app/Contents/Helpers" "$TOOLS.new/Helpers"
  ditto "$app/Contents/Frameworks/libdxcompiler.dylib" "$TOOLS.new/Frameworks/libdxcompiler.dylib"
  ditto "$app/Contents/Resources/Licenses" "$TOOLS.new/Licenses" 2>/dev/null || true
  rm -rf "$TOOLS"
  mv "$TOOLS.new" "$TOOLS"
  tools_ok || { echo "error: the shader tools do not run on this Mac" >&2; exit 1; }
  echo "  cached in $TOOLS"
fi

echo "[2/4] rebuilding the fix's shaders from your Spider-Man2.exe (build $build)"
mkdir "$WORK/shaders"
while read -r source kind space register want; do
  w="$WORK/shaders/$source"
  "$SHADERTOOL" extract "$EXE" "$source" "$w.dxil" > /dev/null || {
    echo "error: shader $source is not in this Spider-Man2.exe." >&2
    echo "       This game version is not supported yet; nothing was changed." >&2
    exit 1
  }
  "$TOOLS/Helpers/dxc" -dumpbin "$w.dxil" -Fc "$w.ll" > /dev/null
  if [ "$kind" = uav ]; then rewrite=rewrite-uav; else rewrite=rewrite; fi
  "$SHADERTOOL" "$rewrite" "$w.ll" "$space" "$register" "$w.raw.ll" > /dev/null
  "$TOOLS/Helpers/dxa" -o "$w.asm.dxil" "$w.raw.ll" > /dev/null
  "$TOOLS/Helpers/dxv" "$w.asm.dxil" -o "$w.signed.dxil" > /dev/null
  got="$("$SHADERTOOL" hash "$w.signed.dxil")"
  [ "$got" = "$want" ] || {
    echo "error: shader $source rebuilt as $got, not the verified $want; nothing was changed" >&2
    exit 1
  }
  cp "$w.signed.dxil" "$WORK/shaders/sm2fix-pool-$source.dxil"
  echo "  $source: identical to the verified shader"
done <<< "$SHADERS"

if ! is_ours "$LIVE"; then
  echo "[3/4] checking the proxy forwards everything the game imports"
  # Read each side separately (see install-p5s-bridge.sh).
  if ! live_exports="$(/usr/bin/perl "$EXPORTS" exports "$LIVE" 2>&1)"; then
    echo "error: cannot read the exports of $LIVE" >&2
    exit 1
  fi
  proxy_exports="$(/usr/bin/perl "$EXPORTS" exports "$PROXY")"
  missing="$(comm -23 <(printf '%s\n' "$live_exports" | sort) <(printf '%s\n' "$proxy_exports" | sort))"
  if [ -n "$missing" ]; then
    echo "error: this build's amd_ags exports symbols the shipped proxy does not:" >&2
    echo "$missing" | sed 's/^/       /' >&2
    exit 1
  fi
elif cmp -s "$LIVE" "$PROXY"; then
  echo "[3/4] the fix's amd_ags_x64.dll is already in place"
else
  echo "[3/4] the fix's amd_ags_x64.dll is an older build; it will be replaced"
fi

echo "[4/4] installing"
for f in "$WORK"/shaders/sm2fix-pool-*.dxil; do cp "$f" "$GAME/"; done
if is_ours "$LIVE" && ! cmp -s "$LIVE" "$PROXY"; then
  # The original stays where it is, under $REAL; only our own file is replaced.
  cp "$PROXY" "$LIVE.new" && mv -f "$LIVE.new" "$LIVE" || {
    rm -f "$LIVE.new"; echo "error: could not replace the older fix DLL" >&2; exit 1; }
fi
fog_barrier="$(ini_fog_barrier)"
[ "$fog_barrier" = 0 ] || fog_barrier=1
printf '# Spider-Man 2 T-Pose Fix (MacGameVideoFix). The fix only acts on this game build.\ngame_build = %s\nshaders = %s\n# A UAV barrier after the volumetric fog copy; 0 turns it off.\nmgvf_fog_barrier = %s\n' \
  "$build" "$SHADER_LIST" "$fog_barrier" > "$INI.new"
mv -f "$INI.new" "$INI"
if ! is_ours "$LIVE"; then
  # The original is moved aside before anything is written over it
  # (see install-p5s-bridge.sh for the failure this order prevents).
  mv -f "$LIVE" "$REAL" || { echo "error: could not move the original aside; nothing was changed" >&2; exit 1; }
  [ -f "$REAL" ] || { echo "error: the original is not where it should be; refusing to write" >&2; exit 1; }
  cp "$PROXY" "$LIVE" || {
    echo "error: could not install the fix; putting the original back" >&2
    mv -f "$REAL" "$LIVE" || echo "       and that failed too -- it is at $REAL" >&2
    exit 1
  }
fi
echo
echo "installed"
echo "  the whole mesh pool is read and written raw; the game writes $MARKER beside itself"
if theirs_present; then
  echo "  note: the Spider-Man 2 T-Pose Fix app's d3d12.dll is in the game folder too."
  echo "        Remove it with that app (or drop its d3d12 override): one of the two is enough."
fi
