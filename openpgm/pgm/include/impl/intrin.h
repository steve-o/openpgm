/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 * 
 * AMD64 MASM compatibility API.
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
#ifndef __PGM_INTRIN_H__
#define __PGM_INTRIN_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

BYTE __InterlockedExchangeAdd8 (BYTE volatile *Addend, BYTE Value);
BYTE __InterlockedIncrement8 (BYTE volatile *Addend);
SHORT __InterlockedExchangeAdd16 (SHORT volatile *Addend, SHORT Value);
SHORT __InterlockedIncrement16 (SHORT volatile *Addend);

PGM_END_DECLS

#endif /* __PGM_INTRIN_H__ */
