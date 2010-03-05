/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable weak pseudo-random generator.
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

#ifndef __PGM_RAND_H__
#define __PGM_RAND_H__

#include <glib.h>

struct pgm_rand_t {
	guint32		seed;
};

typedef struct pgm_rand_t pgm_rand_t;


G_BEGIN_DECLS

void pgm_rand_new (pgm_rand_t*);
guint32 pgm_rand_int (pgm_rand_t*);
gint32 pgm_rand_int_range (pgm_rand_t*, gint32, gint32);
guint32 pgm_random_int (void);
gint32 pgm_random_int_range (gint32, gint32);

G_END_DECLS

#endif /* __PGM_RAND_H__ */
