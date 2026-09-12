#!/bin/bash
#
# Build the four engine files that let a Windows client learn which bus a
# controller is on, and let wine keep the pad to itself while it has it:
# winebus.sys (mgvf-0002, and mgvf-0005, mgvf-0007, mgvf-0008 and mgvf-0009 for
# the pad that must not be told), setupapi.dll (mgvf-0003), ntoskrnl.exe (mgvf-0004) --
# three PE files -- and winebus.so (mgvf-0006), the UNIX half of winebus.
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
# leaves every other byte as the client wrote it. mgvf-0008 is the odd one out
# and is marked as such wherever it is named: an EXPERIMENT rather than a fix.
# In the emulation path a feature-report WRITE is answered as if it had
# succeeded and no byte of it goes to the pad, because the feature write is the
# one piece of traffic present in every session the pad left within seconds and
# absent from the one it stayed in. It is what a fresh install does, and a
# registry value puts the write back on the wire without another build. What it
# does not establish is in its own header, at length. mgvf-0009 is the newest
# and is neither a repair nor an experiment but a PREFERENCE: two per-device
# values, both absent by default, that rewrite the vibration a client asks for
# -- the haptic path becomes the legacy compatible motors, which one person
# reported feels clearly stronger, and a percentage scales the motor bytes. It
# is the only one of the set that acts on the pad's own 0x31 as well as on
# mgvf-0005's translation, because the title it was measured on writes that
# report itself.
#
# AND WHY THERE IS NOW A FOURTH FILE. mgvf-0006 changes how winebus OPENS the
# pad, and that is bus_iohid.c, which compiles into the unix half. Measured
# from macOS's own log on 2026-09-08: macOS drives a connected DualSense itself
# and writes Bluetooth output reports to it, winebus opened the same pad shared
# and wrote its own, and macOS's writes then time out -- 163 of them in a day,
# every one inside a minute a game was running under wine -- after which its
# driver tears itself down and the Bluetooth link drops. So the pad is seized,
# and the set grows a unix half: dlls/winebus.sys/winebus.so, which the engine
# has been carrying as CodeWeavers' own build until now.
#
# It reuses build-winegstreamer.sh's tree, on purpose: same sources, same
# configure, same toolchain, same engine stamp. Run that first. winebus's
# unixlib interface is unchanged -- mgvf-0006 adds one INT to struct
# device_options, which both halves compile from the same header -- but the two
# halves of winebus are still built and shipped together, because a struct they
# both read changed shape.
#
# STRIPPED, AND PROVED SO. The configured tree compiles PE with -g and links with
# -Wl,-debug:dwarf, so what make produces carries six .debug_* sections and a
# COFF symbol table: 236 KB where CodeWeavers' winebus.sys is 43 KB, 1.8 MB
# where their setupapi.dll is 478 KB. The engine gets the stripped file and the
# build directory keeps the unstripped one as <name>.unstripped. Stripping is
# not taken on faith: the export and import tables of the stripped file are
# compared with the unstripped one and the stamp is not written if they differ.
#
# The .so gets the same treatment in the terms a Mach-O has. It has no COFF
# tables, so what is compared across the strip is the EXPORTED SYMBOL LIST and
# the LINKED LIBRARIES -- what a loader reads -- and what must be gone
# afterwards is the local symbol table rather than a .debug_ section: macOS
# keeps DWARF in the .o files, so an unstripped .so carries 143 local symbols
# and no debug section at all. CodeWeavers ship theirs with none either.
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
# mgvf-0008 rewrites mgvf-0007's lines the same way, and mgvf-0009 rewrites
# lines of both, so the question was asked again with eight in the list: the
# ordered reversal answers all eight correctly, and needed no change to do it.
#
# So the question is asked where it can be answered: in a SCRATCH COPY of the
# files the set touches, with the stack taken back off it LAST FIRST. Reversing
# mgvf-0007 there puts the copy back into the state mgvf-0005 was applied to,
# and mgvf-0005's own reversal then says what it always meant. Nothing is
# written to the tree by the test; the tree is only ever forward-applied to,
# and in order.
PATCHSET="mgvf-0002 mgvf-0003 mgvf-0004 mgvf-0005 mgvf-0006 mgvf-0007 mgvf-0008 mgvf-0009 mgvf-0010 mgvf-0011 mgvf-0012 mgvf-0014 mgvf-0016 mgvf-0017 mgvf-0018 mgvf-0019 mgvf-0020 mgvf-0021 mgvf-0022 mgvf-0023 mgvf-0024 mgvf-0025 mgvf-0026"
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

