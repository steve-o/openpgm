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
#include "pgm/timer.h"

#define TIMER_DEBUG

#ifndef TIMER_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* externals */
extern pgm_time_t min_nak_expiry (pgm_time_t, pgm_transport_t*);
extern int send_spm_unlocked (pgm_transport_t*);
extern void check_peer_nak_state (pgm_transport_t*);


/* internal: Glib event loop GSource of spm & rx state timers */
struct pgm_timer_t {
	GSource		source;
	pgm_time_t	expiration;
	pgm_transport_t* transport;
};

typedef struct pgm_timer_t pgm_timer_t;

static gboolean pgm_timer_prepare (GSource*, gint*);
static gboolean pgm_timer_check (GSource*);
static gboolean pgm_timer_dispatch (GSource*, GSourceFunc, gpointer);

static GSourceFuncs g_pgm_timer_funcs = {
	.prepare		= pgm_timer_prepare,
	.check			= pgm_timer_check,
	.dispatch		= pgm_timer_dispatch,
	.finalize		= NULL,
	.closure_callback	= NULL
};


/* timer thread execution function.
 *
 * when thread loop is terminated, returns NULL, to be returned by
 * g_thread_join()
 */

gpointer
pgm_timer_thread (
	gpointer		data
	)
{
	pgm_transport_t* transport = (pgm_transport_t*)data;

	transport->timer_context = g_main_context_new ();
	g_mutex_lock (transport->thread_mutex);
	transport->timer_loop = g_main_loop_new (transport->timer_context, FALSE);
	g_cond_signal (transport->thread_cond);
	g_mutex_unlock (transport->thread_mutex);

	g_trace ("entering event loop.");
	g_main_loop_run (transport->timer_loop);
	g_trace ("leaving event loop.");

/* cleanup */
	g_main_loop_unref (transport->timer_loop);
	g_main_context_unref (transport->timer_context);

	return NULL;
}

GSource*
pgm_timer_create (
	pgm_transport_t*	transport
	)
{
	g_return_val_if_fail (transport != NULL, NULL);

	GSource *source = g_source_new (&g_pgm_timer_funcs, sizeof(pgm_timer_t));
	pgm_timer_t *timer = (pgm_timer_t*)source;

	timer->transport = transport;

	return source;
}

/* on success, returns id of GSource 
 */

int
pgm_timer_add_full (
	pgm_transport_t*	transport,
	gint			priority
	)
{
	g_return_val_if_fail (transport != NULL, -EINVAL);

	GSource* source = pgm_timer_create (transport);

	if (priority != G_PRIORITY_DEFAULT)
		g_source_set_priority (source, priority);

	guint id = g_source_attach (source, transport->timer_context);
	g_source_unref (source);

	return id;
}

int
pgm_timer_add (
	pgm_transport_t*	transport
	)
{
	return pgm_timer_add_full (transport, G_PRIORITY_HIGH_IDLE);
}

/* determine which timer fires next: spm (ihb_tmr), nak_rb_ivl, nak_rpt_ivl, or nak_rdata_ivl
 * and check whether its already due.
 */

static
gboolean
pgm_timer_prepare (
	GSource*		source,
	gint*			timeout
	)
{
	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	pgm_transport_t* transport = pgm_timer->transport;
	glong msec;

	g_static_mutex_lock (&transport->mutex);
	pgm_time_t now = pgm_time_update_now();
	pgm_time_t expiration = now + pgm_secs( 30 );

	if (transport->can_send_data)
	{
		expiration = transport->spm_heartbeat_state ? MIN(transport->next_heartbeat_spm, transport->next_ambient_spm) : transport->next_ambient_spm;
		g_trace ("spm %" G_GINT64_FORMAT " usec", (gint64)expiration - (gint64)now);
	}

/* save the nearest timer */
	if (transport->can_recv)
	{
		g_static_rw_lock_reader_lock (&transport->peers_lock);
		expiration = min_nak_expiry (expiration, transport);
		g_static_rw_lock_reader_unlock (&transport->peers_lock);
	}

	transport->next_poll = pgm_timer->expiration = expiration;
	g_static_mutex_unlock (&transport->mutex);

/* advance time again to adjust for processing time out of the event loop, this
 * could cause further timers to expire even before checking for new wire data.
 */
	msec = pgm_to_msecs((gint64)expiration - (gint64)now);
	if (msec < 0)
		msec = 0;
	else
		msec = MIN (G_MAXINT, (guint)msec);

	*timeout = (gint)msec;

	g_trace ("expiration in %i msec", (gint)msec);

	return (msec == 0);
}

static
gboolean
pgm_timer_check (
	GSource*		source
	)
{
	g_trace ("pgm_timer_check");

	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	const pgm_time_t now = pgm_time_update_now();

	gboolean retval = ( pgm_time_after_eq(now, pgm_timer->expiration) );
	if (!retval) g_thread_yield();
	return retval;
}

/* call all timers, assume that time_now has been updated by either pgm_timer_prepare
 * or pgm_timer_check and no other method calls here.
 */

static
gboolean
pgm_timer_dispatch (
	GSource*			source,
	G_GNUC_UNUSED GSourceFunc	callback,
	G_GNUC_UNUSED gpointer		user_data
	)
{
	g_trace ("pgm_timer_dispatch");

	pgm_timer_t* pgm_timer = (pgm_timer_t*)source;
	pgm_transport_t* transport = pgm_timer->transport;

/* find which timers have expired and call each */
	if (transport->can_send_data)
	{
		g_static_mutex_lock (&transport->mutex);
		if ( pgm_time_after_eq (pgm_time_now, transport->next_ambient_spm) )
		{
			send_spm_unlocked (transport);
			transport->spm_heartbeat_state = 0;
			transport->next_ambient_spm = pgm_time_now + transport->spm_ambient_interval;
		}
		else if ( transport->spm_heartbeat_state &&
			 pgm_time_after_eq (pgm_time_now, transport->next_heartbeat_spm) )
		{
			send_spm_unlocked (transport);
		
			if (transport->spm_heartbeat_interval[transport->spm_heartbeat_state])
			{
				transport->next_heartbeat_spm = pgm_time_now + transport->spm_heartbeat_interval[transport->spm_heartbeat_state++];
			}
			else
			{	/* transition heartbeat to ambient */
				transport->spm_heartbeat_state = 0;
			}
		}
		g_static_mutex_unlock (&transport->mutex);
	}

	if (transport->can_recv)
	{
		g_static_mutex_lock (&transport->waiting_mutex);
		g_static_mutex_lock (&transport->mutex);
		g_static_rw_lock_reader_lock (&transport->peers_lock);
		check_peer_nak_state (transport);
		g_static_rw_lock_reader_unlock (&transport->peers_lock);
		g_static_mutex_unlock (&transport->mutex);
		g_static_mutex_unlock (&transport->waiting_mutex);
	}

	return TRUE;
}

/* eof */
