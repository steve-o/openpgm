/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM packet formats, RFC 3208.
 *
 * Copyright (c) 2006-2011 Miru Limited.
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

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/packet_parse.h>


//#define PACKET_DEBUG

#ifndef PACKET_DEBUG
#	define PGM_DISABLE_ASSERT
#endif


/* locals */

static bool pgm_parse (struct pgm_sk_buff_t*const restrict, pgm_error_t**restrict);


/* Parse a raw-IP packet for IP and PGM header and any payload.
 */

#define PGM_MIN_SIZE	( \
				sizeof(struct pgm_ip) + 	/* IPv4 header */ \
				sizeof(struct pgm_header) 	/* PGM header */ \
			)

PGM_GNUC_INTERNAL
bool
pgm_parse_raw (
	struct pgm_sk_buff_t* const restrict skb,	/* data will be modified */
	struct sockaddr*      const restrict dst,
	pgm_error_t**		    restrict error
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);
	pgm_assert (NULL != dst);

	pgm_debug ("pgm_parse_raw (skb:%p dst:%p error:%p)",
		(const void*)skb, (const void*)dst, (const void*)error);

/* minimum size should be IPv4 header plus PGM header, check IP version later */
	if (PGM_UNLIKELY(skb->len < PGM_MIN_SIZE))
	{
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_BOUNDS,
			     _("IP packet too small at %" PRIu16 " bytes, expecting at least %" PRIu16 " bytes."),
			     skb->len, (uint16_t)PGM_MIN_SIZE);
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
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_AFNOSUPPORT,
			     _("IPv6 is not supported for raw IP header parsing."));
		return FALSE;

	default:
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_AFNOSUPPORT,
			     _("IP header reports an invalid version %d."),
			     ip->ip_v);
		return FALSE;
	}

	const size_t ip_header_length = ip->ip_hl * 4;		/* IP header length in 32bit octets */
	if (PGM_UNLIKELY(ip_header_length < sizeof(struct pgm_ip))) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_BOUNDS,
			     _("IP header reports an invalid header length %" PRIzu " bytes."),
			     ip_header_length);
		return FALSE;
	}

#ifndef HAVE_HOST_ORDER_IP_LEN
	size_t packet_length = ntohs (ip->ip_len);	/* total packet length */
#else
	size_t packet_length = ip->ip_len;		/* total packet length */
#endif


/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	if (skb->len == packet_length + ip_header_length) {
		packet_length += ip_header_length;
	}

	if (PGM_UNLIKELY(skb->len < packet_length)) {	/* redundant: often handled in kernel */
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_BOUNDS,
			     _("IP packet received at %" PRIu16 " bytes whilst IP header reports %" PRIzu " bytes."),
			     skb->len, packet_length);
		return FALSE;
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
#ifdef PGM_CHECK_IN_CKSUM
	const uint16_t sum = in_cksum (data, packet_length, 0);
	if (PGM_UNLIKELY(0 != sum)) {
		const uint16_t ip_sum = ntohs (ip->ip_sum);
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_CKSUM,
			     _("IP packet checksum mismatch, reported 0x%x whilst calculated 0x%x."),
			     ip_sum, sum);
		return FALSE;
	}
#endif

/* fragmentation offset, bit 0: 0, bit 1: do-not-fragment, bit 2: more-fragments */
#ifndef HAVE_HOST_ORDER_IP_OFF
	const uint16_t offset = ntohs (ip->ip_off);
#else
	const uint16_t offset = ip->ip_off;
#endif
	if (PGM_UNLIKELY((offset & 0x1fff) != 0)) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_PROTO,
			     _("IP header reports packet fragmentation, offset %u."),
			     offset & 0x1fff);
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

	skb->pgm_header = (void*)( (char*)skb->data + ip_header_length );

/* advance DATA pointer to PGM packet */
	skb->data	= skb->pgm_header;
	skb->len       -= ip_header_length;
	return pgm_parse (skb, error);
}

