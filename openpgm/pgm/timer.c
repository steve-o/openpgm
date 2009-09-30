/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM timer thread.
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

#include "pgm/transport.h"
#include "pgm/source.h"
#include "pgm/receiver.h"
#include "pgm/timer.h"

//#define TIMER_DEBUG

#ifndef TIMER_DEBUG
#	define G_DISABLE_ASSERT
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* determine which timer fires next: spm (ihb_tmr), nak_rb_ivl, nak_rpt_ivl, or nak_rdata_ivl
 * and check whether its already due.
 *
 * called in transport creation so locks unrequired.
 */

gboolean
pgm_timer_prepare (
	pgm_transport_t* const	transport
	)
{
	glong msec;

/* pre-conditions */
	g_assert (NULL != transport);
	g_assert (transport->can_send_data || transport->can_recv_data);

	pgm_time_t now = pgm_time_update_now();
	pgm_time_t expiration;

	if (transport->can_send_data)
		expiration = transport->next_ambient_spm;
	else
		expiration = now + transport->peer_expiry;

	transport->next_poll = expiration;

/* advance time again to adjust for processing time out of the event loop, this
 * could cause further timers to expire even before checking for new wire data.
 */
	msec = pgm_to_msecs ((gint64)expiration - (gint64)now);
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (G_MAXINT, (guint)msec);
	g_trace ("%dms", (gint)msec);
	return (msec == 0);
}

gboolean
pgm_timer_check (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	gboolean expired;

/* pre-conditions */
	g_assert (NULL != transport);

	pgm_timer_lock (transport);
	expired = pgm_time_after_eq (now, transport->next_poll);
	pgm_timer_unlock (transport);
	return expired;
}

/* return next timer expiration in microseconds (μs)
 */

long
pgm_timer_expiration (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	long expiration;

/* pre-conditions */
	g_assert (NULL != transport);

	pgm_timer_lock (transport);
	expiration = pgm_time_after (transport->next_poll, now) ? (long)pgm_to_usecs (transport->next_poll - now) : 0;
	pgm_timer_unlock (transport);
	return expiration;
}

/* call all timers, assume that time_now has been updated by either pgm_timer_prepare
 * or pgm_timer_check and no other method calls here.
 * 
 * returns TRUE on success, returns FALSE on blocked send-in-receive operation.
 */

gboolean
pgm_timer_dispatch (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	pgm_time_t next_expiration = 0;

/* pre-conditions */
	g_assert (NULL != transport);

	g_trace ("pgm_timer_dispatch (transport:%p)", (gpointer)transport);

/* find which timers have expired and call each */
	if (transport->can_recv_data)
	{
		if (!pgm_check_peer_nak_state (transport, now))
			return FALSE;
		next_expiration = pgm_min_nak_expiry (now + transport->peer_expiry, transport);
g_warning ("min-nak returned %i", (int)(next_expiration - now));
	}

	if (transport->can_send_data)
	{
		pgm_timer_lock (transport);
		guint spm_heartbeat_state = transport->spm_heartbeat_state;
		pgm_time_t next_heartbeat_spm = transport->next_heartbeat_spm;
		pgm_time_t next_spm = spm_heartbeat_state ? next_heartbeat_spm : transport->next_ambient_spm;
		pgm_timer_unlock (transport);

		if (pgm_time_after_eq (now, next_spm))
		{
			if (!pgm_send_spm (transport, 0))
				return FALSE;

			if (spm_heartbeat_state)
			{
				do {
					next_spm += transport->spm_heartbeat_interval[spm_heartbeat_state++];
					if (spm_heartbeat_state == transport->spm_heartbeat_len) {
						spm_heartbeat_state = 0;
						next_spm = now + transport->spm_ambient_interval;
						break;
					}
				} while (pgm_time_after_eq (now, next_spm));
			}
			else
				next_spm = now + transport->spm_ambient_interval;
			
			next_expiration = next_expiration > 0 ? MIN(next_expiration, next_spm) : next_spm;
		}
	}

	pgm_timer_lock (transport);
	transport->next_poll = next_expiration;
	pgm_timer_unlock (transport);
	return TRUE;
}

/* eof */
