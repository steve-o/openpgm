/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * transport session ID helper functions
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
#ifndef __PGM_TSI_H__
#define __PGM_TSI_H__

typedef struct pgm_tsi_t pgm_tsi_t;

#include <pgm/types.h>
#include <pgm/gsi.h>

PGM_BEGIN_DECLS

/* maximum length of TSI as a string */
#define PGM_TSISTRLEN		(sizeof("000.000.000.000.000.000.00000"))
#define PGM_TSI_INIT		{ PGM_GSI_INIT, 0 }

struct pgm_tsi_t {
	pgm_gsi_t	gsi;		/* global session identifier */
	uint16_t	sport;		/* source port: a random number to help detect session re-starts */
};

PGM_STATIC_ASSERT(sizeof(struct pgm_tsi_t) == 8);

char* pgm_tsi_print (const pgm_tsi_t*) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_tsi_print_r (const pgm_tsi_t*restrict, char*restrict, size_t);
bool pgm_tsi_equal (const void*restrict, const void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_TSI_H__ */
