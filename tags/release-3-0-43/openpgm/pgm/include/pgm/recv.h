/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * Transport receive API.
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

#ifndef __PGM_RECV_H__
#define __PGM_RECV_H__

#include <pgm/framework.h>
#include <pgm/tsi.h>
#include <pgm/transport.h>

PGM_BEGIN_DECLS

int pgm_recvmsg (pgm_transport_t* const, struct pgm_msgv_t* const, const int, size_t*, pgm_error_t**) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recvmsgv (pgm_transport_t* const, struct pgm_msgv_t* const, const size_t, const int, size_t*, pgm_error_t**) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recv (pgm_transport_t* const, void*, const size_t, const int, size_t* const, pgm_error_t**) PGM_GNUC_WARN_UNUSED_RESULT;
int pgm_recvfrom (pgm_transport_t* const, void*, const size_t, const int, size_t*, pgm_tsi_t*, pgm_error_t**) PGM_GNUC_WARN_UNUSED_RESULT;

PGM_END_DECLS

#endif /* __PGM_RECV_H__ */

