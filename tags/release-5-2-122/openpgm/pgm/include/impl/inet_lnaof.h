/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Portable implementation of inet_lnaof.
 *
 * Copyright (c) 2006-2012 Miru Limited.
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
#ifndef __PGM_INET_LNAOF_H__
#define __PGM_INET_LNAOF_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

bool pgm_inet_lnaof (struct in_addr* restrict dst, const struct in_addr* restrict src, const struct in_addr* restrict netmask);
bool pgm_inet6_lnaof (struct in6_addr* restrict dst, const struct in6_addr* restrict src, const struct in6_addr* restrict netmask);

PGM_END_DECLS

#endif /* __PGM_INET_LNAOF_H__ */
