/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * PGM sequence list.
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
#ifndef __PGM_IMPL_SQN_LIST_H__
#define __PGM_IMPL_SQN_LIST_H__

struct pgm_sqn_list_t;

#include <impl/framework.h>

PGM_BEGIN_DECLS

struct pgm_sqn_list_t {
	uint8_t			len;
	uint32_t		sqn[63];	/* list of sequence numbers */
};

PGM_END_DECLS

#endif /* __PGM_IMPL_SQN_LIST_H__ */
