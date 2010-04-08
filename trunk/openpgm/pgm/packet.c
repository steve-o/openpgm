/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM packet formats, RFC 3208.
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <libintl.h>
#define _(String) dgettext (GETTEXT_PACKAGE, String)
#include <glib.h>

#ifdef G_OS_UNIX
#	include <netdb.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <netinet/ip.h>
#	include <arpa/inet.h>
#else
#	include <ws2tcpip.h>
#	include <ipexport.h>
#endif

#include "pgm/string.h"
#include "pgm/ip.h"
#include "pgm/checksum.h"
#include "pgm/skbuff.h"
#include "pgm/packet.h"


//#define PACKET_DEBUG

#ifndef PACKET_DEBUG
#	define g_trace(...)		while (0)
#else
#	define g_trace(...)		g_debug(__VA_ARGS__)
#endif


/* globals */

#ifndef IPOPT_NOP
#	define IPOPT_NOP	IP_OPT_NOP
#endif
#ifndef IPOPT_EOL
#	define IPOPT_EOL	IP_OPT_EOL
#endif
#ifndef IPOPT_RR
#	define IPOPT_RR		IP_OPT_RR
#endif
#ifndef IPOPT_TS
#	define IPOPT_TS		IP_OPT_TS
#endif


static gboolean pgm_parse (struct pgm_sk_buff_t* const, pgm_error_t**);
static gboolean pgm_print_spm (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_poll (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_polr (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_odata (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_rdata (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_nak (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_nnak (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_ncf (const struct pgm_header* const, gconstpointer, const gsize);
static gboolean pgm_print_spmr (const struct pgm_header* const, gconstpointer, const gsize);
static gssize pgm_print_options (gconstpointer, gsize);


/* Parse a raw-IP packet for IP and PGM header and any payload.
 */

#define PGM_MIN_SIZE	( \
				sizeof(struct pgm_ip) + 	/* IPv4 header */ \
				sizeof(struct pgm_header) 	/* PGM header */ \
			)

gboolean
pgm_parse_raw (
	struct pgm_sk_buff_t* const	skb,		/* data will be modified */
	struct sockaddr* const		dst,
	pgm_error_t**			error
	)
{
/* pre-conditions */
	g_assert (NULL != skb);
	g_assert (NULL != dst);

	g_trace ("pgm_parse_raw (skb:%p dst:%p error:%p)",
		(gpointer)skb, (gpointer)dst, (gpointer)error);

/* minimum size should be IPv4 header plus PGM header, check IP version later */
	if (G_UNLIKELY(skb->len < PGM_MIN_SIZE))
	{
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_BOUNDS,
			     _("IP packet too small at %" G_GUINT16_FORMAT " bytes, expecting at least %" G_GUINT16_FORMAT " bytes."),
			     skb->len, (guint16)PGM_MIN_SIZE);
		return FALSE;
	}

/* IP packet header: IPv4
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |Version|  HL   |      ToS      |            Length             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Fragment ID         |R|D|M|     Fragment Offset     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      TTL      |    Protocol   |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Source IP Address                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Destination IP Address                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | IP Options when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+ ...
 * | Data ...
 * +-+-+- ...
 *
 * IPv6
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |Version| Traffic Class |             Flow Label                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Payload Length      |   Next Header |   Hop Limit   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                       Source IP Address                       |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                     Destination IP Address                    |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | IP Options when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+ ...
 * | Data ...
 * +-+-+- ...
 *
 */

/* decode IP header */
	const struct pgm_ip* ip = (struct pgm_ip*)skb->data;
	switch (ip->ip_v) {
	case 4: {
		struct sockaddr_in* sin = (struct sockaddr_in*)dst;
		sin->sin_family		= AF_INET;
		sin->sin_addr.s_addr	= ip->ip_dst.s_addr;
		break;
	}

	case 6:
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_AFNOSUPPORT,
			     _("IPv6 is not supported for raw IP header parsing."));
		return FALSE;

	default:
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_AFNOSUPPORT,
			     _("IP header reports an invalid version %d."),
			     ip->ip_v);
		return FALSE;
	}

	const gsize ip_header_length = ip->ip_hl * 4;		/* IP header length in 32bit octets */
	if (G_UNLIKELY(ip_header_length < sizeof(struct pgm_ip)))
	{
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_BOUNDS,
			     _("IP header reports an invalid header length %" G_GSIZE_FORMAT " bytes."),
			     ip_header_length);
		return FALSE;
	}

	gsize packet_length = g_ntohs(ip->ip_len);	/* total packet length */

/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	if (skb->len == packet_length + ip_header_length) {
		packet_length += ip_header_length;
	}

	if (G_UNLIKELY(skb->len < packet_length)) {	/* redundant: often handled in kernel */
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_BOUNDS,
			     _("IP packet received at %" G_GUINT16_FORMAT " bytes whilst IP header reports %" G_GSIZE_FORMAT " bytes."),
			     skb->len, packet_length);
		return FALSE;
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
#if PGM_CHECK_IN_CKSUM
	const int sum = in_cksum (data, packet_length, 0);
	if (G_UNLIKELY(0 != sum)) {
		const int ip_sum = g_ntohs (ip->ip_sum);
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_CKSUM,
			     _("IP packet checksum mismatch, reported 0x%x whilst calculated 0x%x."),
			     ip_sum, sum);
		return FALSE;
	}
#endif

/* fragmentation offset, bit 0: 0, bit 1: do-not-fragment, bit 2: more-fragments */
	const guint offset = g_ntohs (ip->ip_off);
	if (G_UNLIKELY((offset & 0x1fff) != 0)) {
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_PROTO,
			     _("IP header reports packet fragmentation."));
		return FALSE;
	}

/* PGM payload, header looks as follows:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         Source Port           |       Destination Port        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Type     |    Options    |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Global Source ID                   ... |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ...    Global Source ID       |           TSDU Length         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Type specific data ...
 * +-+-+-+-+-+-+-+-+-+- ...
 */

	skb->pgm_header = (gpointer)( (guint8*)skb->data + ip_header_length );

/* advance DATA pointer to PGM packet */
	skb->data	= skb->pgm_header;
	skb->len       -= ip_header_length;
	return pgm_parse (skb, error);
}

