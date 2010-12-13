/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable implementations of inet_network and inet_network6.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#       error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_INET_NETWORK_H__
#define __PGM_IMPL_INET_NETWORK_H__

#ifndef _WIN32
#	include <netinet/in.h>
#endif
#include <pgm/types.h>

PGM_BEGIN_DECLS

PGM_GNUC_INTERNAL int pgm_inet_network (const char*restrict, struct in_addr*restrict);
PGM_GNUC_INTERNAL int pgm_inet6_network (const char*restrict, struct in6_addr*restrict);
PGM_GNUC_INTERNAL int pgm_sa6_network (const char*restrict, struct sockaddr_in6*restrict);
PGM_GNUC_INTERNAL struct in_addr pgm_inet_makeaddr (uint32_t, uint32_t);

PGM_END_DECLS

#endif /* __PGM_IMPL_INET_NETWORK_H__ */
