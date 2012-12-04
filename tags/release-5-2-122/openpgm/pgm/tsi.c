/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * transport session ID helper functions.
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
#include <stdio.h>
#include <impl/framework.h>


//#define TSI_DEBUG


/* locals */


/* re-entrant form of pgm_tsi_print()
 *
 * returns number of bytes written to buffer on success, returns -1 on
 * invalid parameters.
 */

int
pgm_tsi_print_r (
	const pgm_tsi_t* restrict tsi,
	char*		 restrict buf,
	size_t			  bufsize
	)
{
	pgm_return_val_if_fail (NULL != tsi, -1);
	pgm_return_val_if_fail (NULL != buf, -1);
	pgm_return_val_if_fail (bufsize > 0, -1);

	const uint8_t* gsi = (const uint8_t*)tsi;
	const uint16_t source_port = tsi->sport;

	return pgm_snprintf_s (buf, bufsize, _TRUNCATE, "%u.%u.%u.%u.%u.%u.%u",
				 gsi[0], gsi[1], gsi[2], gsi[3], gsi[4], gsi[5], ntohs (source_port));
}

/* transform TSI to ASCII string form.
 *
 * on success, returns pointer to ASCII string.  on error, returns NULL.
 */

char*
pgm_tsi_print (
	const pgm_tsi_t*	tsi
	)
{
	static char buf[PGM_TSISTRLEN];

	pgm_return_val_if_fail (tsi != NULL, NULL);

	pgm_tsi_print_r (tsi, buf, sizeof(buf));
	return buf;
}

/* create hash value of TSI for use with GLib hash tables.
 *
 * on success, returns a hash value corresponding to the TSI.  on error, fails
 * on assert.
 */

PGM_GNUC_INTERNAL
pgm_hash_t
pgm_tsi_hash (
	const void*	 p
        )
{
	const union {
		pgm_tsi_t	tsi;
		uint32_t	l[2];
	} *u = p;

/* pre-conditions */
	pgm_assert (NULL != p);

	return u->l[0] ^ u->l[1];
}

/* compare two transport session identifier TSI values.
 *
 * returns TRUE if they are equal, FALSE if they are not.
 */

bool
pgm_tsi_equal (
	const void* restrict p1,
	const void* restrict p2
        )
{
	const union {
		pgm_tsi_t	tsi;
		uint32_t	l[2];
		uint64_t	ll;
	} *restrict u1 = p1, *restrict u2 = p2;

/* pre-conditions */
	pgm_assert (NULL != p1);
	pgm_assert (NULL != p2);

	return (u1->l[0] == u2->l[0] && u1->l[1] == u2->l[1]);
}

/* eof */
