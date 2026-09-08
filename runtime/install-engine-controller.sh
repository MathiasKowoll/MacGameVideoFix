#!/usr/bin/env bash
#
# Install (or remove) the controller-bus set this project builds, into a
# CrossOver engine: winebus.sys, setupapi.dll and ntoskrnl.exe.
#
#   install-engine-controller.sh <engine app>            install
#   install-engine-controller.sh <engine app> --restore  remove
#   install-engine-controller.sh <engine app> --status   report what is in place
#
# <engine app> is the .app itself, e.g.
#   ~/Applications/Crossover_MGVF.app
#
# WHAT THIS IS. An improvement, not a fix. No title in the table needs it and
# every one of them runs without it; it is offered so that a controller on
# Bluetooth works the way it does over USB. A DualSense on Bluetooth wants
# output report 0x31 with a CRC, and no Windows client under wine ever sends
# one, because every one of them decides USB against Bluetooth the same way:
# hidapi asks the HID device's parent devnode for its compatible ids and looks
# for BTHENUM. Under wine CM_Get_Parent was a stub and winebus named no bus at
# all, so the answer was always "not Bluetooth" -- Steam's log says
# "bluetooth 0" for a pad that is -- and the pad never rumbled. With the three
# files here the answer is the true one: rumble, the PS button and the touchpad
# work over Bluetooth, measured on 2026-09-08; trigger effects ride in the same
# report, and the owner reports them in a title that sends them.
#
# Same shape as install-engine-media.sh, same rules: it writes into the ENGINE,
# which every bottle and every game on it shares, so it refuses an engine these
# were not built for -- name AND version, because a patched fork and stock
# CrossOver report the same version -- keeps the original beside each file as
# .mgvf-stock, never lets a backup be our own build, and replaces by rename so a
# process holding the old file keeps the old file. Unlike the media pair there
# is no unix half: the three are PE files built from the engine's own wine
# source with mgvf-0002, mgvf-0003 and mgvf-0004 on top, and --restore puts
# CodeWeavers' three back.
#
# It signs. The media installer leaves signing to make-engine-copy.sh, which
# runs it partway through and signs at its last step. This one is turned on and
# off after the copy exists, with nothing after it, so it re-signs the bundle
# itself and clears the quarantine attribute, in that order.
#
# MGVF-SCOPE: engine
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

usage() { sed -n '3,11p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

APP="${1%/}"
ACTION="${2:-install}"
# A read-only caller sets MGVF_STATUS_ONLY=1. The default above is the
# DESTRUCTIVE branch, so without this the read-only property of a survey rests
# on the literal --status never being lost from one line of one caller.
# Structural beats positional, as in every other installer here.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then ACTION=--status; fi

# Named literally so make-fixes-bundle.sh collects them. One set, no suffix:
# nothing in these three files links against the engine, so one build serves
# every engine of the name and version the stamp records.
SYS="$HERE/engine-controller-winebus.sys"
DLL="$HERE/engine-controller-setupapi.dll"
KRN="$HERE/engine-controller-ntoskrnl.exe"
BUILTFOR="$HERE/engine-controller-built-for.json"

CX="$APP/Contents/SharedSupport/CrossOver"
SYS_DEST="$CX/lib/wine/x86_64-windows/winebus.sys"
DLL_DEST="$CX/lib/wine/x86_64-windows/setupapi.dll"
KRN_DEST="$CX/lib/wine/x86_64-windows/ntoskrnl.exe"

[ -d "$CX" ] || { echo "error: not a CrossOver app: $APP" >&2; exit 1; }

status() {
  if [ -f "$SYS_DEST.mgvf-stock" ] && [ -f "$DLL_DEST.mgvf-stock" ] && [ -f "$KRN_DEST.mgvf-stock" ]; then
    echo installed
  elif [ -f "$SYS_DEST.mgvf-stock" ] || [ -f "$DLL_DEST.mgvf-stock" ] || [ -f "$KRN_DEST.mgvf-stock" ]; then
    # Some but not all: the three only work together. mgvf-0004 exists because
    # mgvf-0002 and mgvf-0003 alone still read the record of the first boot.
    echo broken
  else
    echo absent
  fi
}

# A bottle that is up has winebus.sys and ntoskrnl.exe loaded in its
# winedevice.exe, and every process it starts loads setupapi.dll at start. A
# swap while it runs is safe for the files -- a process holding the old file
# keeps the old file -- but it leaves the bottle half on one set and half on the
# other until it is shut down, and a toggle that reports success while nothing
# changes is worse than one that refuses. Both directions, for the same reason.
refuse_if_bottle_up() {
  if /usr/bin/pgrep -f "winedevice.exe" >/dev/null 2>&1; then
    echo "error: a wine bottle is running (winedevice.exe). Close Steam, let the bottle shut down, and re-run." >&2
    exit 1
  fi
}

# The three files are sealed resources of the bundle: replacing them breaks the
# signature, and a broken seal is what Finder calls "damaged". Sign, then clear
# attributes, in that order (see patching-a-crossover-copy).
reseal() {
  /usr/bin/codesign --force --deep --sign - "$APP" >/dev/null 2>&1 \
    || echo "  warning: re-signing failed; the app may be reported as damaged" >&2
  /usr/bin/xattr -cr "$APP" 2>/dev/null || true
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
      refuse_if_bottle_up
      n=0
      for d in "$SYS_DEST" "$DLL_DEST" "$KRN_DEST"; do
        if [ -f "$d.mgvf-stock" ]; then mv -f "$d.mgvf-stock" "$d"; n=$((n+1)); fi
      done
      if [ "$n" -gt 0 ]; then
        reseal
        echo "restored $n of 3"
      else
        echo "nothing to restore"
      fi
      exit 0 ;;
  install) ;;
  *) usage ;;
