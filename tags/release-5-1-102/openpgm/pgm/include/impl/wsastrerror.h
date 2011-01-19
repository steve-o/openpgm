/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * Winsock Error strings.
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

#if !defined (__PGM_IMPL_FRAMEWORK_H_INSIDE__) && !defined (PGM_COMPILATION)
#	error "Only <framework.h> can be included directly."
#endif

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif
#ifndef __PGM_IMPL_WSASTRERROR_H__
#define __PGM_IMPL_WSASTRERROR_H__

#include <pgm/types.h>

PGM_BEGIN_DECLS

char* pgm_wsastrerror (const int);
char* pgm_adapter_strerror (const int);
char* pgm_win_strerror (char*, size_t, const int);

PGM_END_DECLS

#endif /* __PGM_IMPL_WSASTRERROR_H__ */
