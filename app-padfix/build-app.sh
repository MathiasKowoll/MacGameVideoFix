#!/usr/bin/env bash
# Build MacGamePadFix.app and bundle the one installer it drives.
#
# The sibling of app/build-app.sh, and much shorter for one reason: this app
# carries a single engine set and offers a single script, so there is no
# manifest to generate and no per-game carrier DLL to warn about. What it keeps
# from that script is the part that matters -- the payload is copied from
# runtime/ unchanged, and the build fails rather than shipping a bundle that is
# missing a file the installer names.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
APP="$HERE/MacGamePadFix.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
CACHE="${TMPDIR:-/tmp}/mgpf-swift-cache"

# The six files this app exists to carry, all from runtime/ and none of them
# rebuilt here. install-engine-controller.sh resolves its payload as
# $HERE/<name>, so a flat Resources directory is exactly the layout it wants.
#
# The set is FOUR engine files, not three: winebus.sys, setupapi.dll and
# ntoskrnl.exe are PE, and engine-controller-winebus.so is the unix half of
# winebus, which the installer writes to lib/wine/x86_64-unix/ rather than
# beside the others. It arrived with mgvf-0006 and this list did not follow it
# then; the check below is what said so.
PAYLOAD=(
  install-engine-controller.sh
  engine-controller-winebus.sys
  engine-controller-setupapi.dll
  engine-controller-ntoskrnl.exe
  engine-controller-winebus.so
  engine-controller-built-for.json
)

# Checked before anything is compiled. An app that builds and then cannot do the
# one thing it is for is worse than a build that stops here, and "the payload is
# a subset" is a failure that otherwise turns up at whoever was handed the app.
missing=0
for f in "${PAYLOAD[@]}"; do
  if [ ! -f "$ROOT/runtime/$f" ]; then
    echo "error: runtime/$f is missing, and this app is nothing without it" >&2
    missing=1
  fi
done
[ "$missing" = 0 ] || exit 1

rm -rf "$APP"
mkdir -p "$MACOS" "$RES" "$CACHE"

echo "==> compiling"
swiftc "$HERE/MacGamePadFix.swift" -O -parse-as-library \
  -o "$MACOS/MacGamePadFix" \
  -framework SwiftUI -framework AppKit \
  -target arm64-apple-macos14.0 \
  -module-cache-path "$CACHE"

echo "==> Info.plist"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>MacGamePadFix</string>
  <key>CFBundleDisplayName</key><string>MacGamePadFix</string>
  <key>CFBundleIdentifier</key><string>io.github.macgamepadfix</string>
  <key>CFBundleVersion</key><string>1.0.0</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
  <key>CFBundleExecutable</key><string>MacGamePadFix</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
</dict></plist>
PLIST

# The same icon as MacGameVideoFix, on purpose: the two are one project, and a
# second icon would be a second thing to keep in step for no gain.
echo "==> icon"
if [ -f "$ROOT/app/AppIcon.icns" ]; then
  cp "$ROOT/app/AppIcon.icns" "$RES/AppIcon.icns"
else
  echo "    note: app/AppIcon.icns is missing, so this build has no icon"
fi

echo "==> bundling the controller-bus set"
for f in "${PAYLOAD[@]}"; do
  cp "$ROOT/runtime/$f" "$RES/$f"
done
chmod +x "$RES/install-engine-controller.sh"

# The licence note travels inside the bundle rather than only in the repository.
# The four binaries are wine under LGPL-2.1-or-later, and the notice has to
# reach whoever ends up holding them -- which, for an app meant to be handed
# around, is somebody who will never see this directory.
cp "$HERE/CONTROLLER-LICENCES.md" "$RES/"

echo "==> signing (ad-hoc)"
# Ad hoc: it works locally, and macOS blocks the first launch for anybody who
# downloads it (Right click > Open gets around that, and app-padfix/README.md
# says so in the exact words a person needs). A Developer ID signature and
# notarisation are what app/build-app.sh does for a release; this app is not
# released on its own yet, so it is not pretended here.
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || \
  echo "    note: ad-hoc signing failed; the app still runs after Right click > Open"

echo
echo "built: $APP"

# --- every file the installer names must actually be here --------------------
#
# Straight out of app/build-app.sh, and kept for the same reason: a reference to
# $HERE/<name> in the script is a promise that <name> is beside it, and the one
# time that promise was broken in the other app, two working fixes reported as
# impossible.
missing=0
for want in $(grep -oE '\$HERE/[A-Za-z0-9._-]+' "$RES/install-engine-controller.sh" \
                | sed 's|\$HERE/||' | sort -u); do
  [ -e "$RES/$want" ] || { echo "error: the installer wants $want, which is not in the bundle" >&2
                           missing=1; }
done
[ "$missing" = 0 ] || exit 1
echo "==> every file the installer names is present"

# The payload must be byte for byte what runtime/ holds. A copy taken from a
# build outlives the build it came from -- CODEC-LICENCES.md says so about the
# codecs, and it is just as true of four engine files whose whole contract is
# the stamp that travels with them.
for f in "${PAYLOAD[@]}"; do
  cmp -s "$ROOT/runtime/$f" "$RES/$f" || {
    echo "error: $f in the bundle differs from runtime/$f" >&2; exit 1; }
done
echo "==> the payload is byte-identical to runtime/"
