/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM timer thread.
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
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/timer.h>
#include <impl/receiver.h>
#include <impl/source.h>


//#define TIMER_DEBUG


/* determine which timer fires next: spm (ihb_tmr), nak_rb_ivl, nak_rpt_ivl, or nak_rdata_ivl
 * and check whether its already due.
 *
 * called in sock creation so locks unrequired.
 */

PGM_GNUC_INTERNAL
bool
pgm_timer_prepare (
	pgm_sock_t* const	sock
	)
{
	pgm_time_t	now, expiration;
	int32_t		msec;

/* pre-conditions */
	pgm_assert (NULL != sock);
	pgm_assert (sock->can_send_data || sock->can_recv_data);

	now = pgm_time_update_now();

	if (sock->can_send_data)
		expiration = sock->next_ambient_spm;
	else
		expiration = now + sock->peer_expiry;

	sock->next_poll = expiration;

/* advance time again to adjust for processing time out of the event loop, this
 * could cause further timers to expire even before checking for new wire data.
 */
	msec = (int32_t)pgm_to_msecs ((int64_t)expiration - (int64_t)now);
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (INT32_MAX, msec);
	pgm_trace (PGM_LOG_ROLE_NETWORK,_("Next expiration in %" PRIi32 "ms"), msec);
	return (msec == 0);
}

PGM_GNUC_INTERNAL
bool
pgm_timer_check (
	pgm_sock_t* const	sock
	)
{
	const pgm_time_t now = pgm_time_update_now();
	bool expired;

/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_timer_lock (sock);
	expired = pgm_time_after_eq (now, sock->next_poll);
	pgm_timer_unlock (sock);
	return expired;
}

/* return next timer expiration in microseconds (μs)
 */

PGM_GNUC_INTERNAL
pgm_time_t
pgm_timer_expiration (
	pgm_sock_t* const	sock
	)
{
	const pgm_time_t now = pgm_time_update_now();
	pgm_time_t expiration;

/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_timer_lock (sock);
	expiration = pgm_time_after (sock->next_poll, now) ? pgm_to_usecs (sock->next_poll - now) : 0;
	pgm_timer_unlock (sock);
	return expiration;
}

/* call all timers, assume that time_now has been updated by either pgm_timer_prepare
 * or pgm_timer_check and no other method calls here.
 * 
 * returns TRUE on success, returns FALSE on blocked send-in-receive operation.
 */

PGM_GNUC_INTERNAL
bool
pgm_timer_dispatch (
	pgm_sock_t* const	sock
	)
{
	const pgm_time_t now = pgm_time_update_now();
	pgm_time_t next_expiration = 0;

/* pre-conditions */
	pgm_assert (NULL != sock);

	pgm_debug ("pgm_timer_dispatch (sock:%p)", (const void*)sock);

/* find which timers have expired and call each */
	if (sock->can_recv_data)
	{
		if (!pgm_check_peer_state (sock, now))
			return FALSE;
		next_expiration = pgm_min_receiver_expiry (sock, now + sock->peer_expiry);
	}

	if (sock->can_send_data)
	{
/* reset congestion control on ACK timeout */
		if (sock->use_pgmcc &&
		    sock->tokens < pgm_fp8 (1) &&
		    0 != sock->ack_expiry)
		{
			if (pgm_time_after_eq (now, sock->ack_expiry))
			{
#ifdef DEBUG_PGMCC
char nows[1024];
time_t t = time (NULL);
struct tm* tmp = localtime (&t);
strftime (nows, sizeof(nows), "%Y-%m-%d %H:%M:%S", tmp);
printf ("ACK timeout, T:%u W:%u\n", pgm_fp8tou(sock->tokens), pgm_fp8tou(sock->cwnd_size));
#endif
				sock->tokens = sock->cwnd_size = pgm_fp8 (1);
				sock->ack_bitmap = 0xffffffff;
				sock->ack_expiry = 0;

/* notify blocking tx thread that transmission time is now available */
				pgm_notify_send (&sock->ack_notify);
			}
			next_expiration = next_expiration > 0 ? MIN(next_expiration, sock->ack_expiry) : sock->ack_expiry;
		}

/* SPM broadcast */
		pgm_mutex_lock (&sock->timer_mutex);
		const unsigned spm_heartbeat_state = sock->spm_heartbeat_state;
		const pgm_time_t next_heartbeat_spm = sock->next_heartbeat_spm;
		pgm_mutex_unlock (&sock->timer_mutex);

/* no lock needed on ambient */
		const pgm_time_t next_ambient_spm = sock->next_ambient_spm;
		pgm_time_t next_spm = spm_heartbeat_state ? MIN(next_heartbeat_spm, next_ambient_spm) : next_ambient_spm;

		if (pgm_time_after_eq (now, next_spm) &&
		   !pgm_send_spm (sock, 0))
			return FALSE;

/* ambient timing not so important so base next event off current time */
		if (pgm_time_after_eq (now, next_ambient_spm))
		{
			sock->next_ambient_spm = now + sock->spm_ambient_interval;
			next_spm = spm_heartbeat_state ? MIN(next_heartbeat_spm, sock->next_ambient_spm) : sock->next_ambient_spm;
		}

/* heartbeat timing is often high resolution so base times to last event */
		if (spm_heartbeat_state && pgm_time_after_eq (now, next_heartbeat_spm))
		{
			unsigned new_heartbeat_state = spm_heartbeat_state;
			pgm_time_t new_heartbeat_spm = next_heartbeat_spm;
			do {
				new_heartbeat_spm += sock->spm_heartbeat_interval[new_heartbeat_state++];
				if (new_heartbeat_state == sock->spm_heartbeat_len) {
					new_heartbeat_state = 0;
					new_heartbeat_spm   = now + sock->spm_ambient_interval;
					break;
				}
			} while (pgm_time_after_eq (now, new_heartbeat_spm));
/* check for reset heartbeat */
			pgm_mutex_lock (&sock->timer_mutex);
			if (next_heartbeat_spm == sock->next_heartbeat_spm) {
				sock->spm_heartbeat_state = new_heartbeat_state;
				sock->next_heartbeat_spm  = new_heartbeat_spm;
				next_spm = MIN(sock->next_ambient_spm, new_heartbeat_spm);
			} else
				next_spm = MIN(sock->next_ambient_spm, sock->next_heartbeat_spm);
			sock->next_poll = next_expiration > 0 ? MIN(next_expiration, next_spm) : next_spm;
			pgm_mutex_unlock (&sock->timer_mutex);
			return TRUE;
		}

		next_expiration = next_expiration > 0 ? MIN(next_expiration, next_spm) : next_spm;

/* check for reset */
		pgm_mutex_lock (&sock->timer_mutex);
		sock->next_poll = sock->next_poll > now ? MIN(sock->next_poll, next_expiration) : next_expiration;
		pgm_mutex_unlock (&sock->timer_mutex);
	}
	else
		sock->next_poll = next_expiration;

	return TRUE;
}

/* eof */
