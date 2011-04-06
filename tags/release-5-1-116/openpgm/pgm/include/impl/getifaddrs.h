/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable getifaddrs
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
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_GETIFADDRS_H__
#define __PGM_IMPL_GETIFADDRS_H__

#ifndef _WIN32
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <net/if.h>
#else
#	include <ws2tcpip.h>
#endif

struct pgm_ifaddrs_t;

#include <pgm/types.h>
#include <pgm/error.h>

PGM_BEGIN_DECLS

#ifndef IF_NAMESIZE
#	ifdef IFNAMSIZ
#		define IF_NAMESIZE	IFNAMSIZ
#	elif defined(MAX_INTERFACE_NAME_LEN)
#		define IF_NAMESIZE	MAX_INTERFACE_NAME_LEN
#	elif defined(_WIN32)
/* 40 for UUID, 256 for device path */
#		define IF_NAMESIZE	256
#	else
#		define IF_NAMESIZE	16
#	endif
#endif

struct pgm_ifaddrs_t
{
	struct pgm_ifaddrs_t*	ifa_next;	/* Pointer to the next structure.  */

	char*			ifa_name;	/* Name of this network interface.  */
	unsigned int		ifa_flags;	/* Flags as from SIOCGIFFLAGS ioctl.  */

#ifdef ifa_addr
#	undef ifa_addr
#endif
	struct sockaddr*	ifa_addr;	/* Network address of this interface.  */
	struct sockaddr*	ifa_netmask;	/* Netmask of this interface.  */
};

bool pgm_getifaddrs (struct pgm_ifaddrs_t**restrict, pgm_error_t**restrict);
void pgm_freeifaddrs (struct pgm_ifaddrs_t*);

PGM_END_DECLS

#endif /* __PGM_IMPL_GETIFADDRS_H__ */
