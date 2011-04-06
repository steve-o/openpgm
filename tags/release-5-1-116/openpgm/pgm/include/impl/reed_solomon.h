/*
 * Reed-Solomon forward error correction based on Vandermonde matrices
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_REED_SOLOMON_H__
#define __PGM_IMPL_REED_SOLOMON_H__

typedef struct pgm_rs_t pgm_rs_t;

#include <pgm/types.h>
#include <impl/galois.h>

PGM_BEGIN_DECLS

struct pgm_rs_t {
	uint8_t		n, k;		/* RS(n, k) */
	pgm_gf8_t*	GM;
	pgm_gf8_t*	RM;
};

#define PGM_RS_DEFAULT_N	255

PGM_GNUC_INTERNAL void pgm_rs_create (pgm_rs_t*, const uint8_t, const uint8_t);
PGM_GNUC_INTERNAL void pgm_rs_destroy (pgm_rs_t*);
PGM_GNUC_INTERNAL void pgm_rs_encode (pgm_rs_t*restrict, const pgm_gf8_t**restrict, const uint8_t, pgm_gf8_t*restrict, const uint16_t);
PGM_GNUC_INTERNAL void pgm_rs_decode_parity_inline (pgm_rs_t*restrict, pgm_gf8_t**restrict, const uint8_t*restrict, const uint16_t);
PGM_GNUC_INTERNAL void pgm_rs_decode_parity_appended (pgm_rs_t*restrict, pgm_gf8_t**restrict, const uint8_t*restrict, const uint16_t);

PGM_END_DECLS

#endif /* __PGM_IMPL_REED_SOLOMON_H__ */
