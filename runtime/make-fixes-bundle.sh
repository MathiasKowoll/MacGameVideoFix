#!/usr/bin/env bash
#
# Build the fixes bundle that a launcher downloads: everything needed to apply
# a per-game fix, and nothing else.
#
#     runtime/make-fixes-bundle.sh [output directory]
#
# WHAT GOES IN, AND WHY IT IS DERIVED
#
# The file list is read from the installers themselves rather than written
# here. Each install-*.sh resolves its carrier DLL and the export reader
# through HERE="$(dirname "$0")", so what it needs is exactly what it names --
# and a hand-written list is the second copy that goes stale the first time a
# title is added. The same reasoning produces manifest.json: which DLL belongs
# to which game, which carrier it rides, under what name the original is kept,
# and whether the fix needs a registry override, all read out of the scripts.
#
# EVERYTHING IS FLAT. The installers resolve siblings with $HERE, so a bundle
# with subdirectories breaks every one of them.
#
# NO EXTERNAL DEPENDENCY. The scripts read PE exports with /usr/bin/perl,
# which macOS ships. /usr/bin/python3 is not Python -- it is one of 78 hard
# links to the xcrun dispatcher, and on a Mac without developer tools it opens
# a dialog and fails. A compiled helper is not an option either: downloaded
# binaries arrive quarantined and Gatekeeper stops them, text scripts do not.
#
# PROVENANCE IS NOT OPTIONAL. The manifest records the commit and says plainly
# whether the tree was clean and whether that commit is a released tag. An
# asset attached to a release must be buildable from that release; publishing
# one that is not is the quiet mismatch this project keeps removing.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
OUT="${1:-$REPO/build}"

command -v /usr/bin/perl >/dev/null || { echo "error: /usr/bin/perl is missing" >&2; exit 1; }

# Provenance, decided before anything is copied.
commit="$(git -C "$REPO" rev-parse --short HEAD)"
dirty=false
git -C "$REPO" diff --quiet && git -C "$REPO" diff --cached --quiet || dirty=true
tag="$(git -C "$REPO" describe --tags --exact-match 2>/dev/null || true)"
released=true
[ -n "$tag" ] || released=false
version="${tag:-$(git -C "$REPO" describe --tags 2>/dev/null || echo "0.0.0")}"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

echo "[1/4] collecting what the installers say they need"
count=0
for f in "$REPO"/runtime/install-*.sh; do
  cp "$f" "$stage/"
  count=$((count + 1))
  # Every $HERE/<file> an installer names is a file it must find beside it.
  while read -r want; do
    [ -n "$want" ] || continue
    [ -f "$REPO/runtime/$want" ] || { echo "error: $(basename "$f") wants $want, which is not in runtime/" >&2; exit 1; }
    cp "$REPO/runtime/$want" "$stage/"
  done < <(grep -oE '\$HERE/[A-Za-z0-9._-]+' "$f" | sed 's|\$HERE/||' | sort -u)
done
echo "      $count installers, $(( $(ls "$stage" | wc -l) - count )) files they name"

# stage-codecs.sh is not named by any installer and is not optional: nine titles
# need a plugin staged in front of CrossOver, and three of them need nothing
# else. Leaving it out meant a launcher could apply the NINJA GAIDEN 4 or
# Persona 5 Strikers fix and hand the user back the exact symptom it was for.
# It travels because it is a script the launcher runs, the same as the others.
cp "$REPO/runtime/stage-codecs.sh" "$stage/"

echo "[2/4] refusing anything that still needs python"
if grep -l 'python3' "$stage"/*.sh >/dev/null 2>&1; then
  echo "error: these installers still call python3, which a clean Mac does not have:" >&2
  grep -l 'python3' "$stage"/*.sh | sed 's|.*/|       |' >&2
  exit 1
fi
echo "      none"

