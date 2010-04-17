/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM timer thread.
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

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <pgm/framework.h>
#include "pgm/timer.h"
#include "pgm/receiver.h"
#include "pgm/source.h"


//#define TIMER_DEBUG


/* determine which timer fires next: spm (ihb_tmr), nak_rb_ivl, nak_rpt_ivl, or nak_rdata_ivl
 * and check whether its already due.
 *
 * called in transport creation so locks unrequired.
 */

bool
pgm_timer_prepare (
	pgm_transport_t* const	transport
	)
{
	int32_t msec;

/* pre-conditions */
	pgm_assert (NULL != transport);
	pgm_assert (transport->can_send_data || transport->can_recv_data);

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
	msec = pgm_to_msecs ((int64_t)expiration - (int64_t)now);
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (INT32_MAX, msec);
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiration in %" PRIi32 "ms"), msec);
	return (msec == 0);
}

bool
pgm_timer_check (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	bool expired;

/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_timer_lock (transport);
	expired = pgm_time_after_eq (now, transport->next_poll);
	pgm_timer_unlock (transport);
	return expired;
}

/* return next timer expiration in microseconds (μs)
 */

pgm_time_t
pgm_timer_expiration (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	pgm_time_t expiration;

/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_timer_lock (transport);
	expiration = pgm_time_after (transport->next_poll, now) ? pgm_to_usecs (transport->next_poll - now) : 0;
	pgm_timer_unlock (transport);
	return expiration;
}

/* call all timers, assume that time_now has been updated by either pgm_timer_prepare
 * or pgm_timer_check and no other method calls here.
 * 
 * returns TRUE on success, returns FALSE on blocked send-in-receive operation.
 */

bool
pgm_timer_dispatch (
	pgm_transport_t* const	transport
	)
{
	const pgm_time_t now = pgm_time_update_now();
	pgm_time_t next_expiration = 0;

/* pre-conditions */
	pgm_assert (NULL != transport);

	pgm_debug ("pgm_timer_dispatch (transport:%p)", (const void*)transport);

/* find which timers have expired and call each */
	if (transport->can_recv_data)
	{
		if (!pgm_check_peer_nak_state (transport, now))
			return FALSE;
		next_expiration = pgm_min_nak_expiry (now + transport->peer_expiry, transport);
	}

	if (transport->can_send_data)
	{
		pgm_mutex_lock (&transport->timer_mutex);
		const unsigned spm_heartbeat_state = transport->spm_heartbeat_state;
		const pgm_time_t next_heartbeat_spm = transport->next_heartbeat_spm;
		pgm_mutex_unlock (&transport->timer_mutex);

/* no lock needed on ambient */
		const pgm_time_t next_ambient_spm = transport->next_ambient_spm;
		pgm_time_t next_spm = spm_heartbeat_state ? MIN(next_heartbeat_spm, next_ambient_spm) : next_ambient_spm;

		if (pgm_time_after_eq (now, next_spm)
		    && !pgm_send_spm (transport, 0))
			return FALSE;

/* ambient timing not so important so base next event off current time */
		if (pgm_time_after_eq (now, next_ambient_spm))
		{
			transport->next_ambient_spm = now + transport->spm_ambient_interval;
			next_spm = spm_heartbeat_state ? MIN(next_heartbeat_spm, transport->next_ambient_spm) : transport->next_ambient_spm;
		}

/* heartbeat timing is often high resolution so base times to last event */
		if (spm_heartbeat_state && pgm_time_after_eq (now, next_heartbeat_spm))
		{
			unsigned new_heartbeat_state    = spm_heartbeat_state;
			pgm_time_t new_heartbeat_spm = next_heartbeat_spm;
			do {
				new_heartbeat_spm += transport->spm_heartbeat_interval[new_heartbeat_state++];
				if (new_heartbeat_state == transport->spm_heartbeat_len) {
					new_heartbeat_state = 0;
					new_heartbeat_spm   = now + transport->spm_ambient_interval;
					break;
				}
			} while (pgm_time_after_eq (now, new_heartbeat_spm));
/* check for reset heartbeat */
			pgm_mutex_lock (&transport->timer_mutex);
			if (next_heartbeat_spm == transport->next_heartbeat_spm) {
				transport->spm_heartbeat_state = new_heartbeat_state;
				transport->next_heartbeat_spm  = new_heartbeat_spm;
				next_spm = MIN(transport->next_ambient_spm, new_heartbeat_spm);
			} else
				next_spm = MIN(transport->next_ambient_spm, transport->next_heartbeat_spm);
			transport->next_poll = next_expiration > 0 ? MIN(next_expiration, next_spm) : next_spm;
			pgm_mutex_unlock (&transport->timer_mutex);
			return TRUE;
		}

		next_expiration = next_expiration > 0 ? MIN(next_expiration, next_spm) : next_spm;

/* check for reset */
		pgm_mutex_lock (&transport->timer_mutex);
		transport->next_poll = transport->next_poll > now ? MIN(transport->next_poll, next_expiration) : next_expiration;
		pgm_mutex_unlock (&transport->timer_mutex);
	}
	else
		transport->next_poll = next_expiration;

	return TRUE;
}

/* eof */
