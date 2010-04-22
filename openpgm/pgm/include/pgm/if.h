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

#include <sys/socket.h>
#include <pgm/types.h>
#include <pgm/gsi.h>

PGM_BEGIN_DECLS

struct pgm_transport_info_t {
	pgm_gsi_t				ti_gsi;
	int					ti_flags;
	sa_family_t				ti_family;
	uint16_t				ti_udp_encap_ucast_port;
	uint16_t				ti_udp_encap_mcast_port;
	uint16_t				ti_sport;
	uint16_t				ti_dport;
	size_t					ti_recv_addrs_len;
	struct group_source_req* restrict	ti_recv_addrs;
	size_t					ti_send_addrs_len;
	struct group_source_req* restrict	ti_send_addrs;
};

bool pgm_if_get_transport_info (const char*restrict, const struct pgm_transport_info_t*const restrict, struct pgm_transport_info_t**restrict, pgm_error_t**restrict);
void pgm_if_free_transport_info (struct pgm_transport_info_t*);
void pgm_if_print_all (void);

PGM_END_DECLS

#endif /* __PGM_IF_H__ */
