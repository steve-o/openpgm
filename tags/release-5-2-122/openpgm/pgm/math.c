/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable math.
 *
 * Copyright (c) 2010-2011 Miru Limited.
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


//#define MATH_DEBUG


static const unsigned primes[] =
{
	11,
	19,
	37,
	73,
	109,
	163,
	251,
	367,
	557,
	823,
	1237,
	1861,
	2777,
	4177,
	6247,
	9371,
	14057,
	21089,
	31627,
	47431,
	71143,
	106721,
	160073,
	240101,
	360163,
	540217,
	810343,
	1215497,
	1823231,
	2734867,
	4102283,
	6153409,
	9230113,
	13845163,
};

unsigned
pgm_spaced_primes_closest (unsigned num)
{
	for (unsigned i = 0; i < PGM_N_ELEMENTS(primes); i++)
		if (primes[i] > num)
			return primes[i];
	return primes[PGM_N_ELEMENTS(primes) - 1];
}

/* eof */
