/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM engine.
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
#ifndef __PGM_ENGINE_H__
#define __PGM_ENGINE_H__

#include <pgm/types.h>
#include <pgm/error.h>

PGM_BEGIN_DECLS

bool pgm_init (pgm_error_t**);
bool pgm_supported (void) PGM_GNUC_WARN_UNUSED_RESULT PGM_GNUC_PURE;
bool pgm_shutdown (void);
void pgm_drop_superuser (void);

PGM_END_DECLS

#endif /* __PGM_ENGINE_H__ */
