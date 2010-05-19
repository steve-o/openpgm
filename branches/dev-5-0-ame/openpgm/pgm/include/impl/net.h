/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * network send wrapper.
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

#ifndef __PGM_IMPL_NET_H__
#define __PGM_IMPL_NET_H__

#ifndef _WIN32
#	include <sys/socket.h>
#endif
#include <pgm/framework.h>
#include <pgm/socket.h>

PGM_BEGIN_DECLS

PGM_GNUC_INTERNAL ssize_t pgm_sendto (pgm_sock_t*restrict, bool, bool, const void*restrict, size_t, const struct sockaddr*restrict, socklen_t);
PGM_GNUC_INTERNAL int pgm_set_nonblocking (int fd[2]);

PGM_END_DECLS

#endif /* __PGM_IMPL_NET_H__ */

