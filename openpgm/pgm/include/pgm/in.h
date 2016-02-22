/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Sections 5 and 8.2 of RFC 3678: Multicast group request
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
#ifndef __PGM_IN_H__
#define __PGM_IN_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <pgm/types.h>

#if (!defined( __FreeBSD__ ) && !defined( __APPLE__ )) \
	|| (defined( __APPLE__ ) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070)
/* section 5.1 of RFC 3678: basic (delta-based) protocol-independent multicast
 * source filter APIs.
 *
 * required for OSX 10.6 and earlier, and NetBSD.
 */
struct group_req
{
	uint32_t		gr_interface;	/* interface index */
	struct sockaddr_storage	gr_group;	/* group address */
};

struct group_source_req
{
	uint32_t		gsr_interface;	/* interface index */
	struct sockaddr_storage	gsr_group;	/* group address */
	struct sockaddr_storage	gsr_source;	/* group source */
};

#endif /* section 5.1 of RFC 3678 */

/* section 8.2 of RFC 3678: protocol-independent full-state operations.
 *
 * required for OSX, FreeBSD and NetBSD.
 */
struct group_filter
{
	uint32_t		gf_interface;	/* interface index */
	struct sockaddr_storage	gf_group;	/* multicast address */
	uint32_t		gf_fmode;	/* filter mode */
	uint32_t		gf_numsrc;	/* number of sources */
	struct sockaddr_storage gf_slist[1];	/* source address */
};

PGM_BEGIN_DECLS

/* nc */

PGM_END_DECLS

#endif /* __PGM_IN_H__ */
