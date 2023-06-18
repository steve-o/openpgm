/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM socket.
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
#ifndef __PGM_SOCKET_H__
#define __PGM_SOCKET_H__

typedef struct pgm_sock_t pgm_sock_t;
struct pgm_sockaddr_t;
struct pgm_addrinfo_t;
struct pgm_fecinto_t;

#ifdef HAVE_POLL
#	include <poll.h>
#endif
#ifdef HAVE_EPOLL
#	include <sys/epoll.h>
#endif
#ifndef _WIN32
#ifdef _AIX
#   define IP_MULTICAST
#endif
#	include <sys/select.h>
#	include <sys/socket.h>
#endif
#include <pgm/types.h>
#include <pgm/error.h>
#include <pgm/msgv.h>
#include <pgm/tsi.h>

PGM_BEGIN_DECLS

struct pgm_sockaddr_t {
	uint16_t				sa_port;	/* data-destination port */
	pgm_tsi_t				sa_addr;
};

struct pgm_addrinfo_t {
	sa_family_t				ai_family;
	uint32_t				ai_recv_addrs_len;
	struct pgm_group_source_req* restrict	ai_recv_addrs;
	uint32_t				ai_send_addrs_len;
	struct pgm_group_source_req* restrict	ai_send_addrs;
};

/* Extends RFC 3678 struct group_req, but not always binary compatible due to forced packing, e.g. OSX. */
struct pgm_group_source_req
{
	uint32_t		gsr_interface;	/* interface index */
	struct sockaddr_storage	gsr_group;	/* group address */
	struct sockaddr_storage	gsr_source;	/* group source */
	struct sockaddr_storage	gsr_addr;	/* interface address */
};

struct pgm_interface_req_t {
	uint32_t				ir_interface;
	uint32_t				ir_scope_id;
	struct sockaddr_storage			ir_address;
};

#define	PGM_HAS_IR_ADDRESS	1

struct pgm_fecinfo_t {
	uint8_t					block_size;
	uint8_t					proactive_packets;
	uint8_t					group_size;
	bool					ondemand_parity_enabled;
	bool					var_pktlen_enabled;
};

struct pgm_pgmccinfo_t {
	uint32_t				ack_bo_ivl;
	uint32_t				ack_c;
	uint32_t				ack_c_p;
};

/* socket options */
enum {
	PGM_SEND_SOCK		= 0x2000,
	PGM_RECV_SOCK,
	PGM_REPAIR_SOCK,
	PGM_PENDING_SOCK,
	PGM_ACK_SOCK,
	PGM_TIME_REMAIN,
	PGM_RATE_REMAIN,
	PGM_IP_ROUTER_ALERT,
	PGM_MTU,
	PGM_MSSS,
	PGM_MSS,
	PGM_PDU,
	PGM_MULTICAST_LOOP,
	PGM_MULTICAST_HOPS,
	PGM_TOS,
	PGM_AMBIENT_SPM,
	PGM_HEARTBEAT_SPM,
	PGM_TXW_BYTES,
	PGM_TXW_SQNS,
	PGM_TXW_SECS,
	PGM_TXW_MAX_RTE,
	PGM_PEER_EXPIRY,
	PGM_SPMR_EXPIRY,
	PGM_RXW_BYTES,
	PGM_RXW_SQNS,
	PGM_RXW_SECS,
	PGM_RXW_MAX_RTE,
	PGM_NAK_BO_IVL,
	PGM_NAK_RPT_IVL,
	PGM_NAK_RDATA_IVL,
	PGM_NAK_DATA_RETRIES,
	PGM_NAK_NCF_RETRIES,
	PGM_USE_FEC,
	PGM_USE_CR,
	PGM_USE_PGMCC,
	PGM_SEND_ONLY,
	PGM_RECV_ONLY,
	PGM_PASSIVE,
	PGM_ABORT_ON_RESET,
	PGM_NOBLOCK,
	PGM_SEND_GROUP,
	PGM_JOIN_GROUP,
	PGM_LEAVE_GROUP,
	PGM_BLOCK_SOURCE,
	PGM_UNBLOCK_SOURCE,
	PGM_JOIN_SOURCE_GROUP,
	PGM_LEAVE_SOURCE_GROUP,
	PGM_MSFILTER,
	PGM_UDP_ENCAP_UCAST_PORT,
	PGM_UDP_ENCAP_MCAST_PORT,
	PGM_UNCONTROLLED_ODATA,
	PGM_UNCONTROLLED_RDATA,
	PGM_ODATA_MAX_RTE,
	PGM_RDATA_MAX_RTE
};

