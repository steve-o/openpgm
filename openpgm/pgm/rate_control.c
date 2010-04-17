/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Rate regulation.
 *
 * Copyright (c) 2006-2010 Miru Limited.
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

#include <pgm/framework.h>


/* create machinery for rate regulation.
 * the rate_per_sec is ammortized over millisecond time periods.
 */

void
pgm_rate_create (
	pgm_rate_t*		bucket,
	const ssize_t		rate_per_sec,		/* 0 = disable */
	const size_t		iphdr_len,
	const uint16_t		max_tpdu
	)
{
/* pre-conditions */
	pgm_assert (NULL != bucket);
	pgm_assert (rate_per_sec >= max_tpdu);

	bucket->rate_per_sec	= rate_per_sec;
	bucket->iphdr_len	= iphdr_len;
	bucket->last_rate_check	= pgm_time_update_now ();
/* pre-fill bucket */
	if ((rate_per_sec / 1000) >= max_tpdu) {
		bucket->rate_per_msec	= bucket->rate_per_sec / 1000;
		bucket->rate_limit	= bucket->rate_per_msec;
	} else {
		bucket->rate_limit	= bucket->rate_per_sec;
	}
	pgm_spinlock_init (&bucket->spinlock);
}

void
pgm_rate_destroy (
	pgm_rate_t*		bucket
	)
{
/* pre-conditions */
	pgm_assert (NULL != bucket);

	pgm_spinlock_free (&bucket->spinlock);
}

/* check bit bucket whether an operation can proceed or should wait.
 *
 * returns TRUE when leaky bucket permits unless non-blocking flag is set.
 * returns FALSE if operation should block and non-blocking flag is set.
 */

bool
pgm_rate_check (
	pgm_rate_t*		bucket,
	const size_t		data_size,
	const bool		is_nonblocking
	)
{
	int new_rate_limit;

/* pre-conditions */
	pgm_assert (NULL != bucket);
	pgm_assert (data_size > 0);

	if (0 == bucket->rate_per_sec)
		return TRUE;

	pgm_spinlock_lock (&bucket->spinlock);
	pgm_time_t now = pgm_time_update_now();
	pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;

	if (bucket->rate_per_msec)
	{
		if (time_since_last_rate_check > pgm_msecs(1)) 
			new_rate_limit = bucket->rate_per_msec;
		else {
			new_rate_limit = bucket->rate_limit + ((bucket->rate_per_msec * time_since_last_rate_check) / 1000UL);
			if (new_rate_limit > bucket->rate_per_msec)
				new_rate_limit = bucket->rate_per_msec;
		}
	}
	else
	{
		if (time_since_last_rate_check > pgm_secs(1)) 
			new_rate_limit = bucket->rate_per_sec;
		else {
			new_rate_limit = bucket->rate_limit + ((bucket->rate_per_sec * time_since_last_rate_check) / 1000000UL);
			if (new_rate_limit > bucket->rate_per_sec)
				new_rate_limit = bucket->rate_per_sec;
		}
	}

	new_rate_limit -= ( bucket->iphdr_len + data_size );
	if (is_nonblocking && new_rate_limit < 0) {
		pgm_spinlock_unlock (&bucket->spinlock);
		return FALSE;
	}

	bucket->rate_limit = new_rate_limit;
	bucket->last_rate_check = now;
	if (bucket->rate_limit < 0) {
		int sleep_amount;
		do {
			pgm_thread_yield();
			now = pgm_time_update_now();
			time_since_last_rate_check = now - bucket->last_rate_check;
			sleep_amount = pgm_to_secs (bucket->rate_per_sec * time_since_last_rate_check);
		} while (sleep_amount + bucket->rate_limit < 0);
		bucket->rate_limit += sleep_amount;
		bucket->last_rate_check = now;
	} 
	pgm_spinlock_unlock (&bucket->spinlock);
	return TRUE;
}

pgm_time_t
pgm_rate_remaining (
	pgm_rate_t*		bucket,
	const size_t		n
	)
{
/* pre-conditions */
	pgm_assert (NULL != bucket);

	if (PGM_UNLIKELY(0 == bucket->rate_per_sec))
		return 0;

	pgm_spinlock_lock (&bucket->spinlock);
	const pgm_time_t now = pgm_time_update_now();
	const pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;
	const int bucket_bytes = bucket->rate_limit + pgm_to_secs (bucket->rate_per_sec * time_since_last_rate_check) - n;
	pgm_spinlock_unlock (&bucket->spinlock);

	return bucket_bytes >= 0 ? 0 : (bucket->rate_per_sec / -bucket_bytes);
}

/* eof */