gboolean
pgm_parse_udp_encap (
	struct pgm_sk_buff_t*	skb,		/* will be modified */
	pgm_error_t**		error
	)
{
	g_assert (NULL != skb);

	if (G_UNLIKELY(skb->len < sizeof(struct pgm_header))) {
		pgm_set_error (error,
			     PGM_PACKET_ERROR,
			     PGM_PACKET_ERROR_BOUNDS,
			     _("UDP payload too small for PGM packet at %" G_GUINT16_FORMAT " bytes, expecting at least %" G_GSIZE_FORMAT " bytes."),
			     skb->len, sizeof(struct pgm_header));
		return FALSE;
	}

/* DATA payload is PGM packet, no headers */
	skb->pgm_header = skb->data;
	return pgm_parse (skb, error);
}

/* will modify packet contents to calculate and check PGM checksum
 */
static
gboolean
pgm_parse (
	struct pgm_sk_buff_t* const	skb,		/* will be modified to calculate checksum */
	pgm_error_t**			error
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

/* pgm_checksum == 0 means no transmitted checksum */
	if (skb->pgm_header->pgm_checksum)
	{
		const int sum = skb->pgm_header->pgm_checksum;
		skb->pgm_header->pgm_checksum = 0;
		const int pgm_sum = pgm_csum_fold (pgm_csum_partial ((const char*)skb->pgm_header, skb->len, 0));
		skb->pgm_header->pgm_checksum = sum;
		if (G_UNLIKELY(pgm_sum != sum)) {
			pgm_set_error (error,
				     PGM_PACKET_ERROR,
				     PGM_PACKET_ERROR_CKSUM,
			     	     _("PGM packet checksum mismatch, reported 0x%x whilst calculated 0x%x."),
			     	     pgm_sum, sum);
			return FALSE;
		}
	} else {
		if (PGM_ODATA == skb->pgm_header->pgm_type ||
		    PGM_RDATA == skb->pgm_header->pgm_type)
		{
			pgm_set_error (error,
				     PGM_PACKET_ERROR,
				     PGM_PACKET_ERROR_PROTO,
			     	     _("PGM checksum missing whilst mandatory for %cDATA packets."),
				     PGM_ODATA == skb->pgm_header->pgm_type ? 'O' : 'R');
			return FALSE;
		}
		g_trace ("No PGM checksum :O");
	}

/* copy packets source transport identifier */
	memcpy (&skb->tsi.gsi, skb->pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	skb->tsi.sport = skb->pgm_header->pgm_sport;
	return TRUE;
}

gboolean
pgm_print_packet (
	gpointer	data,
	gsize		len
	)
{
/* pre-conditions */
	g_assert (NULL != data);
	g_assert (len > 0);

/* minimum size should be IP header plus PGM header */
	if (len < (sizeof(struct pgm_ip) + sizeof(struct pgm_header))) 
	{
		printf ("Packet size too small: %" G_GSIZE_FORMAT " bytes, expecting at least %" G_GSIZE_FORMAT " bytes.\n",
			len, sizeof(struct pgm_ip) + sizeof(struct pgm_header));
		return FALSE;
	}

/* decode IP header */
	const struct pgm_ip* ip = (struct pgm_ip*)data;
	if (ip->ip_v != 4) 				/* IP version, 4 or 6 */
	{
		puts ("not IP4 packet :/");		/* v6 not currently handled */
		return FALSE;
	}
	printf ("IP ");

	const gsize ip_header_length = ip->ip_hl * 4;		/* IP header length in 32bit octets */
	if (ip_header_length < sizeof(struct pgm_ip)) 
	{
		puts ("bad IP header length :(");
		return FALSE;
	}

	gsize packet_length = g_ntohs(ip->ip_len);	/* total packet length */

/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	if (len == packet_length + ip_header_length) {
		packet_length += ip_header_length;
	}

	if (len < packet_length) {				/* redundant: often handled in kernel */
		puts ("truncated IP packet");
		return FALSE;
	}

/* TCP Segmentation Offload (TSO) might have zero length here */
	if (packet_length < ip_header_length) {
		puts ("bad length :(");
		return FALSE;
	}

	const guint offset = g_ntohs(ip->ip_off);

/* 3 bits routing priority, 4 bits type of service: delay, throughput, reliability, cost */
	printf ("(tos 0x%x", (int)ip->ip_tos);
	switch (ip->ip_tos & 0x3)
	{
	case 1: printf (",ECT(1)"); break;
	case 2: printf (",ECT(0)"); break;
	case 3: printf (",CE"); break;
	default: break;
	}

/* time to live */
	if (ip->ip_ttl >= 1) printf (", ttl %u", ip->ip_ttl);

/* fragmentation */
#define IP_RDF	0x8000
#define IP_DF	0x4000
#define IP_MF	0x2000
#define IP_OFFMASK	0x1fff

	printf (", id %u, offset %u, flags [%s%s]",
		g_ntohs(ip->ip_id),
		(offset & 0x1fff) * 8,
		((offset & IP_DF) ? "DF" : ""),
		((offset & IP_MF) ? "+" : ""));
	printf (", length %" G_GSIZE_FORMAT, packet_length);

/* IP options */
	if ((ip_header_length - sizeof(struct pgm_ip)) > 0) {
		printf (", options (");
		pgm_ipopt_print((gconstpointer)(ip + 1), ip_header_length - sizeof(struct pgm_ip));
		printf (" )");
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
	const int ip_sum = pgm_inet_checksum(data, packet_length, 0);
	if (ip_sum != 0) {
		const int encoded_ip_sum = g_ntohs(ip->ip_sum);
		printf (", bad cksum! %i", encoded_ip_sum);
	}

	printf (") ");

/* fragmentation offset, bit 0: 0, bit 1: do-not-fragment, bit 2: more-fragments */
	if ((offset & 0x1fff) != 0) {
		puts ("fragmented packet :/");
		return FALSE;
	}

/* PGM payload, header looks as follows:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         Source Port           |       Destination Port        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Type     |    Options    |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Global Source ID                   ... |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ...    Global Source ID       |           TSDU Length         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Type specific data ...
 * +-+-+-+-+-+-+-+-+-+- ...
 */
	struct pgm_header* pgm_header = (struct pgm_header*)((guint8*)data + ip_header_length);
	const gsize pgm_length = packet_length - ip_header_length;

	if (pgm_length < sizeof(pgm_header)) {
		puts ("bad packet size :(");
		return FALSE;
	}

	printf ("%s.%s > ",
		pgm_gethostbyaddr((const struct in_addr*)&ip->ip_src), pgm_udpport_string(pgm_header->pgm_sport));
	printf ("%s.%s: PGM\n",
		pgm_gethostbyaddr((const struct in_addr*)&ip->ip_dst), pgm_udpport_string(pgm_header->pgm_dport));

	printf ("type: %s [%i] (version=%i, reserved=%i)\n"
		"options: extensions=%s, network-significant=%s, parity packet=%s (variable size=%s)\n"
		"global source id: %i.%i.%i.%i.%i.%i\n"
		"tsdu length: %i\n",

		/* packet type */		/* packet version */			/* reserved = 0x0 */
		pgm_type_string(pgm_header->pgm_type & 0xf),
		(pgm_header->pgm_type & 0xf),	((pgm_header->pgm_type & 0xc0) >> 6),	((pgm_header->pgm_type & 0x30) >> 4),

/* bit 0 set => one or more option extensions are present */
		((pgm_header->pgm_options & (0x1 << 7)) ? "true" : "false"),
/* bit 1 set => one or more options are network-significant */
			((pgm_header->pgm_options & (0x1 << 6)) ? "true" : "false"),
/* bit 7 set => parity packet (OPT_PARITY) */
			((pgm_header->pgm_options & (0x1 << 0)) ? "true" : "false"),
/* bit 6 set => parity packet for variable packet sizes  (OPT_VAR_PKTLEN) */
			((pgm_header->pgm_options & (0x1 << 1)) ? "true" : "false"),

		pgm_header->pgm_gsi[0], pgm_header->pgm_gsi[1], pgm_header->pgm_gsi[2], pgm_header->pgm_gsi[3], pgm_header->pgm_gsi[4], pgm_header->pgm_gsi[5],
		g_ntohs(pgm_header->pgm_tsdu_length));

	if (pgm_header->pgm_checksum)
	{
		const int encoded_pgm_sum = pgm_header->pgm_checksum;
/* requires modification of data buffer */
		pgm_header->pgm_checksum = 0;
		const int pgm_sum = pgm_csum_fold (pgm_csum_partial((const char*)pgm_header, pgm_length, 0));
		if (pgm_sum != encoded_pgm_sum) {
			printf ("PGM checksum incorrect, packet %x calculated %x  :(\n", encoded_pgm_sum, pgm_sum);
			return FALSE;
		}
	} else {
		puts ("No PGM checksum :O");
	}

/* now decode PGM packet types */
	gconstpointer pgm_data = pgm_header + 1;
	const gsize pgm_data_length = pgm_length - sizeof(pgm_header);		/* can equal zero for SPMR's */

	gboolean err = FALSE;
	switch (pgm_header->pgm_type) {
	case PGM_SPM:	err = pgm_print_spm (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_POLL:	err = pgm_print_poll (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_POLR:	err = pgm_print_polr (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_ODATA:	err = pgm_print_odata (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_RDATA:	err = pgm_print_rdata (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_NAK:	err = pgm_print_nak (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_NNAK:	err = pgm_print_nnak (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_NCF:	err = pgm_print_ncf (pgm_header, pgm_data, pgm_data_length); break;
	case PGM_SPMR:	err = pgm_print_spmr (pgm_header, pgm_data, pgm_data_length); break;
	default:	puts ("unknown packet type :("); break;
	}

	return err;
}

/* 8.1.  Source Path Messages (SPM)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     SPM's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Trailing Edge Sequence Number                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Leading Edge Sequence Number                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Path NLA                     ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * NLA = Network Layer Address
 * NLA AFI = NLA Address Family Indicator: rfc 1700 (ADDRESS FAMILY NUMBERS)
 * => Path NLA = IP address of last network element
 */

#define PGM_MIN_SPM_SIZE	( sizeof(struct pgm_spm) )

gboolean
pgm_verify_spm (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	const struct pgm_spm* spm = (const struct pgm_spm*)skb->data;
	switch (g_ntohs (spm->spm_nla_afi)) {
/* truncated packet */
	case AFI_IP6:
		if (G_UNLIKELY(skb->len < sizeof(struct pgm_spm6)))
			return FALSE;
		break;
	case AFI_IP:
		if (G_UNLIKELY(skb->len < sizeof(struct pgm_spm)))
			return FALSE;
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

static
gboolean
pgm_print_spm (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("SPM: ");

	if (len < PGM_MIN_SPM_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_spm * spm  = (const struct pgm_spm *)data;
	const struct pgm_spm6* spm6 = (const struct pgm_spm6*)data;
	const guint spm_nla_afi = g_ntohs (spm->spm_nla_afi);

	printf ("sqn %lu trail %lu lead %lu nla-afi %u ",
		(gulong)g_ntohl(spm->spm_sqn),
		(gulong)g_ntohl(spm->spm_trail),
		(gulong)g_ntohl(spm->spm_lead),
		spm_nla_afi);	/* address family indicator */

	char s[INET6_ADDRSTRLEN];
	gconstpointer pgm_opt;
	gsize pgm_opt_len;
	switch (spm_nla_afi) {
	case AFI_IP:
		pgm_inet_ntop ( AF_INET, &spm->spm_nla, s, sizeof (s) );
		pgm_opt = (const guint8*)data + sizeof( struct pgm_spm );
		pgm_opt_len = len - sizeof( struct pgm_spm );
		break;

	case AFI_IP6:
		if (len < sizeof (struct pgm_spm6)) {
			puts ("packet truncated :(");
			return FALSE;
		}

		pgm_inet_ntop ( AF_INET6, &spm6->spm6_nla, s, sizeof(s) );
		pgm_opt = (const guint8*)data + sizeof(struct pgm_spm6);
		pgm_opt_len = len - sizeof(struct pgm_spm6);
		break;

	default:
		printf ("unsupported afi");
		return FALSE;
	}

	printf ("%s", s);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
	    pgm_print_options (pgm_opt, pgm_opt_len) < 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* 14.7.1.  Poll Request
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    POLL's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         POLL's Round          |       POLL's Sub-type         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Path NLA                     ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * |                  POLL's  Back-off Interval                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Random String                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Matching Bit-Mask                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Sent to ODATA multicast group with IP Router Alert option.
 */

#define PGM_MIN_POLL_SIZE	( sizeof(struct pgm_poll) )

gboolean
pgm_verify_poll (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	const struct pgm_poll* poll4 = (const struct pgm_poll*)skb->data;
	switch (g_ntohs (poll4->poll_nla_afi)) {
/* truncated packet */
	case AFI_IP6:
		if (G_UNLIKELY(skb->len < sizeof(struct pgm_poll6)))
			return FALSE;
		break;
	case AFI_IP:
		if (G_UNLIKELY(skb->len < sizeof(struct pgm_poll)))
			return FALSE;
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

static
gboolean
pgm_print_poll (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("POLL: ");

	if (len < PGM_MIN_POLL_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_poll * poll4 = (const struct pgm_poll *)data;
	const struct pgm_poll6* poll6 = (const struct pgm_poll6*)data;
	const guint poll_nla_afi = g_ntohs (poll4->poll_nla_afi);

	printf ("sqn %lu round %u sub-type %u nla-afi %u ",
		(gulong)g_ntohl(poll4->poll_sqn),
		g_ntohs(poll4->poll_round),
		g_ntohs(poll4->poll_s_type),
		poll_nla_afi);	/* address family indicator */

	char s[INET6_ADDRSTRLEN];
	gconstpointer pgm_opt;
	gsize pgm_opt_len;
	switch (poll_nla_afi) {
	case AFI_IP:
		pgm_inet_ntop ( AF_INET, &poll4->poll_nla, s, sizeof (s) );
		pgm_opt = (const guint8*)data + sizeof(struct pgm_poll);
		pgm_opt_len = len - sizeof(struct pgm_poll);
		printf ("%s", s);

/* back-off interval in microseconds */
		printf (" bo_ivl %u", poll4->poll_bo_ivl);

/* random string */
		printf (" rand [%c%c%c%c]",
			isprint (poll4->poll_rand[0]) ? poll4->poll_rand[0] : '.',
			isprint (poll4->poll_rand[1]) ? poll4->poll_rand[1] : '.',
			isprint (poll4->poll_rand[2]) ? poll4->poll_rand[2] : '.',
			isprint (poll4->poll_rand[3]) ? poll4->poll_rand[3] : '.' );

/* matching bit-mask */
		printf (" mask 0x%x", poll4->poll_mask);
		break;

	case AFI_IP6:
		if (len < sizeof (struct pgm_poll6)) {
			puts ("packet truncated :(");
			return FALSE;
		}

		pgm_inet_ntop ( AF_INET6, &poll6->poll6_nla, s, sizeof (s) );
		pgm_opt = (const guint8*)data + sizeof(struct pgm_poll6);
		pgm_opt_len = len - sizeof(struct pgm_poll6);
		printf ("%s", s);

/* back-off interval in microseconds */
		printf (" bo_ivl %u", poll6->poll6_bo_ivl);

/* random string */
		printf (" rand [%c%c%c%c]",
			isprint (poll6->poll6_rand[0]) ? poll6->poll6_rand[0] : '.',
			isprint (poll6->poll6_rand[1]) ? poll6->poll6_rand[1] : '.',
			isprint (poll6->poll6_rand[2]) ? poll6->poll6_rand[2] : '.',
			isprint (poll6->poll6_rand[3]) ? poll6->poll6_rand[3] : '.' );

/* matching bit-mask */
		printf (" mask 0x%x", poll6->poll6_mask);
		break;

	default:
		printf ("unsupported afi");
		return FALSE;
	}


/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
	    pgm_print_options (pgm_opt, pgm_opt_len) < 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* 14.7.2.  Poll Response
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    POLR's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         POLR's Round          |           reserved            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

gboolean
pgm_verify_polr (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

/* truncated packet */
	if (G_UNLIKELY(skb->len < sizeof(struct pgm_polr)))
		return FALSE;
	return TRUE;
}

static
gboolean
pgm_print_polr (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("POLR: ");

	if (len < sizeof(struct pgm_polr)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_polr* polr = (const struct pgm_polr*)data;

	printf("sqn %lu round %u",
		(gulong)g_ntohl(polr->polr_sqn),
		g_ntohs(polr->polr_round));

	gconstpointer pgm_opt = (const guint8*)data + sizeof(struct pgm_polr);
	gsize pgm_opt_len = len - sizeof(struct pgm_polr);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
	    pgm_print_options (pgm_opt, pgm_opt_len) < 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* 8.2.  Data Packet
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Data Packet Sequence Number                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Trailing Edge Sequence Number                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Data ...
 * +-+-+- ...
 */

static
gboolean
pgm_print_odata (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("ODATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_data* odata = (const struct pgm_data*)data;

	printf ("sqn %lu trail %lu [",
		(gulong)g_ntohl(odata->data_sqn),
		(gulong)g_ntohl(odata->data_trail));

/* option extensions */
	gconstpointer pgm_opt = (const guint8*)data + sizeof(struct pgm_data);
	gsize pgm_opt_len = len - sizeof(struct pgm_data);
	const char* payload = pgm_opt;

	if (header->pgm_options & PGM_OPT_PRESENT) {
		const gssize opt_len = pgm_print_options (pgm_opt, pgm_opt_len);
		if (opt_len < 0)
			return FALSE;
		payload	+= opt_len;
	}

/* data */
	const char* end = payload + g_ntohs (header->pgm_tsdu_length);
	while (payload < end) {
		if (isprint(*payload))
			putchar(*payload);
		else
			putchar('.');
		payload++;
	}

	printf ("]\n");
	return TRUE;
}

/* 8.2.  Repair Data
 */

static
gboolean
pgm_print_rdata (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("RDATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_data* rdata = (const struct pgm_data*)data;

	printf ("sqn %lu trail %lu [",
		(gulong)g_ntohl(rdata->data_sqn),
		(gulong)g_ntohl(rdata->data_trail));

/* option extensions */
	gconstpointer pgm_opt = (const guint8*)data + sizeof(struct pgm_data);
	gsize pgm_opt_len = len - sizeof(struct pgm_data);
	const char* payload = pgm_opt;

	if (header->pgm_options & PGM_OPT_PRESENT) {
		const gssize opt_len = pgm_print_options (pgm_opt, pgm_opt_len);
		if (opt_len < 0)
			return FALSE;
		payload	+= opt_len;
	}

/* data */
	const char* end = payload + g_ntohs (header->pgm_tsdu_length);
	while (payload < end) {
		if (isprint(*payload))
			putchar(*payload);
		else
			putchar('.');
		payload++;
	}

	printf ("]\n");
	return TRUE;
}

/* 8.3.  NAK
 *
 * Technically the AFI of the source and multicast group can be different
 * but that would be very wibbly wobbly.  One example is using a local DLR
 * with a IPv4 address to reduce NAK cost for recovery on wide IPv6
 * distribution.
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   Requested Sequence Number                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           Source NLA                    ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Multicast Group NLA                ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

#define PGM_MIN_NAK_SIZE	( sizeof(struct pgm_nak) )

gboolean
pgm_verify_nak (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	g_trace ("pgm_verify_nak (skb:%p)", (gconstpointer)skb);

/* truncated packet */
	if (G_UNLIKELY(skb->len < PGM_MIN_NAK_SIZE))
		return FALSE;

	const struct pgm_nak* nak = (struct pgm_nak*)skb->data;
	const int nak_src_nla_afi = g_ntohs (nak->nak_src_nla_afi);
	int nak_grp_nla_afi = -1;

/* check source NLA: unicast address of the ODATA sender */
	switch (nak_src_nla_afi) {
	case AFI_IP:
		nak_grp_nla_afi = g_ntohs (nak->nak_grp_nla_afi);
		break;

	case AFI_IP6:
		nak_grp_nla_afi = g_ntohs (((const struct pgm_nak6*)nak)->nak6_grp_nla_afi);
		break;

	default:
		return FALSE;
	}

/* check multicast group NLA */
	switch (nak_grp_nla_afi) {
	case AFI_IP6:
		switch (nak_src_nla_afi) {
/* IPv4 + IPv6 NLA */
		case AFI_IP:
			if (G_UNLIKELY(skb->len < ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) )))
				return FALSE;
			break;

/* IPv6 + IPv6 NLA */
		case AFI_IP6:
			if (G_UNLIKELY(skb->len < sizeof(struct pgm_nak6)))
				return FALSE;
			break;
		}

	case AFI_IP:
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

static
gboolean
pgm_print_nak (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("NAK: ");

	if (len < PGM_MIN_NAK_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_nak * nak  = (const struct pgm_nak *)data;
	const struct pgm_nak6* nak6 = (const struct pgm_nak6*)data;
	const guint nak_src_nla_afi = g_ntohs (nak->nak_src_nla_afi);

	printf ("sqn %lu src ", 
		(gulong)g_ntohl(nak->nak_sqn));

	char s[INET6_ADDRSTRLEN];
	gconstpointer pgm_opt;
	gsize pgm_opt_len;

/* source nla */
	switch (nak_src_nla_afi) {
	case AFI_IP: {
		const guint nak_grp_nla_afi = g_ntohs (nak->nak_grp_nla_afi);
		if (nak_src_nla_afi != nak_grp_nla_afi) {
			puts ("different source & group afi very wibbly wobbly :(");
			return FALSE;
		}

		pgm_inet_ntop ( AF_INET, &nak->nak_src_nla, s, sizeof(s) );
		pgm_opt = (const guint8*)data + sizeof(struct pgm_nak);
		pgm_opt_len = len - sizeof(struct pgm_nak);
		printf ("%s grp ", s);

		pgm_inet_ntop ( AF_INET, &nak->nak_grp_nla, s, sizeof(s) );
		printf ("%s", s);
		break;
	}

	case AFI_IP6: {
		if (len < sizeof (struct pgm_nak6)) {
			puts ("packet truncated :(");
			return FALSE;
		}

		const guint nak_grp_nla_afi = g_ntohs (nak6->nak6_grp_nla_afi);
		if (nak_src_nla_afi != nak_grp_nla_afi) {
			puts ("different source & group afi very wibbly wobbly :(");
			return FALSE;
		}

		pgm_inet_ntop ( AF_INET6, &nak6->nak6_src_nla, s, sizeof(s) );
		pgm_opt = (const guint8*)data + sizeof(struct pgm_nak6);
		pgm_opt_len = len - sizeof(struct pgm_nak6);
		printf ("%s grp ", s);

		pgm_inet_ntop ( AF_INET6, &nak6->nak6_grp_nla, s, sizeof(s) );
		printf ("%s", s);
		break;
	}

	default:
		puts ("unsupported afi");
		return FALSE;
	}


/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
	    pgm_print_options (pgm_opt, pgm_opt_len) < 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* 8.3.  N-NAK
 */

gboolean
pgm_verify_nnak (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	return pgm_verify_nak (skb);
}

static
gboolean
pgm_print_nnak (
	G_GNUC_UNUSED const struct pgm_header* const	header,
	G_GNUC_UNUSED gconstpointer			data,
	const gsize					len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("N-NAK: ");

	if (len < sizeof(struct pgm_nak)) {
		puts ("packet truncated :(");
		return FALSE;
	}

//	struct pgm_nak* nnak = (struct pgm_nak*)data;

	return TRUE;
}

/* 8.3.  NCF
 */

gboolean
pgm_verify_ncf (
	const struct pgm_sk_buff_t* const	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	return pgm_verify_nak (skb);
}

gboolean
pgm_print_ncf (
	G_GNUC_UNUSED const struct pgm_header* const	header,
	G_GNUC_UNUSED gconstpointer			data,
	const gsize					len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("NCF: ");

	if (len < sizeof(struct pgm_nak)) {
		puts ("packet truncated :(");
		return FALSE;
	}

//	struct pgm_nak* ncf = (struct pgm_nak*)data;

	return TRUE;
}

/* 13.6.  SPM Request
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

gboolean
pgm_verify_spmr (
	G_GNUC_UNUSED const struct pgm_sk_buff_t*	skb
	)
{
/* pre-conditions */
	g_assert (NULL != skb);

	return TRUE;
}

static
gboolean
pgm_print_spmr (
	const struct pgm_header* const	header,
	gconstpointer			data,
	const gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != header);
	g_assert (NULL != data);
	g_assert (len > 0);

	printf ("SPMR: ");

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
	    pgm_print_options (data, len) < 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* Parse PGM options fields, alters contents of packet.
 * 
 * returns -1 on failure, or total length in octets of the option fields
 */

static
gssize
pgm_print_options (
	gconstpointer		data,
	gsize			len
	)
{
/* pre-conditions */
	g_assert (NULL != data);
	g_assert (len > 0);

	printf (" OPTIONS:");
	if (len < sizeof(struct pgm_opt_length)) {
		puts (" packet truncated :(");
		return -1;
	}

	const struct pgm_opt_length* opt_len = (const struct pgm_opt_length*)data;
	if (opt_len->opt_length != sizeof(struct pgm_opt_length)) {
		printf (" bad opt_length length %u\n", (unsigned)opt_len->opt_length);
		return -1;
	}

	guint opt_total_length = g_ntohs (opt_len->opt_total_length);
	printf (" total len %u ", opt_total_length);
	if (opt_total_length < (sizeof(struct pgm_opt_length) + sizeof(struct pgm_opt_header)) ||
	    opt_total_length > len)
	{
		puts ("bad total length");
		return -1;
	}

/* total length includes opt_length option */
	opt_total_length -= sizeof(struct pgm_opt_length);
	const struct pgm_opt_header* opt_header = (const struct pgm_opt_header*)(opt_len + 1);

/* iterate through options (max 16) */
	int count = 16;
	while (opt_total_length && count)
	{
		if (opt_total_length < sizeof(struct pgm_opt_header) ||
		    opt_header->opt_length > opt_total_length)
		{
			puts ("short on option data :o");
			return -1;
		}

		if (opt_header->opt_type & PGM_OPT_END) {
			printf ("OPT_END+");
		}

		switch (opt_header->opt_type & PGM_OPT_MASK) {
		case PGM_OPT_SYN:
			printf ("OPT_SYN ");
			break;

		case PGM_OPT_FIN:
			printf ("OPT_FIN ");
			break;

		case PGM_OPT_RST:
			printf ("OPT_RST ");
			break;

		case PGM_OPT_PARITY_PRM:
			printf ("OPT_PARITY_PRM ");
			break;

		case PGM_OPT_CURR_TGSIZE:
			printf ("OPT_CURR_TGSIZE ");
			break;

		default:
			printf ("OPT-%u{%u} ", opt_header->opt_type & PGM_OPT_MASK, opt_header->opt_length);
			break;
		}

		opt_total_length -= opt_header->opt_length;
		opt_header = (const struct pgm_opt_header*)((const char*)opt_header + opt_header->opt_length);

		count--;
	}

	if (!count) {
		puts ("too many options found");
		return -1;
	}

	return ((const guint8*)opt_header - (const guint8*)data);
}

const char*
pgm_type_string (
	guint8		type
	)
{
	const char* c;

	switch (type) {
	case PGM_SPM:		c = "PGM_SPM"; break;
	case PGM_POLL:		c = "PGM_POLL"; break;
	case PGM_POLR:		c = "PGM_POLR"; break;
	case PGM_ODATA:		c = "PGM_ODATA"; break;
	case PGM_RDATA:		c = "PGM_RDATA"; break;
	case PGM_NAK:		c = "PGM_NAK"; break;
	case PGM_NNAK:		c = "PGM_NNAK"; break;
	case PGM_NCF:		c = "PGM_NCF"; break;
	case PGM_SPMR:		c = "PGM_SPMR"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

const char*
pgm_udpport_string (
	int		port
	)
{
	static pgm_hashtable_t *services = NULL;

	if (!services) {
		services = pgm_hash_table_new (g_int_hash, g_int_equal);
	}

	gpointer service_string = pgm_hash_table_lookup (services, &port);
	if (service_string != NULL) {
		return service_string;
	}

	struct servent* se = getservbyport (port, "udp");
	if (se == NULL) {
		char buf[sizeof("00000")];
		snprintf(buf, sizeof(buf), "%i", g_ntohs(port));
		service_string = pgm_strdup(buf);
	} else {
		service_string = pgm_strdup(se->s_name);
	}
	pgm_hash_table_insert (services, &port, service_string);
	return service_string;
}

const char*
pgm_gethostbyaddr (
	const struct in_addr*	ap
	)
{
	static pgm_hashtable_t *hosts = NULL;

	if (!hosts) {
		hosts = pgm_hash_table_new (g_str_hash, g_str_equal);
	}

	gpointer host_string = pgm_hash_table_lookup (hosts, ap);
	if (host_string != NULL) {
		return host_string;
	}

	struct hostent* he = gethostbyaddr((const char*)ap, sizeof(struct in_addr), AF_INET);
	if (he == NULL) {
		struct in_addr in;
		memcpy (&in, ap, sizeof(in));
		host_string = pgm_strdup(inet_ntoa(in));
	} else {
		host_string = pgm_strdup(he->h_name);
	}
	pgm_hash_table_insert (hosts, ap, host_string);
	return host_string;
}

void
pgm_ipopt_print (
	gconstpointer		ipopt,
	gsize			length
	)
{
/* pre-conditions */
	g_assert (NULL != ipopt);

	const char* op = ipopt;

	while (length)
	{
		char len = (*op == IPOPT_NOP || *op == IPOPT_EOL) ? 1 : op[1];
		switch (*op) {
		case IPOPT_EOL:		printf(" eol"); break;
		case IPOPT_NOP:		printf(" nop"); break;
		case IPOPT_RR:		printf(" rr"); break;	/* 1 route */
		case IPOPT_TS:		printf(" ts"); break;	/* 1 TS */
#if 0
		case IPOPT_SECURITY:	printf(" sec-level"); break;
		case IPOPT_LSRR:	printf(" lsrr"); break;	/* 1 route */
		case IPOPT_SATID:	printf(" satid"); break;
		case IPOPT_SSRR:	printf(" ssrr"); break;	/* 1 route */
#endif
		default:		printf(" %x{%d}", (int)*op, (int)len); break;
		}

		if (!len) {
			puts ("invalid IP opt length");
			return;
		}

		op += len;
		length -= len;
	}
}

/* eof */
