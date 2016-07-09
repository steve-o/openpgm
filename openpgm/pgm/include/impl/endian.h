/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Byte ordering macros.
 *
 * Copyright (c) 2016 Miru Limited.
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
#ifndef __PGM_IMPL_ENDIAN_H__
#define __PGM_IMPL_ENDIAN_H__

#ifdef _WIN32
#	define __LITTLE_ENDIAN	1234
#	define __BIG_ENDIAN		4321
#	define __BYTE_ORDER		__LITTLE_ENDIAN
#else
#	include <sys/param.h>
#endif

#if defined( BYTE_ORDER )
#	define PGM_BYTE_ORDER		BYTE_ORDER
#	define PGM_BIG_ENDIAN		BIG_ENDIAN
#	define PGM_LITTLE_ENDIAN	LITTLE_ENDIAN
#elif defined( __BYTE_ORDER )
#	define PGM_BYTE_ORDER		__BYTE_ORDER
#	define PGM_BIG_ENDIAN		__BIG_ENDIAN
#	define PGM_LITTLE_ENDIAN	__LITTLE_ENDIAN
#elif defined( __sun )
#	define PGM_LITTLE_ENDIAN	1234
#	define PGM_BIG_ENDIAN		4321
#	if defined( _BIT_FIELDS_LTOH )
#		define PGM_BYTE_ORDER		PGM_LITTLE_ENDIAN
#	elif defined( _BIT_FIELDS_HTOL )
#		define PGM_BYTE_ORDER		PGM_BIG_ENDIAN
#	else
#		error "Unknown bit field order for Sun Solaris."
#	endif
#else
#	error "BYTE_ORDER not supported."
#endif

#endif /* __PGM_IMPL_ENDIAN_H__ */
