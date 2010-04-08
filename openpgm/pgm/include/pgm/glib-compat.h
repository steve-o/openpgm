/* vim:ts=8:sts=4:sw=4:noai:noexpandtab
 * 
 * GLIB compatibility functions for older library revisions.
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

#ifndef __PGM_GLIB_COMPAT_H__
#define __PGM_GLIB_COMPAT_H__

#include <glib.h>


G_BEGIN_DECLS

#if GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 16
void g_warn_message (const char*, const char*, int, const char*, const char*);
#	define g_warn_if_fail(expr)	do { if G_LIKELY (expr) ; else \
					g_warn_message (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, #expr); } while (0)
#endif

G_END_DECLS

#endif /* __PGM_GLIB_COMPAT_H__ */
