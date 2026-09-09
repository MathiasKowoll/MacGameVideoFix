#!/bin/bash
#
# Build the three PE files that let a Windows client learn which bus a
# controller is on: winebus.sys (mgvf-0002, and mgvf-0005 and mgvf-0007 for the
# pad that must not be told), setupapi.dll (mgvf-0003) and ntoskrnl.exe
# (mgvf-0004).
#
#     scripts/build-controller-bus.sh
#
# WHY THIS EXISTS. A DualSense rumbles over USB and never over Bluetooth in every
# Steam game under CrossOver on macOS. Measured on 2026-09-08: over Bluetooth the
# pad wants report 0x31 with a CRC, and no client under wine ever sends one,
# because hidapi decides USB against Bluetooth by asking the HID device's parent
# devnode for a BTHENUM compatible id -- and under wine CM_Get_Parent is a stub
# and winebus reports no bus at all. Steam's own log says "bluetooth 0" for a pad
# on Bluetooth. The three patches make the truth reachable; this builds them.
# mgvf-0005 is the opposite, for the two consumers that turned out not to want
# the truth: an opt-in per-device option that presents a DualSense on
# Bluetooth as if it were on USB, off by default, so the shipped winebus.sys
# behaves exactly as before unless a registry value says otherwise. mgvf-0007
# is the bill for that lie: a client told the pad is wired asks for the pad's
# speaker, headphone jack and microphone, which are what a cable is for, so the
# emulation takes those fields back out of the report on the pad's behalf and
# leaves every other byte as the client wrote it.
#
# TWO PATCHES IN source-patches/ ARE NOT IN THIS LIST, on purpose. mgvf-0006
# seizes the pad from macOS and mgvf-0008 answers a feature write without
# sending it; both were written, built and run, and neither ships. mgvf-0006 is
# out because what it was built to stop -- macOS and wine writing to one
# Bluetooth output pipe -- turned out not to be what made the pad drop its
# link, and the one thing a user sees from it is that macOS and its own
# applications cannot use the pad while a bottle holds it. mgvf-0008 was an
# experiment and it has been run: with the feature write forwarded again the pad
# did not die either, so the question is answered and the instrument comes out.
# Both patch files stay in source-patches/ as the record of the work, and
# source-patches/README.md says the same there.
#
# It reuses build-winegstreamer.sh's tree, on purpose: same sources, same
# configure, same toolchain, same engine stamp. Run that first. The patches touch
# only the PE side (winebus's unixlib interface is unchanged), so only the three
# PE files are produced; there is no unix half to pair them with.
#
# STRIPPED, AND PROVED SO. The configured tree compiles PE with -g and links with
# -Wl,-debug:dwarf, so what make produces carries six .debug_* sections and a
# COFF symbol table: 200 KB where CodeWeavers' winebus.sys is 43 KB, 1.8 MB
# where their setupapi.dll is 478 KB. The engine gets the stripped file and the
# build directory keeps the unstripped one as <name>.unstripped. Stripping is
# not taken on faith: the export and import tables of the stripped file are
# compared with the unstripped one and the stamp is not written if they differ.
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
for t in llvm-strip llvm-readobj llvm-objdump; do
  command -v "$t" >/dev/null 2>&1 || { say "no $t on PATH -- it ships with llvm-mingw"; exit 1; }
done

# ---- 1. our patches, on top of whatever the winegstreamer build applied ------
#
# The tree is reused between runs, so each patch has to be asked whether it is
# in it already. That used to be a reverse dry run of the one patch, on its own,
# in the tree -- macOS's BSD patch and GNU patch word the "previously applied"
# message differently, so the exit status of a reversal was the portable answer.
# It stopped being a correct answer with mgvf-0007, which rewrites lines
# mgvf-0005 added: in a tree carrying both, mgvf-0005 will not reverse, because
# the context it wants is the text mgvf-0007 replaced, and the build then
# reported a patch it had applied itself as FAILED.
#
# So the question is asked where it can be answered: in a SCRATCH COPY of the
# files the set touches, with the stack taken back off it LAST FIRST. Reversing
# mgvf-0007 there puts the copy back into the state mgvf-0005 was applied to,
# and mgvf-0005's own reversal then says what it always meant. Nothing is
# written to the tree by the test; the tree is only ever forward-applied to,
# and in order.
PATCHSET="mgvf-0002 mgvf-0003 mgvf-0004 mgvf-0005 mgvf-0007"
patch_file() { ls "$OWNPATCHES/$1"-*.patch 2>/dev/null | head -1; }
for p in $PATCHSET; do
  [ -n "$(patch_file "$p")" ] || { say "no patch numbered $p in $OWNPATCHES"; exit 1; }
done

SCRATCH="$(mktemp -d)"; trap 'rm -rf "$SCRATCH"' EXIT
for f in $(for p in $PATCHSET; do /usr/bin/sed -n 's|^--- a/||p' "$(patch_file "$p")"; done | sort -u); do
  [ -f "$TREE/$f" ] || continue
  mkdir -p "$SCRATCH/$(dirname "$f")"; cp "$TREE/$f" "$SCRATCH/$f"
done
have=""
for p in $(printf '%s\n' $PATCHSET | /usr/bin/sed -n '1!G;h;$p'); do
  ( cd "$SCRATCH" && patch -p1 -R -l -F3 <"$(patch_file "$p")" >/dev/null 2>&1 ) && have="$have $p "