# ---- 2. the three PE files and the unix half ----------------------------------
#
# The unix half is dlls/winebus.sys/winebus.so in this tree, not under an
# x86_64-unix/ directory: the PE halves are built into a per-arch subdirectory
# and the unix half is not, which is the same shape winegstreamer has and the
# reason build-winegstreamer.sh reads its .so from the module directory too.
# hidclass.sys and the five xinput DLLs joined the set with mgvf-0011 and
# mgvf-0012. wine builds xinput1_1, 1_2, 1_4 and xinputuap from xinput1_3's
# sources, so one patch produces five binaries and all five have to ship: a
# game links whichever it was built against, and the one it links is the one
# that must know how to read a Sony pad. xinput9_1_0 is NOT here on purpose --
# it is a 20 KB forwarder that loads its functions from xinput1_4.dll, which is.
( cd "$OUT/wine-build" && make -j8 dlls/winebus.sys/x86_64-windows/winebus.sys \
                                  dlls/setupapi/x86_64-windows/setupapi.dll \
                                  dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe \
                                  dlls/hidclass.sys/x86_64-windows/hidclass.sys \
                                  dlls/xinput1_1/x86_64-windows/xinput1_1.dll \
                                  dlls/xinput1_2/x86_64-windows/xinput1_2.dll \
                                  dlls/xinput1_3/x86_64-windows/xinput1_3.dll \
                                  dlls/xinput1_4/x86_64-windows/xinput1_4.dll \
                                  dlls/xinputuap/x86_64-windows/xinputuap.dll \
                                  dlls/winebus.sys/winebus.so >"$OUT/make-controller.log" 2>&1 ) \
  || { say "make failed, see $OUT/make-controller.log"; exit 1; }

SYS="$OUT/wine-build/dlls/winebus.sys/x86_64-windows/winebus.sys"
DLL="$OUT/wine-build/dlls/setupapi/x86_64-windows/setupapi.dll"
KRN="$OUT/wine-build/dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe"
USO="$OUT/wine-build/dlls/winebus.sys/winebus.so"

# Every PE file of the set, and where make left it. Named once here so that the
# strip, the checks and the stamp all walk the same list.
PE_FILES="winebus.sys setupapi.dll ntoskrnl.exe hidclass.sys \
          xinput1_1.dll xinput1_2.dll xinput1_3.dll xinput1_4.dll xinputuap.dll"
pe_built() {
  case "$1" in
    winebus.sys)  echo "$OUT/wine-build/dlls/winebus.sys/x86_64-windows/winebus.sys" ;;
    setupapi.dll) echo "$OUT/wine-build/dlls/setupapi/x86_64-windows/setupapi.dll" ;;
    ntoskrnl.exe) echo "$OUT/wine-build/dlls/ntoskrnl.exe/x86_64-windows/ntoskrnl.exe" ;;
    hidclass.sys) echo "$OUT/wine-build/dlls/hidclass.sys/x86_64-windows/hidclass.sys" ;;
    *)            echo "$OUT/wine-build/dlls/${1%.dll}/x86_64-windows/$1" ;;
  esac
}
for name in $PE_FILES; do
  built="$(pe_built "$name")"
  [ -f "$built" ] || { say "build produced no $name"; exit 1; }
  cp "$built" "$OUT/$name.unstripped"
done
[ -f "$USO" ] || { say "build produced no winebus.so"; exit 1; }
cp "$USO" "$OUT/winebus.so.unstripped"

# ---- 3. strip, and prove the strip changed nothing that matters ---------------
#
# --strip-all, not --strip-debug: CodeWeavers' files have no symbol table and no
# debug sections, and the stripped sizes land within 10 KB of theirs. What a
# loader reads is the export table and the import table, so those are dumped
# from both files (Name: and Symbol: lines, sorted) and compared; a difference
# is a reason to stop, not a warning. The build-tree file keeps its symbols for
# debugging, under the .unstripped name beside the shipped one.
tables() { llvm-readobj --coff-exports --coff-imports "$1" | /usr/bin/grep -E '^[[:space:]]*(Name|Symbol):' | sort; }
for name in $PE_FILES; do
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

