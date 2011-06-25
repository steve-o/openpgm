/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Framework collection.
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
#ifndef __PGM_IMPL_FRAMEWORK_H__
#define __PGM_IMPL_FRAMEWORK_H__

#define __PGM_IMPL_FRAMEWORK_H_INSIDE__

#include <pgm/atomic.h>
#include <pgm/error.h>
#include <pgm/gsi.h>
#include <pgm/list.h>
#include <pgm/macros.h>
#include <pgm/mem.h>
#include <pgm/messages.h>
#include <pgm/msgv.h>
#include <pgm/packet.h>
#include <pgm/skbuff.h>
#include <pgm/socket.h>
#include <pgm/time.h>
#include <pgm/tsi.h>
#include <pgm/types.h>

#include <impl/checksum.h>
#include <impl/errno.h>
#include <impl/fixed.h>
#include <impl/galois.h>
#include <impl/getifaddrs.h>
#include <impl/get_nprocs.h>
#include <impl/getnetbyname.h>
#include <impl/getnodeaddr.h>
#include <impl/getprotobyname.h>
#include <impl/hashtable.h>
#include <impl/histogram.h>
#include <impl/indextoaddr.h>
#include <impl/indextoname.h>
#include <impl/inet_network.h>
#include <impl/ip.h>
#include <impl/list.h>
#include <impl/math.h>
#include <impl/md5.h>
#include <impl/messages.h>
#include <impl/nametoindex.h>
#include <impl/notify.h>
#include <impl/processor.h>
#include <impl/queue.h>
#include <impl/rand.h>
#include <impl/rate_control.h>
#include <impl/reed_solomon.h>
#include <impl/security.h>
#include <impl/slist.h>
#include <impl/sn.h>
#include <impl/sockaddr.h>
#include <impl/string.h>
#include <impl/thread.h>
#include <impl/time.h>
#include <impl/tsi.h>
#include <impl/wsastrerror.h>

#undef __PGM_IMPL_FRAMEWORK_H_INSIDE__

#endif /* __PGM_IMPL_FRAMEWORK_H__ */
