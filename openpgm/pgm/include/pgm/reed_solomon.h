/*
 * Reed-Solomon forward error correction based on Vandermonde matrices
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#ifndef __PGM_REED_SOLOMON_H__
#define __PGM_REED_SOLOMON_H__

#include <glib.h>


struct rs_t {
	guint		n, k;		/* RS(n, k) */
	gpointer	GM;
	gpointer	RM;
};

typedef struct rs_t rs_t;


G_BEGIN_DECLS

#define PGM_RS_DEFAULT_N	255


PGM_GNUC_INTERNAL void pgm_rs_create (rs_t*, guint, guint);
PGM_GNUC_INTERNAL void pgm_rs_destroy (rs_t*);
PGM_GNUC_INTERNAL void pgm_rs_encode (rs_t*, const void**, guint, void*, gsize);
PGM_GNUC_INTERNAL void pgm_rs_decode_parity_inline (rs_t*, void**, guint*, gsize);
PGM_GNUC_INTERNAL void pgm_rs_decode_parity_appended (rs_t*, void**, guint*, gsize);

G_END_DECLS

#endif /* __PGM_REED_SOLOMON_H__ */
