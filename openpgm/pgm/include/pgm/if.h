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


typedef enum
{
	/* Derived from errno */
	PGM_IF_ERROR_NONET,
	PGM_IF_ERROR_NOTUNIQ,
	PGM_IF_ERROR_NODEV,
	PGM_IF_ERROR_XDEV,
	PGM_IF_ERROR_FAULT,		/* gethostname returned EFAULT */
	PGM_IF_ERROR_INVAL,
	PGM_IF_ERROR_PERM,
	PGM_IF_ERROR_ADDRFAMILY,	/* getaddrinfo return EAI_ADDRFAMILY */
	PGM_IF_ERROR_AGAIN,
	PGM_IF_ERROR_BADFLAGS,
	PGM_IF_ERROR_FAIL,
	PGM_IF_ERROR_FAMILY,
	PGM_IF_ERROR_MEMORY,
	PGM_IF_ERROR_NODATA,
	PGM_IF_ERROR_NONAME,
	PGM_IF_ERROR_SERVICE,
	PGM_IF_ERROR_SOCKTYPE,
	PGM_IF_ERROR_SYSTEM,
	PGM_IF_ERROR_FAILED
} pgm_if_error_e;

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
pgm_if_error_e pgm_if_error_from_errno (gint);
pgm_if_error_e pgm_if_error_from_h_errno (gint);
pgm_if_error_e pgm_if_error_from_eai_errno (gint);

G_END_DECLS

#endif /* __PGM_IF_H__ */
