/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Rate regulation.
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

#include <glib.h>

#include <errno.h>
#include <sys/types.h>

#ifdef G_OS_UNIX
#	include <sys/socket.h>
#endif

#include "pgm/time.h"


struct rate_t {
	gint		rate_per_sec;
	guint		iphdr_len;

	gint		rate_limit;		/* signed for math */
	pgm_time_t	last_rate_check;
	GStaticMutex	mutex;
};

typedef struct rate_t rate_t;

/* globals */

G_GNUC_INTERNAL void pgm_rate_create (rate_t**, const guint, const guint);
G_GNUC_INTERNAL void pgm_rate_destroy (rate_t*);
G_GNUC_INTERNAL gboolean pgm_rate_check (rate_t*, const guint, const int);


/* create machinery for rate regulation.
 * the rate_per_sec is ammortized over millisecond time periods.
 */

void
pgm_rate_create (
	rate_t**		bucket_,
	const guint		rate_per_sec,		/* 0 = disable */
	const guint		iphdr_len
	)
{
/* pre-conditions */
	g_assert (NULL != bucket_);

	rate_t* bucket = g_malloc0 (sizeof(rate_t));
	bucket->rate_per_sec	= (gint)rate_per_sec;
	bucket->iphdr_len	= iphdr_len;
	bucket->last_rate_check	= pgm_time_update_now ();
	g_static_mutex_init (&bucket->mutex);
	*bucket_ = bucket;
}

void
pgm_rate_destroy (
	rate_t*			bucket
	)
{
/* pre-conditions */
	g_assert (NULL != bucket);

	g_static_mutex_free (&bucket->mutex);
	g_free (bucket);
}

/* check bit bucket whether an operation can proceed or should wait.
 *
 * returns TRUE when leaky bucket permits unless non-blocking flag is set.
 * returns FALSE if operation should block and non-blocking flag is set.
 */

gboolean
pgm_rate_check (
	rate_t*			bucket,
	const guint		data_size,
	const gboolean		is_nonblocking
	)
{
/* pre-conditions */
	g_assert (NULL != bucket);
	g_assert (data_size > 0);

	if (0 == bucket->rate_per_sec)
		return TRUE;

	g_static_mutex_lock (&bucket->mutex);
	pgm_time_t now = pgm_time_update_now();
	pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;

	bucket->rate_limit += (double)bucket->rate_per_sec * (double)pgm_to_secs((double)time_since_last_rate_check);
/* per milli-second */
	if (bucket->rate_limit > (bucket->rate_per_sec / 1000)) 
		bucket->rate_limit = bucket->rate_per_sec / 1000;
	bucket->last_rate_check = now;

	const gint new_rate_limit = bucket->rate_limit - ( bucket->iphdr_len + data_size );
	if (is_nonblocking && new_rate_limit < 0) {
		g_static_mutex_unlock (&bucket->mutex);
		return FALSE;
	}

	bucket->rate_limit = new_rate_limit;
	if (bucket->rate_limit < 0) {
		gint sleep_amount;
		do {
			g_thread_yield();
			now = pgm_time_update_now();
			time_since_last_rate_check = now - bucket->last_rate_check;
			sleep_amount = (double)bucket->rate_per_sec * (double)pgm_to_secs((double)time_since_last_rate_check);
		} while (sleep_amount + bucket->rate_limit < 0);
		bucket->rate_limit += sleep_amount;
		bucket->last_rate_check = now;
	} 
	g_static_mutex_unlock (&bucket->mutex);
	return TRUE;
}

pgm_time_t
pgm_rate_remaining (
	rate_t*			bucket
	)
{
	pgm_time_t sleep_amount;

/* pre-conditions */
	g_assert (NULL != bucket);
	g_static_mutex_lock (&bucket->mutex);
	const pgm_time_t now = pgm_time_update_now();
	const pgm_time_t time_since_last_rate_check = now - bucket->last_rate_check;
	sleep_amount = (double)bucket->rate_per_sec * (double)pgm_to_secs((double)time_since_last_rate_check);
	g_static_mutex_unlock (&bucket->mutex);
	return sleep_amount;
}

/* eof */
