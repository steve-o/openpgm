/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM source transport.
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

#ifndef __PGM_SOURCE_H__
#define __PGM_SOURCE_H__

#include <glib.h>

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


G_BEGIN_DECLS

int pgm_transport_set_ambient_spm (pgm_transport_t*, guint);
int pgm_transport_set_heartbeat_spm (pgm_transport_t*, const guint*, int);
int pgm_transport_set_txw_preallocate (pgm_transport_t*, guint);
int pgm_transport_set_txw_sqns (pgm_transport_t*, guint);
int pgm_transport_set_txw_secs (pgm_transport_t*, guint);
int pgm_transport_set_txw_max_rte (pgm_transport_t*, guint);

int send_spm_unlocked (pgm_transport_t*);
gboolean on_nak_notify (GIOChannel*, GIOCondition, gpointer);
int on_spmr (pgm_transport_t*, pgm_peer_t*, struct pgm_header*, gpointer, gsize);
int on_nak (pgm_transport_t*, struct pgm_header*, gpointer, gsize);
int on_nnak (pgm_transport_t*, struct pgm_header*, gpointer, gsize);

G_END_DECLS

#endif /* __PGM_SOURCE_H__ */

