/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * PGM socket buffers
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
#include <impl/framework.h>
#include "pgm/skbuff.h"


void
pgm_skb_over_panic (
	const struct pgm_sk_buff_t*const skb,
	const uint16_t		len
	)
{
	pgm_fatal ("skput:over: %u put:%u",
		    skb->len, len);
	pgm_assert_not_reached();
}

void
pgm_skb_under_panic (
	const struct pgm_sk_buff_t*const skb,
	const uint16_t		len
	)
{
	pgm_fatal ("skput:under: %u put:%u",
		    skb->len, len);
	pgm_assert_not_reached();
}

#ifndef SKB_DEBUG
bool
pgm_skb_is_valid (
	PGM_GNUC_UNUSED const struct pgm_sk_buff_t*const skb
	)
{
	return TRUE;
}
#else
bool
pgm_skb_is_valid (
	const struct pgm_sk_buff_t*const skb
	)
{
	pgm_return_val_if_fail (NULL != skb, FALSE);
/* link_ */
/* socket */
	pgm_return_val_if_fail (NULL != skb->sock, FALSE);
/* tstamp */
	pgm_return_val_if_fail (skb->tstamp > 0, FALSE);
/* tsi */
/* sequence can be any value */
/* cb can be any value */
/* len can be any value */
/* zero_padded can be any value */
/* gpointers */
	pgm_return_val_if_fail (NULL != skb->head, FALSE);
	pgm_return_val_if_fail ((const char*)skb->head > (const char*)&skb->users, FALSE);
	pgm_return_val_if_fail (NULL != skb->data, FALSE);
	pgm_return_val_if_fail ((const char*)skb->data >= (const char*)skb->head, FALSE);
	pgm_return_val_if_fail (NULL != skb->tail, FALSE);
	pgm_return_val_if_fail ((const char*)skb->tail >= (const char*)skb->data, FALSE);
	pgm_return_val_if_fail (skb->len == (char*)skb->tail - (const char*)skb->data, FALSE);
	pgm_return_val_if_fail (NULL != skb->end, FALSE);
	pgm_return_val_if_fail ((const char*)skb->end >= (const char*)skb->tail, FALSE);
/* pgm_header */
	if (skb->pgm_header) {
		pgm_return_val_if_fail ((const char*)skb->pgm_header >= (const char*)skb->head, FALSE);
		pgm_return_val_if_fail ((const char*)skb->pgm_header + sizeof(struct pgm_header) <= (const char*)skb->tail, FALSE);
		pgm_return_val_if_fail (NULL != skb->pgm_data, FALSE);
		pgm_return_val_if_fail ((const char*)skb->pgm_data >= (const char*)skb->pgm_header + sizeof(struct pgm_header), FALSE);
		pgm_return_val_if_fail ((const char*)skb->pgm_data <= (const char*)skb->tail, FALSE);
		if (skb->pgm_opt_fragment) {
			pgm_return_val_if_fail ((const char*)skb->pgm_opt_fragment > (const char*)skb->pgm_data, FALSE);
			pgm_return_val_if_fail ((const char*)skb->pgm_opt_fragment + sizeof(struct pgm_opt_fragment) < (const char*)skb->tail, FALSE);
/* of_apdu_first_sqn can be any value */
/* of_frag_offset */
			pgm_return_val_if_fail (ntohl (skb->of_frag_offset) < ntohl (skb->of_apdu_len), FALSE);
/* of_apdu_len can be any value */
		}
		pgm_return_val_if_fail (PGM_ODATA == skb->pgm_header->pgm_type || PGM_RDATA == skb->pgm_header->pgm_type, FALSE);
/* FEC broken */
		pgm_return_val_if_fail (0 == (skb->pgm_header->pgm_options & PGM_OPT_PARITY), FALSE);
		pgm_return_val_if_fail (0 == (skb->pgm_header->pgm_options & PGM_OPT_VAR_PKTLEN), FALSE);
	} else {
		pgm_return_val_if_fail (NULL == skb->pgm_data, FALSE);
		pgm_return_val_if_fail (NULL == skb->pgm_opt_fragment, FALSE);
	}
/* truesize */
	pgm_return_val_if_fail (skb->truesize >= sizeof(struct pgm_sk_buff_t*) + skb->len, FALSE);
	pgm_return_val_if_fail (skb->truesize == ((const char*)skb->end - (const char*)skb), FALSE);
/* users */
	pgm_return_val_if_fail (pgm_atomic_read32 (&skb->users) > 0, FALSE);
	return TRUE;
}
#endif /* SKB_DEBUG */

/* eof */
