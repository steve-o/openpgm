/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Byte swapping API for inline compilation.
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
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_BYTEORDER_H__
#define __PGM_IMPL_BYTEORDER_H__

#if defined(_MSC_VER)
#	include <ws2tcpip.h>
#elif defined (__APPLE__)
#	include <libkern/OSByteOrder.h>
#else
#	include <byteswap.h>
#endif

#include <impl/endian.h>
#include <pgm/types.h>

PGM_BEGIN_DECLS

static inline
uint16_t
pgm_byteswap16 (uint16_t x) {
#if defined(_MSC_VER)
	return _byteswap_ushort (x);
#elif defined (__APPLE__)
	return OSSwapInt16 (x);
#else
	return bswap_16 (x);
#endif
}

static inline
uint32_t
pgm_byteswap32 (uint32_t x) {
#if defined(_MSC_VER)
	return _byteswap_ulong (x);
#elif defined (__APPLE__)
	return OSSwapInt32 (x);
#else
	return bswap_32 (x);
#endif
}

static inline
uint16_t
pgm_ntohs (uint16_t x) {
#if PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	return pgm_byteswap16 (x);
#else
	return x;
#endif
}

static inline
uint32_t
pgm_ntohl (uint32_t x) {
#if PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	return pgm_byteswap32 (x);
#else
	return x;
#endif
}

static inline
uint16_t
pgm_htons (uint16_t x) {
#if PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	return pgm_byteswap16 (x);
#else
	return x;
#endif
}

static inline
uint32_t
pgm_htonl (uint32_t x) {
#if PGM_BYTE_ORDER == PGM_LITTLE_ENDIAN
	return pgm_byteswap32 (x);
#else
	return x;
#endif
}

PGM_END_DECLS

#endif /* __PGM_IMPL_BYTEORDER_H__ */
