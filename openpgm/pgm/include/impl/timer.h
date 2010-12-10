/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_TIMER_H__
#define __PGM_IMPL_TIMER_H__

#include <impl/framework.h>
#include <impl/socket.h>

PGM_BEGIN_DECLS

PGM_GNUC_INTERNAL bool pgm_timer_prepare (pgm_sock_t*const);
PGM_GNUC_INTERNAL bool pgm_timer_check (pgm_sock_t*const);
PGM_GNUC_INTERNAL pgm_time_t pgm_timer_expiration (pgm_sock_t*const);
PGM_GNUC_INTERNAL bool pgm_timer_dispatch (pgm_sock_t*const);

static inline
void
pgm_timer_lock (
	pgm_sock_t* const sock
	)
{
	if (sock->can_send_data)
		pgm_mutex_lock (&sock->timer_mutex);
}

static inline
void
pgm_timer_unlock (
	pgm_sock_t* const sock
	)
{
	if (sock->can_send_data)
		pgm_mutex_unlock (&sock->timer_mutex);
}

PGM_END_DECLS

#endif /* __PGM_IMPL_TIMER_H__ */

