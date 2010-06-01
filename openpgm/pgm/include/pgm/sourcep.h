/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM source transport.
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

#ifndef __PGM_SOURCEP_H__
#define __PGM_SOURCEP_H__

#include <glib.h>

#ifndef __PGM_TRANSPORT_H__
#	include <pgm/transport.h>
#endif


G_BEGIN_DECLS

PGM_GNUC_INTERNAL gboolean pgm_send_spm (pgm_transport_t*, int) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_deferred_nak (pgm_transport_t* const);
PGM_GNUC_INTERNAL gboolean pgm_on_spmr (pgm_transport_t* const, pgm_peer_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_nak (pgm_transport_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL gboolean pgm_on_nnak (pgm_transport_t* const, struct pgm_sk_buff_t* const) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __PGM_SOURCEP_H__ */

