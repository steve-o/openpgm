/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * global session ID helper functions
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
#ifndef __PGM_GSI_H__
#define __PGM_GSI_H__

typedef struct pgm_gsi_t pgm_gsi_t;

#include <pgm/types.h>
#include <pgm/error.h>

PGM_BEGIN_DECLS

#define PGM_GSISTRLEN		(sizeof("000.000.000.000.000.000"))
#define PGM_GSI_INIT		{{ 0, 0, 0, 0, 0, 0 }}

struct pgm_gsi_t {
	uint8_t	identifier[6];
};

PGM_STATIC_ASSERT(sizeof(struct pgm_gsi_t) == 6);

bool pgm_gsi_create_from_hostname (pgm_gsi_t*restrict, pgm_error_t**restrict);
bool pgm_gsi_create_from_addr (pgm_gsi_t*restrict, pgm_error_t**restrict);
bool pgm_gsi_create_from_data (pgm_gsi_t*restrict, const uint8_t*restrict, const size_t);
bool pgm_gsi_create_from_string (pgm_gsi_t*restrict, const char*restrict, ssize_t);
int pgm_gsi_print_r (const pgm_gsi_t*restrict, char*restrict, const size_t);
char* pgm_gsi_print (const pgm_gsi_t*);
bool pgm_gsi_equal (const void*restrict, const void*restrict) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_GSI_H__ */