esac

for f in "$SYS" "$DLL" "$KRN" "$BUILTFOR"; do
  [ -f "$f" ] || { echo "error: $(basename "$f") is not beside this script" >&2; exit 1; }
done

# Refuse an engine these were not built for.
#
# The version alone is not enough: a patched fork and stock CrossOver report
# the SAME CFBundleVersion, 26.3.0.39832 for both, so the stamp records the app
# name as well. A copy this project made is not a different engine; it is the
# same engine under another name, and the name is the only thing the guard can
# see. So the copy records where it came from, in mgvf-origin.json, and the
# stamp naming the original serves it. Nothing else is accepted: an engine with
# no marker and no matching name is refused, with the stamp shown.
want_app="$(/usr/bin/sed -n 's/.*"engine_app": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
want_engine="$(/usr/bin/sed -n 's/.*"engine_version": *"\([^"]*\)".*/\1/p' "$BUILTFOR")"
target_app="$(basename "$APP")"
if [ -n "$want_app" ] && [ "$want_app" != "$target_app" ]; then
  origin="$CX/mgvf-origin.json"
  if [ -f "$origin" ]; then
    from="$(/usr/bin/sed -n 's/.*"copied_from": *"\([^"]*\)".*/\1/p' "$origin")"
    if [ -n "$from" ] && [ "$from" = "$want_app" ]; then
      echo "note: $target_app records itself as a copy of $from made by this project;"
      echo "      using the set built for $from."
      target_app="$from"
    fi
  fi
fi
if [ -n "$want_app" ] && [ "$want_app" != "$target_app" ]; then
  echo "error: these were built for $want_app and this is $(basename "$APP")." >&2
  echo "       Both report the same version, so the name is the only thing that" >&2
  echo "       tells them apart. Refusing rather than writing into the wrong" >&2
  echo "       engine -- pass the app these were built for, or rebuild with" >&2
  echo "       scripts/build-controller-bus.sh against this one." >&2
  exit 1
fi
have_engine="$(/usr/bin/defaults read "$APP/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo "")"
if [ -z "$have_engine" ]; then
  echo "error: could not read a version out of $APP" >&2
  exit 1
fi
if [ "$want_engine" != "$have_engine" ]; then
  echo "error: these were built for engine $want_engine and this is $have_engine." >&2
  echo "       Refusing rather than installing a winebus, setupapi and ntoskrnl" >&2
  echo "       built from a different wine. Rebuild with" >&2
  echo "       scripts/build-controller-bus.sh against this engine, or leave it alone." >&2
  exit 1
fi

refuse_if_bottle_up

install_one() {
  local src="$1" dest="$2"
  [ -f "$dest" ] || { echo "error: no $dest to replace" >&2; exit 1; }

  # Back up the ORIGINAL, and never our own build. Presence answers "have I run
  # before"; the question is "is what I am about to overwrite the original",
  # and only the content answers that. A backup that is byte for byte our own
  # build makes --restore reinstall the patch and report success, so it stops
  # here rather than compounding it. (install-engine-media.sh learned this the
  # hard way; the reasoning is written out there.)
  if [ -f "$dest.mgvf-stock" ]; then
    if cmp -s "$src" "$dest.mgvf-stock"; then
      echo "error: $dest.mgvf-stock is this same build, not the original." >&2
      echo "       --restore would reinstall the patch and report success." >&2
      echo "       Delete it, put the real original back at $dest, and re-run." >&2
      exit 1
    fi
  elif ! cmp -s "$src" "$dest"; then
    cp -p "$dest" "$dest.mgvf-stock"
  else
    echo "  note: $(basename "$dest") is already this build; no backup taken" >&2
  fi

  # By rename, so a process that has the old file mapped keeps the old file.
  cp "$src" "$dest.mgvf-new" && mv -f "$dest.mgvf-new" "$dest"
  echo "  $(basename "$dest")  <- $(basename "$src")"
}
install_one "$SYS" "$SYS_DEST"
install_one "$DLL" "$DLL_DEST"
install_one "$KRN" "$KRN_DEST"
reseal
echo "installed into $(basename "$APP") ($have_engine)"
