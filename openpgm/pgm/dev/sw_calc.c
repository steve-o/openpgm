/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Sliding window calculus for dummies.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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


#include <glib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>


/* globals */

/* is (a) greater than (b) wrt. leading edge of receive window (w) */
#define SLIDINGWINDOW_GT(a,b,l) \
	( \
		( (gint32)(a) - (gint32)(l) ) > ( (gint32)(b) - (gint32)(l) ) \
	)

#define IN_WINDOW(a,l) \
	( \
		(gint32)(a) - (gint32)(l) > ((UINT32_MAX/2) -1 ) \
	)

int
main (
	G_GNUC_UNUSED int	argc,
	G_GNUC_UNUSED char   *argv[]
	)
{
	puts ("sw_calc");

#define um UINT32_MAX
#define undef	2
	guint32 tests[][4] =
	{
		/*	a	b	l	GT	*/
		{	0,	0,	0,	undef	},
		{	10,	20,	30,	FALSE	},
		{	10,	30,	20,	undef	},
		{	30,	10,	20,	undef	},
		{	20,	10,	30,	TRUE	},
		{	20,	30,	10,	undef	},
		{	30,	20,	10,	undef	},

		{	10,	um-10,	30,	TRUE	},
		{	um-10,	um-20,	30,	TRUE	},
		{	um-20,	um-1,	30,	FALSE	},

		{	10,	30,	30,	FALSE	},
	};

	for (unsigned i = 0; i < G_N_ELEMENTS(tests); i++)
	{
		guint32 a = tests[i][0], b = tests[i][1], l = tests[i][2], expected = tests[i][3];

		gint32 ad = (gint32)(a) - (gint32)(l);
		gint32 bd = (gint32)(b) - (gint32)(l);

		gboolean ai = IN_WINDOW(a, l);
		gboolean bi = IN_WINDOW(b, l);

		gint32 res = SLIDINGWINDOW_GT(a, b, l);
		if (!ai || !bi) res = 2;

		printf ("%10" G_GUINT32_FORMAT " [%c] > "
			"%10" G_GUINT32_FORMAT " [%c]"
			" (l:%3" G_GUINT32_FORMAT ") "
			"= %-5s (%" G_GINT32_FORMAT ">%" G_GINT32_FORMAT "=%" G_GINT32_FORMAT ") %s\n",
			a, ai ? 'O' : 'X', b, bi ? 'O' : 'X', l,
			res ? ( res == 2 ? "undef" : "TRUE" ) : "FALSE", 
			ad, bd, res,
			res != expected ? "***" : ""
			);
	}

	puts ("finished.");
	return 0;
}

/* eof */