# ---- 3b. the unix half: the same argument, in the terms a Mach-O has ---------
#
# strip -S -x: -S drops the debugging entries, -x the local symbols. The global
# symbol table is what a loader reads and it is left alone -- and then checked
# rather than trusted, exactly as the COFF tables are above: the exported
# symbols and the linked libraries are listed from both files and compared, and
# a difference stops the build. What must be GONE afterwards is the local
# symbol table, not a .debug_ section: macOS leaves the DWARF in the .o files,
# so the unstripped .so has 143 local symbols and no debug section at all, and
# CodeWeavers' own winebus.so has neither.
#
# The load is checked too, the way build-winegstreamer.sh checks its own .so:
# an @rpath dependency that is not in the engine beside it, or a surviving
# /opt/cxoffice path -- CodeWeavers' build prefix, which exists on no Mac --
# is a file that cannot load its own dependencies. This one links ntdll.so, two
# system frameworks and libSystem, so there is a single @rpath name to resolve
# and its own LC_RPATH of @loader_path/ resolves it in the same directory.
# CodeWeavers' build carries two further rpaths into lib64, for the libinotify
# it links and this configure does not: inotify is used only by bus_udev.c,
# which no macOS engine compiles in.
exports_of() { nm -gU "$1" | sort; }
libs_of()    { otool -L "$1" | tail -n +2 | sort; }
full="$OUT/winebus.so.unstripped"; lean="$OUT/winebus.so"
/usr/bin/strip -S -x -o "$lean" "$full"
if ! cmp -s <(exports_of "$full") <(exports_of "$lean"); then
  say "winebus.so: the exported symbols changed when stripped; refusing to stamp"
  comm -3 <(exports_of "$full") <(exports_of "$lean") | head -20 | sed 's/^/      /'
  rm -f "$lean"; exit 1
fi
if ! cmp -s <(libs_of "$full") <(libs_of "$lean"); then
  say "winebus.so: the linked libraries changed when stripped; refusing to stamp"
  comm -3 <(libs_of "$full") <(libs_of "$lean") | head -20 | sed 's/^/      /'
  rm -f "$lean"; exit 1
fi
locals="$(nm -a "$lean" | /usr/bin/grep -cE '^[0-9a-f]+ [a-z] ' || true)"
[ "$locals" = 0 ] || { say "winebus.so: $locals local symbols survived the strip, not 0; refusing to stamp"; rm -f "$lean"; exit 1; }
if otool -L "$lean" | /usr/bin/grep -q "/opt/cxoffice"; then
  say "winebus.so: an absolute cxoffice path survived; it could not load its own dependencies"; rm -f "$lean"; exit 1
fi
for d in $(otool -L "$lean" | /usr/bin/grep -oE "@rpath/[^ ]+" | /usr/bin/sed 's|@rpath/||'); do
  [ "$d" = "winebus.so" ] && continue
  [ -f "$ENGINE/lib/wine/x86_64-unix/$d" ] || { say "winebus.so: unresolved dependency $d"; rm -f "$lean"; exit 1; }
done
say "winebus.so: $(stat -f %z "$full") -> $(stat -f %z "$lean") bytes, $(exports_of "$lean" | wc -l | tr -d ' ') exported symbols, $(libs_of "$lean" | wc -l | tr -d ' ') linked libraries, lists identical, no local symbols"

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
  "patches": "$PATCHSET"
}
JSON
say "built for: $(sed -n 's/.*"engine_app": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json") / $(sed -n 's/.*"wine_build": "\(.*\)".*/\1/p' "$OUT/controller-built-for.json")"
for name in $PE_FILES; do
  say "built:  $OUT/$name ($(stat -f %z "$OUT/$name") bytes)"
done
say "built:  $OUT/winebus.so ($(stat -f %z "$OUT/winebus.so") bytes)  -- the unix half"
say "put it in the repository with: scripts/install-controller-build.sh"
