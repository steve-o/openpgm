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

#if !defined (__PGM_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#ifndef __PGM_ATOMIC_H__
#define __PGM_ATOMIC_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

int32_t pgm_atomic_int32_exchange_and_add (volatile int32_t*, const int32_t);

void pgm_atomic_int32_add (volatile int32_t*, const int32_t);
int32_t pgm_atomic_int32_get (const volatile int32_t*);
void pgm_atomic_int32_set (volatile int32_t*, const int32_t);

#define pgm_atomic_int32_inc(atomic) (pgm_atomic_int32_add ((volatile int32_t*)(atomic), 1))
#define pgm_atomic_int32_dec(atomic) (pgm_atomic_int32_add ((volatile int32_t*)(atomic), -1))
#define pgm_atomic_int32_dec_and_test(atomic) (pgm_atomic_int32_exchange_and_add ((atomic), -1) == 1)

void pgm_atomic_init (void);
void pgm_atomic_shutdown (void);

PGM_END_DECLS

#endif /* __PGM_ATOMIC_H__ */
