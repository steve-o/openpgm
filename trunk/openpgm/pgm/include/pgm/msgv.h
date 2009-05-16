/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * Vector message container
 *
 * Copyright (c) 2006-2008 Miru Limited.
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

#ifndef __PGM_MSGV_H__
#define __PGM_MSGV_H__

/* struct for scatter/gather I/O */
struct pgm_iovec {
#ifndef _WIN32
	void*		iov_base;
	size_t		iov_len;	/* length of data */
	size_t		iov_offset;	/* offset to data from iov_base */
#else
	u_long		iov_len;
	char*		iov_base;
	u_long		iov_offset;
#endif /* _WIN32 */
};

struct pgm_msgv_t {
#ifdef __PGM_TRANSPORT_H__
	const pgm_tsi_t*	msgv_tsi;
#else
	const void*		msgv_identifier;
#endif
	struct pgm_iovec*	msgv_iov;	/* scatter/gather array */
	size_t			msgv_iovlen;	/* # elements in iov */
};

typedef struct pgm_msgv_t pgm_msgv_t;


#endif /* __PGM_MSGV_H__ */
