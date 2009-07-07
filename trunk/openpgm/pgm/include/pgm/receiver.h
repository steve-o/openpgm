/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM receiver transport.
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

#ifndef __PGM_RECEIVER_H__
#define __PGM_RECEIVER_H__

#include <glib.h>

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


G_BEGIN_DECLS

int pgm_transport_set_rxw_preallocate (pgm_transport_t*, guint);
int pgm_transport_set_rxw_sqns (pgm_transport_t*, guint);
int pgm_transport_set_rxw_secs (pgm_transport_t*, guint);
int pgm_transport_set_rxw_max_rte (pgm_transport_t*, guint);
int pgm_transport_set_peer_expiry (pgm_transport_t*, guint);
int pgm_transport_set_spmr_expiry (pgm_transport_t*, guint);
int pgm_transport_set_nak_bo_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_rpt_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_rdata_ivl (pgm_transport_t*, guint);
int pgm_transport_set_nak_data_retries (pgm_transport_t*, guint);
int pgm_transport_set_nak_ncf_retries (pgm_transport_t*, guint);

gssize pgm_transport_recvmsg (pgm_transport_t*, pgm_msgv_t*, int);
gssize pgm_transport_recvmsgv (pgm_transport_t*, pgm_msgv_t*, gsize, int);
gssize pgm_transport_recv (pgm_transport_t*, gpointer, gsize, int);
gssize pgm_transport_recvfrom (pgm_transport_t*, gpointer, gsize, int, pgm_tsi_t*);

void pgm_peer_unref (pgm_peer_t*);

G_END_DECLS

#endif /* __PGM_RECEIVER_H__ */

