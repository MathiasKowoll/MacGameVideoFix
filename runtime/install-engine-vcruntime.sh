#!/usr/bin/env bash
#
# Raise the version the engine's builtin C++ runtime DLLs report, so that a
# launcher's "is the Visual C++ runtime new enough" check stops failing.
#
#   install-engine-vcruntime.sh <engine app>            install
#   install-engine-vcruntime.sh <engine app> --restore  remove
#   install-engine-vcruntime.sh <engine app> --status   report what is in place
#
# <engine app> is the .app itself, e.g. ~/Applications/Crossover_MGVF.app
#
# WHAT IT FIXES. Unreal 5.6's BootstrapPackagedGame -- the small executable at
# the root of a packaged game -- asks on every launch for "Microsoft Visual C++
# 2015-2022 Redistributable (x64)" when the runtime is older than 14.42.34438.
# It reads the registry (the bottle says 14.51 and passes) and then the file
# version of %SystemRoot%\system32\msvcp140_2.dll and vcruntime140_1.dll. Wine
# does not open the native files that sit there: its load order maps its own
# builtins, and CrossOver 26.3's builtins report 14.42.34433.0 -- five builds
# short. Measured on Silent Hill: Townfall with a +ver,+file,+module trace; the
# dialog is the only consequence, the game itself runs either way.
#
# WHAT IT CHANGES. Only numbers. In each builtin that carries a version
# resource, the VS_FIXEDFILEINFO file and product versions and the two
# FileVersion/ProductVersion strings become 14.51.36247.0 -- the version of the
# Visual C++ runtime installed into the bottles here. No code, no export and no
# load order changes: every program keeps running exactly the DLL it ran
# before. Five files carry a version at all; the rest of the family (vcruntime140,
# msvcp140_1, concrt140, ...) has none to change and is left alone.
#
# WHAT IT COULD AFFECT, reasoned rather than measured: a program that reads
# these versions and takes a different path for a newer runtime. A checker like
# the bootstrapper stops complaining, which is the point. A vc_redist installer
# older than 14.51 may decide a newer runtime is present and skip copying its
# native DLLs -- which changes nothing for a title on wine's default load order,
# since that loads the builtins anyway.
#
# Every original is kept as <file>.mgvf-stock, and --restore puts them back.
# Like every change to an engine, this must run BEFORE the engine is signed;
# make-engine-copy.sh calls it in that order.
#
# MGVF-SCOPE: engine
# Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() { sed -n '6,10p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

APP="${1%/}"
ACTION="${2:-install}"
# A read-only caller sets MGVF_STATUS_ONLY=1; see install-engine-media.sh for
# why this is structural rather than left to the literal --status.
if [ "${MGVF_STATUS_ONLY:-0}" = 1 ]; then ACTION=--status; fi

CX="$APP/Contents/SharedSupport/CrossOver"
[ -d "$CX/lib/wine" ] || { echo "error: not a CrossOver app: $APP" >&2; exit 1; }

WANT="14.51.36247.0"
# Named, not globbed: these are the builtins that carry a version resource on
# this engine. A name missing from another engine is skipped, not an error --
# the i386 side has never had vcruntime140_1.
TARGETS="x86_64-windows/msvcp140.dll x86_64-windows/msvcp140_2.dll
x86_64-windows/vcruntime140_1.dll i386-windows/msvcp140.dll i386-windows/msvcp140_2.dll"

# The file version a DLL reports, as a.b.c.d, or "none".
version_of() {
  /usr/bin/perl -0777 -ne '
    my $i = index($_, pack("V", 0xFEEF04BD));
    if ($i < 0) { print "none"; exit }
    my ($ms, $ls) = unpack("VV", substr($_, $i + 8, 8));
    printf "%d.%d.%d.%d", $ms >> 16, $ms & 0xffff, $ls >> 16, $ls & 0xffff;
  ' "$1"
}

# True when version $1 is lower than version $2.
older() {
  [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -t. -k1,1n -k2,2n -k3,3n -k4,4n | head -1)" = "$1" ]
}

