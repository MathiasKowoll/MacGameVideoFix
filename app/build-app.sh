#!/usr/bin/env bash
# Build MacGameVideoFix.app and bundle the scripts it drives.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
APP="$HERE/MacGameVideoFix.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
CACHE="${TMPDIR:-/tmp}/ms2macfix-swift-cache"

rm -rf "$APP"
mkdir -p "$MACOS" "$RES" "$CACHE"

echo "==> compiling"
swiftc "$HERE/MacGameVideoFix.swift" -O -parse-as-library \
  -o "$MACOS/MacGameVideoFix" \
  -framework SwiftUI -framework AppKit -framework UniformTypeIdentifiers \
  -target arm64-apple-macos14.0 \
  -module-cache-path "$CACHE"

echo "==> Info.plist"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>MacGameVideoFix</string>
  <key>CFBundleDisplayName</key><string>MacGameVideoFix</string>
  <key>CFBundleIdentifier</key><string>io.github.mortalshell2macfix</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleExecutable</key><string>MacGameVideoFix</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
</dict></plist>
PLIST

echo "==> icon"
if [ -f "$HERE/AppIcon.icns" ]; then
  cp "$HERE/AppIcon.icns" "$RES/AppIcon.icns"
elif command -v rsvg-convert >/dev/null && [ -f "$HERE/icon.svg" ]; then
  # Regenerate from source if the prebuilt icns is missing.
  SET="${TMPDIR:-/tmp}/ms2-icon.iconset"
  rm -rf "$SET"; mkdir -p "$SET"
  for s in 16 32 128 256 512; do
    rsvg-convert -w "$s" -h "$s" "$HERE/icon.svg" -o "$SET/icon_${s}x${s}.png"
    rsvg-convert -w "$((s*2))" -h "$((s*2))" "$HERE/icon.svg" -o "$SET/icon_${s}x${s}@2x.png"
  done
  iconutil -c icns "$SET" -o "$RES/AppIcon.icns"
  rm -rf "$SET"
else
  echo "    note: no icon (install librsvg, or keep AppIcon.icns in app/)"
fi

echo "==> bundling scripts"
cp "$ROOT/scripts/transcode-movies.sh" "$ROOT/scripts/pak-hide-videos.py" "$RES/"
cp "$ROOT/runtime/install-p5s-bridge.sh" "$ROOT/runtime/amd_ags_x64.dll" "$RES/"
cp "$ROOT/runtime/install-nioh-bridge.sh" "$ROOT/runtime/install-nioh3-bridge.sh" "$RES/"
cp "$ROOT/runtime/stage-codecs.sh" "$RES/"
chmod +x "$RES/transcode-movies.sh" "$RES/pak-hide-videos.py" \
         "$RES/install-p5s-bridge.sh" "$RES/install-nioh-bridge.sh" \
         "$RES/install-nioh3-bridge.sh"

# The runtime patch: the installer resolves the proxy and the PE reader next to
# itself, so all three have to land in the same folder.
cp "$ROOT/runtime/install-runtime-fix.sh" "$ROOT/runtime/install-dwo-bridge.sh" \
   "$ROOT/runtime/pe.py" "$RES/"
chmod +x "$RES/install-runtime-fix.sh" "$RES/install-dwo-bridge.sh" "$RES/pe.py"

# One prebuilt carrier per game. Missing one disables that game's fix rather
# than failing the build, so the app is still usable for the other.
for dll in libogg_64.dll libxess.dll GfeSDK.dll amd_ags_x64-nioh3.dll; do
  if [ -f "$ROOT/runtime/$dll" ]; then
    cp "$ROOT/runtime/$dll" "$RES/"
  else
    echo "    warning: runtime/$dll missing — the fix that uses it will not work."
  fi
done

# Signing.
#   Ad-hoc by default: works locally, but macOS blocks the first launch for
#   anyone who downloads it (Right click > Open gets around that).
#   For a release, set SIGN_ID to a "Developer ID Application: ..." identity;
#   add NOTARY_PROFILE to notarise and staple, so downloads open with a
#   double click. Notarisation *requires* a Developer ID signature -- Apple
#   rejects ad-hoc and unsigned binaries.
#
#   One-time credential setup, run by you, not by this script:
#     xcrun notarytool store-credentials <profile-name> \
#       --apple-id <your-apple-id> --team-id <TEAMID> --password <app-specific-password>
#
#   SIGN_ID="Developer ID Application: Name (TEAMID)" NOTARY_PROFILE=<profile> ./build-app.sh

if [ -n "${SIGN_ID:-}" ]; then
  echo "==> signing with $SIGN_ID"
  codesign --force --deep --options runtime --timestamp --sign "$SIGN_ID" "$APP"
  codesign --verify --strict --verbose=2 "$APP"

  if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "==> notarising"
    ZIP="${TMPDIR:-/tmp}/MacGameVideoFix.zip"
    ditto -c -k --keepParent "$APP" "$ZIP"
    xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
    rm -f "$ZIP"
    echo "==> stapling"
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
  fi
else
  echo "==> signing (ad-hoc)"
  codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || \
    echo "    note: ad-hoc signing failed; the app still runs after Right click > Open"
fi

echo
echo "built: $APP"
