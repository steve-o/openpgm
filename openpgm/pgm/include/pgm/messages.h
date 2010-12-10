/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * basic message reporting.
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
#ifndef __PGM_MESSAGES_H__
#define __PGM_MESSAGES_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

/* Set bitmask of log roles in environmental variable PGM_LOG_MASK,
 * borrowed from SmartPGM.
 */
enum {
	PGM_LOG_ROLE_MEMORY		= 0x001,
	PGM_LOG_ROLE_NETWORK		= 0x002,
	PGM_LOG_ROLE_CONFIGURATION	= 0x004,
	PGM_LOG_ROLE_SESSION		= 0x010,
	PGM_LOG_ROLE_NAK		= 0x020,
	PGM_LOG_ROLE_RATE_CONTROL	= 0x040,
	PGM_LOG_ROLE_TX_WINDOW		= 0x080,
	PGM_LOG_ROLE_RX_WINDOW		= 0x100,
	PGM_LOG_ROLE_FEC		= 0x400,
	PGM_LOG_ROLE_CONGESTION_CONTROL	= 0x800
};

enum {
	PGM_LOG_LEVEL_DEBUG	= 0,
	PGM_LOG_LEVEL_TRACE	= 1,
	PGM_LOG_LEVEL_MINOR	= 2,
	PGM_LOG_LEVEL_NORMAL	= 3,
	PGM_LOG_LEVEL_WARNING	= 4,
	PGM_LOG_LEVEL_ERROR	= 5,
	PGM_LOG_LEVEL_FATAL	= 6
};

extern int	pgm_log_mask;
extern int	pgm_min_log_level;

typedef void (*pgm_log_func_t) (const int, const char*restrict, void*restrict);

pgm_log_func_t pgm_log_set_handler (pgm_log_func_t, void*);
void pgm_messages_init (void);
void pgm_messages_shutdown (void);

PGM_END_DECLS

#endif /* __PGM_MESSAGES_H__ */
