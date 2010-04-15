/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * portable string manipulation functions.
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

#ifndef __PGM_STRING_H__
#define __PGM_STRING_H__

#include <stdarg.h>

#include <glib.h>


G_BEGIN_DECLS


char* pgm_strdup (const char*) G_GNUC_MALLOC;
int pgm_printf_string_upper_bound (const char*, va_list);
int pgm_vasprintf (char**, char const*, va_list args);
char* pgm_strdup_vprintf (const char*, va_list) G_GNUC_MALLOC;
char* pgm_strconcat (const char*, ...) G_GNUC_MALLOC G_GNUC_NULL_TERMINATED;
char** pgm_strsplit (const char*, const char*, gint) G_GNUC_MALLOC;
void pgm_strfreev (char**);


G_END_DECLS

#endif /* __PGM_STRING_H__ */
