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
  <key>CFBundleVersion</key><string>5.0.1</string>
  <key>CFBundleShortVersionString</key><string>5.0.1</string>
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
cp "$ROOT/runtime/install-nioh-bridge.sh" "$ROOT/runtime/install-nioh3-bridge.sh" \
   "$ROOT/runtime/install-nier-bridge.sh" \
   "$ROOT/runtime/install-kh-bridge.sh" \
   "$ROOT/runtime/install-tmnt-fix.sh" \
   "$ROOT/runtime/install-tormented-fix.sh" \
   "$ROOT/runtime/install-ng4-fix.sh" \
    "$ROOT/runtime/install-resonance-fix.sh" "$ROOT/runtime/NvCloth_x64-resonance.dll" "$RES/"
cp "$ROOT/runtime/stage-codecs.sh" "$RES/"

# NINJA GAIDEN 3 travels in the app even though the app does not offer it.
#
# Its installer is MGVF-SCOPE: bottle -- it writes per-executable overrides into
# a bottle's registry, and this app asks for a game folder and never learns which
# bottle that is. So it stays out of the app's own list. It did not use to be in
# the bundle either, on the reasoning that "a launcher runs it" and a launcher
# downloaded the fixes tarball separately.
#
# That reasoning inverted on 2026-08-31, when the launcher became the app's host
# rather than its downloader. A launcher that embeds this bundle knows the bottle
# and can run this; if the files are not here it cannot, and the failure reads as
# "this title has no fix" rather than "the payload is a subset".
cp "$ROOT/runtime/install-ng3-fix.sh" "$RES/"

# manifest.json travels too, so a launcher hosting this bundle has the index it
# would otherwise have downloaded: which DLL belongs to which game, which carrier
# it rides, under what name the original is kept, whether a registry override is
# needed. Without it the payload is present and unreadable.
#
# Generated with its provenance stated honestly rather than guessed -- see the
# override block in make-fixes-bundle.sh for why "fromReleasedTag" is null here
# and a real answer in the release's own tarball.
MANIFEST_TMP="$(mktemp -d)"
# Read out of the Info.plist this build just wrote, not from a second constant.
APP_VERSION="$(/usr/bin/defaults read "$APP/Contents/Info" CFBundleShortVersionString 2>/dev/null)"
if MGVF_MANIFEST_VERSION="v$APP_VERSION" MGVF_MANIFEST_RELEASED=null \
     "$ROOT/runtime/make-fixes-bundle.sh" "$MANIFEST_TMP" >/dev/null 2>&1; then
  tar xzf "$(ls "$MANIFEST_TMP"/fixes-*.tar.gz | head -1)" -C "$MANIFEST_TMP"
  m="$(/usr/bin/find "$MANIFEST_TMP" -name manifest.json | head -1)"
  [ -n "$m" ] && cp "$m" "$RES/manifest.json"
fi
rm -rf "$MANIFEST_TMP"
[ -f "$RES/manifest.json" ] || { echo "error: manifest.json was not generated" >&2; exit 1; }
cp "$ROOT"/runtime/ng3-*.dll "$RES/"
cp "$ROOT/runtime/ng3-THIRD-PARTY-LICENCES.md" "$RES/"
chmod +x "$RES/install-ng3-fix.sh"

# bottles.sh, which install-kh-bridge.sh and install-nier-bridge.sh source.
#
# It was never copied. Both scripts died on their first line inside the app, the
# app read a non-zero exit as "this copy has no carrier DLL for the fix to ride
# on", and two fixes that were installed and working reported as impossible. The
# check at the end of this file exists so that the next omission is caught here
# rather than by somebody reading a wrong sentence in the window.
cp "$ROOT/runtime/bottles.sh" "$RES/"

# Everything the app needs to build a patched engine of its own: the two scripts,
# every engine set, and the codecs that go inside the copy. Flat, like the rest of
# Resources -- make-engine-copy.sh reads the layout rather than assuming one.
#
# stage-codecs.sh stays for now. It is what the app falls back to when somebody
# turns the copy off, and removing it before the copy has been used in anger
# would leave three titles with no codecs and no way to get them.
cp "$ROOT/scripts/make-engine-copy.sh" "$RES/"
cp "$ROOT/runtime/install-engine-media.sh" "$RES/"
for f in "$ROOT"/runtime/engine-winegstreamer*.dll "$ROOT"/runtime/engine-winegstreamer*.so \
         "$ROOT"/runtime/engine-built-for*.json; do
  [ -f "$f" ] && cp "$f" "$RES/"
done
for f in "$ROOT"/runtime/engine-payload/lib64/gstreamer-1.0/*.dylib \
         "$ROOT"/runtime/engine-payload/lib64/*.dylib; do
  [ -f "$f" ] && cp "$f" "$RES/"
done
[ -f "$ROOT/runtime/engine-payload/CODEC-LICENCES.md" ] && \
  cp "$ROOT/runtime/engine-payload/CODEC-LICENCES.md" "$RES/"
chmod +x "$RES/transcode-movies.sh" "$RES/pak-hide-videos.py" \
         "$RES/install-p5s-bridge.sh" "$RES/install-nioh-bridge.sh" \
         "$RES/install-nioh3-bridge.sh" "$RES/install-nier-bridge.sh" \
         "$RES/install-kh-bridge.sh" "$RES/install-tmnt-fix.sh" \
         "$RES/install-tormented-fix.sh" "$RES/install-ng4-fix.sh" \
    "$RES/install-resonance-fix.sh"

# The runtime patch: the installer resolves the proxy and the PE reader next to
# itself, so all three have to land in the same folder.
#
# pe.pl, not pe.py. The installers read PE exports with /usr/bin/perl now,
# because macOS does not ship python: /usr/bin/python3 is one of 78 hard links
# to the xcrun dispatcher and fails on a Mac without developer tools. Shipping
# the Python one would put installers in the bundle that call a file which
# never travels with them.
cp "$ROOT/runtime/install-runtime-fix.sh" "$ROOT/runtime/install-dwo-bridge.sh" \
   "$ROOT/runtime/pe.pl" "$RES/"
chmod +x "$RES/install-runtime-fix.sh" "$RES/install-dwo-bridge.sh" "$RES/pe.pl"

# One prebuilt carrier per game. Missing one disables that game's fix rather
# than failing the build, so the app is still usable for the other.
for dll in libogg_64.dll libxess.dll GfeSDK.dll amd_ags_x64-nioh3.dll dinput8-nier.dll \
           dinput8-kh.dll fmod-tmnt.dll \
           OpenColorIO_2_3-tormented.dll dstorage-ng4.dll; do
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

# --- every file an installer names must actually be here ---------------------
#
# make-fixes-bundle.sh has always done this and this script never did, which is
# how bottles.sh went missing for as long as it did. Same rule: a reference to
# $HERE/<name> is a promise that <name> is beside it.
missing=0
for f in "$RES"/install-*.sh; do
  [ -f "$f" ] || continue
  for want in $(grep -oE '\$HERE/[A-Za-z0-9._-]+' "$f" | sed 's|\$HERE/||' | sort -u); do
    if [ ! -e "$RES/$want" ]; then
      # A name built from a variable leaves its constant half behind; treat it as
      # a prefix before calling it missing.
      if ! ls "$RES/$want"* >/dev/null 2>&1; then
        echo "error: $(basename "$f") wants $want, which is not in the bundle" >&2
        missing=1
      fi
    fi
  done
done
[ "$missing" = 0 ] || exit 1
echo "==> every file the installers name is present"
