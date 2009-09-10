#!/usr/bin/perl

use strict;
use File::Basename;

die "usage: $0 [text file]\n" unless ($ARGV[0]);
open(MOO, $ARGV[0]) or die "cannot open $ARGV[0]: $!";
my $all = do { local $/; <MOO> };
close(MOO);
$all =~ s/"/\\"/g;
$all =~ s/\n/\\n/mg;
$all =~ s/\r/\\r/mg;

my $var = uc (basename($ARGV[0]));
$var =~ s/\s+/_/g;
$var =~ s/\./_/g;

print<<MOO;
#define WWW_$var	"$all"
MOO
