/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable fail fast memory allocation.
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

#ifndef __PGM_MALLOC_H__
#define __PGM_MALLOC_H__

#include <glib.h>


G_BEGIN_DECLS

gpointer pgm_malloc (gulong) G_GNUC_MALLOC;
gpointer pgm_malloc0 (gulong) G_GNUC_MALLOC;
gpointer pgm_memdup (gconstpointer, guint) G_GNUC_MALLOC;
void pgm_free (gpointer);

/* Convenience memory allocators
 */
#define pgm_new(struct_type, n_structs)		\
    ((struct_type *) pgm_malloc (((gsize) sizeof (struct_type)) * ((gsize) (n_structs))))
#define pgm_new0(struct_type, n_structs)	\
    ((struct_type *) pgm_malloc0 (((gsize) sizeof (struct_type)) * ((gsize) (n_structs))))

G_END_DECLS

#endif /* __PGM_MALLOC_H__ */
