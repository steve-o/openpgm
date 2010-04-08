/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable error reporting.
 *
 * Copyright (c) 2010 Miru Limited.
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

#ifndef __PGM_ERROR_H__
#define __PGM_ERROR_H__

#include <stdarg.h>

#include <glib.h>


G_BEGIN_DECLS

typedef enum
{
	PGM_GSI_ERROR		= 219,
	PGM_IF_ERROR,
	PGM_PACKET_ERROR,
	PGM_RECV_ERROR,
	PGM_TIME_ERROR,
	PGM_TRANSPORT_ERROR,
	PGM_ENGINE_ERROR
} pgm_error_domain_e;

struct pgm_error_t
{
	pgm_error_domain_e	domain;
	gint			code;
	gchar*			message;
};

typedef struct pgm_error_t pgm_error_t;


void pgm_error_free (pgm_error_t*);
void pgm_set_error (pgm_error_t**, pgm_error_domain_e, gint, const gchar*, ...) G_GNUC_PRINTF (4, 5);
void pgm_propagate_error (pgm_error_t**, pgm_error_t*);
void pgm_clear_error (pgm_error_t**);
void pgm_prefix_error (pgm_error_t**, const gchar*, ...) G_GNUC_PRINTF (2, 3);

G_END_DECLS

#endif /* __PGM_ERROR_H__ */
