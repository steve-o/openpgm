/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Processor macros for cross-platform, cross-compiler froyo.
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

#pragma once
#ifndef __PGM_IMPL_PROCESSOR_H__
#define __PGM_IMPL_PROCESSOR_H__

/* Memory prefetch */
#if defined( __sun )
#	include <sun_prefetch.h>

static inline void pgm_prefetch (void *x)
{
	sun_prefetch_read_many (x);
}
static inline void pgm_prefetchw (void *x)
{
	sun_prefetch_write_many (x);
}
#elif defined(__GNUC__) && (__GNUC__ > 2) && defined(__OPTIMIZE__)
static inline void pgm_prefetch (const void *x)
{
	__builtin_prefetch (x, 0 /* read */, 0 /* no temporal locality */);
}
static inline void pgm_prefetchw (const void *x)
{
	__builtin_prefetch (x, 1 /* write */, 3 /* high temporal */);
}
#else
static inline void pgm_prefetch (PGM_GNUC_UNUSED const void *x)
{
}
static inline void pgm_prefetchw (PGM_GNUC_UNUSED const void *x)
{
}
#endif

#endif /* __PGM_IMPL_PROCESSOR_H__ */
