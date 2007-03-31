/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * PGM packet formats, RFC 3208.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm.h"


/* globals */

static gboolean pgm_print_spm (struct pgm_header*, char*, int);
static gboolean pgm_print_poll (struct pgm_header*, char*, int);
static gboolean pgm_print_polr (struct pgm_header*, char*, int);
static gboolean pgm_print_odata (struct pgm_header*, char*, int);
static gboolean pgm_print_rdata (struct pgm_header*, char*, int);
static gboolean pgm_print_nak (struct pgm_header*, char*, int);
static gboolean pgm_print_nnak (struct pgm_header*, char*, int);
static gboolean pgm_print_ncf (struct pgm_header*, char*, int);
static gboolean pgm_print_spmr (struct pgm_header*, char*, int);
static int pgm_print_options (char*, int);

static const char *pgm_packet_type (guint8);


int
pgm_parse_packet (
	char*	data,
	int	len,
	struct pgm_header** header,
	char**	packet,
	int*	packet_len
	)
{
/* minimum size should be IP header plus PGM header */
	if (len < (sizeof(struct iphdr) + sizeof(struct pgm_header))) 
	{
		printf ("Packet size too small: %i bytes, expecting at least %u bytes.\n", len, sizeof(struct pgm_header));
		return -1;
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
	const struct iphdr* ip = (struct iphdr*)data;
	if (ip->version != 4 || ip->version != 6) {	/* IP version, 4 or 6 */
		puts ("unknown IP version :/");	
		return -1;
	}

	guint ip_header_length = ip->ihl * 4;		/* IP header length in 32bit octets */
	if (ip_header_length < sizeof(struct iphdr)) {
		puts ("bad IP header length :(");
		return -1;
	}

/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	int packet_length = g_ntohs(ip->tot_len);	/* total packet length */
	if (len < packet_length) {			/* redundant: often handled in kernel */
		puts ("truncated IP packet");
		return -1;
	}

/* TCP Segmentation Offload (TSO) might have zero length here */
	if (packet_length < ip_header_length) {
		puts ("bad length :(");
		return -1;
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
	int sum = in_cksum(data, packet_length, 0);
	if (sum != 0) {
		int ip_sum = g_ntohs(ip->check);
		printf ("bad cksum! %i\n", ip_sum);
		return -2;
	}

/* fragmentation offset, bit 0: 0, bit 1: do-not-fragment, bit 2: more-fragments */
	int offset = g_ntohs(ip->frag_off);
	if ((offset & 0x1fff) != 0) {
		puts ("fragmented packet :/");
		return -1;
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
	struct pgm_header* pgm_header = (struct pgm_header*)((char*)data + ip_header_length);
	int pgm_length = packet_length - ip_header_length;

	if (pgm_length < sizeof(pgm_header)) {
		puts ("bad packet size :(");
		return -1;
	}

	if (pgm_header->pgm_checksum)
	{
		sum = pgm_header->pgm_checksum;
		pgm_header->pgm_checksum = 0;
		int pgm_sum = pgm_cksum((const char*)pgm_header, pgm_length, 0);
		if (pgm_sum != sum) {
			puts ("PGM checksum bad :(");
			return -2;
		}
	} else {
		puts ("No PGM checksum :O");
	}

/* now decode PGM packet types */
	char* pgm_data = (char*)(pgm_header + 1);
	pgm_length -= sizeof(pgm_header);		/* can equal zero for SPMR's */

	if (pgm_length < 0) {
		puts ("bad packet length :(");
		return -1;
	}

	*header = pgm_header;
	*packet = pgm_data;
	*packet_len = pgm_length;

	return 0;
}

gboolean
pgm_print_packet (
	char* data,
	int len
	)
{
/* minimum size should be IP header plus PGM header */
	if (len < (sizeof(struct iphdr) + sizeof(struct pgm_header))) 
	{
		printf ("Packet size too small: %i bytes, expecting at least %u bytes.\n", len, sizeof(struct pgm_header));
		return FALSE;
	}

/* decode IP header */
	const struct iphdr* ip = (struct iphdr*)data;
	if (ip->version != 4) {				/* IP version, 4 or 6 */
		puts ("not IP4 packet :/");		/* v6 not currently handled */
		return FALSE;
	}
	printf ("IP ");

	guint ip_header_length = ip->ihl * 4;		/* IP header length in 32bit octets */
	if (ip_header_length < sizeof(struct iphdr)) {
		puts ("bad IP header length :(");
		return FALSE;
	}

/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	int packet_length = g_ntohs(ip->tot_len);	/* total packet length */
	if (len < packet_length) {				/* redundant: often handled in kernel */
		puts ("truncated IP packet");
		return FALSE;
	}

/* TCP Segmentation Offload (TSO) might have zero length here */
	if (packet_length < ip_header_length) {
		puts ("bad length :(");
		return FALSE;
	}

	int offset = g_ntohs(ip->frag_off);

/* 3 bits routing priority, 4 bits type of service: delay, throughput, reliability, cost */
	printf ("(tos 0x%x", (int)ip->tos);
	if (ip->tos & 0x3) {
		switch (ip->tos & 0x3) {
		case 1: printf (",ECT(1)"); break;
		case 2: printf (",ECT(0)"); break;
		case 3: printf (",CE"); break;
		}
	}

/* time to live */
	if (ip->ttl >= 1) {
		printf (", ttl %u", ip->ttl);
	}

/* fragmentation */
#define IP_RDF	0x8000
#define IP_DF	0x4000
#define IP_MF	0x2000
#define IP_OFFMASK	0x1fff

	printf (", id %u, offset %u, flags [%s%s]",
		g_ntohs(ip->id),
		(offset & 0x1fff) * 8,
		((offset & IP_DF) ? "DF" : ""),
		((offset & IP_MF) ? "+" : ""));
	printf (", length %u", packet_length);

/* IP options */
	if ((ip_header_length - sizeof(struct iphdr)) > 0) {
		printf (", options (");
		ip_optprint((const char*)(ip + 1), ip_header_length - sizeof(struct iphdr));
		printf (" )");
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
	int sum = in_cksum(data, packet_length, 0);
	if (sum != 0) {
		int ip_sum = g_ntohs(ip->check);
		printf (", bad cksum! %i", ip_sum);
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
	struct pgm_header* pgm_header = (struct pgm_header*)((char*)data + ip_header_length);
	int pgm_length = packet_length - ip_header_length;

	if (pgm_length < sizeof(pgm_header)) {
		puts ("bad packet size :(");
		return FALSE;
	}

	printf ("%s.%s > %s.%s: PGM\n",
		getname((struct in_addr*)&ip->saddr), udpport_string(pgm_header->pgm_sport),
		getname((struct in_addr*)&ip->daddr), udpport_string(pgm_header->pgm_dport));

	printf ("type: %s [%i] (version=%i, reserved=%i)\n"
		"options: extensions=%s, network-significant=%s, parity packet=%s (variable size=%s)\n"
		"global source id: %i.%i.%i.%i.%i.%i\n"
		"tsdu length: %i\n",

		/* packet type */		/* packet version */			/* reserved = 0x0 */
		pgm_packet_type(pgm_header->pgm_type & 0xf),
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
		sum = pgm_header->pgm_checksum;
		pgm_header->pgm_checksum = 0;
		int pgm_sum = pgm_cksum((const char*)pgm_header, pgm_length, 0);
		if (pgm_sum != sum) {
			puts ("PGM checksum bad :(");
			return FALSE;
		}
	} else {
		puts ("No PGM checksum :O");
	}

/* now decode PGM packet types */
	char* pgm_data = (char*)(pgm_header + 1);
	pgm_length -= sizeof(pgm_header);		/* can equal zero for SPMR's */

	if (pgm_length < 0) {
		puts ("bad packet length :(");
		return FALSE;
	}

	gboolean err = FALSE;
	switch (pgm_header->pgm_type) {
	case PGM_SPM:	err = pgm_print_spm (pgm_header, pgm_data, pgm_length); break;
	case PGM_POLL:	err = pgm_print_poll (pgm_header, pgm_data, pgm_length); break;
	case PGM_POLR:	err = pgm_print_polr (pgm_header, pgm_data, pgm_length); break;
	case PGM_ODATA:	err = pgm_print_odata (pgm_header, pgm_data, pgm_length); break;
	case PGM_RDATA:	err = pgm_print_rdata (pgm_header, pgm_data, pgm_length); break;
	case PGM_NAK:	err = pgm_print_nak (pgm_header, pgm_data, pgm_length); break;
	case PGM_NNAK:	err = pgm_print_nnak (pgm_header, pgm_data, pgm_length); break;
	case PGM_NCF:	err = pgm_print_ncf (pgm_header, pgm_data, pgm_length); break;
	case PGM_SPMR:	err = pgm_print_spmr (pgm_header, pgm_data, pgm_length); break;
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

#define PGM_MIN_SPM_SIZE	(sizeof(struct pgm_spm) + sizeof(struct in_addr))

gboolean
pgm_parse_spm (
	struct pgm_header* header,
	char* data,
	int len,
	struct in_addr* addr
	)
{
	if (len < PGM_MIN_SPM_SIZE) {
		puts ("packet truncated :(");
		return -1;
	}

	struct pgm_spm* spm = (struct pgm_spm*)data;

/* path nla */
	data += sizeof(struct pgm_spm);
	len -= sizeof(struct pgm_spm);

	switch (g_ntohs(spm->spm_nla_afi)) {
	case AFI_IP:
		*addr = *(struct in_addr*)(spm + 1);
		data += sizeof(struct in_addr);
		len -= sizeof(struct in_addr);
		break;

/* insert IPv6, etc here */

	default:
		printf ("unsupported afi");
		return -1;
	}

	return 0;
}

static gboolean
pgm_print_spm (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("SPM: ");

	if (len < PGM_MIN_SPM_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_spm* spm = (struct pgm_spm*)data;

	printf ("sqn %lu trail %lu lead %lu nla-afi %u ",
		(gulong)g_ntohl(spm->spm_sqn),
		(gulong)g_ntohl(spm->spm_trail),
		(gulong)g_ntohl(spm->spm_lead),
		g_ntohs(spm->spm_nla_afi));	/* address family indicator */

/* path nla */
	data += sizeof(struct pgm_spm);
	len -= sizeof(struct pgm_spm);

	switch (g_ntohs(spm->spm_nla_afi)) {
	case AFI_IP:
		printf (inet_ntoa(*(struct in_addr*)(spm + 1)));
		data += sizeof(struct in_addr);
		len -= sizeof(struct in_addr);
		break;

/* insert IPv6, etc here */

	default:
		printf ("unsupported afi");
		return FALSE;
	}

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
		pgm_print_options (data, len) > 0 )
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

#define PGM_MIN_POLL_SIZE	( \
				sizeof(struct pgm_poll) + \
				sizeof(struct in_addr) + \
				sizeof(gint32) + \
				( sizeof(char) * 4 ) + \
				sizeof(gint32) \
				)

static gboolean
pgm_print_poll (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("POLL: ");

	if (len < PGM_MIN_POLL_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_poll* poll = (struct pgm_poll*)data;

	printf ("sqn %lu round %u sub-type %u nla-afi %u ",
		(gulong)g_ntohl(poll->poll_sqn),
		g_ntohs(poll->poll_round),
		g_ntohs(poll->poll_s_type),
		g_ntohs(poll->poll_nla_afi));	/* address family indicator */

/* path nla */
	data += sizeof(struct pgm_poll);
	len -= sizeof(struct pgm_poll);

	switch (g_ntohs(poll->poll_nla_afi)) {
	case AFI_IP:
		printf (inet_ntoa(*(struct in_addr*)data));
		data += sizeof(struct in_addr);
		len -= sizeof(struct in_addr);
		break;

/* insert IPv6, etc here */

	default:
		printf ("unknown");
		break;
	}

	if (len < (sizeof(gint32) + ( sizeof(char) * 4 )) + sizeof(gint32)) {
		puts ("bad length");
		return FALSE;
	}

/* back-off interval in microseconds */
	guint32 bo_ivl = g_ntohl(*(guint32*)data);
	data += sizeof(bo_ivl);
	len -= sizeof(bo_ivl);

	printf (" bo_ivl %u", bo_ivl);

/* random string */
	char rand[4];
	memcpy (rand, data, sizeof(rand));
	data += sizeof(rand);
	len -= sizeof(rand);

/* matching bit-mask */
	guint32 mask = g_ntohl(*(guint32*)data);
	data += sizeof(mask);
	len -= sizeof(mask);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
		pgm_print_options (data, len) > 0 )
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

static gboolean
pgm_print_polr (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("POLR: ");

	if (len < sizeof(struct pgm_polr)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_polr* polr = (struct pgm_polr*)data;

	printf("sqn %lu round %u",
		(gulong)g_ntohl(polr->polr_sqn),
		g_ntohs(polr->polr_round));

	data += sizeof(struct pgm_polr);
	len -= sizeof(struct pgm_polr);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
		pgm_print_options (data, len) > 0 )
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

static gboolean
pgm_print_odata (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("ODATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_data* odata = (struct pgm_data*)data;

	printf ("sqn %lu trail %lu [",
		(gulong)g_ntohl(odata->data_sqn),
		(gulong)g_ntohl(odata->data_trail));

/* option extensions */
	data += sizeof(struct pgm_data);
	len -= sizeof(struct pgm_data);

	char* payload = data;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		int opt_len = pgm_print_options (data, len);
		if (opt_len < 0) {
			return FALSE;
		}
		data += opt_len;
		len -= opt_len;
	}

/* data */
	char* end = data + len;
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

static gboolean
pgm_print_rdata (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("RDATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_data* rdata = (struct pgm_data*)data;

	printf ("sqn %lu trail %lu [",
		(gulong)g_ntohl(rdata->data_sqn),
		(gulong)g_ntohl(rdata->data_trail));

/* option extensions */
	data += sizeof(struct pgm_data);
	len -= sizeof(struct pgm_data);

	char* payload = data;
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		int opt_len = pgm_print_options (data, len);
		if (opt_len < 0) {
			return FALSE;
		}
		data += opt_len;
		len -= opt_len;
	}

/* data */
	char* end = data + len;
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

#define PGM_MIN_NAK_SIZE	( \
				sizeof(struct pgm_nak) + \
				sizeof(struct in_addr) + \
				sizeof(gint16) + sizeof(gint16) + \
				sizeof(struct in_addr) \
				)

static gboolean
pgm_print_nak (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("NAK: ");

	if (len < PGM_MIN_NAK_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_nak* nak = (struct pgm_nak*)nak;
	printf ("sqn %lu src ", 
		(gulong)g_ntohl(nak->nak_sqn));

/* source nla */
	data += sizeof(struct pgm_nak);
	len -= sizeof(struct pgm_nak);
	switch (g_ntohs(nak->nak_src_nla_afi)) {
	case AFI_IP:
		printf (inet_ntoa(*(struct in_addr*)data));
		data += sizeof(struct in_addr);
		len -= sizeof(struct in_addr);
		break;

/* insert IPv6, etc here */

	default:
		printf ("unsupported afi");
		break;
	}

	printf (" grp ");

/* multicast group nla */
	guint16 grp_nla_afi = g_ntohs(*(guint16*)data);
	data += sizeof(guint16) + sizeof(guint16);
	len -= sizeof(guint16) + sizeof(guint16);

	switch (grp_nla_afi) {
	case AFI_IP:
		printf (inet_ntoa(*(struct in_addr*)data));
		data += sizeof(struct in_addr);
		len -= sizeof(struct in_addr);
		break;

/* insert IPv6, etc here */

	default:
		printf ("unsupported afi");
		break;
	}

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
		pgm_print_options (data, len) > 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* 8.3.  N-NAK
 */

static gboolean
pgm_print_nnak (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("N-NAK: ");

	if (len < sizeof(struct pgm_nak)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	struct pgm_nak* nnak = (struct pgm_nak*)nnak;

	return TRUE;
}

/* 8.3.  NCF
 */

gboolean
pgm_print_ncf (
	struct pgm_header* header,
	char* data,
	int len
	)
{
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

static gboolean
pgm_print_spmr (
	struct pgm_header* header,
	char* data,
	int len
	)
{
	printf ("SPMR: ");

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT &&
		pgm_print_options (data, len) > 0 )
	{
		return FALSE;
	}

	printf ("\n");
	return TRUE;
}

/* Parse PGM options fields
 * 
 * returns -1 on failure, or total length in octets of the option fields
 */

static int
pgm_print_options (
	char* data,
	int len
	)
{
	printf ("OPTIONS: ");

	if (len < sizeof(struct pgm_opt_length)) {
		puts ("packet truncated :(");
		return -1;
	}

	struct pgm_opt_header* opt = (struct pgm_opt_header*)data;

	if (opt->opt_length != 4) {
		puts ("bad opt_length length");
		return -1;
	}

	struct pgm_opt_length* opt_len = (struct pgm_opt_length*)data;

	printf (" total len %u", opt_len->opt_total_length);

	if (opt_len->opt_total_length < 4 || opt_len->opt_total_length > len) {
		puts ("bad total length");
		return -1;
	}

/* total length includes opt_length option */
	opt_len->opt_total_length -= sizeof(struct pgm_opt_length);
	opt = (struct pgm_opt_header*)(opt_len + 1);

/* iterate through options (max 16) */
	int count = 16;
	while (opt_len->opt_total_length && count) {
		if (opt_len->opt_total_length < 4 || opt->opt_length > opt_len->opt_total_length) {
			puts ("short on option data :o");
			return -1;
		}

		if (opt->opt_type & PGM_OPT_END) {
			printf ("OPT_END ");
		}

		printf ("OPT-%u{%u} ", opt->opt_type & PGM_OPT_MASK, opt->opt_length);

		opt_len->opt_total_length -= opt->opt_length;
		opt = (struct pgm_opt_header*)((char*)opt + opt->opt_length);

		count--;
	}

	if (!count) {
		puts ("too many options found");
		return FALSE;
	}

	return ((char*)opt - data);
}

static const char
*pgm_packet_type (
	guint8 type
	)
{
	const char* c;

	if (type <= 0x3)	c = "SPM";
	else if (type <= 0x7)	c = "DATA";
	else if (type <= 0xb)	c = "NAK";
	else if (type <= 0xf)	c = "SPMR";
	else			c = "Unknown";

	return c;
}

const char*
udpport_string (
	int port
	)
{
	static GHashTable *services = NULL;

	if (!services) {
		services = g_hash_table_new (g_int_hash, g_int_equal);
	}

	gpointer service_string = g_hash_table_lookup (services, &port);
	if (service_string != NULL) {
		return service_string;
	}

	struct servent* se = getservbyport (port, "udp");
	if (se == NULL) {
		char buf[sizeof("00000")];
		snprintf(buf, sizeof(buf), "%i", g_ntohs(port));
		service_string = g_strdup(buf);
	} else {
		service_string = g_strdup(se->s_name);
	}
	g_hash_table_insert (services, &port, service_string);
	return service_string;
}

const char*
getname (
	const struct in_addr* ap
	)
{
	static GHashTable *hosts = NULL;

	if (!hosts) {
		hosts = g_hash_table_new (g_str_hash, g_str_equal);
	}

	gpointer host_string = g_hash_table_lookup (hosts, ap);
	if (host_string != NULL) {
		return host_string;
	}

	struct hostent* he = gethostbyaddr(ap, 4, AF_INET);
	if (he == NULL) {
		struct in_addr in;
		memcpy (&in, ap, sizeof(in));
		host_string = g_strdup(inet_ntoa(in));
	} else {
		host_string = g_strdup(he->h_name);
	}
	g_hash_table_insert (hosts, (gpointer)ap, (gpointer)host_string);
	return host_string;
}

void
ip_optprint (
	const char* cp,
	int length
	)
{
	int len;

	for (; length > 0; cp += len, length -= len)
	{
		int tt = *cp;

		len = (tt == IPOPT_NOP || tt == IPOPT_EOL) ? 1 : cp[1];
		switch (tt) {
		default:	printf(" IPOPT-%d{%d}", cp[0], len); break;
		}

		if (!len) {
			puts ("invalid IP opt length");
			return;
		}
	}
}

guint16
in_cksum (
	const char* addr,
	int len,
	int csum
	)
{
	int nleft = len;
	const guint16 *w = (guint16*)addr;
	guint answer;
	int sum = csum;

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}
	if (nleft == 1)
		sum += htons(*(guchar *)w<<8);

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

guint16
pgm_cksum (
	const char* head,
	int len,
	int csum
	)
{
	guint sum = csum;
	guint16 odd_byte;

	while (len > 1) {
		sum += *(guint16*)head;
		head += 2;
		if (sum & 0x80000000)
			sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}

	if (len) {
		odd_byte = 0;
		*(guchar*)&odd_byte = *head;
		sum += odd_byte;
	}

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	if (sum == 0xffff)
		sum = ~sum;

	return ~sum;
}

/* eof */
