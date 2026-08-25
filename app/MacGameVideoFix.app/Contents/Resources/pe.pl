#!/usr/bin/perl
# pe.pl -- export names of a PE binary, for the one question the installers ask.
#
# Perl rather than Python because macOS ships a real /usr/bin/perl and does not
# ship Python: /usr/bin/python3 is a hard link to the xcrun dispatcher (78 of
# them, one inode), so on a Mac without developer tools it opens a dialog and
# fails. Not a compiled helper either: a binary downloaded from a release
# arrives quarantined and Gatekeeper stops it, while a text script does not.
#
# SPDX-License-Identifier: GPL-3.0-or-later
use strict; use warnings;

my ($verb, $path, @rest) = @ARGV;
die "usage: pe.pl exports <file> [--ordinals]\n"
    unless defined $verb && defined $path && $verb eq 'exports';
# --ordinals prints "<ordinal> <name>", which is what the proxy generators read
# with `while read -r ordinal sym`. Emitting only the name there would put the
# symbol in $ordinal, leave $sym empty, and produce a .def whose every entry
# reads "= X_real. @" -- an export table that builds and forwards nothing.
my $with_ordinals = grep { $_ eq '--ordinals' } @rest;
open my $fh, '<:raw', $path or die "cannot open $path: $!\n";
my $buf = do { local $/; <$fh> };

# DOS header -> PE signature
my $pe = unpack('V', substr($buf, 0x3c, 4));
die "not a PE\n" unless substr($buf, $pe, 4) eq "PE\0\0";

my $coff      = $pe + 4;
my $nsections = unpack('v', substr($buf, $coff + 2, 2));
my $optsize   = unpack('v', substr($buf, $coff + 16, 2));
my $opt       = $coff + 20;
my $magic     = unpack('v', substr($buf, $opt, 2));
my $is64      = ($magic == 0x20b);

# DataDirectory[0] is the export table. It sits after the fixed part of the
# optional header, whose size differs between PE32 and PE32+.
my $dirs = $opt + ($is64 ? 112 : 96);
my ($exp_rva) = unpack('V', substr($buf, $dirs, 4));
exit 0 unless $exp_rva;

# Sections, to turn an RVA into a file offset.
my @sec;
my $s = $opt + $optsize;
for my $i (0 .. $nsections - 1) {
    my $o = $s + $i * 40;
    push @sec, {
        va   => unpack('V', substr($buf, $o + 12, 4)),
        vsz  => unpack('V', substr($buf, $o +  8, 4)),
        raw  => unpack('V', substr($buf, $o + 20, 4)),
        rsz  => unpack('V', substr($buf, $o + 16, 4)),
    };
}
sub rva2off {
    my ($rva) = @_;
    for my $x (@sec) {
        my $size = $x->{vsz} > $x->{rsz} ? $x->{vsz} : $x->{rsz};
        return $x->{raw} + $rva - $x->{va} if $rva >= $x->{va} && $rva < $x->{va} + $size;
    }
    return undef;
}

my $e = rva2off($exp_rva) // exit 0;
my $base     = unpack('V', substr($buf, $e + 16, 4));   # ordinal base
my $nnames   = unpack('V', substr($buf, $e + 24, 4));
my $names_rva= unpack('V', substr($buf, $e + 32, 4));
my $ords_rva = unpack('V', substr($buf, $e + 36, 4));   # AddressOfNameOrdinals
my $names    = rva2off($names_rva) // exit 0;
my $ords     = rva2off($ords_rva);
for my $i (0 .. $nnames - 1) {
    my $off = rva2off(unpack('V', substr($buf, $names + $i * 4, 4))) // next;
    my $end = index($buf, "\0", $off);
    my $name = substr($buf, $off, $end - $off);
    if ($with_ordinals && defined $ords) {
        # The ordinal is an index into the export address table, biased by Base.
        my $ord = unpack('v', substr($buf, $ords + $i * 2, 2)) + $base;
        print "$ord $name\n";
    } else {
        print "$name\n";
    }
}
