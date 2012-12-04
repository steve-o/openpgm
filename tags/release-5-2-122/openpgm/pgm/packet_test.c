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
#include <ctype.h>
#include <stdio.h>
#ifndef _WIN32
#	include <sys/socket.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#endif
#include <impl/i18n.h>
#include <impl/framework.h>
#include <impl/packet_test.h>


//#define PACKET_DEBUG


static bool pgm_print_spm (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_poll (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_polr (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_odata (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_rdata (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_nak (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_nnak (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_ncf (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_spmr (const struct pgm_header* const, const void*, const size_t);
static bool pgm_print_ack (const struct pgm_header* const, const void*, const size_t);
static ssize_t pgm_print_options (const void*, size_t);

PGM_GNUC_INTERNAL
bool
pgm_print_packet (
	const void*	data,
	size_t		len
	)
{
/* pre-conditions */
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

/* minimum size should be IP header plus PGM header */
	if (len < (sizeof(struct pgm_ip) + sizeof(struct pgm_header))) 
	{
		printf ("Packet size too small: %" PRIzu " bytes, expecting at least %" PRIzu " bytes.\n",
			len, sizeof(struct pgm_ip) + sizeof(struct pgm_header));
		return FALSE;
	}

/* decode IP header */
	const struct pgm_ip* ip = (const struct pgm_ip*)data;
	if (ip->ip_v != 4) 				/* IP version, 4 or 6 */
	{
		puts ("not IP4 packet :/");		/* v6 not currently handled */
		return FALSE;
	}
	printf ("IP ");

	const size_t ip_header_length = ip->ip_hl * 4;		/* IP header length in 32bit octets */
	if (ip_header_length < sizeof(struct pgm_ip)) 
	{
		puts ("bad IP header length :(");
		return FALSE;
	}

	size_t packet_length = ntohs(ip->ip_len);	/* total packet length */

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

	const uint16_t offset = ntohs(ip->ip_off);

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
#define IP_RDF		0x8000
#define IP_DF		0x4000
#define IP_MF		0x2000
#define IP_OFFMASK	0x1fff

	printf (", id %u, offset %u, flags [%s%s]",
		ntohs(ip->ip_id),
		(offset & 0x1fff) * 8,
		((offset & IP_DF) ? "DF" : ""),
		((offset & IP_MF) ? "+" : ""));
	printf (", length %" PRIzu "", packet_length);

/* IP options */
	if ((ip_header_length - sizeof(struct pgm_ip)) > 0) {
		printf (", options (");
		pgm_ipopt_print((const void*)(ip + 1), ip_header_length - sizeof(struct pgm_ip));
		printf (" )");
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
	const uint16_t ip_sum = pgm_inet_checksum(data, (uint16_t)packet_length, 0);
	if (ip_sum != 0) {
		const uint16_t encoded_ip_sum = ntohs(ip->ip_sum);
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
	const struct pgm_header* pgm_header = (const struct pgm_header*)((const char*)data + ip_header_length);
	const size_t pgm_length = packet_length - ip_header_length;

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
		ntohs(pgm_header->pgm_tsdu_length));

	if (pgm_header->pgm_checksum)
	{
#if 0
		const uint16_t encoded_pgm_sum = pgm_header->pgm_checksum;
/* requires modification of data buffer */
		pgm_header->pgm_checksum = 0;
		const uint16_t pgm_sum = pgm_csum_fold (pgm_csum_partial((const char*)pgm_header, pgm_length, 0));
		if (pgm_sum != encoded_pgm_sum) {
			printf ("PGM checksum incorrect, packet %x calculated %x  :(\n", encoded_pgm_sum, pgm_sum);
			return FALSE;
		}
#endif
	} else {
		puts ("No PGM checksum :O");
	}

/* now decode PGM packet types */
	const void* pgm_data = pgm_header + 1;
	const size_t pgm_data_length = pgm_length - sizeof(pgm_header);		/* can equal zero for SPMR's */

	bool err = FALSE;
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
	case PGM_ACK:	err = pgm_print_ack (pgm_header, pgm_data, pgm_data_length); break;
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

static
bool
pgm_print_spm (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("SPM: ");

	if (len < PGM_MIN_SPM_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_spm * spm  = (const struct pgm_spm *)data;
	const struct pgm_spm6* spm6 = (const struct pgm_spm6*)data;
	const uint16_t spm_nla_afi = ntohs (spm->spm_nla_afi);

	printf ("sqn %" PRIu32 " trail %" PRIu32 "lu lead %" PRIu32 "lu nla-afi %u ",
		ntohl(spm->spm_sqn),
		ntohl(spm->spm_trail),
		ntohl(spm->spm_lead),
		spm_nla_afi);	/* address family indicator */

	char s[INET6_ADDRSTRLEN];
	const void* pgm_opt;
	size_t pgm_opt_len;
	switch (spm_nla_afi) {
	case AFI_IP:
		pgm_inet_ntop (AF_INET, &spm->spm_nla, s, sizeof(s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_spm);
		pgm_opt_len = len - sizeof(struct pgm_spm);
		break;

	case AFI_IP6:
		if (len < sizeof (struct pgm_spm6)) {
			puts ("packet truncated :(");
			return FALSE;
		}

		pgm_inet_ntop (AF_INET6, &spm6->spm6_nla, s, sizeof(s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_spm6);
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

static
bool
pgm_print_poll (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("POLL: ");

	if (len < PGM_MIN_POLL_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_poll * poll4 = (const struct pgm_poll *)data;
	const struct pgm_poll6* poll6 = (const struct pgm_poll6*)data;
	const uint16_t poll_nla_afi = ntohs (poll4->poll_nla_afi);

	printf ("sqn %" PRIu32 " round %u sub-type %u nla-afi %u ",
		ntohl(poll4->poll_sqn),
		ntohs(poll4->poll_round),
		ntohs(poll4->poll_s_type),
		poll_nla_afi);	/* address family indicator */

	char s[INET6_ADDRSTRLEN];
	const void* pgm_opt;
	size_t pgm_opt_len;
	switch (poll_nla_afi) {
	case AFI_IP:
		pgm_inet_ntop (AF_INET, &poll4->poll_nla, s, sizeof(s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_poll);
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

		pgm_inet_ntop (AF_INET6, &poll6->poll6_nla, s, sizeof (s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_poll6);
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

static
bool
pgm_print_polr (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("POLR: ");

	if (len < sizeof(struct pgm_polr)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_polr* polr = (const struct pgm_polr*)data;

	printf("sqn %" PRIu32 " round %u",
		ntohl(polr->polr_sqn),
		ntohs(polr->polr_round));

	const void* pgm_opt = (const uint8_t*)data + sizeof(struct pgm_polr);
	size_t pgm_opt_len = len - sizeof(struct pgm_polr);

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
bool
pgm_print_odata (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("ODATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_data* odata = (const struct pgm_data*)data;

	printf ("sqn %" PRIu32 " trail %" PRIu32 " [",
		ntohl(odata->data_sqn),
		ntohl(odata->data_trail));

/* option extensions */
	const void* pgm_opt = (const uint8_t*)data + sizeof(struct pgm_data);
	size_t pgm_opt_len = len - sizeof(struct pgm_data);
	const char* payload = pgm_opt;

	if (header->pgm_options & PGM_OPT_PRESENT) {
		const ssize_t opt_len = pgm_print_options (pgm_opt, pgm_opt_len);
		if (opt_len < 0)
			return FALSE;
		payload	+= opt_len;
	}

/* data */
	const char* end = payload + ntohs (header->pgm_tsdu_length);
	while (payload < end) {
		if (isprint (*payload))
			putchar (*payload);
		else
			putchar ('.');
		payload++;
	}

	printf ("]\n");
	return TRUE;
}

/* 8.2.  Repair Data
 */

static
bool
pgm_print_rdata (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("RDATA: ");

	if (len < sizeof(struct pgm_data)) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_data* rdata = (const struct pgm_data*)data;

	printf ("sqn %" PRIu32 " trail %" PRIu32 " [",
		ntohl (rdata->data_sqn),
		ntohl (rdata->data_trail));

/* option extensions */
	const void* pgm_opt = (const uint8_t*)data + sizeof(struct pgm_data);
	size_t pgm_opt_len = len - sizeof(struct pgm_data);
	const char* payload = pgm_opt;

	if (header->pgm_options & PGM_OPT_PRESENT) {
		const ssize_t opt_len = pgm_print_options (pgm_opt, pgm_opt_len);
		if (opt_len < 0)
			return FALSE;
		payload	+= opt_len;
	}

/* data */
	const char* end = payload + ntohs (header->pgm_tsdu_length);
	while (payload < end) {
		if (isprint (*payload))
			putchar (*payload);
		else
			putchar ('.');
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

static
bool
pgm_print_nak (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("NAK: ");

	if (len < PGM_MIN_NAK_SIZE) {
		puts ("packet truncated :(");
		return FALSE;
	}

	const struct pgm_nak * nak  = (const struct pgm_nak *)data;
	const struct pgm_nak6* nak6 = (const struct pgm_nak6*)data;
	const uint16_t nak_src_nla_afi = ntohs (nak->nak_src_nla_afi);

	printf ("sqn %" PRIu32 " src ", 
		ntohl(nak->nak_sqn));

	char s[INET6_ADDRSTRLEN];
	const void* pgm_opt;
	size_t pgm_opt_len;

/* source nla */
	switch (nak_src_nla_afi) {
	case AFI_IP: {
		const uint16_t nak_grp_nla_afi = ntohs (nak->nak_grp_nla_afi);
		if (nak_src_nla_afi != nak_grp_nla_afi) {
			puts ("different source & group afi very wibbly wobbly :(");
			return FALSE;
		}

		pgm_inet_ntop (AF_INET, &nak->nak_src_nla, s, sizeof(s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_nak);
		pgm_opt_len = len - sizeof(struct pgm_nak);
		printf ("%s grp ", s);

		pgm_inet_ntop (AF_INET, &nak->nak_grp_nla, s, sizeof(s));
		printf ("%s", s);
		break;
	}

	case AFI_IP6: {
		if (len < sizeof (struct pgm_nak6)) {
			puts ("packet truncated :(");
			return FALSE;
		}

		const uint16_t nak_grp_nla_afi = ntohs (nak6->nak6_grp_nla_afi);
		if (nak_src_nla_afi != nak_grp_nla_afi) {
			puts ("different source & group afi very wibbly wobbly :(");
			return FALSE;
		}

		pgm_inet_ntop (AF_INET6, &nak6->nak6_src_nla, s, sizeof(s));
		pgm_opt = (const uint8_t*)data + sizeof(struct pgm_nak6);
		pgm_opt_len = len - sizeof(struct pgm_nak6);
		printf ("%s grp ", s);

		pgm_inet_ntop (AF_INET6, &nak6->nak6_grp_nla, s, sizeof(s));
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

static
bool
pgm_print_nnak (
	PGM_GNUC_UNUSED const struct pgm_header* const	header,
	PGM_GNUC_UNUSED const void*			data,
	const size_t					len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

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

static
bool
pgm_print_ncf (
	PGM_GNUC_UNUSED const struct pgm_header* const	header,
	PGM_GNUC_UNUSED const void*			data,
	const size_t					len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

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

static
bool
pgm_print_spmr (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

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

static
bool
pgm_print_ack (
	const struct pgm_header* const	header,
	const void*			data,
	const size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != header);
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

	printf ("ACK: ");

	const struct pgm_ack* ack = (const struct pgm_ack*)data;
	char bitmap[33];

	for (unsigned i = 31; i; i--)
		bitmap[i] = (ack->ack_bitmap & (1 << i)) ? '1' : '0';
	bitmap[32] = '\0';

	printf ("rx_max %" PRIu32 " bitmap [%s] ",
		ntohl(ack->ack_rx_max), bitmap);

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
ssize_t
pgm_print_options (
	const void*		data,
	size_t			len
	)
{
/* pre-conditions */
	pgm_assert (NULL != data);
	pgm_assert (len > 0);

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

	uint16_t opt_total_length = ntohs (opt_len->opt_total_length);
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
	unsigned count = 16;
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
		case PGM_OPT_FRAGMENT:
			printf ("OPT_FRAGMENT ");
			break;

		case PGM_OPT_NAK_LIST:
			printf ("OPT_NAK_LIST ");
			break;

		case PGM_OPT_JOIN:
			printf ("OPT_JOIN ");
			break;

		case PGM_OPT_REDIRECT:
			printf ("OPT_REDIRECT ");
			break;

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

		case PGM_OPT_CR:
			printf ("OPT_CR ");
			break;

		case PGM_OPT_CRQST:
			printf ("OPT_CRQST ");
			break;

		case PGM_OPT_PGMCC_DATA:
			printf ("OPT_PGMCC_DATA ");
			break;

		case PGM_OPT_PGMCC_FEEDBACK:
			printf ("OPT_PGMCC_FEEDBACK ");
			break;

		case PGM_OPT_NAK_BO_IVL:
			printf ("OPT_NAK_BO_IVL ");
			break;

		case PGM_OPT_NAK_BO_RNG:
			printf ("OPT_NAK_BO_RNG ");
			break;

		case PGM_OPT_NBR_UNREACH:
			printf ("OPT_NBR_UNREACH ");
			break;

		case PGM_OPT_PATH_NLA:
			printf ("OPT_PATH_NLA ");
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

	return ((const uint8_t*)opt_header - (const uint8_t*)data);
}

PGM_GNUC_INTERNAL
const char*
pgm_type_string (
	uint8_t		type
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
	case PGM_ACK:		c = "PGM_ACK"; break;
	default: c = "(unknown)"; break;
	}

	return c;
}

PGM_GNUC_INTERNAL
const char*
pgm_udpport_string (
	in_port_t		port
	)
{
	static pgm_hashtable_t *services = NULL;

	if (!services) {
		services = pgm_hashtable_new (pgm_int_hash, pgm_int_equal);
	}

	const int hash_key = port;
	void* service_string = pgm_hashtable_lookup (services, &hash_key);
	if (service_string != NULL) {
		return service_string;
	}

	struct servent* se = getservbyport (port, "udp");
	if (se == NULL) {
		char buf[sizeof("00000")];
		pgm_snprintf_s (buf, sizeof (buf), sizeof (buf), "%u", ntohs (port));
		service_string = pgm_strdup(buf);
	} else {
		service_string = pgm_strdup(se->s_name);
	}
	pgm_hashtable_insert (services, &hash_key, service_string);
	return service_string;
}

PGM_GNUC_INTERNAL
const char*
pgm_gethostbyaddr (
	const struct in_addr*	ap
	)
{
	static pgm_hashtable_t *hosts = NULL;

	if (!hosts) {
		hosts = pgm_hashtable_new (pgm_str_hash, pgm_int_equal);
	}

	const int hash_key = (int)ap->s_addr;
	void* host_string = pgm_hashtable_lookup (hosts, &hash_key);
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
	pgm_hashtable_insert (hosts, &hash_key, host_string);
	return host_string;
}

PGM_GNUC_INTERNAL
void
pgm_ipopt_print (
	const void*		ipopt,
	size_t			length
	)
{
/* pre-conditions */
	pgm_assert (NULL != ipopt);

	const char* op = ipopt;

	while (length)
	{
		char len = (*op == PGM_IPOPT_NOP || *op == PGM_IPOPT_EOL) ? 1 : op[1];
		switch (*op) {
		case PGM_IPOPT_EOL:		printf(" eol"); break;
		case PGM_IPOPT_NOP:		printf(" nop"); break;
		case PGM_IPOPT_RR:		printf(" rr"); break;	/* 1 route */
		case PGM_IPOPT_TS:		printf(" ts"); break;	/* 1 TS */
#if 0
		case PGM_IPOPT_SECURITY:	printf(" sec-level"); break;
		case PGM_IPOPT_LSRR:		printf(" lsrr"); break;	/* 1 route */
		case PGM_IPOPT_SATID:		printf(" satid"); break;
		case PGM_IPOPT_SSRR:		printf(" ssrr"); break;	/* 1 route */
#endif
		default:			printf(" %x{%d}", (int)*op, (int)len); break;
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
