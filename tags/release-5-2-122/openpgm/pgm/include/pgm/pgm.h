/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * OpenPGM, an implementation of the PGM network protocol.
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

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_H__
#define __PGM_H__

#ifdef _MSC_VER
/* library dependencies for Visual Studio application builds */
#	pragma comment (lib, "ws2_32")
#	pragma comment (lib, "iphlpapi")
#	pragma comment (lib, "winmm")
#	pragma comment (lib, "advapi32")
#endif

#include <pgm/atomic.h>
#include <pgm/engine.h>
#include <pgm/error.h>
#include <pgm/gsi.h>
#include <pgm/if.h>
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
#include <pgm/version.h>

#endif /* __PGM_H__ */
