/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * high resolution timers.
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
#ifndef __PGM_TIME_H__
#define __PGM_TIME_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

typedef uint64_t pgm_time_t;
typedef void (*pgm_time_since_epoch_func)(const pgm_time_t*const restrict, time_t*restrict);

#define pgm_to_secs(t)	((uint64_t)( (t) / 1000000UL ))
#define pgm_to_msecs(t)	((uint64_t)( (t) / 1000UL ))
#define pgm_to_usecs(t)	( (t) )
#define pgm_to_nsecs(t)	((uint64_t)( (t) * 1000UL ))

#define pgm_to_secsf(t)	 ( (double)(t) / 1000000.0 )
#define pgm_to_msecsf(t) ( (double)(t) / 1000.0 )
#define pgm_to_usecsf(t) ( (double)(t) )
#define pgm_to_nsecsf(t) ( (double)(t) * 1000.0 )

#define pgm_secs(t)	((uint64_t)( (uint64_t)(t) * 1000000UL ))
#define pgm_msecs(t)	((uint64_t)( (uint64_t)(t) * 1000UL ))
#define pgm_usecs(t)	((uint64_t)( (t) ))
#define pgm_nsecs(t)	((uint64_t)( (t) / 1000UL ))

#define PGM_TIME_FORMAT	PRIu64

extern pgm_time_since_epoch_func	pgm_time_since_epoch;

PGM_END_DECLS

#endif /* __PGM_TIME_H__ */

