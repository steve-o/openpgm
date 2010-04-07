/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * transport session ID helper functions.
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "pgm/tsi.h"

//#define TSI_DEBUG

#ifndef TSI_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* locals */


/* re-entrant form of pgm_tsi_print()
 *
 * returns number of bytes written to buffer on success, returns -1 on
 * invalid parameters.
 */
int
pgm_tsi_print_r (
	const pgm_tsi_t*	tsi,
	char*			buf,
	gsize			bufsize
	)
{
	g_return_val_if_fail (NULL != tsi, -1);
	g_return_val_if_fail (NULL != buf, -1);
	g_return_val_if_fail (bufsize > 0, -1);

	const guint8* gsi = (const guint8*)tsi;
	const guint16 source_port = tsi->sport;

	return snprintf (buf, bufsize, "%i.%i.%i.%i.%i.%i.%i",
			 gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], g_ntohs (source_port));
}

/* transform TSI to ASCII string form.
 *
 * on success, returns pointer to ASCII string.  on error, returns NULL.
 */
gchar*
pgm_tsi_print (
	const pgm_tsi_t*	tsi
	)
{
	g_return_val_if_fail (tsi != NULL, NULL);

	static char buf[PGM_TSISTRLEN];
	pgm_tsi_print_r (tsi, buf, sizeof(buf));
	return buf;
}

/* create hash value of TSI for use with GLib hash tables.
 *
 * on success, returns a hash value corresponding to the TSI.  on error, fails
 * on assert.
 */
guint
pgm_tsi_hash (
	gconstpointer v
        )
{
/* pre-conditions */
	g_assert (NULL != v);

	const pgm_tsi_t* tsi = v;
	char buf[PGM_TSISTRLEN];
	const int valid = pgm_tsi_print_r (tsi, buf, sizeof(buf));
	g_assert (valid > 0);
	return g_str_hash (buf);
}

/* compare two transport session identifier TSI values.
 *
 * returns TRUE if they are equal, FALSE if they are not.
 */
gboolean
pgm_tsi_equal (
	gconstpointer   v,
	gconstpointer   v2
        )
{
/* pre-conditions */
	g_assert (v);
	g_assert (v2);

	return memcmp (v, v2, sizeof(struct pgm_tsi_t)) == 0;
}

/* eof */
