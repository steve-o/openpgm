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

GSource* pgm_timer_create (pgm_transport_t*);
int pgm_timer_add_full (pgm_transport_t*, gint);
int pgm_timer_add (pgm_transport_t*);

gpointer pgm_timer_thread (gpointer);

G_END_DECLS

#endif /* __PGM_TIMER_H__ */

