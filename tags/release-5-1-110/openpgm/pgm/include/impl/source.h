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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_SOURCE_H__
#define __PGM_IMPL_SOURCE_H__

#include <impl/framework.h>
#include <impl/receiver.h>

PGM_BEGIN_DECLS

/* Performance Counters */
enum {
	PGM_PC_SOURCE_DATA_BYTES_SENT,
	PGM_PC_SOURCE_DATA_MSGS_SENT,	    		/* msgs = packets not APDUs */
/*	PGM_PC_SOURCE_BYTES_BUFFERED, */	    	/* tx window contents in bytes */
/*	PGM_PC_SOURCE_MSGS_BUFFERED, */
	PGM_PC_SOURCE_BYTES_SENT,
/*	PGM_PC_SOURCE_RAW_NAKS_RECEIVED, */
	PGM_PC_SOURCE_CKSUM_ERRORS,
	PGM_PC_SOURCE_MALFORMED_NAKS,
	PGM_PC_SOURCE_PACKETS_DISCARDED,
	PGM_PC_SOURCE_PARITY_BYTES_RETRANSMITTED,
	PGM_PC_SOURCE_SELECTIVE_BYTES_RETRANSMITTED,
	PGM_PC_SOURCE_PARITY_MSGS_RETRANSMITTED,
	PGM_PC_SOURCE_SELECTIVE_MSGS_RETRANSMITTED,
	PGM_PC_SOURCE_PARITY_NAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NAK_PACKETS_RECEIVED,   /* total packets */
	PGM_PC_SOURCE_PARITY_NAKS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NAKS_RECEIVED,	    	/* serial numbers */
	PGM_PC_SOURCE_PARITY_NAKS_IGNORED,
	PGM_PC_SOURCE_SELECTIVE_NAKS_IGNORED,
	PGM_PC_SOURCE_ACK_ERRORS,
/*	PGM_PC_SOURCE_PGMCC_ACKER, */
	PGM_PC_SOURCE_TRANSMISSION_CURRENT_RATE,
	PGM_PC_SOURCE_ACK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_PARITY_NNAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NNAK_PACKETS_RECEIVED,
	PGM_PC_SOURCE_PARITY_NNAKS_RECEIVED,
	PGM_PC_SOURCE_SELECTIVE_NNAKS_RECEIVED,
	PGM_PC_SOURCE_NNAK_ERRORS,

/* marker */
	PGM_PC_SOURCE_MAX
};

PGM_GNUC_INTERNAL bool pgm_send_spm (pgm_sock_t*const, const int) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_deferred_nak (pgm_sock_t*const);
PGM_GNUC_INTERNAL bool pgm_on_spmr (pgm_sock_t*const restrict, pgm_peer_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_nak (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_nnak (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_on_ack (pgm_sock_t*const restrict, struct pgm_sk_buff_t*const restrict) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_IMPL_SOURCE_H__ */

