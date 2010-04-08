/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * 32-bit atomic operations.
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

#ifndef __PGM_ATOMIC_H__
#define __PGM_ATOMIC_H__

#include <glib.h>


G_BEGIN_DECLS

gint32 pgm_atomic_int32_exchange_and_add (volatile gint32*, const gint32);

void pgm_atomic_int32_add (volatile gint32*, const gint32);
gint32 pgm_atomic_int32_get (const volatile gint32*);
void pgm_atomic_int32_set (volatile gint32*, const gint32);

#define pgm_atomic_int32_inc(atomic) (pgm_atomic_int32_add ((volatile gint32*)(atomic), 1))
#define pgm_atomic_int32_dec_and_test(atomic) (pgm_atomic_int32_exchange_and_add ((atomic), -1) == 1)

void pgm_atomic_init (void);
void pgm_atomic_shutdown (void);

G_END_DECLS

#endif /* __PGM_ATOMIC_H__ */
