/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Sections 5 and 8.2 of RFC 3768: Multicast group request
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

#pragma once
#ifndef __PGM_IN_H__
#define __PGM_IN_H__

#include <sys/types.h>
#include <sys/socket.h>

/* sections 5 and 8.2 of RFC 3768: Multicast group request */
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

PGM_BEGIN_DECLS

/* nc */

PGM_END_DECLS

#endif /* __PGM_IN_H__ */
