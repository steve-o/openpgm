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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_TYPES_H__
#define __PGM_TYPES_H__

#ifndef _MSC_VER
#	include <sys/param.h>
#	include <sys/types.h>
#endif
#include <pgm/macros.h>

#ifdef _WIN32
#	include <ws2tcpip.h>
#	include <winsock2.h>
#	if !defined( PGM_SA_FAMILY_T_DEFINED )
#		if defined( _MSC_VER )
#			define sa_family_t	ADDRESS_FAMILY
#		else
#			define sa_family_t	USHORT
#		endif
#		define PGM_SA_FAMILY_T_DEFINED
#	endif
#	if !defined( PGM_IN_PORT_T_DEFINED )
#		define in_port_t	uint16_t
#		define PGM_IN_PORT_T_DEFINED
#	endif
#endif

#ifdef _MSC_VER
#	if !defined( PGM_BOOL_DEFINED ) && !defined( __cplusplus )
/* type BOOL causes linkage errors with C++ applications */
#		define pgm_bool_t	unsigned char
#		define bool		pgm_bool_t
#		define PGM_BOOL_DEFINED
#	endif
#	if (_MSC_VER >= 1600)
#		include <stdint.h>
#	else
/* compatibility implementation */
#		include <pgm/winint.h>
#	endif
#	if !defined( PGM_SSIZE_T_DEFINED )
#		define ssize_t		SSIZE_T
#		define PGM_SSIZE_T_DEFINED
#	endif
#	if !defined( PGM_INLINE_DEFINED )
#		define inline		__inline
#		define PGM_INLINE_DEFINED
#	endif
#	if !defined( PGM_RESTRICT_DEFINED )
#		define restrict		__restrict
#		define PGM_RESTRICT_DEFINED
#	endif
#else
#	if (defined( __GNUC__ ) && ( __GNUC__ >= 4 )) || defined( __SUNPRO_C )
/* g++ v4 handles C99 headers without complaints */
#		include <stdbool.h>
#	elif !defined( PGM_BOOL_DEFINED ) && !defined( __cplusplus )
/* g++ v3 and other ancient compilers, should match target platform C++ bool size */
#		define pgm_bool_t	int
#		define bool		pgm_bool_t
#		define PGM_BOOL_DEFINED
#	endif
#	include <stdint.h>
#endif

#if !defined( PGM_BOOL_DEFINED )
#	define pgm_bool_t	bool
#endif

/* TBD: Older versions of Clang are reported to not include support for restrict */
#if !defined( PGM_RESTRICT_DEFINED ) && (!defined( restrict ) || (defined( __STDC_VERSION__ ) && __STDC_VERSION__ < 199901L)) && !(defined( __clang__ ) && defined( __cplusplus ))
/* C89 ANSI standard */
#	define restrict
#	define PGM_RESTRICT_DEFINED
#endif

PGM_BEGIN_DECLS

/* nc */

PGM_END_DECLS

#endif /* __PGM_TYPES_H__ */
