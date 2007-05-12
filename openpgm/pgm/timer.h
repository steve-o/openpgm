/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * high resolution timers.
 *
 * Copyright (c) 2006-2007 Miru Limited.
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

#ifndef __PGM_SN_H
#include "sn.h"
#endif


G_BEGIN_DECLS

typedef guint64 (*time_update_func)(void);
typedef void (*time_sleep_func)(guint64);

#define time_after(a,b)	    ( guint64_lt(a,b) )
#define time_before(a,b)    time_after(b,a)

#define time_after_eq(a,b)  ( guint64_gte(a,b) )
#define time_before_eq(a,b) time_after_eq(b,a)


/* micro-seconds */
extern guint64 time_now;

extern time_update_func time_update_now;
extern time_sleep_func time_sleep;

int time_init (void);
int time_destroy (void);
gboolean time_supported (void);

G_END_DECLS

#endif /* __PGM_TIME_H__ */

