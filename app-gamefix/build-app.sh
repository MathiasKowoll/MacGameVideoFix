#!/usr/bin/env bash
# Build MacGameFix.app: the game-folder fixes, and nothing that patches CrossOver.
#
# The third app built out of this repository, beside MacGameVideoFix and
# MacGamePadFix. It carries only the fixes that live entirely in a game's own
# folder -- a proxy DLL beside the game, plus registry keys in the game's own
# prefix for the one title that needs them -- so it works with a CrossOver as
# CodeWeavers shipped it, with CrossOver Preview, and with a Steam Play tool such
# as NotProton. Nothing here touches an engine.
#
# WHICH GAMES, GATHERED AND NOT LISTED. A written-out list is the thing that went
# stale in the controller app twice. A title is in when all of this holds:
#
#   - an installer under runtime/ names it on an MGVF-GAME line, and declares
#     MGVF-SCOPE: folder
#   - process-features.json, generated from the wiki's own table, gives it no
#     codec -- a staged GStreamer plugin goes INTO the engine -- or names, in
#     codec_shipped_by, a CrossOver that already carries that codec as
#     CodeWeavers ships it; the app then says which one
#   - and no needs_engine -- the table's Motor column, which is what says a fix
#     depends on a winegstreamer of ours or of winevideo's -- or names, in
#     engine_not_needed_on, a CrossOver where it was measured needing none
#
# So a fix that later stops needing the engine arrives here on its own, and one
# that starts needing it leaves.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
APP="$HERE/MacGameFix.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
CACHE="${TMPDIR:-/tmp}/mgf-swift-cache"
VERSION="1.2.0"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> choosing the games"
# games.json is the app's whole table: one row per title, with the installer
# that serves it. Written by python3 from the two sources above; a title the
# rules keep out is named on the way, so the build says what it left behind.
/usr/bin/python3 - "$ROOT" "$STAGE/games.json" <<'PY'
import json, re, sys, pathlib
root, out = pathlib.Path(sys.argv[1]), sys.argv[2]
procs = json.loads((root / "process-features.json").read_text())["processes"]
games, left = [], []
for script in sorted((root / "runtime").glob("install-*.sh")):
    text = script.read_text()
    scope = re.search(r"^# MGVF-SCOPE: (\S+)", text, re.M)
    why = re.search(r"^# MGVF-WHY: (.*)$", text, re.M)
    for m in re.finditer(r"^# MGVF-GAME: (.*?) \| (.*?) \|(.*)$", text, re.M):
        name, exe, carrier = m.group(1).strip(), m.group(2).strip(), m.group(3).strip()
        p = procs.get(exe)
        reason = None
        if not scope or scope.group(1) != "folder":
            reason = "not a game-folder fix"
        elif p is None:
            reason = "not in process-features.json"
        elif p.get("codec") and not p.get("codec_shipped_by"):
            reason = "needs " + p["codec"] + " inside the engine"
        elif p.get("needs_engine") and not p.get("engine_not_needed_on"):
            reason = "needs a winegstreamer carrying " + p["needs_engine"]
        if reason:
            left.append((name, reason)); continue
        games.append({
            "name": name,
            "exe": exe,
            "carrierDir": carrier,
            "script": script.name,
            # The one installer that writes registry keys sources bottles.sh to
            # find where; the app has to name that place for it.
            "needsPrefix": ". \"$HERE/bottles.sh\"" in text,
            "symptom": p.get("symptom", ""),
            "backend": p.get("backend", ""),
            "gptk": p.get("gptk", ""),
            "gptkAlso": p.get("gptk_also", ""),
            # Empty for a title that needs no codec. For one that does, the
            # CrossOver that ships it, which is then the only one it plays on.
            "codecShippedBy": p.get("codec_shipped_by", "") if p.get("codec") else "",
            # A title whose engine requirement holds on stable 26.3 and not on
            # the CrossOver named here, which then is the one it plays on.
            "engineFreeOn": p.get("engine_not_needed_on", "") if p.get("needs_engine") else "",
            # The toolkit from which this title plays without its fix at all.
            "notNeededFrom": p.get("not_needed_from_gptk", ""),
            "why": why.group(1).strip() if why else "",
        })
games.sort(key=lambda g: g["name"].lower())
json.dump({"games": games}, open(out, "w"), indent=2)
for g in games:
    print(f"    in   {g['name']}")
for name, reason in left:
    print(f"    out  {name}: {reason}")
if not games:
    sys.exit("error: no game qualified; the rules above found nothing to carry")
PY

# The payload is every file the chosen installers name as $HERE/<name>, plus the
# installers themselves. Read from the scripts, so a helper an installer starts
# to use is carried without anybody remembering to add it here.
SCRIPTS=()
while IFS= read -r s; do SCRIPTS+=("$s"); done < <(
  /usr/bin/python3 -c 'import json,sys; print("\n".join(sorted({g["script"] for g in json.load(open(sys.argv[1]))["games"]})))' "$STAGE/games.json")
PAYLOAD=()
for s in "${SCRIPTS[@]}"; do
  PAYLOAD+=("$s")
  while IFS= read -r want; do PAYLOAD+=("$want"); done < <(
    grep -oE '\$HERE/[A-Za-z0-9._-]+' "$ROOT/runtime/$s" | sed 's|\$HERE/||' | sort -u)
done
PAYLOAD=($(printf '%s\n' "${PAYLOAD[@]}" | sort -u))

missing=0
for f in "${PAYLOAD[@]}"; do
  [ -e "$ROOT/runtime/$f" ] || { echo "error: runtime/$f is missing, and an installer here names it" >&2; missing=1; }
done
[ "$missing" = 0 ] || exit 1

rm -rf "$APP"
mkdir -p "$MACOS" "$RES" "$CACHE"

echo "==> compiling"
swiftc "$HERE/MacGameFix.swift" -O -parse-as-library \
  -o "$MACOS/MacGameFix" \
  -framework SwiftUI -framework AppKit \
  -target arm64-apple-macos14.0 \
  -module-cache-path "$CACHE"

echo "==> Info.plist"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>MacGameFix</string>
  <key>CFBundleDisplayName</key><string>MacGameFix</string>
  <key>CFBundleIdentifier</key><string>io.github.macgamefix</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleExecutable</key><string>MacGameFix</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
</dict></plist>
PLIST

# The same icon as MacGameVideoFix, for the reason app-padfix gives: one project.
echo "==> icon"
if [ -f "$ROOT/app/AppIcon.icns" ]; then
  cp "$ROOT/app/AppIcon.icns" "$RES/AppIcon.icns"
else
  echo "    note: app/AppIcon.icns is missing, so this build has no icon"
fi

echo "==> bundling ${#PAYLOAD[@]} files"
for f in "${PAYLOAD[@]}"; do
  cp -a "$ROOT/runtime/$f" "$RES/$f"
done
for s in "${SCRIPTS[@]}"; do chmod +x "$RES/$s"; done
cp "$STAGE/games.json" "$RES/games.json"

echo "==> signing (ad-hoc)"
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || \
  echo "    note: ad-hoc signing failed; the app still runs after Right click > Open"

# The payload must be byte for byte what runtime/ holds, for the reason the other
# two apps check it: a copy taken from a build outlives the build it came from.
for f in "${PAYLOAD[@]}"; do
  cmp -s "$ROOT/runtime/$f" "$RES/$f" || { echo "error: $f in the bundle differs from runtime/$f" >&2; exit 1; }
done
echo "==> the payload is byte-identical to runtime/"
echo
echo "built: $APP"
