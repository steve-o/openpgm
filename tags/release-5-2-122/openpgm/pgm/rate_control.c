/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Rate regulation.
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


/* create machinery for rate regulation.
 * the rate_per_sec is ammortized over millisecond time periods.
 *
 * NB: bucket MUST be memset 0 before calling.
 */

PGM_GNUC_INTERNAL
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

PGM_GNUC_INTERNAL
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

PGM_GNUC_INTERNAL
bool
pgm_rate_check2 (
	pgm_rate_t*		major_bucket,
	pgm_rate_t*		minor_bucket,
	const size_t		data_size,
	const bool		is_nonblocking
	)
{
	int64_t new_major_limit, new_minor_limit;
	pgm_time_t now;

/* pre-conditions */
	pgm_assert (NULL != major_bucket);
	pgm_assert (NULL != minor_bucket);
	pgm_assert (data_size > 0);

	if (0 == major_bucket->rate_per_sec && 0 == minor_bucket->rate_per_sec)
		return TRUE;

	if (0 != major_bucket->rate_per_sec)
	{
		pgm_spinlock_lock (&major_bucket->spinlock);
		now = pgm_time_update_now();

		if (major_bucket->rate_per_msec)
		{
			const pgm_time_t time_since_last_rate_check = now - major_bucket->last_rate_check;
			if (time_since_last_rate_check > pgm_msecs(1)) 
				new_major_limit = major_bucket->rate_per_msec;
			else {
				new_major_limit = major_bucket->rate_limit + ((major_bucket->rate_per_msec * time_since_last_rate_check) / 1000UL);
				if (new_major_limit > major_bucket->rate_per_msec)
					new_major_limit = major_bucket->rate_per_msec;
			}
		}
		else
		{
			const pgm_time_t time_since_last_rate_check = now - major_bucket->last_rate_check;
			if (time_since_last_rate_check > pgm_secs(1)) 
				new_major_limit = major_bucket->rate_per_sec;
			else {
				new_major_limit = major_bucket->rate_limit + ((major_bucket->rate_per_sec * time_since_last_rate_check) / 1000000UL);
				if (new_major_limit > major_bucket->rate_per_sec)
					new_major_limit = major_bucket->rate_per_sec;
			}
		}

		new_major_limit -= ( major_bucket->iphdr_len + data_size );
		if (is_nonblocking && new_major_limit < 0) {
			pgm_spinlock_unlock (&major_bucket->spinlock);
			return FALSE;
		}

		if (new_major_limit < 0) {
			const pgm_time_t wait_start = now;
			ssize_t sleep_amount;
			do {
				pgm_thread_yield();
				now = pgm_time_update_now();
				sleep_amount = (ssize_t)pgm_to_secs (major_bucket->rate_per_sec * (now - wait_start));
			} while (sleep_amount + new_major_limit < 0);
			new_major_limit += sleep_amount;
		} 
	}
	else
	{
/* ensure we have a timestamp */
		now = pgm_time_update_now();
	}

	if (0 != minor_bucket->rate_per_sec)
	{
		if (minor_bucket->rate_per_msec)
		{
			const pgm_time_t time_since_last_rate_check = now - minor_bucket->last_rate_check;
			if (time_since_last_rate_check > pgm_msecs(1)) 
				new_minor_limit = minor_bucket->rate_per_msec;
			else {
				new_minor_limit = minor_bucket->rate_limit + ((minor_bucket->rate_per_msec * time_since_last_rate_check) / 1000UL);
				if (new_minor_limit > minor_bucket->rate_per_msec)
					new_minor_limit = minor_bucket->rate_per_msec;
			}
		}
		else
		{
			const pgm_time_t time_since_last_rate_check = now - minor_bucket->last_rate_check;
			if (time_since_last_rate_check > pgm_secs(1)) 
				new_minor_limit = minor_bucket->rate_per_sec;
			else {
				new_minor_limit = minor_bucket->rate_limit + ((minor_bucket->rate_per_sec * time_since_last_rate_check) / 1000000UL);
				if (new_minor_limit > minor_bucket->rate_per_sec)
					new_minor_limit = minor_bucket->rate_per_sec;
			}
		}

		new_minor_limit -= ( minor_bucket->iphdr_len + data_size );
		if (is_nonblocking && new_minor_limit < 0) {
			if (0 != major_bucket->rate_per_sec)
				pgm_spinlock_unlock (&major_bucket->spinlock);
			return FALSE;
		}

/* commit new rate limit */
		minor_bucket->rate_limit = new_minor_limit;
		minor_bucket->last_rate_check = now;
	}

	if (0 != major_bucket->rate_per_sec) {
		major_bucket->rate_limit = new_major_limit;
		major_bucket->last_rate_check = now;
		pgm_spinlock_unlock (&major_bucket->spinlock);
	}

/* sleep on minor bucket outside of lock */
	if (minor_bucket->rate_limit < 0) {
		ssize_t sleep_amount;
		do {
			pgm_thread_yield();
			now = pgm_time_update_now();
			sleep_amount = (ssize_t)pgm_to_secs (minor_bucket->rate_per_sec * (now - minor_bucket->last_rate_check));
		} while (sleep_amount + minor_bucket->rate_limit < 0);
		minor_bucket->rate_limit += sleep_amount;
		minor_bucket->last_rate_check = now;
	} 

	return TRUE;
}

