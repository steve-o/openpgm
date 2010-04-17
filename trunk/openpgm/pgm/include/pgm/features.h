/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Compiler feature flags.
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

#if !defined (__PGM_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#       error "Only <framework.h> can be included directly."
#endif

#ifndef __PGM_FEATURES_H__
#define __PGM_FEATURES_H__

#ifdef _POSIX_C_SOURCE
#	if (_POSIX_C_SOURCE - 0) >= 200112L
#		define CONFIG_HAVE_FTIME		1
#		define CONFIG_HAVE_GETTIMEOFDAY		1
#	endif
#	if (_POSIX_C_SOURCE - 0) >= 199309L
#		define CONFIG_HAVE_CLOCK_GETTIME	1
#		define CONFIG_HAVE_NANOSLEEP		1
#	endif
#endif
#if (defined _XOPEN_SOURCE && (_XOPEN_SOURCE - 0) >= 600)
#	define CONFIG_HAVE_CLOCK_NANOSLEEP
#endif
#if (defined _BSD_SOURCE || (_XOPEN_SOURCE - 0) >= 500)
#	define CONFIG_HAVE_USLEEP
#endif

#endif /* __PGM_FEATURES_H__ */
