/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * network interface handling.
 *
 * Copyright (c) 2006 Miru Limited.
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

#ifndef __PGM_IF_H__
#define __PGM_IF_H__

#include <glib.h>

#ifndef __PGM_GSI_H__
#	include <pgm/gsi.h>
#endif


struct pgm_transport_info_t {
	pgm_gsi_t			ti_gsi;
	int				ti_flags;
	int				ti_family;
	int				ti_udp_encap_ucast_port;
	int				ti_udp_encap_mcast_port;
	int				ti_sport;
	int				ti_dport;
	gsize				ti_recv_addrs_len;
	struct group_source_req*	ti_recv_addrs;
	gsize				ti_send_addrs_len;
	struct group_source_req*	ti_send_addrs;
};

G_BEGIN_DECLS

gboolean pgm_if_get_transport_info (const char*, const struct pgm_transport_info_t*, struct pgm_transport_info_t**, pgm_error_t**);
void pgm_if_free_transport_info (struct pgm_transport_info_t*);
void pgm_if_print_all (void);

G_END_DECLS

#endif /* __PGM_IF_H__ */
