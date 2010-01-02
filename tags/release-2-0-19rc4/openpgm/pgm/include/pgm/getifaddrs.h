/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable getifaddrs
 *
 * Copyright (c) 2006-2009 Miru Limited.
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

#ifndef __PGM_GETIFADDRS_H__
#define __PGM_GETIFADDRS_H__

#include <glib.h>

#ifdef G_OS_UNIX
#	include <net/if.h>
#endif

#ifdef CONFIG_HAVE_GETIFADDRS
#	include <ifaddrs.h>
#else
#	define getifaddrs	pgm_getifaddrs
#	define freeifaddrs	pgm_freeifaddrs
#	define ifaddrs		pgm_ifaddrs
#endif

#ifndef IF_NAMESIZE
#	ifdef IFNAMSIZ
#		define IF_NAMESIZE	IFNAMSIZ
#	else
#		define IF_NAMESIZE	16
#	endif
#endif

struct pgm_ifaddrs
{
	struct pgm_ifaddrs*	ifa_next;	/* Pointer to the next structure.  */

	char*			ifa_name;	/* Name of this network interface.  */
	unsigned int		ifa_flags;	/* Flags as from SIOCGIFFLAGS ioctl.  */

	struct sockaddr*	ifa_addr;	/* Network address of this interface.  */
	struct sockaddr*	ifa_netmask;	/* Netmask of this interface.  */
};


G_BEGIN_DECLS

G_GNUC_INTERNAL int pgm_getifaddrs (struct pgm_ifaddrs**);
G_GNUC_INTERNAL void pgm_freeifaddrs (struct pgm_ifaddrs*);

G_END_DECLS

#endif /* __PGM_GETIFADDRS_H__ */
