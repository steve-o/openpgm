#!/usr/bin/perl
# -*- perl -*-
# Extract version number from version_generator.py and dump to stdout.
#
# For Autoconf/libtool & CMake

open (MOO, 'version_generator.py') || die "version.py: error: version_generator.py not found.\n";
while (<MOO>) {
	if (/pgm_(major|minor|micro)_version = (\d+);/) {
		$version{$1} = $2;
	}
}
$_ = "%major.%minor.%micro";
$_ = "${ARGV[0]}" if ($ARGV[0]);
s/%major/${version{'major'}}/g;
s/%minor/${version{'minor'}}/g;
s/%micro/${version{'micro'}}/g;
print;