/* IO status */
enum {
	PGM_IO_STATUS_ERROR,		/* an error occurred */
	PGM_IO_STATUS_NORMAL,		/* success */
	PGM_IO_STATUS_RESET,		/* session reset */
	PGM_IO_STATUS_FIN,		/* session finished */
	PGM_IO_STATUS_EOF,		/* socket closed */
	PGM_IO_STATUS_WOULD_BLOCK,	/* resource temporarily unavailable */
	PGM_IO_STATUS_RATE_LIMITED,	/* would-block on rate limit, check timer */
	PGM_IO_STATUS_TIMER_PENDING,	/* would-block with pending timer */
	PGM_IO_STATUS_CONGESTION	/* would-block waiting on ACK or timeout */
};

/* Socket count for event handlers */
#define PGM_SEND_SOCKET_READ_COUNT		3
#define PGM_SEND_SOCKET_WRITE_COUNT		1
#define PGM_RECV_SOCKET_READ_COUNT		2
#define PGM_RECV_SOCKET_WRITE_COUNT		0
#define PGM_BUS_SOCKET_READ_COUNT		PGM_SEND_SOCKET_READ_COUNT
#define PGM_BUS_SOCKET_WRITE_COUNT		PGM_SEND_SOCKET_WRITE_COUNT

bool pgm_socket (pgm_sock_t**restrict, const sa_family_t, const int, const int, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_bind (pgm_sock_t*restrict, const struct pgm_sockaddr_t*const restrict, const socklen_t, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_bind3 (pgm_sock_t*restrict, const struct pgm_sockaddr_t*const restrict, const socklen_t, const struct pgm_interface_req_t*const, const socklen_t, const struct pgm_interface_req_t*const, const socklen_t, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_connect (pgm_sock_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
bool pgm_close (pgm_sock_t*, bool);
bool pgm_setsockopt (pgm_sock_t*const restrict, const int, const int, const void*restrict, const socklen_t);
bool pgm_getsockopt (pgm_sock_t*const restrict, const int, const int, void*restrict, socklen_t*restrict);
bool pgm_getaddrinfo (const char*restrict, const struct pgm_addrinfo_t*const restrict, struct pgm_addrinfo_t**restrict, pgm_error_t**restrict);
void pgm_freeaddrinfo (struct pgm_addrinfo_t*);
int pgm_send (pgm_sock_t*const restrict, const void*restrict, const size_t, size_t*restrict);
int pgm_sendv (pgm_sock_t*const restrict, const struct pgm_iovec*const restrict, const unsigned, const bool, size_t*restrict);
int pgm_send_skbv (pgm_sock_t*const restrict, struct pgm_sk_buff_t**const restrict, const unsigned, const bool, size_t*restrict);
int pgm_recvmsg (pgm_sock_t*const restrict, struct pgm_msgv_t*const restrict, const int, size_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recvmsgv (pgm_sock_t*const restrict, struct pgm_msgv_t*const restrict, const size_t, const int, size_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recv (pgm_sock_t*const restrict, void*restrict, const size_t, const int, size_t*const restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recvfrom (pgm_sock_t*const restrict, void*restrict, const size_t, const int, size_t*restrict, struct pgm_sockaddr_t*restrict, socklen_t*restrict, pgm_error_t**restrict) PGM_GNUC_WARN_UNUSED_RESULT;

bool pgm_getsockname (pgm_sock_t*const restrict, struct pgm_sockaddr_t*restrict, socklen_t*restrict);
int pgm_select_info (pgm_sock_t*const restrict, fd_set*const restrict, fd_set*const restrict, int*const restrict);
#if defined( POLLIN ) && defined( POLLOUT )
int pgm_poll_info (pgm_sock_t*const restrict, struct pollfd*const restrict, int*const restrict, const short);
#endif
#if defined( _WIN32 ) && ( _WIN32_WINNT >= 0x0600 )
int pgm_wsapoll_info (pgm_sock_t*const restrict, WSAPOLLFD*const restrict, ULONG*const restrict, const short);
#endif
#if defined( EPOLLIN ) && defined( EPOLLOUT )
int pgm_epoll_ctl (pgm_sock_t*const, const int, const int, const int);
#endif

static inline
const char*
pgm_family_string (
	const int       family
	)
{
	const char* c;

	switch (family) {
	case AF_UNSPEC:         c = "AF_UNSPEC"; break;
	case AF_INET:           c = "AF_INET"; break;
	case AF_INET6:          c = "AF_INET6"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

char* pgm_gsr_to_string (const struct pgm_group_source_req* gsr, char* text, size_t len);
char* pgm_addrinfo_to_string (const struct pgm_addrinfo_t* addr, char* text, size_t len);

PGM_END_DECLS

#endif /* __PGM_SOCKET_H__ */