PGM_GNUC_INTERNAL
bool
pgm_parse_udp_encap (
	struct pgm_sk_buff_t*const restrict skb,		/* will be modified */
	pgm_error_t**	      restrict error
	)
{
	pgm_assert (NULL != skb);

	if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_header))) {
		pgm_set_error (error,
			     PGM_ERROR_DOMAIN_PACKET,
			     PGM_ERROR_BOUNDS,
			     _("UDP payload too small for PGM packet at %" PRIu16 " bytes, expecting at least %" PRIzu " bytes."),
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
bool
pgm_parse (
	struct pgm_sk_buff_t*const restrict skb,		/* will be modified to calculate checksum */
	pgm_error_t**		    restrict error
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

/* pgm_checksum == 0 means no transmitted checksum */
	if (skb->pgm_header->pgm_checksum)
	{
		const uint16_t sum = skb->pgm_header->pgm_checksum;
		skb->pgm_header->pgm_checksum = 0;
		const uint16_t pgm_sum = pgm_csum_fold (pgm_csum_partial ((const char*)skb->pgm_header, skb->len, 0));
		skb->pgm_header->pgm_checksum = sum;
		if (PGM_UNLIKELY(pgm_sum != sum)) {
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_PACKET,
				     PGM_ERROR_CKSUM,
			     	     _("PGM packet checksum mismatch, reported 0x%x whilst calculated 0x%x."),
			     	     pgm_sum, sum);
			return FALSE;
		}
	} else {
		if (PGM_ODATA == skb->pgm_header->pgm_type ||
		    PGM_RDATA == skb->pgm_header->pgm_type)
		{
			pgm_set_error (error,
				     PGM_ERROR_DOMAIN_PACKET,
				     PGM_ERROR_PROTO,
			     	     _("PGM checksum missing whilst mandatory for %cDATA packets."),
				     PGM_ODATA == skb->pgm_header->pgm_type ? 'O' : 'R');
			return FALSE;
		}
		pgm_debug ("No PGM checksum :O");
	}

/* copy packets source transport identifier */
	memcpy (&skb->tsi.gsi, skb->pgm_header->pgm_gsi, sizeof(pgm_gsi_t));
	skb->tsi.sport = skb->pgm_header->pgm_sport;
	return TRUE;
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

PGM_GNUC_INTERNAL
bool
pgm_verify_spm (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	const struct pgm_spm* spm = (const struct pgm_spm*)skb->data;
	switch (ntohs (spm->spm_nla_afi)) {
/* truncated packet */
	case AFI_IP6:
		if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_spm6)))
			return FALSE;
		break;
	case AFI_IP:
		if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_spm)))
			return FALSE;
		break;

	default:
		return FALSE;
	}

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

PGM_GNUC_INTERNAL
bool
pgm_verify_poll (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	const struct pgm_poll* poll4 = (const struct pgm_poll*)skb->data;
	switch (ntohs (poll4->poll_nla_afi)) {
/* truncated packet */
	case AFI_IP6:
		if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_poll6)))
			return FALSE;
		break;
	case AFI_IP:
		if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_poll)))
			return FALSE;
		break;

	default:
		return FALSE;
	}

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

PGM_GNUC_INTERNAL
bool
pgm_verify_polr (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

/* truncated packet */
	if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_polr)))
		return FALSE;
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

/* no verification api */

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

PGM_GNUC_INTERNAL
bool
pgm_verify_nak (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	pgm_debug ("pgm_verify_nak (skb:%p)", (const void*)skb);

/* truncated packet */
	if (PGM_UNLIKELY(skb->len < PGM_MIN_NAK_SIZE))
		return FALSE;

	const struct pgm_nak* nak = (struct pgm_nak*)skb->data;
	const uint16_t nak_src_nla_afi = ntohs (nak->nak_src_nla_afi);
	uint16_t nak_grp_nla_afi = 0;

/* check source NLA: unicast address of the ODATA sender */
	switch (nak_src_nla_afi) {
	case AFI_IP:
		nak_grp_nla_afi = ntohs (nak->nak_grp_nla_afi);
		break;

	case AFI_IP6:
		nak_grp_nla_afi = ntohs (((const struct pgm_nak6*)nak)->nak6_grp_nla_afi);
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
			if (PGM_UNLIKELY(skb->len < ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) )))
				return FALSE;
			break;

/* IPv6 + IPv6 NLA */
		case AFI_IP6:
			if (PGM_UNLIKELY(skb->len < sizeof(struct pgm_nak6)))
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

/* 8.3.  N-NAK
 */

PGM_GNUC_INTERNAL
bool
pgm_verify_nnak (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	return pgm_verify_nak (skb);
}

/* 8.3.  NCF
 */

PGM_GNUC_INTERNAL
bool
pgm_verify_ncf (
	const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	return pgm_verify_nak (skb);
}

/* 13.6.  SPM Request
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

PGM_GNUC_INTERNAL
bool
pgm_verify_spmr (
	PGM_GNUC_UNUSED const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	return TRUE;
}

/* PGMCC: ACK
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            RX_MAX                             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Received Packet Bitmap                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

#define PGM_MIN_ACK_SIZE	( sizeof(struct pgm_ack) )

PGM_GNUC_INTERNAL
bool
pgm_verify_ack (
	PGM_GNUC_UNUSED const struct pgm_sk_buff_t*const	skb
	)
{
/* pre-conditions */
	pgm_assert (NULL != skb);

	return TRUE;
}

/* eof */
