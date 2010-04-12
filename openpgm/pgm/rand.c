/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable weak pseudo-random generator.
 *
 * Copyright (c) 2010 Miru Limited.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include "pgm/messages.h"
#include "pgm/time.h"
#include "pgm/thread.h"
#include "pgm/rand.h"


//#define RAND_DEBUG


static pgm_rand_t g_rand = { .seed = 0 };
static pgm_mutex_t g_rand_mutex;

void
pgm_rand_init (void)
{
	pgm_mutex_init (&g_rand_mutex);
}

void
pgm_rand_shutdown (void)
{
	pgm_mutex_free (&g_rand_mutex);
}

void
pgm_rand_create (
	pgm_rand_t*	rand_
	)
{
/* pre-conditions */
	pgm_assert (NULL != rand_);

#ifdef G_OS_UNIX
/* attempt to read seed from kernel
 */
	FILE* fp;
	do {
		fp = fopen ("/dev/urandom", "rb");
	} while (G_UNLIKELY(EINTR == errno));
	if (fp) {
		size_t items_read;
		do {
			items_read = fread (&rand_->seed, sizeof(rand_->seed), 1, fp);
		} while (G_UNLIKELY(EINTR == errno));
		fclose (fp);
		if (1 == items_read)
			return;
	}
#endif /* !G_OS_UNIX */
	const pgm_time_t now = pgm_time_update_now();
	rand_->seed = (guint32)pgm_to_msecs (now);
}

/* derived from POSIX.1-2001 example implementation of rand()
 */

guint32
pgm_rand_int (
	pgm_rand_t*	rand_
	)
{
/* pre-conditions */
	pgm_assert (NULL != rand_);

	rand_->seed = rand_->seed * 1103515245 + 12345;
	return rand_->seed;
}

gint32
pgm_rand_int_range (
	pgm_rand_t*	rand_,
	gint32		begin,
	gint32		end
	)
{
/* pre-conditions */
	pgm_assert (NULL != rand_);

	return begin + pgm_rand_int (rand_) % (end - begin);
}

guint32
pgm_random_int (void)
{
	guint32 rand_value;
	pgm_mutex_lock (&g_rand_mutex);
	if (!g_rand.seed)
		pgm_rand_create (&g_rand);
	rand_value = pgm_rand_int (&g_rand);
	pgm_mutex_unlock (&g_rand_mutex);
	return rand_value;
}

gint32
pgm_random_int_range (
	gint32		begin,
	gint32		end
	)
{
	const guint32 rand_value = pgm_random_int();
	return begin + rand_value % (end - begin);
}

/* eof */
