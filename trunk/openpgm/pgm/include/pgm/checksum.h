/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM checksum routines
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

#ifndef __PGM_CHECKSUM_H__
#define __PGM_CHECKSUM_H__

#include <glib.h>


G_BEGIN_DECLS

guint16 pgm_inet_checksum (const void*, int, int);

#ifdef CONFIG_CKSUM_COPY
#   define pgm_csum_partial		csum_partial
#   define pgm_csum_partial_copy	csum_partial_copy_nocheck
#   include "pgm/csum-copy.h"
#else

#   define pgm_csum_partial		pgm_csum_partial_
#   define pgm_csum_partial_copy	pgm_csum_partial_copy_

guint32 pgm_csum_partial_ (const void*, int, guint32);
guint32 pgm_csum_partial_copy_ (const void*, void*, int, guint32);

#endif

guint16 pgm_csum_fold (guint32);
guint32 pgm_csum_block_add (guint32, guint32, int);

G_END_DECLS

#endif /* __PGM_CHECKSUM_H__ */

