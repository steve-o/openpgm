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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_RAND_H__
#define __PGM_IMPL_RAND_H__

typedef struct pgm_rand_t pgm_rand_t;

#include <pgm/types.h>

PGM_BEGIN_DECLS

struct pgm_rand_t {
	uint32_t	seed;
};

PGM_GNUC_INTERNAL void pgm_rand_create (pgm_rand_t*);
PGM_GNUC_INTERNAL uint32_t pgm_rand_int (pgm_rand_t*);
PGM_GNUC_INTERNAL int32_t pgm_rand_int_range (pgm_rand_t*, int32_t, int32_t);
PGM_GNUC_INTERNAL uint32_t pgm_random_int (void);
PGM_GNUC_INTERNAL int32_t pgm_random_int_range (int32_t, int32_t);

PGM_GNUC_INTERNAL void pgm_rand_init (void);
PGM_GNUC_INTERNAL void pgm_rand_shutdown (void);

PGM_END_DECLS

#endif /* __PGM_IMPL_RAND_H__ */
