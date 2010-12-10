/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Supplementary printf format modifiers for size_t & ssize_t.
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
#ifndef __PGM_ZINTTYPES_H__
#define __PGM_ZINTTYPES_H__

#if !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS)

#ifdef _WIN32
#	define PRIzd		"ld"
#	define PRIzi		"li"
#	define PRIzo		"lo"
#	define PRIzu		"lu"
#	define PRIzx		"lx"
#	define PRIzX		"lX"
#else
#	define PRIzd		"zd"
#	define PRIzi		"zi"
#	define PRIzo		"zo"
#	define PRIzu		"zu"
#	define PRIzx		"zx"
#	define PRIzX		"zX"
#endif

#endif	/* !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS) */

#endif /* __PGM_ZINTTYPES_H__ */
