/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * global session ID helper functions
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#ifndef __PGM_GSI_H__
#define __PGM_GSI_H__

#include <stdbool.h>
#include <stdint.h>

#include <glib.h>

#ifdef G_OS_UNIX
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <arpa/inet.h>
#endif

typedef struct pgm_gsi_t pgm_gsi_t;

#ifndef __PGM_ERROR_H__
#	include <pgm/error.h>
#endif


#define PGM_GSISTRLEN		(sizeof("000.000.000.000.000.000"))

struct pgm_gsi_t {
	uint8_t	identifier[6];
};

G_BEGIN_DECLS

bool pgm_gsi_create_from_hostname (pgm_gsi_t*, pgm_error_t**);
bool pgm_gsi_create_from_addr (pgm_gsi_t*, pgm_error_t**);
bool pgm_gsi_create_from_data (pgm_gsi_t*, const uint8_t*, const size_t);
bool pgm_gsi_create_from_string (pgm_gsi_t*, const char*, ssize_t);
int pgm_gsi_print_r (const pgm_gsi_t*, char*, gsize);
char* pgm_gsi_print (const pgm_gsi_t*);
bool pgm_gsi_equal (const void* restrict, const void* restrict) G_GNUC_WARN_UNUSED_RESULT;


G_END_DECLS

#endif /* __PGM_GSI_H__ */
