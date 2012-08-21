#!/usr/bin/perl
#
# Galois field table generator.
#
# Copyright (c) 2006-2011 Miru Limited.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

use strict;

my $GF_ELEMENT_BYTES = 1;
my $GF_ELEMENT_BITS = 8 * $GF_ELEMENT_BYTES;
my $GF_NO_ELEMENTS = 1 << $GF_ELEMENT_BITS;
my $GF_MAX = $GF_NO_ELEMENTS - 1;

my $GF_GENERATOR = 0x11d;

my @gflog;
my @gfantilog;

my $j = 1;

for (my $i = 0; $i < $GF_MAX; $i++)
{
	$gflog[ $j ] = $i;
	$gfantilog[ $i ] = $j;

	$j <<= 1;
	if ($j & $GF_NO_ELEMENTS) {
		$j ^= $GF_GENERATOR;
	}
}

$gflog[ 0 ] = $GF_MAX;
$gfantilog[ $GF_MAX ] = 0;

print<<MOO;
/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Galois field tables
 *
 * Copyright (c) 2006-2011 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/framework.h>


/* globals */

const pgm_gf8_t pgm_gflog[PGM_GF_NO_ELEMENTS] =
{
MOO

# print out y = log₂(x) table
for (my $i = 0; $i < $GF_NO_ELEMENTS; $i++)
{
	print "\t" if ($i % 8 == 0);
	print sprintf("0x%2.2x", $gflog[ $i ]);
	print ',' unless ($i == $GF_MAX);
	print ( (($i % 8) == 7) ? "\n" : ' ' );
}

print<<MOO;
};

const pgm_gf8_t pgm_gfantilog[PGM_GF_NO_ELEMENTS] =
{
MOO

# print out y = antilog₂(x) table, aka pow2(x), 2^^x
for (my $i = 0; $i < $GF_NO_ELEMENTS; $i++)
{
	print "\t" if ($i % 8 == 0);
	print sprintf("0x%2.2x", $gfantilog[ $i ]);
	print ',' unless ($i == $GF_MAX);
	print ( (($i % 8) == 7) ? "\n" : ' ' );
}

print<<MOO;
};

#ifdef USE_GALOIS_MUL_LUT
const pgm_gf8_t pgm_gftable[PGM_GF_NO_ELEMENTS * PGM_GF_NO_ELEMENTS] =
{
MOO

sub gfmul {
	my($a, $b) = @_;
	return 0 if ($a == 0 || $b == 0);
	my $sum = $gflog[ $a ] + $gflog[ $b ];
	return ($sum >= $GF_MAX) ? $gfantilog[ $sum - $GF_MAX ] : $gfantilog[ $sum ];
}

# print out multiplication table z = x • y
for (my $i = 0; $i < $GF_NO_ELEMENTS; $i++)
{
	for (my $j = 0; $j < $GF_NO_ELEMENTS; $j++)
	{
		print "\t" if ($j % 8 == 0);
		print sprintf("0x%2.2x", gfmul( $i, $j ));
		print ',' unless ($i == $GF_MAX && $j == $GF_MAX);
		print ( (($j % 8) == 7) ? "\n" : ' ' );
	}
}

print<<MOO;
};
#endif

/* eof */
MOO

# eof