echo "[3/4] writing the manifest"
# What a title needs CONFIGURED, as opposed to installed: which backend it
# requires and whether it is tied to one toolkit generation. Emitted by the file
# that measured it rather than copied here, because a second hand-written list
# is how this repository's counts went wrong twice in one day. Empty is a real
# answer: a generation is reported only where it is a requirement.
CONFIG="$(cd "$REPO/wiki" && python3 games.py --config-json 2>/dev/null)"
ONLYEXE="$(cd "$REPO/wiki" && python3 -c 'import json,games; print(json.dumps(games.CODEC_ONLY_EXE))' 2>/dev/null)"
[ -n "$CONFIG" ] || { echo "error: wiki/games.py --config-json produced nothing" >&2; exit 1; }
/usr/bin/perl - "$stage" "$version" "$commit" "$dirty" "$released" "$REPO" "$CONFIG" "$ONLYEXE" <<'PERL' > "$stage/manifest.json"
use strict; use warnings;
my ($stage, $version, $commit, $dirty, $released, $repo, $config, $onlyexe) = @ARGV;
my %only_exe;
while ($onlyexe =~ /"([^"]+)":\s*"([^"]+)"/g) { $only_exe{$1} = $2; }

# Hand-parsed rather than pulled in with a JSON module: this runs on whatever
# perl the machine has, and the shape is one this file produced a line ago.
my %cfg;
while ($config =~ /"([^"]+)":\s*\{(.*?)\}\s*,?\s*(?="[^"]+":\s*\{|\z)/gs) {
    my ($title, $body) = ($1, $2);
    my ($backend) = $body =~ /"backend":\s*"([^"]*)"/;
    my ($gptk)    = $body =~ /"gptk":\s*"([^"]*)"/;
    my ($env)     = $body =~ /"env":\s*(\{.*?\})/s;
    my ($codec)   = $body =~ /"codec":\s*"([^"]*)"/;
    $cfg{$title} = { backend => $backend // "", gptk => $gptk // "",
                     codec => $codec // "", env => $env // "{}" };
    $cfg{$title}{env} =~ s/\s+/ /g;
}
my @games;
my %seen_title;
for my $f (sort glob("$repo/runtime/install-*.sh")) {
    open my $fh, '<', $f or next;
    my $s = do { local $/; <$fh> };

    # The /m goes INSIDE the qr. A qr carries its own flags, and interpolating
    # it into /$re/m does not add them -- so `^` only matched the very start of
    # the file and every one of these came out empty, in a manifest whose whole
    # job is to carry them.
    my $grab = sub { my ($re) = @_; $s =~ $re ? $1 : "" };
    my %seen; my @files = grep { !$seen{$_}++ } ($s =~ /\$HERE\/([A-Za-z0-9._-]+\.dll)/g);

    # NG4 names its carrier CARRIER= because the file it rides is the game's
    # own DirectStorage rather than something renamed in place. Both spellings
    # are read rather than one being imposed on a script that reads better as
    # it is.
    my $carrier = $grab->(qr/^(?:LIVE|CARRIER)="[^"]*?\/([^"\/]+)"/m);
    my $kept    = $grab->(qr/^REAL="[^"]*?\/([^"\/]+)"/m);
    my $why     = $grab->(qr/^# MGVF-WHY: (.+)$/m);
    my $script  = $f; $script =~ s{.*/}{};
    # reg.exe was the only way this project wrote the registry until NINJA
    # GAIDEN 3, which appends its AppDefaults key to user.reg directly -- so the
    # old test called it false and a launcher would not have known to bring the
    # bottle down first. Both spellings count.
    my $reg     = (($s =~ /reg\.exe/ || $s =~ /user\.reg/) ? "true" : "false");

    # What the installer's first argument IS. Everything here took a game folder
    # until NINJA GAIDEN 3, and a launcher could safely assume it; that one takes
    # a bottle, because its DLLs go into the bottle's system32 and its override
    # is a registry key, neither reachable from the folder a user drops on a
    # window. Absent means folder, so no existing entry changes meaning.
    my $scope   = $grab->(qr/^# MGVF-SCOPE: (\S+)$/m);
    # Declared, never defaulted. Falling back to "folder" would give a new
    # installer that forgot the line the right answer by accident eleven times
    # out of twelve, and the wrong one silently on the twelfth -- which is the
    # failure this whole field exists to prevent. An omission is a mistake and
    # is said so here, where it costs a rerun, rather than at a user's machine.
    die "error: $script declares no # MGVF-SCOPE: line (folder, bottle or engine)\n"
        unless defined $scope && $scope =~ /^(folder|bottle|engine)$/;

    # An engine-scoped installer serves no single title, so it produces no
    # entry in "games" and is described in "engine" below instead. Skipped here
    # rather than filtered later, because a nameless row in a per-title list is
    # a row somebody eventually tries to match to a folder.
    next if $scope eq "engine";
    my $esc = sub { my ($t) = @_; $t //= ""; $t =~ s/(["\\])/\\$1/g; $t };

    # One entry per TITLE, not per script: four of these serve more than one
    # game, and a launcher matching a folder needs the executable, which is the
    # only identity there is. No AppID exists anywhere in this project, and the
    # folder name belongs to Valve.
    $seen_title{$_} = 1 for map { (split(/\|/, $_, 3))[0] =~ s/^\s+|\s+$//gr } ($s =~ /^# MGVF-GAME: (.+)$/mg);
    my @titles = ($s =~ /^# MGVF-GAME: (.+)$/mg);
    for my $line (@titles) {
        my ($name, $exe, $dir) = map { s/^\s+|\s+$//gr } split(/\|/, $line, 3);
        my $c = $cfg{$name};
        # Loud, not silent: a title whose name does not cross would simply come
        # out with no backend and no toolkit, which reads exactly like "this one
        # does not care" -- the one answer that must never be guessed.
        unless ($c) {
            warn "  warning: no measured setup for \"$name\"; backend and gptk left empty\n";
            $c = { backend => "", gptk => "", env => "{}" };
        }
        push @games, sprintf(
          qq({"name":"%s","exe":"%s","script":"%s","scope":"%s","carrier":"%s","keptAs":"%s","carrierDir":"%s","writesRegistry":%s,"backend":"%s","gptk":"%s","codec":"%s","env":%s,"why":"%s","files":[%s]}),
          $esc->($name), $esc->($exe), $script, $scope, $esc->($carrier), $esc->($kept),
          $esc->($dir // ""), $reg, $esc->($c->{backend}), $esc->($c->{gptk}), $esc->($c->{codec}), $c->{env},
          $esc->($why), join(",", map { qq("$_") } @files));
    }
}
# Titles whose whole fix is the staged codec. No installer names them, because
# nothing is installed beside the game -- so without this they were absent from
# the manifest entirely and a launcher had no way to know they exist.
for my $title (sort keys %cfg) {
    next if $seen_title{$title};
    my $c = $cfg{$title};
    next unless $c->{codec};
    my $exe = $only_exe{$title} // "";
    unless ($exe) {
        # Said out loud. An entry with no executable cannot be matched to a
        # folder, and a silent absence is indistinguishable from a title that
        # needs nothing.
        warn "  note: \"$title\" needs $c->{codec} and no installer; its executable has not been read off a real install, so it is not in the manifest\n";
        next;
    }
    my $esc = sub { my ($t) = @_; $t //= ""; $t =~ s/(["\\])/\\$1/g; $t };
    push @games, sprintf(
      qq({"name":"%s","exe":"%s","script":"","scope":"folder","carrier":"","keptAs":"","carrierDir":"","writesRegistry":false,"backend":"%s","gptk":"%s","codec":"%s","env":%s,"why":"%s","files":[]}),
      $esc->($title), $esc->($exe), $esc->($c->{backend}), $esc->($c->{gptk}),
      $esc->($c->{codec}), $c->{env},
      $esc->("The whole fix is the staged codec: nothing is installed beside the game."));
}

# Stays 3, and "scope" was added under it deliberately.
#
# This was briefly bumped to 4 on the reasoning that adding a field quietly is
# how a version number stops being an answer. That reasoning is right in general
# and wrong here, because of what the consumer does with the number: RaccoonBot
# keeps supportedSchemas = [2, 3] and REFUSES a manifest outside it -- there is a
# test asserting that schema 4 throws. So the bump would not have made one field
# unreadable, it would have made the whole catalogue unreadable, for all of
# them, over a field that is optional and whose absence means exactly what it
# always meant.
#
# Bumping this is a two-sided change: the consumer has to accept the new number
# before the producer emits it. Until that lands, adding a field whose absence
# is a valid reading is a compatible change and belongs under the same schema.
# What is installed into the engine rather than a game or a bottle.
#
# Read out of the installer the same way everything else here is, so it cannot
# drift from what the script actually does. It is a separate list because it is
# a different kind of thing: the engine is shared by every bottle and every game
# on it, which is a wider blast radius than anything in "games", and a launcher
# should be able to see that difference without inferring it.
my $engine_block = "";
if (-f "$repo/runtime/install-engine-media.sh" && -f "$repo/runtime/engine-built-for.json") {
    open my $bf, '<', "$repo/runtime/engine-built-for.json" or die;
    my $j = do { local $/; <$bf> };
    my ($ev) = $j =~ /"engine_version":\s*"([^"]*)"/;
    my ($ea) = $j =~ /"engine_app":\s*"([^"]*)"/;
    my ($wb) = $j =~ /"wine_build":\s*"([^"]*)"/;
    $engine_block = sprintf(
      qq(\n  "engine": {"script":"install-engine-media.sh","scope":"engine",)
      . qq("files":["engine-winegstreamer.dll","engine-winegstreamer.so","engine-built-for.json"],)
      . qq("install":[{"file":"engine-winegstreamer.dll","dest":"lib/wine/x86_64-windows/winegstreamer.dll"},)
      . qq({"file":"engine-winegstreamer.so","dest":"lib/wine/x86_64-unix/winegstreamer.so"}],)
      . qq("builtFor":{"app":"%s","version":"%s","wine":"%s"},)
      . qq("why":"Wine's GStreamer bridge, both halves, built here because codecs that were present would not show. The two halves must match each other and the engine; the installer refuses any other. Not per-title: it is shared by every bottle on that engine."},),
      $ea // "", $ev // "", $wb // "");
}
printf qq({\n  "schema": 3,%s\n), $engine_block;
printf qq(  "version": "%s",\n  "commit": "%s",\n  "treeDirty": %s,\n  "fromReleasedTag": %s,\n  "note": "Generated by runtime/make-fixes-bundle.sh from the installers themselves. Do not edit.",\n  "scopeWarning": "gptk is a requirement of the title, not an isolation guarantee. The toolkit is installed into the shared CrossOver application, so whichever game was launched last leaves its generation in place for every other one. A launcher can honour this field and should say so plainly; nothing here makes it per-game.",\n  "games": [\n    %s\n  ]\n}\n),
  $version, $commit, $dirty, $released, join(",\n    ", @games);
PERL

echo "[4/4] packing"
mkdir -p "$OUT"
tarball="$OUT/fixes-$version.tar.gz"
# Reproducible enough to compare two builds of the same commit: sorted, no
# directory entry, no macOS extended attributes.
( cd "$stage" && COPYFILE_DISABLE=1 tar --no-xattrs -czf "$tarball" $(ls | sort) )
shasum -a 256 "$tarball" | sed "s|$OUT/||" > "$tarball.sha256"

echo
echo "      $tarball"
echo "      $(du -h "$tarball" | cut -f1), $(ls "$stage" | wc -l | tr -d ' ') files"
echo "      $(cat "$tarball.sha256")"
[ "$dirty" = true ] && echo "      WARNING: built from a dirty tree -- not publishable as-is" >&2
[ "$released" = false ] && echo "      WARNING: HEAD is not a tag -- do not attach this to a release" >&2
exit 0