PGM_GNUC_INTERNAL
bool
pgm_rate_check (
	pgm_rate_t*		bucket,
	const size_t		data_size,
	const bool		is_nonblocking
	)
{
	int64_t new_rate_limit;

/* pre-conditions */
	pgm_assert (NULL != bucket);
	pgm_assert (data_size > 0);

	if (0 == bucket->rate_per_sec)
		return TRUE;

	pgm_spinlock_lock (&bucket->spinlock);
	pgm_time_t now = pgm_time_update_now();

	if (bucket->rate_per_msec)
	{
		const pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;
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
		const pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;
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
		ssize_t sleep_amount;
		do {
			pgm_thread_yield();
			now = pgm_time_update_now();
			sleep_amount = (ssize_t)pgm_to_secs (bucket->rate_per_sec * (now - bucket->last_rate_check));
		} while (sleep_amount + bucket->rate_limit < 0);
		bucket->rate_limit += sleep_amount;
		bucket->last_rate_check = now;
	} 
	pgm_spinlock_unlock (&bucket->spinlock);
	return TRUE;
}

PGM_GNUC_INTERNAL
pgm_time_t
pgm_rate_remaining2 (
	pgm_rate_t*		major_bucket,
	pgm_rate_t*		minor_bucket,
	const size_t		n
	)
{
	pgm_time_t remaining = 0;
	pgm_time_t now;

/* pre-conditions */
	pgm_assert (NULL != major_bucket);
	pgm_assert (NULL != minor_bucket);

	if (PGM_UNLIKELY(0 == major_bucket->rate_per_sec && 0 == minor_bucket->rate_per_sec))
		return remaining;

	if (0 != major_bucket->rate_per_sec)
	{
		pgm_spinlock_lock (&major_bucket->spinlock);
		now = pgm_time_update_now();
		const int64_t bucket_bytes = major_bucket->rate_limit + pgm_to_secs (major_bucket->rate_per_sec * (now - major_bucket->last_rate_check)) - n;

		if (bucket_bytes < 0) {
			const int64_t outstanding_bytes = -bucket_bytes;
			const pgm_time_t major_remaining = (1000000UL * outstanding_bytes) / major_bucket->rate_per_sec;
			remaining = major_remaining;
		}
	}
	else
	{
/* ensure we have a timestamp */
		now = pgm_time_update_now();
	}

	if (0 != minor_bucket->rate_per_sec)
	{
		const int64_t bucket_bytes = minor_bucket->rate_limit + pgm_to_secs (minor_bucket->rate_per_sec * (now - minor_bucket->last_rate_check)) - n;

		if (bucket_bytes < 0) {
			const int64_t outstanding_bytes = -bucket_bytes;
			const pgm_time_t minor_remaining = (1000000UL * outstanding_bytes) / minor_bucket->rate_per_sec;
			remaining = remaining > 0 ? MIN(remaining, minor_remaining) : minor_remaining;
		}
	}

	if (0 != major_bucket->rate_per_sec)
	{
		pgm_spinlock_unlock (&major_bucket->spinlock);
	}

	return remaining;
}

PGM_GNUC_INTERNAL
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
	const int64_t bucket_bytes = bucket->rate_limit + pgm_to_secs (bucket->rate_per_sec * time_since_last_rate_check) - n;
	pgm_spinlock_unlock (&bucket->spinlock);

	if (bucket_bytes >= 0)
		return 0;

	const int64_t outstanding_bytes = -bucket_bytes;
	const pgm_time_t remaining = (1000000UL * outstanding_bytes) / bucket->rate_per_sec;

	return remaining;
}

/* eof */
