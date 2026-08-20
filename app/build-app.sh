#!/usr/bin/env bash
# Build MortalShell2MacFix.app and bundle the scripts it drives.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
APP="$HERE/MortalShell2MacFix.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
CACHE="${TMPDIR:-/tmp}/ms2macfix-swift-cache"

rm -rf "$APP"
mkdir -p "$MACOS" "$RES" "$CACHE"

echo "==> compiling"
swiftc "$HERE/MortalShell2MacFix.swift" -O -parse-as-library \
  -o "$MACOS/MortalShell2MacFix" \
  -framework SwiftUI -framework AppKit -framework UniformTypeIdentifiers \
  -target arm64-apple-macos14.0 \
  -module-cache-path "$CACHE"

echo "==> Info.plist"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>MortalShell2MacFix</string>
  <key>CFBundleDisplayName</key><string>MortalShell2MacFix</string>
  <key>CFBundleIdentifier</key><string>io.github.mortalshell2macfix</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleExecutable</key><string>MortalShell2MacFix</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
</dict></plist>
PLIST

echo "==> bundling scripts"
cp "$ROOT/scripts/transcode-movies.sh" "$ROOT/scripts/pak-hide-videos.py" "$RES/"
chmod +x "$RES/transcode-movies.sh" "$RES/pak-hide-videos.py"

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
    ZIP="${TMPDIR:-/tmp}/MortalShell2MacFix.zip"
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