# Rewrites the version in place. The strings are rewritten only when the old
# and new text are the same length, which they are for 14.42.34433.0; the
# fixed-size structure is what every checker reads, and it is always written.
patch_file() {
  /usr/bin/perl -0777 -i -pe '
    BEGIN { ($want) = @ARGV[0]; shift @ARGV; }
    my ($a, $b, $c, $d) = split /\./, $want;
    my $i = index($_, pack("V", 0xFEEF04BD));
    die "no VS_FIXEDFILEINFO\n" if $i < 0;
    my ($ms, $ls) = unpack("VV", substr($_, $i + 8, 8));
    my $old = sprintf "%d.%d.%d.%d", $ms >> 16, $ms & 0xffff, $ls >> 16, $ls & 0xffff;
    my $newms = ($a << 16) | $b;  my $newls = ($c << 16) | $d;
    substr($_, $i + 8, 16) = pack("VVVV", $newms, $newls, $newms, $newls);
    if (length($old) == length($want)) {
      my $from = join("", map { $_ . "\0" } split //, $old);
      my $to   = join("", map { $_ . "\0" } split //, $want);
      my $n = s/\Q$from\E/$to/g;
      print STDERR "  strings: $n\n";
    } else {
      print STDERR "  strings: left as $old (a different length from $want)\n";
    }
  ' "$WANT" "$1"
}

status() {
  local ours=0 stock=0 other=0 missing=0 f v
  for t in $TARGETS; do
    f="$CX/lib/wine/$t"
    if [ ! -f "$f" ]; then missing=$((missing + 1)); continue; fi
    v=$(version_of "$f")
    if [ "$v" = "$WANT" ] && [ -f "$f.mgvf-stock" ]; then ours=$((ours + 1))
    elif [ -f "$f.mgvf-stock" ]; then other=$((other + 1))
    else stock=$((stock + 1)); fi
  done
  if [ "$other" -gt 0 ]; then echo broken
  elif [ "$ours" -gt 0 ] && [ "$stock" -eq 0 ]; then echo installed
  elif [ "$ours" -eq 0 ]; then echo absent
  else echo broken
  fi
}

case "$ACTION" in
  --status) status; exit 0 ;;
  --restore)
    n=0
    for t in $TARGETS; do
      f="$CX/lib/wine/$t"
      if [ -f "$f.mgvf-stock" ]; then mv -f "$f.mgvf-stock" "$f"; n=$((n + 1)); fi
    done
    [ "$n" -gt 0 ] && echo "restored $n" || echo "nothing to restore"
    exit 0 ;;
  install) ;;
  *) usage ;;
esac

done_any=0
for t in $TARGETS; do
  f="$CX/lib/wine/$t"
  [ -f "$f" ] || { echo "  $t: not in this engine, skipped"; continue; }
  v=$(version_of "$f")
  if [ "$v" = none ]; then
    echo "  $t: carries no version, skipped"
    continue
  fi
  if ! older "$v" "$WANT"; then
    echo "  $t: already $v"
    continue
  fi
  # Back up the ORIGINAL, and never a copy this has already rewritten -- the
  # lesson install-engine-media.sh learned: a backup of the patch makes
  # --restore reinstall the patch and report success.
  if [ -f "$f.mgvf-stock" ]; then
    b=$(version_of "$f.mgvf-stock")
    if [ "$b" = "$WANT" ]; then
      echo "error: $f.mgvf-stock already reports $WANT, so it is not the original." >&2
      echo "       Delete it, put the engine's own file back, and run again." >&2
      exit 1
    fi
  else
    cp -p "$f" "$f.mgvf-stock"
  fi
  echo "  $t: $v -> $WANT"
  patch_file "$f"
  got=$(version_of "$f")
  [ "$got" = "$WANT" ] || { echo "error: $t reads back $got" >&2; exit 1; }
  done_any=1
done
[ "$done_any" = 1 ] && echo "installed into $(basename "$APP")" || echo "nothing to change in $(basename "$APP")"
