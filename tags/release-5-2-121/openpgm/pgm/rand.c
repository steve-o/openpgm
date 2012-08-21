/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Portable weak pseudo-random generator.  Performance is explicitly
 * chosen in preference to randomness.
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
#ifndef _WIN32
#	include <errno.h>
#	include <stdio.h>
#endif
#include <impl/framework.h>


//#define RAND_DEBUG


/* locals */

static pgm_rand_t		global_rand = { .seed = 0 };
static volatile uint32_t	rand_ref_count = 0;
static pgm_mutex_t		rand_mutex;

PGM_GNUC_INTERNAL
void
pgm_rand_init (void)
{
	if (pgm_atomic_exchange_and_add32 (&rand_ref_count, 1) > 0)
		return;

	pgm_mutex_init (&rand_mutex);
}

PGM_GNUC_INTERNAL
void
pgm_rand_shutdown (void)
{
	pgm_return_if_fail (pgm_atomic_read32 (&rand_ref_count) > 0);

	if (pgm_atomic_exchange_and_add32 (&rand_ref_count, (uint32_t)-1) != 1)
		return;

	pgm_mutex_free (&rand_mutex);
}

PGM_GNUC_INTERNAL
void
pgm_rand_create (
	pgm_rand_t*	new_rand
	)
{
/* pre-conditions */
	pgm_assert (NULL != new_rand);

#ifndef _WIN32
/* attempt to read seed from kernel
 */
	FILE* fp;
	do {
		fp = fopen ("/dev/urandom", "rb");
	} while (PGM_UNLIKELY(NULL == fp && EINTR == errno));
	if (fp) {
		size_t items_read;
		do {
			items_read = fread (&new_rand->seed, sizeof(new_rand->seed), 1, fp);
		} while (PGM_UNLIKELY(EINTR == errno));
		fclose (fp);
		if (1 == items_read)
			return;
	}
#endif /* !_WIN32 */
	const pgm_time_t now = pgm_time_update_now();
	new_rand->seed = (uint32_t)pgm_to_msecs (now);
}

/* derived from POSIX.1-2001 example implementation of rand()
 */

PGM_GNUC_INTERNAL
uint32_t
pgm_rand_int (
	pgm_rand_t*	r
	)
{
/* pre-conditions */
	pgm_assert (NULL != r);

	r->seed = r->seed * 1103515245 + 12345;
	return r->seed;
}

PGM_GNUC_INTERNAL
int32_t
pgm_rand_int_range (
	pgm_rand_t*	r,
	int32_t		begin,
	int32_t		end
	)
{
/* pre-conditions */
	pgm_assert (NULL != r);

	return begin + pgm_rand_int (r) % (end - begin);
}

PGM_GNUC_INTERNAL
uint32_t
pgm_random_int (void)
{
	pgm_mutex_lock (&rand_mutex);
	if (PGM_UNLIKELY(!global_rand.seed))
		pgm_rand_create (&global_rand);
	const uint32_t rand_value = pgm_rand_int (&global_rand);
	pgm_mutex_unlock (&rand_mutex);
	return rand_value;
}

PGM_GNUC_INTERNAL
int32_t
pgm_random_int_range (
	int32_t		begin,
	int32_t		end
	)
{
	const uint32_t rand_value = pgm_random_int();
	return begin + rand_value % (end - begin);
}

/* eof */
