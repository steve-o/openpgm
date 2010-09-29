/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM receiver transport.
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

#ifndef __PGM_RECEIVERP_H__
#define __PGM_RECEIVERP_H__

#include <glib.h>

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


G_BEGIN_DECLS

PGM_GNUC_INTERNAL pgm_peer_t* pgm_new_peer (pgm_transport_t* const, const pgm_tsi_t* const, const struct sockaddr* const, const gsize, const struct sockaddr* const, const gsize, const pgm_time_t);
PGM_GNUC_INTERNAL void pgm_peer_unref (pgm_peer_t*);
PGM_GNUC_INTERNAL int pgm_flush_peers_pending (pgm_transport_t* const, pgm_msgv_t**, const pgm_msgv_t* const, gsize* const, guint* const);
PGM_GNUC_INTERNAL gboolean pgm_peer_has_pending (pgm_peer_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL void pgm_peer_set_pending (pgm_transport_t* const, pgm_peer_t* const);
PGM_GNUC_INTERNAL gboolean pgm_check_peer_nak_state (pgm_transport_t* const, const pgm_time_t);
PGM_GNUC_INTERNAL void pgm_set_reset_error (pgm_transport_t* const, pgm_peer_t* const, pgm_msgv_t* const);
PGM_GNUC_INTERNAL pgm_time_t pgm_min_nak_expiry (pgm_time_t, pgm_transport_t*) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_peer_nak (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_data (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_ncf (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_spm (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_poll (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __PGM_RECEIVERP_H__ */