done

for p in $PATCHSET; do
  f="$(patch_file "$p")"
  case "$have" in
    *" $p "*) say "already applied $(basename "$f")" ;;
    *)
      if ( cd "$TREE" && patch -p1 --forward -l -F3 <"$f" >/dev/null 2>&1 ); then
        say "applied $(basename "$f")"
      else
        say "FAILED to apply $(basename "$f")"; exit 1
      fi ;;
  esac
done

# ---- 2. just the three PE files -----------------------------------------------
( cd "$OUT/wine-build" && make -j8 dlls/winebus.sys/x86_64-windows/winebus.sys \
                                  dlls/setupapi/x86_64-windows/setupapi.dll \
                                  dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe >"$OUT/make-controller.log" 2>&1 ) \
  || { say "make failed, see $OUT/make-controller.log"; exit 1; }

SYS="$OUT/wine-build/dlls/winebus.sys/x86_64-windows/winebus.sys"
DLL="$OUT/wine-build/dlls/setupapi/x86_64-windows/setupapi.dll"
KRN="$OUT/wine-build/dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe"
[ -f "$SYS" ] && [ -f "$DLL" ] && [ -f "$KRN" ] || { say "build produced no output"; exit 1; }
cp "$SYS" "$OUT/winebus.sys.unstripped"
cp "$DLL" "$OUT/setupapi.dll.unstripped"
cp "$KRN" "$OUT/ntoskrnl.exe.unstripped"

# ---- 3. strip, and prove the strip changed nothing that matters ---------------
#
# --strip-all, not --strip-debug: CodeWeavers' files have no symbol table and no
# debug sections, and the stripped sizes land within 10 KB of theirs. What a
# loader reads is the export table and the import table, so those are dumped
# from both files (Name: and Symbol: lines, sorted) and compared; a difference
# is a reason to stop, not a warning. The build-tree file keeps its symbols for
# debugging, under the .unstripped name beside the shipped one.
tables() { llvm-readobj --coff-exports --coff-imports "$1" | /usr/bin/grep -E '^[[:space:]]*(Name|Symbol):' | sort; }
for name in winebus.sys setupapi.dll ntoskrnl.exe; do
  full="$OUT/$name.unstripped"; lean="$OUT/$name"
  llvm-strip --strip-all "$full" -o "$lean"
  if ! cmp -s <(tables "$full") <(tables "$lean"); then
    say "$name: the export or import table changed when stripped; refusing to stamp"
    comm -3 <(tables "$full") <(tables "$lean") | head -20 | sed 's/^/      /'
    rm -f "$lean"; exit 1
  fi
  if llvm-objdump -h "$lean" | /usr/bin/grep -q '\.debug_'; then
    say "$name: still carries a .debug_ section after the strip; refusing to stamp"; rm -f "$lean"; exit 1
  fi
  syms="$(llvm-readobj --file-header "$lean" | /usr/bin/sed -n 's/.*SymbolCount: *//p')"
  [ "$syms" = 0 ] || { say "$name: SymbolCount is $syms after the strip, not 0; refusing to stamp"; rm -f "$lean"; exit 1; }
  exports="$(llvm-readobj --coff-exports "$lean" | /usr/bin/grep -cE '^[[:space:]]*Name:' || true)"
  say "$name: $(stat -f %z "$full") -> $(stat -f %z "$lean") bytes, $exports exports, tables identical, no symbols, no debug sections"
done

# ---- 4. the stamp the installer matches on, same fields as built-for.json ----
#
# engine_app is the ORIGIN, not the copy. A copy this project made records what
# it was copied from in mgvf-origin.json, and install-engine-controller.sh
# serves a copy through that record the way install-engine-media.sh does; so a
# stamp naming the copy would serve that one copy and refuse the engine it came
# from, and every other copy made from it. Nothing in these three files links
# against the engine, so one build serves every engine of that name and
# version. An engine with no marker is stamped by its own name, as before.
ENGINE_APP="$(cd "$ENGINE/../../.." && pwd)"
STAMP_APP="$(basename "$ENGINE_APP")"
if [ -f "$ENGINE/mgvf-origin.json" ]; then
  from="$(/usr/bin/sed -n 's/.*"copied_from": *"\([^"]*\)".*/\1/p' "$ENGINE/mgvf-origin.json")"
  [ -n "$from" ] && STAMP_APP="$from"
fi
cat > "$OUT/controller-built-for.json" <<JSON
{
  "engine_app": "$STAMP_APP",
  "engine_version": "$(defaults read "$ENGINE_APP/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo unknown)",
  "wine_build": "$(strings -a "$ENGINE/lib/wine/x86_64-unix/ntdll.so" | grep -oE 'wine-[0-9]+\.[0-9]+[^ ]*' | head -1)",
  "patches": "mgvf-0002 mgvf-0003 mgvf-0004 mgvf-0005 mgvf-0007"
}
JSON
say "built for: $(sed -n 's/.*"engine_app": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json") / $(sed -n 's/.*"wine_build": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json")"
say "built:  $OUT/winebus.sys  ($(stat -f %z "$OUT/winebus.sys") bytes)"
say "        $OUT/setupapi.dll ($(stat -f %z "$OUT/setupapi.dll") bytes)"
say "        $OUT/ntoskrnl.exe ($(stat -f %z "$OUT/ntoskrnl.exe") bytes)"
say "put it in the repository with: scripts/install-controller-build.sh"
