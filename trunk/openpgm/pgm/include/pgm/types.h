/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Cross-platform data types.
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

#ifndef __PGM_TYPES_H__
#define __PGM_TYPES_H__

#ifndef _MSC_VER
#	include <sys/param.h>
#endif
#include <pgm/macros.h>

#ifdef _WIN32
#	include <ws2tcpip.h>
#	include <winsock2.h>
#	ifdef _MSC_VER
#		define sa_family_t	ADDRESS_FAMILY
#	else
#		define sa_family_t	USHORT
#	endif
#	define in_port_t	uint16_t
#endif

#ifdef _MSC_VER
#	include <pgm/winint.h>
#	define bool		BOOL
#	define ssize_t		SSIZE_T
#	define inline		__inline
#elif !defined(__cplusplus) || (__GNUC__ >= 4)
/* g++ v4 handles C99 headers without complaints */
#	include <stdbool.h>
#	include <stdint.h>
#else
/* g++ v3 and other ancient compilers */
#	define bool		int
#	include <stdint.h>
#endif

#ifndef _MSC_VER
#	define errno_t		int
#endif

#if !defined(restrict) || (__STDC_VERSION__ < 199901L)
/* C89 ANSI standard */
#	define restrict
#endif

PGM_BEGIN_DECLS

/* nc */

PGM_END_DECLS

#endif /* __PGM_TYPES_H__ */
