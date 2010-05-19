/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM source socket.
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

#ifndef __PGM_SOURCE_H__
#define __PGM_SOURCE_H__

#include <pgm/framework.h>
#include <pgm/receiver.h>
#include <pgm/socket.h>

PGM_BEGIN_DECLS

int pgm_send (pgm_sock_t*const restrict, const void*restrict, const size_t, size_t*restrict);
int pgm_sendv (pgm_sock_t*const restrict, const struct pgm_iovec*const restrict, const unsigned, const bool, size_t*restrict);
int pgm_send_skbv (pgm_sock_t*const restrict, struct pgm_sk_buff_t**restrict, const unsigned, const bool, size_t*restrict);

PGM_GNUC_INTERNAL bool pgm_send_spm (pgm_sock_t*, int) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_deferred_nak (pgm_sock_t*const);
PGM_GNUC_INTERNAL bool pgm_on_spmr (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_nak (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_nnak (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_ack (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_SOURCE_H__ */

