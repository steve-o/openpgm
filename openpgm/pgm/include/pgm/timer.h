/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
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

#ifndef __PGM_TIMER_H__
#define __PGM_TIMER_H__

#include <glib.h>

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


G_BEGIN_DECLS

PGM_GNUC_INTERNAL gboolean pgm_timer_prepare (pgm_transport_t* const);
PGM_GNUC_INTERNAL gboolean pgm_timer_check (pgm_transport_t* const);
PGM_GNUC_INTERNAL long pgm_timer_expiration (pgm_transport_t* const);
PGM_GNUC_INTERNAL gboolean pgm_timer_dispatch (pgm_transport_t* const);

static inline void pgm_timer_lock (pgm_transport_t* const transport)
{
	if (transport->can_send_data) g_static_mutex_lock (&transport->timer_mutex);
}

static inline void pgm_timer_unlock (pgm_transport_t* const transport)
{
	if (transport->can_send_data) g_static_mutex_unlock (&transport->timer_mutex);
}

G_END_DECLS

#endif /* __PGM_TIMER_H__ */

