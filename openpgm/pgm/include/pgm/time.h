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

#ifndef __PGM_TIME_H__
#define __PGM_TIME_H__

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#ifndef __PGM_ERROR_H__
#	include <pgm/error.h>
#endif


typedef enum
{
        PGM_TIME_ERROR_FAILED
} pgm_time_error_e;

typedef uint64_t pgm_time_t;

G_BEGIN_DECLS

typedef pgm_time_t (*pgm_time_update_func)(void);
typedef pgm_time_t (*pgm_time_sleep_func)(uint32_t);
typedef void (*pgm_time_since_epoch_func)(pgm_time_t*, time_t*);

#define pgm_time_after(a,b)	( (a) > (b) )
#define pgm_time_before(a,b)    ( pgm_time_after((b),(a)) )

#define pgm_time_after_eq(a,b)  ( (a) >= (b) )
#define pgm_time_before_eq(a,b) ( pgm_time_after_eq((b),(a)) )

#define pgm_to_secs(t)	((pgm_time_t)( (t) / 1000000UL ))
#define pgm_to_msecs(t)	((pgm_time_t)( (t) / 1000UL ))
#define pgm_to_usecs(t)	( (t) )
#define pgm_to_nsecs(t)	((pgm_time_t)( (t) * 1000UL ))

#define pgm_to_secsf(t)	 ( (double)(t) / 1000000.0 )
#define pgm_to_msecsf(t) ( (double)(t) / 1000.0 )
#define pgm_to_usecsf(t) ( (double)(t) )
#define pgm_to_nsecsf(t) ( (double)(t) * 1000.0 )

#define pgm_secs(t)	((pgm_time_t)( (pgm_time_t)(t) * 1000000UL ))
#define pgm_msecs(t)	((pgm_time_t)( (pgm_time_t)(t) * 1000UL ))
#define pgm_usecs(t)	((pgm_time_t)( (t) ))
#define pgm_nsecs(t)	((pgm_time_t)( (t) / 1000UL ))

#define PGM_TIME_FORMAT	G_GUINT64_FORMAT

extern pgm_time_update_func pgm_time_update_now;
extern pgm_time_sleep_func pgm_time_sleep;
extern pgm_time_since_epoch_func pgm_time_since_epoch;

PGM_GNUC_INTERNAL bool pgm_time_init (pgm_error_t**) G_GNUC_WARN_UNUSED_RESULT;
PGM_GNUC_INTERNAL bool pgm_time_shutdown (void);


G_END_DECLS

#endif /* __PGM_TIME_H__ */

